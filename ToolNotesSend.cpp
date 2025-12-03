#include "Eap2Common.h"
#include "NotesManager.h"
#include "Avx2Utils.h"

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