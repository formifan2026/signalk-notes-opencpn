#ifndef _SIGNALK_NOTES_OPENCPN_PI_H_
#define _SIGNALK_NOTES_OPENCPN_PI_H_

#include "ocpn_plugin.h"
#include <wx/string.h>
#include <vector>

class tpicons;
class tpSignalKNotesManager;
class tpConfigDialog;
class SignalKNote;

class signalk_notes_opencpn_pi : public opencpn_plugin_118 {
public:
  signalk_notes_opencpn_pi(void* ppimgr);
  ~signalk_notes_opencpn_pi() override;

  // ---------------------------------------------------------
  // CLUSTER-STRUKTUR
  // ---------------------------------------------------------
  struct NoteCluster {
    std::vector<const SignalKNote*> notes;
    double centerLat = 0.0;
    double centerLon = 0.0;
    wxPoint screenPos;
  };

struct ClusterZoomState {
    bool active = false;
    bool justStarted = false;

    // Zielpunkt des Clusters (aus OnClusterClick)
    double targetLat = 0.0;
    double targetLon = 0.0;

    // Mittelpunkt während des Pan/Zoom-Prozesses
    double centerLat = 0.0;
    double centerLon = 0.0;

    // Alle Notes, die zu diesem Cluster gehören
    std::vector<const SignalKNote*> notes;
};


  ClusterZoomState m_clusterZoom;

  // Bitmap-Erzeugung / GL-Vorbereitung
  wxBitmap PrepareIconBitmapForGL(const wxBitmap& src, int targetSize);
  wxBitmap CreateClusterBitmap(size_t count);
  void DrawGLBitmap(const wxBitmap& bmp, int x, int y);

  // Plugin-Interface
  void ShowPreferencesDialog(wxWindow* parent) override;
  int Init(void) override;
  bool DeInit(void) override;

  int GetAPIVersionMajor() override;
  int GetAPIVersionMinor() override;

  int GetPlugInVersionMajor() override;
  int GetPlugInVersionMinor() override;
  int GetPlugInVersionPatch() override;
  int GetPlugInVersionPost() override;

  wxString GetCommonName() override;
  wxString GetShortDescription() override;
  wxString GetLongDescription() override;

  int GetToolbarToolCount(void) override { return 1; }

  void OnToolbarToolCallback(int id) override;
  void OnToolbarToolDownCallback(int id) override;
  void OnToolbarToolUpCallback(int id) override;

  bool RenderOverlay(wxDC& dc, PlugIn_ViewPort* vp) override;
  bool RenderGLOverlay(wxGLContext* pcontext, PlugIn_ViewPort* vp) override;

  bool RenderOverlayMultiCanvas(wxDC& dc, PlugIn_ViewPort* vp,
                                int canvasIndex) override;
  bool RenderGLOverlayMultiCanvas(wxGLContext* pcontext, PlugIn_ViewPort* vp,
                                  int canvasIndex) override;

  bool MouseEventHook(wxMouseEvent& event) override;
  bool KeyboardEventHook(wxKeyEvent& event) override;

  void LateInit(void) override;

  wxBitmap* GetPlugInBitmap() override;

  // Config
  void SaveConfig();
  void LoadConfig();

  // Utility
  void appendOSDirSlash(wxString* pString);
  double CalculateMaxDistance(PlugIn_ViewPort& vp);
  void UpdateOverviewDialog();
  wxString GetPluginIconDir() const;
  int GetVisibleNoteCount() const;

  // Public state
  tpSignalKNotesManager* m_pSignalKNotesManager = nullptr;
  PlugIn_ViewPort m_lastViewPort;
  bool m_lastViewPortValid = false;
  wxWindow* m_parent_window = nullptr;
  wxString m_clientUUID;

  void SetDisplaySettings(int iconSize, int clusterSize, int clusterRadius,
                          const wxColour& clusterColor,
                          const wxColour& textColor, int fontSize);

  void ProcessClusterPanStep();

private:
  // Config + UI
  wxFileConfig* m_pTPConfig = nullptr;
  int m_signalk_notes_opencpn_button_id = -1;

  tpicons* m_ptpicons = nullptr;
  tpConfigDialog* m_pOverviewDialog = nullptr;
  tpConfigDialog* m_pConfigDialog = nullptr;
  bool m_btpDialog = false;

  // Fetch-Tracking
  double m_lastFetchCenterLat = 0.0;
  double m_lastFetchCenterLon = 0.0;
  double m_lastFetchDistance = 0.0;
  wxLongLong m_lastFetchTime = 0;

  // Clustering
  std::vector<NoteCluster> BuildClusters(
      const std::vector<const SignalKNote*>& notes, const PlugIn_ViewPort& vp,
      int clusterRadius = 60);

  void OnClusterClick(const NoteCluster& cluster);

  std::vector<NoteCluster> m_currentClusters;

  // Display-Einstellungen
  int m_iconSize;
  int m_clusterSize;
  int m_clusterRadius;
  wxColour m_clusterColor;
  wxColour m_clusterTextColor;
  int m_clusterFontSize;

  bool IsClusterVisible(const PlugIn_ViewPort& vp);
  void MoveViewportTowardsCluster(PlugIn_ViewPort& vp);
  bool AreAllNotesVisibleAfterNextZoom(const PlugIn_ViewPort& vp, double zoomFactor) const;

};

#endif
