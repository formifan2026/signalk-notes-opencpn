/******************************************************************************
 * Project:   SignalK Notes Plugin for OpenCPN
 * Purpose:   Handling of SignalK note objects and data updates
 * Author:    Dirk Behrendt
 * Copyright: Copyright (c) 2024 Dirk Behrendt
 * Licence:   GPLv2
 *
 * Icon Licensing:
 *   - Some icons are derived from freeboard-sk (Apache License 2.0)
 *   - Some icons are based on OpenCPN standard icons (GPLv2)
 ******************************************************************************/
#include "ocpn_plugin.h"
#include "tpSignalKNotes.h"
#include "signalk_notes_opencpn_pi.h"
#include "tpConfigDialog.h"
#include <wx/filename.h>
#include <wx/jsonreader.h>
#include <wx/url.h>
#include <wx/mstream.h>
#include <wx/image.h>
#include <wx/log.h>
#include <wx/html/htmlwin.h>
#include <wx/protocol/http.h>
#include <wx/datetime.h>
#include <wx/artprov.h>
#include <cstring>

tpSignalKNotesManager::tpSignalKNotesManager(signalk_notes_opencpn_pi* parent) {
  m_parent = parent;
  m_serverHost = wxEmptyString;
  m_serverPort = 3000;
}

tpSignalKNotesManager::~tpSignalKNotesManager() { ClearAllIcons(); }

void tpSignalKNotesManager::SetServerDetails(const wxString& host, int port) {
  m_serverHost = host;
  m_serverPort = port;
}

bool tpSignalKNotesManager::FetchNotesForViewport(double centerLat,
                                                  double centerLon,
                                                  double maxDistance) {
  if (m_serverHost.IsEmpty()) {
    SKN_LOG(m_parent, _("Server host not configured"));
    return false;
  }

  return FetchNotesList(centerLat, centerLon, maxDistance);
}

void tpSignalKNotesManager::UpdateDisplayedIcons(double centerLat,
                                                 double centerLon,
                                                 double maxDistance) {
  // 1. Notes vom Server holen
  if (!FetchNotesForViewport(centerLat, centerLon, maxDistance)) {
    SKN_LOG(m_parent, "Failed to fetch notes");
    return;
  }

  // 2. Neue Icon-Mappings automatisch speichern
  bool newMappingsFound = false;

  for (auto& pair : m_notes) {
    SignalKNote& note = pair.second;

    if (!note.iconName.IsEmpty()) {
      if (m_iconMappings.find(note.iconName) == m_iconMappings.end()) {
        wxString iconPath = ResolveIconPath(note.iconName);
        m_iconMappings[note.iconName] = iconPath;
        newMappingsFound = true;
      }
    }
  }

  if (newMappingsFound && m_parent) {
    m_parent->SaveConfig();
  }

  // 3. Provider-Filter anwenden
  std::set<wxString> visibleNoteIds;

  for (auto& pair : m_notes) {
    SignalKNote& note = pair.second;

    bool providerEnabled = true;

    if (!note.source.IsEmpty()) {
      auto it = m_providerSettings.find(note.source);
      if (it != m_providerSettings.end()) {
        providerEnabled = it->second;
      }
    }

    if (providerEnabled) {
      visibleNoteIds.insert(note.id);
    }
  }

  // 4. Notes entfernen, die nicht mehr sichtbar sind
  for (auto it = m_displayedGUIDs.begin(); it != m_displayedGUIDs.end();) {
    if (visibleNoteIds.find(*it) == visibleNoteIds.end()) {
      // Note aus Anzeige entfernen
      auto noteIt = m_notes.find(*it);
      if (noteIt != m_notes.end()) {
        noteIt->second.isDisplayed = false;
      }
      it = m_displayedGUIDs.erase(it);
    } else {
      ++it;
    }
  }

  // 5. Neue Notes anzeigen
  for (auto& pair : m_notes) {
    SignalKNote& note = pair.second;

    // Provider deaktiviert?
    bool providerEnabled = true;
    if (!note.source.IsEmpty()) {
      auto it = m_providerSettings.find(note.source);
      if (it != m_providerSettings.end()) {
        providerEnabled = it->second;
      }
    }
    if (!providerEnabled) continue;

    // Bereits sichtbar?
    if (note.isDisplayed) continue;

    // Icon laden (Details NICHT laden!)
    if (CreateNoteIcon(note)) {
      note.isDisplayed = true;

      // ID in Liste aufnehmen
      if (std::find(m_displayedGUIDs.begin(), m_displayedGUIDs.end(),
                    note.id) == m_displayedGUIDs.end()) {
        m_displayedGUIDs.push_back(note.id);
      }
    }
  }
}

void tpSignalKNotesManager::ClearAllIcons() {
  // Keine ODraw-Icons mehr löschen – nur interne Zustände zurücksetzen
  m_displayedGUIDs.clear();

  for (auto& pair : m_notes) {
    pair.second.isDisplayed = false;
  }
}

SignalKNote* tpSignalKNotesManager::GetNoteByGUID(const wxString& guid) {
  auto it = m_notes.find(guid);
  if (it != m_notes.end()) return &it->second;

  return nullptr;
}

void tpSignalKNotesManager::OnIconClick(const wxString& guid) {
  SKN_LOG(m_parent, "OnIconClick called with guid='%s'", guid);

  SignalKNote* note = GetNoteByGUID(guid);
  if (!note) {
    SKN_LOG(m_parent, "Note with guid='%s' not found!", guid);
    return;
  }

  // Details beim Klick laden
  if (note->name.IsEmpty() || note->description.IsEmpty()) {
    if (!FetchNoteDetails(note->id, *note)) {
      SKN_LOG(m_parent, "Failed to fetch details for %s",
              note->id);
      if (note->name.IsEmpty()) note->name = note->id;
      if (note->description.IsEmpty())
        note->description = _("Details konnten nicht geladen werden.");
    }
  }

  SKN_LOG(m_parent, "Found note '%s'", note->name);

  wxDialog* dlg =
      new wxDialog(NULL, wxID_ANY, _("SignalK Note Details"), wxDefaultPosition,
                   wxSize(500, 400), wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);

  wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);

  // Titel
  wxStaticText* title = new wxStaticText(dlg, wxID_ANY, note->name);
  wxFont font = title->GetFont();
  font.SetPointSize(font.GetPointSize() + 2);
  font.SetWeight(wxFONTWEIGHT_BOLD);
  title->SetFont(font);
  sizer->Add(title, 0, wxALL | wxEXPAND, 10);

  // HTML-Fenster
  wxHtmlWindow* htmlWin = new wxHtmlWindow(dlg, wxID_ANY, wxDefaultPosition,
                                           wxDefaultSize, wxHW_SCROLLBAR_AUTO);

  htmlWin->Bind(wxEVT_HTML_LINK_CLICKED, [this](wxHtmlLinkEvent& evt) {
    wxString url = evt.GetLinkInfo().GetHref();
    SKN_LOG(m_parent, "Opening URL: %s", url);
    wxLaunchDefaultBrowser(url);
  });

  wxString htmlContent;
  htmlContent << "<html><body><font size=\"3\">" << note->description
              << "</font>";

  if (!note->url.IsEmpty()) {
    htmlContent << "<br><br>"
                << "<b>Link:</b> "
                << "<a href=\"" << note->url << "\">" << note->url << "</a>";
  }

  htmlContent << "</body></html>";

  htmlWin->SetPage(htmlContent);
  sizer->Add(htmlWin, 1, wxALL | wxEXPAND, 10);

  // Button-Sizer
  wxBoxSizer* btnSizer = new wxBoxSizer(wxHORIZONTAL);

  // Zentrieren-Button
  wxButton* centerBtn = new wxButton(dlg, wxID_ANY, _("Auf Karte zentrieren"));
  centerBtn->Bind(wxEVT_BUTTON, [note, this](wxCommandEvent&) {
    PlugIn_ViewPort vp = m_parent->m_lastViewPort;

    double lat = note->latitude;
    double lon = note->longitude;

    double scale =
        vp.view_scale_ppm;  // oder chart_scale, je nachdem was gültig ist

    // Mittelpunkt setzen
    vp.clat = lat;
    vp.clon = lon;

    // min/max neu berechnen
    double latSpan = vp.lat_max - vp.lat_min;
    double lonSpan = vp.lon_max - vp.lon_min;

    vp.lat_min = lat - latSpan / 2.0;
    vp.lat_max = lat + latSpan / 2.0;
    vp.lon_min = lon - lonSpan / 2.0;
    vp.lon_max = lon + lonSpan / 2.0;

    SKN_LOG(m_parent,
            "Centering via VP lat=%.6f lon=%.6f scale=%.6f", lat,
            lon, scale);

    JumpToPosition(lat, lon, scale);
  });
  btnSizer->Add(centerBtn, 0, wxALL, 5);

  btnSizer->AddStretchSpacer();

  wxButton* okBtn = new wxButton(dlg, wxID_OK, _("OK"));
  btnSizer->Add(okBtn, 0, wxALL, 5);

  sizer->Add(btnSizer, 0, wxALL | wxEXPAND, 5);

  dlg->SetSizer(sizer);

  dlg->ShowModal();
  dlg->Destroy();
}

bool tpSignalKNotesManager::FetchNotesList(double centerLat, double centerLon,
                                           double maxDistance) {
  // URL erzeugen
  wxString path;
  path.Printf(
      wxT("/signalk/v2/api/resources/notes?position=[%f,%f]&distance=%.0f"),
      centerLon, centerLat, maxDistance);

  // HTTP-Abfrage
  wxString response = MakeHTTPRequest(path);

  // Ergebnis ins Log schreiben
  if (response.IsEmpty()) {
    SKN_LOG(m_parent, "FetchNotesList FAILED - empty response");
    return false;
  }

  // JSON parsen
  bool ok = ParseNotesListJSON(response);

  if (!ok) {
    SKN_LOG(m_parent,
            "FetchNotesList FAILED - JSON parse error");
  }
  return ok;
}

bool tpSignalKNotesManager::FetchNoteDetails(const wxString& noteId,
                                             SignalKNote& note) {
  wxString path;
  path.Printf(wxT("/signalk/v2/api/resources/notes/%s"), noteId);

  wxString response = MakeHTTPRequest(path);
  if (response.IsEmpty()) {
    return false;
  }

  return ParseNoteDetailsJSON(response, note);
}

wxString tpSignalKNotesManager::MakeHTTPRequest(const wxString& path) {
  wxString urlStr;
  urlStr.Printf(wxT("http://%s:%d%s"), m_serverHost, m_serverPort, path);
  SKN_LOG(m_parent, _("SignalKNotes: fetching data with URL: %s"), urlStr);
  wxURL url(urlStr);
  if (!url.IsOk()) {
    SKN_LOG(m_parent, _("Invalid URL: %s"), urlStr);
    return wxEmptyString;
  }

  wxInputStream* in_stream = url.GetInputStream();
  if (!in_stream || !in_stream->IsOk()) {
    SKN_LOG(m_parent, _("Failed to connect to: %s"), urlStr);
    if (in_stream) delete in_stream;
    return wxEmptyString;
  }

  wxString response;
  wxStringOutputStream out_stream(&response);
  in_stream->Read(out_stream);

  delete in_stream;
  return response;
}

bool tpSignalKNotesManager::ParseNotesListJSON(const wxString& json) {
  wxJSONReader reader;
  wxJSONValue root;

  int errors = reader.Parse(json, &root);
  if (errors > 0) {
    SKN_LOG(m_parent, _("Failed to parse notes list JSON"));
    return false;
  }

  m_notes.clear();

  wxArrayString memberNames = root.GetMemberNames();

  for (size_t i = 0; i < memberNames.GetCount(); i++) {
    wxString noteId = memberNames[i];
    wxJSONValue noteData = root[noteId];

    SignalKNote note;
    note.id = noteId;

    if (noteData.HasMember(wxT("name"))) {
      note.name = noteData[wxT("name")].AsString();
    }

    // SOURCE AUSLESEN
    if (noteData.HasMember(wxT("$source"))) {
      note.source = noteData[wxT("$source")].AsString();
      m_discoveredProviders.insert(note.source);

      // Neue Provider standardmäßig aktivieren
      if (m_providerSettings.find(note.source) == m_providerSettings.end()) {
        m_providerSettings[note.source] = true;
      }
    }

    if (noteData.HasMember(wxT("position"))) {
      wxJSONValue pos = noteData[wxT("position")];
      if (pos.HasMember(wxT("latitude"))) {
        note.latitude = pos[wxT("latitude")].AsDouble();
      }
      if (pos.HasMember(wxT("longitude"))) {
        note.longitude = pos[wxT("longitude")].AsDouble();
      }
    }

    if (noteData.HasMember(wxT("url"))) {
      note.url = noteData[wxT("url")].AsString();
    }

    // ICON NAME AUSLESEN
    if (noteData.HasMember(wxT("properties"))) {
      wxJSONValue props = noteData[wxT("properties")];
      if (props.HasMember(wxT("skIcon"))) {
        note.iconName = props[wxT("skIcon")].AsString();
        m_discoveredIcons.insert(note.iconName);
      }
    }

    note.isDisplayed = false;
    m_notes[noteId] = note;
  }
  return true;
}

bool tpSignalKNotesManager::ParseNoteDetailsJSON(const wxString& json,
                                                 SignalKNote& note) {
  wxJSONReader reader;
  wxJSONValue root;

  int errors = reader.Parse(json, &root);
  if (errors > 0) {
    SKN_LOG(m_parent, _("Failed to parse note details JSON"));
    return false;
  }

  if (root.HasMember(wxT("name"))) {
    note.name = root[wxT("name")].AsString();
  }

  if (root.HasMember(wxT("description"))) {
    note.description = root[wxT("description")].AsString();
    // HTML-Tags NICHT mehr entfernen!
  }

  if (root.HasMember(wxT("position"))) {
    wxJSONValue pos = root[wxT("position")];
    if (pos.HasMember(wxT("latitude"))) {
      note.latitude = pos[wxT("latitude")].AsDouble();
    }
    if (pos.HasMember(wxT("longitude"))) {
      note.longitude = pos[wxT("longitude")].AsDouble();
    }
  }

  if (root.HasMember(wxT("properties"))) {
    wxJSONValue props = root[wxT("properties")];
    if (props.HasMember(wxT("skIcon"))) {
      note.iconName = props[wxT("skIcon")].AsString();
    }
  }

  return true;
}

bool tpSignalKNotesManager::DownloadIcon(const wxString& iconName,
                                         wxBitmap& bitmap) {
  // 1. Cache prüfen
  auto cacheIt = m_iconCache.find(iconName);
  if (cacheIt != m_iconCache.end()) {
    if (cacheIt->second.IsOk()) {
      bitmap = cacheIt->second;
      return true;
    }
  }

  wxString iconPath;

  // 2. Prüfen ob bereits Mapping existiert (aus Config geladen)
  auto mapIt = m_iconMappings.find(iconName);
  if (mapIt != m_iconMappings.end() && !mapIt->second.IsEmpty()) {
    // Mapping existiert - verwende es
    iconPath = mapIt->second;
  } else {
    // Kein Mapping - automatisch auflösen
    iconPath = ResolveIconPath(iconName);

    // Mapping speichern für zukünftige Verwendung
    m_iconMappings[iconName] = iconPath;
  }

  // 3. Icon laden
  wxBitmap bmp;

  if (iconPath.EndsWith(wxT(".svg"))) {
    // SVG laden (32x32 für Karte)
    wxBitmapBundle bundle =
        wxBitmapBundle::FromSVGFile(iconPath, wxSize(32, 32));
    if (bundle.IsOk()) {
      bmp = bundle.GetBitmap(wxSize(32, 32));
    }
  } else {
    // PNG/andere Formate
    bmp.LoadFile(iconPath, wxBITMAP_TYPE_ANY);
  }

  if (!bmp.IsOk()) {
    SKN_LOG(m_parent, "Failed to load icon from: %s", iconPath);
    return false;
  }

  // 4. In Cache speichern
  m_iconCache[iconName] = bmp;
  bitmap = bmp;

  return true;
}

// ----------------------------------------------------------
// Note-Icon "erzeugen" (hier: nur sicherstellen, dass Icon geladen ist)
// ----------------------------------------------------------
bool tpSignalKNotesManager::CreateNoteIcon(SignalKNote& note) {
  wxString iconName = note.iconName;
  if (iconName.IsEmpty()) {
    iconName = wxT("fallback");
  }

  wxBitmap bmp;
  if (!DownloadIcon(iconName, bmp)) {
    SKN_LOG(m_parent,
            "CreateNoteIcon - failed to load icon '%s'",
            iconName);
    return false;
  }

  return true;
}

// ----------------------------------------------------------
// Note-Icon "löschen" (nur interne Flags)
// ----------------------------------------------------------
bool tpSignalKNotesManager::DeleteNoteIcon(const wxString& guid) {
  // GUID entspricht intern note.id
  auto it = m_notes.find(guid);
  if (it == m_notes.end()) return false;

  it->second.isDisplayed = false;
  return true;
}

void tpSignalKNotesManager::GetVisibleNotes(
    std::vector<const SignalKNote*>& outNotes) {
  outNotes.clear();
  for (auto& pair : m_notes) {
    const SignalKNote& note = pair.second;
    if (note.isDisplayed) {
      outNotes.push_back(&note);
    }
  }
}

// Icon-Bitmap mit Konfiguration holen
bool tpSignalKNotesManager::GetIconBitmapForNote(const SignalKNote& note,
                                                 wxBitmap& bmp) {
  wxString skIcon = note.iconName;

  // 1. Prüfen: Gibt es ein Mapping?
  auto it = m_iconMappings.find(skIcon);
  if (it != m_iconMappings.end()) {
    wxString mappedPath = it->second;

    if (wxFileExists(mappedPath)) {
      wxBitmapBundle bundle =
          wxBitmapBundle::FromSVGFile(mappedPath, wxSize(24, 24));
      if (bundle.IsOk()) {
        wxBitmap raw = bundle.GetBitmap(wxSize(24, 24));
        bmp = m_parent->PrepareIconBitmapForGL(raw, 24);
        return true;
      }
    }
  }
/******************************************************************************
 * Project:   SignalK Notes Plugin for OpenCPN
 * Purpose:   Handling of SignalK note objects and data updates
 * Author:    Dirk Behrendt
 * Copyright: Copyright (c) 2024 Dirk Behrendt
 * Licence:   GPLv2
 *
 * Icon Licensing:
 *   - Some icons are derived from freeboard-sk (Apache License 2.0)
 *   - Some icons are based on OpenCPN standard icons (GPLv2)
 ******************************************************************************/
#include "ocpn_plugin.h"
#include "tpSignalKNotes.h"
#include "signalk_notes_opencpn_pi.h"
#include "tpConfigDialog.h"
#include <wx/filename.h>
#include <wx/jsonreader.h>
#include <wx/url.h>
#include <wx/mstream.h>
#include <wx/image.h>
#include <wx/log.h>
#include <wx/html/htmlwin.h>
#include <wx/protocol/http.h>
#include <wx/datetime.h>
#include <wx/artprov.h>
#include <cstring>

tpSignalKNotesManager::tpSignalKNotesManager(signalk_notes_opencpn_pi* parent) {
  m_parent = parent;
  m_serverHost = wxEmptyString;
  m_serverPort = 3000;
}

tpSignalKNotesManager::~tpSignalKNotesManager() { ClearAllIcons(); }

void tpSignalKNotesManager::SetServerDetails(const wxString& host, int port) {
  m_serverHost = host;
  m_serverPort = port;
}

bool tpSignalKNotesManager::FetchNotesForViewport(double centerLat,
                                                  double centerLon,
                                                  double maxDistance) {
  if (m_serverHost.IsEmpty()) {
    SKN_LOG(m_parent, _("Server host not configured"));
    return false;
  }

  return FetchNotesList(centerLat, centerLon, maxDistance);
}

void tpSignalKNotesManager::UpdateDisplayedIcons(double centerLat,
                                                 double centerLon,
                                                 double maxDistance) {
  if (!FetchNotesForViewport(centerLat, centerLon, maxDistance)) {
    SKN_LOG(m_parent, "Failed to fetch notes");
    return;
  }

  bool newMappingsFound = false;

  for (auto& pair : m_notes) {
    SignalKNote& note = pair.second;

    if (!note.iconName.IsEmpty()) {
      if (m_iconMappings.find(note.iconName) == m_iconMappings.end()) {
        wxString iconPath = ResolveIconPath(note.iconName);
        m_iconMappings[note.iconName] = iconPath;
        newMappingsFound = true;
      }
    }
  }

  if (newMappingsFound && m_parent) {
    m_parent->SaveConfig();
  }

  std::set<wxString> visibleNoteIds;

  for (auto& pair : m_notes) {
    SignalKNote& note = pair.second;

    bool providerEnabled = true;

    if (!note.source.IsEmpty()) {
      auto it = m_providerSettings.find(note.source);
      if (it != m_providerSettings.end()) {
        providerEnabled = it->second;
      }
    }

    if (providerEnabled) {
      visibleNoteIds.insert(note.id);
    }
  }

  for (auto it = m_displayedGUIDs.begin(); it != m_displayedGUIDs.end();) {
    if (visibleNoteIds.find(*it) == visibleNoteIds.end()) {
      auto noteIt = m_notes.find(*it);
      if (noteIt != m_notes.end()) {
        noteIt->second.isDisplayed = false;
      }
      it = m_displayedGUIDs.erase(it);
    } else {
      ++it;
    }
  }

  for (auto& pair : m_notes) {
    SignalKNote& note = pair.second;

    bool providerEnabled = true;
    if (!note.source.IsEmpty()) {
      auto it = m_providerSettings.find(note.source);
      if (it != m_providerSettings.end()) {
        providerEnabled = it->second;
      }
    }
    if (!providerEnabled) continue;

    if (note.isDisplayed) continue;

    if (CreateNoteIcon(note)) {
      note.isDisplayed = true;

      if (std::find(m_displayedGUIDs.begin(), m_displayedGUIDs.end(),
                    note.id) == m_displayedGUIDs.end()) {
        m_displayedGUIDs.push_back(note.id);
      }
    }
  }
}

void tpSignalKNotesManager::ClearAllIcons() {
  m_displayedGUIDs.clear();

  for (auto& pair : m_notes) {
    pair.second.isDisplayed = false;
  }
}

SignalKNote* tpSignalKNotesManager::GetNoteByGUID(const wxString& guid) {
  auto it = m_notes.find(guid);
  if (it != m_notes.end()) return &it->second;

  return nullptr;
}

void tpSignalKNotesManager::OnIconClick(const wxString& guid) {
  SKN_LOG(m_parent, "OnIconClick called with guid='%s'", guid);

  SignalKNote* note = GetNoteByGUID(guid);
  if (!note) {
    SKN_LOG(m_parent, "Note with guid='%s' not found!", guid);
    return;
  }

  if (note->name.IsEmpty() || note->description.IsEmpty()) {
    if (!FetchNoteDetails(note->id, *note)) {
      SKN_LOG(m_parent, "Failed to fetch details for %s", note->id);
      if (note->name.IsEmpty()) note->name = note->id;
      if (note->description.IsEmpty())
        note->description = _("Details konnten nicht geladen werden.");
    }
  }

  SKN_LOG(m_parent, "Found note '%s'", note->name);

  wxDialog* dlg =
      new wxDialog(NULL, wxID_ANY, _("SignalK Note Details"), wxDefaultPosition,
                   wxSize(500, 400), wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);

  wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);

  wxStaticText* title = new wxStaticText(dlg, wxID_ANY, note->name);
  wxFont font = title->GetFont();
  font.SetPointSize(font.GetPointSize() + 2);
  font.SetWeight(wxFONTWEIGHT_BOLD);
  title->SetFont(font);
  sizer->Add(title, 0, wxALL | wxEXPAND, 10);

  wxHtmlWindow* htmlWin = new wxHtmlWindow(dlg, wxID_ANY, wxDefaultPosition,
                                           wxDefaultSize, wxHW_SCROLLBAR_AUTO);

  htmlWin->Bind(wxEVT_HTML_LINK_CLICKED, [this](wxHtmlLinkEvent& evt) {
    wxString url = evt.GetLinkInfo().GetHref();
    SKN_LOG(m_parent, "Opening URL: %s", url);
    wxLaunchDefaultBrowser(url);
  });

  wxString htmlContent;
  htmlContent << "<html><body><font size=\"3\">" << note->description
              << "</font>";

  if (!note->url.IsEmpty()) {
    htmlContent << "<br><br>"
                << "<b>Link:</b> "
                << "<a href=\"" << note->url << "\">" << note->url << "</a>";
  }

  htmlContent << "</body></html>";

  htmlWin->SetPage(htmlContent);
  sizer->Add(htmlWin, 1, wxALL | wxEXPAND, 10);

  wxBoxSizer* btnSizer = new wxBoxSizer(wxHORIZONTAL);

  wxButton* centerBtn = new wxButton(dlg, wxID_ANY, _("Auf Karte zentrieren"));
  centerBtn->Bind(wxEVT_BUTTON, [note, this](wxCommandEvent&) {
    PlugIn_ViewPort vp = m_parent->m_lastViewPort;

    double lat = note->latitude;
    double lon = note->longitude;

    double scale = vp.view_scale_ppm;

    vp.clat = lat;
    vp.clon = lon;

    double latSpan = vp.lat_max - vp.lat_min;
    double lonSpan = vp.lon_max - vp.lon_min;

    vp.lat_min = lat - latSpan / 2.0;
    vp.lat_max = lat + latSpan / 2.0;
    vp.lon_min = lon - lonSpan / 2.0;
    vp.lon_max = lon + lonSpan / 2.0;

    SKN_LOG(m_parent,
            "Centering via VP lat=%.6f lon=%.6f scale=%.6f", lat, lon, scale);

    JumpToPosition(lat, lon, scale);
  });
  btnSizer->Add(centerBtn, 0, wxALL, 5);

  btnSizer->AddStretchSpacer();

  wxButton* okBtn = new wxButton(dlg, wxID_OK, _("OK"));
  btnSizer->Add(okBtn, 0, wxALL, 5);

  sizer->Add(btnSizer, 0, wxALL | wxEXPAND, 5);

  dlg->SetSizer(sizer);

  dlg->ShowModal();
  dlg->Destroy();
}

bool tpSignalKNotesManager::FetchNotesList(double centerLat, double centerLon,
                                           double maxDistance) {
  wxString path;
  path.Printf(
      wxT("/signalk/v2/api/resources/notes?position=[%f,%f]&distance=%.0f"),
      centerLon, centerLat, maxDistance);

  wxString response = MakeHTTPRequest(path);

  if (response.IsEmpty()) {
    SKN_LOG(m_parent, "FetchNotesList FAILED - empty response");
    return false;
  }

  bool ok = ParseNotesListJSON(response);

  if (!ok) {
    SKN_LOG(m_parent, "FetchNotesList FAILED - JSON parse error");
  }
  return ok;
}

bool tpSignalKNotesManager::FetchNoteDetails(const wxString& noteId,
                                             SignalKNote& note) {
  wxString path;
  path.Printf(wxT("/signalk/v2/api/resources/notes/%s"), noteId);

  wxString response = MakeHTTPRequest(path);
  if (response.IsEmpty()) {
    return false;
  }

  return ParseNoteDetailsJSON(response, note);
}

wxString tpSignalKNotesManager::MakeHTTPRequest(const wxString& path) {
  wxString urlStr;
  urlStr.Printf(wxT("http://%s:%d%s"), m_serverHost, m_serverPort, path);
  SKN_LOG(m_parent, _("SignalKNotes: fetching data with URL: %s"), urlStr);
  wxURL url(urlStr);
  if (!url.IsOk()) {
    SKN_LOG(m_parent, _("Invalid URL: %s"), urlStr);
    return wxEmptyString;
  }

  wxInputStream* in_stream = url.GetInputStream();
  if (!in_stream || !in_stream->IsOk()) {
    SKN_LOG(m_parent, _("Failed to connect to: %s"), urlStr);
    if (in_stream) delete in_stream;
    return wxEmptyString;
  }

  wxString response;
  wxStringOutputStream out_stream(&response);
  in_stream->Read(out_stream);

  delete in_stream;
  return response;
}

bool tpSignalKNotesManager::ParseNotesListJSON(const wxString& json) {
  wxJSONReader reader;
  wxJSONValue root;

  int errors = reader.Parse(json, &root);
  if (errors > 0) {
    SKN_LOG(m_parent, _("Failed to parse notes list JSON"));
    return false;
  }

  m_notes.clear();

  wxArrayString memberNames = root.GetMemberNames();

  for (size_t i = 0; i < memberNames.GetCount(); i++) {
    wxString noteId = memberNames[i];
    wxJSONValue noteData = root[noteId];

    SignalKNote note;
    note.id = noteId;

    if (noteData.HasMember(wxT("name"))) {
      note.name = noteData[wxT("name")].AsString();
    }

    if (noteData.HasMember(wxT("$source"))) {
      note.source = noteData[wxT("$source")].AsString();
      m_discoveredProviders.insert(note.source);

      if (m_providerSettings.find(note.source) == m_providerSettings.end()) {
        m_providerSettings[note.source] = true;
      }
    }

    if (noteData.HasMember(wxT("position"))) {
      wxJSONValue pos = noteData[wxT("position")];
      if (pos.HasMember(wxT("latitude"))) {
        note.latitude = pos[wxT("latitude")].AsDouble();
      }
      if (pos.HasMember(wxT("longitude"))) {
        note.longitude = pos[wxT("longitude")].AsDouble();
      }
    }

    if (noteData.HasMember(wxT("url"))) {
      note.url = noteData[wxT("url")].AsString();
    }

    if (noteData.HasMember(wxT("properties"))) {
      wxJSONValue props = noteData[wxT("properties")];
      if (props.HasMember(wxT("skIcon"))) {
        note.iconName = props[wxT("skIcon")].AsString();
        m_discoveredIcons.insert(note.iconName);
      }
    }

    note.isDisplayed = false;
    m_notes[noteId] = note;
  }
  return true;
}

bool tpSignalKNotesManager::ParseNoteDetailsJSON(const wxString& json,
                                                 SignalKNote& note) {
  wxJSONReader reader;
  wxJSONValue root;

  int errors = reader.Parse(json, &root);
  if (errors > 0) {
    SKN_LOG(m_parent, _("Failed to parse note details JSON"));
    return false;
  }

  if (root.HasMember(wxT("name"))) {
    note.name = root[wxT("name")].AsString();
  }

  if (root.HasMember(wxT("description"))) {
    note.description = root[wxT("description")].AsString();
  }

  if (root.HasMember(wxT("position"))) {
    wxJSONValue pos = root[wxT("position")];
    if (pos.HasMember(wxT("latitude"))) {
      note.latitude = pos[wxT("latitude")].AsDouble();
    }
    if (pos.HasMember(wxT("longitude"))) {
      note.longitude = pos[wxT("longitude")].AsDouble();
    }
  }

  if (root.HasMember(wxT("properties"))) {
    wxJSONValue props = root[wxT("properties")];
    if (props.HasMember(wxT("skIcon"))) {
      note.iconName = props[wxT("skIcon")].AsString();
    }
  }

  return true;
}

bool tpSignalKNotesManager::DownloadIcon(const wxString& iconName,
                                         wxBitmap& bitmap) {
  auto cacheIt = m_iconCache.find(iconName);
  if (cacheIt != m_iconCache.end()) {
    if (cacheIt->second.IsOk()) {
      bitmap = cacheIt->second;
      return true;
    }
  }

  wxString iconPath;

  auto mapIt = m_iconMappings.find(iconName);
  if (mapIt != m_iconMappings.end() && !mapIt->second.IsEmpty()) {
    iconPath = mapIt->second;
  } else {
    iconPath = ResolveIconPath(iconName);
    m_iconMappings[iconName] = iconPath;
  }

  wxBitmap bmp;

#ifdef __OCPN__ANDROID__
  if (iconPath.EndsWith(".svg")) {
    wxString pngPath = iconPath;
    pngPath.Replace(".svg", ".png");
    bmp.LoadFile(pngPath, wxBITMAP_TYPE_PNG);
  } else {
    bmp.LoadFile(iconPath, wxBITMAP_TYPE_ANY);
  }
#else
  if (iconPath.EndsWith(wxT(".svg"))) {
    wxBitmapBundle bundle =
        wxBitmapBundle::FromSVGFile(iconPath, wxSize(32, 32));
    if (bundle.IsOk()) {
      bmp = bundle.GetBitmap(wxSize(32, 32));
    }
  } else {
    bmp.LoadFile(iconPath, wxBITMAP_TYPE_ANY);
  }
#endif

  if (!bmp.IsOk()) {
    SKN_LOG(m_parent, "Failed to load icon from: %s", iconPath);
    return false;
  }

  m_iconCache[iconName] = bmp;
  bitmap = bmp;

  return true;
}

bool tpSignalKNotesManager::CreateNoteIcon(SignalKNote& note) {
  wxString iconName = note.iconName;
  if (iconName.IsEmpty()) {
    iconName = wxT("fallback");
  }

  wxBitmap bmp;
  if (!DownloadIcon(iconName, bmp)) {
    SKN_LOG(m_parent, "CreateNoteIcon - failed to load icon '%s'", iconName);
    return false;
  }

  return true;
}

bool tpSignalKNotesManager::DeleteNoteIcon(const wxString& guid) {
  auto it = m_notes.find(guid);
  if (it == m_notes.end()) return false;

  it->second.isDisplayed = false;
  return true;
}

void tpSignalKNotesManager::GetVisibleNotes(
    std::vector<const SignalKNote*>& outNotes) {
  outNotes.clear();
  for (auto& pair : m_notes) {
    const SignalKNote& note = pair.second;
    if (note.isDisplayed) {
      outNotes.push_back(&note);
    }
  }
}

bool tpSignalKNotesManager::GetIconBitmapForNote(const SignalKNote& note,
                                                 wxBitmap& bmp) {
  wxString skIcon = note.iconName;

  auto it = m_iconMappings.find(skIcon);
  if (it != m_iconMappings.end()) {
    wxString mappedPath = it->second;

    if (wxFileExists(mappedPath)) {
#ifdef __OCPN__ANDROID__
      wxBitmap raw;
      if (mappedPath.EndsWith(".svg")) {
        wxString pngPath = mappedPath;
        pngPath.Replace(".svg", ".png");
        raw.LoadFile(pngPath, wxBITMAP_TYPE_PNG);
      } else {
        raw.LoadFile(mappedPath, wxBITMAP_TYPE_ANY);
      }
      if (raw.IsOk()) {
        bmp = m_parent->PrepareIconBitmapForGL(raw, 24);
        return true;
      }
#else
      if (mappedPath.EndsWith(".svg")) {
        wxBitmapBundle bundle =
            wxBitmapBundle::FromSVGFile(mappedPath, wxSize(24, 24));
        if (bundle.IsOk()) {
          wxBitmap raw = bundle.GetBitmap(wxSize(24, 24));
          bmp = m_parent->PrepareIconBitmapForGL(raw, 24);
          return true;
        }
      } else {
        wxBitmap raw(mappedPath, wxBITMAP_TYPE_ANY);
        if (raw.IsOk()) {
          bmp = m_parent->PrepareIconBitmapForGL(raw, 24);
          return true;
        }
      }
#endif
    }
  }

  wxString pluginIcon = m_parent->GetPluginIconDir() + skIcon + ".svg";
  if (wxFileExists(pluginIcon)) {
#ifdef __OCPN__ANDROID__
    wxString pngPath = pluginIcon;
    pngPath.Replace(".svg", ".png");
    wxBitmap raw(pngPath, wxBITMAP_TYPE_PNG);
    if (raw.IsOk()) {
      bmp = m_parent->PrepareIconBitmapForGL(raw, 24);
      return true;
    }
#else
    wxBitmapBundle bundle =
        wxBitmapBundle::FromSVGFile(pluginIcon, wxSize(24, 24));
    if (bundle.IsOk()) {
      wxBitmap raw = bundle.GetBitmap(wxSize(24, 24));
      bmp = m_parent->PrepareIconBitmapForGL(raw, 24);
      return true;
    }
#endif
  }

  wxString fallback = m_parent->GetPluginIconDir() + "notice-to-mariners.svg";
  if (wxFileExists(fallback)) {
#ifdef __OCPN__ANDROID__
    wxString pngPath = fallback;
    pngPath.Replace(".svg", ".png");
    wxBitmap raw(pngPath, wxBITMAP_TYPE_PNG);
    if (raw.IsOk()) {
      bmp = m_parent->PrepareIconBitmapForGL(raw, 24);
      return true;
    }
#else
    wxBitmapBundle bundle =
        wxBitmapBundle::FromSVGFile(fallback, wxSize(24, 24));
    if (bundle.IsOk()) {
      wxBitmap raw = bundle.GetBitmap(wxSize(24, 24));
      bmp = m_parent->PrepareIconBitmapForGL(raw, 24);
      return true;
    }
#endif
  }

  return false;
}

int tpSignalKNotesManager::GetVisibleIconCount(const PlugIn_ViewPort& vp) {
  PlugIn_ViewPort vpCopy = vp;

  std::vector<const SignalKNote*> visibleNotes;
  GetVisibleNotes(visibleNotes);

  int count = 0;

  for (const SignalKNote* note : visibleNotes) {
    if (!note) continue;

    wxPoint p;
    GetCanvasPixLL(&vpCopy, &p, note->latitude, note->longitude);

    if (p.x >= 0 && p.x < vpCopy.pix_width && p.y >= 0 &&
        p.y < vpCopy.pix_height) {
      count++;
    }
  }

  return count;
}

void tpSignalKNotesManager::SetProviderSettings(
    const std::map<wxString, bool>& settings) {
  m_providerSettings = settings;
}

void tpSignalKNotesManager::SetIconMappings(
    const std::map<wxString, wxString>& mappings) {
  m_iconMappings = mappings;
}

wxString tpSignalKNotesManager::ResolveIconPath(const wxString& skIconName) {
  wxFileName fn;
  fn.SetPath(GetPluginDataDir("signalk_notes_opencpn_pi"));
  fn.AppendDir("data");
  fn.AppendDir("icons");

  auto it = m_iconMappings.find(skIconName);
  if (it != m_iconMappings.end()) {
    fn.SetName(it->second);
    fn.SetExt("svg");
    if (fn.FileExists()) {
      return fn.GetFullPath();
    }
  }

  fn.SetName(skIconName);
  fn.SetExt("svg");
  if (fn.FileExists()) {
    return fn.GetFullPath();
  }

  fn.SetName("notice-to-mariners");
  fn.SetExt("svg");
  return fn.GetFullPath();
}

wxString tpSignalKNotesManager::RenderHTMLDescription(
    const wxString& htmlText) {
  wxString result = htmlText;

  if (!result.Contains("<")) {
    return result;
  }

  return result;
}

bool tpSignalKNotesManager::RequestAuthorization() {
  wxString path = "/signalk/v1/access/requests";

  wxString body = wxString::Format(
      "{"
      "  \"clientId\": \"%s\","
      "  \"description\": \"OpenCPN SignalK Notes Plugin\""
      "}",
      m_parent->m_clientUUID);

  wxHTTP http;
  http.SetTimeout(10);
  if (!http.Connect(m_serverHost, m_serverPort)) {
    SKN_LOG(m_parent,
            "SignalK Notes Auth: Connection failed (RequestAuthorization)");
    return false;
  }

  wxMemoryBuffer postData;
  wxCharBuffer utf8 = body.ToUTF8();
  postData.AppendData(utf8.data(), utf8.length());

  http.SetPostBuffer("application/json", postData);

  wxInputStream* in = http.GetInputStream(path);

  if (!in || !in->IsOk()) {
    SKN_LOG(m_parent, "SignalK Notes Auth: POST request failed (no response)");
    delete in;
    return false;
  }

  wxString response;
  wxStringOutputStream out(&response);
  in->Read(out);
  delete in;

  wxJSONReader reader;
  wxJSONValue root;
  if (reader.Parse(response, &root) > 0) {
    SKN_LOG(m_parent, "SignalK Notes Auth: Failed to parse JSON response");
    return false;
  }

  if (root.HasMember("href")) {
    wxString href = root["href"].AsString();

    SetAuthRequestHref(href);
    m_authRequestTime = wxDateTime::Now();

    return true;
  }

  SKN_LOG(m_parent, "SignalK Notes Auth: ERROR – server did not return href");
  return false;
}

bool tpSignalKNotesManager::CheckAuthorizationStatus() {
  if (m_authRequestHref.IsEmpty()) return false;

  wxString response = MakeHTTPRequest(m_authRequestHref);

  if (response.IsEmpty()) {
    return false;
  }

  wxJSONReader reader;
  wxJSONValue root;
  if (reader.Parse(response, &root) > 0) {
    SKN_LOG(m_parent, "SignalK Notes Auth: Failed to parse JSON");
    return false;
  }

  wxString state;
  if (root.HasMember("state"))
    state = root["state"].AsString();
  else if (root.HasMember("status"))
    state = root["status"].AsString();

  if (state == "PENDING") {
    return false;
  }

  if (!root.HasMember("accessRequest")) {
    SKN_LOG(m_parent,
            "SignalK Notes Auth: No accessRequest although state !=PENDING → "
            "treating as failed");
    ClearAuthRequest();
    return false;
  }

  wxJSONValue access = root["accessRequest"];

  if (access.HasMember("permission") &&
      access["permission"].AsString() == "DENIED") {
    SKN_LOG(m_parent, "SignalK Notes Auth: Request denied by server");
    ClearAuthRequest();
    return false;
  }

  if (access.HasMember("token")) {
    m_authToken = access["token"].AsString();
    ClearAuthRequest();
    return true;
  }

  SKN_LOG(m_parent,
          "SignalK Notes Auth: Completed without token → treating as failed");
  ClearAuthRequest();
  return false;
}

bool tpSignalKNotesManager::ValidateToken() {
  if (m_authToken.IsEmpty()) {
    SKN_LOG(m_parent, "ValidateToken - token is empty");
    return false;
  }

  wxHTTP http;
  http.SetTimeout(10);

  if (!http.Connect(m_serverHost, m_serverPort)) {
    SKN_LOG(m_parent, "ValidateToken - connection failed");
    return false;
  }

  http.SetHeader("Authorization", "Bearer " + m_authToken);

  wxInputStream* in = http.GetInputStream("/plugins/");

  if (!in) {
    SKN_LOG(
        m_parent,
        "ValidateToken - GetInputStream returned NULL (likely "
        "401/403 - no admin rights)");
    return false;
  }

  if (!in->IsOk()) {
    SKN_LOG(m_parent, "ValidateToken - stream not ok (unauthorized)");
    delete in;
    return false;
  }

  int responseCode = http.GetResponse();

  wxString response;
  wxStringOutputStream out(&response);
  in->Read(out);
  delete in;

  if (responseCode == 401 || responseCode == 403) {
    SKN_LOG(m_parent,
            "Token is invalid or lacks admin rights (HTTP %d)", responseCode);
    return false;
  }

  if (responseCode == 200) {
    if (response.IsEmpty()) {
      SKN_LOG(m_parent,
              "Token validation - EMPTY response despite HTTP 200 "
              "(suspicious)");
      return false;
    }

    wxJSONReader reader;
    wxJSONValue root;

    if (reader.Parse(response, &root) == 0) {
      if (!root.IsArray()) {
        SKN_LOG(m_parent,
                "Token validation - Response is not an array "
                "(expected plugin list)");
        return false;
      }

      return true;
    } else {
      SKN_LOG(m_parent,
              "Token validation - Response is not valid JSON");
      return false;
    }
  }

  SKN_LOG(m_parent,
          "Unexpected response code %d - treating as invalid", responseCode);
  return false;
}

wxString tpSignalKNotesManager::MakeAuthenticatedHTTPRequest(
    const wxString& path) {
  wxString urlStr =
      wxString::Format("http://%s:%d%s", m_serverHost, m_serverPort, path);

  wxHTTP http;
  http.SetTimeout(10);

  if (!http.Connect(m_serverHost, m_serverPort)) {
    return wxEmptyString;
  }

  if (!m_authToken.IsEmpty()) {
    http.SetHeader("Authorization", "Bearer " + m_authToken);
  }

  wxInputStream* in = http.GetInputStream(path);
  if (!in || !in->IsOk()) {
    delete in;
    return wxEmptyString;
  }

  wxString response;
  wxStringOutputStream out(&response);
  in->Read(out);
  delete in;

  return response;
}

bool tpSignalKNotesManager::FetchInstalledPlugins(
    std::map<wxString, bool>& plugins) {
  wxString path = "/plugins/";
  wxString response = MakeAuthenticatedHTTPRequest(path);

  if (response.IsEmpty()) {
    SKN_LOG(m_parent, "Failed to fetch installed plugins");
    return false;
  }

  wxJSONReader reader;
  wxJSONValue root;
  if (reader.Parse(response, &root) > 0) {
    SKN_LOG(m_parent, "Failed to parse plugins JSON");
    return false;
  }

  if (!root.IsArray()) {
    SKN_LOG(m_parent, "Expected array in plugins response");
    return false;
  }

  plugins.clear();
  m_providerDetails.clear();

  for (int i = 0; i < root.Size(); i++) {
    wxJSONValue plugin = root[i];

    if (plugin.HasMember("id")) {
      wxString id = plugin["id"].AsString();

      ProviderDetails details;
      details.id = id;
      details.name = plugin.Get("name", id).AsString();
      details.description = plugin.Get("description", "").AsString();

      bool enabled = false;
      if (plugin.HasMember("data")) {
        wxJSONValue data = plugin["data"];
        if (data.HasMember("enabled")) {
          enabled = data["enabled"].AsBool();
        }
      }

      details.enabled = enabled;
      plugins[id] = enabled;
      m_providerDetails[id] = details;
    }
  }

  return true;
}

void tpSignalKNotesManager::CleanupDisabledProviders() {
  std::map<wxString, bool> installedPlugins;

  if (!FetchInstalledPlugins(installedPlugins)) {
    SKN_LOG(m_parent, "Cannot cleanup providers - plugin fetch failed");
    return;
  }

  std::vector<wxString> providersToRemove;

  for (const auto& providerPair : m_providerSettings) {
    wxString provider = providerPair.first;

    auto it = installedPlugins.find(provider);

    if (it == installedPlugins.end()) {
      SKN_LOG(m_parent, "Removing provider '%s' - not installed", provider);
      providersToRemove.push_back(provider);
    } else if (!it->second) {
      SKN_LOG(m_parent,
              "Removing provider '%s' - disabled in SignalK", provider);
      providersToRemove.push_back(provider);
    }
  }

  for (const wxString& provider : providersToRemove) {
    m_providerSettings.erase(provider);
    m_discoveredProviders.erase(provider);
  }

  if (!providersToRemove.empty() && m_parent) {
    m_parent->SaveConfig();
  }
}

std::vector<tpSignalKNotesManager::ProviderInfo>
tpSignalKNotesManager::GetProviderInfos() const {
  std::vector<ProviderInfo> infos;

  for (const auto& pair : m_providerDetails) {
    ProviderInfo info;
    info.id = pair.second.id;
    info.name = pair.second.name;
    info.description = pair.second.description;
    infos.push_back(info);
  }

  return infos;
}

void tpSignalKNotesManager::SetAuthToken(const wxString& token) {
  m_authToken = token;
  if (m_parent) {
    m_parent->SaveConfig();
  }
}

void tpSignalKNotesManager::SetAuthRequestHref(const wxString& href) {
  m_authRequestHref = href;
  m_authPending = !href.IsEmpty();

  if (m_parent) {
    m_parent->SaveConfig();
  }
}

void tpSignalKNotesManager::ClearAuthRequest() {
  m_authRequestHref.Clear();
  m_authPending = false;

  if (m_parent) {
    m_parent->SaveConfig();
  }
}

