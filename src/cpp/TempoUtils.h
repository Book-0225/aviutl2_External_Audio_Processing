#include "Eap2Common.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#pragma once

struct Time_Signature {
    int32_t numerator;
    int32_t denominator;
};

struct AviUtl_Tempo_Segment {
    double start_time;
    double start_tick;
    double ticks_per_sec;
};

inline std::vector<BPM_INFO> get_all_bpm(EDIT_SECTION* edit) {
    if (!edit) return {};
    int32_t num = edit->get_grid_bpm_list(nullptr, 0, sizeof(BPM_INFO));
    if (num <= 0) return {};
    std::vector<BPM_INFO> list(num);
    int32_t got = edit->get_grid_bpm_list(list.data(), num, sizeof(BPM_INFO));
    list.resize(got);
    std::sort(list.begin(), list.end(), [](const BPM_INFO& a, const BPM_INFO& b) {
        return a.start < b.start;
    });
    return list;
}

inline const BPM_INFO* get_bpm_info(const std::vector<BPM_INFO>& list, double time) {
    const BPM_INFO* result = nullptr;
    for (const BPM_INFO& info : list) {
        if (info.start <= time)
            result = &info;
        else
            break;
    }
    return result;
}

inline Time_Signature get_time_signature(const BPM_INFO& info) {
    return Time_Signature{ info.beat, 4 };
}

inline std::vector<AviUtl_Tempo_Segment> build_aviutl_tempo_segments(const std::vector<BPM_INFO>& bpm_list, uint16_t tpqn) {
    std::vector<AviUtl_Tempo_Segment> segs;
    if (bpm_list.empty()) return segs;
    segs.reserve(bpm_list.size());

    double cum_tick = 0.0;
    double safe_tpqn = (tpqn > 0) ? static_cast<double>(tpqn) : 480.0;
    for (size_t i = 0; i < bpm_list.size(); ++i) {
        double start_time = bpm_list[i].start;
        double tempo = (bpm_list[i].tempo > 0) ? static_cast<double>(bpm_list[i].tempo) : 120.0;
        double ticks_per_sec = tempo / 60.0 * safe_tpqn;
        if (i > 0) {
            double dt = start_time - segs.back().start_time;
            cum_tick = segs.back().start_tick + dt * segs.back().ticks_per_sec;
        }
        segs.push_back({ start_time, cum_tick, ticks_per_sec });
    }
    return segs;
}

inline double aviutl_cumulative_tick(const std::vector<AviUtl_Tempo_Segment>& segs, double t) {
    if (segs.empty()) return 0.0;
    if (t <= segs.front().start_time) {
        const AviUtl_Tempo_Segment& s = segs.front();
        return s.start_tick + (t - s.start_time) * s.ticks_per_sec;
    }
    auto it = std::upper_bound(
        segs.begin(), segs.end(), t,
        [](double time, const AviUtl_Tempo_Segment& s) { return time < s.start_time; });
    const AviUtl_Tempo_Segment& seg = *(it - 1);
    return seg.start_tick + (t - seg.start_time) * seg.ticks_per_sec;
}

inline double aviutl_time_at_tick(const std::vector<AviUtl_Tempo_Segment>& segs, double tick) {
    if (segs.empty()) return 0.0;
    if (tick <= segs.front().start_tick) {
        const AviUtl_Tempo_Segment& s = segs.front();
        if (s.ticks_per_sec <= 0) return s.start_time;
        return s.start_time + (tick - s.start_tick) / s.ticks_per_sec;
    }
    auto it = std::upper_bound(
        segs.begin(), segs.end(), tick,
        [](double tk, const AviUtl_Tempo_Segment& s) { return tk < s.start_tick; });
    const AviUtl_Tempo_Segment& seg = *(it - 1);
    if (seg.ticks_per_sec <= 0) return seg.start_time;
    return seg.start_time + (tick - seg.start_tick) / seg.ticks_per_sec;
}