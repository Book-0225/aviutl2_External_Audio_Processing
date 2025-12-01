#include "Eap2Common.h"
#include "NotesManager.h"
#include "Avx2Utils.h"

#define TOOL_NAME_MEDIA L"Notes Send (Media)"

FILTER_ITEM_TRACK notes_send_id(L"ID", 1.0, 1.0, NotesManager::MAX_ID, 1.0);
FILTER_ITEM_TRACK notes_send_note(L"Note", 60.0, 0.0, 127.0, 1.0);

void* filter_items_notes_send[] = {
    &notes_send_id,
	&notes_send_note,
    nullptr
};

bool func_proc_audio_notes_send(FILTER_PROC_AUDIO* audio) {
	int32_t id_idx = static_cast<int32_t>(notes_send_id.value) - 1;
	int32_t note_num = static_cast<uint8_t>(notes_send_note.value);
    {
        std::lock_guard<std::mutex> lock(NotesManager::notes_mutexes[id_idx]);
        auto& note = NotesManager::notes[id_idx];
        const auto& ids = note.effect_id;
        auto it = std::find(ids.begin(), ids.end(), audio->object->effect_id);
        if (it != ids.end())
        {
            auto data_idx = std::distance(ids.begin(), it);
            note.number[data_idx] = note_num;
            note.update_count[data_idx]++;
        }
        else {
            auto free_it = std::find(ids.begin(), ids.end(), -1);
            if (free_it != ids.end())
            {
                auto free_idx = std::distance(ids.begin(), free_it);
                note.effect_id[free_idx] = audio->object->effect_id;
                note.number[free_idx] = note_num;
                note.update_count[free_idx] = 1;
            }
        }
    }
    return true;
}

FILTER_PLUGIN_TABLE filter_plugin_table_notes_send_media = {
    FILTER_PLUGIN_TABLE::FLAG_AUDIO | FILTER_PLUGIN_TABLE::FLAG_INPUT,
    GEN_TOOL_NAME(TOOL_NAME_MEDIA),
    label,
    GEN_FILTER_INFO(TOOL_NAME_MEDIA),
    filter_items_notes_send,
    nullptr,
    func_proc_audio_notes_send
};