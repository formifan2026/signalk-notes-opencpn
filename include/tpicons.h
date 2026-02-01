/******************************************************************************
 * Project:   SignalK Notes Plugin for OpenCPN
 * Purpose:   Icon definitions and icon resource declarations
 * Author:    Dirk Behrendt
 * Copyright: Copyright (c) 2024 Dirk Behrendt
 * Licence:   GPLv2
 *
 * Icon Licensing:
 *   - Some icons are derived from freeboard-sk (Apache License 2.0)
 *   - Some icons are based on OpenCPN standard icons (GPLv2)
 ******************************************************************************/
#ifndef TPICONS_H
#define TPICONS_H 1
class signalk_notes_opencpn_pi;

#include "ocpn_plugin.h"
#include <wx/wxprec.h>
#ifndef WX_PRECOMP
#include <wx/wx.h>
#endif

class tpicons {
public:
  tpicons();
  ~tpicons();

  void initialize_images(void);
  bool ScaleIcons(void);
  bool SetScaleFactor(void);
  void SetColourScheme(PI_ColorScheme cs);
  void ChangeScheme(void);

  wxBitmap m_bm_signalk_notes_opencpn_pi;
  wxBitmap m_bm_signalk_notes_opencpn_toggled_pi;
  wxBitmap m_bm_signalk_notes_opencpn_grey_pi;

  wxString m_s_signalk_notes_opencpn_pi;
  wxString m_s_signalk_notes_opencpn_toggled_pi;
  wxString m_s_signalk_notes_opencpn_grey_pi;

  bool m_bUpdateIcons;
  tpicons(signalk_notes_opencpn_pi* plugin);

private:
  signalk_notes_opencpn_pi* m_plugin;
  wxBitmap* ScaleIcon(wxBitmap bitmap, double sf);
  void CreateSchemeIcons(void);
  wxBitmap BuildDimmedToolBitmap(wxBitmap bmp_normal, unsigned char dim_ratio);

#ifdef PLUGIN_USE_SVG
  wxBitmap LoadSVG(const wxString filename, int width = -1, int height = -1);
  wxBitmap ScaleIcon(wxBitmap bitmap, const wxString filename, double sf);
#endif

  wxBitmap m_bm_day_signalk_notes_opencpn_pi;
  wxBitmap m_bm_day_signalk_notes_opencpn_toggled_pi;
  wxBitmap m_bm_day_signalk_notes_opencpn_grey_pi;

  wxBitmap m_bm_dusk_signalk_notes_opencpn_pi;
  wxBitmap m_bm_dusk_signalk_notes_opencpn_toggled_pi;
  wxBitmap m_bm_dusk_signalk_notes_opencpn_grey_pi;

  wxBitmap m_bm_night_signalk_notes_opencpn_pi;
  wxBitmap m_bm_night_signalk_notes_opencpn_toggled_pi;
  wxBitmap m_bm_night_signalk_notes_opencpn_grey_pi;

  double m_dScaleFactor;
  PI_ColorScheme m_ColourScheme;
  bool m_failedBitmapLoad;

  int m_iDisplayScaleFactor;
  int m_iToolScaleFactor;
  int m_iImageRefSize;
};

#endif /* TPICONS_H */
