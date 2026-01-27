#ifndef _TP_CONFIG_DIALOG_H_
#define _TP_CONFIG_DIALOG_H_

#include <wx/wx.h>
#include <wx/checklst.h>
#include <wx/bmpcbox.h>
#include <wx/scrolwin.h>
#include <map>
#include <set>

class signalk_notes_opencpn_pi;

class tpConfigDialog : public wxDialog {
public:
  tpConfigDialog(signalk_notes_opencpn_pi* parent, wxWindow* winparent);
  ~tpConfigDialog();
  void UpdateCount(int count);
  void UpdateProviders(const std::set<wxString>& providers);
  void UpdateIconMappings(const std::set<wxString>& skIcons);

  std::map<wxString, bool> GetProviderSettings() const;
  std::map<wxString, wxString> GetIconMappings() const;

  void LoadSettings(const std::map<wxString, bool>& providers,
                    const std::map<wxString, wxString>& iconMappings);
  void UpdateVisibleCount(int count);

private:
  void InitializeAuthUI();
  void ShowAuthenticatedState();
  void ShowPendingState();
  void ShowInitialState();
  void CreateControls();
  void LoadPluginIcons();
  wxBitmap LoadSvgBitmap(const wxString& path, int size);

  // Event-Handler
  void OnIconMappingChanged(wxCommandEvent& event);
  void SaveProviderSettings();
  void OnOK(wxCommandEvent& event);
  void OnCancel(wxCommandEvent& event);

  // UI-Elemente
  wxStaticText* m_countLabel;
  wxStaticText* m_infoLabel;
  wxCheckListBox* m_providerList;

  // Icon-Mapping UI
  wxScrolledWindow* m_iconMappingPanel;
  wxFlexGridSizer* m_iconMappingSizer;

  // Map: skIconName -> BitmapComboBox*
  std::map<wxString, wxBitmapComboBox*> m_iconCombos;

  // Icon-Daten
  wxArrayString m_pluginIcons;                       // Dateinamen ohne .svg
  std::map<wxString, wxBitmap> m_pluginIconBitmaps;  // Cache für Icons
  wxString m_iconDir;  // Absoluter Pfad zu data/icons/

  // Einstellungen
  std::set<wxString> m_enabledProviders;
  std::map<wxString, wxString> m_currentIconMappings;

  signalk_notes_opencpn_pi* m_parent;

  wxButton* m_authButton;
  wxStaticText* m_authStatusLabel;
  wxStaticBitmap* m_authStatusIcon;
  wxTimer* m_authCheckTimer;

  void OnAuthButtonClick(wxCommandEvent& event);
  void OnAuthCheckTimer(wxTimerEvent& event);

  void OnCancelAuthRequest(wxCommandEvent& event);
  wxButton* m_cancelAuthButton;

  DECLARE_EVENT_TABLE()
};

#endif