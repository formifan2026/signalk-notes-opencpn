#ifndef _SIGNALK_NOTES_OPENCPN_PI_H_
#define _SIGNALK_NOTES_OPENCPN_PI_H_

#include "ocpn_plugin.h"
#include <wx/string.h>

class tpicons;
class tpSignalKNotesManager;
class tpConfigDialog;
class SignalKNote;

class signalk_notes_opencpn_pi : public opencpn_plugin_118 {
public:
  signalk_notes_opencpn_pi(void* ppimgr);
  ~signalk_notes_opencpn_pi() override;

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
  struct NoteCluster {
    std::vector<const SignalKNote*> notes;
    double centerLat;
    double centerLon;
    wxPoint screenPos;
  };

  std::vector<NoteCluster> BuildClusters(
      const std::vector<const SignalKNote*>& notes, const PlugIn_ViewPort& vp,
      int clusterRadius = 60);

  void OnClusterClick(const NoteCluster& cluster);

  std::vector<NoteCluster> m_currentClusters;
};

#endif