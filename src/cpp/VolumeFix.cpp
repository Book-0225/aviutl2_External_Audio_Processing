#include "VolumeFix.h"

#include "AnalyzerColor.h"

constexpr auto TOOL_NAME = L"Volume";

FILTER_ITEM_TRACK vol_gain(L"ゲイン", 100.0, 0.0, 10000.0, 0.1, nullptr, 1.0);

void* filter_items_volume[] = {
    &vol_gain,
    nullptr
};

namespace {

void dtw(HDC hdc, const wchar_t* s, RECT r, COLORREF c, UINT fmt = DT_LEFT | DT_VCENTER | DT_SINGLELINE) {
    SetTextColor(hdc, c);
    DrawText(hdc, s, -1, &r, fmt);
}
} // namespace

VolumeFixProposal make_volume_fix_proposal(
    int32_t f_start, const std::wstring& obj_name,
    double measured_integrated, double measured_peak,
    double target_lufs, double peak_ceiling) {
    VolumeFixProposal p;
    p.f_start = f_start;
    p.obj_name = obj_name;
    p.measured_integrated = measured_integrated;
    p.measured_peak = measured_peak;
    p.target_lufs = target_lufs;
    p.peak_ceiling = peak_ceiling;
    p.valid = (measured_integrated > -99.0);
    p.suggested_gain_db = p.valid ? (target_lufs - measured_integrated) : 0.0;
    p.user_gain_db = p.suggested_gain_db;
    return p;
}

std::wstring VOLUME_EFFECT_NAME = GEN_TOOL_NAME(TOOL_NAME);
std::wstring VOLUME_ITEM_NAME = vol_gain.name;

bool apply_volume_fix(EDIT_SECTION* edit, const VolumeFixProposal& prop) {
    if (!prop.valid) return false;
    OBJECT_HANDLE obj = edit->get_selected_object(0);
    if (!obj) obj = edit->get_focus_object();
    if (!obj) return false;
    edit->create_effect(obj, VOLUME_EFFECT_NAME.c_str());
    std::wstring target = VOLUME_EFFECT_NAME + L":" + std::to_wstring(edit->count_object_effect(obj, VOLUME_EFFECT_NAME.c_str()) - 1);
    return edit->set_object_item_value(obj, target.c_str(), VOLUME_ITEM_NAME.c_str(), std::to_string(prop.user_gain_db < -140.0 ? 0.0 : (std::max)(0.0, (std::min)(1000.0, 100.0 * std::pow(10.0, prop.user_gain_db / 20.0)))).c_str());
}

void VolumeFixPanel::create(HWND parent, HINSTANCE hinst, int32_t base_ctrl_id) {
    m_parent = parent;
    m_edit_gain = CreateWindow(L"EDIT", L"0.00", WS_CHILD | ES_RIGHT | ES_AUTOHSCROLL, 0, 0, 70, 20, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(base_ctrl_id + 0)), hinst, nullptr);
    m_btn_apply = CreateWindow(L"BUTTON", TrText(L"適用"), WS_CHILD | BS_PUSHBUTTON, 0, 0, 60, 24, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(base_ctrl_id + 1)), hinst, nullptr);
    m_btn_reset = CreateWindow(L"BUTTON", TrText(L"提案値に戻す"), WS_CHILD | BS_PUSHBUTTON, 0, 0, 100, 24, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(base_ctrl_id + 2)), hinst, nullptr);
}

void VolumeFixPanel::destroy() {
    if (m_edit_gain) DestroyWindow(m_edit_gain);
    if (m_btn_apply) DestroyWindow(m_btn_apply);
    if (m_btn_reset) DestroyWindow(m_btn_reset);
    m_edit_gain = m_btn_apply = m_btn_reset = nullptr;
}

void VolumeFixPanel::refresh_edit_text() {
    if (!m_edit_gain) return;
    wchar_t buf[32];
    swprintf_s(buf, L"%.2f", m_prop.user_gain_db);
    SetWindowText(m_edit_gain, buf);
}

double VolumeFixPanel::read_edit_gain() const {
    if (!m_edit_gain) return m_prop.user_gain_db;
    wchar_t buf[32];
    GetWindowText(m_edit_gain, buf, 32);
    wchar_t* endp = nullptr;
    double v = wcstod(buf, &endp);
    if (endp == buf) return m_prop.user_gain_db;
    return std::abs(v) < 140 ? v : m_prop.user_gain_db;
}

void VolumeFixPanel::set_proposal(const VolumeFixProposal& prop) {
    m_prop = prop;
    m_visible = prop.valid;
    refresh_edit_text();
    if (m_edit_gain) ShowWindow(m_edit_gain, m_visible ? SW_SHOW : SW_HIDE);
    if (m_btn_apply) ShowWindow(m_btn_apply, m_visible ? SW_SHOW : SW_HIDE);
    if (m_btn_reset) ShowWindow(m_btn_reset, m_visible ? SW_SHOW : SW_HIDE);
    m_last_rect = {};
}

VolumeFixProposal VolumeFixPanel::current_proposal_with_edit() const {
    VolumeFixProposal p = m_prop;
    p.user_gain_db = read_edit_gain();
    return p;
}

void VolumeFixPanel::reset_to_suggested() {
    m_prop.user_gain_db = m_prop.suggested_gain_db;
    refresh_edit_text();
}

void VolumeFixPanel::paint_and_layout(HDC mdc, const RECT& area) {
    if (!m_visible) return;

    if (!EqualRect(&m_last_rect, &area)) {
        m_last_rect = area;
        const int32_t x = area.left;
        const int32_t ctrl_y = area.top + 20;
        if (m_edit_gain) SetWindowPos(m_edit_gain, nullptr, x, ctrl_y + 2, 70, 20, SWP_NOZORDER);
        if (m_btn_apply) SetWindowPos(m_btn_apply, nullptr, x + 104, ctrl_y, 60, 24, SWP_NOZORDER);
        if (m_btn_reset) SetWindowPos(m_btn_reset, nullptr, x + 172, ctrl_y, 100, 24, SWP_NOZORDER);
    }
    const VolumeFixProposal live = current_proposal_with_edit();

    dtw(mdc, TrText(L"音量の調整"), { area.left, area.top, area.left + 120, area.top + 18 }, C_LABEL);

    wchar_t preview[160];
    const bool over = live.exceeds_ceiling();
    swprintf_s(preview, L"%s%s: Integrated %.1f LUFS / Peak %.1f dBTP", over ? L"⚠ " : L"", TrText(L"適用後の予測"), live.predicted_integrated(), live.predicted_peak());
    dtw(mdc, preview, { area.left, area.top, area.right, area.top + 18 }, over ? C_RED : C_GREEN, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    dtw(mdc, L"dB", { area.left + 76, area.top + 20, area.left + 100, area.top + 44 }, C_LABEL);
}

bool func_proc_audio_volume(FILTER_PROC_AUDIO* audio) {
    int32_t total_samples = audio->object->sample_num;
    if (total_samples <= 0) return true;
    int32_t channels = (std::min)(2, audio->object->channel_num);

    float gain_val = static_cast<float>(vol_gain.value);

    if (gain_val == 100.0f)
        return true;

    thread_local std::vector<float> bufL, bufR;
    if (bufL.size() < static_cast<size_t>(total_samples)) {
        bufL.resize(total_samples);
        bufR.resize(total_samples);
    }

    if (channels >= 1) audio->get_sample_data(bufL.data(), 0);
    if (channels >= 2) audio->get_sample_data(bufR.data(), 1);
    else if (channels == 1) Avx2Utils::CopyBufferAVX2(bufR.data(), bufL.data(), total_samples);

    float gain_ratio = gain_val / 100.0f;

    if (gain_ratio != 1.0f) Avx2Utils::ScaleBufferAVX2(bufL.data(), bufL.data(), total_samples, gain_ratio);
    if (channels >= 2 && gain_ratio != 1.0f) Avx2Utils::ScaleBufferAVX2(bufR.data(), bufR.data(), total_samples, gain_ratio);

    if (channels >= 1) audio->set_sample_data(bufL.data(), 0);
    if (channels >= 2) audio->set_sample_data(bufR.data(), 1);

    return true;
}

FILTER_PLUGIN_TABLE filter_plugin_table_volume = {
    FILTER_PLUGIN_TABLE::FLAG_AUDIO,
    GEN_TOOL_NAME(TOOL_NAME),
    label,
    GEN_FILTER_INFO(TOOL_NAME),
    filter_items_volume,
    nullptr,
    func_proc_audio_volume,
    nullptr,
    nullptr
};