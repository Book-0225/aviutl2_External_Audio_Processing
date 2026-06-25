#include "Eap2Common.h"
#include "Eap2Config.h"

static constexpr int32_t BATCH_SIZE = 64;
static constexpr UINT WM_PROGRESS = WM_USER + 1;
static constexpr UINT WM_DONE = WM_USER + 2;
static constexpr UINT WM_FRAME_CHANGE = WM_USER + 3;

static constexpr COLORREF C_BG = RGB(18, 18, 22);
static constexpr COLORREF C_PANEL = RGB(26, 26, 34);
static constexpr COLORREF C_CTRL = RGB(30, 30, 42);
static constexpr COLORREF C_BORDER = RGB(44, 44, 58);
static constexpr COLORREF C_TEXT = RGB(216, 216, 230);
static constexpr COLORREF C_LABEL = RGB(110, 110, 134);
static constexpr COLORREF C_GREEN = RGB(60, 200, 100);
static constexpr COLORREF C_YELLOW = RGB(224, 192, 44);
static constexpr COLORREF C_RED = RGB(220, 56, 48);
static constexpr COLORREF C_BLUE = RGB(60, 148, 220);
static constexpr COLORREF C_ORANGE = RGB(220, 128, 50);
static constexpr COLORREF C_GRAPHBG = RGB(10, 10, 18);
static constexpr COLORREF C_GRID = RGB(32, 32, 44);
static constexpr COLORREF C_GRIDLBL = RGB(56, 56, 70);
static constexpr COLORREF C_PROGFG = RGB(44, 100, 190);

struct Preset {
    const wchar_t* name;
    double lufs;
    double peak;
};

static const Preset PRESETS[] = {
    { L"YouTube / Spotify", -14.0, -1.0 },
    { L"Apple Music", -16.0, -1.0 },
    { L"Amazon Music", -14.0, -2.0 },
    { L"Netflix", -27.0, -2.0 },
    { L"放送 (EBU R128 / NHK)", -23.0, -1.0 },
    { L"CD マスタリング", -9.0, -0.1 },
    { L"カスタム", -14.0, -1.0 },
};

static constexpr int32_t PRESET_COUNT = 7;
static constexpr int32_t PRESET_CUSTOM = PRESET_COUNT - 1;

static int32_t find_preset_index() {
    for (int32_t i = 0; i < PRESET_CUSTOM; i++)
        if (std::abs(PRESETS[i].lufs - settings.analyzer.target_lufs) < 0.05 && std::abs(PRESETS[i].peak - settings.analyzer.target_peak) < 0.05)
            return i;
    return PRESET_CUSTOM;
}

struct BqCoeff {
    double b0;
    double b1;
    double b2;
    double a1;
    double a2;
};

struct BqState {
    double z1 = 0.0;
    double z2 = 0.0;
};

static double bq(const BqCoeff& c, BqState& s, double x) {
    double y = c.b0 * x + s.z1;
    s.z1 = c.b1 * x - c.a1 * y + s.z2;
    s.z2 = c.b2 * x - c.a2 * y;
    return y;
}

static BqCoeff make_highshelf(double fs, double f0, double gain_db, double Q) {
    double A = std::pow(10.0, gain_db / 40.0);
    double w0 = 2.0 * M_PI * f0 / fs;
    double sw = std::sin(w0);
    double cw = std::cos(w0);
    double sA = std::sqrt(A);
    double al = sw / (2.0 * Q);
    double a0 = (A + 1) - (A - 1) * cw + 2.0 * sA * al;
    return { A * ((A + 1) + (A - 1) * cw + 2.0 * sA * al) / a0, -2.0 * A * ((A - 1) + (A + 1) * cw) / a0,
             A * ((A + 1) + (A - 1) * cw - 2.0 * sA * al) / a0, 2.0 * ((A - 1) - (A + 1) * cw) / a0,
             ((A + 1) - (A - 1) * cw - 2.0 * sA * al) / a0 };
}

static BqCoeff make_highpass(double fs, double f0, double Q) {
    double w0 = 2.0 * M_PI * f0 / fs;
    double sw = std::sin(w0);
    double cw = std::cos(w0);
    double al = sw / (2.0 * Q);
    double a0 = 1.0 + al;
    return { (1.0 + cw) / 2.0 / a0, -(1.0 + cw) / a0, (1.0 + cw) / 2.0 / a0, -2.0 * cw / a0, (1.0 - al) / a0 };
}

struct KWeight {
    BqCoeff pre{};
    BqCoeff rlb{};
    BqState pl;
    BqState pr;
    BqState rl;
    BqState rr;

    void init(double fs) {
        pre = make_highshelf(fs, 1681.974450955533, 3.999843853973347, 0.7071752369554196);
        rlb = make_highpass(fs, 38.13547087602444, 0.5003270373238773);
    }

    double L(double x) {
        return bq(rlb, rl, bq(pre, pl, x));
    }

    double R(double x) {
        return bq(rlb, rr, bq(pre, pr, x));
    }
};

struct Issue {
    enum Type { CLIP,
                SILENCE } type;
    int32_t f_start;
    int32_t f_end;
};

enum class JudgeStatus { NONE,
                         PASS,
                         WARN,
                         FAIL };

struct AnalyzeResult {
    bool valid = false;
    double integrated = -100.0;
    double true_peak = -100.0;
    double lra = 0.0;
    double st_max = -100.0;
    double mom_max = -100.0;
    std::vector<double> st_hist;
    std::vector<double> mom_hist;
    std::vector<Issue> issues;
    int32_t f_start = 0;
    int32_t f_end = 0;
    AnalyzerConfig snap;
    int32_t fps_rate = 30;
    int32_t fps_scale = 1;
    int32_t sample_rate = 44100;
};

static HWND g_hwnd = nullptr;
static AnalyzeResult g_result;
static std::mutex g_result_mtx;
static std::atomic<bool> g_busy{ false };
static std::atomic<int32_t> g_prog{ 0 };
static std::atomic<int32_t> g_cur_frame{ -1 };

static double g_gt_zoom = 1.0;
static double g_gt_pan = 0.0;
static double g_gv_min = -54.0;
static double g_gv_max = 0.0;

static int32_t s_gx = 0;
static int32_t s_gy = 0;
static int32_t s_gw = 0;
static int32_t s_gh = 0;
static int32_t g_issue_filter = 0;
static int32_t s_issues_scroll = 0;

static RECT s_flt_btn[3] = {};

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
static SbGeom s_sb;
static bool s_sb_drag = false;
static int32_t s_sb_drag_y0 = 0;
static int32_t s_sb_drag_s0 = 0;

struct IssueHit {
    int32_t y0;
    int32_t y1;
    int32_t f_start;
};

static std::vector<IssueHit> s_issue_hits;
static int32_t s_hovered_issue = -1;

static HWND s_btn_rng = nullptr;
static HWND s_btn_all = nullptr;
static HWND s_combo = nullptr;
static HWND s_edit_l = nullptr;
static HWND s_edit_p = nullptr;
static HWND s_edit_sil = nullptr;
static HBRUSH s_br_ctrl = nullptr;

struct BatchBuf {
    std::map<int32_t, std::pair<std::vector<float>, std::vector<float>>> frames;
    std::mutex mtx;
};

static void audio_cb(void* param, int32_t frame, const float* L, const float* R, int32_t n) {
    BatchBuf* buf = static_cast<BatchBuf*>(param);
    std::lock_guard<std::mutex> lk(buf->mtx);
    std::pair<std::vector<float>, std::vector<float>>& f = buf->frames[frame];
    f.first.assign(L, L + n);
    f.second.assign(R, R + n);
}

static double ms_to_lufs(double ms) {
    return ms > 0.0 ? -0.691 + 10.0 * std::log10(ms) : -100.0;
}

static int32_t frame_to_hist_idx(const AnalyzeResult& res, int32_t frame) {
    if (res.fps_rate == 0 || res.sample_rate == 0) return -1;
    const int32_t spf = res.sample_rate * res.fps_scale / (std::max)(1, res.fps_rate);
    const int32_t step = res.sample_rate * 100 / 1000;
    if (step == 0) return -1;
    const int32_t rel = frame - res.f_start;
    if (rel < 0) return -1;
    return (rel * spf) / step;
}

static void jump_to_frame_cb(void* p, EDIT_SECTION* edit) {
    edit->set_cursor_layer_frame(edit->info->layer, *static_cast<int32_t*>(p));
}

static void jump_to_frame(int32_t frame) {
    g_edit_handle->call_edit_section_param(&frame, jump_to_frame_cb);
}

static void analyze_thread(int32_t f0, int32_t f1, int32_t rate, int32_t scale, int32_t sr, AnalyzerConfig snap, HWND hwnd) {
    const int32_t mom_len = sr * 400 / 1000;
    const int32_t st_len = sr * 3;
    const int32_t step = sr * 100 / 1000;
    const int32_t total_frames = f1 - f0 + 1;

    KWeight kw;
    kw.init(sr);
    std::vector<float> kw_sq;
    kw_sq.reserve(total_frames * (sr / (std::max)(1, rate / scale) + 2));

    struct FStat {
        int32_t frame;
        double peak;
        double rms;
    };
    std::vector<FStat> fstats;
    fstats.reserve(total_frames);

    double raw_peak = 0.0;
    BatchBuf batch;
    bool abort = false;

    for (int32_t bs = f0; bs <= f1; bs += BATCH_SIZE) {
        const int32_t be = (std::min)(bs + BATCH_SIZE - 1, f1);
        {
            std::lock_guard<std::mutex> lk(batch.mtx);
            batch.frames.clear();
        }
        for (int32_t f = bs; f <= be; f++) {
            if (!g_edit_handle->rendering_scene_audio(f, &batch, audio_cb)) {
                g_edit_handle->wait_rendering_task();
                abort = true;
                break;
            }
        }
        if (abort) break;
        g_edit_handle->wait_rendering_task();
        {
            std::lock_guard<std::mutex> lk(batch.mtx);
            for (int32_t f = bs; f <= be; f++) {
                auto it = batch.frames.find(f);
                if (it == batch.frames.end()) continue;
                auto& [Lbuf, Rbuf] = it->second;
                const int32_t n = static_cast<int32_t>(Lbuf.size());
                if (n == 0) continue;
                double fp = 0.0;
                double sq = 0.0;
                for (int32_t i = 0; i < n; i++) {
                    double kl = kw.L(Lbuf[i]);
                    double kr = kw.R(Rbuf[i]);
                    kw_sq.push_back(static_cast<float>((kl * kl + kr * kr) * 0.5));
                    double s = (std::max)(std::abs(static_cast<double>(Lbuf[i])), std::abs(static_cast<double>(Rbuf[i])));
                    if (s > fp) fp = s;
                    if (s > raw_peak) raw_peak = s;
                    sq += static_cast<double>(Lbuf[i]) * Lbuf[i] + static_cast<double>(Rbuf[i]) * Rbuf[i];
                }
                fstats.push_back({ f, fp, std::sqrt(sq / (2.0 * n)) });
            }
        }
        g_prog.store(static_cast<int32_t>(100.0 * (be - f0 + 1) / total_frames));
        PostMessage(hwnd, WM_PROGRESS, g_prog.load(), 0);
    }

    if (!abort) {
        AnalyzeResult res;
        res.f_start = f0;
        res.f_end = f1;
        res.snap = snap;
        res.fps_rate = rate;
        res.fps_scale = scale;
        res.sample_rate = sr;

        const int32_t N = static_cast<int32_t>(kw_sq.size());
        if (N > 0) {
            res.true_peak = raw_peak > 0.0 ? 20.0 * std::log10(raw_peak) : -100.0;

            std::vector<double> mom_ms;
            for (int32_t p = 0; p + mom_len <= N; p += step) {
                double ms = 0.0;
                for (int32_t i = p; i < p + mom_len; i++) ms += kw_sq[i];
                ms /= mom_len;
                mom_ms.push_back(ms);
                double l = ms_to_lufs(ms);
                res.mom_hist.push_back(l);
                if (l > res.mom_max) res.mom_max = l;
            }
            std::vector<double> st_ms;
            for (int32_t p = 0; p + st_len <= N; p += step) {
                double ms = 0.0;
                for (int32_t i = p; i < p + st_len; i++) ms += kw_sq[i];
                ms /= st_len;
                st_ms.push_back(ms);
                double l = ms_to_lufs(ms);
                res.st_hist.push_back(l);
                if (l > res.st_max) res.st_max = l;
            }

            {
                std::vector<double> p1;
                for (auto ms : mom_ms)
                    if (ms_to_lufs(ms) >= -70.0) p1.push_back(ms);
                if (!p1.empty()) {
                    double m1 = 0.0;
                    for (auto ms : p1) m1 += ms;
                    m1 /= p1.size();
                    double rel = ms_to_lufs(m1) - 10.0;
                    std::vector<double> p2;
                    for (auto ms : p1)
                        if (ms_to_lufs(ms) >= rel) p2.push_back(ms);
                    if (p2.empty()) p2 = p1;
                    double m2 = 0.0;
                    for (auto ms : p2) m2 += ms;
                    m2 /= p2.size();
                    res.integrated = ms_to_lufs(m2);
                }
            }
            if (st_ms.size() >= 2) {
                std::vector<double> lra_v;
                for (auto ms : st_ms) {
                    double l = ms_to_lufs(ms);
                    if (l >= -70.0) lra_v.push_back(l);
                }
                if (lra_v.size() >= 2) {
                    double avg = 0.0;
                    for (auto ms : st_ms) avg += ms;
                    avg /= st_ms.size();
                    double rel = ms_to_lufs(avg) - 20.0;
                    std::vector<double> gated;
                    for (auto l : lra_v)
                        if (l >= rel) gated.push_back(l);
                    if (gated.size() >= 2) {
                        std::sort(gated.begin(), gated.end());
                        const int32_t n = static_cast<int32_t>(gated.size());
                        res.lra = gated[(std::min)(n - 1, static_cast<int32_t>(n * 0.95))] - gated[(std::max)(0, static_cast<int32_t>(n * 0.10))];
                    }
                }
            }
            {
                const double sil_lin = std::pow(10.0, snap.sil_db / 20.0);
                const int32_t sil_min = static_cast<int32_t>(snap.sil_min_s * rate / scale);
                bool in_clip = false;
                bool in_sil = false;
                int32_t clip_s = 0;
                int32_t sil_s = 0;
                for (auto& st : fstats) {
                    if (st.peak >= 1.0) {
                        if (!in_clip) {
                            in_clip = true;
                            clip_s = st.frame;
                        }
                    } else {
                        if (in_clip) {
                            res.issues.push_back({ Issue::CLIP, clip_s, st.frame - 1 });
                            in_clip = false;
                        }
                    }
                    if (st.rms < sil_lin) {
                        if (!in_sil) {
                            in_sil = true;
                            sil_s = st.frame;
                        }
                    } else {
                        if (in_sil) {
                            if (st.frame - sil_s >= sil_min) res.issues.push_back({ Issue::SILENCE, sil_s, st.frame - 1 });
                            in_sil = false;
                        }
                    }
                }
                if (!fstats.empty()) {
                    if (in_clip) res.issues.push_back({ Issue::CLIP, clip_s, f1 });
                    if (in_sil && f1 - sil_s >= sil_min) res.issues.push_back({ Issue::SILENCE, sil_s, f1 });
                }
            }
            res.valid = true;
        }
        std::lock_guard<std::mutex> lk(g_result_mtx);
        g_result = std::move(res);
    }
    g_busy.store(false);
    PostMessage(hwnd, WM_DONE, 0, 0);
}

static void start_analysis(int32_t f_start, int32_t f_end) {
    if (g_busy.exchange(true)) return;
    g_prog.store(0);
    InvalidateRect(g_hwnd, nullptr, TRUE);
    EDIT_INFO info;
    g_edit_handle->get_edit_info(&info, sizeof(info));
    f_start = (std::max)(0, f_start);
    f_end = (std::min)(f_end, info.frame_max);
    if (f_start > f_end) {
        g_busy.store(false);
        return;
    }
    AnalyzerConfig snap = settings.analyzer;
    HWND hwnd = g_hwnd;
    std::thread([=]() { analyze_thread(f_start, f_end, info.rate, info.scale, info.sample_rate, snap, hwnd); }).detach();
}

static void read_controls_to_settings() {
    wchar_t buf[32];
    double v;
    GetWindowText(s_edit_l, buf, 32);
    v = wcstod(buf, nullptr);
    if (v < 0.0 && v >= -60.0) settings.analyzer.target_lufs = v;
    GetWindowText(s_edit_p, buf, 32);
    v = wcstod(buf, nullptr);
    if (v < 0.0 && v >= -20.0) settings.analyzer.target_peak = v;
    GetWindowText(s_edit_sil, buf, 32);
    v = wcstod(buf, nullptr);
    if (v < 0.0 && v >= -90.0) settings.analyzer.sil_db = v;
}

static void write_settings_to_controls() {
    wchar_t buf[32];
    swprintf_s(buf, L"%.1f", settings.analyzer.target_lufs);
    SetWindowText(s_edit_l, buf);
    swprintf_s(buf, L"%.1f", settings.analyzer.target_peak);
    SetWindowText(s_edit_p, buf);
    swprintf_s(buf, L"%.1f", settings.analyzer.sil_db);
    SetWindowText(s_edit_sil, buf);
    SendMessage(s_combo, CB_SETCURSEL, find_preset_index(), 0);
}

static void frame_change_cb(void*) {
    EDIT_INFO info;
    g_edit_handle->get_edit_info(&info, sizeof(info));
    g_cur_frame.store(info.frame);
    PostMessage(g_hwnd, WM_FRAME_CHANGE, 0, 0);
}

static HFONT g_fL = nullptr;
static HFONT g_fS = nullptr;
static HFONT g_fM = nullptr;

static void dtw(HDC hdc, const wchar_t* s, RECT r, COLORREF c, UINT fmt = DT_LEFT | DT_VCENTER | DT_SINGLELINE) {
    SetTextColor(hdc, c);
    DrawText(hdc, s, -1, &r, fmt);
}

static std::wstring fmtL(double v) {
    if (v <= -99.0) return L"---";
    wchar_t b[32];
    swprintf_s(b, L"%.1f", v);
    return b;
}

static std::wstring fmtDiff(double d) {
    wchar_t b[32];
    swprintf_s(b, d >= 0 ? L"+%.1f LU" : L"%.1f LU", d);
    return b;
}

static JudgeStatus judge_lufs(double v, const AnalyzerConfig& s) {
    if (v <= -99.0) return JudgeStatus::NONE;
    double d = v - s.target_lufs;
    if (std::abs(d) <= s.lufs_tol) return JudgeStatus::PASS;
    if (d > s.lufs_tol + 2.0) return JudgeStatus::FAIL;
    if (d > s.lufs_tol || d < -8.0) return JudgeStatus::WARN;
    return JudgeStatus::PASS;
}

static JudgeStatus judge_peak(double v, const AnalyzerConfig& s) {
    if (v <= -99.0) return JudgeStatus::NONE;
    if (v <= s.target_peak) return JudgeStatus::PASS;
    if (v <= s.target_peak + 1.0) return JudgeStatus::WARN;
    return JudgeStatus::FAIL;
}

static COLORREF judge_color(JudgeStatus j) {
    switch (j) {
        case JudgeStatus::PASS:
            return C_GREEN;
        case JudgeStatus::WARN:
            return C_YELLOW;
        case JudgeStatus::FAIL:
            return C_RED;
        default:
            return C_LABEL;
    }
}
static void draw_badge(HDC hdc, HFONT fnt, int32_t x, int32_t y, int32_t w, JudgeStatus j, const wchar_t* txt) {
    if (j == JudgeStatus::NONE) return;
    COLORREF col = judge_color(j);
    HBRUSH br = CreateSolidBrush(RGB(GetRValue(col) / 5, GetGValue(col) / 5, GetBValue(col) / 5));
    RECT br_r = { x, y + 1, x + w, y + 15 };
    FillRect(hdc, &br_r, br);
    DeleteObject(br);
    SelectObject(hdc, fnt);
    RECT tr = { x, y, x + w, y + 16 };
    dtw(hdc, txt, tr, col, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

static void draw_graph(HDC hdc, int32_t gx, int32_t gy, int32_t gw, int32_t gh, const std::vector<double>& st, const std::vector<double>& mom, double target_lufs, double cursor_frac, double t_pan, double t_zoom, double v_min, double v_max) {
    auto yx = [&](double l) -> int32_t {
        l = (std::max)(v_min, (std::min)(v_max, l));
        return gy + gh - static_cast<int32_t>((l - v_min) / (v_max - v_min) * gh);
    };
    auto xx = [&](int32_t i, int32_t n) -> int32_t {
        double t = (n > 1) ? static_cast<double>(i) / (n - 1) : 0.0;
        return gx + static_cast<int32_t>((t - t_pan) * t_zoom * gw);
    };

    {
        HBRUSH b = CreateSolidBrush(C_GRAPHBG);
        RECT r = { gx, gy, gx + gw, gy + gh };
        FillRect(hdc, &r, b);
        DeleteObject(b);
    }

    double v_range = v_max - v_min;
    int32_t gstep = (v_range > 30) ? 10 : (v_range > 12) ? 5
                                                         : 2;
    {
        HPEN pg = CreatePen(PS_SOLID, 1, C_GRID);
        HPEN op = static_cast<HPEN>(SelectObject(hdc, pg));
        int32_t db = static_cast<int32_t>(std::floor(v_min / gstep) * gstep);
        for (; static_cast<double>(db) <= v_max; db += gstep) {
            if (static_cast<double>(db) < v_min) continue;
            int32_t y = yx(static_cast<double>(db));
            MoveToEx(hdc, gx, y, nullptr);
            LineTo(hdc, gx + gw, y);
        }
        SelectObject(hdc, op);
        DeleteObject(pg);
    }

    if (target_lufs > v_min && target_lufs < v_max) {
        int32_t ty = yx(target_lufs);
        HPEN pt = CreatePen(PS_DOT, 1, RGB(60, 120, 60));
        HPEN ot = static_cast<HPEN>(SelectObject(hdc, pt));
        MoveToEx(hdc, gx, ty, nullptr);
        LineTo(hdc, gx + gw, ty);
        SelectObject(hdc, ot);
        DeleteObject(pt);
    }

    SelectObject(hdc, g_fS);
    {
        int32_t db = static_cast<int32_t>(std::floor(v_min / gstep) * gstep);
        for (; static_cast<double>(db) <= v_max; db += gstep) {
            if (static_cast<double>(db) < v_min) continue;
            int32_t y = yx(static_cast<double>(db));
            wchar_t lb[8];
            swprintf_s(lb, L"%d", db);
            RECT lr = { gx + gw + 2, y - 7, gx + gw + 30, y + 7 };
            SetTextColor(hdc, C_GRIDLBL);
            DrawText(hdc, lb, -1, &lr, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        }
    }
    if (target_lufs > v_min && target_lufs < v_max) {
        int32_t ty = yx(target_lufs);
        wchar_t tl[16];
        swprintf_s(tl, L"%.0f", target_lufs);
        RECT tlr = { gx + gw + 2, ty - 7, gx + gw + 30, ty + 7 };
        SetTextColor(hdc, RGB(80, 160, 80));
        DrawText(hdc, tl, -1, &tlr, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }

    double t_end = t_pan + 1.0 / t_zoom;
    auto draw_series = [&](const std::vector<double>& h, COLORREF col, int32_t lw = 1) {
        const int32_t n = static_cast<int32_t>(h.size());
        if (n < 2) return;
        int32_t i0 = (std::max)(0, static_cast<int32_t>(t_pan * (n - 1)) - 1);
        int32_t i1 = (std::min)(n - 1, static_cast<int32_t>(t_end * (n - 1)) + 1);
        if (i0 >= i1) return;
        HPEN p = CreatePen(PS_SOLID, lw, col);
        HPEN op = static_cast<HPEN>(SelectObject(hdc, p));
        MoveToEx(hdc, xx(i0, n), yx(h[i0]), nullptr);
        for (int32_t i = i0 + 1; i <= i1; i++) LineTo(hdc, xx(i, n), yx(h[i]));
        SelectObject(hdc, op);
        DeleteObject(p);
    };
    draw_series(st, C_BLUE);
    draw_series(mom, C_ORANGE);

    if (cursor_frac >= 0.0 && cursor_frac <= 1.0) {
        double vis = (cursor_frac - t_pan) * t_zoom;
        if (vis >= 0.0 && vis <= 1.0) {
            int32_t cx = gx + static_cast<int32_t>(vis * gw);
            HPEN pc = CreatePen(PS_SOLID, 1, RGB(210, 210, 210));
            HPEN oc = static_cast<HPEN>(SelectObject(hdc, pc));
            MoveToEx(hdc, cx, gy, nullptr);
            LineTo(hdc, cx, gy + gh);
            SelectObject(hdc, oc);
            DeleteObject(pc);
        }
    }

    SelectObject(hdc, g_fS);
    int32_t lx = gx + 4;
    for (auto& [label, col] : std::initializer_list<std::pair<const wchar_t*, COLORREF>>{ { L"Short-term", C_BLUE }, { L"Momentary", C_ORANGE } }) {
        HPEN p = CreatePen(PS_SOLID, 2, col);
        HPEN op = static_cast<HPEN>(SelectObject(hdc, p));
        MoveToEx(hdc, lx, gy + 8, nullptr);
        LineTo(hdc, lx + 14, gy + 8);
        SelectObject(hdc, op);
        DeleteObject(p);
        RECT r = { lx + 16, gy, lx + 110, gy + 16 };
        dtw(hdc, label, r, col, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        lx += 112;
    }

    if (t_zoom > 1.01 || v_min != -54.0 || v_max != 0.0) {
        wchar_t zi[48];
        if (t_zoom > 1.01) swprintf_s(zi, L"T×%.1f  右クリックでリセット", t_zoom);
        else swprintf_s(zi, L"右クリックでリセット");
        RECT zr = { lx, gy, gx + gw, gy + 14 };
        SetTextColor(hdc, C_GRIDLBL);
        DrawText(hdc, zi, -1, &zr, DT_RIGHT | DT_TOP | DT_SINGLELINE);
    }
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE: {
            s_br_ctrl = CreateSolidBrush(C_CTRL);
            s_btn_rng = CreateWindow(L"BUTTON", L"選択範囲を計測", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 8, 8, 148, 26, hwnd, reinterpret_cast<HMENU>(1), g_hinstance, nullptr);
            s_btn_all = CreateWindow(L"BUTTON", L"シーン全体を計測", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 160, 8, 148, 26, hwnd, reinterpret_cast<HMENU>(2), g_hinstance, nullptr);
            s_combo = CreateWindow(L"COMBOBOX", nullptr, WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL, 8, 42, 300, 200, hwnd, reinterpret_cast<HMENU>(10), g_hinstance, nullptr);
            for (auto& p : PRESETS) SendMessage(s_combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(p.name));
            s_edit_l = CreateWindow(L"EDIT", L"-14.0", WS_CHILD | WS_VISIBLE | ES_RIGHT | ES_AUTOHSCROLL, 8, 74, 52, 20, hwnd, reinterpret_cast<HMENU>(11), g_hinstance, nullptr);
            s_edit_p = CreateWindow(L"EDIT", L"-1.0", WS_CHILD | WS_VISIBLE | ES_RIGHT | ES_AUTOHSCROLL, 148, 74, 44, 20, hwnd, reinterpret_cast<HMENU>(12), g_hinstance, nullptr);
            s_edit_sil = CreateWindow(L"EDIT", L"-60.0", WS_CHILD | WS_VISIBLE | ES_RIGHT | ES_AUTOHSCROLL, 248, 74, 52, 20, hwnd, reinterpret_cast<HMENU>(13), g_hinstance, nullptr);
            write_settings_to_controls();
            return 0;
        }
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORLISTBOX: {
            HDC dc = reinterpret_cast<HDC>(wp);
            SetTextColor(dc, C_TEXT);
            SetBkColor(dc, C_CTRL);
            return reinterpret_cast<LRESULT>(s_br_ctrl);
        }
        case WM_COMMAND: {
            const int32_t id = LOWORD(wp), notif = HIWORD(wp);
            if ((id == 1 || id == 2) && !g_busy.load()) {
                if (g_edit_handle->get_edit_state() != g_edit_handle->EDIT_STATE_EDIT) {
                    MessageBox(hwnd, L"プレビュー中や書き出し中は計測できません。", L"EAP2 Analyzer", MB_ICONWARNING);
                    break;
                }
                read_controls_to_settings();
                EDIT_INFO info;
                g_edit_handle->get_edit_info(&info, sizeof(info));
                if (id == 1) {
                    if (info.select_range_start < 0 || info.select_range_end < 0) {
                        MessageBox(hwnd, L"タイムラインでフレーム範囲を選択してから計測してください", L"EAP2 Analyzer", MB_ICONWARNING);
                        break;
                    }
                    start_analysis(info.select_range_start, info.select_range_end);
                } else {
                    start_analysis(0, info.frame_max);
                }
                break;
            }
            if (id == 10 && notif == CBN_SELCHANGE) {
                int32_t sel = static_cast<int32_t>(SendMessage(s_combo, CB_GETCURSEL, 0, 0));
                if (sel >= 0 && sel < PRESET_CUSTOM) {
                    settings.analyzer.target_lufs = PRESETS[sel].lufs;
                    settings.analyzer.target_peak = PRESETS[sel].peak;
                    write_settings_to_controls();
                }
                SaveConfig();
                InvalidateRect(hwnd, nullptr, FALSE);
                break;
            }
            if ((id == 11 || id == 12 || id == 13) && notif == EN_KILLFOCUS) {
                read_controls_to_settings();
                if (find_preset_index() == PRESET_CUSTOM) SendMessage(s_combo, CB_SETCURSEL, PRESET_CUSTOM, 0);
                SaveConfig();
                InvalidateRect(hwnd, nullptr, FALSE);
                break;
            }
            break;
        }

        case WM_FRAME_CHANGE:
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;

        case WM_MOUSEWHEEL: {
            POINT pt;
            GetCursorPos(&pt);
            ScreenToClient(hwnd, &pt);
            const int32_t delta = static_cast<int32_t>(GET_WHEEL_DELTA_WPARAM(wp)) / WHEEL_DELTA;
            const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            const bool in_graph = s_gw > 0 && pt.x >= s_gx && pt.x < s_gx + s_gw && pt.y >= s_gy && pt.y < s_gy + s_gh;

            if (in_graph) {
                if (ctrl) {
                    double vis_f = static_cast<double>(pt.x - s_gx) / s_gw;
                    double d_at = g_gt_pan + vis_f / g_gt_zoom;
                    g_gt_zoom = (std::max)(1.0, (std::min)(64.0, g_gt_zoom * (delta > 0 ? 1.5 : 1.0 / 1.5)));
                    g_gt_pan = d_at - vis_f / g_gt_zoom;
                    g_gt_pan = (std::max)(0.0, (std::min)(1.0 - 1.0 / g_gt_zoom, g_gt_pan));
                } else if (shift) {
                    double v_f = static_cast<double>(pt.y - s_gy) / s_gh;
                    double v_at = g_gv_max + v_f * (g_gv_min - g_gv_max);
                    double fac = (delta > 0) ? (1.0 / 1.5) : 1.5;
                    double nm = v_at + (g_gv_min - v_at) * fac;
                    double nM = v_at + (g_gv_max - v_at) * fac;
                    double rng = nM - nm;
                    if (rng >= 4.0 && rng <= 80.0) {
                        g_gv_min = (std::max)(-90.0, nm);
                        g_gv_max = (std::min)(12.0, nM);
                    }
                } else {
                    if (g_gt_zoom > 1.01) {
                        double step = 0.2 / g_gt_zoom;
                        g_gt_pan -= delta * step;
                        g_gt_pan = (std::max)(0.0, (std::min)(1.0 - 1.0 / g_gt_zoom, g_gt_pan));
                    }
                }
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            s_issues_scroll = (std::max)(0, s_issues_scroll - delta);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }

        case WM_LBUTTONDOWN: {
            const int32_t mx = static_cast<int32_t>(LOWORD(lp));
            const int32_t my = static_cast<int32_t>(HIWORD(lp));

            for (int32_t fi = 0; fi < 3; fi++) {
                RECT& r = s_flt_btn[fi];
                if (r.right > r.left && mx >= r.left && mx < r.right && my >= r.top && my < r.bottom) {
                    g_issue_filter = fi;
                    s_issues_scroll = 0;
                    s_hovered_issue = -1;
                    InvalidateRect(hwnd, nullptr, FALSE);
                    return 0;
                }
            }
            if (s_sb.valid && mx >= s_sb.x && mx < s_sb.x + s_sb.w) {
                if (my >= s_sb.thumb_y && my < s_sb.thumb_y + s_sb.thumb_h) {
                    s_sb_drag = true;
                    s_sb_drag_y0 = my;
                    s_sb_drag_s0 = s_issues_scroll;
                    SetCapture(hwnd);
                } else if (my >= s_sb.track_y && my < s_sb.track_y + s_sb.track_h) {
                    s_issues_scroll = (my < s_sb.thumb_y)
                                          ? (std::max)(0, s_issues_scroll - s_sb.vis_n)
                                          : (std::min)(s_sb.max_sc, s_issues_scroll + s_sb.vis_n);
                    InvalidateRect(hwnd, nullptr, FALSE);
                }
                return 0;
            }
            if (s_gw > 0 && mx >= s_gx && mx < s_gx + s_gw && my >= s_gy && my < s_gy + s_gh) {
                double vis_f = static_cast<double>(mx - s_gx) / s_gw;
                double data_f = g_gt_pan + vis_f / g_gt_zoom;
                data_f = (std::max)(0.0, (std::min)(1.0, data_f));
                AnalyzeResult res;
                {
                    std::lock_guard<std::mutex> lk(g_result_mtx);
                    res = g_result;
                }
                if (res.valid && res.f_end > res.f_start) {
                    int32_t tf = res.f_start + static_cast<int32_t>(data_f * (res.f_end - res.f_start));
                    tf = (std::max)(res.f_start, (std::min)(res.f_end, tf));
                    jump_to_frame(tf);
                }
                return 0;
            }
            for (auto& hit : s_issue_hits) {
                if (my >= hit.y0 && my < hit.y1) {
                    jump_to_frame(hit.f_start);
                    return 0;
                }
            }
            return 0;
        }
        case WM_LBUTTONUP:
            if (s_sb_drag) {
                s_sb_drag = false;
                ReleaseCapture();
            }
            return 0;

        case WM_RBUTTONDOWN: {
            const int32_t mx = static_cast<int32_t>(LOWORD(lp));
            const int32_t my = static_cast<int32_t>(HIWORD(lp));
            if (s_gw > 0 && mx >= s_gx && mx < s_gx + s_gw && my >= s_gy && my < s_gy + s_gh) {
                g_gt_zoom = 1.0;
                g_gt_pan = 0.0;
                g_gv_min = -54.0;
                g_gv_max = 0.0;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        }

        case WM_MOUSEMOVE: {
            const int32_t my = static_cast<int32_t>(HIWORD(lp));

            if (s_sb_drag) {
                int32_t dy = my - s_sb_drag_y0;
                int32_t pr = s_sb.track_h - s_sb.thumb_h;
                if (pr > 0 && s_sb.max_sc > 0)
                    s_issues_scroll = (std::max)(0, (std::min)(s_sb.max_sc, s_sb_drag_s0 + dy * s_sb.max_sc / pr));
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            int32_t new_hover = -1;
            for (int32_t i = 0; i < static_cast<int32_t>(s_issue_hits.size()); i++) {
                if (my >= s_issue_hits[i].y0 && my < s_issue_hits[i].y1) {
                    new_hover = i;
                    break;
                }
            }
            if (new_hover != s_hovered_issue) {
                s_hovered_issue = new_hover;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hwnd, 0 };
            TrackMouseEvent(&tme);
            return 0;
        }
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
            if (s_gw > 0 && pt.x >= s_gx && pt.x < s_gx + s_gw && pt.y >= s_gy && pt.y < s_gy + s_gh) {
                SetCursor(LoadCursor(nullptr, IDC_CROSS));
                return TRUE;
            }
            for (auto& hit : s_issue_hits)
                if (pt.y >= hit.y0 && pt.y < hit.y1) {
                    SetCursor(LoadCursor(nullptr, IDC_HAND));
                    return TRUE;
                }
            if (s_sb.valid && pt.x >= s_sb.x && pt.x < s_sb.x + s_sb.w &&
                pt.y >= s_sb.thumb_y && pt.y < s_sb.thumb_y + s_sb.thumb_h) {
                SetCursor(LoadCursor(nullptr, IDC_SIZENS));
                return TRUE;
            }
            break;
        }

        case WM_PROGRESS:
            InvalidateRect(hwnd, nullptr, FALSE);
            break;

        case WM_DONE:
            s_issues_scroll = 0;
            g_gt_zoom = 1.0;
            g_gt_pan = 0.0;
            g_gv_min = -54.0;
            g_gv_max = 0.0;
            EnableWindow(s_btn_rng, TRUE);
            EnableWindow(s_btn_all, TRUE);
            InvalidateRect(hwnd, nullptr, TRUE);
            break;

        case WM_PAINT: {
            s_issue_hits.clear();
            s_gx = s_gy = s_gw = s_gh = 0;
            memset(s_flt_btn, 0, sizeof(s_flt_btn));
            s_sb.valid = false;

            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            RECT client;
            GetClientRect(hwnd, &client);
            const int32_t W = client.right;
            const int32_t H = client.bottom;

            HDC mdc = CreateCompatibleDC(hdc);
            HBITMAP mbmp = static_cast<HBITMAP>(SelectObject(mdc, CreateCompatibleBitmap(hdc, W, H)));
            SetBkMode(mdc, TRANSPARENT);
            {
                HBRUSH b = CreateSolidBrush(C_BG);
                FillRect(mdc, &client, b);
                DeleteObject(b);
            }

            FONT_INFO* font = g_config_handle->get_font_info(g_config_handle, "DefaultFamily");
            if (!g_fL) g_fL = CreateFont(24, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, font->name);
            if (!g_fS) g_fS = CreateFont(16, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, font->name);
            if (!g_fM) g_fM = CreateFont(20, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, font->name);
            SelectObject(mdc, g_fS);

            dtw(mdc, L"目標 LUFS", { 64, 74, 120, 94 }, C_LABEL);
            dtw(mdc, L"TP limit", { 196, 74, 246, 94 }, C_LABEL);
            dtw(mdc, L"無音 dBFS", { 304, 74, 360, 94 }, C_LABEL);

            {
                HPEN p = CreatePen(PS_SOLID, 1, C_BORDER);
                HPEN op = static_cast<HPEN>(SelectObject(mdc, p));
                MoveToEx(mdc, 8, 100, nullptr);
                LineTo(mdc, W - 8, 100);
                SelectObject(mdc, op);
                DeleteObject(p);
            }

            int32_t y = 104;
            bool paint_end = false;

            if (g_busy.load()) {
                const int32_t prog = g_prog.load();
                RECT pb = { 8, y, W - 8, y + 22 };
                {
                    HBRUSH b = CreateSolidBrush(C_PANEL);
                    FillRect(mdc, &pb, b);
                    DeleteObject(b);
                }
                {
                    RECT pf = { 8, y, 8 + static_cast<int32_t>((W - 16) * prog / 100.0), y + 22 };
                    HBRUSH b = CreateSolidBrush(C_PROGFG);
                    FillRect(mdc, &pf, b);
                    DeleteObject(b);
                }
                SelectObject(mdc, g_fS);
                wchar_t pg[32];
                swprintf_s(pg, L"計測中... %d%%", prog);
                dtw(mdc, pg, pb, C_TEXT, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                paint_end = true;
            }

            AnalyzeResult res;
            if (!paint_end) {
                std::lock_guard<std::mutex> lk(g_result_mtx);
                res = g_result;
            }
            if (!paint_end && !res.valid) {
                RECT r = { 0, y + 16, W, y + 36 };
                dtw(mdc, L"「計測」ボタンを押してください", r, C_LABEL, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                paint_end = true;
            }

            if (!paint_end) {
                const AnalyzerConfig& sn = res.snap;
                {
                    HBRUSH b = CreateSolidBrush(C_PANEL);
                    RECT r = { 8, y, W - 8, y + 128 };
                    FillRect(mdc, &r, b);
                    DeleteObject(b);
                }

                struct Row {
                    const wchar_t* label;
                    std::wstring value;
                    std::wstring unit;
                    JudgeStatus judge;
                    std::wstring badge;
                    double diff;
                };
                JudgeStatus j_int = judge_lufs(res.integrated, sn);
                JudgeStatus j_peak = judge_peak(res.true_peak, sn);
                const wchar_t* bstr[] = { L"", L"PASS", L"WARN", L"FAIL" };
                double ldiff = res.integrated - sn.target_lufs;
                Row rows[] = {
                    { L"Integrated LUFS", fmtL(res.integrated), L"LUFS", j_int, bstr[static_cast<int32_t>(j_int)], ldiff },
                    { L"True Peak", fmtL(res.true_peak), L"dBTP", j_peak, bstr[static_cast<int32_t>(j_peak)], 0.0 },
                    { L"LRA", [&] {wchar_t b[16];swprintf_s(b,L"%.1f",res.lra);return std::wstring(b); }(), L"LU", JudgeStatus::NONE, L"", 0.0 },
                    { L"Short-term Max", fmtL(res.st_max), L"LUFS", JudgeStatus::NONE, L"", 0.0 },
                    { L"Momentary Max", fmtL(res.mom_max), L"LUFS", JudgeStatus::NONE, L"", 0.0 },
                };
                const int32_t rh = 25;
                for (int32_t i = 0; i < 5; i++) {
                    int32_t ry = y + 1 + i * rh;
                    Row& row = rows[i];
                    SelectObject(mdc, g_fS);
                    dtw(mdc, row.label, { 14, ry + 6, 140, ry + rh }, C_LABEL);
                    SelectObject(mdc, g_fL);
                    COLORREF vc = (row.judge != JudgeStatus::NONE) ? judge_color(row.judge) : C_TEXT;
                    dtw(mdc, row.value.c_str(), { 138, ry + 2, 222, ry + rh }, vc, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
                    SelectObject(mdc, g_fS);
                    dtw(mdc, row.unit.c_str(), { 224, ry + 6, 252, ry + rh }, C_LABEL);
                    if (row.judge != JudgeStatus::NONE)
                        draw_badge(mdc, g_fS, 256, ry + 5, 42, row.judge, row.badge.c_str());
                    if (i == 0 && res.integrated > -99.0) {
                        COLORREF dc2 = std::abs(ldiff) < sn.lufs_tol ? C_GREEN : ldiff > 0 ? C_RED
                                                                                           : C_LABEL;
                        dtw(mdc, fmtDiff(ldiff).c_str(), { 300, ry + 6, W - 10, ry + rh }, dc2, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
                    }
                    if (i < 4) {
                        HPEN p = CreatePen(PS_SOLID, 1, RGB(32, 32, 44));
                        HPEN op = static_cast<HPEN>(SelectObject(mdc, p));
                        MoveToEx(mdc, 14, ry + rh, nullptr);
                        LineTo(mdc, W - 14, ry + rh);
                        SelectObject(mdc, op);
                        DeleteObject(p);
                    }
                }
                y += 132;
            }

            if (!paint_end && (!res.st_hist.empty() || !res.mom_hist.empty())) {
                const int32_t MR = 32;
                s_gx = 8;
                s_gy = y + 4;
                s_gw = W - 8 - MR;
                s_gh = 108;

                double cursor_frac = -1.0;
                {
                    int32_t cur = g_cur_frame.load();
                    if (cur >= res.f_start && cur <= res.f_end && res.f_end > res.f_start)
                        cursor_frac = static_cast<double>(cur - res.f_start) / (res.f_end - res.f_start);
                }
                draw_graph(mdc, s_gx, s_gy, s_gw, s_gh, res.st_hist, res.mom_hist, res.snap.target_lufs, cursor_frac, g_gt_pan, g_gt_zoom, g_gv_min, g_gv_max);
                y += 120;

                {
                    int32_t cur = g_cur_frame.load();
                    if (cur >= res.f_start && cur <= res.f_end) {
                        int32_t idx = frame_to_hist_idx(res, cur);
                        int32_t mn = static_cast<int32_t>(res.mom_hist.size());
                        int32_t sn2 = static_cast<int32_t>(res.st_hist.size());
                        double mv = (idx >= 0 && idx < mn) ? res.mom_hist[idx] : -100.0;
                        double sv = (idx >= 0 && idx < sn2) ? res.st_hist[idx] : -100.0;
                        {
                            HBRUSH b = CreateSolidBrush(C_PANEL);
                            RECT r = { 8, y, W - 8, y + 20 };
                            FillRect(mdc, &r, b);
                            DeleteObject(b);
                        }
                        SelectObject(mdc, g_fS);
                        wchar_t fb[24];
                        swprintf_s(fb, L"F%d", cur + 1);
                        dtw(mdc, fb, { 14, y + 2, 58, y + 20 }, C_LABEL, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
                        dtw(mdc, L"Mom", { 60, y + 2, 90, y + 20 }, C_LABEL, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
                        dtw(mdc, fmtL(mv).c_str(), { 90, y + 2, 140, y + 20 }, C_ORANGE, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
                        dtw(mdc, L"LUFS", { 142, y + 2, 172, y + 20 }, C_LABEL, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
                        dtw(mdc, L"ST", { 182, y + 2, 202, y + 20 }, C_LABEL, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
                        dtw(mdc, fmtL(sv).c_str(), { 202, y + 2, 256, y + 20 }, C_BLUE, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
                        dtw(mdc, L"LUFS", { 258, y + 2, 288, y + 20 }, C_LABEL, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
                        y += 22;
                    }
                }
            }

            if (!paint_end && !res.issues.empty()) {
                std::vector<int32_t> flt;
                for (int32_t i = 0; i < static_cast<int32_t>(res.issues.size()); i++) {
                    Issue& iss = res.issues[i];
                    if (g_issue_filter == 0 ||
                        (g_issue_filter == 1 && iss.type == Issue::CLIP) ||
                        (g_issue_filter == 2 && iss.type == Issue::SILENCE))
                        flt.push_back(i);
                }
                const int32_t total_iss = static_cast<int32_t>(res.issues.size());
                const int32_t flt_cnt = static_cast<int32_t>(flt.size());

                y += 4;
                SelectObject(mdc, g_fS);
                dtw(mdc, L"問題箇所", { 8, y, 108, y + 16 }, C_YELLOW);
                {
                    const wchar_t* flt_lbl[] = { L"全", L"C", L"S" };
                    COLORREF flt_col[] = { C_TEXT, C_RED, C_LABEL };
                    int32_t bx = 112;
                    for (int32_t fi = 0; fi < 3; fi++) {
                        s_flt_btn[fi] = { bx, y, bx + 22, y + 16 };
                        bool sel = (g_issue_filter == fi);
                        COLORREF bc = flt_col[fi];
                        {
                            HBRUSH b = CreateSolidBrush(sel ? RGB(GetRValue(bc) / 6, GetGValue(bc) / 6, GetBValue(bc) / 6) : RGB(30, 30, 44));
                            FillRect(mdc, &s_flt_btn[fi], b);
                            DeleteObject(b);
                        }
                        {
                            HBRUSH b = CreateSolidBrush(sel ? bc : C_BORDER);
                            FrameRect(mdc, &s_flt_btn[fi], b);
                            DeleteObject(b);
                        }
                        dtw(mdc, flt_lbl[fi], s_flt_btn[fi], sel ? bc : C_LABEL, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                        bx += 26;
                    }
                }
                {
                    wchar_t cnt[32];
                    if (g_issue_filter != 0) swprintf_s(cnt, L"%d / %d件", flt_cnt, total_iss);
                    else swprintf_s(cnt, L"%d件", total_iss);
                    dtw(mdc, cnt, { W - 60, y, W - 8, y + 16 }, C_LABEL, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
                }
                y += 18;

                const int32_t ITEM_H = 18;
                const int32_t avail = H - y - 2;
                const int32_t vis_n = (std::max)(1, avail / ITEM_H);
                const int32_t max_sc = (std::max)(0, flt_cnt - vis_n);
                if (s_issues_scroll > max_sc) s_issues_scroll = max_sc;

                const bool need_sb = (flt_cnt > vis_n);
                const int32_t SB_W = need_sb ? 6 : 0;
                const int32_t list_r = W - 8 - SB_W - (need_sb ? 3 : 0);

                int32_t drawn = 0;
                for (int32_t i = s_issues_scroll; i < flt_cnt && drawn < vis_n; i++, drawn++) {
                    const int32_t iy = y + drawn * ITEM_H;
                    if (iy + ITEM_H > H) break;
                    Issue& iss = res.issues[flt[i]];

                    if (s_hovered_issue == drawn) {
                        HBRUSH bh = CreateSolidBrush(RGB(36, 36, 52));
                        RECT hr = { 8, iy, list_r, iy + ITEM_H };
                        FillRect(mdc, &hr, bh);
                        DeleteObject(bh);
                    }
                    s_issue_hits.push_back({ iy, iy + ITEM_H, iss.f_start });

                    wchar_t buf[128];
                    if (iss.type == Issue::CLIP) {
                        swprintf_s(buf, L"● クリッピング  F%d〜F%d", iss.f_start + 1, iss.f_end + 1);
                        dtw(mdc, buf, { 14, iy, list_r, iy + ITEM_H }, C_RED);
                    } else {
                        swprintf_s(buf, L"○ 無音区間  F%d〜F%d", iss.f_start + 1, iss.f_end + 1);
                        dtw(mdc, buf, { 14, iy, list_r, iy + ITEM_H }, C_LABEL);
                    }
                }

                if (need_sb && drawn > 0) {
                    const int32_t sbx = W - 8 - SB_W;
                    const int32_t sby = y;
                    const int32_t sbh = drawn * ITEM_H;
                    {
                        HBRUSH b = CreateSolidBrush(C_PANEL);
                        RECT r = { sbx, sby, sbx + SB_W, sby + sbh };
                        FillRect(mdc, &r, b);
                        DeleteObject(b);
                    }
                    float vr = static_cast<float>(vis_n) / flt_cnt;
                    float pr = (max_sc > 0) ? static_cast<float>(s_issues_scroll) / max_sc : 0.0f;
                    int32_t thumb_h = (std::max)(12, static_cast<int32_t>(sbh * vr));
                    int32_t thumb_y = sby + static_cast<int32_t>((sbh - thumb_h) * pr);
                    {
                        HBRUSH b = CreateSolidBrush(s_sb_drag ? C_BLUE : C_LABEL);
                        RECT r = { sbx, thumb_y, sbx + SB_W, thumb_y + thumb_h };
                        FillRect(mdc, &r, b);
                        DeleteObject(b);
                    }
                    s_sb = { sbx, SB_W, sby, sbh, thumb_y, thumb_h, vis_n, max_sc, true };
                }
            }

            BitBlt(hdc, 0, 0, W, H, mdc, 0, 0, SRCCOPY);
            DeleteObject(SelectObject(mdc, mbmp));
            DeleteDC(mdc);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_SIZE:
            InvalidateRect(hwnd, nullptr, TRUE);
            return 0;
        case WM_DESTROY:
            if (s_br_ctrl) {
                DeleteObject(s_br_ctrl);
                s_br_ctrl = nullptr;
            }
            if (g_fL) {
                DeleteObject(g_fL);
                g_fL = nullptr;
            }
            if (g_fS) {
                DeleteObject(g_fS);
                g_fS = nullptr;
            }
            if (g_fM) {
                DeleteObject(g_fM);
                g_fM = nullptr;
            }
            return 0;
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}

void Register_Analyzer(HOST_APP_TABLE* host) {
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = g_hinstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = L"EAP2_Analyzer";
    RegisterClassEx(&wc);
    g_hwnd = CreateWindowEx(0, L"EAP2_Analyzer", nullptr, WS_CHILD, 0, 0, 340, 580, g_host_hwnd, nullptr, g_hinstance, nullptr);
    host->register_window_client(L"EAP2 Analyzer", g_hwnd);
    host->register_event_listener(EVENT_TYPE::CHANGE_EDIT_FRAME, nullptr, frame_change_cb);
}

void Uninitialize_Analyzer() {
    DestroyWindow(g_hwnd);
}