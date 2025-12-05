#include "Eap2Common.h"
#include <string>
#include <filesystem>
#include <cmath>
#include <algorithm>
#include <map>
#include <vector>
#include "MidiParser.h"
#include "StringUtils.h"
#include "Avx2Utils.h"

#define TOOL_NAME L"MIDI Visualizer"

struct PseudoRandom {
    uint32_t state;
    PseudoRandom(uint32_t seed) : state(seed) {}
    uint32_t Next() {
        state = state * 1664525u + 1013904223u;
        return state;
    }
    float NextFloat() {
        return (Next() & 0xFFFFFF) / 16777215.0f;
    }
    float NextFloatRange(float min, float max) {
        return min + NextFloat() * (max - min);
    }
};

struct ProcessedNote {
    uint32_t startTick;
    uint32_t endTick;
    uint8_t pitch;
    uint8_t velocity;
    uint8_t channel;
};

struct VisualizerData {
    MidiParser parser;
    std::vector<ProcessedNote> notes;
    std::wstring lastFilePath;
    bool isLoaded = false;
    void RebuildNotes() {
        notes.clear();
        const auto &events = parser.GetEvents();
        struct ActiveInfo {
            uint32_t startTick;
            uint8_t velocity;
            bool active;
        };
        ActiveInfo activeNotes[16][128] = {0};
        for (const auto &ev : events)
        {
            uint8_t statusType = ev.status & 0xF0;
            uint8_t ch = ev.status & 0x0F;
            uint8_t note = ev.data1;
            uint8_t vel = ev.data2;
            if (statusType == 0x90 && vel > 0) {
                if (activeNotes[ch][note].active) {
                    ProcessedNote pn = {activeNotes[ch][note].startTick, ev.absoluteTick, note, activeNotes[ch][note].velocity, ch};
                    notes.push_back(pn);
                }
                activeNotes[ch][note] = {ev.absoluteTick, vel, true};
            }
            else if ((statusType == 0x80) || (statusType == 0x90 && vel == 0)) {
                if (activeNotes[ch][note].active) {
                    ProcessedNote pn = {activeNotes[ch][note].startTick, ev.absoluteTick, note, activeNotes[ch][note].velocity, ch};
                    notes.push_back(pn);
                    activeNotes[ch][note].active = false;
                }
            }
        }
    }
};

static std::map<int64_t, VisualizerData> g_dataMap;

PIXEL_RGBA HsvToRgb(double h, double s, double v, uint8_t a = 255) {
    double r = 0, g = 0, b = 0;
    if (s == 0) {
        r = v;
        g = v;
        b = v;
    }
    else {
        int32_t i = (int32_t)(h / 60.0);
        double f = (h / 60.0) - i;
        double p = v * (1.0 - s);
        double q = v * (1.0 - s * f);
        double t = v * (1.0 - s * (1.0 - f));
        switch (i % 6) {
        case 0: r = v; g = t; b = p; break;
        case 1: r = q; g = v; b = p; break;
        case 2: r = p; g = v; b = t; break;
        case 3: r = p; g = q; b = v; break;
        case 4: r = t; g = p; b = v; break;
        case 5: r = v; g = p; b = q; break;
        }
    }
    return {(uint8_t)(r * 255), (uint8_t)(g * 255), (uint8_t)(b * 255), a};
}

inline void BlendPixel(PIXEL_RGBA *buf, int32_t w, int32_t h, int32_t x, int32_t y, PIXEL_RGBA c) {
    if (x < 0 || y < 0 || x >= w || y >= h) return;
    int32_t idx = y * w + x;

    if (c.a == 0) return;
    if (c.a == 255) {
        buf[idx] = c;
    }
    else {
        PIXEL_RGBA bg = buf[idx];
        float alpha = c.a / 255.0f;
        float invAlpha = 1.0f - alpha;
        buf[idx].r = (uint8_t)(c.r * alpha + bg.r * invAlpha);
        buf[idx].g = (uint8_t)(c.g * alpha + bg.g * invAlpha);
        buf[idx].b = (uint8_t)(c.b * alpha + bg.b * invAlpha);
        buf[idx].a = (uint8_t)(std::min)(255, bg.a + c.a);
    }
}

void DrawBox(PIXEL_RGBA *buf, int32_t w, int32_t h, int32_t x, int32_t y, int32_t rw, int32_t rh, PIXEL_RGBA fillColor, PIXEL_RGBA borderColor, int32_t borderThick, int32_t radius, bool gradient) {
    if (rw <= 0 || rh <= 0) return;
    int32_t sx = (std::max)(0, x);
    int32_t sy = (std::max)(0, y);
    int32_t ex = (std::min)(w, x + rw);
    int32_t ey = (std::min)(h, y + rh);
    if (sx >= ex || sy >= ey) return;

    int32_t r = (std::min)(radius, (std::min)(rw / 2, rh / 2));
    int32_t r2 = r * r;
    
    for (int32_t iy = sy; iy < ey; ++iy) {
        PIXEL_RGBA *line = buf + (iy * w);
        for (int32_t ix = sx; ix < ex; ++ix) {
            int32_t lx = ix - x;
            int32_t ly = iy - y;
            
            if (r > 0) {
                int32_t dx = 0, dy = 0;
                if (lx < r) dx = r - lx;
                else if (lx >= rw - r) dx = lx - (rw - r - 1);
                
                if (ly < r) dy = r - ly;
                else if (ly >= rh - r) dy = ly - (rh - r - 1);
                
                if (dx > 0 && dy > 0) {
                    if (dx * dx + dy * dy > r2) continue;
                }
            }

            bool isBorder = false;
            if (borderThick > 0) {
                if (lx < borderThick || lx >= rw - borderThick || ly < borderThick || ly >= rh - borderThick) {
                    isBorder = true;
                }
            }

            if (isBorder) {
                BlendPixel(buf, w, h, ix, iy, borderColor);
            }
            else {
                PIXEL_RGBA fc = fillColor;
                if (gradient) {
                    float ratio = (float)lx / rw + (float)ly / rh;
                    float factor = 1.2f - (ratio * 0.4f);
                    fc.r = (uint8_t)(std::min)(255, (int32_t)(fc.r * factor));
                    fc.g = (uint8_t)(std::min)(255, (int32_t)(fc.g * factor));
                    fc.b = (uint8_t)(std::min)(255, (int32_t)(fc.b * factor));
                }
                BlendPixel(buf, w, h, ix, iy, fc);
            }
        }
    }
}

void DrawRing(PIXEL_RGBA *buf, int32_t w, int32_t h, int32_t cx, int32_t cy, float radius, float thickness, PIXEL_RGBA col) {
    int32_t rOut = (int32_t)ceil(radius);
    int32_t rIn = (int32_t)(radius - thickness);
    if (rIn < 0) rIn = 0;
    int32_t sx = (std::max)(0, cx - rOut);
    int32_t sy = (std::max)(0, cy - rOut);
    int32_t ex = (std::min)(w, cx + rOut);
    int32_t ey = (std::min)(h, cy + rOut);
    int32_t rOut2 = rOut * rOut;
    int32_t rIn2 = rIn * rIn;
    for (int32_t iy = sy; iy < ey; ++iy) {
        for (int32_t ix = sx; ix < ex; ++ix) {
            int32_t dx = ix - cx;
            int32_t dy = iy - cy;
            int32_t d2 = dx * dx + dy * dy;
            
            if (d2 <= rOut2 && d2 >= rIn2) {
                PIXEL_RGBA c = col;
                if (d2 > rOut2 - rOut) c.a /= 2;
                else if (d2 < rIn2 + rIn) c.a /= 2;
                
                BlendPixel(buf, w, h, ix, iy, c);
            }
        }
    }
}

FILTER_ITEM_FILE track_file = {L"MIDI File", L"", L"MIDI File (*.mid)\0*.mid;*.midi\0"};
FILTER_ITEM_GROUP group_canvas = {L"表示設定"};
FILTER_ITEM_TRACK track_width = {L"幅", 1280.0, 10.0, 4000.0, 1.0};
FILTER_ITEM_TRACK track_height = {L"高さ", 720.0, 10.0, 4000.0, 1.0};
FILTER_ITEM_COLOR color_bg = {L"背景色", 0x000000};
FILTER_ITEM_TRACK track_bg_alpha = {L"背景透明度", 0.0, 0.0, 255.0, 1.0};
FILTER_ITEM_GROUP group_view = {L"表示とスクロール"};
FILTER_ITEM_SELECT::ITEM list_scroll[] = {
    {L"右から左", 0},
    {L"左から右", 1},
    {L"上から下", 2},
    {L"下から上", 3},
    {nullptr}
};
FILTER_ITEM_SELECT select_scroll = {L"方向", 0, list_scroll};
FILTER_ITEM_SELECT::ITEM list_reaction[] = {
    {L"通過", 0},
    {L"消失+変色", 1},
    {L"消失+LED", 2},
    {L"消失", 3},
    {nullptr}
};
FILTER_ITEM_SELECT select_reaction = {L"挙動", 0, list_reaction};
FILTER_ITEM_TRACK track_zoom_time = {L"拡大率", 1.0, 0.01, 20.0, 0.01};
FILTER_ITEM_TRACK track_scroll_pos = {L"位置", 0.0, 0.0, 2000.0, 1.0};
FILTER_ITEM_GROUP group_keys = {L"キーの範囲とサイズ"};
FILTER_ITEM_TRACK track_key_min = {L"最小キー", 0, 0, 127, 1};
FILTER_ITEM_TRACK track_key_max = {L"最大キー", 127, 0, 127, 1};
FILTER_ITEM_CHECK check_auto_fit = {L"自動調整", true};
FILTER_ITEM_TRACK track_key_size = {L"サイズ(手動)", 12.0, 1.0, 200.0, 1.0};
FILTER_ITEM_GROUP group_sync = {L"同期", false};
FILTER_ITEM_TRACK track_offset = {L"オフセット", 0.0, -100.0, 100.0, 0.01};
FILTER_ITEM_CHECK check_sync_bpm = {L"BPMを同期", true};
FILTER_ITEM_TRACK track_manual_bpm = {L"BPM(手動)", 120.0, 1.0, 999.0, 0.1};
FILTER_ITEM_TRACK track_speed_mul = {L"速度", 1.0, 0.1, 10.0, 0.1};
FILTER_ITEM_GROUP group_filter = {L"フィルタ", false};
FILTER_ITEM_TRACK track_ch_target = {L"チャンネル", 0, 0, 16, 1};
FILTER_ITEM_TRACK track_vel_min = {L"最小強度", 0, 0, 127, 1};
FILTER_ITEM_CHECK check_hide_perc = {L"ドラムを無効", false};
FILTER_ITEM_GROUP group_style = {L"ノートスタイル"};
FILTER_ITEM_CHECK check_draw_notes = {L"ノーツを描画", true};
FILTER_ITEM_SELECT::ITEM list_col_mode[] = {
    {L"単色", 0},
    {L"虹色", 1},
    {L"チャンネル", 2},
    {L"強弱", 3},
    {nullptr}
};
FILTER_ITEM_SELECT select_col_mode = {L"カラーモード", 1, list_col_mode};
FILTER_ITEM_COLOR color_base = {L"基本色", 0x00FF00};
FILTER_ITEM_TRACK track_radius = {L"角の半径", 2.0, 0.0, 20.0, 1.0};
FILTER_ITEM_CHECK check_gradient = {L"グラデーション", true};
FILTER_ITEM_TRACK track_padding = {L"余白", 1.0, 0.0, 10.0, 1.0};
FILTER_ITEM_GROUP group_border = {L"縁", false};
FILTER_ITEM_CHECK check_border = {L"縁を描画", false};
FILTER_ITEM_TRACK track_border_w = {L"縁幅", 1.0, 0.0, 10.0, 1.0};
FILTER_ITEM_COLOR color_border = {L"縁色", 0xFFFFFF};
FILTER_ITEM_TRACK track_note_alpha = {L"ノートの不透明度", 255.0, 0.0, 255.0, 1.0};
FILTER_ITEM_GROUP group_kb = {L"装飾"};
FILTER_ITEM_CHECK check_draw_kb = {L"キーボード", true};
FILTER_ITEM_TRACK track_kb_width = {L"幅", 40.0, 0.0, 200.0, 1.0};
FILTER_ITEM_TRACK track_kb_black_ratio = {L"黒鍵割合", 0.65, 0.1, 1.0, 0.01};
FILTER_ITEM_TRACK track_kb_led_pos = {L"LED位置", 85.0, 0.0, 100.0, 1.0};
FILTER_ITEM_TRACK track_kb_led_size = {L"LEDサイズ", 60.0, 1.0, 100.0, 1.0};
FILTER_ITEM_TRACK track_kb_led_alpha = {L"LED不透明度", 75.0, 0.0, 100.0, 1.0};
FILTER_ITEM_COLOR color_grid = {L"グリッド色", 0xFFFFFF};
FILTER_ITEM_TRACK track_grid_width = {L"グリッド幅", 1.0, 0.0, 10.0, 1.0};
FILTER_ITEM_CHECK check_grid_h = {L"キーグリッド", true};
FILTER_ITEM_CHECK check_grid_v = {L"ビートグリッド", false};
FILTER_ITEM_GROUP group_effect = {L"エフェクト"};
FILTER_ITEM_TRACK track_sink_depth = {L"沈み込み", 0.0, 0.0, 10.0, 1.0};
FILTER_ITEM_TRACK track_ripple_size = {L"波紋", 0.0, 0.0, 500.0, 1.0};
FILTER_ITEM_TRACK track_particle_amt = {L"パーティクル", 10.0, 0.0, 500.0, 1.0};

void *filter_items_midi_visualizer[] = {
    &track_file,
    &group_canvas,
    &track_width,
    &track_height,
    &color_bg,
    &track_bg_alpha,
    &group_view,
    &select_scroll,
    &select_reaction,
    &track_zoom_time,
    &track_scroll_pos,
    &group_keys,
    &track_key_min,
    &track_key_max,
    &check_auto_fit,
    &track_key_size,
    &group_sync,
    &track_offset,
    &check_sync_bpm,
    &track_manual_bpm,
    &track_speed_mul,
    &group_filter,
    &track_ch_target,
    &track_vel_min,
    &check_hide_perc,
    &group_style,
    &check_draw_notes,
    &select_col_mode,
    &color_base,
    &track_radius,
    &check_gradient,
    &track_padding,
    &group_border,
    &check_border,
    &track_border_w,
    &color_border,
    &track_note_alpha,
    &group_kb,
    &check_draw_kb,
    &track_kb_width,
    &track_kb_black_ratio,
    &track_kb_led_pos,
    &track_kb_led_size,
    &track_kb_led_alpha,
    &color_grid,
    &track_grid_width,
    &check_grid_h,
    &check_grid_v,
    &group_effect,
    &track_sink_depth,
    &track_ripple_size,
    &track_particle_amt,
    nullptr
};

bool func_proc_video_midi_visualizer(FILTER_PROC_VIDEO *video) {
    int64_t objId = video->object->id;
    VisualizerData &data = g_dataMap[objId];
    LPCWSTR currentPathW = track_file.value;
    if (currentPathW && wcslen(currentPathW) > 0) {
        if (data.lastFilePath != currentPathW) {
            std::string pathUtf8 = StringUtils::WideToUtf8(currentPathW);
            if (data.parser.Load(pathUtf8)) {
                data.RebuildNotes();
                data.isLoaded = true;
            }
            else {
                data.isLoaded = false;
            }
            data.lastFilePath = currentPathW;
        }
    }
    if (!data.isLoaded) return true;
    int32_t w = (int32_t)track_width.value;
    int32_t h = (int32_t)track_height.value;
    if (w <= 0 || h <= 0) return true;
    std::vector<PIXEL_RGBA> imgBuf(w * h);
    uint8_t bgAlpha = (uint8_t)track_bg_alpha.value;
    if (bgAlpha == 0) {
        std::fill(imgBuf.begin(), imgBuf.end(), PIXEL_RGBA{0, 0, 0, 0});
    }
    else {
        PIXEL_RGBA bgCol = {(uint8_t)color_bg.value.b, (uint8_t)color_bg.value.g, (uint8_t)color_bg.value.r, bgAlpha};
        std::fill(imgBuf.begin(), imgBuf.end(), bgCol);
    }
    double currentTime = video->object->time + track_offset.value;
    double speedMul = track_speed_mul.value;
    int64_t currentTick = 0;
    uint16_t tpqn = data.parser.GetTPQN();
    double ticksPerSec = 0;
    double bpm = 120.0;
    if (check_sync_bpm.value) {
        currentTick = data.parser.GetTickAtTime(currentTime * speedMul);
        auto tempos = data.parser.GetTempoEvents();
        uint32_t mpqn = 500000;
        for (const auto& t : tempos) {
            if (t.absoluteTick <= currentTick) mpqn = t.mpqn;
            else break;
        }
        if (mpqn > 0) bpm = 60000000.0 / mpqn;
    }
    else {
        bpm = track_manual_bpm.value;
        if (bpm <= 0) bpm = 120;
        currentTick = (int64_t)(currentTime * (bpm * tpqn / 60.0) * speedMul);
    }
    
    ticksPerSec = (bpm * tpqn / 60.0) * speedMul;
    if (ticksPerSec <= 0) ticksPerSec = 1;
    int32_t scrollMode = select_scroll.value;
    int32_t reactionMode = select_reaction.value;
    double zoomT = track_zoom_time.value;
    double scrollPos = track_scroll_pos.value;
    int32_t minKey = (int32_t)track_key_min.value;
    int32_t maxKey = (int32_t)track_key_max.value;
    if (minKey > maxKey) std::swap(minKey, maxKey);
    double keySize = track_key_size.value;
    if (check_auto_fit.value) {
        int32_t range = maxKey - minKey + 1;
        if (range < 1) range = 1;
        if (scrollMode == 0 || scrollMode == 1)  keySize = (double)h / (double)range;
        else  keySize = (double)w / (double)range;
    }
    int32_t filterCh = (int32_t)track_ch_target.value;
    int32_t minVel = (int32_t)track_vel_min.value;
    bool hidePerc = check_hide_perc.value;
    int32_t colMode = select_col_mode.value;
    PIXEL_RGBA baseColor = {color_base.value.r, color_base.value.g, color_base.value.b, 255};
    int32_t radius = (int32_t)track_radius.value;
    bool gradient = check_gradient.value;
    int32_t pad = (int32_t)track_padding.value;
    bool border = check_border.value;
    int32_t borderW = (int32_t)track_border_w.value;
    PIXEL_RGBA borderColor = {color_border.value.r, color_border.value.g, color_border.value.b, 255};
    uint8_t noteAlpha = (uint8_t)track_note_alpha.value;
    bool drawKb = check_draw_kb.value;
    bool drawNotes = check_draw_notes.value;
    double kbWidth = track_kb_width.value;
    double blackKeyRatio = track_kb_black_ratio.value;
    double ledPosRatio = track_kb_led_pos.value / 100.0;
    double ledSizeRatio = track_kb_led_size.value / 100.0;
    uint8_t ledAlpha = (uint8_t)((track_kb_led_alpha.value * UINT8_MAX) / 100.0);
    int32_t grid_width = (int32_t)track_grid_width.value;
    int32_t range = maxKey - minKey + 1;
    int32_t sinkDepth = (int32_t)track_sink_depth.value;
    double rippleMaxR = track_ripple_size.value;
    int32_t particleAmt = (int32_t)track_particle_amt.value;
    bool enableRipple = (rippleMaxR > 0);
    bool enableParticle = (particleAmt > 0);
    double rippleLife = 1.5;
    double particleLife = 0.5;
    auto GetNoteColor = [&](const ProcessedNote &note) -> PIXEL_RGBA {
        PIXEL_RGBA col = baseColor;
        if (colMode == 1) col = HsvToRgb(fmod(note.pitch * 30.0, 360.0), 0.7, 1.0);
        else if (colMode == 2) col = HsvToRgb(note.channel * 22.5, 0.8, 1.0);
        else if (colMode == 3) col = HsvToRgb((127 - note.velocity) * 2.0, 1.0, 1.0);
        col.a = 255;
        return col;
    };
    std::vector<PIXEL_RGBA> activeKeyColors(128, {0,0,0,0});
    std::vector<bool> keyIsActive(128, false);
    double maxLookBackSec = (std::max)(rippleLife, particleLife);
    int64_t lookBackTicks = (int64_t)(maxLookBackSec * ticksPerSec);
    int64_t searchStartTick = currentTick - lookBackTicks;
    if (searchStartTick < 0) searchStartTick = 0;
    struct EffectEvent {
        int32_t pitch;
        double timeSec;
        PIXEL_RGBA color;
    };
    std::vector<EffectEvent> effectEvents;
    for (const auto &note : data.notes) {
        if (filterCh != 0 && note.channel != (filterCh - 1)) continue;
        if (hidePerc && note.channel == 9) continue;
        if (note.pitch < minKey || note.pitch > maxKey) continue;
        if (note.velocity < minVel) continue;
        if (reactionMode > 0) {
            if (currentTick >= note.startTick && currentTick < note.endTick) {
                keyIsActive[note.pitch] = true;
                activeKeyColors[note.pitch] = GetNoteColor(note);
            }
        }
        if ((enableRipple || enableParticle) && reactionMode > 0) {
            int64_t elapsedTick = currentTick - (int64_t)note.startTick;
            if (elapsedTick >= 0 && elapsedTick < lookBackTicks) {
                EffectEvent ev;
                ev.pitch = note.pitch;
                ev.timeSec = (double)elapsedTick / ticksPerSec;
                ev.color = GetNoteColor(note);
                effectEvents.push_back(ev);
            }
        }
    }
    struct ParticleEvent {
        int32_t pitch;
        double timeSec;
        PIXEL_RGBA color;
        uint32_t seed;
    };
    std::vector<ParticleEvent> particleEvents;
    if (enableParticle && reactionMode > 0) {
        for (const auto &note : data.notes) {
            if (filterCh != 0 && note.channel != (filterCh - 1)) continue;
            if (hidePerc && note.channel == 9) continue;
            if (note.pitch < minKey || note.pitch > maxKey) continue;
            if (note.velocity < minVel) continue;
            
            int64_t elapsedTick = currentTick - (int64_t)note.startTick;
            if (elapsedTick >= 0 && elapsedTick < (int64_t)(particleLife * ticksPerSec)) {
                uint32_t seed = note.pitch * 12345 + note.channel * 678 + note.startTick;
                particleEvents.push_back({note.pitch, (double)elapsedTick / ticksPerSec, GetNoteColor(note), seed});
            }
        }
    }
    auto isBlackKey = [&](int32_t note) -> bool {
        int32_t n = note % 12;
        if (n < 0) n += 12;
        return (n == 1 || n == 3 || n == 6 || n == 8 || n == 10);
    };
    PIXEL_RGBA colorWhiteKey = {240, 240, 240, 255};
    PIXEL_RGBA colorBlackKey = {40, 40, 40, 255};
    PIXEL_RGBA keyBorderCol = {0, 0, 0, 255};
    PIXEL_RGBA gridColH = {(uint8_t)color_grid.value.r, (uint8_t)color_grid.value.g, (uint8_t)color_grid.value.b, 100};
    PIXEL_RGBA gridColV = {(uint8_t)color_grid.value.b, (uint8_t)color_grid.value.g, (uint8_t)color_grid.value.r, 100};
    auto DrawLED = [&](int32_t kx, int32_t ky, int32_t kw, int32_t kh, PIXEL_RGBA col) {
        col.a = ledAlpha;
        int32_t ledSize = 0;
        int32_t lx = 0, ly = 0;
        if (scrollMode == 0 || scrollMode == 1) {
            ledSize = (int32_t)(kh * ledSizeRatio);
            if (ledSize < 2) ledSize = 2;
            if (scrollMode == 0) {
                int32_t distFromBase = (int32_t)(kw * (1.0 - ledPosRatio));
                lx = kx + distFromBase;
            } else {
                int32_t distFromBase = (int32_t)(kw * (1.0 - ledPosRatio));
                lx = (kx + kw) - distFromBase - ledSize;
            }
            ly = ky + (kh - ledSize) / 2;
        } else {
            ledSize = (int32_t)(kw * ledSizeRatio);
            if (ledSize < 2) ledSize = 2;
            lx = kx + (kw - ledSize) / 2;
            if (scrollMode == 2) {
                int32_t distFromBase = (int32_t)(kh * (1.0 - ledPosRatio));
                ly = (ky + kh) - distFromBase - ledSize;
            } else {
                int32_t distFromBase = (int32_t)(kh * (1.0 - ledPosRatio));
                ly = ky + distFromBase;
            }
        }
        DrawBox(imgBuf.data(), w, h, lx, ly, ledSize, ledSize, col, {}, 0, ledSize / 2, false);
    };
    auto RenderRipple = [&](int32_t cx, int32_t cy, PIXEL_RGBA col, double timeSec) {
        if (timeSec >= rippleLife) return;
        double progress = timeSec / rippleLife;
        double radius = rippleMaxR * progress;
        double alphaFactor = 1.0 - progress;
        col.a = (uint8_t)(col.a * alphaFactor);
        float thick = 8.0f + 12.0f * (float)progress;
        DrawRing(imgBuf.data(), w, h, cx, cy, (float)radius, thick, col);
    };
    for (int32_t i = 0; i < range; i++) {
        int32_t noteNum = minKey + i;
        bool currentIsBlack = isBlackKey(noteNum);
        int32_t n = noteNum % 12;
        if (n < 0) n += 12;
        bool isActive = keyIsActive[noteNum];
        PIXEL_RGBA activeCol = activeKeyColors[noteNum];
        int32_t kx = 0, ky = 0, kw = 0, kh = 0;
        int32_t sinkOffsetX = 0;
        int32_t sinkOffsetY = 0;
        if (isActive) {
            if (scrollMode == 0) sinkOffsetX = sinkDepth;
            else if (scrollMode == 1) sinkOffsetX = -sinkDepth;
            else if (scrollMode == 2) sinkOffsetY = -sinkDepth;
            else if (scrollMode == 3) sinkOffsetY = sinkDepth;
        }
        if (scrollMode == 0 || scrollMode == 1) {
            kh = (int32_t)ceil(keySize);
            kw = (int32_t)kbWidth;
            ky = h - (int32_t)((i + 1) * keySize);
            kx = (scrollMode == 0) ? 0 : w - kw;
            if (check_grid_h.value) {
                int32_t gy = ky + kh;
                if (gy < h && gy >= 0) DrawBox(imgBuf.data(), w, h, 0, gy, w, grid_width, gridColH, {}, 0, 0, false);
            }
            if (drawKb && kw > 0) {
                PIXEL_RGBA bgCol = colorWhiteKey;
                PIXEL_RGBA bkCol = colorBlackKey;
                if (reactionMode == 1 && isActive) {
                    if (currentIsBlack) bkCol = activeCol;
                    else bgCol = activeCol;
                }
                if (currentIsBlack) {
                    DrawBox(imgBuf.data(), w, h, kx, ky, kw, kh, colorWhiteKey, {}, 0, 0, false);
                    int32_t cy = ky + kh / 2;
                    DrawBox(imgBuf.data(), w, h, kx, cy, kw, 1, keyBorderCol, {}, 0, 0, false);
                    double shortKw = (double)kw * blackKeyRatio;
                    int32_t diff = kw - (int32_t)shortKw;
                    int32_t blackKw = (int32_t)shortKw;
                    int32_t blackKx = kx;
                    if (scrollMode == 0) blackKx += diff;
                    DrawBox(imgBuf.data(), w, h, blackKx + sinkOffsetX, ky + sinkOffsetY, blackKw, kh, bkCol, keyBorderCol, 1, 0, false);
                    if (reactionMode == 2 && isActive) DrawLED(blackKx + sinkOffsetX, ky + sinkOffsetY, blackKw, kh, activeCol);
                } else {
                    DrawBox(imgBuf.data(), w, h, kx + sinkOffsetX, ky + sinkOffsetY, kw, kh, bgCol, {}, 0, 0, false);
                    if (reactionMode == 2 && isActive) DrawLED(kx + sinkOffsetX, ky + sinkOffsetY, kw, kh, activeCol);
                    if (n == 0 || n == 5) DrawBox(imgBuf.data(), w, h, kx, ky + kh - 1, kw, 1, keyBorderCol, {}, 0, 0, false);
                }
            }
        }
        else {
            kw = (int32_t)ceil(keySize);
            kh = (int32_t)kbWidth;
            kx = (int32_t)(i * keySize);
            ky = (scrollMode == 2) ? h - kh : 0;
            if (check_grid_h.value) {
                int32_t gx = kx;
                if (gx < w && gx >= 0) DrawBox(imgBuf.data(), w, h, gx, 0, grid_width, h, gridColV, {}, 0, 0, false);
            }
            if (drawKb && kh > 0) {
                PIXEL_RGBA bgCol = colorWhiteKey;
                PIXEL_RGBA bkCol = colorBlackKey;
                if (reactionMode == 1 && isActive) {
                    if (currentIsBlack) bkCol = activeCol;
                    else bgCol = activeCol;
                }
                if (currentIsBlack) {
                    DrawBox(imgBuf.data(), w, h, kx, ky, kw, kh, colorWhiteKey, {}, 0, 0, false);
                    int32_t cx = kx + kw / 2;
                    DrawBox(imgBuf.data(), w, h, cx, ky, 1, kh, keyBorderCol, {}, 0, 0, false);
                    double shortKh = (double)kh * blackKeyRatio;
                    int32_t diff = kh - (int32_t)shortKh;
                    int32_t blackKh = (int32_t)shortKh;
                    int32_t blackKy = ky;
                    if (scrollMode == 3) blackKy += diff;
                    DrawBox(imgBuf.data(), w, h, kx + sinkOffsetX, blackKy + sinkOffsetY, kw, blackKh, bkCol, keyBorderCol, 1, 0, false);
                    if (reactionMode == 2 && isActive) DrawLED(kx + sinkOffsetX, blackKy + sinkOffsetY, kw, blackKh, activeCol);
                } else {
                    DrawBox(imgBuf.data(), w, h, kx + sinkOffsetX, ky + sinkOffsetY, kw, kh, bgCol, {}, 0, 0, false);
                    if (reactionMode == 2 && isActive) DrawLED(kx + sinkOffsetX, ky + sinkOffsetY, kw, kh, activeCol);
                    if (n == 0 || n == 5) {
                        DrawBox(imgBuf.data(), w, h, kx, ky, 1, kh, keyBorderCol, {}, 0, 0, false);
                    }
                }
            }
        }
    }
    if (enableRipple) {
        for (const auto& ev : effectEvents) {
            if (ev.pitch < minKey || ev.pitch > maxKey) continue;
            int32_t i = ev.pitch - minKey;
            int32_t cx = 0, cy = 0;
            int32_t sPos = (int32_t)scrollPos;
            if (scrollMode == 0 || scrollMode == 1) {
                int32_t kh = (int32_t)ceil(keySize);
                int32_t ky = h - (int32_t)((i + 1) * keySize);
                cy = ky + kh / 2;
                if (scrollMode == 0) cx = sPos;
                else cx = w - sPos;
            } else {
                int32_t kw = (int32_t)ceil(keySize);
                int32_t kx = (int32_t)(i * keySize);
                cx = kx + kw / 2;
                if (scrollMode == 2) cy = h - sPos;
                else cy = sPos;
            }
            RenderRipple(cx, cy, ev.color, ev.timeSec);
        }
    }
    if (check_grid_v.value && tpqn > 0) {
        PIXEL_RGBA beatCol = {(uint8_t)color_grid.value.r, (uint8_t)color_grid.value.g, (uint8_t)color_grid.value.b, 80};
        PIXEL_RGBA measureCol = {(uint8_t)color_grid.value.r, (uint8_t)color_grid.value.g, (uint8_t)color_grid.value.b, 160};
        int32_t maxDim = (scrollMode < 2) ? w : h;
        double pixelsPerBeat = zoomT * tpqn;
        if (pixelsPerBeat > 0.5) {
            int64_t tickOffset = (int64_t)(maxDim / zoomT) + (int64_t)(scrollPos / zoomT) + tpqn * 8;
            int64_t minTick = (std::max)((int64_t)0, currentTick - tickOffset);
            int64_t maxTick = currentTick + tickOffset;
            int64_t startLoopTick = (minTick / tpqn) * tpqn;
            for (int64_t t = startLoopTick; t < maxTick; t += tpqn) {
                auto ts = data.parser.GetTimeSignatureAt((uint32_t)t);
                int64_t measureLen = (int64_t)ts.numerator * tpqn * 4 / ts.denominator;
                if (measureLen == 0) measureLen = tpqn * 4;
                int64_t relTick = t - ts.absoluteTick;
                bool isMeasure = (relTick >= 0) && (relTick % measureLen == 0);
                int64_t diff = t - currentTick;
                int32_t pos = 0;
                if (scrollMode == 0) pos = (int32_t)(scrollPos + diff * zoomT);
                else if (scrollMode == 1) pos = (int32_t)(w - (scrollPos + diff * zoomT));
                else if (scrollMode == 2) pos = (int32_t)(h - (scrollPos + diff * zoomT));
                else if (scrollMode == 3) pos = (int32_t)(scrollPos + diff * zoomT);
                if (pos < -2 || pos > maxDim + 2) continue;
                PIXEL_RGBA col = isMeasure ? measureCol : beatCol;
                if (scrollMode == 0 || scrollMode == 1) DrawBox(imgBuf.data(), w, h, pos, 0, grid_width, h, col, {}, 0, 0, false);
                else DrawBox(imgBuf.data(), w, h, 0, pos, w, grid_width, col, {}, 0, 0, false);
            }
        }
    }
    if (drawNotes) {
        for (const auto &note : data.notes) {
            if (filterCh != 0 && note.channel != (filterCh - 1)) continue;
            if (hidePerc && note.channel == 9) continue;
            if (note.pitch < minKey || note.pitch > maxKey) continue;
            if (note.velocity < minVel) continue;
            int64_t diffStart = (int64_t)note.startTick - currentTick;
            int64_t diffEnd = (int64_t)note.endTick - currentTick;
            if (reactionMode > 0) {
                if (diffEnd <= 0) continue;
                if (diffStart < 0) diffStart = 0;
            }
            int32_t x = 0, y = 0, rw = 0, rh = 0;
            int32_t keyIndex = note.pitch - minKey;
            if (scrollMode == 0) {
                int32_t nStart = (int32_t)(scrollPos + diffStart * zoomT);
                int32_t nEnd = (int32_t)(scrollPos + diffEnd * zoomT);
                x = nStart;
                rw = nEnd - nStart;
                rh = (int32_t)keySize;
                y = h - (int32_t)((keyIndex + 1) * keySize);
            }
            else if (scrollMode == 1) {
                int32_t nStart = (int32_t)(w - (scrollPos + diffStart * zoomT));
                int32_t nEnd = (int32_t)(w - (scrollPos + diffEnd * zoomT));
                x = nEnd;
                rw = nStart - nEnd;
                rh = (int32_t)keySize;
                y = h - (int32_t)((keyIndex + 1) * keySize);
            }
            else if (scrollMode == 2) {
                int32_t nStart = (int32_t)(h - (scrollPos + diffStart * zoomT));
                int32_t nEnd = (int32_t)(h - (scrollPos + diffEnd * zoomT));
                y = nEnd;
                rh = nStart - nEnd;
                rw = (int32_t)keySize;
                x = (int32_t)(keyIndex * keySize);
            }
            else if (scrollMode == 3) {
                int32_t nStart = (int32_t)(scrollPos + diffStart * zoomT);
                int32_t nEnd = (int32_t)(scrollPos + diffEnd * zoomT);
                y = nStart;
                rh = nEnd - nStart;
                rw = (int32_t)keySize;
                x = (int32_t)(keyIndex * keySize);
            }
            if (rw <= 0) rw = 1;
            if (rh <= 0) rh = 1;
            x += pad;
            y += pad;
            rw -= pad * 2;
            rh -= pad * 2;
            PIXEL_RGBA col = GetNoteColor(note);
            col.a = noteAlpha;
            DrawBox(imgBuf.data(), w, h, x, y, rw, rh, col, borderColor, border ? borderW : 0, radius, gradient);
        }
    }
    if (enableParticle && reactionMode > 0) {
        float emissionInterval = 0.03f;
        for (const auto &note : data.notes) {
            if (filterCh != 0 && note.channel != (filterCh - 1)) continue;
            if (hidePerc && note.channel == 9) continue;
            if (note.pitch < minKey || note.pitch > maxKey) continue;
            if (note.velocity < minVel) continue;
            double noteDurationSec = (double)(note.endTick - note.startTick) / ticksPerSec;
            double timeSinceStart = (double)(currentTick - (int64_t)note.startTick) / ticksPerSec;
            double minEmitTime = (std::max)(0.0, timeSinceStart - particleLife);
            double maxEmitTime = (std::min)(noteDurationSec, timeSinceStart);
            if (minEmitTime >= maxEmitTime) continue;
            int32_t k_min = (int32_t)ceil(minEmitTime / emissionInterval);
            int32_t k_max = (int32_t)floor(maxEmitTime / emissionInterval);
            int32_t i = note.pitch - minKey;
            int32_t cx = 0, cy = 0;
            int32_t sPos = (int32_t)scrollPos;
            if (scrollMode == 0 || scrollMode == 1) {
                int32_t kh = (int32_t)ceil(keySize);
                int32_t ky = h - (int32_t)((i + 1) * keySize);
                cy = ky + kh / 2;
                if (scrollMode == 0) cx = sPos; else cx = w - sPos;
            } else {
                int32_t kw = (int32_t)ceil(keySize);
                int32_t kx = (int32_t)(i * keySize);
                cx = kx + kw / 2;
                if (scrollMode == 2) cy = h - sPos; else cy = sPos;
            }
            PIXEL_RGBA noteColor = GetNoteColor(note);
            int32_t countPerStep = (std::max)(1, (int32_t)(particleAmt * 0.2));
            int32_t total_k = k_max - k_min + 1;
            int32_t total_particles = total_k * countPerStep;
            Avx2Utils::ParticleBatchParams params;
            params.countPerStep = countPerStep;
            params.k_min = k_min;
            params.emissionInterval = (float)emissionInterval;
            params.timeSinceStart = (float)timeSinceStart;
            params.baseSeed = note.pitch * 99999 + note.startTick;
            params.cx = (float)cx;
            params.cy = (float)cy;
            params.scrollMode = scrollMode;
            params.gravity = 500.0f;
            params.particleLife = (float)particleLife;
            for (int32_t idx = 0; idx < total_particles; idx += 8) {
                params.start_idx = idx;
                float px[8], py[8], ages[8];
                int32_t mask = Avx2Utils::ComputeParticleBatchAVX2(params, px, py, ages);
                for (int32_t m = 0; m < 8; ++m) {
                    if (idx + m >= total_particles) break;
                    if ((mask >> m) & 1) {
                        if (px[m] >= 0 && px[m] < w && py[m] >= 0 && py[m] < h) {
                            float lifeRatio = ages[m] / (float)particleLife;
                            PIXEL_RGBA col = noteColor;
                            col.a = (uint8_t)(255 * (1.0f - lifeRatio));
                            int32_t pSize = (int32_t)(3.0f * (1.0f - lifeRatio) + 1.0f);
                            DrawBox(imgBuf.data(), w, h, (int32_t)px[m], (int32_t)py[m], pSize, pSize, col, {}, 0, 0, false);
                        }
                    }
                }
            }
        }
    }
    video->set_image_data(imgBuf.data(), w, h);
    return true;
}

FILTER_PLUGIN_TABLE filter_plugin_table_midi_visualizer = {
    FILTER_PLUGIN_TABLE::FLAG_VIDEO | FILTER_PLUGIN_TABLE::FLAG_INPUT,
    GEN_TOOL_NAME(TOOL_NAME),
    label,
    GEN_FILTER_INFO(TOOL_NAME),
    filter_items_midi_visualizer,
    func_proc_video_midi_visualizer,
    nullptr
};