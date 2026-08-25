#pragma once
#include "Avx2Utils.h"
#include "FftUtils.h"

#include <algorithm>
#include <complex>
#include <cstdint>
#include <memory>
#include <vector>

namespace ConvReverb {

class IrSpectrum {
  public:
    void Build(const std::vector<float>& ir, int32_t block_size) {
        block_size_ = block_size;
        fft_size_ = block_size * 2;

        int32_t partitions = (static_cast<int32_t>(ir.size()) + block_size_ - 1) / block_size_;
        if (partitions < 1) partitions = 1;
        num_partitions_ = partitions;

        h_freq_.assign(num_partitions_, std::vector<std::complex<float>>(fft_size_));
        for (int32_t k = 0; k < num_partitions_; ++k) {
            std::vector<std::complex<float>> buf(fft_size_, std::complex<float>(0.0f, 0.0f));
            for (int32_t i = 0; i < block_size_; ++i) {
                int64_t idx = static_cast<int64_t>(k) * block_size_ + i;
                float v = (idx < static_cast<int64_t>(ir.size())) ? ir[idx] : 0.0f;
                buf[i] = std::complex<float>(v, 0.0f);
            }
            FftUtils::FftInplace(buf.data(), fft_size_, false);
            h_freq_[k] = std::move(buf);
        }
        built_ = true;
    }

    bool IsBuilt() const { return built_; }
    int32_t BlockSize() const { return block_size_; }
    int32_t FftSize() const { return fft_size_; }
    int32_t NumPartitions() const { return num_partitions_; }
    const std::vector<std::complex<float>>& Partition(int32_t k) const { return h_freq_[k]; }

  private:
    int32_t block_size_ = 0;
    int32_t fft_size_ = 0;
    int32_t num_partitions_ = 0;
    bool built_ = false;
    std::vector<std::vector<std::complex<float>>> h_freq_;
};

class PartitionedConvolver {
  public:
    void Init(std::shared_ptr<const IrSpectrum> spectrum) {
        spectrum_ = std::move(spectrum);
        if (!spectrum_ || !spectrum_->IsBuilt()) {
            ready_ = false;
            return;
        }
        const int32_t p = spectrum_->NumPartitions();
        const int32_t fft_size = spectrum_->FftSize();
        block_size_ = spectrum_->BlockSize();

        x_hist_.assign(p, std::vector<std::complex<float>>(fft_size, std::complex<float>(0.0f, 0.0f)));
        sliding_.assign(fft_size, 0.0f);
        y_scratch_.assign(fft_size, std::complex<float>(0.0f, 0.0f));
        hist_write_ = 0;
        ready_ = true;
    }

    bool IsReady() const { return ready_; }
    int32_t BlockSize() const { return block_size_; }

    void Process(const float* in, float* out) {
        if (!ready_) {
            std::fill(out, out + block_size_, 0.0f);
            return;
        }
        const int32_t fft_size = spectrum_->FftSize();
        const int32_t p = spectrum_->NumPartitions();

        for (int32_t i = 0; i < block_size_; ++i) sliding_[i] = sliding_[block_size_ + i];
        for (int32_t i = 0; i < block_size_; ++i) sliding_[block_size_ + i] = in[i];

        std::vector<std::complex<float>>& x0 = x_hist_[hist_write_];
        for (int32_t i = 0; i < fft_size; ++i) x0[i] = std::complex<float>(sliding_[i], 0.0f);
        FftUtils::FftInplace(x0.data(), fft_size, false);

        std::fill(y_scratch_.begin(), y_scratch_.end(), std::complex<float>(0.0f, 0.0f));
        for (int32_t k = 0; k < p; ++k) {
            int32_t idx = hist_write_ - k;
            if (idx < 0) idx += p;
            const auto& X = x_hist_[idx];
            const auto& H = spectrum_->Partition(k);
            Avx2Utils::ComplexMulAccumulateAVX2(y_scratch_.data(), X.data(), H.data(), static_cast<size_t>(fft_size));
        }

        FftUtils::FftInplace(y_scratch_.data(), fft_size, true);
        for (int32_t i = 0; i < block_size_; ++i) out[i] = y_scratch_[block_size_ + i].real();

        hist_write_ = (hist_write_ + 1) % p;
    }

    void Clear() {
        for (auto& v : x_hist_) std::fill(v.begin(), v.end(), std::complex<float>(0.0f, 0.0f));
        std::fill(sliding_.begin(), sliding_.end(), 0.0f);
        hist_write_ = 0;
    }

  private:
    std::shared_ptr<const IrSpectrum> spectrum_;
    int32_t block_size_ = 0;
    int32_t hist_write_ = 0;
    bool ready_ = false;

    std::vector<std::vector<std::complex<float>>> x_hist_;
    std::vector<std::complex<float>> y_scratch_;
    std::vector<float> sliding_;
};

class StreamingConvolver {
  public:
    void Init(std::shared_ptr<const IrSpectrum> spectrum) {
        conv_.Init(spectrum);
        ready_ = conv_.IsReady();
        if (!ready_) return;
        block_size_ = conv_.BlockSize();
        in_buf_.assign(block_size_, 0.0f);
        in_fill_ = 0;
        out_buf_.assign(block_size_, 0.0f);
        out_read_ = 0;
    }

    bool IsReady() const { return ready_; }

    void Process(const float* in, float* out, int32_t n) {
        if (!ready_) {
            std::fill(out, out + n, 0.0f);
            return;
        }
        int32_t i = 0;
        while (i < n) {
            int32_t space = block_size_ - in_fill_;
            int32_t take = (std::min)(space, n - i);
            std::copy(in + i, in + i + take, in_buf_.begin() + in_fill_);
            in_fill_ += take;
            i += take;
            if (in_fill_ == block_size_) {
                std::vector<float> block_out(block_size_);
                conv_.Process(in_buf_.data(), block_out.data());
                out_buf_.insert(out_buf_.end(), block_out.begin(), block_out.end());
                in_fill_ = 0;
            }
        }

        int32_t avail = static_cast<int32_t>(out_buf_.size()) - out_read_;
        int32_t give = (std::min)(avail, n);
        for (int32_t k = 0; k < give; ++k) out[k] = out_buf_[out_read_ + k];
        for (int32_t k = give; k < n; ++k) out[k] = 0.0f;
        out_read_ += give;

        if (out_read_ > block_size_ * 4) {
            out_buf_.erase(out_buf_.begin(), out_buf_.begin() + out_read_);
            out_read_ = 0;
        }
    }

    void Clear() {
        conv_.Clear();
        std::fill(in_buf_.begin(), in_buf_.end(), 0.0f);
        in_fill_ = 0;
        out_buf_.assign(block_size_, 0.0f);
        out_read_ = 0;
    }

  private:
    PartitionedConvolver conv_;
    int32_t block_size_ = 0;
    int32_t in_fill_ = 0;
    bool ready_ = false;
    std::vector<float> in_buf_;
    std::vector<float> out_buf_;
    int32_t out_read_ = 0;
};

class TrueStereoSpectrum {
  public:
    void Build(const std::vector<float>& ir_ll, const std::vector<float>& ir_lr, const std::vector<float>& ir_rl, const std::vector<float>& ir_rr, int32_t block_size) {
        size_t max_len = (std::max)({ ir_ll.size(), ir_lr.size(), ir_rl.size(), ir_rr.size() });
        auto pad = [&](const std::vector<float>& v) {
            std::vector<float> out = v;
            out.resize(max_len, 0.0f);
            return out;
        };
        h_ll_.Build(pad(ir_ll), block_size);
        h_lr_.Build(pad(ir_lr), block_size);
        h_rl_.Build(pad(ir_rl), block_size);
        h_rr_.Build(pad(ir_rr), block_size);
        built_ = true;
    }

    bool IsBuilt() const { return built_; }
    int32_t BlockSize() const { return h_ll_.BlockSize(); }
    int32_t FftSize() const { return h_ll_.FftSize(); }
    int32_t NumPartitions() const { return h_ll_.NumPartitions(); }
    const IrSpectrum& LL() const { return h_ll_; }
    const IrSpectrum& LR() const { return h_lr_; }
    const IrSpectrum& RL() const { return h_rl_; }
    const IrSpectrum& RR() const { return h_rr_; }

  private:
    IrSpectrum h_ll_;
    IrSpectrum h_lr_;
    IrSpectrum h_rl_;
    IrSpectrum h_rr_;
    bool built_ = false;
};

class TrueStereoConvolver {
  public:
    void Init(std::shared_ptr<const TrueStereoSpectrum> spectrum) {
        spectrum_ = std::move(spectrum);
        if (!spectrum_ || !spectrum_->IsBuilt()) {
            ready_ = false;
            return;
        }
        block_size_ = spectrum_->BlockSize();
        const int32_t fft_size = spectrum_->FftSize();
        const int32_t p = spectrum_->NumPartitions();

        x_hist_l_.assign(p, std::vector<std::complex<float>>(fft_size, std::complex<float>(0.0f, 0.0f)));
        x_hist_r_.assign(p, std::vector<std::complex<float>>(fft_size, std::complex<float>(0.0f, 0.0f)));
        sliding_l_.assign(fft_size, 0.0f);
        sliding_r_.assign(fft_size, 0.0f);
        y_l_.assign(fft_size, std::complex<float>(0.0f, 0.0f));
        y_r_.assign(fft_size, std::complex<float>(0.0f, 0.0f));
        hist_write_ = 0;
        ready_ = true;
    }

    bool IsReady() const { return ready_; }
    int32_t BlockSize() const { return block_size_; }

    void Process(const float* in_l, const float* in_r, float* out_l, float* out_r) {
        if (!ready_) {
            std::fill(out_l, out_l + block_size_, 0.0f);
            std::fill(out_r, out_r + block_size_, 0.0f);
            return;
        }
        const int32_t fft_size = spectrum_->FftSize();
        const int32_t p = spectrum_->NumPartitions();

        for (int32_t i = 0; i < block_size_; ++i) sliding_l_[i] = sliding_l_[block_size_ + i];
        for (int32_t i = 0; i < block_size_; ++i) sliding_l_[block_size_ + i] = in_l[i];
        for (int32_t i = 0; i < block_size_; ++i) sliding_r_[i] = sliding_r_[block_size_ + i];
        for (int32_t i = 0; i < block_size_; ++i) sliding_r_[block_size_ + i] = in_r[i];

        auto& x0l = x_hist_l_[hist_write_];
        auto& x0r = x_hist_r_[hist_write_];
        for (int32_t i = 0; i < fft_size; ++i) x0l[i] = std::complex<float>(sliding_l_[i], 0.0f);
        for (int32_t i = 0; i < fft_size; ++i) x0r[i] = std::complex<float>(sliding_r_[i], 0.0f);
        FftUtils::FftInplace(x0l.data(), fft_size, false);
        FftUtils::FftInplace(x0r.data(), fft_size, false);

        std::fill(y_l_.begin(), y_l_.end(), std::complex<float>(0.0f, 0.0f));
        std::fill(y_r_.begin(), y_r_.end(), std::complex<float>(0.0f, 0.0f));
        for (int32_t k = 0; k < p; ++k) {
            int32_t idx = hist_write_ - k;
            if (idx < 0) idx += p;
            const auto& XL = x_hist_l_[idx];
            const auto& XR = x_hist_r_[idx];
            Avx2Utils::ComplexMulAccumulateAVX2(y_l_.data(), XL.data(), spectrum_->LL().Partition(k).data(), static_cast<size_t>(fft_size));
            Avx2Utils::ComplexMulAccumulateAVX2(y_l_.data(), XR.data(), spectrum_->RL().Partition(k).data(), static_cast<size_t>(fft_size));
            Avx2Utils::ComplexMulAccumulateAVX2(y_r_.data(), XL.data(), spectrum_->LR().Partition(k).data(), static_cast<size_t>(fft_size));
            Avx2Utils::ComplexMulAccumulateAVX2(y_r_.data(), XR.data(), spectrum_->RR().Partition(k).data(), static_cast<size_t>(fft_size));
        }

        FftUtils::FftInplace(y_l_.data(), fft_size, true);
        FftUtils::FftInplace(y_r_.data(), fft_size, true);
        for (int32_t i = 0; i < block_size_; ++i) {
            out_l[i] = y_l_[block_size_ + i].real();
            out_r[i] = y_r_[block_size_ + i].real();
        }

        hist_write_ = (hist_write_ + 1) % p;
    }

    void Clear() {
        for (auto& v : x_hist_l_) std::fill(v.begin(), v.end(), std::complex<float>(0.0f, 0.0f));
        for (auto& v : x_hist_r_) std::fill(v.begin(), v.end(), std::complex<float>(0.0f, 0.0f));
        std::fill(sliding_l_.begin(), sliding_l_.end(), 0.0f);
        std::fill(sliding_r_.begin(), sliding_r_.end(), 0.0f);
        hist_write_ = 0;
    }

  private:
    std::shared_ptr<const TrueStereoSpectrum> spectrum_;
    int32_t block_size_ = 0;
    int32_t hist_write_ = 0;
    bool ready_ = false;

    std::vector<std::vector<std::complex<float>>> x_hist_l_;
    std::vector<std::vector<std::complex<float>>> x_hist_r_;
    std::vector<std::complex<float>> y_l_;
    std::vector<std::complex<float>> y_r_;
    std::vector<float> sliding_l_;
    std::vector<float> sliding_r_;
};

class TrueStereoStreamingConvolver {
  public:
    void Init(std::shared_ptr<const TrueStereoSpectrum> spectrum) {
        conv_.Init(spectrum);
        ready_ = conv_.IsReady();
        if (!ready_) return;
        block_size_ = conv_.BlockSize();
        in_l_.assign(block_size_, 0.0f);
        in_r_.assign(block_size_, 0.0f);
        in_fill_ = 0;
        out_l_.assign(block_size_, 0.0f);
        out_r_.assign(block_size_, 0.0f);
        out_read_ = 0;
    }

    bool IsReady() const { return ready_; }

    void Process(const float* in_l, const float* in_r, float* out_l, float* out_r, int32_t n) {
        if (!ready_) {
            std::fill(out_l, out_l + n, 0.0f);
            std::fill(out_r, out_r + n, 0.0f);
            return;
        }
        int32_t i = 0;
        while (i < n) {
            int32_t space = block_size_ - in_fill_;
            int32_t take = (std::min)(space, n - i);
            std::copy(in_l + i, in_l + i + take, in_l_.begin() + in_fill_);
            std::copy(in_r + i, in_r + i + take, in_r_.begin() + in_fill_);
            in_fill_ += take;
            i += take;
            if (in_fill_ == block_size_) {
                std::vector<float> bl(block_size_), br(block_size_);
                conv_.Process(in_l_.data(), in_r_.data(), bl.data(), br.data());
                out_l_.insert(out_l_.end(), bl.begin(), bl.end());
                out_r_.insert(out_r_.end(), br.begin(), br.end());
                in_fill_ = 0;
            }
        }

        int32_t avail = static_cast<int32_t>(out_l_.size()) - out_read_;
        int32_t give = (std::min)(avail, n);
        for (int32_t k = 0; k < give; ++k) {
            out_l[k] = out_l_[out_read_ + k];
            out_r[k] = out_r_[out_read_ + k];
        }
        for (int32_t k = give; k < n; ++k) {
            out_l[k] = 0.0f;
            out_r[k] = 0.0f;
        }
        out_read_ += give;

        if (out_read_ > block_size_ * 4) {
            out_l_.erase(out_l_.begin(), out_l_.begin() + out_read_);
            out_r_.erase(out_r_.begin(), out_r_.begin() + out_read_);
            out_read_ = 0;
        }
    }

    void Clear() {
        conv_.Clear();
        std::fill(in_l_.begin(), in_l_.end(), 0.0f);
        std::fill(in_r_.begin(), in_r_.end(), 0.0f);
        in_fill_ = 0;
        out_l_.assign(block_size_, 0.0f);
        out_r_.assign(block_size_, 0.0f);
        out_read_ = 0;
    }

  private:
    TrueStereoConvolver conv_;
    int32_t block_size_ = 0;
    int32_t in_fill_ = 0;
    bool ready_ = false;
    std::vector<float> in_l_;
    std::vector<float> in_r_;
    std::vector<float> out_l_;
    std::vector<float> out_r_;
    int32_t out_read_ = 0;
};

} // namespace ConvReverb