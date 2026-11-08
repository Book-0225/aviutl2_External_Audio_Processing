#include "VolumeFix.h"

#include "AnalyzerColor.h"
#include "Avx2Utils.h"
#include "FilterItemHelper.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

constexpr auto TOOL_NAME = L"Volume";

namespace {
constexpr double GAIN_MIN_PERCENT = 0.0;
constexpr double GAIN_MAX_PERCENT = 10000.0;
constexpr double GAIN_MIN_DB = -140.0;
const double GAIN_MAX_DB = 20.0 * std::log10(GAIN_MAX_PERCENT / 100.0);

double clamp_gain_db(double db) {
    return std::clamp(db, GAIN_MIN_DB, GAIN_MAX_DB);
}

double gain_db_to_percent(double db) {
    if (db <= GAIN_MIN_DB) return GAIN_MIN_PERCENT;
    return std::clamp(100.0 * std::pow(10.0, db / 20.0), GAIN_MIN_PERCENT, GAIN_MAX_PERCENT);
}

std::optional<double> parse_double_locale_free(std::wstring_view s) {
    while (!s.empty() && iswspace(s.front())) s.remove_prefix(1);
    while (!s.empty() && iswspace(s.back())) s.remove_suffix(1);
    if (s.empty()) return std::nullopt;

    std::string a;
    a.reserve(s.size());
    for (wchar_t c : s) {
        if (c > 127) return std::nullopt;
        a.push_back(c == L',' ? '.' : static_cast<char>(c));
    }
    std::string_view v = a;
    if (v.starts_with('+')) v.remove_prefix(1);
    if (v.empty()) return std::nullopt;

    double out = 0.0;
    auto [ptr, ec] = std::from_chars(v.data(), v.data() + v.size(), out, std::chars_format::general);
    if (ec != std::errc{} || ptr != v.data() + v.size() || !std::isfinite(out)) return std::nullopt;
    return out;
}

void dtw(HDC hdc, const wchar_t* s, RECT r, COLORREF c, UINT fmt = DT_LEFT | DT_VCENTER | DT_SINGLELINE) {
    SetTextColor(hdc, c);
    DrawTextW(hdc, s, -1, &r, fmt);
}

OBJECT_HANDLE find_target_object(EDIT_SECTION* edit, const VolumeFixProposal& prop) {
    auto matches = [&](OBJECT_HANDLE h) {
        return h && edit->get_object_layer_frame(h).start == prop.f_start;
    };
    const int32_t n = edit->get_selected_object_num();
    for (int32_t i = 0; i < n; ++i) {
        OBJECT_HANDLE h = edit->get_selected_object(i);
        if (matches(h)) return h;
    }
    OBJECT_HANDLE focus = edit->get_focus_object();
    return matches(focus) ? focus : nullptr;
}

} // namespace

FILTER_ITEM_TRACK vol_gain(L"ゲイン", 100.0, GAIN_MIN_PERCENT, GAIN_MAX_PERCENT, 0.1, nullptr, 1.0);

static std::vector<void*> filter_items_volume = FilterItemHelper::ItemList().Add(vol_gain).Build();

VolumeFixProposal make_volume_fix_proposal(int32_t f_start, const std::wstring& obj_name, double measured_integrated, double measured_peak, double target_lufs, double peak_ceiling) {
    VolumeFixProposal p;
    p.f_start = f_start;
    p.obj_name = obj_name;
    p.measured_integrated = measured_integrated;
    p.measured_peak = measured_peak;
    p.target_lufs = target_lufs;
    p.peak_ceiling = peak_ceiling;
    p.valid = (measured_integrated > -99.0);
    p.suggested_gain_db = p.valid ? clamp_gain_db(target_lufs - measured_integrated) : 0.0;
    p.user_gain_db = p.suggested_gain_db;
    return p;
}

std::wstring VOLUME_EFFECT_NAME = GEN_TOOL_NAME(TOOL_NAME);
std::wstring VOLUME_ITEM_NAME = vol_gain.name;

bool apply_volume_fix(EDIT_SECTION* edit, const VolumeFixProposal& prop) {
    if (!prop.valid) return false;
    OBJECT_HANDLE obj = find_target_object(edit, prop);
    if (!obj) return false;
    const int32_t before = edit->count_object_effect(obj, VOLUME_EFFECT_NAME.c_str());
    edit->create_effect(obj, VOLUME_EFFECT_NAME.c_str());
    const int32_t after = edit->count_object_effect(obj, VOLUME_EFFECT_NAME.c_str());
    if (after <= before) return false;
    const std::wstring target = std::format(L"{}:{}", VOLUME_EFFECT_NAME, after - 1);
    const double percent = gain_db_to_percent(prop.user_gain_db);
    const std::string value = std::format("{:.4f}", percent);
    return edit->set_object_item_value(obj, target.c_str(), VOLUME_ITEM_NAME.c_str(), value.c_str());
}

void VolumeFixPanel::create(HWND parent, HINSTANCE hinst, int32_t base_ctrl_id, HFONT font) {
    m_parent = parent;
    m_edit_gain = CreateWindowW(L"EDIT", L"0.00", WS_CHILD | ES_RIGHT | ES_AUTOHSCROLL, 0, 0, 70, 20, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(base_ctrl_id + 0)), hinst, nullptr);
    m_btn_apply = CreateWindowW(L"BUTTON", TrText(L"適用"), WS_CHILD | BS_PUSHBUTTON, 0, 0, 60, 24, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(base_ctrl_id + 1)), hinst, nullptr);
    m_btn_reset = CreateWindowW(L"BUTTON", TrText(L"提案値に戻す"), WS_CHILD | BS_PUSHBUTTON, 0, 0, 100, 24, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(base_ctrl_id + 2)), hinst, nullptr);
    HWND controls[] = {
        m_edit_gain,
        m_btn_apply,
        m_btn_reset
    };
    for (HWND h : controls)
        if (h)
            SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
}

void VolumeFixPanel::destroy() {
    if (m_edit_gain) DestroyWindow(m_edit_gain);
    if (m_btn_apply) DestroyWindow(m_btn_apply);
    if (m_btn_reset) DestroyWindow(m_btn_reset);
    m_edit_gain = m_btn_apply = m_btn_reset = nullptr;
}

void VolumeFixPanel::refresh_edit_text() {
    if (!m_edit_gain) return;
    const std::wstring buf = std::format(L"{:.2f}", clamp_gain_db(m_prop.user_gain_db));
    SetWindowTextW(m_edit_gain, buf.c_str());
}

double VolumeFixPanel::read_edit_gain() const {
    if (!m_edit_gain) return m_prop.user_gain_db;
    wchar_t buf[64];
    GetWindowTextW(m_edit_gain, buf, 64);
    const auto v = parse_double_locale_free(buf);
    if (!v) return m_prop.user_gain_db;
    return clamp_gain_db(*v);
}

void VolumeFixPanel::set_proposal(const VolumeFixProposal& prop) {
    m_prop = prop;
    m_prop.user_gain_db = clamp_gain_db(m_prop.user_gain_db);
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
    m_prop.user_gain_db = clamp_gain_db(m_prop.suggested_gain_db);
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

    dtw(mdc, TrText(L"音量の調整"), { area.left, area.top, area.left + 120, area.top + 18 }, C_TEXT_SUB);
    const bool over = live.exceeds_ceiling();
    const std::wstring preview = std::format(L"{}{}: Integrated {:.1f} LUFS / Peak {:.1f} dBTP", over ? L"⚠ " : L"", TrText(L"適用後の予測"), live.predicted_integrated(), live.predicted_peak());
    dtw(mdc, preview.c_str(), { area.left, area.top, area.right, area.top + 18 }, over ? C_FAIL : C_PASS, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    dtw(mdc, L"dB", { area.left + 76, area.top + 20, area.left + 100, area.top + 44 }, C_TEXT_SUB);
}

bool func_proc_audio_volume(FILTER_PROC_AUDIO* audio) {
    const int32_t total_samples = audio->object->sample_num;
    if (total_samples <= 0) return true;
    const int32_t channels = std::min(2, audio->object->channel_num);
    if (channels <= 0) return true;

    const float gain_val = static_cast<float>(vol_gain.value);
    if (gain_val == 100.0f) return true;

    const float gain_ratio = gain_val / 100.0f;

    thread_local std::vector<float> bufL, bufR;
    const size_t n = static_cast<size_t>(total_samples);
    if (bufL.size() < n) bufL.resize(n);
    if (channels >= 2 && bufR.size() < n) bufR.resize(n);

    audio->get_sample_data(bufL.data(), 0);
    Avx2Utils::ScaleBufferAVX2(bufL.data(), bufL.data(), total_samples, gain_ratio);
    audio->set_sample_data(bufL.data(), 0);

    if (channels >= 2) {
        audio->get_sample_data(bufR.data(), 1);
        Avx2Utils::ScaleBufferAVX2(bufR.data(), bufR.data(), total_samples, gain_ratio);
        audio->set_sample_data(bufR.data(), 1);
    }

    return true;
}

FILTER_PLUGIN_TABLE filter_plugin_table_volume = {
    FILTER_PLUGIN_TABLE::FLAG_AUDIO,
    GEN_TOOL_NAME(TOOL_NAME),
    label,
    GEN_FILTER_INFO(TOOL_NAME),
    filter_items_volume.data(),
    nullptr,
    func_proc_audio_volume,
    nullptr,
    nullptr
};