#pragma once
#include <array>
#include <mutex>

struct NotesData
{
    static constexpr int32_t MAX_PER_ID = 64;

    std::array<int64_t, MAX_PER_ID> effect_id = []() {
        std::array<int64_t, MAX_PER_ID> arr;
        arr.fill(-1);
        return arr;
        }();

    std::array<uint8_t, MAX_PER_ID> number = { 0 };
    std::array<uint32_t, MAX_PER_ID> update_count = { 0 };
};

class NotesManager
{
public:
    static const int32_t MAX_ID = 64;
    static constexpr int32_t MAX_PER_ID = NotesData::MAX_PER_ID;
    static inline std::array<NotesData, MAX_ID> notes;
    static inline std::array<std::mutex, MAX_ID> notes_mutexes;
};