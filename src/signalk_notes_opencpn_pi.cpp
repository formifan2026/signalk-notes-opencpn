/******************************************************************************
 * Project:  OpenCPN
 * Purpose:  SignalK Notes Plugin
 * Author:   Dirk (bereinigt)
 *
 ***************************************************************************
 *   GPL License
 ***************************************************************************
 */
#if defined(__WXGTK__)
#include <GL/gl.h>
#include <GL/glu.h>
#elif defined(__WXMSW__)
#include <GL/gl.h>
#include <GL/glu.h>
#elif defined(__WXOSX__)
#include <OpenGL/gl.h>
#include <OpenGL/glu.h>
#endif
#if defined(__OCPN__ANDROID__)
#include <GLES2/gl2.h>
#else
#include <GL/gl.h>
#include <GL/glu.h>
#endif

#include "version.h"
#include "tpSignalKNotes.h"

#include "ocpn_plugin.h"
#include <cmath>
#include "wx/wxprec.h"

#ifndef WX_PRECOMP
#include "wx/wx.h"
#endif

#include <wx/stdpaths.h>
#include <wx/timer.h>
#include <wx/event.h>
#include <wx/sysopt.h>
#include <wx/dir.h>
#include <wx/filefn.h>
#include <wx/msgdlg.h>
#include <wx/wfstream.h>
#include <wx/txtstrm.h>
#include <wx/aui/aui.h>
#include <wx/fileconf.h>
#include <wx/filename.h>
#include <wx/graphics.h>

#include <uuid/uuid.h>

#include "signalk_notes_opencpn_pi.h"
#include "wxWTranslateCatalog.h"

#include "tpicons.h"
#include "tpConfigDialog.h"

#include "wx/jsonwriter.h"

#ifndef DECL_EXP
#ifdef __WXMSW__
#define DECL_EXP __declspec(dllexport)
#else
#define DECL_EXP
#endif
#endif

signalk_notes_opencpn_pi* g_signalk_notes_opencpn_pi = nullptr;
wxString* g_PrivateDataDir = nullptr;

wxString* g_pLayerDir = nullptr;

PlugIn_ViewPort* g_pVP = nullptr;
PlugIn_ViewPort g_VP;

wxFont* g_pFontTitle = nullptr;
wxFont* g_pFontData = nullptr;
wxFont* g_pFontLabel = nullptr;
wxFont* g_pFontSmall = nullptr;

// Factory
extern "C" DECL_EXP opencpn_plugin* create_pi(void* ppimgr) {
  return new signalk_notes_opencpn_pi(ppimgr);
}

extern "C" DECL_EXP void destroy_pi(opencpn_plugin* p) { delete p; }

// ---------------------------------------------------------------------------------------
// Constructor / Destructor
// ---------------------------------------------------------------------------------------

signalk_notes_opencpn_pi::signalk_notes_opencpn_pi(void* ppimgr)
    : opencpn_plugin_118(ppimgr) {
  g_signalk_notes_opencpn_pi = this;

  // Plugin-Datenverzeichnis von OpenCPN ermitteln
  wxString pluginDir = GetPluginDataDir("signalk_notes_opencpn_pi");

  // Sicherstellen, dass der Pfad mit Slash endet
  if (!pluginDir.EndsWith("/")) pluginDir += "/";

  // Globale Verzeichnisse setzen
  g_PrivateDataDir = new wxString(pluginDir);

  // data/ Verzeichnis
  wxString dataDir = pluginDir + "data/";
  if (!wxDir::Exists(dataDir)) wxMkdir(dataDir);

  // icons/ Verzeichnis
  wxString iconDir = dataDir + "icons/";
  if (!wxDir::Exists(iconDir)) wxMkdir(iconDir);

  // Layers/ Verzeichnis
  wxString layerDir = pluginDir + "Layers/";
  if (!wxDir::Exists(layerDir)) wxMkdir(layerDir);

  // Globale Pointer setzen (falls noch benötigt)
  g_pLayerDir = new wxString(layerDir);

  // Plugin-Komponenten initialisieren
  m_ptpicons = new tpicons();
  m_pSignalKNotesManager = new tpSignalKNotesManager(this);
  m_lastViewPortValid = false;
}

signalk_notes_opencpn_pi::~signalk_notes_opencpn_pi() {
  delete g_PrivateDataDir;
  delete g_pLayerDir;

  if (m_pSignalKNotesManager) delete m_pSignalKNotesManager;
}

// ---------------------------------------------------------------------------
// Version / Metadata (aus version.h)
// ---------------------------------------------------------------------------

int signalk_notes_opencpn_pi::GetAPIVersionMajor() {
  return OCPN_API_VERSION_MAJOR;
}
int signalk_notes_opencpn_pi::GetAPIVersionMinor() {
  return OCPN_API_VERSION_MINOR;
}

int signalk_notes_opencpn_pi::GetPlugInVersionMajor() {
  return PLUGIN_VERSION_MAJOR;
}
int signalk_notes_opencpn_pi::GetPlugInVersionMinor() {
  return PLUGIN_VERSION_MINOR;
}
int signalk_notes_opencpn_pi::GetPlugInVersionPatch() {
  return PLUGIN_VERSION_PATCH;
}
int signalk_notes_opencpn_pi::GetPlugInVersionPost() {
  return PLUGIN_VERSION_TWEAK;
}

wxString signalk_notes_opencpn_pi::GetCommonName() {
  return _T(PLUGIN_COMMON_NAME);
}
wxString signalk_notes_opencpn_pi::GetShortDescription() {
  return _(PLUGIN_SHORT_DESCRIPTION);
}
wxString signalk_notes_opencpn_pi::GetLongDescription() {
  return _(PLUGIN_LONG_DESCRIPTION);
}

// ---------------------------------------------------------------------------------------
// Init / DeInit
// ---------------------------------------------------------------------------------------

int signalk_notes_opencpn_pi::Init(void) {
  AddLocaleCatalog(PLUGIN_CATALOG_NAME);

  m_parent_window = GetOCPNCanvasWindow();
  m_pTPConfig = GetOCPNConfigObject();

  LoadConfig();

  // ← KEINE Änderung nötig, kein Initial-Fetch!

#ifdef PLUGIN_USE_SVG
  m_signalk_notes_opencpn_button_id = InsertPlugInToolSVG(
      _("SignalK Notes"), m_ptpicons->m_s_signalk_notes_opencpn_grey_pi,
      m_ptpicons->m_s_signalk_notes_opencpn_pi,
      m_ptpicons->m_s_signalk_notes_opencpn_toggled_pi, wxITEM_CHECK,
      _("SignalK Notes"), wxS(""), nullptr, -1, 0, this);
#else
  m_signalk_notes_opencpn_button_id = InsertPlugInTool(
      _("SignalK Notes"), &m_ptpicons->m_bm_signalk_notes_opencpn_grey_pi,
      &m_ptpicons->m_bm_signalk_notes_opencpn_pi, wxITEM_CHECK,
      _("SignalK Notes"), wxS(""), nullptr, -1, 0, this);
#endif

  g_pFontTitle = GetOCPNScaledFont_PlugIn("tp_Title");
  g_pFontLabel = GetOCPNScaledFont_PlugIn("tp_Label");
  g_pFontData = GetOCPNScaledFont_PlugIn("tp_Data");
  g_pFontSmall = GetOCPNScaledFont_PlugIn("tp_Small");

  return (WANTS_CURSOR_LATLON | WANTS_TOOLBAR_CALLBACK | INSTALLS_TOOLBAR_TOOL |
          INSTALLS_TOOLBOX_PAGE | INSTALLS_CONTEXTMENU_ITEMS |
          WANTS_OVERLAY_CALLBACK | WANTS_OPENGL_OVERLAY_CALLBACK |
          WANTS_PLUGIN_MESSAGING | WANTS_LATE_INIT | WANTS_MOUSE_EVENTS |
          WANTS_KEYBOARD_EVENTS | WANTS_ONPAINT_VIEWPORT);
}

void signalk_notes_opencpn_pi::LateInit(void) {
  SendPluginMessage("SIGNALK_NOTES_OPENCPN_PI_READY_FOR_REQUESTS", "TRUE");
}

bool signalk_notes_opencpn_pi::DeInit(void) {
  if (m_pOverviewDialog) {
    m_pOverviewDialog->Destroy();
    m_pOverviewDialog = nullptr;
  }

  if (m_pTPConfig) SaveConfig();

  return true;
}

// ---------------------------------------------------------------------------------------
// Toolbar
// ---------------------------------------------------------------------------------------

void signalk_notes_opencpn_pi::OnToolbarToolCallback(int id) {
  // Token validieren BEVOR Dialog geöffnet wird
  if (!m_pSignalKNotesManager->GetAuthToken().IsEmpty()) {
    if (!m_pSignalKNotesManager->ValidateToken()) {
      wxLogMessage("SignalK Notes: Token invalid - clearing");
      m_pSignalKNotesManager->SetAuthToken("");
      m_pSignalKNotesManager->ClearAuthRequest();
      SaveConfig();
    }
  }

  // Provider-Cleanup
  if (m_pSignalKNotesManager) {
    m_pSignalKNotesManager->CleanupDisabledProviders();
  }

  if (m_pConfigDialog) {
    m_pConfigDialog->Destroy();
    m_pConfigDialog = nullptr;
  }

  m_pConfigDialog = new tpConfigDialog(this, GetOCPNCanvasWindow());

  m_pConfigDialog->UpdateProviders(
      m_pSignalKNotesManager->GetDiscoveredProviders());
  m_pConfigDialog->UpdateIconMappings(
      m_pSignalKNotesManager->GetDiscoveredIcons());
  m_pConfigDialog->LoadSettings(m_pSignalKNotesManager->GetProviderSettings(),
                                m_pSignalKNotesManager->GetIconMappings());

  if (m_pConfigDialog->ShowModal() == wxID_OK) {
    m_pSignalKNotesManager->SetProviderSettings(
        m_pConfigDialog->GetProviderSettings());
    m_pSignalKNotesManager->SetIconMappings(m_pConfigDialog->GetIconMappings());

    SaveConfig();

    if (m_lastViewPortValid) {
      m_pSignalKNotesManager->UpdateDisplayedIcons(
          m_lastViewPort.clat, m_lastViewPort.clon,
          CalculateMaxDistance(m_lastViewPort));
    }

    RequestRefresh(m_parent_window);
  }

  m_pConfigDialog->Destroy();
  m_pConfigDialog = nullptr;

  UpdateOverviewDialog();
}

void signalk_notes_opencpn_pi::ShowPreferencesDialog(wxWindow* parent) {
  if (m_pConfigDialog) {
    m_pConfigDialog->Destroy();
    m_pConfigDialog = nullptr;
  }

  m_pConfigDialog = new tpConfigDialog(this, parent);

  m_pConfigDialog->UpdateProviders(
      m_pSignalKNotesManager->GetDiscoveredProviders());
  m_pConfigDialog->UpdateIconMappings(
      m_pSignalKNotesManager->GetDiscoveredIcons());
  m_pConfigDialog->LoadSettings(m_pSignalKNotesManager->GetProviderSettings(),
                                m_pSignalKNotesManager->GetIconMappings());

  // WICHTIG: NICHT modal öffnen!
  m_pConfigDialog->Show();
}

void signalk_notes_opencpn_pi::OnToolbarToolDownCallback(int id) {}

void signalk_notes_opencpn_pi::OnToolbarToolUpCallback(int id) {
  m_ptpicons->SetScaleFactor();
}

void signalk_notes_opencpn_pi::UpdateOverviewDialog() {
  if (!m_lastViewPortValid) return;

  // 1. Notes aktualisieren
  m_pSignalKNotesManager->UpdateDisplayedIcons(
      m_lastViewPort.clat, m_lastViewPort.clon,
      m_lastViewPort.view_scale_ppm * 1000  // oder dein Radius
  );

  // 2. aktuellen Wert holen
  int count = m_pSignalKNotesManager->GetVisibleIconCount(m_lastViewPort);

  // 3. Dialog aktualisieren
  if (m_pOverviewDialog) m_pOverviewDialog->UpdateCount(count);
}

// ---------------------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------------------

bool signalk_notes_opencpn_pi::RenderOverlay(wxDC& dc, PlugIn_ViewPort* vp) {
  if (!vp || !m_pSignalKNotesManager) return false;

  m_lastViewPort = *vp;
  m_lastViewPortValid = true;

  double centerLat = vp->clat;
  double centerLon = vp->clon;
  double maxDistance = CalculateMaxDistance(*vp);

  wxLongLong now = wxGetLocalTimeMillis();

  if (m_lastFetchTime == 0 || (now - m_lastFetchTime).ToLong() > 30000 ||
      fabs(centerLat - m_lastFetchCenterLat) > 0.01 ||
      fabs(centerLon - m_lastFetchCenterLon) > 0.01 ||
      fabs(maxDistance - m_lastFetchDistance) > maxDistance * 0.2) {
    m_pSignalKNotesManager->UpdateDisplayedIcons(centerLat, centerLon,
                                                 maxDistance);
    m_lastFetchCenterLat = centerLat;
    m_lastFetchCenterLon = centerLon;
    m_lastFetchDistance = maxDistance;
    m_lastFetchTime = now;
  }

  std::vector<const SignalKNote*> visibleNotes;
  m_pSignalKNotesManager->GetVisibleNotes(visibleNotes);

  if (visibleNotes.empty()) return false;

  // Clustering
  m_currentClusters = BuildClusters(visibleNotes, *vp);

  bool drewSomething = false;

  for (const auto& cluster : m_currentClusters) {
    // ---------------------------------------------------------
    // EINZEL-ICON
    // ---------------------------------------------------------
    if (cluster.notes.size() == 1) {
      const SignalKNote* note = cluster.notes[0];

      wxBitmap bmp;
      if (!m_pSignalKNotesManager->GetIconBitmapForNote(*note, bmp)) continue;

      // ICON SCHÄRFEN + ANTI-ALIASING + Y-FLIP
      wxBitmap glBmp = PrepareIconBitmapForGL(bmp, bmp.GetWidth());

      dc.DrawBitmap(glBmp, cluster.screenPos.x - glBmp.GetWidth() / 2,
                    cluster.screenPos.y - glBmp.GetHeight() / 2, true);

      drewSomething = true;
    }

    // ---------------------------------------------------------
    // CLUSTER-ICON
    // ---------------------------------------------------------
    else {
      wxBitmap clusterBmp = CreateClusterBitmap(cluster.notes.size());

      dc.DrawBitmap(clusterBmp, cluster.screenPos.x - clusterBmp.GetWidth() / 2,
                    cluster.screenPos.y - clusterBmp.GetHeight() / 2, true);

      drewSomething = true;
    }
  }

  return drewSomething;
}

bool signalk_notes_opencpn_pi::RenderGLOverlay(wxGLContext* pcontext,
                                               PlugIn_ViewPort* vp) {
  if (!vp || !m_pSignalKNotesManager) return false;

  m_lastViewPort = *vp;
  m_lastViewPortValid = true;

  double centerLat = vp->clat;
  double centerLon = vp->clon;
  double maxDistance = CalculateMaxDistance(*vp);

  wxLongLong now = wxGetLocalTimeMillis();

  if (m_lastFetchTime == 0 || (now - m_lastFetchTime).ToLong() > 30000 ||
      fabs(centerLat - m_lastFetchCenterLat) > 0.01 ||
      fabs(centerLon - m_lastFetchCenterLon) > 0.01 ||
      fabs(maxDistance - m_lastFetchDistance) > maxDistance * 0.2) {
    m_pSignalKNotesManager->UpdateDisplayedIcons(centerLat, centerLon,
                                                 maxDistance);
    m_lastFetchCenterLat = centerLat;
    m_lastFetchCenterLon = centerLon;
    m_lastFetchDistance = maxDistance;
    m_lastFetchTime = now;
  }

  std::vector<const SignalKNote*> visibleNotes;
  m_pSignalKNotesManager->GetVisibleNotes(visibleNotes);

  if (visibleNotes.empty()) return false;

  // CLUSTERING AUCH FÜR OPENGL
  m_currentClusters = BuildClusters(visibleNotes, *vp);

  bool drewSomething = false;

  for (const auto& cluster : m_currentClusters) {
    if (cluster.notes.size() == 1) {
      // Einzelnes Icon
      const SignalKNote* note = cluster.notes[0];
      wxBitmap bmp;
      if (!m_pSignalKNotesManager->GetIconBitmapForNote(*note, bmp)) continue;

      DrawGLBitmap(bmp, cluster.screenPos.x - bmp.GetWidth() / 2,
                   cluster.screenPos.y - bmp.GetHeight() / 2);
      drewSomething = true;
    } else {
      // Cluster-Icon zeichnen
      // Für OpenGL müssen wir ein Cluster-Bitmap erstellen
      wxBitmap clusterBmp = CreateClusterBitmap(cluster.notes.size());
      DrawGLBitmap(clusterBmp, cluster.screenPos.x - clusterBmp.GetWidth() / 2,
                   cluster.screenPos.y - clusterBmp.GetHeight() / 2);
      drewSomething = true;
    }
  }

  return drewSomething;
}

bool signalk_notes_opencpn_pi::RenderOverlayMultiCanvas(wxDC& dc,
                                                        PlugIn_ViewPort* vp,
                                                        int canvasIndex) {
  return RenderOverlay(dc, vp);
}

bool signalk_notes_opencpn_pi::RenderGLOverlayMultiCanvas(wxGLContext* pcontext,
                                                          PlugIn_ViewPort* vp,
                                                          int canvasIndex) {
  return RenderGLOverlay(pcontext, vp);
}

// ---------------------------------------------------------------------------------------
// Mouse / Keyboard
// ---------------------------------------------------------------------------------------

bool signalk_notes_opencpn_pi::KeyboardEventHook(wxKeyEvent& event) {
  return false;
}

bool signalk_notes_opencpn_pi::MouseEventHook(wxMouseEvent& event) {
  if (!event.LeftDown()) {
    return false;
  }

  if (!m_pSignalKNotesManager || !m_lastViewPortValid) {
    return false;
  }

  wxPoint mousePos = event.GetPosition();
  double clickLat, clickLon;
  GetCanvasLLPix(&m_lastViewPort, mousePos, &clickLat, &clickLon);

  // wxLogMessage("SignalK Notes: Mouse clicked at lat=%.6f lon=%.6f, screen pos
  // (%d, %d)",clickLat, clickLon, mousePos.x, mousePos.y);

  // 1. ERST einzelne Icons prüfen (höhere Priorität)
  std::vector<const SignalKNote*> visibleNotes;
  m_pSignalKNotesManager->GetVisibleNotes(visibleNotes);

  const SignalKNote* clickedNote = nullptr;
  double minDistance = 25.0;  // Pixel-Toleranz für einzelne Icons

  for (const SignalKNote* note : visibleNotes) {
    if (!note) continue;

    wxBitmap bmp;
    if (!m_pSignalKNotesManager->GetIconBitmapForNote(*note, bmp)) continue;

    wxPoint iconPos;
    GetCanvasPixLL(&m_lastViewPort, &iconPos, note->latitude, note->longitude);

    int dx = mousePos.x - iconPos.x;
    int dy = mousePos.y - iconPos.y;
    double dist = std::sqrt(dx * dx + dy * dy);

    // wxLogMessage("SignalK Notes: Distance to note '%s': %.1f
    // pixels",note->name.c_str(), dist);

    if (dist < minDistance) {
      minDistance = dist;
      clickedNote = note;
    }
  }

  // Einzelnes Icon getroffen?
  if (clickedNote) {
    // wxLogMessage("SignalK Notes: Single icon clicked: %s (distance %.1f
    // px)",clickedNote->name.c_str(), minDistance);
    event.Skip(false);
    event.StopPropagation();
    m_pSignalKNotesManager->OnIconClick(clickedNote->id);
    return true;
  }

  // 2. DANN Cluster prüfen (nur wenn kein einzelnes Icon getroffen)
  for (const auto& cluster : m_currentClusters) {
    if (cluster.notes.size() <= 1) continue;  // Nur echte Cluster

    int dx = mousePos.x - cluster.screenPos.x;
    int dy = mousePos.y - cluster.screenPos.y;
    double dist = std::sqrt(dx * dx + dy * dy);

    // wxLogMessage("SignalK Notes: Distance to cluster (%zu notes): %.1f
    // pixels",cluster.notes.size(), dist);

    if (dist <= 25) {  // Cluster-Radius + Toleranz
      // wxLogMessage("SignalK Notes: Cluster clicked with %zu
      // notes",cluster.notes.size());
      event.Skip(false);
      event.StopPropagation();
      OnClusterClick(cluster);
      return true;
    }
  }

  // wxLogMessage("SignalK Notes: No icon or cluster hit");
  return false;
}

// ---------------------------------------------------------------------------------------
// Config
// ---------------------------------------------------------------------------------------

void signalk_notes_opencpn_pi::SaveConfig() {
  wxFileConfig* pConf = m_pTPConfig;
  if (!pConf) return;

  pConf->SetPath("/Settings/signalk_notes_opencpn_pi");

  // Server
  pConf->Write("SignalKHost", m_pSignalKNotesManager->GetServerHost());
  pConf->Write("SignalKPort", m_pSignalKNotesManager->GetServerPort());

  // Provider settings
  pConf->SetPath("/Settings/signalk_notes_opencpn_pi/Providers");
  for (auto& it : m_pSignalKNotesManager->GetProviderSettings()) {
    pConf->Write(it.first, it.second);
  }

  // Icon mappings
  pConf->SetPath("/Settings/signalk_notes_opencpn_pi/IconMappings");
  for (auto& it : m_pSignalKNotesManager->GetIconMappings()) {
    pConf->Write(it.first, it.second);
  }

  // Zurück zum Basis-Pfad
  pConf->SetPath("/Settings/signalk_notes_opencpn_pi");

  // -------------------------
  // Auth speichern (NEU)
  // -------------------------
  pConf->Write("AuthToken", m_pSignalKNotesManager->GetAuthToken());
  pConf->Write("AuthRequestHref", m_pSignalKNotesManager->GetAuthRequestHref());

  // Client UUID
  pConf->Write("ClientUUID", m_clientUUID);
}

void signalk_notes_opencpn_pi::LoadConfig() {
  wxFileConfig* pConf = m_pTPConfig;
  if (!pConf) return;

  pConf->SetPath("/Settings/signalk_notes_opencpn_pi");

  // Server
  wxString host;
  int port;
  pConf->Read("SignalKHost", &host, "192.168.188.25");
  pConf->Read("SignalKPort", &port, 4000);
  m_pSignalKNotesManager->SetServerDetails(host, port);

  // Provider-Einstellungen
  std::map<wxString, bool> providers;
  pConf->SetPath("/Settings/signalk_notes_opencpn_pi/Providers");
  wxString providerName;
  long providerIndex;
  bool hasMore = pConf->GetFirstEntry(providerName, providerIndex);
  while (hasMore) {
    bool enabled;
    pConf->Read(providerName, &enabled, true);
    providers[providerName] = enabled;
    hasMore = pConf->GetNextEntry(providerName, providerIndex);
  }
  m_pSignalKNotesManager->SetProviderSettings(providers);

  // Icon-Mappings
  std::map<wxString, wxString> iconMappings;
  pConf->SetPath("/Settings/signalk_notes_opencpn_pi/IconMappings");
  wxString iconName;
  long iconIndex;
  hasMore = pConf->GetFirstEntry(iconName, iconIndex);
  while (hasMore) {
    wxString ocpnIcon;
    pConf->Read(iconName, &ocpnIcon, wxEmptyString);
    iconMappings[iconName] = ocpnIcon;
    hasMore = pConf->GetNextEntry(iconName, iconIndex);
  }
  m_pSignalKNotesManager->SetIconMappings(iconMappings);

  // Auth-Token & Request-Href
  pConf->SetPath("/Settings/signalk_notes_opencpn_pi");

  wxString authToken;
  wxString requestHref;

  pConf->Read("AuthToken", &authToken, wxEmptyString);
  pConf->Read("AuthRequestHref", &requestHref, wxEmptyString);

  // wxLogMessage("SignalK Notes LoadConfig: token=%s,
  // href=%s",authToken.IsEmpty() ? "empty" : "present",requestHref.IsEmpty() ?
  // "empty" : "present");

  // Token setzen (ohne Validierung beim Laden)
  m_pSignalKNotesManager->SetAuthToken(authToken);

  // Href setzen falls vorhanden
  if (!requestHref.IsEmpty()) {
    m_pSignalKNotesManager->SetAuthRequestHref(requestHref);
  } else {
    m_pSignalKNotesManager->ClearAuthRequest();
  }

  // Client UUID
  pConf->Read("ClientUUID", &m_clientUUID, "");
  if (m_clientUUID.IsEmpty()) {
    uuid_t binuuid;
    uuid_generate_random(binuuid);

    char uuid_str[37];
    uuid_unparse_lower(binuuid, uuid_str);

    m_clientUUID = wxString(uuid_str);
    pConf->Write("ClientUUID", m_clientUUID);

    // wxLogMessage("SignalK Notes: Generated new client UUID: %s",
    // m_clientUUID);
  }
}

wxString signalk_notes_opencpn_pi::GetPluginIconDir() const {
  wxString dir = GetPluginDataDir("signalk_notes_opencpn_pi");
  if (!dir.EndsWith("/")) dir += "/";
  dir += "data/icons/";
  return dir;
}

wxBitmap* signalk_notes_opencpn_pi::GetPlugInBitmap() {
  return &m_ptpicons->m_bm_signalk_notes_opencpn_pi;
}

void signalk_notes_opencpn_pi::appendOSDirSlash(wxString* pString) {
  wxChar sep = wxFileName::GetPathSeparator();
  if (pString->Last() != sep) pString->Append(sep);
}

double signalk_notes_opencpn_pi::CalculateMaxDistance(PlugIn_ViewPort& vp) {
  // Viewport-Abmessungen
  double vpWidth = vp.pix_width;
  double vpHeight = vp.pix_height;

  // Diagonale in Pixeln (Halbdiagonale)
  double diagonalPixels =
      std::sqrt(vpWidth * vpWidth + vpHeight * vpHeight) / 2.0;

  // Meter pro Pixel
  double metersPerPixel = vp.view_scale_ppm;
  if (metersPerPixel > 0) {
    double maxDistance = diagonalPixels / metersPerPixel;

    // Immer aufrunden, selbst bei 0.0000000001 → 1
    return std::ceil(maxDistance);
  }

  // Fallback: grobe Abschätzung über Breiten-Ausdehnung
  double latSpan = vpHeight * vp.view_scale_ppm / 111000.0;
  double maxDistanceKm = latSpan * 111.0 / 2.0;

  // Auch hier: immer aufrunden
  return std::ceil(maxDistanceKm * 1000.0);
}

int signalk_notes_opencpn_pi::GetVisibleNoteCount() const {
  std::vector<const SignalKNote*> notes;
  m_pSignalKNotesManager->GetVisibleNotes(notes);
  return notes.size();
}

std::vector<signalk_notes_opencpn_pi::NoteCluster>
signalk_notes_opencpn_pi::BuildClusters(
    const std::vector<const SignalKNote*>& notes, const PlugIn_ViewPort& vp,
    int clusterRadius) {
  // wxLogMessage("SignalK Notes: BuildClusters called with %zu notes,
  // radius=%d",notes.size(), clusterRadius);

  std::vector<NoteCluster> clusters;
  std::vector<bool> clustered(notes.size(), false);

  // Nicht-const Kopie für GetCanvasPixLL
  PlugIn_ViewPort vpCopy = vp;

  for (size_t i = 0; i < notes.size(); i++) {
    if (clustered[i]) continue;

    wxPoint p1;
    GetCanvasPixLL(&vpCopy, &p1, notes[i]->latitude, notes[i]->longitude);

    // wxLogMessage("SignalK Notes: Note %zu at screen pos (%d, %d)", i,
    // p1.x,p1.y);

    NoteCluster cluster;
    cluster.notes.push_back(notes[i]);
    clustered[i] = true;

    // Finde alle Notes im Cluster-Radius
    for (size_t j = i + 1; j < notes.size(); j++) {
      if (clustered[j]) continue;

      wxPoint p2;
      GetCanvasPixLL(&vpCopy, &p2, notes[j]->latitude, notes[j]->longitude);

      int dx = p2.x - p1.x;
      int dy = p2.y - p1.y;
      double dist = std::sqrt(dx * dx + dy * dy);

      // wxLogMessage("SignalK Notes: Distance from note %zu to %zu: %.1f pixels
      // (threshold: %d)",i, j, dist, clusterRadius);

      if (dist < clusterRadius) {
        // wxLogMessage("SignalK Notes: Adding note %zu to cluster (distance
        // %.1f < %d)", j,dist, clusterRadius);
        cluster.notes.push_back(notes[j]);
        clustered[j] = true;
      }
    }

    // Cluster-Zentrum berechnen
    double sumLat = 0, sumLon = 0;
    for (const auto* note : cluster.notes) {
      sumLat += note->latitude;
      sumLon += note->longitude;
    }
    cluster.centerLat = sumLat / cluster.notes.size();
    cluster.centerLon = sumLon / cluster.notes.size();

    GetCanvasPixLL(&vpCopy, &cluster.screenPos, cluster.centerLat,
                   cluster.centerLon);

    // wxLogMessage("SignalK Notes: Created cluster with %zu notes at screen pos
    // (%d, %d)",cluster.notes.size(), cluster.screenPos.x,
    // cluster.screenPos.y);

    clusters.push_back(cluster);
  }

  // wxLogMessage("SignalK Notes: BuildClusters created %zu clusters
  // total",clusters.size());

  return clusters;
}

void signalk_notes_opencpn_pi::OnClusterClick(const NoteCluster& cluster) {
  // wxLogMessage("SignalK Notes: Cluster clicked with %zu
  // notes",cluster.notes.size());

  // Bounding Box der Notes berechnen
  double minLat = cluster.notes[0]->latitude;
  double maxLat = cluster.notes[0]->latitude;
  double minLon = cluster.notes[0]->longitude;
  double maxLon = cluster.notes[0]->longitude;

  for (const auto* note : cluster.notes) {
    minLat = std::min(minLat, note->latitude);
    maxLat = std::max(maxLat, note->latitude);
    minLon = std::min(minLon, note->longitude);
    maxLon = std::max(maxLon, note->longitude);
  }

  double centerLat = (minLat + maxLat) / 2.0;
  double centerLon = (minLon + maxLon) / 2.0;

  // Berechne benötigten Zoom-Level
  // Distanz zwischen den äußersten Notes
  double latSpan = maxLat - minLat;
  double lonSpan = maxLon - minLon;

  // wxLogMessage("SignalK Notes: Cluster span: lat=%.6f, lon=%.6f",
  // latSpan,lonSpan);

  // Wenn alle Notes am gleichen Punkt sind, fixe Zoom-Stufe
  if (latSpan < 0.00001 && lonSpan < 0.00001) {
    // Sehr stark zoomen für überlappende Notes
    double newScale = m_lastViewPort.chart_scale * 0.1;  // 10x Zoom
    // wxLogMessage("SignalK Notes: Notes overlap, zooming to scale
    // %.2f",newScale);
    JumpToPosition(centerLat, centerLon, newScale);
    return;
  }

  // Berechne erforderlichen Scale damit Notes mind. 60 Pixel auseinander sind
  // (größer als Cluster-Radius von 40 Pixeln)
  double requiredPixelDistance = 80;  // Mindestabstand in Pixeln

  // Konvertiere Lat/Lon-Spanne in Meter
  double latMeters = latSpan * 111000.0;  // 1° ≈ 111km
  double lonMeters = lonSpan * 111000.0 * cos(centerLat * M_PI / 180.0);
  double maxSpanMeters = std::max(latMeters, lonMeters);

  // Berechne benötigten PPM (Pixel per Meter)
  double requiredPPM = requiredPixelDistance / maxSpanMeters;

  // Chart scale berechnen (scale = 1 / ppm)
  double newScale = 1.0 / requiredPPM;

  // Limitiere den Zoom (nicht zu nah, nicht zu weit)
  double minScale = m_lastViewPort.chart_scale * 0.05;  // Max 20x Zoom
  double maxScale = m_lastViewPort.chart_scale * 0.5;   // Min 2x Zoom
  newScale = std::max(minScale, std::min(maxScale, newScale));

  // wxLogMessage("SignalK Notes: Zooming to scale %.2f (current: %.2f)",
  // newScale,m_lastViewPort.chart_scale);

  JumpToPosition(centerLat, centerLon, newScale);
}

wxBitmap signalk_notes_opencpn_pi::CreateClusterBitmap(size_t count) {
  const int size = 24;
  const int radius = 10;
  const int centerX = size / 2;
  const int centerY = size / 2;

  // Bitmap mit Alpha
  wxBitmap bmp(size, size, 32);
  bmp.UseAlpha(true);

  wxMemoryDC dc;
  dc.SelectObject(bmp);

  // Voll transparent löschen
  dc.SetBackground(*wxTRANSPARENT_BRUSH);
  dc.Clear();

  wxGraphicsContext* gc = wxGraphicsContext::Create(dc);
  if (!gc) {
    dc.SelectObject(wxNullBitmap);
    return bmp;
  }

  gc->SetAntialiasMode(wxANTIALIAS_DEFAULT);

  wxColour fill(30, 144, 255, 200);  // leicht transparenter Blau-Kreis
  wxColour border(0, 0, 0, 220);

  gc->SetBrush(wxBrush(fill));
  gc->SetPen(wxPen(border, 1));
  gc->DrawEllipse(centerX - radius, centerY - radius, radius * 2, radius * 2);

  wxFont font(8, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD,
              false, "Arial");
  gc->SetFont(font, *wxWHITE);

  wxString text = wxString::Format("%zu", count);

  double tw, th;
  gc->GetTextExtent(text, &tw, &th);

  gc->SetTransform(gc->CreateMatrix());
  gc->DrawText(text, centerX - tw / 2, centerY - th / 2);

  delete gc;
  dc.SelectObject(wxNullBitmap);

  // Y-Flip für OpenGL UND konsistente Darstellung
  wxImage img = bmp.ConvertToImage();
  img = img.Mirror(false);
  wxBitmap flipped(img);
  flipped.UseAlpha(true);

  return flipped;
}

wxBitmap signalk_notes_opencpn_pi::PrepareIconBitmapForGL(const wxBitmap& src,
                                                          int targetSize) {
  // Neues Bitmap mit Alpha
  wxBitmap bmp(targetSize, targetSize, 32);
  bmp.UseAlpha(true);

  wxMemoryDC dc;
  dc.SelectObject(bmp);

  dc.SetBackground(*wxTRANSPARENT_BRUSH);
  dc.Clear();

  wxGraphicsContext* gc = wxGraphicsContext::Create(dc);
  if (!gc) {
    dc.SelectObject(wxNullBitmap);
    return src;
  }

  gc->SetAntialiasMode(wxANTIALIAS_DEFAULT);

  // Quelle skalieren
  wxImage img = src.ConvertToImage();
  img.InitAlpha();
  wxImage scaled = img.Scale(targetSize, targetSize, wxIMAGE_QUALITY_HIGH);

  wxBitmap scaledBmp(scaled);

  // Bitmap zeichnen
  gc->DrawBitmap(scaledBmp, 0, 0, targetSize, targetSize);

  delete gc;
  dc.SelectObject(wxNullBitmap);

  // Y-Flip für OpenGL
  wxImage flipped = bmp.ConvertToImage().Mirror(false);
  wxBitmap finalBmp(flipped);
  finalBmp.UseAlpha(true);

  return finalBmp;
}

#if !defined(__OCPN__ANDROID__)
void signalk_notes_opencpn_pi::DrawGLBitmap(const wxBitmap& bmp, int x, int y) {
  wxImage img = bmp.ConvertToImage();  // bereits Y-gefliipt aus
                                       // CreateClusterBitmap / Icons
  img.InitAlpha();

  int w = img.GetWidth();
  int h = img.GetHeight();

  unsigned char* rgb = img.GetData();
  unsigned char* alpha = img.GetAlpha();

  if (!rgb) return;

  std::vector<unsigned char> buffer(w * h * 4);
  for (int i = 0; i < w * h; i++) {
    buffer[i * 4 + 0] = rgb[i * 3 + 0];
    buffer[i * 4 + 1] = rgb[i * 3 + 1];
    buffer[i * 4 + 2] = rgb[i * 3 + 2];
    buffer[i * 4 + 3] = alpha ? alpha[i] : 255;
  }

  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  glRasterPos2i(x, y);
  glDrawPixels(w, h, GL_RGBA, GL_UNSIGNED_BYTE, buffer.data());
}
#endif

#if defined(__OCPN__ANDROID__)
void signalk_notes_opencpn_pi::DrawGLBitmap(const wxBitmap& bmp, int x, int y) {
  wxImage img = bmp.ConvertToImage();  // bereits Y-gefliipt
  img.InitAlpha();

  int w = img.GetWidth();
  int h = img.GetHeight();

  if (!img.GetData()) return;

  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  GLuint texId;
  glGenTextures(1, &texId);
  glBindTexture(GL_TEXTURE_2D, texId);

  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE,
               img.GetData());

  glEnable(GL_TEXTURE_2D);

  glBegin(GL_QUADS);
  glTexCoord2f(0, 0);
  glVertex2f(x, y);
  glTexCoord2f(1, 0);
  glVertex2f(x + w, y);
  glTexCoord2f(1, 1);
  glVertex2f(x + w, y + h);
  glTexCoord2f(0, 1);
  glVertex2f(x, y + h);
  glEnd();

  glDisable(GL_TEXTURE_2D);
  glDeleteTextures(1, &texId);
}
#endif
