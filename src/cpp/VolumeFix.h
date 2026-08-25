#pragma once
#include "Eap2Common.h"
#include "Eap2Config.h"

struct VolumeFixProposal {
    bool valid = false;

    int32_t f_start = 0;
    std::wstring obj_name;

    double measured_integrated = -99.0;
    double measured_peak = -99.0;
    double target_lufs = -14.0;
    double peak_ceiling = -1.0;

    double suggested_gain_db = 0.0;
    double user_gain_db = 0.0;

    double predicted_integrated() const { return measured_integrated + user_gain_db; }
    double predicted_peak() const { return measured_peak + user_gain_db; }
    bool exceeds_ceiling() const { return measured_peak > -99.0 && predicted_peak() > peak_ceiling; }
};

VolumeFixProposal make_volume_fix_proposal(int32_t f_start, const std::wstring& obj_name, double measured_integrated, double measured_peak, double target_lufs, double peak_ceiling);

bool apply_volume_fix(EDIT_SECTION* edit, const VolumeFixProposal& prop);

class VolumeFixPanel {
  public:
    void create(HWND parent, HINSTANCE hinst, int32_t base_ctrl_id);
    void destroy();

    void set_proposal(const VolumeFixProposal& prop);

    bool visible() const { return m_visible; }
    int32_t height() const { return m_visible ? kPanelHeight : 0; }

    void paint_and_layout(HDC mdc, const RECT& area);

    HWND apply_button() const { return m_btn_apply; }
    HWND reset_button() const { return m_btn_reset; }
    HWND gain_edit() const { return m_edit_gain; }

    VolumeFixProposal current_proposal_with_edit() const;

    void reset_to_suggested();

  private:
    static constexpr int32_t kPanelHeight = 58;

    HWND m_parent = nullptr;
    HWND m_edit_gain = nullptr;
    HWND m_btn_apply = nullptr;
    HWND m_btn_reset = nullptr;

    VolumeFixProposal m_prop;
    bool m_visible = false;
    RECT m_last_rect = {};

    void refresh_edit_text();
    double read_edit_gain() const;
};