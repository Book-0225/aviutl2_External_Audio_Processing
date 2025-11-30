#define _USE_MATH_DEFINES
#include "Eap2Common.h"
#include <cmath>
#include <map>
#include "Avx2Utils.h"

#define TOOL_NAME L"DeEsser"

FILTER_ITEM_TRACK deess_freq(L"Frequency", 6000.0, 2000.0, 15000.0, 1.0);
FILTER_ITEM_TRACK deess_thresh(L"Threshold", -20.0, -60.0, 0.0, 0.1);
FILTER_ITEM_TRACK deess_amt(L"Amount", 10.0, 0.0, 48.0, 0.1);
FILTER_ITEM_TRACK deess_width(L"Width", 2.0, 0.1, 10.0, 0.1);

void* filter_items_deesser[] = {
    &deess_freq, &deess_thresh, &deess_amt, &deess_width, nullptr
};

struct DeesserBiquad {
    float x1 = 0.0f, x2 = 0.0f, y1 = 0.0f, y2 = 0.0f;
    inline float process_ret(float in, float b0, float b1, float b2, float a1, float a2) {
        float out = b0 * in + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
        if (std::abs(out) < 1e-20f) out = 0.0f;
        x2 = x1; x1 = in;
        y2 = y1; y1 = out;
        return out;
    }
};

struct DeesserState {
    DeesserBiquad scFilterL, scFilterR;
    DeesserBiquad mainFilterL, mainFilterR;
    float envelope = 0.0f;
    bool initialized = false;
    int64_t last_sample_index = -1;
    float cur_b0 = 1.0f, cur_b1 = 0.0f, cur_b2 = 0.0f, cur_a1 = 0.0f, cur_a2 = 0.0f;
};

static std::mutex g_deess_mutex;
static std::map<const void*, DeesserState> g_deess_states;

const int32_t BLOCK_SIZE = 64;
const int32_t CONTROL_RATE = 8;

bool func_proc_audio_deesser(FILTER_PROC_AUDIO* audio) {
    int32_t total_samples = audio->object->sample_num;
    if (total_samples <= 0) return true;

    float freq = (float)deess_freq.value;
    float thresh_db = (float)deess_thresh.value;
    float thresh_lin = std::pow(10.0f, thresh_db / 20.0f);
    float max_cut = -(float)deess_amt.value;
    float q = (float)deess_width.value;
    double sr = (audio->scene->sample_rate > 0) ? audio->scene->sample_rate : 44100.0;

    float att_coeff = std::exp(-1.0f / (5.0f * 0.001f * (float)sr));
    float rel_coeff = std::exp(-1.0f / (50.0f * 0.001f * (float)sr));

    DeesserState* state = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_deess_mutex);
        state = &g_deess_states[audio->object];
        if (state->last_sample_index != -1 && state->last_sample_index + total_samples != audio->object->sample_index) {
            state->scFilterL = DeesserBiquad(); state->scFilterR = DeesserBiquad();
            state->mainFilterL = DeesserBiquad(); state->mainFilterR = DeesserBiquad();
            state->envelope = 0.0f;
        }
        state->last_sample_index = audio->object->sample_index;
        state->initialized = true;
    }

    int32_t channels = (std::min)(2, audio->object->channel_num);
    thread_local std::vector<float> bufL, bufR;
    if (bufL.size() < static_cast<size_t>(total_samples)) {
        bufL.resize(total_samples);
        bufR.resize(total_samples);
    }
    if (channels >= 1) audio->get_sample_data(bufL.data(), 0);
    if (channels >= 2) audio->get_sample_data(bufR.data(), 1); else Avx2Utils::CopyBufferAVX2(bufR.data(), bufL.data(), total_samples);

    float omega_sc = 2.0f * (float)M_PI * freq / (float)sr;
    float sn_sc = std::sin(omega_sc);
    float cs_sc = std::cos(omega_sc);
    float alpha_sc = sn_sc / (2.0f * 0.707f);
    float norm_sc_inv = 1.0f / (1.0f + alpha_sc);
    float b0_sc = ((1.0f + cs_sc) * 0.5f) * norm_sc_inv;
    float b1_sc = -(1.0f + cs_sc) * norm_sc_inv;
    float b2_sc = b0_sc;
    float a1_sc = (-2.0f * cs_sc) * norm_sc_inv;
    float a2_sc = (1.0f - alpha_sc) * norm_sc_inv;
    float omega = 2.0f * (float)M_PI * freq / (float)sr;
    float sn = std::sin(omega);
    float cs = std::cos(omega);
    float alpha = sn / (2.0f * q);

    float c_b0 = state->cur_b0; float c_b1 = state->cur_b1; float c_b2 = state->cur_b2;
    float c_a1 = state->cur_a1; float c_a2 = state->cur_a2;

    for (int32_t i = 0; i < total_samples; i += BLOCK_SIZE) {
        int32_t block_count = (std::min)(BLOCK_SIZE, total_samples - i);
        float* pL = bufL.data() + i;
        float* pR = bufR.data() + i;

        for (int32_t k = 0; k < block_count; ++k) {
            float scL = state->scFilterL.process_ret(pL[k], b0_sc, b1_sc, b2_sc, a1_sc, a2_sc);
            float scR = state->scFilterR.process_ret(pR[k], b0_sc, b1_sc, b2_sc, a1_sc, a2_sc);
            float sc_level = (std::max)(std::abs(scL), std::abs(scR));

            if (sc_level > state->envelope)
                state->envelope = att_coeff * (state->envelope - sc_level) + sc_level;
            else
                state->envelope = rel_coeff * (state->envelope - sc_level) + sc_level;

            if ((i + k) % CONTROL_RATE == 0) {
                float gain_db = 0.0f;
                if (state->envelope > thresh_lin) {
                    float over = state->envelope - thresh_lin;
                    float ratio = (std::min)(1.0f, over * 4.0f);
                    gain_db = max_cut * ratio;
                }

                float A = std::pow(10.0f, gain_db * 0.025f);
                float norm = 1.0f + alpha / A;
                float norm_inv = 1.0f / norm;

                c_b0 = (1.0f + alpha * A) * norm_inv;
                c_b1 = (-2.0f * cs) * norm_inv;
                c_b2 = (1.0f - alpha * A) * norm_inv;
                c_a1 = (-2.0f * cs) * norm_inv;
                c_a2 = (1.0f - alpha / A) * norm_inv;
            }

            pL[k] = state->mainFilterL.process_ret(pL[k], c_b0, c_b1, c_b2, c_a1, c_a2);
            pR[k] = state->mainFilterR.process_ret(pR[k], c_b0, c_b1, c_b2, c_a1, c_a2);
        }
    }

    state->cur_b0 = c_b0; state->cur_b1 = c_b1; state->cur_b2 = c_b2;
    state->cur_a1 = c_a1; state->cur_a2 = c_a2;

    if (channels >= 1) audio->set_sample_data(bufL.data(), 0);
    if (channels >= 2) audio->set_sample_data(bufR.data(), 1);

    return true;
}

FILTER_PLUGIN_TABLE filter_plugin_table_deesser = {
    FILTER_PLUGIN_TABLE::FLAG_AUDIO,
    GEN_TOOL_NAME(TOOL_NAME),
    label,
    GEN_FILTER_INFO(TOOL_NAME),
    filter_items_deesser,
    nullptr,
    func_proc_audio_deesser
};