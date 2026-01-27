#ifndef _TPSIGNALKNOTES_H_
#define _TPSIGNALKNOTES_H_

#include "ocpn_plugin.h"
#include "wx/wxprec.h"

#ifndef WX_PRECOMP
#include "wx/wx.h"
#endif

#include <wx/protocol/http.h>
#include <wx/sstream.h>
#include <wx/jsonval.h>
#include <wx/filename.h>
#include <vector>
#include <map>
#include <wx/string.h>
#include <set>

// Forward declaration
class signalk_notes_opencpn_pi;

class SignalKNote {
public:
  wxString id;
  wxString name;
  wxString description;
  double latitude;
  double longitude;
  wxString iconName;
  wxString url;
  wxString source;
  wxString GUID;
  bool isDisplayed;

  SignalKNote() : latitude(0.0), longitude(0.0), isDisplayed(false) {}
};

class tpSignalKNotesManager {
public:
  tpSignalKNotesManager(signalk_notes_opencpn_pi* parent);
  tpSignalKNotesManager() = default;
  ~tpSignalKNotesManager();

  // Server settings
  wxString GetServerHost() const { return m_serverHost; }
  int GetServerPort() const { return m_serverPort; }
  void SetServerDetails(const wxString& host, int port);

  // Notes
  bool FetchNotesForViewport(double centerLat, double centerLon,
                             double maxDistance);
  void UpdateDisplayedIcons(double centerLat, double centerLon,
                            double maxDistance);
  void ClearAllIcons();

  SignalKNote* GetNoteByGUID(const wxString& guid);
  void OnIconClick(const wxString& guid);

  void GetVisibleNotes(std::vector<const SignalKNote*>& outNotes);
  bool GetIconBitmapForNote(const SignalKNote& note, wxBitmap& outBitmap);
  int GetVisibleIconCount(const PlugIn_ViewPort& vp);

  // Provider & Icon mappings
  void SetProviderSettings(const std::map<wxString, bool>& settings);
  void SetIconMappings(const std::map<wxString, wxString>& mappings);

  std::map<wxString, bool> GetProviderSettings() const {
    return m_providerSettings;
  }
  std::map<wxString, wxString> GetIconMappings() const {
    return m_iconMappings;
  }

  std::set<wxString> GetDiscoveredProviders() const {
    return m_discoveredProviders;
  }
  std::set<wxString> GetDiscoveredIcons() const { return m_discoveredIcons; }

  wxString RenderHTMLDescription(const wxString& htmlText);

  // ----------------------
  // Authentication Handling
  // ----------------------
  bool RequestAuthorization();
  bool CheckAuthorizationStatus();
  bool ValidateToken();

  wxString GetAuthToken() const { return m_authToken; }
  void SetAuthToken(const wxString& token) { m_authToken = token; }

  wxString GetAuthRequestHref() const { return m_authRequestHref; }
  void SetAuthRequestHref(const wxString& href) {
    m_authRequestHref = href;
    m_authPending = !href.IsEmpty();
  }

  bool IsAuthPending() const {
    return m_authPending && !m_authRequestHref.IsEmpty();
  }

  void ClearAuthRequest() {
    m_authRequestHref.Clear();
    m_authPending = false;
  }

  bool FetchInstalledPlugins(std::map<wxString, bool>& plugins);
  void CleanupDisabledProviders();

private:
  signalk_notes_opencpn_pi* m_parent = nullptr;

  bool FetchNotesList(double centerLat, double centerLon, double maxDistance);
  bool FetchNoteDetails(const wxString& noteId, SignalKNote& note);

  wxString ResolveIconPath(const wxString& skIconName);
  bool DownloadIcon(const wxString& iconName, wxBitmap& bitmap);
  bool CreateNoteIcon(SignalKNote& note);
  bool DeleteNoteIcon(const wxString& guid);

  bool ParseNotesListJSON(const wxString& json);
  bool ParseNoteDetailsJSON(const wxString& json, SignalKNote& note);

  wxString MakeHTTPRequest(const wxString& path);
  wxString MakeAuthenticatedHTTPRequest(const wxString& path);

  // Server data
  wxString m_serverHost;
  int m_serverPort;

  // Authentication data
  wxString m_authRequestHref;  // vom Server geliefertes href
  wxString m_authToken;
  bool m_authPending = false;
  wxDateTime m_authRequestTime;

  // Notes
  std::map<wxString, SignalKNote> m_notes;
  std::vector<wxString> m_displayedGUIDs;

  std::map<wxString, wxBitmap> m_iconCache;

  std::map<wxString, bool> m_providerSettings;
  std::map<wxString, wxString> m_iconMappings;

  std::set<wxString> m_discoveredProviders;
  std::set<wxString> m_discoveredIcons;
};

#endif  // _TPSIGNALKNOTES_H_
