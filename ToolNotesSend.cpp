#include "Eap2Common.h"
#include "NotesManager.h"
#include "Avx2Utils.h"
#include "PluginManager.h"
#include "StringUtils.h"
#include <string>
#include <map>
#include <vector>
#include <mutex>
#include <algorithm>

#define TOOL_NAME_MEDIA L"Notes Send (Media)"

FILTER_ITEM_TRACK notes_send_id(L"ID", 1.0, 1.0, NotesManager::MAX_ID, 1.0);
FILTER_ITEM_SELECT::ITEM notes_list[] = {
    // Octave 0
    { L"C0", 12 }, { L"C#0", 13 }, { L"D0", 14 }, { L"D#0", 15 },
    { L"E0", 16 }, { L"F0", 17 }, { L"F#0", 18 }, { L"G0", 19 },
    { L"G#0", 20 }, { L"A0", 21 }, { L"A#0", 22 }, { L"B0", 23 },
    // Octave 1
    { L"C1", 24 }, { L"C#1", 25 }, { L"D1", 26 }, { L"D#1", 27 },
    { L"E1", 28 }, { L"F1", 29 }, { L"F#1", 30 }, { L"G1", 31 },
    { L"G#1", 32 }, { L"A1", 33 }, { L"A#1", 34 }, { L"B1", 35 },
    // Octave 2
    { L"C2", 36 }, { L"C#2", 37 }, { L"D2", 38 }, { L"D#2", 39 },
    { L"E2", 40 }, { L"F2", 41 }, { L"F#2", 42 }, { L"G2", 43 },
    { L"G#2", 44 }, { L"A2", 45 }, { L"A#2", 46 }, { L"B2", 47 },
    // Octave 3
    { L"C3", 48 }, { L"C#3", 49 }, { L"D3", 50 }, { L"D#3", 51 },
    { L"E3", 52 }, { L"F3", 53 }, { L"F#3", 54 }, { L"G3", 55 },
    { L"G#3", 56 }, { L"A3", 57 }, { L"A#3", 58 }, { L"B3", 59 },
    // Octave 4 (Middle C = 60)
    { L"C4", 60 }, { L"C#4", 61 }, { L"D4", 62 }, { L"D#4", 63 },
    { L"E4", 64 }, { L"F4", 65 }, { L"F#4", 66 }, { L"G4", 67 },
    { L"G#4", 68 }, { L"A4", 69 }, { L"A#4", 70 }, { L"B4", 71 },
    // Octave 5
    { L"C5", 72 }, { L"C#5", 73 }, { L"D5", 74 }, { L"D#5", 75 },
    { L"E5", 76 }, { L"F5", 77 }, { L"F#5", 78 }, { L"G5", 79 },
    { L"G#5", 80 }, { L"A5", 81 }, { L"A#5", 82 }, { L"B5", 83 },
    // Octave 6
    { L"C6", 84 }, { L"C#6", 85 }, { L"D6", 86 }, { L"D#6", 87 },
    { L"E6", 88 }, { L"F6", 89 }, { L"F#6", 90 }, { L"G6", 91 },
    { L"G#6", 92 }, { L"A6", 93 }, { L"A#6", 94 }, { L"B6", 95 },
    // Octave 7
    { L"C7", 96 }, { L"C#7", 97 }, { L"D7", 98 }, { L"D#7", 99 },
    { L"E7", 100 }, { L"F7", 101 }, { L"F#7", 102 }, { L"G7", 103 },
    { L"G#7", 104 }, { L"A7", 105 }, { L"A#7", 106 }, { L"B7", 107 },
    // Octave 8
    { L"C8", 108 }, { L"C#8", 109 }, { L"D8", 110 }, { L"D#8", 111 },
    { L"E8", 112 }, { L"F8", 113 }, { L"F#8", 114 }, { L"G8", 115 },
    { L"G#8", 116 }, { L"A8", 117 }, { L"A#8", 118 }, { L"B8", 119 },
    // Octave 9
    { L"C9", 120 }, { L"C#9", 121 }, { L"D9", 122 }, { L"D#9", 123 },
    { L"E9", 124 }, { L"F9", 125 }, { L"F#9", 126 }, { L"G9", 127 },

    { nullptr } // Terminator
};
FILTER_ITEM_SELECT notes_send_note(L"Note", 60, notes_list);

struct NotesSendData {
    char uuid[40] = { 0 };
    int32_t last_note = -1;
    int32_t last_id = -1;
};
FILTER_ITEM_DATA<NotesSendData> notes_send_data(L"NOTES_SEND_DATA");

void* filter_items_notes_send[] = {
    &notes_send_id,
    &notes_send_note,
    &notes_send_data,
    nullptr
};

std::wstring GetNoteName(int32_t note) {
    static const wchar_t* noteNames[] = { L"C", L"C#", L"D", L"D#", L"E", L"F", L"F#", L"G", L"G#", L"A", L"A#", L"B" };
    int32_t octave = (note / 12) - 1;
    int32_t noteIdx = note % 12;
    if (noteIdx < 0) noteIdx += 12;
    return std::wstring(noteNames[noteIdx]) + std::to_wstring(octave);
}

std::wstring GetNotesSendParamsString(int32_t note, int32_t id) {
    return L" [" + GetNoteName(note) + L"] [ID:" + std::to_wstring(id) + L"]";
}

struct RenameParam {
    std::wstring newName;
    std::wstring oldNameCandidate;
    std::wstring defaultName;
    std::string id;
};

static void func_proc_check_and_rename(void* param, EDIT_SECTION* edit) {
    RenameParam* p = (RenameParam*)param;
    OBJECT_HANDLE obj = nullptr;
    int32_t max_layer = edit->info->layer_max;
    for (int32_t layer = 0; layer <= max_layer; ++layer) {
        OBJECT_HANDLE obj_temp = edit->find_object(layer, 0);
        while (!obj) {
            int32_t effect_count = edit->count_object_effect(obj_temp, GEN_TOOL_NAME(TOOL_NAME_MEDIA));
            for (int32_t i = 0; i < effect_count; ++i) {
                std::wstring indexed_filter_name = std::wstring(GEN_TOOL_NAME(TOOL_NAME_MEDIA));
                if (i > 0) indexed_filter_name += L":" + std::to_wstring(i);
                LPCSTR hex_encoded_id_str = edit->get_object_item_value(obj_temp, indexed_filter_name.c_str(), notes_send_data.name);
                if (hex_encoded_id_str && hex_encoded_id_str[0] != '\0') {
                    std::string raw_data = StringUtils::HexToString(hex_encoded_id_str);
                    if (raw_data.size() >= sizeof(NotesSendData::uuid)) {
                        const NotesSendData* data = reinterpret_cast<const NotesSendData*>(raw_data.data());
                        if (p->id == data->uuid) {
                            obj = obj_temp;
                            break;
                        }
                    }
                }
            }
            int32_t end_frame = edit->get_object_layer_frame(obj_temp).end;
            obj_temp = edit->find_object(layer, end_frame + 1);
            if (!obj_temp) break;
        }
    }
    if (!obj) return;
    LPCWSTR currentNamePtr = edit->get_object_name(obj);
    std::wstring currentName = currentNamePtr ? currentNamePtr : L"";
    bool doRename = false;
    if (currentName.empty()) doRename = true;
    else if (currentName == p->defaultName) doRename = true;
    else if (!p->oldNameCandidate.empty() && currentName == p->oldNameCandidate) doRename = true;
    if (doRename) edit->set_object_name(obj, p->newName.c_str());
}

bool func_proc_audio_notes_send(FILTER_PROC_AUDIO* audio) {
    int32_t id_idx = static_cast<int32_t>(notes_send_id.value) - 1;
    int32_t note_num = static_cast<uint8_t>(notes_send_note.value);
    int32_t display_id = id_idx + 1;
    std::string instance_id;
    if (notes_send_data.value->uuid[0] != '\0') {
        instance_id = notes_send_data.value->uuid;
    }
    else {
        instance_id = StringUtils::GenerateUUID();
        strcpy_s(notes_send_data.value->uuid, sizeof(notes_send_data.value->uuid), instance_id.c_str());
    }
    if (!instance_id.empty()) {
        int64_t effect_id = audio->object->effect_id;
        bool is_copy = false;
        PluginManager::GetInstance().RegisterOrUpdateInstance(instance_id, effect_id, is_copy);
        if (is_copy) {
            strcpy_s(notes_send_data.value->uuid, sizeof(notes_send_data.value->uuid), instance_id.c_str());
            notes_send_data.value->last_note = notes_send_data.default_value.last_note;
            notes_send_data.value->last_id = notes_send_data.default_value.last_id;
        }
    }
    if (notes_send_data.value->last_note != note_num || notes_send_data.value->last_id != display_id) {
        int32_t old_note = (notes_send_data.value->last_note == -1) ? note_num : notes_send_data.value->last_note;
        int32_t old_id = (notes_send_data.value->last_id == -1) ? display_id : notes_send_data.value->last_id;
        g_main_thread_tasks.push_back([instance_id, note_num, display_id, old_note, old_id] {
            if (g_edit_handle) {
                RenameParam rp;
                rp.id = instance_id;
                rp.defaultName = GEN_TOOL_NAME(TOOL_NAME_MEDIA);
                rp.newName = std::wstring(rp.defaultName) + GetNotesSendParamsString(note_num, display_id);
                rp.oldNameCandidate = std::wstring(rp.defaultName) + GetNotesSendParamsString(old_note, old_id);
                g_edit_handle->call_edit_section_param(&rp, func_proc_check_and_rename);
            }
            });

        notes_send_data.value->last_note = note_num;
        notes_send_data.value->last_id = display_id;
    }
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