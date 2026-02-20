/******************************************************************************
 * Project:   SignalK Notes Plugin for OpenCPN
 * Purpose:   Main plugin implementation and OpenCPN integration
 * Author:    Dirk Behrendt
 * Licence:   GPLv2
 ******************************************************************************/

#ifdef _WIN32
  #define WIN32_LEAN_AND_MEAN
  #include <windows.h>
#endif

#if defined(ocpnUSE_GL) && !defined(__OCPN__ANDROID__) && !defined(__APPLE__)
  #include <GL/gl.h>
  #include <GL/glu.h>
#endif

#if defined(ocpnUSE_GL) && defined(__APPLE__)
  #include <OpenGL/gl.h>
#endif

#ifdef __OCPN__ANDROID__
  #include <GLES2/gl2.h>
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

#ifdef __WXMSW__
  #include <rpc.h>
  #pragma comment(lib, "Rpcrt4.lib")
#elif defined(__OCPN__ANDROID__)
  #include "android_uuid.h"
#else
  #include <uuid/uuid.h>
#endif

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

// -----------------------------------------------------------------------------
// Factory
// -----------------------------------------------------------------------------

extern "C" DECL_EXP opencpn_plugin* create_pi(void* ppimgr) {
  return new signalk_notes_opencpn_pi(ppimgr);
}

extern "C" DECL_EXP void destroy_pi(opencpn_plugin* p) {
  delete p;
}

extern "C" DECL_EXP int get_api_major_version() {
  return OCPN_API_VERSION_MAJOR;
}

extern "C" DECL_EXP int get_api_minor_version() {
  return OCPN_API_VERSION_MINOR;
}

// -----------------------------------------------------------------------------
// Constructor / Destructor
// -----------------------------------------------------------------------------

signalk_notes_opencpn_pi::signalk_notes_opencpn_pi(void* ppimgr)
    : opencpn_plugin_120(ppimgr) {

  g_signalk_notes_opencpn_pi = this;

  wxString pluginDir = GetPluginDataDir("signalk_notes_opencpn_pi");
  if (!pluginDir.EndsWith("/")) pluginDir += "/";

  g_PrivateDataDir = new wxString(pluginDir);

  wxString dataDir = pluginDir + "data/";
  if (!wxDir::Exists(dataDir)) wxMkdir(dataDir);

  wxString iconDir = dataDir + "icons/";
  if (!wxDir::Exists(iconDir)) wxMkdir(iconDir);

  wxString layerDir = pluginDir + "Layers/";
  if (!wxDir::Exists(layerDir)) wxMkdir(layerDir);

  g_pLayerDir = new wxString(layerDir);

  m_ptpicons = new tpicons(this);
  m_pSignalKNotesManager = new tpSignalKNotesManager(this);
  m_lastViewPortValid = false;
}

signalk_notes_opencpn_pi::~signalk_notes_opencpn_pi() {
  delete g_PrivateDataDir;
  delete g_pLayerDir;
  if (m_pSignalKNotesManager) delete m_pSignalKNotesManager;
}

// -----------------------------------------------------------------------------
// Viewport / Cluster-Zoom
// -----------------------------------------------------------------------------

void signalk_notes_opencpn_pi::MoveViewportTowardsCluster(PlugIn_ViewPort& vp) {
  const double zoomFactor = 2.0;

  SKN_LOG(this, "MoveViewportTowardsCluster ENTER scale=%.3f", vp.view_scale_ppm);

  bool allVisible = AreAllNotesVisibleAfterNextZoom(vp, zoomFactor);
  SKN_LOG(this, "MoveViewportTowardsCluster allVisible=%d", allVisible);

  if (!allVisible) {
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

    JumpToPosition(newCenterLat, newCenterLon, vp.view_scale_ppm);
    return;
  }

  double newScale = vp.view_scale_ppm * zoomFactor;
  JumpToPosition(m_clusterZoom.targetLat, m_clusterZoom.targetLon, newScale);
  m_clusterZoom.active = false;
}

void signalk_notes_opencpn_pi::ProcessClusterPanStep() {
  if (!m_lastViewPortValid) return;

  PlugIn_ViewPort vp = m_lastViewPort;
  MoveViewportTowardsCluster(vp);

  if (m_clusterZoom.notes.size() <= 1) {
    m_clusterZoom.active = false;
  }
}

// -----------------------------------------------------------------------------
// Metadata
// -----------------------------------------------------------------------------

int signalk_notes_opencpn_pi::GetAPIVersionMajor() { return OCPN_API_VERSION_MAJOR; }
int signalk_notes_opencpn_pi::GetAPIVersionMinor() { return OCPN_API_VERSION_MINOR; }

int signalk_notes_opencpn_pi::GetPlugInVersionMajor() { return PLUGIN_VERSION_MAJOR; }
int signalk_notes_opencpn_pi::GetPlugInVersionMinor() { return PLUGIN_VERSION_MINOR; }
int signalk_notes_opencpn_pi::GetPlugInVersionPatch() { return PLUGIN_VERSION_PATCH; }
int signalk_notes_opencpn_pi::GetPlugInVersionPost()  { return PLUGIN_VERSION_TWEAK; }

wxString signalk_notes_opencpn_pi::GetCommonName()        { return _T(PLUGIN_COMMON_NAME); }
wxString signalk_notes_opencpn_pi::GetShortDescription()  { return _(PLUGIN_SHORT_DESCRIPTION); }
wxString signalk_notes_opencpn_pi::GetLongDescription()   { return _(PLUGIN_LONG_DESCRIPTION); }

// -----------------------------------------------------------------------------
// Init / DeInit
// -----------------------------------------------------------------------------

int signalk_notes_opencpn_pi::Init(void) {

  AddLocaleCatalog(PLUGIN_CATALOG_NAME);

  m_parent_window = GetOCPNCanvasWindow();
  m_pTPConfig = GetOCPNConfigObject();

  LoadConfig();

#ifdef PLUGIN_USE_SVG
  m_signalk_notes_opencpn_button_id = InsertPlugInToolSVG(
      _("SignalK Notes"),
      m_ptpicons->m_s_signalk_notes_opencpn_grey_pi,
      m_ptpicons->m_s_signalk_notes_opencpn_pi,
      m_ptpicons->m_s_signalk_notes_opencpn_toggled_pi,
      wxITEM_CHECK, _("SignalK Notes"), wxS(""), nullptr, -1, 0, this);
#else
  m_signalk_notes_opencpn_button_id = InsertPlugInTool(
      _("SignalK Notes"),
      &m_ptpicons->m_bm_signalk_notes_opencpn_grey_pi,
      &m_ptpicons->m_bm_signalk_notes_opencpn_pi,
      wxITEM_CHECK, _("SignalK Notes"), wxS(""), nullptr, -1, 0, this);
#endif

  g_pFontTitle = GetOCPNScaledFont_PlugIn("tp_Title");
  g_pFontLabel = GetOCPNScaledFont_PlugIn("tp_Label");
  g_pFontData  = GetOCPNScaledFont_PlugIn("tp_Data");
  g_pFontSmall = GetOCPNScaledFont_PlugIn("tp_Small");

  return (WANTS_CURSOR_LATLON | WANTS_TOOLBAR_CALLBACK | INSTALLS_TOOLBAR_TOOL |
          INSTALLS_TOOLBOX_PAGE | INSTALLS_CONTEXTMENU_ITEMS |
          WANTS_OVERLAY_CALLBACK | WANTS_OPENGL_OVERLAY_CALLBACK |
          WANTS_PLUGIN_MESSAGING | WANTS_LATE_INIT | WANTS_MOUSE_EVENTS |
          WANTS_KEYBOARD_EVENTS | WANTS_ONPAINT_VIEWPORT | WANTS_PREFERENCES);
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

// -----------------------------------------------------------------------------
// Toolbar
// -----------------------------------------------------------------------------

void signalk_notes_opencpn_pi::OnToolbarToolCallback(int id) {

  if (!m_pSignalKNotesManager->GetAuthToken().IsEmpty()) {
    if (!m_pSignalKNotesManager->ValidateToken()) {
      m_pSignalKNotesManager->SetAuthToken("");
      m_pSignalKNotesManager->ClearAuthRequest();
      SaveConfig();
    }
  }

  if (m_pSignalKNotesManager) {
    m_pSignalKNotesManager->CleanupDisabledProviders();
  }

  if (m_pConfigDialog) {
    m_pConfigDialog->Destroy();
    m_pConfigDialog = nullptr;
  }

  m_pConfigDialog = new tpConfigDialog(this, GetOCPNCanvasWindow());
  m_pConfigDialog->ShowModal();

  if (m_pConfigDialog->GetReturnCode() == wxID_OK) {

    m_pSignalKNotesManager->SetProviderSettings(
        m_pConfigDialog->GetProviderSettings());

    m_pSignalKNotesManager->SetIconMappings(
        m_pConfigDialog->GetIconMappings());

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

void signalk_notes_opencpn_pi::OnToolbarToolDownCallback(int id) {}
void signalk_notes_opencpn_pi::OnToolbarToolUpCallback(int id) {
  m_ptpicons->SetScaleFactor();
}

void signalk_notes_opencpn_pi::UpdateOverviewDialog() {
  if (!m_lastViewPortValid) return;

  m_pSignalKNotesManager->UpdateDisplayedIcons(
      m_lastViewPort.clat, m_lastViewPort.clon,
      m_lastViewPort.view_scale_ppm * 1000);

  int count = m_pSignalKNotesManager->GetVisibleIconCount(m_lastViewPort);

  if (m_pOverviewDialog)
    m_pOverviewDialog->UpdateVisibleCount(count);
}

// -----------------------------------------------------------------------------
// Rendering — BLOCK 1 ends here
// -----------------------------------------------------------------------------

bool signalk_notes_opencpn_pi::RenderOverlay(wxDC& dc, PlugIn_ViewPort* vp) {

  if (!vp || !m_pSignalKNotesManager) return false;

  m_lastViewPort = *vp;
  m_lastViewPortValid = true;

  if (m_clusterZoom.active) {
    if (m_clusterZoom.justStarted)
      m_clusterZoom.justStarted = false;
    else
      ProcessClusterPanStep();
  }

  double centerLat = vp->clat;
  double centerLon = vp->clon;
  double maxDistance = CalculateMaxDistance(*vp);

  wxLongLong now = wxGetLocalTimeMillis();

  if (m_lastFetchTime == 0 ||
      (now - m_lastFetchTime).ToLong() > 30000 ||
      fabs(centerLat - m_lastFetchCenterLat) > 0.01 ||
      fabs(centerLon - m_lastFetchCenterLon) > 0.01 ||
      fabs(maxDistance - m_lastFetchDistance) > maxDistance * 0.2) {

    m_pSignalKNotesManager->UpdateDisplayedIcons(centerLat, centerLon, maxDistance);

    m_lastFetchCenterLat = centerLat;
    m_lastFetchCenterLon = centerLon;
    m_lastFetchDistance  = maxDistance;
    m_lastFetchTime      = now;
  }

  std::vector<const SignalKNote*> visibleNotes;
  m_pSignalKNotesManager->GetVisibleNotes(visibleNotes);

  if (visibleNotes.empty()) return false;

  m_currentClusters = BuildClusters(visibleNotes, *vp);

  bool drewSomething = false;

  for (const auto& cluster : m_currentClusters) {

    if (cluster.notes.size() == 1) {

      const SignalKNote* note = cluster.notes[0];
      wxBitmap bmp;

      if (!m_pSignalKNotesManager->GetIconBitmapForNote(*note, bmp))
        continue;

#ifdef __OCPN__ANDROID__
      wxBitmap drawBmp = bmp;
#else
      wxBitmap drawBmp = PrepareIconBitmapForGL(bmp, bmp.GetWidth());
#endif

      dc.DrawBitmap(drawBmp,
                    cluster.screenPos.x - drawBmp.GetWidth() / 2,
                    cluster.screenPos.y - drawBmp.GetHeight() / 2,
                    true);

      drewSomething = true;

    } else {

      wxBitmap clusterBmp = CreateClusterBitmap(cluster.notes.size());

      dc.DrawBitmap(clusterBmp,
                    cluster.screenPos.x - clusterBmp.GetWidth() / 2,
                    cluster.screenPos.y - clusterBmp.GetHeight() / 2,
                    true);

      drewSomething = true;
    }
  }

  return drewSomething;
}

bool signalk_notes_opencpn_pi::RenderGLOverlay(wxGLContext* pcontext,
                                               PlugIn_ViewPort* vp) {
#ifdef __OCPN__ANDROID__
  SKN_LOG(this, "RenderGLOverlay called on Android -> ignored");
  return false;
#endif

  if (!vp || !m_pSignalKNotesManager) return false;

  m_lastViewPort = *vp;
  m_lastViewPortValid = true;

  if (m_clusterZoom.active) {
    if (m_clusterZoom.justStarted)
      m_clusterZoom.justStarted = false;
    else
      ProcessClusterPanStep();
  }

  double centerLat = vp->clat;
  double centerLon = vp->clon;
  double maxDistance = CalculateMaxDistance(*vp);

  wxLongLong now = wxGetLocalTimeMillis();

  if (m_lastFetchTime == 0 ||
      (now - m_lastFetchTime).ToLong() > 30000 ||
      fabs(centerLat - m_lastFetchCenterLat) > 0.01 ||
      fabs(centerLon - m_lastFetchCenterLon) > 0.01 ||
      fabs(maxDistance - m_lastFetchDistance) > maxDistance * 0.2) {

    m_pSignalKNotesManager->UpdateDisplayedIcons(centerLat, centerLon, maxDistance);

    m_lastFetchCenterLat = centerLat;
    m_lastFetchCenterLon = centerLon;
    m_lastFetchDistance  = maxDistance;
    m_lastFetchTime      = now;
  }

  std::vector<const SignalKNote*> visibleNotes;
  m_pSignalKNotesManager->GetVisibleNotes(visibleNotes);

  if (visibleNotes.empty()) return false;

  m_currentClusters = BuildClusters(visibleNotes, *vp);

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

    } else {

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

// -----------------------------------------------------------------------------
// Mouse / Keyboard
// -----------------------------------------------------------------------------

bool signalk_notes_opencpn_pi::KeyboardEventHook(wxKeyEvent& event) {
  return false;
}

bool signalk_notes_opencpn_pi::MouseEventHook(wxMouseEvent& event) {

  if (!event.LeftDown()) return false;
  if (!m_pSignalKNotesManager || !m_lastViewPortValid) return false;

  wxPoint mousePos = event.GetPosition();
  double clickLat, clickLon;
  GetCanvasLLPix(&m_lastViewPort, mousePos, &clickLat, &clickLon);

  SKN_LOG(this, "Mouse clicked at lat=%.6f lon=%.6f screen(%d,%d)",
          clickLat, clickLon, mousePos.x, mousePos.y);

  auto pxToMeters = [&](int px) { return px * m_lastViewPort.view_scale_ppm; };

  const double baseScale = 800.0;
  const double baseTolMeters = 10.0;
  const double scaleFactor = (double)m_lastViewPort.chart_scale / baseScale;
  const double tolMeters = baseTolMeters * scaleFactor;

  const double metersPerDegLat = 111320.0;
  const double metersPerDegLonClick =
      metersPerDegLat * cos(clickLat * M_PI / 180.0);

  auto computeBoxAroundClick = [&](int pixelSize) {
    double halfSizeMeters = pxToMeters(pixelSize) / 2.0 + tolMeters;

    double dLat = halfSizeMeters / metersPerDegLat;
    double dLon = halfSizeMeters / metersPerDegLonClick;

    struct Box { double minLat, maxLat, minLon, maxLon; };
    return Box{clickLat - dLat, clickLat + dLat,
               clickLon - dLon, clickLon + dLon};
  };

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

  SKN_LOG(this, "No icon or cluster hit");
  return false;
}

// -----------------------------------------------------------------------------
// Config
// -----------------------------------------------------------------------------

void signalk_notes_opencpn_pi::SaveConfig() {

  wxFileConfig* pConf = m_pTPConfig;
  if (!pConf) return;

  pConf->SetPath("/Settings/signalk_notes_opencpn_pi");

  pConf->Write("SignalKHost", m_pSignalKNotesManager->GetServerHost());
  pConf->Write("SignalKPort", m_pSignalKNotesManager->GetServerPort());

  pConf->SetPath("/Settings/signalk_notes_opencpn_pi/Providers");

  for (auto& it : m_pSignalKNotesManager->GetProviderSettings())
    pConf->Write(it.first, it.second);

  pConf->SetPath("/Settings/signalk_notes_opencpn_pi/IconMappings");

  for (auto& it : m_pSignalKNotesManager->GetIconMappings())
    pConf->Write(it.first, it.second);

  pConf->SetPath("/Settings/signalk_notes_opencpn_pi");

  pConf->Write("AuthToken", m_pSignalKNotesManager->GetAuthToken());
  pConf->Write("AuthRequestHref", m_pSignalKNotesManager->GetAuthRequestHref());
  pConf->Write("ClientUUID", m_clientUUID);

  if (m_pConfigDialog) {

    m_pTPConfig->Write("DisplaySettings/IconSize",
                       m_pConfigDialog->GetIconSize());

    m_pTPConfig->Write("DisplaySettings/ClusterSize",
                       m_pConfigDialog->GetClusterSize());

    m_pTPConfig->Write("DisplaySettings/ClusterRadius",
                       m_pConfigDialog->GetClusterRadius());

    wxColour clusterColor = m_pConfigDialog->GetClusterColor();
    m_pTPConfig->Write("DisplaySettings/ClusterColorR", (int)clusterColor.Red());
    m_pTPConfig->Write("DisplaySettings/ClusterColorG", (int)clusterColor.Green());
    m_pTPConfig->Write("DisplaySettings/ClusterColorB", (int)clusterColor.Blue());

    wxColour textColor = m_pConfigDialog->GetClusterTextColor();
    m_pTPConfig->Write("DisplaySettings/ClusterTextColorR", (int)textColor.Red());
    m_pTPConfig->Write("DisplaySettings/ClusterTextColorG", (int)textColor.Green());
    m_pTPConfig->Write("DisplaySettings/ClusterTextColorB", (int)textColor.Blue());

    m_pTPConfig->Write("DisplaySettings/ClusterFontSize",
                       m_pConfigDialog->GetClusterFontSize());

    m_pTPConfig->Write("DisplaySettings/DebugMode", (long)m_debugMode);
  }
}

void signalk_notes_opencpn_pi::LoadConfig() {

  wxFileConfig* pConf = m_pTPConfig;
  if (!pConf) return;

  pConf->SetPath("/Settings/signalk_notes_opencpn_pi");

  wxString host;
  int port;

  pConf->Read("SignalKHost", &host, "192.168.188.25");
  pConf->Read("SignalKPort", &port, 4000);

  m_pSignalKNotesManager->SetServerDetails(host, port);

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

  pConf->SetPath("/Settings/signalk_notes_opencpn_pi");

  wxString authToken;
  wxString requestHref;

  pConf->Read("AuthToken", &authToken, wxEmptyString);
  pConf->Read("AuthRequestHref", &requestHref, wxEmptyString);

  m_pSignalKNotesManager->SetAuthToken(authToken);

  if (!requestHref.IsEmpty())
    m_pSignalKNotesManager->SetAuthRequestHref(requestHref);
  else
    m_pSignalKNotesManager->ClearAuthRequest();

  pConf->Read("ClientUUID", &m_clientUUID, "");

  if (m_clientUUID.IsEmpty()) {

#ifdef __WXMSW__
    UUID uuid;
    UuidCreate(&uuid);

    RPC_CSTR str = nullptr;
    UuidToStringA(&uuid, &str);

    if (str) {
      m_clientUUID = wxString(reinterpret_cast<char*>(str));
      RpcStringFreeA(&str);
    }

#else
    uuid_t binuuid;
    uuid_generate_random(binuuid);

    char uuid_str[37];
    uuid_unparse_lower(binuuid, uuid_str);

    m_clientUUID = wxString(uuid_str);
#endif

    pConf->Write("ClientUUID", m_clientUUID);
    SKN_LOG(this, "Generated new client UUID: %s", m_clientUUID);
  }

  m_iconSize = m_pTPConfig->Read("DisplaySettings/IconSize",
                                 (long)tpConfigDialog::DEFAULT_ICON_SIZE);

  m_clusterSize = m_pTPConfig->Read("DisplaySettings/ClusterSize",
                                    (long)tpConfigDialog::DEFAULT_CLUSTER_SIZE);

  m_clusterRadius =
      m_pTPConfig->Read("DisplaySettings/ClusterRadius",
                        (long)tpConfigDialog::DEFAULT_CLUSTER_RADIUS);

  int r = m_pTPConfig->Read("DisplaySettings/ClusterColorR",
                            (long)tpConfigDialog::DEFAULT_CLUSTER_COLOR.Red());

  int g = m_pTPConfig->Read("DisplaySettings/ClusterColorG",
                            (long)tpConfigDialog::DEFAULT_CLUSTER_COLOR.Green());

  int b = m_pTPConfig->Read("DisplaySettings/ClusterColorB",
                            (long)tpConfigDialog::DEFAULT_CLUSTER_COLOR.Blue());

  m_clusterColor = wxColour(r, g, b);

  r = m_pTPConfig->Read("DisplaySettings/ClusterTextColorR",
                        (long)tpConfigDialog::DEFAULT_CLUSTER_TEXT_COLOR.Red());

  g = m_pTPConfig->Read("DisplaySettings/ClusterTextColorG",
                        (long)tpConfigDialog::DEFAULT_CLUSTER_TEXT_COLOR.Green());

  b = m_pTPConfig->Read("DisplaySettings/ClusterTextColorB",
                        (long)tpConfigDialog::DEFAULT_CLUSTER_TEXT_COLOR.Blue());

  m_clusterTextColor = wxColour(r, g, b);

  m_clusterFontSize =
      m_pTPConfig->Read("DisplaySettings/ClusterFontSize",
                        (long)tpConfigDialog::DEFAULT_CLUSTER_FONT_SIZE);

  m_debugMode = m_pTPConfig->Read("DisplaySettings/DebugMode", (long)0);
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

  double vpWidth  = vp.pix_width;
  double vpHeight = vp.pix_height;

  double diagonalPixels =
      std::sqrt(vpWidth * vpWidth + vpHeight * vpHeight) / 2.0;

  double metersPerPixel = vp.view_scale_ppm;

  if (metersPerPixel > 0) {
    double maxDistance = diagonalPixels / metersPerPixel;
    return std::ceil(maxDistance);
  }

  double latSpan = vpHeight * vp.view_scale_ppm / 111000.0;
  double maxDistanceKm = latSpan * 111.0 / 2.0;

  return std::ceil(maxDistanceKm * 1000.0);
}

int signalk_notes_opencpn_pi::GetVisibleNoteCount() const {
  std::vector<const SignalKNote*> notes;
  m_pSignalKNotesManager->GetVisibleNotes(notes);
  return notes.size();
}

std::vector<signalk_notes_opencpn_pi::NoteCluster>
signalk_notes_opencpn_pi::BuildClusters(
    const std::vector<const SignalKNote*>& notes,
    const PlugIn_ViewPort& vp,
    int clusterRadius) {

  std::vector<NoteCluster> clusters;
  std::vector<bool> clustered(notes.size(), false);

  PlugIn_ViewPort vpCopy = vp;

  for (size_t i = 0; i < notes.size(); i++) {

    if (clustered[i]) continue;

    wxPoint p1;
    GetCanvasPixLL(&vpCopy, &p1, notes[i]->latitude, notes[i]->longitude);

    NoteCluster cluster;
    cluster.notes.push_back(notes[i]);
    clustered[i] = true;

    for (size_t j = i + 1; j < notes.size(); j++) {

      if (clustered[j]) continue;

      wxPoint p2;
      GetCanvasPixLL(&vpCopy, &p2, notes[j]->latitude, notes[j]->longitude);

      int dx = p2.x - p1.x;
      int dy = p2.y - p1.y;

      double dist = std::sqrt(dx * dx + dy * dy);

      if (dist < clusterRadius) {
        cluster.notes.push_back(notes[j]);
        clustered[j] = true;
      }
    }

    double sumLat = 0, sumLon = 0;

    for (const auto* note : cluster.notes) {
      sumLat += note->latitude;
      sumLon += note->longitude;
    }

    cluster.centerLat = sumLat / cluster.notes.size();
    cluster.centerLon = sumLon / cluster.notes.size();

    GetCanvasPixLL(&vpCopy, &cluster.screenPos,
                   cluster.centerLat, cluster.centerLon);

    clusters.push_back(cluster);
  }

  SKN_LOG(this, "BuildClusters created %zu clusters", clusters.size());
  return clusters;
}

void signalk_notes_opencpn_pi::OnClusterClick(const NoteCluster& cluster) {

  SKN_LOG(this, "===== ClusterClick START =====");
  SKN_LOG(this, "Cluster with %zu notes", cluster.notes.size());

  m_clusterZoom.notes = cluster.notes;
  m_clusterZoom.targetLat = cluster.centerLat;
  m_clusterZoom.targetLon = cluster.centerLon;
  m_clusterZoom.active = true;
  m_clusterZoom.justStarted = true;

  SKN_LOG(this, "===== ClusterClick END =====");

  RequestRefresh(m_parent_window);
}

void signalk_notes_opencpn_pi::SetDisplaySettings(
    int iconSize, int clusterSize, int clusterRadius,
    const wxColour& clusterColor, const wxColour& textColor, int fontSize) {

  m_iconSize = iconSize;
  m_clusterSize = clusterSize;
  m_clusterRadius = clusterRadius;
  m_clusterColor = clusterColor;
  m_clusterTextColor = textColor;
  m_clusterFontSize = fontSize;
}

wxBitmap signalk_notes_opencpn_pi::CreateClusterBitmap(size_t count) {
  int size = m_clusterSize;
  int radius = m_clusterRadius;
  wxColour circleColor = m_clusterColor;
  wxColour textColor = m_clusterTextColor;
  int fontSize = m_clusterFontSize;

  const int centerX = size / 2;
  const int centerY = size / 2;

#ifdef __OCPN__ANDROID__
  // Android: ohne wxGraphicsContext, nur wxDC
  wxBitmap bmp(size, size, 32);
  wxMemoryDC dc;
  dc.SelectObject(bmp);

  dc.SetBackground(*wxTRANSPARENT_BRUSH);
  dc.Clear();

  dc.SetBrush(wxBrush(circleColor));
  dc.SetPen(*wxBLACK_PEN);
  dc.DrawCircle(centerX, centerY, radius);

  wxFont font(fontSize, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL,
              wxFONTWEIGHT_BOLD, false, "Arial");
  dc.SetFont(font);
  dc.SetTextForeground(textColor);

  wxString text = wxString::Format("%zu", count);
  wxSize ts = dc.GetTextExtent(text);
  dc.DrawText(text, centerX - ts.GetWidth() / 2,
              centerY - ts.GetHeight() / 2);

  dc.SelectObject(wxNullBitmap);
  return bmp;

#else
  wxBitmap bmp(size, size, 32);
  bmp.UseAlpha(true);

  wxMemoryDC dc;
  dc.SelectObject(bmp);

  dc.SetBackground(*wxTRANSPARENT_BRUSH);
  dc.Clear();

  wxGraphicsContext* gc = wxGraphicsContext::Create(dc);
  if (!gc) {
    dc.SelectObject(wxNullBitmap);
    return bmp;
  }

#if defined(wxANTIALIAS_DEFAULT)
  gc->SetAntialiasMode(wxANTIALIAS_DEFAULT);
#endif

  wxColour fill(circleColor.Red(), circleColor.Green(), circleColor.Blue(), 200);
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
  gc->DrawText(text, centerX - tw / 2, centerY - th / 2);

  delete gc;
  dc.SelectObject(wxNullBitmap);

  wxImage img = bmp.ConvertToImage();
  img = img.Mirror(false);
  wxBitmap flipped(img);
  flipped.UseAlpha(true);

  return flipped;
#endif
}

wxBitmap signalk_notes_opencpn_pi::PrepareIconBitmapForGL(const wxBitmap& src,
                                                          int targetSize) {
#ifdef __OCPN__ANDROID__
  // Android: kein GL/Y-Flip, nur skalieren
  if (targetSize <= 0 ||
      (src.GetWidth() == targetSize && src.GetHeight() == targetSize)) {
    return src;
  }

  wxImage img = src.ConvertToImage();
  img.InitAlpha();
  wxImage scaled = img.Scale(targetSize, targetSize, wxIMAGE_QUALITY_HIGH);
  return wxBitmap(scaled);

#else
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

#if defined(wxANTIALIAS_DEFAULT)
  gc->SetAntialiasMode(wxANTIALIAS_DEFAULT);
#endif

  wxImage img = src.ConvertToImage();
  img.InitAlpha();
  wxImage scaled = img.Scale(targetSize, targetSize, wxIMAGE_QUALITY_HIGH);

  wxBitmap scaledBmp(scaled);
  gc->DrawBitmap(scaledBmp, 0, 0, targetSize, targetSize);

  delete gc;
  dc.SelectObject(wxNullBitmap);

  wxImage flipped = bmp.ConvertToImage().Mirror(false);
  wxBitmap finalBmp(flipped);
  finalBmp.UseAlpha(true);

  return finalBmp;
#endif
}

#if defined(ocpnUSE_GL) && !defined(__OCPN__ANDROID__)

// Desktop OpenGL
void signalk_notes_opencpn_pi::DrawGLBitmap(const wxBitmap& bmp, int x, int y) {
  wxImage img = bmp.ConvertToImage();
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

#elif defined(__OCPN__ANDROID__)

// Android: kein GL-Rendering
void signalk_notes_opencpn_pi::DrawGLBitmap(const wxBitmap&, int, int) {
  // noop
}

#else

void signalk_notes_opencpn_pi::DrawGLBitmap(const wxBitmap&, int, int) {
  // noop
}

#endif

// -----------------------------------------------------------------------------
// Cluster-Zoom Hilfsfunktionen
// -----------------------------------------------------------------------------

struct FutureViewPort {
  double lat_min, lat_max;
  double lon_min, lon_max;
};

static FutureViewPort MakeFutureViewportForCluster(const PlugIn_ViewPort& vp,
                                                   double centerLat,
                                                   double centerLon,
                                                   double zoomFactor) {

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

bool signalk_notes_opencpn_pi::AreAllNotesVisibleAfterNextZoom(
    const PlugIn_ViewPort& vp, double zoomFactor) const {

  if (m_clusterZoom.notes.empty()) return false;

  double centerLat = m_clusterZoom.targetLat;
  double centerLon = m_clusterZoom.targetLon;

  FutureViewPort fvp =
      MakeFutureViewportForCluster(vp, centerLat, centerLon, zoomFactor);

  bool allInside = true;

  for (const auto* note : m_clusterZoom.notes) {

    double lat = note->latitude;
    double lon = note->longitude;

    bool inside = !(lat < fvp.lat_min || lat > fvp.lat_max ||
                    lon < fvp.lon_min || lon > fvp.lon_max);

    if (!inside) allInside = false;
  }

  return allInside;
}

// -----------------------------------------------------------------------------
// Preferences
// -----------------------------------------------------------------------------

bool signalk_notes_opencpn_pi::HasOptions() { return true; }

void signalk_notes_opencpn_pi::ShowPreferencesDialog(wxWindow* parent) {
  tpConfigDialog dlg(this, parent);
  dlg.ShowModal();
}

wxWindow* signalk_notes_opencpn_pi::GetParentWindow() {
  return GetOCPNCanvasWindow() ? GetOCPNCanvasWindow() : m_parent_window;
}
