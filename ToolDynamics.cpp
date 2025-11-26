#include "Eap2Common.h"
#include <cmath>
#include <vector>
#include <map>
#include <mutex>
#include <algorithm>
#include <regex>

#define TOOL_NAME L"Dynamics"

FILTER_ITEM_TRACK dyn_gate_thresh(L"Gate Threshold", -60.0, -90.0, 0.0, 0.1);
FILTER_ITEM_TRACK dyn_gate_rel(L"Gate Release", 200.0, 10.0, 2000.0, 10.0);
FILTER_ITEM_TRACK dyn_comp_thresh(L"Comp Threshold", 0.0, -60.0, 0.0, 0.1);
FILTER_ITEM_TRACK dyn_comp_ratio(L"Comp Ratio", 1.0, 1.0, 20.0, 0.1);
FILTER_ITEM_TRACK dyn_comp_attack(L"Comp Attack", 5.0, 0.1, 100.0, 0.1);
FILTER_ITEM_TRACK dyn_comp_release(L"Comp Release", 50.0, 10.0, 1000.0, 1.0);
FILTER_ITEM_TRACK dyn_comp_makeup(L"Makeup Gain", 0.0, 0.0, 30.0, 0.1);
FILTER_ITEM_TRACK dyn_limiter(L"Limiter", 0.0, -20.0, 0.0, 0.1);

void* filter_items_dynamics[] = {
    &dyn_gate_thresh,
    &dyn_gate_rel,
    &dyn_comp_thresh,
    &dyn_comp_ratio,
    &dyn_comp_attack,
    &dyn_comp_release,
    &dyn_comp_makeup,
    &dyn_limiter,
    nullptr
};

struct DynamicsState {
    double gate_gain = 1.0;
    double comp_envelope = 0.0;
    int64_t last_sample_index = -1;
};

static std::mutex g_dyn_state_mutex;
static std::map<const void*, DynamicsState> g_dyn_states;

bool func_proc_audio_dynamics(FILTER_PROC_AUDIO* audio) {
    int total_samples = audio->object->sample_num;
    if (total_samples <= 0) return true;
    int channels = (std::min)(2, audio->object->channel_num);

    double gate_th_db = dyn_gate_thresh.value;
    double gate_rel_ms = dyn_gate_rel.value;
    double comp_th_db = dyn_comp_thresh.value;
    double comp_ratio = dyn_comp_ratio.value;
    double comp_att_ms = dyn_comp_attack.value;
    double comp_rel_ms = dyn_comp_release.value;
    double comp_makeup_db = dyn_comp_makeup.value;
    double lim_db = dyn_limiter.value;

    if (gate_th_db <= -90.0 &&
        comp_ratio == 1.0 && comp_makeup_db == 0.0 &&
        lim_db >= 0.0) {
        return true;
    }

    DynamicsState* state = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_dyn_state_mutex);
        state = &g_dyn_states[audio->object];
        if (state->last_sample_index != -1 &&
            state->last_sample_index + total_samples != audio->object->sample_index) {
            state->gate_gain = 1.0;
            state->comp_envelope = 0.0;
        }
        state->last_sample_index = audio->object->sample_index;
    }

    double Fs = (audio->scene->sample_rate > 0) ? audio->scene->sample_rate : 44100.0;
    double gate_th_lin = pow(10.0, gate_th_db / 20.0);
    double gate_rel_coef = exp(-1.0 / (gate_rel_ms * 0.001 * Fs));
    double gate_atk_coef = exp(-1.0 / (10.0 * 0.001 * Fs));
    double comp_att_coef = exp(-1.0 / (comp_att_ms * 0.001 * Fs));
    double comp_rel_coef = exp(-1.0 / (comp_rel_ms * 0.001 * Fs));
    double makeup_lin = pow(10.0, comp_makeup_db / 20.0);
    bool use_comp = (comp_ratio > 1.0 || comp_makeup_db > 0.0);
    double lim_lin = pow(10.0, lim_db / 20.0);

    thread_local std::vector<float> bufL, bufR;
    if (bufL.size() < total_samples) { bufL.resize(total_samples); bufR.resize(total_samples); }

    if (channels >= 1) audio->get_sample_data(bufL.data(), 0);
    if (channels >= 2) audio->get_sample_data(bufR.data(), 1);
    else if (channels == 1) bufR = bufL;

    double current_gate_gain = state->gate_gain;
    double current_comp_env = state->comp_envelope;

    for (int i = 0; i < total_samples; ++i) {
        float l = bufL[i];
        float r = bufR[i];

        double in_abs = fabs(l);
        if (channels >= 2) in_abs = (std::max)(in_abs, (double)fabs(r));


        double gate_target;
        if (in_abs > gate_th_lin) gate_target = 1.0;
        else gate_target = 0.0;

        if (gate_target > current_gate_gain) {
            current_gate_gain = gate_target + gate_atk_coef * (current_gate_gain - gate_target);
        }
        else {
            current_gate_gain = gate_target + gate_rel_coef * (current_gate_gain - gate_target);
        }

        l *= (float)current_gate_gain;
        r *= (float)current_gate_gain;

        double gated_abs = in_abs * current_gate_gain;

        if (use_comp) {
            if (gated_abs > current_comp_env) {
                current_comp_env = gated_abs + comp_att_coef * (current_comp_env - gated_abs);
            }
            else {
                current_comp_env = gated_abs + comp_rel_coef * (current_comp_env - gated_abs);
            }
            double env_db = (current_comp_env > 1.0e-6) ? 20.0 * log10(current_comp_env) : -120.0;

            double gain_reduction_db = 0.0;
            if (env_db > comp_th_db) {
                gain_reduction_db = (env_db - comp_th_db) * (1.0 / comp_ratio - 1.0);
            }

            double comp_gain = pow(10.0, gain_reduction_db / 20.0);

            comp_gain *= makeup_lin;

            l *= (float)comp_gain;
            r *= (float)comp_gain;
        }

        if (l > lim_lin) l = (float)lim_lin;
        else if (l < -lim_lin) l = -(float)lim_lin;

        if (r > lim_lin) r = (float)lim_lin;
        else if (r < -lim_lin) r = -(float)lim_lin;


        bufL[i] = l;
        bufR[i] = r;
    }

    state->gate_gain = current_gate_gain;
    state->comp_envelope = current_comp_env;

    if (channels >= 1) audio->set_sample_data(bufL.data(), 0);
    if (channels >= 2) audio->set_sample_data(bufR.data(), 1);

    return true;
}

FILTER_PLUGIN_TABLE filter_plugin_table_dynamics = {
    FILTER_PLUGIN_TABLE::FLAG_AUDIO,
    []() {
        static std::wstring s = std::regex_replace(tool_name, std::wregex(regex_tool_name), TOOL_NAME);
        return s.c_str();
    }(),
    L"音声効果",
    []() {
        static std::wstring s = std::regex_replace(filter_info, std::wregex(regex_info_name), TOOL_NAME);
        return s.c_str();
    }(),
    filter_items_dynamics,
    nullptr,
    func_proc_audio_dynamics
};