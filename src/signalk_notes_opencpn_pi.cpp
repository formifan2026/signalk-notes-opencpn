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
#include <algorithm>
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

bool signalk_notes_opencpn_pi::IsClusterVisible(const PlugIn_ViewPort& vp)
{
    // 1. Bounding Box der Notes berechnen
    double minLat = 90.0, maxLat = -90.0;
    double minLon = 180.0, maxLon = -180.0;

    for (const auto* note : m_clusterZoom.notes) {
        minLat = std::min(minLat, note->latitude);
        maxLat = std::max(maxLat, note->latitude);
        minLon = std::min(minLon, note->longitude);
        maxLon = std::max(maxLon, note->longitude);
    }

    // 2. Viewport nach dem nächsten Zoomschritt simulieren
    double zoomFactor = 0.8; // z. B. 20% hineinzoomen
    double newLatSpan = (vp.lat_max - vp.lat_min) * zoomFactor;
    double newLonSpan = (vp.lon_max - vp.lon_min) * zoomFactor;

    double newLatMin = vp.clat - newLatSpan / 2.0;
    double newLatMax = vp.clat + newLatSpan / 2.0;
    double newLonMin = vp.clon - newLonSpan / 2.0;
    double newLonMax = vp.clon + newLonSpan / 2.0;

    // 3. Prüfen, ob ALLE Notes nach dem Zoom noch sichtbar wären
    return (minLat >= newLatMin && maxLat <= newLatMax &&
            minLon >= newLonMin && maxLon <= newLonMax);
}


void signalk_notes_opencpn_pi::MoveViewportTowardsCluster(PlugIn_ViewPort& vp)
{
    const double zoomFactor = 2.0; // ein Zoomschritt

    // wxLogMessage("SKN: MoveViewportTowardsCluster: ENTER scale=%.3f", vp.view_scale_ppm);

    bool allVisible = AreAllNotesVisibleAfterNextZoom(vp, zoomFactor);
    // wxLogMessage("SKN: MoveViewportTowardsCluster: allVisibleAfterZoom=%d", allVisible);

    if (!allVisible) {
        // PAN wie bisher
        double vpCenterLat = (vp.lat_min + vp.lat_max) / 2.0;
        double vpCenterLon = (vp.lon_min + vp.lon_max) / 2.0;

        double dLat = m_clusterZoom.targetLat - vpCenterLat;
        double dLon = m_clusterZoom.targetLon - vpCenterLon;

        double step = 0.3;

        double newCenterLat = vpCenterLat + dLat * step;
        double newCenterLon = vpCenterLon + dLon * step;

        double latSpan = vp.lat_max - vp.lat_min;
        double lonSpan = vp.lon_max - vp.lon_min;

        vp.lat_min = newCenterLat - latSpan / 2.0;
        vp.lat_max = newCenterLat + latSpan / 2.0;
        vp.lon_min = newCenterLon - lonSpan / 2.0;
        vp.lon_max = newCenterLon + lonSpan / 2.0;

        // wxLogMessage("SKN: MoveViewportTowardsCluster: PAN -> center=%.6f/%.6f scale=%.3f",newCenterLat, newCenterLon, vp.view_scale_ppm);

        JumpToPosition(newCenterLat, newCenterLon, vp.view_scale_ppm);
        return;
    }

    double newScale = vp.view_scale_ppm * zoomFactor;

    // wxLogMessage("SKN: MoveViewportTowardsCluster: ZOOM -> targetLat=%.6f targetLon=%.6f newScale=%.3f (deactivating)",m_clusterZoom.targetLat, m_clusterZoom.targetLon, newScale);

    JumpToPosition(m_clusterZoom.targetLat, m_clusterZoom.targetLon, newScale);

    m_clusterZoom.active = false;
}

void signalk_notes_opencpn_pi::ProcessClusterPanStep()
{
    if (!m_lastViewPortValid) {
        // wxLogMessage("SKN: ProcessClusterPanStep: lastViewPort INVALID");
        return;
    }

    // wxLogMessage("SKN: ProcessClusterPanStep: ENTER active=%d notes=%zu",m_clusterZoom.active, m_clusterZoom.notes.size());

    PlugIn_ViewPort vp = m_lastViewPort;

    // wxLogMessage("SKN: ProcessClusterPanStep: BEFORE MoveViewportTowardsCluster scale=%.3f",vp.view_scale_ppm);

    MoveViewportTowardsCluster(vp);

    // wxLogMessage("SKN: ProcessClusterPanStep: AFTER MoveViewportTowardsCluster (local vp) scale=%.3f active=%d",vp.view_scale_ppm, m_clusterZoom.active);

    // Optionaler Fallback, falls aus irgendeinem Grund nichts passiert:
    if (m_clusterZoom.notes.size() <= 1) {
        // wxLogMessage("SKN: ProcessClusterPanStep: cluster resolved by notes.size -> deactivating");
        m_clusterZoom.active = false;
    }
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

  // Provider-Infos laden, falls Token vorhanden
  if (!m_pSignalKNotesManager->GetAuthToken().IsEmpty()) {
    std::map<wxString, bool> plugins;
    m_pSignalKNotesManager->FetchInstalledPlugins(plugins);
  }

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

  // Falls authentifiziert: Plugin-Liste holen (NEU!)
  if (!m_pSignalKNotesManager->GetAuthToken().IsEmpty()) {
    std::map<wxString, bool> plugins;
    m_pSignalKNotesManager->FetchInstalledPlugins(plugins);
  }

  // Provider und Icon-Mappings laden
  m_pConfigDialog->UpdateProviders(
      m_pSignalKNotesManager->GetDiscoveredProviders());
  m_pConfigDialog->UpdateIconMappings(
      m_pSignalKNotesManager->GetDiscoveredIcons());
  m_pConfigDialog->LoadSettings(m_pSignalKNotesManager->GetProviderSettings(),
                                m_pSignalKNotesManager->GetIconMappings());

  // Display-Einstellungen laden
  m_pConfigDialog->LoadDisplaySettings(m_iconSize, m_clusterSize,
                                       m_clusterRadius, m_clusterColor,
                                       m_clusterTextColor, m_clusterFontSize);

  // Sichtbare Note-Anzahl aktualisieren
  if (m_pSignalKNotesManager) {
    int visibleCount = GetVisibleNoteCount();
    m_pConfigDialog->UpdateVisibleCount(visibleCount);
  }

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
  if (m_clusterZoom.active) {
    if (m_clusterZoom.justStarted) {
      m_clusterZoom.justStarted = false;
    } else {
      ProcessClusterPanStep();
    }
  }

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
                                               PlugIn_ViewPort* vp)
{
    if (!vp || !m_pSignalKNotesManager)
        return false;

    // Viewport speichern
    m_lastViewPort = *vp;
    m_lastViewPortValid = true;
// wxLogMessage("SKN: RenderGLOverlay: active=%d justStarted=%d scale=%.3f lat_span=%.6f lon_span=%.6f",m_clusterZoom.active, m_clusterZoom.justStarted,vp->view_scale_ppm,vp->lat_max - vp->lat_min,vp->lon_max - vp->lon_min);

    // --- PAN / ZOOM STEUERUNG -----------------------------------------
    //wxLogMessage("SKN: RenderGLOverlay, clusterZoom.active=%d, justStarted=%d",m_clusterZoom.active, m_clusterZoom.justStarted);

if (m_clusterZoom.active) {
  // wxLogMessage("SKN: RenderGLOverlay: clusterZoom active, justStarted=%d", m_clusterZoom.justStarted);
  if (m_clusterZoom.justStarted) {
    m_clusterZoom.justStarted = false;
  } else {
    // wxLogMessage("SKN: RenderGLOverlay: calling ProcessClusterPanStep()");
    ProcessClusterPanStep();
  }
}



    // --- ICON FETCH ----------------------------------------------------
    double centerLat = vp->clat;
    double centerLon = vp->clon;
    double maxDistance = CalculateMaxDistance(*vp);

    wxLongLong now = wxGetLocalTimeMillis();

    if (m_lastFetchTime == 0 ||
        (now - m_lastFetchTime).ToLong() > 30000 ||
        fabs(centerLat - m_lastFetchCenterLat) > 0.01 ||
        fabs(centerLon - m_lastFetchCenterLon) > 0.01 ||
        fabs(maxDistance - m_lastFetchDistance) > maxDistance * 0.2)
    {
        m_pSignalKNotesManager->UpdateDisplayedIcons(centerLat, centerLon, maxDistance);
        m_lastFetchCenterLat = centerLat;
        m_lastFetchCenterLon = centerLon;
        m_lastFetchDistance = maxDistance;
        m_lastFetchTime = now;
    }

    // --- SICHTBARE NOTES HOLEN ----------------------------------------
    std::vector<const SignalKNote*> visibleNotes;
    m_pSignalKNotesManager->GetVisibleNotes(visibleNotes);

    if (visibleNotes.empty())
        return false;

    // --- CLUSTERING ----------------------------------------------------
    m_currentClusters = BuildClusters(visibleNotes, *vp);

    // --- ZEICHNEN ------------------------------------------------------
    bool drewSomething = false;

    for (const auto& cluster : m_currentClusters) {

        if (cluster.notes.size() == 1) {
            const SignalKNote* note = cluster.notes[0];
            wxBitmap bmp;
            if (!m_pSignalKNotesManager->GetIconBitmapForNote(*note, bmp))
                continue;

            DrawGLBitmap(bmp,
                         cluster.screenPos.x - bmp.GetWidth() / 2,
                         cluster.screenPos.y - bmp.GetHeight() / 2);

            drewSomething = true;
        }
        else {
            wxBitmap clusterBmp = CreateClusterBitmap(cluster.notes.size());
            DrawGLBitmap(clusterBmp,
                         cluster.screenPos.x - clusterBmp.GetWidth() / 2,
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
  if (!event.LeftDown()) return false;

  if (!m_pSignalKNotesManager || !m_lastViewPortValid) return false;

  wxPoint mousePos = event.GetPosition();
  double clickLat, clickLon;
  GetCanvasLLPix(&m_lastViewPort, mousePos, &clickLat, &clickLon);

  // wxLogMessage("SignalK Notes: Mouse clicked at lat=%.6f lon=%.6f, screen
  // pos(%d, %d)",clickLat, clickLon, mousePos.x, mousePos.y);

  auto pxToMeters = [&](int px) { return px * m_lastViewPort.view_scale_ppm; };

  const double baseScale = 800.0;
  const double baseTolMeters = 10.0;
  const double scaleFactor = (double)m_lastViewPort.chart_scale / baseScale;
  const double tolMeters = baseTolMeters * scaleFactor;

  const double metersPerDegLat = 111320.0;
  const double metersPerDegLonClick =
      metersPerDegLat * cos(clickLat * M_PI / 180.0);

  // ---------------------------------------------------------
  // Bounding-Box Methode zur Berechnung um den Klickpunkt
  // ---------------------------------------------------------

  auto computeBoxAroundClick = [&](int pixelSize) {
    double halfSizeMeters = pxToMeters(pixelSize) / 2.0 + tolMeters;

    double dLat = halfSizeMeters / metersPerDegLat;
    double dLon = halfSizeMeters / metersPerDegLonClick;

    struct Box {
      double minLat, maxLat, minLon, maxLon;
    };
    return Box{clickLat - dLat, clickLat + dLat, clickLon - dLon,
               clickLon + dLon};
  };

  // ---------------------------------------------------------
  // CLUSTER prüfen
  // ---------------------------------------------------------
  auto clusterBox = computeBoxAroundClick(m_clusterSize);
  for (size_t i = 0; i < m_currentClusters.size(); i++) {
    auto& cluster = m_currentClusters[i];
    if (cluster.notes.size() <= 1) continue;
    double lat = cluster.centerLat;
    double lon = cluster.centerLon;
    if (lat >= clusterBox.minLat && lat <= clusterBox.maxLat &&
        lon >= clusterBox.minLon && lon <= clusterBox.maxLon) {
      OnClusterClick(cluster);
      return true;
    }
  }

  // ---------------------------------------------------------
  // Einzelne Notes prüfen
  // ---------------------------------------------------------
  // Einzelne Notes prüfen
  auto noteBox = computeBoxAroundClick(m_iconSize);
  std::vector<const SignalKNote*> visibleNotes;
  m_pSignalKNotesManager->GetVisibleNotes(visibleNotes);

  for (size_t i = 0; i < visibleNotes.size(); i++) {
    const SignalKNote* note = visibleNotes[i];
    if (!note) continue;

    double lat = note->latitude;
    double lon = note->longitude;

    if (lat >= noteBox.minLat && lat <= noteBox.maxLat &&
        lon >= noteBox.minLon && lon <= noteBox.maxLon) {
      m_pSignalKNotesManager->OnIconClick(note->id);
      return true;
    }
  }

  // wxLogMessage("SignalK Notes: No icon or cluster hit");
  return false;  // ← KLAR: nichts getroffen
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
  if (m_pConfigDialog) {
    m_pTPConfig->Write("DisplaySettings/IconSize",
                       m_pConfigDialog->GetIconSize());
    m_pTPConfig->Write("DisplaySettings/ClusterSize",
                       m_pConfigDialog->GetClusterSize());
    m_pTPConfig->Write("DisplaySettings/ClusterRadius",
                       m_pConfigDialog->GetClusterRadius());

    wxColour clusterColor = m_pConfigDialog->GetClusterColor();
    m_pTPConfig->Write("DisplaySettings/ClusterColorR",
                       (int)clusterColor.Red());
    m_pTPConfig->Write("DisplaySettings/ClusterColorG",
                       (int)clusterColor.Green());
    m_pTPConfig->Write("DisplaySettings/ClusterColorB",
                       (int)clusterColor.Blue());

    wxColour textColor = m_pConfigDialog->GetClusterTextColor();
    m_pTPConfig->Write("DisplaySettings/ClusterTextColorR",
                       (int)textColor.Red());
    m_pTPConfig->Write("DisplaySettings/ClusterTextColorG",
                       (int)textColor.Green());
    m_pTPConfig->Write("DisplaySettings/ClusterTextColorB",
                       (int)textColor.Blue());

    m_pTPConfig->Write("DisplaySettings/ClusterFontSize",
                       m_pConfigDialog->GetClusterFontSize());
  }
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
  // Display-Einstellungen laden
  m_iconSize = m_pTPConfig->Read("DisplaySettings/IconSize",
                                 (long)tpConfigDialog::DEFAULT_ICON_SIZE);
  m_clusterSize = m_pTPConfig->Read("DisplaySettings/ClusterSize",
                                    (long)tpConfigDialog::DEFAULT_CLUSTER_SIZE);
  m_clusterRadius =
      m_pTPConfig->Read("DisplaySettings/ClusterRadius",
                        (long)tpConfigDialog::DEFAULT_CLUSTER_RADIUS);

  int r = m_pTPConfig->Read("DisplaySettings/ClusterColorR",
                            (long)tpConfigDialog::DEFAULT_CLUSTER_COLOR.Red());
  int g =
      m_pTPConfig->Read("DisplaySettings/ClusterColorG",
                        (long)tpConfigDialog::DEFAULT_CLUSTER_COLOR.Green());
  int b = m_pTPConfig->Read("DisplaySettings/ClusterColorB",
                            (long)tpConfigDialog::DEFAULT_CLUSTER_COLOR.Blue());
  m_clusterColor = wxColour(r, g, b);

  r = m_pTPConfig->Read("DisplaySettings/ClusterTextColorR",
                        (long)tpConfigDialog::DEFAULT_CLUSTER_TEXT_COLOR.Red());
  g = m_pTPConfig->Read(
      "DisplaySettings/ClusterTextColorG",
      (long)tpConfigDialog::DEFAULT_CLUSTER_TEXT_COLOR.Green());
  b = m_pTPConfig->Read(
      "DisplaySettings/ClusterTextColorB",
      (long)tpConfigDialog::DEFAULT_CLUSTER_TEXT_COLOR.Blue());
  m_clusterTextColor = wxColour(r, g, b);

  m_clusterFontSize =
      m_pTPConfig->Read("DisplaySettings/ClusterFontSize",
                        (long)tpConfigDialog::DEFAULT_CLUSTER_FONT_SIZE);
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
    // wxLogMessage("SKN: ===== ClusterClick START =====");
    // wxLogMessage("SKN: Cluster with %zu notes", cluster.notes.size());

    // Notes übernehmen
    m_clusterZoom.notes = cluster.notes;

    // Zielpunkt setzen
    m_clusterZoom.targetLat = cluster.centerLat;
    m_clusterZoom.targetLon = cluster.centerLon;

    // wxLogMessage("SKN: ClusterClick: targetLat=%.6f targetLon=%.6f",m_clusterZoom.targetLat, m_clusterZoom.targetLon);

    // State aktivieren
    m_clusterZoom.active = true;
    m_clusterZoom.justStarted = true;

    // wxLogMessage("SKN: clusterZoom activated (active=1, justStarted=1)");
    // wxLogMessage("SKN: ===== ClusterClick END =====");

    RequestRefresh(m_parent_window);
}

void signalk_notes_opencpn_pi::SetDisplaySettings(int iconSize, int clusterSize,
                                                  int clusterRadius,
                                                  const wxColour& clusterColor,
                                                  const wxColour& textColor,
                                                  int fontSize) {
  m_iconSize = iconSize;
  m_clusterSize = clusterSize;
  m_clusterRadius = clusterRadius;
  m_clusterColor = clusterColor;
  m_clusterTextColor = textColor;
  m_clusterFontSize = fontSize;
}

wxBitmap signalk_notes_opencpn_pi::CreateClusterBitmap(size_t count) {
  // Einstellungen aus Member-Variablen verwenden (wurden aus Config geladen)
  int size = m_clusterSize;
  int radius = m_clusterRadius;
  wxColour circleColor = m_clusterColor;
  wxColour textColor = m_clusterTextColor;
  int fontSize = m_clusterFontSize;

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

  // Farben mit Alpha-Kanal
  wxColour fill(circleColor.Red(), circleColor.Green(), circleColor.Blue(),
                200);  // leicht transparent
  wxColour border(0, 0, 0, 220);

  gc->SetBrush(wxBrush(fill));
  gc->SetPen(wxPen(border, 1));
  gc->DrawEllipse(centerX - radius, centerY - radius, radius * 2, radius * 2);

  wxFont font(fontSize, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL,
              wxFONTWEIGHT_BOLD, false, "Arial");
  gc->SetFont(font, textColor);

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


struct FutureViewPort {
    double lat_min, lat_max;
    double lon_min, lon_max;
};

static FutureViewPort MakeFutureViewportForCluster(const PlugIn_ViewPort& vp,
                                                   double centerLat,
                                                   double centerLon,
                                                   double zoomFactor)
{
    double curLatSpan = vp.lat_max - vp.lat_min;
    double curLonSpan = vp.lon_max - vp.lon_min;

    double newLatSpan = curLatSpan / zoomFactor;
    double newLonSpan = curLonSpan / zoomFactor;

    FutureViewPort fvp;
    fvp.lat_min = centerLat - newLatSpan / 2.0;
    fvp.lat_max = centerLat + newLatSpan / 2.0;
    fvp.lon_min = centerLon - newLonSpan / 2.0;
    fvp.lon_max = centerLon + newLonSpan / 2.0;

    return fvp;
}

bool signalk_notes_opencpn_pi::AreAllNotesVisibleAfterNextZoom(const PlugIn_ViewPort& vp,
                                                               double zoomFactor) const
{
    // wxLogMessage("SKN: AreAllNotesVisibleAfterNextZoom: ENTER zoomFactor=%.2f", zoomFactor);

    if (m_clusterZoom.notes.empty()) {
        // wxLogMessage("SKN:   no notes -> false");
        return false;
    }

    double centerLat = m_clusterZoom.targetLat;
    double centerLon = m_clusterZoom.targetLon;

    // wxLogMessage("SKN:   target center = %.6f / %.6f", centerLat, centerLon);

    FutureViewPort fvp = MakeFutureViewportForCluster(vp, centerLat, centerLon, zoomFactor);

    // wxLogMessage("SKN:   future viewport lat=[%.6f .. %.6f] lon=[%.6f .. %.6f]",fvp.lat_min, fvp.lat_max, fvp.lon_min, fvp.lon_max);

    bool allInside = true;

    for (const auto* note : m_clusterZoom.notes) {
        if (!note)
            continue;

        double lat = note->latitude;
        double lon = note->longitude;

        bool inside =
            !(lat < fvp.lat_min || lat > fvp.lat_max ||
              lon < fvp.lon_min || lon > fvp.lon_max);

        // wxLogMessage("SKN:     note at %.6f / %.6f -> %s",lat, lon, inside ? "inside" : "OUTSIDE");

        if (!inside)
            allInside = false;
    }

    // wxLogMessage("SKN: AreAllNotesVisibleAfterNextZoom: result=%s",allInside ? "true" : "false");

    return allInside;
}

