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
#include <wx/listctrl.h>
#include <algorithm>

#include <wx/dcclient.h>
#include <wx/display.h>
#include <wx/window.h>

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
#if defined(_WIN32) || defined(WIN32) || defined(__WIN32__)
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

extern "C" DECL_EXP void destroy_pi(opencpn_plugin* p) { delete p; }

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
// Metadata
// -----------------------------------------------------------------------------

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
          WANTS_VECTOR_CHART_OBJECT_INFO | WANTS_OVERLAY_CALLBACK |
          WANTS_OPENGL_OVERLAY_CALLBACK | WANTS_PLUGIN_MESSAGING |
          WANTS_LATE_INIT | WANTS_MOUSE_EVENTS | WANTS_KEYBOARD_EVENTS |
          WANTS_ONPAINT_VIEWPORT | WANTS_PREFERENCES);
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

  if (m_pOverviewDialog) m_pOverviewDialog->UpdateVisibleCount(count);
}

// -----------------------------------------------------------------------------
// Rendering — BLOCK 1 ends here
// -----------------------------------------------------------------------------

bool signalk_notes_opencpn_pi::RenderOverlay(wxDC& dc, PlugIn_ViewPort* vp) {
  if (!vp || !m_pSignalKNotesManager) return false;

  // ============================================================
  // GET VISIBLE NOTES AND BUILD CLUSTERS
  // ============================================================
  std::vector<const SignalKNote*> visibleNotes;
  m_pSignalKNotesManager->GetVisibleNotes(visibleNotes);

  if (visibleNotes.empty()) return false;

  m_currentClusters = BuildClusters(visibleNotes, m_lastViewPort);

  // ============================================================
  // RENDER CLUSTERS
  // ============================================================
  bool drewSomething = false;

  for (const auto& cluster : m_currentClusters) {
    if (cluster.notes.size() == 1) {
      const SignalKNote* note = cluster.notes[0];
      wxBitmap bmp;

      if (!m_pSignalKNotesManager->GetIconBitmapForNote(*note, bmp)) continue;

#ifdef __OCPN__ANDROID__
      wxBitmap drawBmp = bmp;
#else
      wxBitmap drawBmp = PrepareIconBitmapForGL(bmp, bmp.GetWidth());
#endif

      dc.DrawBitmap(drawBmp, cluster.screenPos.x - drawBmp.GetWidth() / 2,
                    cluster.screenPos.y - drawBmp.GetHeight() / 2, true);

      drewSomething = true;

    } else {
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
  SKN_LOG(this, "RenderGLOverlay - START");
#ifdef __OCPN__ANDROID__
  SKN_LOG(this, "RenderGLOverlay called on Android -> ignored");
  return false;
#endif

  if (!m_lastViewPortValid || !m_pSignalKNotesManager) return false;

  // ============================================================
  // GET VISIBLE NOTES AND BUILD CLUSTERS
  // ============================================================
  std::vector<const SignalKNote*> visibleNotes;
  m_pSignalKNotesManager->GetVisibleNotes(visibleNotes);

  if (visibleNotes.empty()) return false;

  m_currentClusters = BuildClusters(visibleNotes, m_lastViewPort);

  // ============================================================
  // RENDER CLUSTERS
  // ============================================================
  bool drewSomething = false;

  for (const auto& cluster : m_currentClusters) {
    if (cluster.notes.size() == 1) {
      const SignalKNote* note = cluster.notes[0];
      wxBitmap bmp;

      if (!m_pSignalKNotesManager->GetIconBitmapForNote(*note, bmp)) continue;

      DrawGLBitmap(bmp, cluster.screenPos.x - bmp.GetWidth() / 2,
                   cluster.screenPos.y - bmp.GetHeight() / 2);

      drewSomething = true;

    } else {
      wxBitmap clusterBmp = CreateClusterBitmap(cluster.notes.size());

      DrawGLBitmap(clusterBmp, cluster.screenPos.x - clusterBmp.GetWidth() / 2,
                   cluster.screenPos.y - clusterBmp.GetHeight() / 2);

      drewSomething = true;
    }
  }

  SKN_LOG(this, "RenderGLOverlay - END");
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
  if (!event.LeftDown()) {
    return false;
  }

  if (!m_pSignalKNotesManager || !m_lastViewPortValid) {
    return false;
  }

  wxPoint mousePos = event.GetPosition();
  wxWindow* canvas = GetOCPNCanvasWindow();

  double clickLat = 0.0, clickLon = 0.0;
  GetCanvasLLPix(&m_lastViewPort, mousePos, &clickLat, &clickLon);

  SKN_LOG(this, "Mouse clicked at lat=%.6f lon=%.6f screen(%d,%d)", clickLat,
          clickLon, mousePos.x, mousePos.y);

  // PIXEL-BASED HIT-TESTING

  struct ClickableElement {
    enum Type { NOTE, CLUSTER } type;
    double distancePixels;
    wxString noteGuid;
    size_t clusterIndex;
    wxString description;
  };

  std::vector<ClickableElement> hitElements;

  auto pixelDistance = [](wxPoint p1, wxPoint p2) {
    int dx = p2.x - p1.x;
    int dy = p2.y - p1.y;
    return std::sqrt(dx * dx + dy * dy);
  };

  auto noteIsInMultiCluster = [&](const SignalKNote* note) {
    for (const auto& c : m_currentClusters) {
      if (c.notes.size() <= 1) continue;
      for (auto* n : c.notes) {
        if (n == note) return true;
      }
    }
    return false;
  };

  std::vector<const SignalKNote*> visibleNotes;
  m_pSignalKNotesManager->GetVisibleNotes(visibleNotes);

  double noteTolerance = GetIconSize() / 2;
  double clusterTolerance = GetClusterSize() / 2;

  SKN_LOG(this, "Hit-Test tolerances: note=%.1f px, cluster=%.1f px",
          noteTolerance, clusterTolerance);

  // CLUSTER-HIT-TEST
  SKN_LOG(this, "Testing %zu clusters for hit...", m_currentClusters.size());

  for (size_t i = 0; i < m_currentClusters.size(); i++) {
    const auto& cluster = m_currentClusters[i];
    if (cluster.notes.size() <= 1) continue;

    double distPx = pixelDistance(mousePos, cluster.screenPos);

    if (distPx <= clusterTolerance) {
      ClickableElement elem;
      elem.type = ClickableElement::CLUSTER;
      elem.distancePixels = distPx;
      elem.clusterIndex = i;
      elem.description = wxString::Format("Cluster %zu (%zu notes) at %.1f px",
                                          i, cluster.notes.size(), distPx);
      hitElements.push_back(elem);

      SKN_LOG(this, "Cluster HIT: %s (tolerance=%.1f px)",
              elem.description.mb_str(), clusterTolerance);
    }
  }

  // NOTE-HIT-TEST
  SKN_LOG(this, "Testing %zu individual notes for hit...", visibleNotes.size());

  for (const SignalKNote* note : visibleNotes) {
    if (!note) continue;
    if (noteIsInMultiCluster(note)) continue;

    wxPoint noteScreenPos;
    GetCanvasPixLL(&m_lastViewPort, &noteScreenPos, note->latitude,
                   note->longitude);

    double distPx = pixelDistance(mousePos, noteScreenPos);

    if (distPx <= noteTolerance) {
      ClickableElement elem;
      elem.type = ClickableElement::NOTE;
      elem.distancePixels = distPx;
      elem.noteGuid = note->id;
      elem.description =
          wxString::Format("Note '%s' at %.1f px", note->id, distPx);
      hitElements.push_back(elem);

      SKN_LOG(this, "Note HIT: %s (tolerance=%.1f px)",
              elem.description.mb_str(), noteTolerance);
    }
  }

  // NO HITS
  if (hitElements.empty()) {
    SKN_LOG(this, "No icon or cluster hit");
    return false;
  }

  // SORTING
  std::sort(hitElements.begin(), hitElements.end(),
            [](const ClickableElement& a, const ClickableElement& b) {
              return a.distancePixels < b.distancePixels;
            });

  const ClickableElement& winner = hitElements[0];

  // TREAT WINNERS
  if (winner.type == ClickableElement::CLUSTER) {
    const auto& cluster = m_currentClusters[winner.clusterIndex];
    SKN_LOG(this, "Cluster clicked with %zu notes", cluster.notes.size());
    OnClusterClick(cluster);
    return true;
  }

  SKN_LOG(this, "Note clicked: %s", winner.noteGuid.mb_str());
  m_pSignalKNotesManager->OnIconClick(winner.noteGuid, m_lastViewPort);
  return true;
}

void signalk_notes_opencpn_pi::SaveConfig() {
  wxFileConfig* pConf = m_pTPConfig;
  if (!pConf) return;
  SKN_LOG(this, "SaveConfig called, token='%s'",  // ← NEU
          m_pSignalKNotesManager->GetAuthToken().Left(20));

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

    m_pTPConfig->Write("DisplaySettings/DebugMode", (long)m_debugMode);
  }
  // Write changes to disk
  pConf->Flush();
  SKN_LOG(this, "SaveConfig Flush done");  // ← NEU
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

double signalk_notes_opencpn_pi::CalculateMaxDistance(PlugIn_ViewPort& vp) {
  double vpWidth = vp.pix_width;
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
    const std::vector<const SignalKNote*>& notes, const PlugIn_ViewPort& vp,
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

    GetCanvasPixLL(&vpCopy, &cluster.screenPos, cluster.centerLat,
                   cluster.centerLon);

    clusters.push_back(cluster);
  }

  SKN_LOG(this, "BuildClusters created %zu clusters", clusters.size());
  return clusters;
}

void signalk_notes_opencpn_pi::OnClusterClick(const NoteCluster& cluster) {
  SKN_LOG(this, "OnClusterClick: cluster with %zu notes", cluster.notes.size());
  TryZoomToCluster(cluster);
}

void signalk_notes_opencpn_pi::TryZoomToCluster(const NoteCluster& cluster) {
  SKN_LOG(this, "TryZoomToCluster: Starting zoom for %zu notes",
          cluster.notes.size());

  if (cluster.notes.size() <= 1) return;

  const int MAX_SCALE_LIMIT = 800;
  int currentScale = (int)std::round(m_lastViewPort.chart_scale);

  if (currentScale <= MAX_SCALE_LIMIT) {
    ShowClusterSelectionDialog(cluster);
    return;
  }

  // ============================================================
  // Cluster-Zoom aktivieren und Ziel speichern
  // ============================================================
  m_clusterZoom.active = true;
  m_clusterZoom.noteIds.clear();
  for (const auto* note : cluster.notes) {
    m_clusterZoom.noteIds.push_back(note->id);  // <- IDs speichern!
  }
  m_clusterZoom.targetLat = cluster.centerLat;
  m_clusterZoom.targetLon = cluster.centerLon;

  // ============================================================
  // EINEN Zoom-Schritt ausführen
  // ============================================================
  if (m_lastViewPortValid) {
    double factor = 1.4;  // entspricht etwa einem "+"-Zoom
    double newScale = m_lastViewPort.view_scale_ppm * factor;

    SKN_LOG(this,
            "TryZoomToCluster: JumpToPosition lat=%.6f lon=%.6f scale=%.3f",
            m_clusterZoom.targetLat, m_clusterZoom.targetLon, newScale);

    JumpToPosition(m_clusterZoom.targetLat, m_clusterZoom.targetLon, newScale);
  }

  // ============================================================
  // Methode sofort beenden – weiterer Zoom erfolgt in SetCurrentViewPort()
  // ============================================================
  return;
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
  dc.DrawText(text, centerX - ts.GetWidth() / 2, centerY - ts.GetHeight() / 2);

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

  wxColour fill(circleColor.Red(), circleColor.Green(), circleColor.Blue(),
                200);
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

  glRasterPos2i(x, y + h);
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

void signalk_notes_opencpn_pi::ShowPreferencesDialog(wxWindow* parent) {
  tpConfigDialog dlg(this, parent);
  dlg.ShowModal();
}

wxWindow* signalk_notes_opencpn_pi::GetParentWindow() {
  return GetOCPNCanvasWindow() ? GetOCPNCanvasWindow() : m_parent_window;
}

void signalk_notes_opencpn_pi::ShowClusterSelectionDialog(NoteCluster cluster) {
  wxDialog* dlg =
      new wxDialog(GetParentWindow(), wxID_ANY, _("Notes an dieser Position"),
                   wxDefaultPosition, wxSize(600, 400),
                   wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);
  dlg->CenterOnScreen();

  wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);

  wxListCtrl* listCtrl =
      new wxListCtrl(dlg, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                     wxLC_REPORT | wxLC_SINGLE_SEL | wxLC_NO_HEADER);
  listCtrl->AppendColumn("", wxLIST_FORMAT_LEFT, 560);

  wxImageList* imgList = new wxImageList(24, 24, true);
  listCtrl->SetImageList(imgList, wxIMAGE_LIST_SMALL);

  for (size_t i = 0; i < cluster.notes.size(); i++) {
    const SignalKNote* note = cluster.notes[i];
    wxString label = note->name.IsEmpty() ? note->id : note->name;

    int imgIdx = -1;
    wxBitmap bmp;
    if (m_pSignalKNotesManager->GetIconBitmapForNote(*note, bmp) &&
        bmp.IsOk()) {
      wxImage img = bmp.ConvertToImage().Scale(24, 24, wxIMAGE_QUALITY_HIGH);
      img = img.Mirror(false);
      imgIdx = imgList->Add(wxBitmap(img));
    }

    listCtrl->InsertItem(i, label, imgIdx);
  }

  sizer->Add(listCtrl, 1, wxALL | wxEXPAND, 10);

  wxBoxSizer* btnSizer = new wxBoxSizer(wxHORIZONTAL);
  wxButton* okBtn = new wxButton(dlg, wxID_OK, _("OK"));
  btnSizer->Add(okBtn, 0, wxALL, 5);
  sizer->Add(btnSizer, 0, wxALIGN_RIGHT | wxALL, 5);
  dlg->SetSizer(sizer);

  wxString selectedNoteId;

  auto handler = [&](wxListEvent& evt) {
    long sel = evt.GetIndex();
    if (sel < 0 || sel >= (long)cluster.notes.size()) return;

    selectedNoteId = cluster.notes[sel]->id;
    dlg->EndModal(wxID_OK);
  };
  listCtrl->Bind(wxEVT_LIST_ITEM_SELECTED, handler);

  dlg->ShowModal();
  dlg->Destroy();

  wxWindow* canvas = GetOCPNCanvasWindow();
  if (canvas) {
    wxMouseEvent upEvent(wxEVT_LEFT_UP);
    upEvent.SetPosition(wxGetMousePosition());
    canvas->GetEventHandler()->ProcessEvent(upEvent);
  }

  if (!selectedNoteId.IsEmpty()) {
    m_pSignalKNotesManager->OnIconClick(selectedNoteId, m_lastViewPort);
    return;
  }
}

void signalk_notes_opencpn_pi::SetCurrentViewPort(PlugIn_ViewPort& vp) {
  double oldScale = m_lastViewPortValid ? m_lastViewPort.chart_scale : -1;
  m_prevChartScale = oldScale;
  m_lastViewPort = vp;
  m_lastViewPortValid = true;

  wxString lat_str = FormatDMM(m_lastViewPort.clat, true);
  wxString lon_str = FormatDMM(m_lastViewPort.clon, false);

  wxWindow* canvas = GetOCPNCanvasWindow();

  wxPoint mousePos = canvas->ScreenToClient(wxGetMousePosition());

  double mouseLat = 0.0, mouseLon = 0.0;
  GetCanvasLLPix(&m_lastViewPort, mousePos, &mouseLat, &mouseLon);

  wxString mouse_lat_str = FormatDMM(mouseLat, true);
  wxString mouse_lon_str = FormatDMM(mouseLon, false);

  SKN_LOG(
      this,
      wxString("SetCurrentViewPort: ") + "Maßstab(1:" +
          wxString::Format("%d", (int)std::round(m_lastViewPort.chart_scale)) +
          ") " +
          wxString::Format("view_scale_ppm=%.6f ",
                           m_lastViewPort.view_scale_ppm) +
          "VP lat=" + wxString::Format("%.6f", m_lastViewPort.clat) + " (" +
          lat_str + ") " + "lon=" +
          wxString::Format("%.6f", m_lastViewPort.clon) + " (" + lon_str +
          ") " + "Mouse lat=" + wxString::Format("%.6f", mouseLat) + " (" +
          mouse_lat_str + ") " + "lon=" + wxString::Format("%.6f", mouseLon) +
          " (" + mouse_lon_str + ") " + "screen(" +
          wxString::Format("%d,%d", mousePos.x, mousePos.y) + ") ");

  // PROCESS CLUSTER ZOOM
  if (m_clusterZoom.active && m_lastViewPort.chart_scale != m_prevChartScale) {
    const double zoomFactor = 1.4;
    const int MAX_SCALE_LIMIT = 800;
    int currentScale = (int)std::round(m_lastViewPort.chart_scale);

    SKN_LOG(this, "ClusterZoom: current scale 1:%d", currentScale);

    // FIND THE NOTES USING THE IDS
    std::vector<const SignalKNote*> originalNotes;
    for (const auto& id : m_clusterZoom.noteIds) {
      SignalKNote* note = m_pSignalKNotesManager->GetNoteByGUID(id);
      if (note) {
        originalNotes.push_back(note);
      }
    }

    // LIMIT REACHED?
    if (currentScale <= MAX_SCALE_LIMIT) {
      SKN_LOG(this, "ClusterZoom: Scale limit reached, showing dialog");
      m_clusterZoom.active = false;

      NoteCluster dialogCluster;
      dialogCluster.notes = originalNotes;
      dialogCluster.centerLat = m_clusterZoom.targetLat;
      dialogCluster.centerLon = m_clusterZoom.targetLon;

      ShowClusterSelectionDialog(dialogCluster);
      return;
    }

    // CALCULATE NEW CLUSTERS
    std::vector<NoteCluster> newClusters =
        BuildClusters(originalNotes, m_lastViewPort);

    // ARE THE NOTES STILL TOGETHER?
    bool stillTogether = false;
    for (const auto& nc : newClusters) {
      if (nc.notes.size() == originalNotes.size()) {
        stillTogether = true;
        break;
      }
    }

    // CLUSTER BLOWN OPEN!
    if (!stillTogether) {
      SKN_LOG(this, "ClusterZoom: Cluster successfully separated");
      m_clusterZoom.active = false;
      return;
    }

    // PERFORM THE NEXT ZOOM
    PlugIn_ViewPort nextVp = m_lastViewPort;
    double latSpan = nextVp.lat_max - nextVp.lat_min;
    double lonSpan = nextVp.lon_max - nextVp.lon_min;

    nextVp.view_scale_ppm *= zoomFactor;
    nextVp.lat_min = m_clusterZoom.targetLat - latSpan / (2.0 * zoomFactor);
    nextVp.lat_max = m_clusterZoom.targetLat + latSpan / (2.0 * zoomFactor);
    nextVp.lon_min = m_clusterZoom.targetLon - lonSpan / (2.0 * zoomFactor);
    nextVp.lon_max = m_clusterZoom.targetLon + lonSpan / (2.0 * zoomFactor);

    JumpToPosition(m_clusterZoom.targetLat, m_clusterZoom.targetLon,
                   nextVp.view_scale_ppm);
    return;
  }

  // UPDATE DISPLAYED ICONS
  double centerLat = m_lastViewPort.clat;
  double centerLon = m_lastViewPort.clon;
  double maxDistance = CalculateMaxDistance(m_lastViewPort);

  wxLongLong now = wxGetLocalTimeMillis();
  if (!m_clusterZoom.active &&
      (m_lastFetchTime == 0 || (now - m_lastFetchTime).ToLong() > 30000 ||
       fabs(centerLat - m_lastFetchCenterLat) > 0.01 ||
       fabs(centerLon - m_lastFetchCenterLon) > 0.01 ||
       fabs(maxDistance - m_lastFetchDistance) > maxDistance * 0.2)) {
    m_pSignalKNotesManager->UpdateDisplayedIcons(centerLat, centerLon,
                                                 maxDistance);

    m_lastFetchCenterLat = centerLat;
    m_lastFetchCenterLon = centerLon;
    m_lastFetchDistance = maxDistance;
    m_lastFetchTime = now;
  }
}

wxString FormatDMM(double angle, bool isLat) {
  double abs_val = fabs(angle);
  int degrees = (int)abs_val;
  double minutes = (abs_val - degrees) * 60.0;

  wxString hemi;
  if (isLat)
    hemi = angle >= 0 ? "N" : "S";
  else
    hemi = angle >= 0 ? "E" : "W";
  return wxString::Format("%02d° %07.4f' %s", degrees, minutes, hemi);
}

int ComputeScale(const PlugIn_ViewPort& vp) {
  if (vp.view_scale_ppm <= 0) {
    return 1;
  }
  double scale = 1.0 / vp.view_scale_ppm;
  return (int)scale;
}