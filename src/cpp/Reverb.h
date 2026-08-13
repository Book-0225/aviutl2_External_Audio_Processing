#pragma once
class OnePoleLPF {
  public:
    float store = 0.0f;
    float a0 = 1.0f;
    float b1 = 0.0f;

    void set_cutoff(float cutoff, float sample_rate) {
        if (cutoff >= sample_rate * 0.49f) {
            a0 = 1.0f;
            b1 = 0.0f;
            return;
        }
        float costh = 2.0f - cosf(2.0f * 3.14159f * cutoff / sample_rate);
        b1 = costh - sqrtf(costh * costh - 1.0f);
        a0 = 1.0f - b1;
    }

    void set_damping(float damping) {
        b1 = damping;
        a0 = 1.0f - b1;
    }

    float process(float in) {
        store = in * a0 + store * b1;
        return store;
    }

    void clear() {
        store = 0.0f;
    }
};