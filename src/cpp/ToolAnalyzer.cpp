#include "AnalyzerColor.h"
#include "Eap2Common.h"
#include "Eap2Config.h"
#include "VolumeFix.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <memory>
#include <mutex>
#include <numbers>
#include <numeric>
#include <optional>
#include <span>
#include <string>
#include <unordered_set>
#include <vector>
#include <windowsx.h>

int32_t normalize_batch_size(const int32_t value) {
    int32_t result = 512;
    if (value > 0) {
        const int32_t clamped = std::clamp(value, 64, 8192);
        const int32_t upper = static_cast<int32_t>(std::bit_ceil(static_cast<uint32_t>(clamped)));
        const int32_t lower = upper >> 1;
        result = (upper == clamped) ? clamped : ((clamped - lower <= upper - clamped) ? lower : upper);
    }
    if (result != value)
        DbgPrint(L"[Analyzer] BatchSize " + std::to_wstring(value) + L" -> " + std::to_wstring(result), LOG_VERBOSE);
    return result;
}

namespace {

constexpr const wchar_t* APP_NAME = L"EAP2 Analyzer";
constexpr const wchar_t* APP_CLASS = L"EAP2_Analyzer";

constexpr UINT WM_PROGRESS = WM_USER + 1;
constexpr UINT WM_DONE = WM_USER + 2;
constexpr UINT WM_FRAME_CHANGE = WM_USER + 3;
constexpr UINT_PTR ANALYSIS_TIMER_ID = 1;

constexpr int32_t ID_BTN_RANGE = 1;
constexpr int32_t ID_BTN_SCENE = 2;
constexpr int32_t ID_BTN_ABORT = 3;
constexpr int32_t ID_BTN_OBJECT = 4;
constexpr int32_t ID_COMBO_PRESET = 10;
constexpr int32_t ID_EDIT_LUFS = 11;
constexpr int32_t ID_EDIT_PEAK = 12;
constexpr int32_t ID_EDIT_SILENCE = 13;
constexpr int32_t ID_VOLFIX_EDIT = 200;
constexpr int32_t ID_VOLFIX_APPLY = 201;
constexpr int32_t ID_VOLFIX_RESET = 202;

constexpr double ABSOLUTE_GATE_LUFS = -70.0;
constexpr double SILENT_LUFS = -100.0;
constexpr double CHANNEL_COUNT = 2.0;

constexpr UINT TXT_LEFT = DT_LEFT | DT_VCENTER | DT_SINGLELINE;
constexpr UINT TXT_RIGHT = DT_RIGHT | DT_VCENTER | DT_SINGLELINE;
constexpr UINT TXT_CENTER = DT_CENTER | DT_VCENTER | DT_SINGLELINE;

struct Preset {
    const wchar_t* name;
    double lufs;
    double peak;
};

constexpr std::array PRESETS{
    Preset{ L"YouTube / Spotify", -14.0, -1.0 },
    Preset{ L"Apple Music", -16.0, -1.0 },
    Preset{ L"Amazon Music", -14.0, -2.0 },
    Preset{ L"Netflix", -27.0, -2.0 },
    Preset{ L"Broadcast", -23.0, -1.0 },
    Preset{ L"CD", -9.0, -0.1 },
    Preset{ L"Custom", -14.0, -1.0 },
};
constexpr int32_t PRESET_CUSTOM = static_cast<int32_t>(PRESETS.size()) - 1;

int32_t find_preset_index() {
    for (int32_t i = 0; i < PRESET_CUSTOM; i++)
        if (std::abs(PRESETS[i].lufs - settings.analyzer.target_lufs) < 0.05 &&
            std::abs(PRESETS[i].peak - settings.analyzer.target_peak) < 0.05)
            return i;
    return PRESET_CUSTOM;
}

struct BqCoeff {
    double b0, b1, b2, a1, a2;
};

struct BqState {
    double z1 = 0.0;
    double z2 = 0.0;
};

[[nodiscard]] inline double bq(const BqCoeff& c, BqState& s, double x) noexcept {
    const double y = c.b0 * x + s.z1;
    s.z1 = c.b1 * x - c.a1 * y + s.z2;
    s.z2 = c.b2 * x - c.a2 * y;
    return y;
}

BqCoeff make_k_prefilter(double fs) {
    constexpr double f0 = 1681.974450955533;
    constexpr double gain_db = 3.999843853973347;
    constexpr double Q = 0.7071752369554196;
    const double K = std::tan(std::numbers::pi * f0 / fs);
    const double Vh = std::pow(10.0, gain_db / 20.0);
    const double Vb = std::pow(Vh, 0.4996667741545416);
    const double a0 = 1.0 + K / Q + K * K;
    return { (Vh + Vb * K / Q + K * K) / a0,
             2.0 * (K * K - Vh) / a0,
             (Vh - Vb * K / Q + K * K) / a0,
             2.0 * (K * K - 1.0) / a0,
             (1.0 - K / Q + K * K) / a0 };
}

BqCoeff make_k_highpass(double fs) {
    constexpr double f0 = 38.13547087602444;
    constexpr double Q = 0.5003270373238773;
    const double K = std::tan(std::numbers::pi * f0 / fs);
    const double a0 = 1.0 + K / Q + K * K;
    return { 1.0, -2.0, 1.0, 2.0 * (K * K - 1.0) / a0, (1.0 - K / Q + K * K) / a0 };
}

struct KWeight {
    BqCoeff pre{};
    BqCoeff rlb{};
    BqState pre_l, pre_r;
    BqState rlb_l, rlb_r;

    void init(double fs) {
        pre = make_k_prefilter(fs);
        rlb = make_k_highpass(fs);
    }

    [[nodiscard]] double power(double l, double r) noexcept {
        const double kl = bq(rlb, rlb_l, bq(pre, pre_l, l));
        const double kr = bq(rlb, rlb_r, bq(pre, pre_r, r));
        return kl * kl + kr * kr;
    }
};

[[nodiscard]] inline double bessel_i0(double x) noexcept {
    const double q = x * x * 0.25;
    double term = 1.0;
    double sum = 1.0;
    for (int32_t k = 1; k < 64 && term > sum * 1e-17; k++) {
        term *= q / (static_cast<double>(k) * k);
        sum += term;
    }
    return sum;
}

struct TruePeakCoeffs {
    static constexpr int32_t TAPS = 12;
    int32_t factor = 1;
    std::array<std::array<double, TAPS>, 4> phase{};

    void init(double sample_rate) {
        factor = sample_rate < 96000.0 ? 4 : (sample_rate < 192000.0 ? 2 : 1);
        if (factor == 1) return;

        constexpr double beta = 8.0;
        const int32_t n_total = TAPS * factor;
        const double center = n_total / 2.0;
        const double i0_beta = bessel_i0(beta);

        std::vector<double> h(static_cast<size_t>(n_total));
        for (int32_t n = 0; n < n_total; n++) {
            const double x = (n - center) / factor;
            const double sinc = (x == 0.0) ? 1.0 : std::sin(std::numbers::pi * x) / (std::numbers::pi * x);
            const double r = (n - center) / center;
            h[n] = sinc * bessel_i0(beta * std::sqrt(std::max(0.0, 1.0 - r * r))) / i0_beta;
        }
        for (int32_t p = 1; p < factor; p++) {
            double sum = 0.0;
            for (int32_t k = 0; k < TAPS; k++) sum += h[static_cast<size_t>(p + k * factor)];
            for (int32_t k = 0; k < TAPS; k++) phase[p][k] = h[static_cast<size_t>(p + k * factor)] / sum;
        }
    }
};

struct TruePeakDetector {
    static constexpr int32_t TAPS = TruePeakCoeffs::TAPS;

    const TruePeakCoeffs* coef = nullptr;
    std::array<double, 2 * TAPS> hist{};
    int32_t pos = 0;
    int32_t filled = 0;

    [[nodiscard]] double push(double x) noexcept {
        pos = (pos == 0) ? TAPS - 1 : pos - 1;
        hist[pos] = hist[pos + TAPS] = x;
        if (filled < TAPS) {
            ++filled;
            return 0.0;
        }
        const double* h = &hist[pos];

        double pk = 0.0;
        for (int32_t p = 1; p < coef->factor; p++) {
            double acc = 0.0;
            for (int32_t k = 0; k < TAPS; k++) acc += coef->phase[p][k] * h[k];
            pk = std::max(pk, std::abs(acc));
        }
        return pk;
    }
};

[[nodiscard]] inline double ms_to_lufs(double ms) noexcept {
    return ms > 0.0 ? -0.691 + 10.0 * std::log10(ms) : SILENT_LUFS;
}

struct Issue {
    enum class Type : std::uint8_t {
        CLIP,
        SILENCE
    };

    Type type;
    int32_t f_start;
    int32_t f_end;
};

using IssueId = int64_t;

[[nodiscard]] constexpr IssueId issue_seen_id(const Issue& iss) noexcept {
    return static_cast<IssueId>((static_cast<uint64_t>(static_cast<uint32_t>(iss.f_start)) << 33) |
                                (static_cast<uint64_t>(static_cast<uint32_t>(iss.f_end)) << 2) |
                                static_cast<uint64_t>(iss.type));
}

struct UnpackedIssueId {
    Issue::Type type;
    int32_t f_start;
    int32_t f_end;
};

[[nodiscard]] constexpr UnpackedIssueId unpack_issue_id(IssueId id) noexcept {
    return { static_cast<Issue::Type>(id & 0x3),
             static_cast<int32_t>((id >> 33) & 0x7FFFFFFF),
             static_cast<int32_t>((id >> 2) & 0x7FFFFFFF) };
}

enum class JudgeStatus : std::uint8_t {
    NONE,
    PASS,
    WARN,
    FAIL
};

struct AnalyzeResult {
    bool valid = false;
    double integrated = SILENT_LUFS;
    double true_peak = SILENT_LUFS;
    double lra = 0.0;
    double st_max = SILENT_LUFS;
    double mom_max = SILENT_LUFS;
    int32_t st_max_frame = -1;
    int32_t mom_max_frame = -1;
    std::vector<double> st_hist;
    std::vector<double> mom_hist;
    std::vector<Issue> issues;
    int32_t f_start = 0;
    int32_t f_end = 0;
    AnalyzerConfig snap;
    int32_t fps_rate = 30;
    int32_t fps_scale = 1;
    int32_t sample_rate = 44100;
    bool is_object = false;
    std::wstring obj_name;
};

enum class Notice : std::int32_t {
    None,
    Failed,
    Aborted,
    Done
};

std::mutex g_result_mtx;
std::shared_ptr<const AnalyzeResult> g_result = std::make_shared<const AnalyzeResult>();

[[nodiscard]] std::shared_ptr<const AnalyzeResult> current_result() {
    std::lock_guard lk(g_result_mtx);
    return g_result;
}

void publish_result(std::shared_ptr<const AnalyzeResult> r) {
    std::lock_guard lk(g_result_mtx);
    g_result = std::move(r);
}

HWND g_hwnd = nullptr;
std::atomic<bool> g_busy{ false };
std::atomic<bool> g_abort{ false };
std::atomic<int32_t> g_prog{ 0 };
std::atomic<int32_t> g_cur_frame{ -1 };
std::atomic<Notice> g_notice{ Notice::None };

struct GraphView {
    static constexpr double DEFAULT_MIN = -54.0;
    static constexpr double DEFAULT_MAX = 0.0;

    double t_zoom = 1.0;
    double t_pan = 0.0;
    double v_min = DEFAULT_MIN;
    double v_max = DEFAULT_MAX;

    [[nodiscard]] bool value_axis_is_default() const noexcept {
        return v_min == DEFAULT_MIN && v_max == DEFAULT_MAX;
    }
    void clamp_pan() noexcept {
        t_pan = std::clamp(t_pan, 0.0, 1.0 - 1.0 / t_zoom);
    }
};
GraphView g_view;

enum IssueFilter : int32_t {
    FILTER_ALL = 0,
    FILTER_CLIP = 1,
    FILTER_SILENCE = 2,
    FILTER_COUNT = 3
};

constexpr int32_t MAXROW_COUNT = 5;

RECT s_graph = {};
int32_t g_issue_filter = FILTER_ALL;
int32_t s_issues_scroll = 0;

RECT s_flt_btn[FILTER_COUNT] = {};
RECT s_maxrow_hits[MAXROW_COUNT] = {};
int32_t s_maxrow_frames[MAXROW_COUNT] = { -1, -1, -1, -1, -1 };
RECT s_next_unseen_btn = {};
RECT s_keep_seen_hit = { 380, 74, 490, 94 };

struct SbGeom {
    int32_t x = 0;
    int32_t w = 0;
    int32_t track_y = 0;
    int32_t track_h = 0;
    int32_t thumb_y = 0;
    int32_t thumb_h = 0;
    int32_t vis_n = 0;
    int32_t max_sc = 0;
    bool valid = false;
};
SbGeom s_sb;
bool s_sb_drag = false;
bool s_graph_drag = false;
int32_t s_sb_drag_y0 = 0;
int32_t s_sb_drag_s0 = 0;

struct IssueRowHit {
    int32_t y0;
    int32_t y1;
    int32_t f_start;
    IssueId id;
};
std::vector<IssueRowHit> s_issue_rows;
int32_t s_hovered_issue = -1;

std::unordered_set<IssueId> g_seen_issues;
bool g_keep_seen = true;

HWND s_btn_rng = nullptr;
HWND s_btn_all = nullptr;
HWND s_btn_obj = nullptr;
HWND s_btn_abort = nullptr;
HWND s_combo = nullptr;
HWND s_edit_l = nullptr;
HWND s_edit_p = nullptr;
HWND s_edit_sil = nullptr;
HBRUSH s_br_ctrl = nullptr;

std::wstring g_font_name;
HFONT g_fL = nullptr;
HFONT g_fLB = nullptr;
HFONT g_fS = nullptr;

VolumeFixPanel g_vol_fix_panel;

[[nodiscard]] bool hit(const RECT& r, int32_t x, int32_t y) noexcept {
    const POINT p{ x, y };
    return PtInRect(&r, p) != FALSE;
}

[[nodiscard]] int32_t graph_w() noexcept {
    return s_graph.right - s_graph.left;
}
[[nodiscard]] int32_t graph_h() noexcept {
    return s_graph.bottom - s_graph.top;
}

[[nodiscard]] bool is_seen(const Issue& iss) {
    return g_seen_issues.contains(issue_seen_id(iss));
}

[[nodiscard]] bool issue_matches_filter(const Issue& iss, int32_t filter) noexcept {
    return filter == FILTER_ALL ||
           (filter == FILTER_CLIP && iss.type == Issue::Type::CLIP) ||
           (filter == FILTER_SILENCE && iss.type == Issue::Type::SILENCE);
}

[[nodiscard]] std::vector<int32_t> filtered_issue_indices(const AnalyzeResult& res) {
    std::vector<int32_t> out;
    out.reserve(res.issues.size());
    for (int32_t i = 0; i < std::ssize(res.issues); i++)
        if (issue_matches_filter(res.issues[i], g_issue_filter)) out.push_back(i);
    return out;
}

struct StereoGain {
    double l = 1.0;
    double r = 1.0;
};

struct ObjVolume {
    int32_t f0 = 0;
    std::vector<float> gl;
    std::vector<float> gr;

    [[nodiscard]] bool valid() const {
        return !gl.empty();
    }

    [[nodiscard]] StereoGain gains_at(int32_t f) const {
        if (gl.empty()) return {};
        const int64_t last = std::ssize(gl) - 1;
        const auto i = static_cast<size_t>(std::clamp<int64_t>(static_cast<int64_t>(f) - f0, 0, last));
        return { gl[i], gr[i] };
    }

    [[nodiscard]] static StereoGain pan_gains(double p) noexcept {
        return { p > 0.0 ? (100.0 - p) / 100.0 : 1.0,
                 p < 0.0 ? (100.0 + p) / 100.0 : 1.0 };
    }
};

struct ParamSrc {
    enum class Kind : std::uint8_t {
        None,
        Constant,
        Track
    };

    Kind kind = Kind::None;
    double c = 0.0;
    EFFECT_HANDLE h = nullptr;
    const wchar_t* item = nullptr;

    [[nodiscard]] double eval(EDIT_SECTION* edit, double frame, double def) const {
        switch (kind) {
            case Kind::Track: {
                double v = 0.0;
                return edit->get_effect_track_value(h, item, frame, &v) ? v : def;
            }
            case Kind::Constant:
                return c;
            default:
                return def;
        }
    }
};

ParamSrc probe_param(EDIT_SECTION* edit, EFFECT_HANDLE h, const wchar_t* item, double frame) {
    ParamSrc p;
    p.h = h;
    p.item = item;
    double v = 0.0;
    if (edit->get_effect_track_value(h, item, frame, &v)) {
        p.kind = ParamSrc::Kind::Track;
        return p;
    }
    const LPCSTR raw = edit->get_effect_item_value(h, item);
    if (raw && *raw) {
        char* end = nullptr;
        v = std::strtod(raw, &end);
        if (end != raw) {
            p.kind = ParamSrc::Kind::Constant;
            p.c = v;
        }
    }
    return p;
}

struct ObjVolLayer {
    ParamSrc vol;
    ParamSrc pan;
};

ObjVolume read_object_volume(EDIT_SECTION* edit, OBJECT_HANDLE obj, int32_t f_start, int32_t f_end) {
    ObjVolume r;
    const int32_t len = f_end - f_start + 1;
    if (len <= 0) return r;

    const int32_t total = edit->get_effect_list(obj, nullptr, 0);
    if (total <= 0) return r;
    std::vector<EFFECT_HANDLE> list(total);
    const int32_t n = edit->get_effect_list(obj, list.data(), total);

    static constexpr const wchar_t* BASE_EFFECTS[] = { L"映像再生", L"音声再生" };
    static constexpr const wchar_t* FILTER_NAME = L"音量調整";

    const double probe_f = static_cast<double>(f_start);
    std::vector<ObjVolLayer> layers;
    bool base_found = false;

    for (int32_t i = 0; i < n; i++) {
        const EFFECT_HANDLE h = list[i];
        const LPCWSTR name = edit->get_effect_name(h);
        if (!name) continue;

        const bool is_base = std::ranges::any_of(BASE_EFFECTS, [&](const wchar_t* b) { return std::wcscmp(name, b) == 0; });
        const bool is_filter = std::wcscmp(name, FILTER_NAME) == 0;
        if (!is_base && !is_filter) continue;
        if (is_base && base_found) continue;
        if (!edit->get_effect_enable(h)) continue;

        ObjVolLayer l;
        l.vol = probe_param(edit, h, L"音量", probe_f);
        l.pan = probe_param(edit, h, L"左右", probe_f);
        if (l.vol.kind == ParamSrc::Kind::None && l.pan.kind == ParamSrc::Kind::None) continue;
        if (is_base) base_found = true;
        layers.push_back(l);
    }
    if (layers.empty()) return r;

    r.f0 = f_start;
    r.gl.resize(len);
    r.gr.resize(len);
    for (int32_t k = 0; k < len; k++) {
        const double tf = static_cast<double>(f_start + k);
        double gl = 1.0;
        double gr = 1.0;
        for (const ObjVolLayer& l : layers) {
            const double g = l.vol.eval(edit, tf, 100.0) / 100.0;
            const double p = std::clamp(l.pan.eval(edit, tf, 0.0), -100.0, 100.0);
            const StereoGain pg = ObjVolume::pan_gains(p);
            gl *= g * pg.l;
            gr *= g * pg.r;
        }
        r.gl[k] = static_cast<float>(gl);
        r.gr[k] = static_cast<float>(gr);
    }
    return r;
}

struct ObjPickResult {
    OBJECT_HANDLE obj = nullptr;
    int32_t f_start = 0;
    int32_t f_end = 0;
    std::wstring name;
    ObjVolume vol;
    bool found = false;
};

void pick_target_object_cb(void* p, EDIT_SECTION* edit) {
    auto* r = static_cast<ObjPickResult*>(p);
    OBJECT_HANDLE obj = edit->get_selected_object(0);
    if (!obj) obj = edit->get_focus_object();
    if (!obj) return;

    const OBJECT_LAYER_FRAME lf = edit->get_object_layer_frame(obj);
    const LPCWSTR nm = edit->get_object_name(obj);

    r->obj = obj;
    r->f_start = lf.start;
    r->f_end = lf.end;
    r->name = nm ? nm : L"";
    r->vol = read_object_volume(edit, obj, lf.start, lf.end);
    r->found = true;
}

void jump_to_frame_cb(void* p, EDIT_SECTION* edit) {
    edit->set_cursor_layer_frame(edit->info->layer, *static_cast<int32_t*>(p));
}

void jump_to_frame(int32_t frame) {
    g_edit_handle->call_edit_section_param(&frame, jump_to_frame_cb);
}

void center_graph_on_frame(int32_t frame) {
    const auto res = current_result();
    if (!res->valid || res->f_end <= res->f_start) return;

    const double frac = static_cast<double>(frame - res->f_start) / (res->f_end - res->f_start);
    const double visible = 1.0 / g_view.t_zoom;
    if (g_view.t_zoom <= 1.01 || !(frac < g_view.t_pan || frac > g_view.t_pan + visible)) return;

    g_view.t_pan = frac - 0.5 * visible;
    g_view.clamp_pan();
}

void focus_frame(int32_t frame) {
    jump_to_frame(frame);
    center_graph_on_frame(frame);
}

void jump_from_mouse(int32_t mx) {
    const auto res = current_result();
    if (!res->valid || res->f_end <= res->f_start) return;

    mx = std::clamp<int32_t>(mx, s_graph.left, s_graph.right);
    const double vis_f = static_cast<double>(mx - s_graph.left) / graph_w();
    const double data_f = std::clamp(g_view.t_pan + vis_f / g_view.t_zoom, 0.0, 1.0);
    const int32_t tf = res->f_start + static_cast<int32_t>(data_f * (res->f_end - res->f_start));
    jump_to_frame(std::clamp(tf, res->f_start, res->f_end));
}

struct FrameAudio {
    std::vector<float> l;
    std::vector<float> r;
};

struct BatchBuf {
    std::mutex mtx;
    int32_t first_frame = 0;
    int32_t count = 0;
    std::vector<FrameAudio> frames;

    void begin(int32_t first, int32_t n) {
        std::lock_guard lk(mtx);
        first_frame = first;
        count = n;
        if (std::ssize(frames) < n) frames.resize(n);
        for (int32_t i = 0; i < n; i++) {
            frames[i].l.clear();
            frames[i].r.clear();
        }
    }
};

void audio_cb(void* param, int32_t frame, const float* L, const float* R, int32_t n) {
    auto* buf = static_cast<BatchBuf*>(param);
    std::lock_guard lk(buf->mtx);
    const int64_t idx = static_cast<int64_t>(frame) - buf->first_frame;
    if (idx < 0 || idx >= buf->count) return;
    FrameAudio& f = buf->frames[static_cast<size_t>(idx)];
    f.l.assign(L, L + n);
    f.r.assign(R, R + n);
}

struct FStat {
    int32_t frame;
    double peak;
};

struct AnalysisRequest {
    int32_t f_start = 0;
    int32_t f_end = 0;
    OBJECT_HANDLE obj = nullptr;
    std::wstring obj_name;
    bool apply_effect = false;
    ObjVolume vol;
};

struct AnalysisState {
    int32_t f0 = 0;
    int32_t f1 = 0;
    int32_t rate = 30;
    int32_t scale = 1;
    int32_t sr = 44100;
    AnalyzerConfig snap{};
    OBJECT_HANDLE target_obj = nullptr;
    std::wstring obj_name;
    bool apply_effect = false;
    ObjVolume vol;

    int32_t mom_len = 0;
    int32_t st_len = 0;
    int32_t step = 0;
    int32_t total_frames = 0;
    int32_t batch_size = 512;
    int32_t next_frame = 0;

    KWeight kw;
    TruePeakCoeffs tp_coef;
    TruePeakDetector tp_l;
    TruePeakDetector tp_r;
    double tp_peak = 0.0;
    std::vector<float> kw_sq;
    std::vector<FStat> fstats;

    BatchBuf batch;
    bool abort_flag = false;
    bool render_failed = false;

    [[nodiscard]] int32_t samples_per_frame() const {
        return std::max(1, sr * scale / std::max(1, rate));
    }
};

std::unique_ptr<AnalysisState> g_analysis;

class BlockEnergy {
  public:
    BlockEnergy(std::span<const float> kw, int32_t step) : kw_(kw), step_(step) {
        const size_t nb = kw.size() / static_cast<size_t>(step);
        sums_.resize(nb);
        prefix_.assign(nb + 1, 0.0);
        for (size_t k = 0; k < nb; k++) {
            const auto first = kw.begin() + static_cast<ptrdiff_t>(k) * step;
            sums_[k] = std::accumulate(first, first + step, 0.0);
            prefix_[k + 1] = prefix_[k] + sums_[k];
        }
    }

    [[nodiscard]] int64_t block_count() const {
        return std::ssize(sums_);
    }

    [[nodiscard]] double block_mean_square(int64_t k) const {
        return sums_[static_cast<size_t>(k)] / step_;
    }

    [[nodiscard]] double mean_square(int64_t block, int32_t len) const {
        const int64_t whole = len / step_;
        double sum = prefix_[static_cast<size_t>(block + whole)] - prefix_[static_cast<size_t>(block)];
        const int64_t rem_begin = (block + whole) * step_;
        for (int64_t i = rem_begin; i < block * step_ + len; i++) sum += kw_[static_cast<size_t>(i)];
        return sum / len;
    }

  private:
    std::span<const float> kw_;
    int32_t step_;
    std::vector<double> sums_;
    std::vector<double> prefix_;
};

struct WindowSweep {
    std::vector<double> energy;
    std::vector<double> lufs;
    double max_lufs = SILENT_LUFS;
    int32_t max_frame = -1;
};

WindowSweep sweep_windows(const BlockEnergy& blocks, int64_t total, int32_t len, int32_t step, int32_t f0, int32_t spf) {
    WindowSweep out;
    if (total >= len) {
        const auto n = static_cast<size_t>((total - len) / step + 1);
        out.energy.reserve(n);
        out.lufs.reserve(n);
    }
    for (int64_t k = 0; k * step + len <= total; k++) {
        const double ms = blocks.mean_square(k, len);
        const double l = ms_to_lufs(ms);
        out.energy.push_back(ms);
        out.lufs.push_back(l);
        if (l > out.max_lufs) {
            out.max_lufs = l;
            out.max_frame = f0 + static_cast<int32_t>(k * step / spf);
        }
    }
    return out;
}

double integrated_lufs(const WindowSweep& mom) {
    double sum = 0.0;
    size_t cnt = 0;
    for (size_t i = 0; i < mom.energy.size(); i++)
        if (mom.lufs[i] >= ABSOLUTE_GATE_LUFS) {
            sum += mom.energy[i];
            cnt++;
        }
    if (cnt == 0) return SILENT_LUFS;

    const double rel = ms_to_lufs(sum / cnt) - 10.0;
    double sum2 = 0.0;
    size_t cnt2 = 0;
    for (size_t i = 0; i < mom.energy.size(); i++)
        if (mom.lufs[i] >= ABSOLUTE_GATE_LUFS && mom.lufs[i] >= rel) {
            sum2 += mom.energy[i];
            cnt2++;
        }
    return cnt2 > 0 ? ms_to_lufs(sum2 / cnt2) : ms_to_lufs(sum / cnt);
}

double percentile_sorted(const std::vector<double>& v, double p) {
    const double pos = p * static_cast<double>(v.size() - 1);
    const auto i = static_cast<size_t>(pos);
    const double frac = pos - static_cast<double>(i);
    return i + 1 < v.size() ? v[i] + (v[i + 1] - v[i]) * frac : v[i];
}

double loudness_range(const WindowSweep& st) {
    double sum = 0.0;
    size_t cnt = 0;
    for (size_t i = 0; i < st.lufs.size(); i++)
        if (st.lufs[i] >= ABSOLUTE_GATE_LUFS) {
            sum += st.energy[i];
            cnt++;
        }
    if (cnt < 2) return 0.0;

    const double rel = ms_to_lufs(sum / static_cast<double>(cnt)) - 20.0;
    std::vector<double> v;
    v.reserve(cnt);
    for (const double l : st.lufs)
        if (l >= ABSOLUTE_GATE_LUFS && l >= rel) v.push_back(l);
    if (v.size() < 2) return 0.0;

    std::ranges::sort(v);
    return percentile_sorted(v, 0.95) - percentile_sorted(v, 0.10);
}

void detect_clips(const AnalysisState& s, std::vector<Issue>& out) {
    bool in_clip = false;
    int32_t clip_s = 0;
    for (const FStat& st : s.fstats) {
        if (st.peak >= 1.0) {
            if (!in_clip) {
                in_clip = true;
                clip_s = st.frame;
            }
        } else if (in_clip) {
            out.push_back({ Issue::Type::CLIP, clip_s, st.frame - 1 });
            in_clip = false;
        }
    }
    if (in_clip) out.push_back({ Issue::Type::CLIP, clip_s, s.f1 });
}

void detect_silences(const AnalysisState& s, const BlockEnergy& blocks, int64_t total, std::vector<Issue>& out) {
    const int32_t spf = s.samples_per_frame();
    const double sil_pow = std::pow(10.0, s.snap.sil_db / 10.0);
    const int64_t min_samples = std::max(1, static_cast<int32_t>(s.snap.sil_min_s * s.sr));
    const int64_t step = s.step;

    bool in_sil = false;
    int64_t start = 0;
    for (int64_t k = 0; k < blocks.block_count(); k++) {
        const int64_t i = k * step;
        if (blocks.block_mean_square(k) / CHANNEL_COUNT < sil_pow) {
            if (!in_sil) {
                in_sil = true;
                start = i;
            }
        } else if (in_sil) {
            if (i - start >= min_samples)
                out.push_back({ Issue::Type::SILENCE,
                                s.f0 + static_cast<int32_t>(start / spf),
                                std::min(s.f1, s.f0 + static_cast<int32_t>((i - 1) / spf)) });
            in_sil = false;
        }
    }
    if (in_sil && total - start >= min_samples)
        out.push_back({ Issue::Type::SILENCE, s.f0 + static_cast<int32_t>(start / spf), s.f1 });
}

AnalyzeResult build_result(AnalysisState& s) {
    AnalyzeResult res;
    res.valid = true;
    res.f_start = s.f0;
    res.f_end = s.f1;
    res.snap = s.snap;
    res.fps_rate = s.rate;
    res.fps_scale = s.scale;
    res.sample_rate = s.sr;
    res.is_object = (s.target_obj != nullptr);
    res.obj_name = std::move(s.obj_name);
    res.true_peak = s.tp_peak > 0.0 ? 20.0 * std::log10(s.tp_peak) : SILENT_LUFS;

    const auto total = static_cast<int64_t>(s.kw_sq.size());
    const int32_t spf = s.samples_per_frame();
    const BlockEnergy blocks(s.kw_sq, s.step);

    WindowSweep mom = sweep_windows(blocks, total, s.mom_len, s.step, s.f0, spf);
    WindowSweep st = sweep_windows(blocks, total, s.st_len, s.step, s.f0, spf);

    res.integrated = integrated_lufs(mom);
    res.lra = loudness_range(st);
    res.mom_max = mom.max_lufs;
    res.mom_max_frame = mom.max_frame;
    res.st_max = st.max_lufs;
    res.st_max_frame = st.max_frame;
    res.mom_hist = std::move(mom.lufs);
    res.st_hist = std::move(st.lufs);

    detect_clips(s, res.issues);
    detect_silences(s, blocks, total, res.issues);
    return res;
}

void set_measure_buttons(bool busy) {
    EnableWindow(s_btn_rng, !busy);
    EnableWindow(s_btn_all, !busy);
    EnableWindow(s_btn_obj, !busy);
    EnableWindow(s_btn_abort, busy);
}

void finish_analysis(HWND hwnd) {
    if (!g_analysis) return;
    {
        AnalysisState& s = *g_analysis;
        if (s.abort_flag) {
            g_notice.store(Notice::Aborted);
        } else if (s.render_failed || s.kw_sq.empty()) {
            g_notice.store(Notice::Failed);
        } else {
            publish_result(std::make_shared<const AnalyzeResult>(build_result(s)));
            g_notice.store(Notice::Done);
        }
    }
    g_analysis.reset();
    g_busy.store(false);
    KillTimer(hwnd, ANALYSIS_TIMER_ID);
    PostMessage(hwnd, WM_DONE, 0, 0);
}

void accumulate_frame(AnalysisState& s, int32_t frame, const FrameAudio& fa) {
    const size_t n = std::min(fa.l.size(), fa.r.size());
    if (n == 0) return;

    const StereoGain g = (s.snap.use_obj_param && s.target_obj && s.vol.valid()) ? s.vol.gains_at(frame) : StereoGain{};

    const size_t base = s.kw_sq.size();
    s.kw_sq.resize(base + n);
    float* out = s.kw_sq.data() + base;

    double sample_peak = 0.0;
    double interp_peak = 0.0;
    for (size_t i = 0; i < n; i++) {
        const double lv = static_cast<double>(fa.l[i]) * g.l;
        const double rv = static_cast<double>(fa.r[i]) * g.r;
        out[i] = static_cast<float>(s.kw.power(lv, rv));
        sample_peak = std::max(sample_peak, std::max(std::abs(lv), std::abs(rv)));
        interp_peak = std::max(interp_peak, std::max(s.tp_l.push(lv), s.tp_r.push(rv)));
    }
    s.tp_peak = std::max({ s.tp_peak, sample_peak, interp_peak });
    s.fstats.push_back({ frame, sample_peak });
}

bool render_batch(AnalysisState& s, int32_t first, int32_t last) {
    for (int32_t f = first; f <= last; f++) {
        const bool ok = s.target_obj
                            ? g_edit_handle->rendering_object_audio(s.target_obj, f, s.apply_effect, &s.batch, audio_cb)
                            : g_edit_handle->rendering_scene_audio(f, &s.batch, audio_cb);
        if (!ok) return false;
    }
    return true;
}

void analysis_step(HWND hwnd) {
    if (!g_analysis) return;
    AnalysisState& s = *g_analysis;

    if (g_abort.load()) {
        s.abort_flag = true;
        finish_analysis(hwnd);
        return;
    }
    if (s.next_frame > s.f1) {
        finish_analysis(hwnd);
        return;
    }

    const int32_t first = s.next_frame;
    const int32_t last = std::min(first + s.batch_size - 1, s.f1);
    s.batch.begin(first, last - first + 1);

    const bool rendered = render_batch(s, first, last);
    g_edit_handle->wait_rendering_task();
    if (!rendered) {
        s.render_failed = true;
        finish_analysis(hwnd);
        return;
    }

    {
        std::lock_guard lk(s.batch.mtx);
        for (int32_t f = first; f <= last; f++)
            accumulate_frame(s, f, s.batch.frames[static_cast<size_t>(f - first)]);
    }

    const int32_t prog = static_cast<int32_t>(100.0 * (last - s.f0 + 1) / s.total_frames);
    g_prog.store(prog);
    PostMessage(hwnd, WM_PROGRESS, prog, 0);

    s.next_frame = first + s.batch_size;
}

void start_analysis(AnalysisRequest req) {
    if (g_busy.exchange(true)) return;

    EDIT_INFO info;
    g_edit_handle->get_edit_info(&info, sizeof(info));
    const int32_t f_start = std::max(0, req.f_start);
    const int32_t f_end = std::min(req.f_end, info.frame_max);
    if (f_start > f_end) {
        g_busy.store(false);
        return;
    }

    g_abort.store(false);
    g_prog.store(0);
    g_notice.store(Notice::None);
    set_measure_buttons(true);
    InvalidateRect(g_hwnd, nullptr, TRUE);
    UpdateWindow(g_hwnd);

    g_analysis = std::make_unique<AnalysisState>();
    AnalysisState& s = *g_analysis;
    s.f0 = f_start;
    s.f1 = f_end;
    s.rate = info.rate;
    s.scale = info.scale;
    s.sr = info.sample_rate;
    s.snap = settings.analyzer;
    s.target_obj = req.obj;
    s.obj_name = std::move(req.obj_name);
    s.apply_effect = req.apply_effect;
    s.vol = std::move(req.vol);
    s.mom_len = s.sr * 400 / 1000;
    s.st_len = s.sr * 3;
    s.step = s.sr * 100 / 1000;
    s.total_frames = f_end - f_start + 1;
    s.batch_size = normalize_batch_size(settings.analyzer.batch_size);
    s.next_frame = f_start;
    s.kw.init(s.sr);
    s.tp_coef.init(s.sr);
    s.tp_l.coef = &s.tp_coef;
    s.tp_r.coef = &s.tp_coef;
    s.kw_sq.reserve(static_cast<size_t>(s.total_frames) * (static_cast<size_t>(s.samples_per_frame()) + 1));
    s.fstats.reserve(s.total_frames);

    SetTimer(g_hwnd, ANALYSIS_TIMER_ID, 1, nullptr);
}

AnalysisRequest request_from_pick(ObjPickResult& pick, bool apply_effect) {
    return { .f_start = pick.f_start, .f_end = pick.f_end, .obj = pick.obj, .obj_name = std::move(pick.name), .apply_effect = apply_effect, .vol = std::move(pick.vol) };
}

void refresh_volume_fix_proposal() {
    const auto res = current_result();
    if (res->valid && res->is_object && res->integrated > -99.0) {
        g_vol_fix_panel.set_proposal(make_volume_fix_proposal(res->f_start, res->obj_name, res->integrated, res->true_peak,
                                                              res->snap.target_lufs, res->snap.target_peak));
    } else {
        g_vol_fix_panel.set_proposal(VolumeFixProposal{});
    }
}

bool confirm_warning(const wchar_t* text) {
    return MessageBoxW(g_hwnd, text, APP_NAME, MB_ICONWARNING | MB_YESNO) == IDYES;
}

void apply_volume_fix_and_remeasure() {
    if (g_busy.load()) return;
    VolumeFixProposal prop = g_vol_fix_panel.current_proposal_with_edit();
    if (!prop.valid) return;

    if (prop.exceeds_ceiling()) {
        const double predicted = prop.predicted_peak();
        const double ceiling = prop.peak_ceiling;
        const std::wstring msg = std::vformat(TrText(L"適用するとTrue Peakが約{:.1f} dBTPになり、上限({:.1f} dBTP)を超える見込みです。\nこのまま適用しますか？"),
                                              std::make_wformat_args(predicted, ceiling));
        if (!confirm_warning(msg.c_str())) return;
    }
    if (std::abs(prop.user_gain_db) <= 0.01 && !confirm_warning(TrText(L"適用結果にほぼ変化が無い見込みです。\nこのまま適用しますか？")))
        return;

    bool ok = false;
    struct Ctx {
        const VolumeFixProposal* prop;
        bool* ok;
    } ctx{ &prop, &ok };
    g_edit_handle->call_edit_section_param(&ctx, [](void* p, EDIT_SECTION* edit) {
        auto* c = static_cast<Ctx*>(p);
        *c->ok = apply_volume_fix(edit, *c->prop);
    });

    if (!ok) {
        DbgMessage(TrText(L"音量の調整に失敗しました。対象オブジェクトの選択状態を確認してください。"), LOG_WARN);
        return;
    }

    ObjPickResult pick;
    g_edit_handle->call_read_section_param(&pick, pick_target_object_cb);
    if (pick.found) start_analysis(request_from_pick(pick, true));
}

void read_edit_into(HWND edit, double lower_bound, double& target) {
    wchar_t buf[32];
    GetWindowTextW(edit, buf, 32);
    const double v = wcstod(buf, nullptr);
    if (v < 0.0 && v >= lower_bound) target = v;
}

void read_controls_to_settings() {
    read_edit_into(s_edit_l, -60.0, settings.analyzer.target_lufs);
    read_edit_into(s_edit_p, -20.0, settings.analyzer.target_peak);
    read_edit_into(s_edit_sil, -90.0, settings.analyzer.sil_db);
}

void write_settings_to_controls() {
    const auto set = [](HWND edit, double v) { SetWindowTextW(edit, std::format(L"{:.1f}", v).c_str()); };
    set(s_edit_l, settings.analyzer.target_lufs);
    set(s_edit_p, settings.analyzer.target_peak);
    set(s_edit_sil, settings.analyzer.sil_db);
    SendMessage(s_combo, CB_SETCURSEL, find_preset_index(), 0);
}

void frame_change_cb(void*) {
    EDIT_INFO info;
    g_edit_handle->get_edit_info(&info, sizeof(info));
    g_cur_frame.store(info.frame);
    PostMessage(g_hwnd, WM_FRAME_CHANGE, 0, 0);
}

bool parse_hex_color(const std::wstring& raw, COLORREF& out) {
    std::wstring s = TrimCopy(raw);
    if (!s.empty() && s[0] == L'#') s.erase(0, 1);
    if (s.size() != 6 && s.size() != 8) return false;
    if (!std::ranges::all_of(s, [](wchar_t c) { return std::iswxdigit(c) != 0; })) return false;
    const auto v = static_cast<uint32_t>(std::stoul(s.substr(0, 6), nullptr, 16));
    out = RGB((v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF);
    return true;
}

bool read_host_color(const AnalyzerColorSlot& sl, COLORREF& out) {
    if (!sl.host_key || !g_config_handle || !g_config_handle->get_color_code_index) return false;
    if (g_config_handle->get_color_code_index(g_config_handle, sl.host_key, -1) <= sl.host_index) return false;
    const auto v = static_cast<uint32_t>(g_config_handle->get_color_code_index(g_config_handle, sl.host_key, sl.host_index)) & 0xFFFFFF;
    out = RGB((v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF);
    return true;
}

void load_analyzer_colors() {
    const AnalyzerColorSlot* slots = analyzer_color_slots();
    for (int32_t i = 0; i < AC_COUNT; i++) {
        COLORREF c = slots[i].def;
        if (settings.analyzer.use_host_color) read_host_color(slots[i], c);
        parse_hex_color(settings.analyzer.color_override[i], c);
        *slots[i].target = c;
    }
    analyzer_colors_update_derived();
}

void ensure_fonts() {
    if (g_fL && g_fLB && g_fS) return;

    if (g_font_name.empty())
        g_font_name = g_config_handle->get_font_info(g_config_handle, "DefaultFamily")->name;
    const auto control_size = [] {
        return static_cast<int32_t>(g_config_handle->get_font_info(g_config_handle, "Control")->size);
    };
    const auto make_font = [](int32_t size, int32_t weight) {
        return CreateFontW(size, 0, 0, 0, weight, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, g_font_name.c_str());
    };

    int32_t size_l = settings.analyzer.font_L;
    if (size_l < 1) {
        const int32_t c = control_size();
        size_l = -(c + (c >> 1));
    }
    int32_t size_s = settings.analyzer.font_S;
    if (size_s < 1) size_s = -control_size();

    if (!g_fL) g_fL = make_font(size_l, FW_NORMAL);
    if (!g_fLB) g_fLB = make_font(size_l, FW_BOLD);
    if (!g_fS) g_fS = make_font(size_s, FW_NORMAL);
}

void delete_gdi(HGDIOBJ& obj) {
    if (obj) {
        DeleteObject(obj);
        obj = nullptr;
    }
}

void fill_rect(HDC dc, const RECT& r, COLORREF c) {
    SetDCBrushColor(dc, c);
    FillRect(dc, &r, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
}

void frame_rect(HDC dc, const RECT& r, COLORREF c) {
    SetDCBrushColor(dc, c);
    FrameRect(dc, &r, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
}

[[nodiscard]] COLORREF darken(COLORREF c, int32_t divisor) noexcept {
    return RGB(GetRValue(c) / divisor, GetGValue(c) / divisor, GetBValue(c) / divisor);
}

class ScopedPen {
  public:
    ScopedPen(HDC dc, int32_t style, int32_t width, COLORREF color)
        : dc_(dc), pen_(CreatePen(style, width, color)), old_(static_cast<HPEN>(SelectObject(dc, pen_))) {}
    ~ScopedPen() {
        SelectObject(dc_, old_);
        DeleteObject(pen_);
    }
    ScopedPen(const ScopedPen&) = delete;
    ScopedPen& operator=(const ScopedPen&) = delete;

  private:
    HDC dc_;
    HPEN pen_;
    HPEN old_;
};

void draw_line(HDC dc, int32_t x0, int32_t y0, int32_t x1, int32_t y1, COLORREF color, int32_t width = 1, int32_t style = PS_SOLID) {
    const ScopedPen pen(dc, style, width, color);
    MoveToEx(dc, x0, y0, nullptr);
    LineTo(dc, x1, y1);
}

void dtw(HDC dc, const wchar_t* s, RECT r, COLORREF c, UINT fmt = TXT_LEFT) {
    SetTextColor(dc, c);
    DrawTextW(dc, s, -1, &r, fmt);
}

void draw_checkbox(HDC dc, int32_t x, int32_t y, bool checked) {
    constexpr int32_t SIZE = 12;
    const RECT box = { x, y, x + SIZE, y + SIZE };
    fill_rect(dc, box, checked ? C_PASS : C_BOX_BG);
    frame_rect(dc, box, checked ? C_PASS : C_BORDER);
    if (checked) {
        const ScopedPen pen(dc, PS_SOLID, 2, C_BACKGROUND);
        MoveToEx(dc, x + 2, y + 6, nullptr);
        LineTo(dc, x + 5, y + 9);
        LineTo(dc, x + 10, y + 2);
    }
}

std::wstring fmtL(double v) {
    return v <= -99.0 ? std::wstring(L"---") : std::format(L"{:.1f}", v);
}

std::wstring fmtDiff(double d) {
    return std::format(L"{:+.1f} LU", d);
}

std::wstring fmtTimecode(int32_t frame, int32_t rate, int32_t scale) {
    if (frame < 0 || rate <= 0) return L"";
    const double fps = static_cast<double>(rate) / std::max(1, scale);
    const double total_sec = frame / fps;
    const auto whole_sec = static_cast<int32_t>(total_sec);
    const int32_t hh = whole_sec / 3600;
    const int32_t mm = (whole_sec / 60) % 60;
    const double ss = total_sec - hh * 3600 - mm * 60;
    if (hh > 0) return std::format(L"{:d}:{:02d}:{:05.2f} (F{:d})", hh, mm, ss, frame + 1);
    return std::format(L"{:d}:{:05.2f} (F{:d})", mm, ss, frame + 1);
}

JudgeStatus judge_lufs(double v, const AnalyzerConfig& s) {
    if (v <= -99.0) return JudgeStatus::NONE;
    const double d = v - s.target_lufs;
    if (std::abs(d) <= s.lufs_tol) return JudgeStatus::PASS;
    if (d > s.lufs_tol + s.lufs_fail) return JudgeStatus::FAIL;
    if (d > s.lufs_tol || d <= -s.lufs_warn) return JudgeStatus::WARN;
    return JudgeStatus::PASS;
}

JudgeStatus judge_peak(double v, const AnalyzerConfig& s) {
    if (v <= -99.0) return JudgeStatus::NONE;
    if (v <= s.target_peak) return JudgeStatus::PASS;
    if (v <= s.target_peak + s.peak_fail) return JudgeStatus::WARN;
    return JudgeStatus::FAIL;
}

COLORREF judge_color(JudgeStatus j) {
    switch (j) {
        case JudgeStatus::PASS:
            return C_PASS;
        case JudgeStatus::WARN:
            return C_WARN;
        case JudgeStatus::FAIL:
            return C_FAIL;
        default:
            return C_TEXT_SUB;
    }
}

const wchar_t* judge_text(JudgeStatus j) {
    static constexpr const wchar_t* TEXT[] = { L"", L"PASS", L"WARN", L"FAIL" };
    return TEXT[static_cast<size_t>(j)];
}

void draw_badge(HDC dc, int32_t x, int32_t y, int32_t w, JudgeStatus j) {
    if (j == JudgeStatus::NONE) return;
    const COLORREF col = judge_color(j);
    fill_rect(dc, { x, y + 1, x + w, y + 15 }, darken(col, 5));
    SelectObject(dc, g_fS);
    dtw(dc, judge_text(j), { x, y, x + w, y + 16 }, col, TXT_CENTER);
}

void draw_graph(HDC dc, const RECT& area, const std::vector<double>& st, const std::vector<double>& mom,
                double target_lufs, double cursor_frac, const GraphView& view) {
    const int32_t gx = area.left;
    const int32_t gy = area.top;
    const int32_t gw = area.right - area.left;
    const int32_t gh = area.bottom - area.top;
    const double v_min = view.v_min;
    const double v_max = view.v_max;

    const auto yx = [&](double l) -> int32_t {
        l = std::clamp(l, v_min, v_max);
        return gy + gh - static_cast<int32_t>((l - v_min) / (v_max - v_min) * gh);
    };
    const auto xx = [&](int32_t i, int32_t n) -> int32_t {
        const double t = (n > 1) ? static_cast<double>(i) / (n - 1) : 0.0;
        return gx + static_cast<int32_t>((t - view.t_pan) * view.t_zoom * gw);
    };

    fill_rect(dc, area, C_GRAPH_BG);

    const double v_range = v_max - v_min;
    const int32_t gstep = (v_range > 30) ? 10 : (v_range > 12) ? 5
                                                               : 2;
    const auto for_each_grid_db = [&](auto&& fn) {
        for (auto db = static_cast<int32_t>(std::floor(v_min / gstep) * gstep); static_cast<double>(db) <= v_max; db += gstep)
            if (static_cast<double>(db) >= v_min) fn(db);
    };

    {
        const ScopedPen pen(dc, PS_SOLID, 1, C_GRAPH_GRID);
        for_each_grid_db([&](int32_t db) {
            const int32_t yy = yx(static_cast<double>(db));
            MoveToEx(dc, gx, yy, nullptr);
            LineTo(dc, gx + gw, yy);
        });
    }

    const bool target_visible = target_lufs > v_min && target_lufs < v_max;
    if (target_visible) draw_line(dc, gx, yx(target_lufs), gx + gw, yx(target_lufs), C_TARGET_LINE, 1, PS_DOT);

    SelectObject(dc, g_fS);
    for_each_grid_db([&](int32_t db) {
        const int32_t yy = yx(static_cast<double>(db));
        dtw(dc, std::format(L"{:d}", db).c_str(), { gx + gw + 2, yy - 7, gx + gw + 30, yy + 7 }, C_GRAPH_LABEL);
    });
    if (target_visible) {
        const int32_t ty = yx(target_lufs);
        dtw(dc, std::format(L"{:.0f}", target_lufs).c_str(), { gx + gw + 2, ty - 7, gx + gw + 30, ty + 7 }, C_TARGET_LABEL);
    }

    const double t_end = view.t_pan + 1.0 / view.t_zoom;
    std::vector<POINT> pts;
    const auto draw_series = [&](const std::vector<double>& h, COLORREF col) {
        const int32_t n = static_cast<int32_t>(h.size());
        if (n < 2) return;
        const int32_t i0 = std::max(0, static_cast<int32_t>(view.t_pan * (n - 1)) - 1);
        const int32_t i1 = std::min(n - 1, static_cast<int32_t>(t_end * (n - 1)) + 1);
        if (i0 >= i1) return;
        pts.clear();
        pts.reserve(static_cast<size_t>(i1 - i0 + 1));
        for (int32_t i = i0; i <= i1; ++i) pts.push_back({ xx(i, n), yx(h[i]) });
        const ScopedPen pen(dc, PS_SOLID, 1, col);
        Polyline(dc, pts.data(), static_cast<int32_t>(pts.size()));
    };
    draw_series(st, C_SHORT_TERM);
    draw_series(mom, C_MOMENTARY);

    if (cursor_frac >= 0.0 && cursor_frac <= 1.0) {
        const double vis = (cursor_frac - view.t_pan) * view.t_zoom;
        if (vis >= 0.0 && vis <= 1.0) {
            const int32_t cx = gx + static_cast<int32_t>(vis * gw);
            draw_line(dc, cx, gy, cx, gy + gh, C_TEXT);
        }
    }

    SelectObject(dc, g_fS);
    int32_t lx = gx + 4;
    struct Legend {
        const wchar_t* label;
        COLORREF col;
    };
    for (const Legend& lg : { Legend{ L"Short-term", C_SHORT_TERM }, Legend{ L"Momentary", C_MOMENTARY } }) {
        draw_line(dc, lx, gy + 8, lx + 14, gy + 8, lg.col, 2);
        dtw(dc, lg.label, { lx + 16, gy, lx + 110, gy + 16 }, lg.col);
        lx += 112;
    }

    if (view.t_zoom > 1.01 || !view.value_axis_is_default()) {
        std::wstring zi;
        if (view.t_zoom > 1.01) zi = std::format(L"T×{:.1f} ", view.t_zoom);
        zi += TrText(L"右クリックでリセット");
        dtw(dc, zi.c_str(), { lx, gy, gx + gw, gy + 14 }, C_GRAPH_LABEL, DT_RIGHT | DT_TOP | DT_SINGLELINE);
    }
}

struct BackBuffer {
    HDC dc = nullptr;
    HBITMAP bmp = nullptr;
    HGDIOBJ original = nullptr;
    int32_t w = 0;
    int32_t h = 0;

    HDC acquire(HDC ref, int32_t width, int32_t height) {
        if (!dc) {
            dc = CreateCompatibleDC(ref);
            SetBkMode(dc, TRANSPARENT);
        }
        if (!bmp || width != w || height != h) {
            HBITMAP fresh = CreateCompatibleBitmap(ref, width, height);
            HGDIOBJ prev = SelectObject(dc, fresh);
            if (!original) original = prev;
            if (bmp) DeleteObject(bmp);
            bmp = fresh;
            w = width;
            h = height;
        }
        return dc;
    }

    void destroy() {
        if (dc && original) SelectObject(dc, original);
        if (bmp) DeleteObject(bmp);
        if (dc) DeleteDC(dc);
        *this = {};
    }
};
BackBuffer g_backbuf;

void reset_hit_regions() {
    s_issue_rows.clear();
    s_graph = {};
    std::ranges::fill(s_flt_btn, RECT{});
    std::ranges::fill(s_maxrow_hits, RECT{});
    std::ranges::fill(s_maxrow_frames, -1);
    s_next_unseen_btn = {};
    s_sb.valid = false;
}

void draw_toolbar_labels(HDC dc, int32_t W) {
    SelectObject(dc, g_fS);
    dtw(dc, TrText(L"目標 LUFS"), { 64, 74, 128, 94 }, C_TEXT_SUB);
    dtw(dc, TrText(L"TP Limit"), { 196, 74, 246, 94 }, C_TEXT_SUB);
    dtw(dc, TrText(L"無音 dBFS"), { 304, 74, 370, 94 }, C_TEXT_SUB);

    const Notice nt = g_notice.load();
    if (!g_busy.load() && nt != Notice::None) {
        const wchar_t* msg = nullptr;
        COLORREF col = C_TEXT_SUB;
        switch (nt) {
            case Notice::Done:
                msg = TrText(L"計測が完了しました");
                col = C_PASS;
                break;
            case Notice::Aborted:
                msg = TrText(L"計測を中止しました(前回の結果を表示中)");
                break;
            default:
                msg = TrText(L"計測に失敗しました(前回の結果を表示中)");
                col = judge_color(JudgeStatus::WARN);
                break;
        }
        dtw(dc, msg, { 316, 42, W - 8, 68 }, col);
    }

    draw_checkbox(dc, s_keep_seen_hit.left + 4, s_keep_seen_hit.top + 4, g_keep_seen);
    dtw(dc, TrText(L"確認済を維持"), { s_keep_seen_hit.left + 22, s_keep_seen_hit.top, s_keep_seen_hit.right, s_keep_seen_hit.bottom }, C_TEXT_SUB);

    draw_line(dc, 8, 100, W - 8, 100, C_BORDER);
}

void draw_progress(HDC dc, int32_t W, int32_t y) {
    const int32_t prog = g_prog.load();
    const RECT pb = { 8, y, W - 8, y + 22 };
    fill_rect(dc, pb, C_PANEL);
    fill_rect(dc, { 8, y, 8 + static_cast<int32_t>((W - 16) * prog / 100.0), y + 22 }, C_PROGRESS);
    SelectObject(dc, g_fS);
    dtw(dc, (std::wstring(TrText(L"計測中")) + std::format(L"{:d}%", prog)).c_str(), pb, C_TEXT, TXT_CENTER);
}

struct SummaryRow {
    const wchar_t* label;
    std::wstring value;
    const wchar_t* unit;
    JudgeStatus judge;
    int32_t jump_frame;
};

int32_t draw_summary_panel(HDC dc, int32_t W, int32_t y, const AnalyzeResult& res) {
    const AnalyzerConfig& sn = res.snap;
    const double ldiff = res.integrated - sn.target_lufs;
    const int32_t panel_h = 140 + g_vol_fix_panel.height();
    fill_rect(dc, { 8, y, W - 8, y + panel_h }, C_PANEL);

    const std::array<SummaryRow, MAXROW_COUNT> rows{
        SummaryRow{ L"Integrated LUFS", fmtL(res.integrated), L"LUFS", judge_lufs(res.integrated, sn), -1 },
        SummaryRow{ L"True Peak", fmtL(res.true_peak), L"dBTP", judge_peak(res.true_peak, sn), -1 },
        SummaryRow{ L"Loudness Range", std::format(L"{:.1f}", res.lra), L"LU", JudgeStatus::NONE, -1 },
        SummaryRow{ L"Max Short-term", fmtL(res.st_max), L"LUFS", JudgeStatus::NONE, res.st_max_frame },
        SummaryRow{ L"Max Momentary", fmtL(res.mom_max), L"LUFS", JudgeStatus::NONE, res.mom_max_frame },
    };

    constexpr int32_t RH = 25;
    for (int32_t i = 0; i < MAXROW_COUNT; i++) {
        const int32_t ry = y + 1 + i * RH;
        const SummaryRow& row = rows[i];

        SelectObject(dc, g_fS);
        dtw(dc, row.label, { 14, ry + 6, 140, ry + RH }, C_TEXT_SUB);
        SelectObject(dc, g_fLB);
        const COLORREF vc = (row.judge != JudgeStatus::NONE) ? judge_color(row.judge) : C_TEXT;
        dtw(dc, row.value.c_str(), { 138, ry + 2, 222, ry + RH }, vc, TXT_RIGHT);
        SelectObject(dc, g_fS);
        dtw(dc, row.unit, { 224, ry + 6, 252, ry + RH }, C_TEXT_SUB);
        draw_badge(dc, 256, ry + 5, 42, row.judge);

        if (i == 0 && res.integrated > -99.0) {
            const COLORREF dcol = std::abs(ldiff) < sn.lufs_tol ? C_PASS : (ldiff > 0 ? C_FAIL : C_TEXT_SUB);
            dtw(dc, fmtDiff(ldiff).c_str(), { 300, ry + 6, W - 10, ry + RH }, dcol, TXT_RIGHT);
        }
        if (row.jump_frame >= 0) {
            dtw(dc, fmtTimecode(row.jump_frame, res.fps_rate, res.fps_scale).c_str(), { 220, ry + 2, W - 10, ry + RH }, C_ACCENT, TXT_RIGHT);
            s_maxrow_hits[i] = { 8, ry, W - 8, ry + RH };
            s_maxrow_frames[i] = row.jump_frame;
        }
        if (i < MAXROW_COUNT - 1) draw_line(dc, 14, ry + RH, W - 14, ry + RH, C_SEPARATOR);
    }

    std::wstring range_text;
    if (res.is_object) {
        std::wstring nm = res.obj_name.empty() ? std::wstring(TrText(L"(名称未設定)")) : res.obj_name;
        if (nm.size() > 60) nm = nm.substr(0, 60) + L"...";
        range_text = std::format(L"[{}]  F{:d} 〜 F{:d}", nm, res.f_start + 1, res.f_end + 1);
    } else {
        range_text = std::format(L"{}  F{:d} 〜 F{:d}", std::wstring(TrText(L"解析範囲")), res.f_start + 1, res.f_end + 1);
    }
    dtw(dc, range_text.c_str(), { 14, y + 128, W - 14, y + 140 }, C_TEXT_SUB, TXT_RIGHT);

    if (g_vol_fix_panel.visible()) {
        const RECT vb = { 14, y + 140, W - 14, y + panel_h };
        g_vol_fix_panel.paint_and_layout(dc, vb);
    }
    return panel_h;
}

void draw_cursor_readout(HDC dc, int32_t y, int32_t W, const AnalyzeResult& res, int32_t cur) {
    const int32_t idx = [&]() -> int32_t {
        if (res.fps_rate == 0 || res.sample_rate == 0) return -1;
        const int32_t spf = res.sample_rate * res.fps_scale / std::max(1, res.fps_rate);
        const int32_t step = res.sample_rate * 100 / 1000;
        const int64_t rel = cur - res.f_start;
        if (step == 0 || rel < 0) return -1;
        return static_cast<int32_t>((rel * spf) / step);
    }();
    const auto at = [idx](const std::vector<double>& h) { return (idx >= 0 && idx < std::ssize(h)) ? h[idx] : SILENT_LUFS; };

    fill_rect(dc, { 8, y, W - 8, y + 20 }, C_PANEL);
    SelectObject(dc, g_fS);
    dtw(dc, std::format(L"F{:d}", cur + 1).c_str(), { 14, y + 2, 58, y + 20 }, C_TEXT_SUB);
    dtw(dc, L"Mom", { 60, y + 2, 90, y + 20 }, C_TEXT_SUB);
    dtw(dc, fmtL(at(res.mom_hist)).c_str(), { 90, y + 2, 140, y + 20 }, C_MOMENTARY, TXT_RIGHT);
    dtw(dc, L"LUFS", { 142, y + 2, 172, y + 20 }, C_TEXT_SUB);
    dtw(dc, L"ST", { 182, y + 2, 202, y + 20 }, C_TEXT_SUB);
    dtw(dc, fmtL(at(res.st_hist)).c_str(), { 202, y + 2, 256, y + 20 }, C_SHORT_TERM, TXT_RIGHT);
    dtw(dc, L"LUFS", { 258, y + 2, 288, y + 20 }, C_TEXT_SUB);
}

void draw_issue_list(HDC dc, int32_t W, int32_t H, int32_t y, const AnalyzeResult& res) {
    const std::vector<int32_t> flt = filtered_issue_indices(res);
    const auto total_iss = static_cast<int32_t>(res.issues.size());
    const auto flt_cnt = static_cast<int32_t>(flt.size());

    y += 4;
    SelectObject(dc, g_fS);
    dtw(dc, TrText(L"問題箇所"), { 8, y, 108, y + 16 }, C_WARN);

    const wchar_t* const flt_lbl[FILTER_COUNT] = { L"All", L"C", L"S" };
    const COLORREF flt_col[FILTER_COUNT] = { C_TEXT, C_FAIL, C_TEXT_SUB };
    int32_t bx = 112;
    for (int32_t fi = 0; fi < FILTER_COUNT; fi++) {
        s_flt_btn[fi] = { bx, y, bx + 22, y + 16 };
        const bool sel = (g_issue_filter == fi);
        fill_rect(dc, s_flt_btn[fi], sel ? darken(flt_col[fi], 6) : C_CONTROL);
        frame_rect(dc, s_flt_btn[fi], sel ? flt_col[fi] : C_BORDER);
        dtw(dc, flt_lbl[fi], s_flt_btn[fi], sel ? flt_col[fi] : C_TEXT_SUB, TXT_CENTER);
        bx += 26;
    }

    const bool has_unseen = std::ranges::any_of(flt, [&](int32_t i) { return !is_seen(res.issues[i]); });
    const RECT nb = { bx + 6, y, bx + 6 + 92, y + 16 };
    if (has_unseen) s_next_unseen_btn = nb;
    dtw(dc, TrText(L"▶ 次の未確認"), nb, has_unseen ? C_ACCENT : C_TEXT_DISABLED);

    const auto seen_cnt = static_cast<int32_t>(std::ranges::count_if(flt, [&](int32_t i) { return is_seen(res.issues[i]); }));
    const std::wstring cnt = (g_issue_filter != FILTER_ALL)
                                 ? std::format(L"{} / {} issues (✓{})", flt_cnt, total_iss, seen_cnt)
                                 : std::format(L"{} issues (✓{})", total_iss, seen_cnt);
    dtw(dc, cnt.c_str(), { W - 130, y, W - 8, y + 16 }, C_TEXT_SUB, TXT_RIGHT);
    y += 18;

    constexpr int32_t ITEM_H = 18;
    const int32_t vis_n = std::max(1, (H - y - 2) / ITEM_H);
    const int32_t max_sc = std::max(0, flt_cnt - vis_n);
    s_issues_scroll = std::min(s_issues_scroll, max_sc);

    const bool need_sb = flt_cnt > vis_n;
    const int32_t SB_W = need_sb ? 6 : 0;
    const int32_t list_r = W - 8 - SB_W - (need_sb ? 3 : 0);

    int32_t drawn = 0;
    for (int32_t i = s_issues_scroll; i < flt_cnt && drawn < vis_n; i++, drawn++) {
        const int32_t iy = y + drawn * ITEM_H;
        if (iy + ITEM_H > H) break;
        const Issue& iss = res.issues[flt[i]];

        if (s_hovered_issue == drawn) fill_rect(dc, { 8, iy, list_r, iy + ITEM_H }, C_ROW_HOVER);

        const IssueId sid = issue_seen_id(iss);
        const bool seen = g_seen_issues.contains(sid);
        s_issue_rows.push_back({ iy, iy + ITEM_H, iss.f_start, sid });
        draw_checkbox(dc, 14, iy + (ITEM_H - 12) / 2, seen);

        const bool is_clip = iss.type == Issue::Type::CLIP;
        const COLORREF base_col = is_clip ? C_FAIL : C_TEXT_SUB;
        const std::wstring name = is_clip ? std::wstring(TrText(L"クリッピング")) : std::wstring(TrText(L"無音区間"));
        const std::wstring line = std::format(L"{} {}  F{:d}〜F{:d}", is_clip ? L"●" : L"○", name, iss.f_start + 1, iss.f_end + 1);
        dtw(dc, line.c_str(), { 32, iy, list_r, iy + ITEM_H }, seen ? darken(base_col, 2) : base_col);
    }

    if (need_sb && drawn > 0) {
        const int32_t sbx = W - 8 - SB_W;
        const int32_t sbh = drawn * ITEM_H;
        fill_rect(dc, { sbx, y, sbx + SB_W, y + sbh }, C_PANEL);

        const float vr = static_cast<float>(vis_n) / flt_cnt;
        const float pr = (max_sc > 0) ? static_cast<float>(s_issues_scroll) / max_sc : 0.0f;
        const int32_t thumb_h = std::max(12, static_cast<int32_t>(sbh * vr));
        const int32_t thumb_y = y + static_cast<int32_t>((sbh - thumb_h) * pr);
        fill_rect(dc, { sbx, thumb_y, sbx + SB_W, thumb_y + thumb_h }, s_sb_drag ? C_ACCENT : C_TEXT_SUB);
        s_sb = { sbx, SB_W, y, sbh, thumb_y, thumb_h, vis_n, max_sc, true };
    }
}

void paint_window(HWND hwnd) {
    reset_hit_regions();

    PAINTSTRUCT ps;
    const HDC hdc = BeginPaint(hwnd, &ps);
    RECT client;
    GetClientRect(hwnd, &client);
    const int32_t W = client.right;
    const int32_t H = client.bottom;

    if (W > 0 && H > 0) {
        ensure_fonts();
        const HDC mdc = g_backbuf.acquire(hdc, W, H);
        fill_rect(mdc, client, C_BACKGROUND);
        draw_toolbar_labels(mdc, W);

        int32_t y = 104;
        if (g_busy.load()) {
            draw_progress(mdc, W, y);
        } else {
            const auto res_ptr = current_result();
            const AnalyzeResult& res = *res_ptr;
            if (!res.valid) {
                SelectObject(mdc, g_fS);
                dtw(mdc, TrText(L"「計測」ボタンを押してください"), { 0, y + 16, W, y + 36 }, C_TEXT_SUB, TXT_CENTER);
            } else {
                y += draw_summary_panel(mdc, W, y, res) + 4;

                if (!res.st_hist.empty() || !res.mom_hist.empty()) {
                    constexpr int32_t MARGIN_R = 32;
                    s_graph = { 8, y + 4, W - MARGIN_R, y + 4 + 108 };

                    const int32_t cur = g_cur_frame.load();
                    const bool cur_in_range = cur >= res.f_start && cur <= res.f_end;
                    const double cursor_frac = (cur_in_range && res.f_end > res.f_start)
                                                   ? static_cast<double>(cur - res.f_start) / (res.f_end - res.f_start)
                                                   : -1.0;
                    draw_graph(mdc, s_graph, res.st_hist, res.mom_hist, res.snap.target_lufs, cursor_frac, g_view);
                    y += 120;

                    if (cur_in_range) {
                        draw_cursor_readout(mdc, y, W, res, cur);
                        y += 22;
                    }
                }

                if (!res.issues.empty()) draw_issue_list(mdc, W, H, y, res);
            }
        }

        const RECT& rc = ps.rcPaint;
        BitBlt(hdc, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top, mdc, rc.left, rc.top, SRCCOPY);
    }
    EndPaint(hwnd, &ps);
}

void zoom_time_axis(double anchor_frac, bool zoom_in) {
    const double data_at = g_view.t_pan + anchor_frac / g_view.t_zoom;
    g_view.t_zoom = std::clamp(g_view.t_zoom * (zoom_in ? 1.5 : 1.0 / 1.5), 1.0, 64.0);
    g_view.t_pan = data_at - anchor_frac / g_view.t_zoom;
    g_view.clamp_pan();
}

void zoom_value_axis(double anchor_frac, bool zoom_in) {
    const double v_at = g_view.v_max + anchor_frac * (g_view.v_min - g_view.v_max);
    const double fac = zoom_in ? 1.0 / 1.5 : 1.5;
    const double new_min = v_at + (g_view.v_min - v_at) * fac;
    const double new_max = v_at + (g_view.v_max - v_at) * fac;
    const double range = new_max - new_min;
    if (range >= 4.0 && range <= 80.0) {
        g_view.v_min = std::max(-90.0, new_min);
        g_view.v_max = std::min(12.0, new_max);
    }
}

void on_mouse_wheel(HWND hwnd, WPARAM wp) {
    POINT pt;
    GetCursorPos(&pt);
    ScreenToClient(hwnd, &pt);
    const int32_t delta = static_cast<int32_t>(GET_WHEEL_DELTA_WPARAM(wp)) / WHEEL_DELTA;
    const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;

    if (hit(s_graph, pt.x, pt.y)) {
        if (ctrl) {
            zoom_time_axis(static_cast<double>(pt.x - s_graph.left) / graph_w(), delta > 0);
        } else if (shift) {
            zoom_value_axis(static_cast<double>(pt.y - s_graph.top) / graph_h(), delta > 0);
        } else if (g_view.t_zoom > 1.01) {
            g_view.t_pan -= delta * 0.3 / g_view.t_zoom;
            g_view.clamp_pan();
        }
    } else {
        s_issues_scroll = std::max(0, s_issues_scroll - delta);
    }
    InvalidateRect(hwnd, nullptr, FALSE);
}

void jump_to_next_unseen() {
    const auto res = current_result();
    const std::vector<int32_t> flt = filtered_issue_indices(*res);
    for (int32_t i = 0; i < std::ssize(flt); i++) {
        const Issue& iss = res->issues[flt[i]];
        if (is_seen(iss)) continue;
        focus_frame(iss.f_start);
        s_issues_scroll = i;
        return;
    }
}

void on_lbutton_down(HWND hwnd, int32_t mx, int32_t my) {
    const auto redraw = [hwnd] { InvalidateRect(hwnd, nullptr, FALSE); };

    if (hit(s_keep_seen_hit, mx, my)) {
        g_keep_seen = !g_keep_seen;
        redraw();
        return;
    }
    for (int32_t i = 0; i < MAXROW_COUNT; i++)
        if (s_maxrow_frames[i] >= 0 && hit(s_maxrow_hits[i], mx, my)) {
            focus_frame(s_maxrow_frames[i]);
            redraw();
            return;
        }
    if (hit(s_next_unseen_btn, mx, my)) {
        jump_to_next_unseen();
        redraw();
        return;
    }
    for (int32_t fi = 0; fi < FILTER_COUNT; fi++)
        if (hit(s_flt_btn[fi], mx, my)) {
            g_issue_filter = fi;
            s_issues_scroll = 0;
            s_hovered_issue = -1;
            redraw();
            return;
        }
    if (s_sb.valid && mx >= s_sb.x && mx < s_sb.x + s_sb.w) {
        if (my >= s_sb.thumb_y && my < s_sb.thumb_y + s_sb.thumb_h) {
            s_sb_drag = true;
            s_sb_drag_y0 = my;
            s_sb_drag_s0 = s_issues_scroll;
            SetCapture(hwnd);
        } else if (my >= s_sb.track_y && my < s_sb.track_y + s_sb.track_h) {
            s_issues_scroll = (my < s_sb.thumb_y) ? std::max(0, s_issues_scroll - s_sb.vis_n)
                                                  : std::min(s_sb.max_sc, s_issues_scroll + s_sb.vis_n);
            redraw();
        }
        return;
    }
    if (hit(s_graph, mx, my)) {
        s_graph_drag = true;
        SetCapture(hwnd);
        jump_from_mouse(mx);
        return;
    }
    for (const IssueRowHit& row : s_issue_rows) {
        if (my < row.y0 || my >= row.y1) continue;
        if (mx >= 10 && mx < 28) {
            if (!g_seen_issues.erase(row.id)) g_seen_issues.insert(row.id);
        } else {
            focus_frame(row.f_start);
            if (settings.analyzer.auto_check) g_seen_issues.insert(row.id);
        }
        redraw();
        return;
    }
}

void on_mouse_move(HWND hwnd, int32_t mx, int32_t my) {
    if (s_graph_drag) {
        jump_from_mouse(mx);
        return;
    }
    if (s_sb_drag) {
        const int32_t dy = my - s_sb_drag_y0;
        const int32_t pr = s_sb.track_h - s_sb.thumb_h;
        if (pr > 0 && s_sb.max_sc > 0)
            s_issues_scroll = std::clamp(s_sb_drag_s0 + dy * s_sb.max_sc / pr, 0, s_sb.max_sc);
        InvalidateRect(hwnd, nullptr, FALSE);
        return;
    }

    int32_t new_hover = -1;
    for (int32_t i = 0; i < std::ssize(s_issue_rows); i++)
        if (my >= s_issue_rows[i].y0 && my < s_issue_rows[i].y1) {
            new_hover = i;
            break;
        }
    if (new_hover != s_hovered_issue) {
        s_hovered_issue = new_hover;
        InvalidateRect(hwnd, nullptr, FALSE);
    }
    TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hwnd, 0 };
    TrackMouseEvent(&tme);
}

[[nodiscard]] LPCWSTR cursor_at(POINT pt) {
    if (hit(s_keep_seen_hit, pt.x, pt.y)) return IDC_HAND;
    if (hit(s_graph, pt.x, pt.y)) return IDC_CROSS;
    for (int32_t i = 0; i < MAXROW_COUNT; i++)
        if (s_maxrow_frames[i] >= 0 && hit(s_maxrow_hits[i], pt.x, pt.y)) return IDC_HAND;
    if (hit(s_next_unseen_btn, pt.x, pt.y)) return IDC_HAND;
    if (s_sb.valid) {
        const RECT thumb = { s_sb.x, s_sb.thumb_y, s_sb.x + s_sb.w, s_sb.thumb_y + s_sb.thumb_h };
        if (hit(thumb, pt.x, pt.y)) return IDC_SIZENS;
    }
    for (const IssueRowHit& row : s_issue_rows)
        if (pt.y >= row.y0 && pt.y < row.y1) return IDC_HAND;
    return nullptr;
}

void carry_over_seen_issues(const AnalyzeResult& res) {
    if (!g_keep_seen) {
        g_seen_issues.clear();
        return;
    }

    std::unordered_set<IssueId> next_seen;
    std::unordered_set<IssueId> used_old;
    for (const Issue& iss : res.issues) {
        const IssueId id = issue_seen_id(iss);
        if (g_seen_issues.contains(id)) {
            next_seen.insert(id);
            used_old.insert(id);
        }
    }

    if (settings.analyzer.fuzzy_match_issues) {
        const int32_t fuzzy_dist = settings.analyzer.fuzzy_match_dist < 0 ? 30 : settings.analyzer.fuzzy_match_dist;
        for (const Issue& iss : res.issues) {
            const IssueId id = issue_seen_id(iss);
            if (next_seen.contains(id)) continue;
            std::optional<IssueId> best;
            int32_t best_dist = INT32_MAX;
            for (const IssueId old_id : g_seen_issues) {
                if (used_old.contains(old_id)) continue;
                const UnpackedIssueId old = unpack_issue_id(old_id);
                if (old.type != iss.type) continue;
                const bool overlap = iss.f_start <= old.f_end && iss.f_end >= old.f_start;
                const int32_t dist = std::abs(iss.f_start - old.f_start);
                if (!(overlap || dist < fuzzy_dist)) continue;
                if (dist < best_dist || (dist == best_dist && old_id < *best)) {
                    best_dist = dist;
                    best = old_id;
                }
            }
            if (best) {
                next_seen.insert(id);
                used_old.insert(*best);
            }
        }
    }
    g_seen_issues = std::move(next_seen);
}

void on_analysis_done(HWND hwnd) {
    const Notice n = g_notice.load();
    if (n != Notice::Failed && n != Notice::Aborted) {
        s_issues_scroll = 0;
        g_view = GraphView{};
        carry_over_seen_issues(*current_result());
        refresh_volume_fix_proposal();
    }
    set_measure_buttons(false);
    InvalidateRect(hwnd, nullptr, TRUE);
}

void warn_box(HWND hwnd, const wchar_t* text) {
    MessageBoxW(hwnd, text, APP_NAME, MB_ICONWARNING);
}

void begin_measure(HWND hwnd, int32_t id) {
    if (g_edit_handle->get_edit_state() != g_edit_handle->EDIT_STATE_EDIT) {
        warn_box(hwnd, TrText(L"プレビュー中や書き出し中は計測できません。"));
        return;
    }
    read_controls_to_settings();
    EDIT_INFO info;
    g_edit_handle->get_edit_info(&info, sizeof(info));
    switch (id) {
        case ID_BTN_RANGE:
            if (info.select_range_start < 0 || info.select_range_end < 0) {
                warn_box(hwnd, TrText(L"タイムラインでフレーム範囲を選択してから計測してください"));
                return;
            }
            start_analysis({ .f_start = info.select_range_start, .f_end = info.select_range_end });
            break;
        case ID_BTN_SCENE:
            start_analysis({ .f_start = 0, .f_end = info.frame_max });
            break;
        case ID_BTN_OBJECT: {
            ObjPickResult pick;
            g_edit_handle->call_read_section_param(&pick, pick_target_object_cb);
            if (!pick.found) {
                warn_box(hwnd, TrText(L"計測するオブジェクトを選択してください"));
                return;
            }
            start_analysis(request_from_pick(pick, true));
            break;
        }
        default:
            break;
    }
}

void on_command(HWND hwnd, WPARAM wp) {
    const int32_t id = LOWORD(wp);
    const int32_t notif = HIWORD(wp);
    switch (id) {
        case ID_BTN_RANGE:
        case ID_BTN_SCENE:
        case ID_BTN_OBJECT:
            if (!g_busy.load()) begin_measure(hwnd, id);
            return;
        case ID_BTN_ABORT:
            g_abort.store(true);
            return;
        case ID_COMBO_PRESET:
            if (notif == CBN_SELCHANGE) {
                const auto sel = static_cast<int32_t>(SendMessage(s_combo, CB_GETCURSEL, 0, 0));
                if (sel >= 0 && sel < PRESET_CUSTOM) {
                    settings.analyzer.target_lufs = PRESETS[sel].lufs;
                    settings.analyzer.target_peak = PRESETS[sel].peak;
                    write_settings_to_controls();
                }
                SaveConfig();
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return;
        case ID_EDIT_LUFS:
        case ID_EDIT_PEAK:
        case ID_EDIT_SILENCE:
            if (notif == EN_KILLFOCUS) {
                read_controls_to_settings();
                if (find_preset_index() == PRESET_CUSTOM) SendMessage(s_combo, CB_SETCURSEL, PRESET_CUSTOM, 0);
                SaveConfig();
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return;
        case ID_VOLFIX_APPLY:
            apply_volume_fix_and_remeasure();
            InvalidateRect(hwnd, nullptr, FALSE);
            return;
        case ID_VOLFIX_RESET:
            g_vol_fix_panel.reset_to_suggested();
            InvalidateRect(hwnd, nullptr, FALSE);
            return;
        case ID_VOLFIX_EDIT:
            if (notif == EN_CHANGE) InvalidateRect(hwnd, nullptr, FALSE);
            return;
        default:
            return;
    }
}

HWND create_child(HWND parent, const wchar_t* cls, const wchar_t* text, DWORD style, int32_t x, int32_t y, int32_t w, int32_t h, int32_t id) {
    return CreateWindowW(cls, text, WS_CHILD | WS_VISIBLE | style, x, y, w, h, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), g_hinstance, nullptr);
}

void on_create(HWND hwnd) {
    load_analyzer_colors();
    s_br_ctrl = CreateSolidBrush(C_CONTROL);
    s_btn_rng = create_child(hwnd, L"BUTTON", TrText(L"選択範囲を計測"), BS_PUSHBUTTON, 8, 8, 148, 26, ID_BTN_RANGE);
    s_btn_all = create_child(hwnd, L"BUTTON", TrText(L"シーン全体を計測"), BS_PUSHBUTTON, 160, 8, 148, 26, ID_BTN_SCENE);
    s_btn_obj = create_child(hwnd, L"BUTTON", TrText(L"選択オブジェクトを計測"), BS_PUSHBUTTON, 312, 8, 184, 26, ID_BTN_OBJECT);
    s_btn_abort = create_child(hwnd, L"BUTTON", TrText(L"中止"), BS_PUSHBUTTON | WS_DISABLED, 500, 8, 76, 26, ID_BTN_ABORT);
    s_combo = create_child(hwnd, L"COMBOBOX", nullptr, CBS_DROPDOWNLIST | WS_VSCROLL, 8, 42, 300, 200, ID_COMBO_PRESET);
    for (const Preset& p : PRESETS) SendMessage(s_combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(p.name));
    s_edit_l = create_child(hwnd, L"EDIT", L"-14.0", ES_RIGHT | ES_AUTOHSCROLL, 8, 74, 52, 20, ID_EDIT_LUFS);
    s_edit_p = create_child(hwnd, L"EDIT", L"-1.0", ES_RIGHT | ES_AUTOHSCROLL, 148, 74, 44, 20, ID_EDIT_PEAK);
    s_edit_sil = create_child(hwnd, L"EDIT", L"-60.0", ES_RIGHT | ES_AUTOHSCROLL, 248, 74, 52, 20, ID_EDIT_SILENCE);
    ensure_fonts();
    for (const HWND h : { s_btn_rng, s_btn_all, s_btn_obj, s_btn_abort, s_combo, s_edit_l, s_edit_p, s_edit_sil })
        if (h) SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(g_fL), TRUE);
    write_settings_to_controls();
    g_vol_fix_panel.create(hwnd, g_hinstance, ID_VOLFIX_EDIT, g_fL);
}

void on_destroy(HWND hwnd) {
    g_vol_fix_panel.destroy();
    KillTimer(hwnd, ANALYSIS_TIMER_ID);
    g_analysis.reset();
    g_backbuf.destroy();
    HGDIOBJ objs[] = { s_br_ctrl, g_fLB, g_fL, g_fS };
    for (HGDIOBJ& o : objs) delete_gdi(o);
    s_br_ctrl = nullptr;
    g_fLB = g_fL = g_fS = nullptr;
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE:
            on_create(hwnd);
            return 0;
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORLISTBOX: {
            const auto dc = reinterpret_cast<HDC>(wp);
            SetTextColor(dc, C_TEXT);
            SetBkColor(dc, C_CONTROL);
            return reinterpret_cast<LRESULT>(s_br_ctrl);
        }
        case WM_COMMAND:
            on_command(hwnd, wp);
            return 0;
        case WM_FRAME_CHANGE:
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        case WM_TIMER:
            if (wp == ANALYSIS_TIMER_ID) {
                analysis_step(hwnd);
                return 0;
            }
            break;
        case WM_MOUSEWHEEL:
            on_mouse_wheel(hwnd, wp);
            return 0;
        case WM_LBUTTONDOWN:
            on_lbutton_down(hwnd, GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
            return 0;
        case WM_LBUTTONUP:
            if (s_graph_drag) {
                s_graph_drag = false;
                ReleaseCapture();
            }
            if (s_sb_drag) {
                s_sb_drag = false;
                ReleaseCapture();
            }
            return 0;
        case WM_RBUTTONDOWN:
            if (hit(s_graph, GET_X_LPARAM(lp), GET_Y_LPARAM(lp))) {
                g_view = GraphView{};
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        case WM_MOUSEMOVE:
            on_mouse_move(hwnd, GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
            return 0;
        case WM_MOUSELEAVE:
            if (s_hovered_issue != -1) {
                s_hovered_issue = -1;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        case WM_SETCURSOR: {
            POINT pt;
            GetCursorPos(&pt);
            ScreenToClient(hwnd, &pt);
            if (const LPCWSTR cur = cursor_at(pt)) {
                SetCursor(LoadCursor(nullptr, cur));
                return TRUE;
            }
            break;
        }
        case WM_PROGRESS:
            InvalidateRect(hwnd, nullptr, FALSE);
            UpdateWindow(hwnd);
            return 0;
        case WM_DONE:
            on_analysis_done(hwnd);
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT:
            paint_window(hwnd);
            return 0;
        case WM_CONTEXTMENU: {
            POINT pt;
            GetCursorPos(&pt);
            ScreenToClient(hwnd, &pt);
            if (hit(s_graph, pt.x, pt.y)) return 0;
            break;
        }
        case WM_SIZE:
            InvalidateRect(hwnd, nullptr, TRUE);
            return 0;
        case WM_DESTROY:
            on_destroy(hwnd);
            return 0;
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}

void menu_measure_cb(void* param) {
    if (g_hwnd) {
        const auto id = reinterpret_cast<intptr_t>(param);
        PostMessage(g_hwnd, WM_COMMAND, MAKEWPARAM(static_cast<WORD>(id), 0), 0);
    }
}
} // namespace

void Register_Analyzer(HOST_APP_TABLE* host) {
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = g_hinstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = APP_CLASS;
    RegisterClassExW(&wc);
    g_hwnd = CreateWindowExW(0, APP_CLASS, nullptr, WS_CHILD | WS_CLIPCHILDREN, 0, 0, 640, 600, g_host_hwnd, nullptr, g_hinstance, nullptr);
    host->register_window_client(APP_NAME, g_hwnd);
    host->register_event_listener(EVENT_TYPE::CHANGE_EDIT_FRAME, nullptr, frame_change_cb);
    host->register_filter_plugin(&filter_plugin_table_volume);

    static const std::wstring menu_range = std::wstring(APP_NAME) + L"\\" + TrText(L"選択範囲を計測");
    static const std::wstring menu_all = std::wstring(APP_NAME) + L"\\" + TrText(L"シーン全体を計測");
    static const std::wstring menu_obj = std::wstring(APP_NAME) + L"\\" + TrText(L"選択オブジェクトを計測");

    const auto id_param = [](int32_t id) { return reinterpret_cast<void*>(static_cast<intptr_t>(id)); };
    host->register_edit_menu_param(menu_range.c_str(), id_param(ID_BTN_RANGE), menu_measure_cb);
    host->register_edit_menu_param(menu_all.c_str(), id_param(ID_BTN_SCENE), menu_measure_cb);
    host->register_edit_menu_param(menu_obj.c_str(), id_param(ID_BTN_OBJECT), menu_measure_cb);
}

void Uninitialize_Analyzer() {
    DestroyWindow(g_hwnd);
}