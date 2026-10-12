#pragma once
#include <windows.h>
#include <cstdint>

inline COLORREF C_BACKGROUND = RGB(18, 18, 22);
inline COLORREF C_PANEL = RGB(26, 26, 34);
inline COLORREF C_CONTROL = RGB(30, 30, 42);
inline COLORREF C_BORDER = RGB(44, 44, 58);
inline COLORREF C_TEXT = RGB(216, 216, 230);
inline COLORREF C_TEXT_SUB = RGB(110, 110, 134);
inline COLORREF C_PASS = RGB(60, 200, 100);
inline COLORREF C_WARN = RGB(224, 192, 44);
inline COLORREF C_FAIL = RGB(220, 56, 48);
inline COLORREF C_ACCENT = RGB(60, 148, 220);
inline COLORREF C_SHORT_TERM = RGB(60, 148, 220);
inline COLORREF C_MOMENTARY = RGB(220, 128, 50);
inline COLORREF C_GRAPH_BG = RGB(10, 10, 18);
inline COLORREF C_GRAPH_GRID = RGB(32, 32, 44);
inline COLORREF C_GRAPH_LABEL = RGB(56, 56, 70);
inline COLORREF C_PROGRESS = RGB(44, 100, 190);
inline COLORREF C_BOX_BG = RGB(20, 20, 28);
inline COLORREF C_ROW_HOVER = RGB(36, 36, 52);
inline COLORREF C_SEPARATOR = RGB(32, 32, 44);
inline COLORREF C_TEXT_DISABLED = RGB(50, 50, 62);
inline COLORREF C_TARGET_LINE = RGB(60, 120, 60);
inline COLORREF C_TARGET_LABEL = RGB(80, 160, 80);

enum AnalyzerColorId : int32_t {
    AC_BACKGROUND,
    AC_PANEL,
    AC_CONTROL,
    AC_BORDER,
    AC_TEXT,
    AC_TEXT_SUB,
    AC_PASS,
    AC_WARN,
    AC_FAIL,
    AC_ACCENT,
    AC_SHORT_TERM,
    AC_MOMENTARY,
    AC_GRAPH_BG,
    AC_GRAPH_GRID,
    AC_GRAPH_LABEL,
    AC_PROGRESS,
    AC_COUNT
};

struct AnalyzerColorSlot {
    const wchar_t* cfg_key;
    COLORREF* target;
    COLORREF def;
    const char* host_key;
    int32_t host_index;
};

inline const AnalyzerColorSlot* analyzer_color_slots() {
    static const AnalyzerColorSlot slots[AC_COUNT] = {
        { L"ColorBackground", &C_BACKGROUND, RGB(18, 18, 22), "Background", 0 },
        { L"ColorPanel", &C_PANEL, RGB(26, 26, 34), "Grouping", 0 },
        { L"ColorControl", &C_CONTROL, RGB(30, 30, 42), "TrackBarRange", 0 },
        { L"ColorBorder", &C_BORDER, RGB(44, 44, 58), "GroupingSeparator", 0 },
        { L"ColorText", &C_TEXT, RGB(216, 216, 230), "Text", 0 },
        { L"ColorTextSub", &C_TEXT_SUB, RGB(110, 110, 134), "TextDisable", 0 },
        { L"ColorPass", &C_PASS, RGB(60, 200, 100), "LogScript", 0 },
        { L"ColorWarn", &C_WARN, RGB(224, 192, 44), "LogWarn", 0 },
        { L"ColorFail", &C_FAIL, RGB(220, 56, 48), "LogError", 0 },
        { L"ColorAccent", &C_ACCENT, RGB(60, 148, 220), "ZoomGauge", 0 },
        { L"ColorShortTerm", &C_SHORT_TERM, RGB(60, 148, 220), "ZoomGauge", 0 },
        { L"ColorMomentary", &C_MOMENTARY, RGB(220, 128, 50), nullptr, 0 },
        { L"ColorGraphBackground", &C_GRAPH_BG, RGB(10, 10, 18), "WindowSeparator", 0 },
        { L"ColorGraphGrid", &C_GRAPH_GRID, RGB(32, 32, 44), "Grouping", 0 },
        { L"ColorGraphLabel", &C_GRAPH_LABEL, RGB(56, 56, 70), "WindowBorder", 0 },
        { L"ColorProgress", &C_PROGRESS, RGB(44, 100, 190), "FooterProgress", 0 },
    };
    return slots;
}

inline COLORREF ac_mix(COLORREF a, COLORREF b, int32_t pct_b) {
    const int32_t pa = 100 - pct_b;
    return RGB((GetRValue(a) * pa + GetRValue(b) * pct_b) / 100,
               (GetGValue(a) * pa + GetGValue(b) * pct_b) / 100,
               (GetBValue(a) * pa + GetBValue(b) * pct_b) / 100);
}

inline COLORREF ac_scale(COLORREF c, int32_t pct) {
    return RGB(GetRValue(c) * pct / 100, GetGValue(c) * pct / 100, GetBValue(c) * pct / 100);
}

inline void analyzer_colors_update_derived() {
    C_BOX_BG = ac_mix(C_BACKGROUND, C_PANEL, 50);
    C_ROW_HOVER = ac_mix(C_CONTROL, C_BORDER, 40);
    C_SEPARATOR = ac_mix(C_PANEL, C_BORDER, 35);
    C_TEXT_DISABLED = ac_scale(C_TEXT_SUB, 50);
    C_TARGET_LINE = ac_scale(C_PASS, 60);
    C_TARGET_LABEL = ac_scale(C_PASS, 80);
}