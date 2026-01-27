#include "signalk_notes_opencpn_pi.h"
#include "tpConfigDialog.h"
#include "tpSignalKNotes.h"
#include "ocpn_plugin.h"

#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/button.h>
#include <wx/notebook.h>
#include <wx/filename.h>
#include <wx/dir.h>
#include <wx/bmpbndl.h>
#include <wx/timer.h>
#include <wx/artprov.h>

BEGIN_EVENT_TABLE(tpConfigDialog, wxDialog)
EVT_BUTTON(wxID_OK, tpConfigDialog::OnOK)
EVT_BUTTON(wxID_CANCEL, tpConfigDialog::OnCancel)
END_EVENT_TABLE()

tpConfigDialog::tpConfigDialog(signalk_notes_opencpn_pi* parent,
                               wxWindow* winparent)
    : wxDialog(winparent, wxID_ANY, _("SignalK Notes Konfiguration"),
               wxDefaultPosition, wxSize(700, 600),  // ← Größer für Icon-Grid
               wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER) {
  m_parent = parent;

  // Icon-Ordner bestimmen
  m_iconDir = m_parent->GetPluginIconDir();

  CreateControls();
  LoadPluginIcons();  // Icons FRÜH laden, bevor UpdateIconMappings aufgerufen
                      // wird
}

void tpConfigDialog::CreateControls() {
  wxBoxSizer* mainSizer = new wxBoxSizer(wxVERTICAL);

  // --- Sichtbare Icons ---
  m_countLabel =
      new wxStaticText(this, wxID_ANY, _("Icons im Kartenausschnitt: 0"));
  mainSizer->Add(m_countLabel, 0, wxALL | wxEXPAND, 10);
  UpdateVisibleCount(m_parent->GetVisibleNoteCount());

  // --- Hinweis ---
  m_infoLabel = new wxStaticText(
      this, wxID_ANY,
      _("Hinweis: Provider werden beim Bewegen der Karte entdeckt."));
  m_infoLabel->SetForegroundColour(*wxBLUE);
  mainSizer->Add(m_infoLabel, 0, wxALL | wxEXPAND, 5);

  // --- Notebook ---
  wxNotebook* notebook = new wxNotebook(this, wxID_ANY);

  // ========== Provider-Tab ==========
  wxPanel* providerPanel = new wxPanel(notebook);
  wxBoxSizer* providerSizer = new wxBoxSizer(wxVERTICAL);

  providerSizer->Add(
      new wxStaticText(providerPanel, wxID_ANY,
                       _("Wählen Sie die anzuzeigenden Provider:")),
      0, wxALL, 5);

  m_providerList = new wxCheckListBox(providerPanel, wxID_ANY);
  providerSizer->Add(m_providerList, 1, wxALL | wxEXPAND, 5);

  // === Auth-Status Bereich (NUR Status-Anzeige) ===
  wxBoxSizer* authStatusSizer = new wxBoxSizer(wxHORIZONTAL);

  m_authStatusIcon = new wxStaticBitmap(providerPanel, wxID_ANY, wxNullBitmap);
  authStatusSizer->Add(m_authStatusIcon, 0, wxALIGN_CENTER_VERTICAL | wxALL, 5);

  m_authStatusLabel =
      new wxStaticText(providerPanel, wxID_ANY, _("SignalK Verbindung"));
  authStatusSizer->Add(m_authStatusLabel, 0, wxALIGN_CENTER_VERTICAL | wxALL,
                       5);

  providerSizer->Add(authStatusSizer, 0, wxALL, 5);

  providerPanel->SetSizer(providerSizer);
  notebook->AddPage(providerPanel, _("Provider"));

  // ========== Icon-Zuordnung ==========
  wxPanel* iconPanel = new wxPanel(notebook);
  wxBoxSizer* iconPanelSizer = new wxBoxSizer(wxVERTICAL);

  iconPanelSizer->Add(
      new wxStaticText(iconPanel, wxID_ANY,
                       _("Ordnen Sie SignalK-Icons Plugin-Icons zu:")),
      0, wxALL, 5);

  m_iconMappingPanel =
      new wxScrolledWindow(iconPanel, wxID_ANY, wxDefaultPosition,
                           wxDefaultSize, wxVSCROLL | wxBORDER_SUNKEN);
  m_iconMappingPanel->SetScrollRate(0, 20);

  m_iconMappingSizer = new wxFlexGridSizer(2, 10, 10);
  m_iconMappingSizer->AddGrowableCol(1, 1);

  m_iconMappingPanel->SetSizer(m_iconMappingSizer);
  iconPanelSizer->Add(m_iconMappingPanel, 1, wxALL | wxEXPAND, 5);

  iconPanel->SetSizer(iconPanelSizer);
  notebook->AddPage(iconPanel, _("Icon-Zuordnung"));

  mainSizer->Add(notebook, 1, wxALL | wxEXPAND, 5);

  // ========== BUTTON-BEREICH AM UNTEREN RAND ==========
  wxBoxSizer* buttonSizer = new wxBoxSizer(wxHORIZONTAL);

  // Auth-Buttons (links)
  m_authButton = new wxButton(this, wxID_ANY, _("SignalK Authentifizierung"));
  m_authButton->Bind(wxEVT_BUTTON, &tpConfigDialog::OnAuthButtonClick, this);
  buttonSizer->Add(m_authButton, 0, wxALL, 5);

  m_cancelAuthButton =
      new wxButton(this, wxID_ANY, _("Authentifizierung abbrechen"));
  m_cancelAuthButton->Bind(wxEVT_BUTTON, &tpConfigDialog::OnCancelAuthRequest,
                           this);
  m_cancelAuthButton->Hide();
  buttonSizer->Add(m_cancelAuthButton, 0, wxALL, 5);

  // Platz zwischen Auth-Buttons und OK/Abbrechen
  buttonSizer->AddStretchSpacer();

  // OK/Abbrechen (rechts)
  buttonSizer->Add(new wxButton(this, wxID_OK, _("OK")), 0, wxALL, 5);
  buttonSizer->Add(new wxButton(this, wxID_CANCEL, _("Abbrechen")), 0, wxALL,
                   5);

  mainSizer->Add(buttonSizer, 0, wxEXPAND | wxALL, 5);

  // --- Timer ---
  m_authCheckTimer = new wxTimer(this);
  Bind(wxEVT_TIMER, &tpConfigDialog::OnAuthCheckTimer, this);

  SetSizer(mainSizer);

  // Auth-UI initialisieren
  InitializeAuthUI();

  Layout();
}
void tpConfigDialog::LoadPluginIcons() {
  m_pluginIcons.Clear();
  m_pluginIconBitmaps.clear();

  wxDir dir(m_iconDir);
  if (!dir.IsOpened()) {
    wxLogMessage("SignalK Notes Config: Icon directory not found: %s",
                 m_iconDir);
    return;
  }

  wxString filename;
  bool cont = dir.GetFirst(&filename, "*.svg", wxDIR_FILES);

  while (cont) {
    wxFileName fn(filename);
    wxString name = fn.GetName();  // ohne .svg

    m_pluginIcons.Add(name);

    wxString fullPath = m_iconDir + filename;
    wxBitmap bmp = LoadSvgBitmap(fullPath, 24);

    if (bmp.IsOk()) {
      m_pluginIconBitmaps[name] = bmp;
    }

    cont = dir.GetNext(&filename);
  }

  // wxLogMessage("SignalK Notes Config: Loaded %zu plugin
  // icons",m_pluginIcons.GetCount());
}

wxBitmap tpConfigDialog::LoadSvgBitmap(const wxString& path, int size) {
  wxBitmapBundle bundle = wxBitmapBundle::FromSVGFile(path, wxSize(size, size));
  if (bundle.IsOk()) return bundle.GetBitmap(wxSize(size, size));

  return wxBitmap(size, size);  // Fallback: leeres Bitmap
}

void tpConfigDialog::UpdateCount(int count) {
  m_countLabel->SetLabel(
      wxString::Format(_("Icons im Kartenausschnitt: %d"), count));
}

void tpConfigDialog::UpdateProviders(const std::set<wxString>& providers) {
  // Bestehende Provider-Liste NICHT löschen!
  // Nur neue hinzufügen

  for (const wxString& provider : providers) {
    // Prüfen ob Provider schon in Liste ist
    if (m_providerList->FindString(provider) == wxNOT_FOUND) {
      int index = m_providerList->Append(provider);
      // Standardmäßig aktiviert
      m_providerList->Check(index, true);
    }
  }

  // Info-Label ausblenden wenn Provider da sind
  if (m_providerList->GetCount() > 0) {
    m_infoLabel->Hide();
  }

  Layout();
}

void tpConfigDialog::UpdateIconMappings(const std::set<wxString>& skIcons) {
  // Für jedes neue SignalK-Icon eine Zeile hinzufügen
  for (const wxString& skIconName : skIcons) {
    // Prüfen ob schon vorhanden
    if (m_iconCombos.find(skIconName) != m_iconCombos.end()) {
      continue;  // Schon da
    }

    // Label: SignalK Icon-Name
    wxStaticText* label =
        new wxStaticText(m_iconMappingPanel, wxID_ANY, skIconName);
    m_iconMappingSizer->Add(label, 0, wxALIGN_CENTER_VERTICAL | wxALL, 5);

    // Dropdown: Alle Plugin-Icons
    wxBitmapComboBox* combo = new wxBitmapComboBox(
        m_iconMappingPanel, wxID_ANY, "", wxDefaultPosition, wxSize(300, -1), 0,
        nullptr, wxCB_READONLY);

    m_pluginIcons.Sort();
    // Alle Plugin-Icons zur Dropdown hinzufügen
    for (const wxString& pluginIconName : m_pluginIcons) {
      auto it = m_pluginIconBitmaps.find(pluginIconName);
      if (it != m_pluginIconBitmaps.end()) {
        combo->Append(pluginIconName, it->second);
      } else {
        combo->Append(pluginIconName);
      }
    }

    // Vorauswahl setzen (falls Mapping existiert)
    auto mapIt = m_currentIconMappings.find(skIconName);
    if (mapIt != m_currentIconMappings.end()) {
      // Mapping existiert - extrahiere Dateinamen aus Pfad
      wxFileName fn(mapIt->second);
      wxString iconName = fn.GetName();  // ohne .svg

      int idx = combo->FindString(iconName);
      if (idx != wxNOT_FOUND) {
        combo->SetSelection(idx);
      }
    } else {
      // Auto-Mapping: skIconName.svg
      int idx = combo->FindString(skIconName);
      if (idx != wxNOT_FOUND) {
        combo->SetSelection(idx);
      } else {
        // Fallback: notice-to-mariners
        idx = combo->FindString(wxT("notice-to-mariners"));
        if (idx != wxNOT_FOUND) {
          combo->SetSelection(idx);
        }
      }
    }

    wxString selectedIconName = combo->GetStringSelection();
    if (!selectedIconName.IsEmpty()) {
      wxFileName iconPath(m_iconDir);
      iconPath.SetFullName(selectedIconName + ".svg");
      m_currentIconMappings[skIconName] = iconPath.GetFullPath();
    }

    // Event-Handler binden
    combo->Bind(wxEVT_COMBOBOX, &tpConfigDialog::OnIconMappingChanged, this);

    m_iconMappingSizer->Add(combo, 1, wxEXPAND | wxALL, 5);

    // In Map speichern
    m_iconCombos[skIconName] = combo;
  }

  // Layout aktualisieren
  m_iconMappingPanel->FitInside();
  m_iconMappingPanel->Layout();
  Layout();
}

void tpConfigDialog::LoadSettings(
    const std::map<wxString, bool>& providers,
    const std::map<wxString, wxString>& iconMappings) {
  // Provider aus Settings in Liste laden
  m_providerList->Clear();

  for (const auto& pair : providers) {
    int index = m_providerList->Append(pair.first);
    m_providerList->Check(index, pair.second);
    m_enabledProviders.insert(pair.first);
  }

  if (providers.empty()) {
    m_infoLabel->SetLabel(
        _("Hinweis: Noch keine Provider konfiguriert. Bewegen Sie die Karte, "
          "um Provider zu entdecken."));
    m_infoLabel->Show();
  } else {
    m_infoLabel->Hide();
  }

  // Icon-Mappings laden (VOLLSTÄNDIGE Pfade)
  m_currentIconMappings = iconMappings;

  Layout();
}

std::map<wxString, wxString> tpConfigDialog::GetIconMappings() const {
  std::map<wxString, wxString> mappings;

  // Alle Combos durchgehen
  for (const auto& pair : m_iconCombos) {
    const wxString& skIconName = pair.first;
    wxBitmapComboBox* combo = pair.second;

    wxString selectedIconName = combo->GetStringSelection();

    if (!selectedIconName.IsEmpty()) {
      // Vollständigen Pfad bauen
      wxFileName iconPath(m_iconDir);
      iconPath.SetFullName(selectedIconName + wxT(".svg"));

      mappings[skIconName] = iconPath.GetFullPath();
    }
  }

  return mappings;
}

std::map<wxString, bool> tpConfigDialog::GetProviderSettings() const {
  std::map<wxString, bool> settings;

  for (unsigned int i = 0; i < m_providerList->GetCount(); i++) {
    settings[m_providerList->GetString(i)] = m_providerList->IsChecked(i);
  }

  return settings;
}

void tpConfigDialog::SaveProviderSettings() {
  m_enabledProviders.clear();

  for (unsigned int i = 0; i < m_providerList->GetCount(); i++) {
    if (m_providerList->IsChecked(i))
      m_enabledProviders.insert(m_providerList->GetString(i));
  }
}

void tpConfigDialog::OnOK(wxCommandEvent& event) {
  SaveProviderSettings();

  // Icon-Update triggern
  if (m_parent && m_parent->m_pSignalKNotesManager &&
      m_parent->m_lastViewPortValid) {
    m_parent->m_pSignalKNotesManager->UpdateDisplayedIcons(
        m_parent->m_lastViewPort.clat, m_parent->m_lastViewPort.clon,
        m_parent->CalculateMaxDistance(m_parent->m_lastViewPort));
    RequestRefresh(m_parent->m_parent_window);
  }

  // Timer stoppen
  if (m_authCheckTimer && m_authCheckTimer->IsRunning()) {
    m_authCheckTimer->Stop();
  }

  // Config speichern
  m_parent->SaveConfig();

  EndModal(wxID_OK);
}

void tpConfigDialog::OnCancel(wxCommandEvent& event) {
  if (m_authCheckTimer->IsRunning()) m_authCheckTimer->Stop();
  EndModal(wxID_CANCEL);
}

void tpConfigDialog::OnIconMappingChanged(wxCommandEvent& event) {
  // Wird aufgerufen wenn User eine Zuordnung ändert
  // Mappings werden beim OK-Klick aus den Combos ausgelesen
}

void tpConfigDialog::UpdateVisibleCount(int count) {
  m_countLabel->SetLabel(
      wxString::Format("Icons im Kartenausschnitt: %d", count));
  Layout();
}

void tpConfigDialog::OnAuthButtonClick(wxCommandEvent& event) {
  if (m_parent->m_pSignalKNotesManager->RequestAuthorization()) {
    // wxLogMessage("SignalK Notes: Auth request started");
    ShowPendingState();
    m_authCheckTimer->Start(2000);
    m_parent->SaveConfig();
  } else {
    wxMessageBox(_("Fehler beim Anfordern der Authentifizierung"), _("Fehler"),
                 wxOK | wxICON_ERROR);
  }
}

void tpConfigDialog::OnAuthCheckTimer(wxTimerEvent& event) {
  // wxLogMessage("SignalK Notes: Auth check timer triggered");

  auto* mgr = m_parent->m_pSignalKNotesManager;

  // Prüfe ob Token empfangen wurde
  if (mgr->CheckAuthorizationStatus()) {
    // wxLogMessage("SignalK Notes: Token received!");
    m_authCheckTimer->Stop();
    m_parent->SaveConfig();

    ShowAuthenticatedState();

    // wxMessageBox(_("Authentifizierung erfolgreich!"), _("Erfolg"),wxOK |
    // wxICON_INFORMATION);
    return;
  }

  // Noch pending?
  if (mgr->IsAuthPending()) {
    // wxLogMessage("SignalK Notes: Still pending, continue waiting");
    return;
  }

  // Anfrage fehlgeschlagen
  // wxLogMessage("SignalK Notes: Request failed or denied");
  m_authCheckTimer->Stop();
  ShowInitialState();
}

tpConfigDialog::~tpConfigDialog() {
  if (m_authCheckTimer && m_authCheckTimer->IsRunning())
    m_authCheckTimer->Stop();
}

void tpConfigDialog::OnCancelAuthRequest(wxCommandEvent& event) {
  // wxLogMessage("SignalK Notes: User cancelled auth request");
  m_authCheckTimer->Stop();
  m_parent->m_pSignalKNotesManager->ClearAuthRequest();
  m_parent->SaveConfig();
  ShowInitialState();
}

void tpConfigDialog::InitializeAuthUI() {
  auto* mgr = m_parent->m_pSignalKNotesManager;

  wxString token = mgr->GetAuthToken();
  wxString requestHref = mgr->GetAuthRequestHref();
  bool pending = mgr->IsAuthPending();

  // wxLogMessage("SignalK Notes: InitializeAuthUI - token=%s, href=%s,
  // pending=%d",token.IsEmpty() ? "empty" : "present",requestHref.IsEmpty() ?
  // "empty" : "present", pending);

  // 1. Token vorhanden → NUR validieren wenn KEINE Anfrage läuft
  if (!token.IsEmpty() && !pending) {
    // wxLogMessage("SignalK Notes: Token present, validating...");

    // Kleine Verzögerung falls Token gerade erst empfangen wurde
    wxMilliSleep(500);

    if (mgr->ValidateToken()) {
      // wxLogMessage("SignalK Notes: Token is valid → showing authenticated
      // state");
      ShowAuthenticatedState();
      return;
    } else {
      // Token ungültig → löschen
      wxLogMessage(
          "SignalK Notes: Token INVALID → clearing and showing initial state");
      mgr->SetAuthToken("");
      mgr->ClearAuthRequest();
      m_parent->SaveConfig();
      token.Clear();
      requestHref.Clear();
      pending = false;
    }
  }

  // 2. Authentifizierung läuft
  if (pending && !requestHref.IsEmpty()) {
    // wxLogMessage("SignalK Notes: Auth pending → showing pending state");
    ShowPendingState();
    return;
  }

  // 3. Keine gültige Authentifizierung
  wxLogMessage("SignalK Notes: No valid auth → showing initial state");
  ShowInitialState();
}

void tpConfigDialog::ShowAuthenticatedState() {
  m_authStatusIcon->SetBitmap(
      wxArtProvider::GetBitmap(wxART_TICK_MARK, wxART_OTHER, wxSize(16, 16)));
  m_authStatusLabel->SetForegroundColour(*wxGREEN);

  // Beide Buttons verstecken
  m_authButton->Hide();
  m_cancelAuthButton->Hide();

  // Timer stoppen
  if (m_authCheckTimer->IsRunning()) {
    m_authCheckTimer->Stop();
  }

  Layout();
}

void tpConfigDialog::ShowPendingState() {
  m_authStatusIcon->SetBitmap(
      wxArtProvider::GetBitmap(wxART_WARNING, wxART_OTHER, wxSize(16, 16)));
  m_authStatusLabel->SetForegroundColour(*wxBLUE);

  // Auth-Button deaktiviert mit neuem Text
  m_authButton->SetLabel(_("Authentifizierung läuft..."));
  m_authButton->Enable(false);
  m_authButton->Show();

  // Abbruch-Button anzeigen
  m_cancelAuthButton->Show();
  m_cancelAuthButton->Enable(true);

  // Timer mit 2 Sekunden
  if (m_authCheckTimer->IsRunning()) {
    m_authCheckTimer->Stop();
  }
  m_authCheckTimer->Start(2000);

  Layout();
}

void tpConfigDialog::ShowInitialState() {
  // Timer stoppen
  if (m_authCheckTimer->IsRunning()) {
    m_authCheckTimer->Stop();
  }

  m_authStatusIcon->SetBitmap(
      wxArtProvider::GetBitmap(wxART_ERROR, wxART_OTHER, wxSize(16, 16)));
  m_authStatusLabel->SetForegroundColour(*wxRED);

  // Auth-Button aktiv
  m_authButton->SetLabel(_("SignalK Authentifizierung"));
  m_authButton->Enable(true);
  m_authButton->Show();

  // Abbruch-Button verstecken
  m_cancelAuthButton->Hide();

  Layout();
}