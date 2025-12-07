#pragma once
#include <array>
#include <mutex>

struct ChainData
{
    static constexpr int32_t MAX_PER_ID = 64;

    std::array<int64_t, MAX_PER_ID> effect_id = []() {
        std::array<int64_t, MAX_PER_ID> arr;
        arr.fill(-1);
        return arr;
        }();

    std::array<float, MAX_PER_ID> level = { 0 };
    std::array<uint32_t, MAX_PER_ID> update_count = { 0 };
};

class ChainManager
{
public:
    static const int32_t MAX_ID = 64;
    static constexpr int32_t MAX_PER_ID = ChainData::MAX_PER_ID;
    static inline std::array<ChainData, MAX_ID> chains;
    static inline std::array<std::mutex, MAX_ID> chains_mutexes;
};