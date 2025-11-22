#include "PluginManager.h"
#include "StringUtils.h"

PluginManager& PluginManager::GetInstance() {
    static PluginManager instance;
    return instance;
}

void PluginManager::CleanupResources() {
    {
        std::lock_guard<std::mutex> lock(m_states_mutex);
        m_hosts.clear();
        m_plugin_state_database.clear();
        m_pending_reinitialization.clear();
    }
    {
        std::lock_guard<std::mutex> lock(m_instance_ownership_mutex);
        m_instance_id_to_effect_id_map.clear();
    }
    {
        std::lock_guard<std::mutex> lock(m_last_audio_state_mutex);
        m_last_audio_states.clear();
    }
}

std::string PluginManager::PrepareProjectState(const std::set<std::string>& active_ids) {
    std::scoped_lock lock(m_states_mutex, m_instance_ownership_mutex);
    for (const auto& instance_id : active_ids) {
        auto ownership_it = m_instance_id_to_effect_id_map.find(instance_id);
        if (ownership_it != m_instance_id_to_effect_id_map.end()) {
            int64_t effect_id = ownership_it->second;
            auto host_it = m_hosts.find(effect_id);
            if (host_it != m_hosts.end() && host_it->second) {
                std::string live_state = host_it->second->GetState();
                if (!live_state.empty()) {
                    m_plugin_state_database[instance_id] = live_state;
                }
            }
        }
    }
    for (auto it = m_plugin_state_database.begin(); it != m_plugin_state_database.end(); ) {
        if (active_ids.find(it->first) == active_ids.end()) {
            it = m_plugin_state_database.erase(it);
        } else {
            ++it;
        }
    }
    for (auto it = m_instance_id_to_effect_id_map.begin(); it != m_instance_id_to_effect_id_map.end(); ) {
        if (active_ids.find(it->first) == active_ids.end()) {
            it = m_instance_id_to_effect_id_map.erase(it);
        } else {
            ++it;
        }
    }
    std::set<int64_t> active_effect_ids;
    for (const auto& pair : m_instance_id_to_effect_id_map) {
        active_effect_ids.insert(pair.second);
    }
    for (auto it = m_hosts.begin(); it != m_hosts.end(); ) {
        if (active_effect_ids.find(it->first) == active_effect_ids.end()) {
            it = m_hosts.erase(it);
        } else {
            ++it;
        }
    }
    std::string all_data_str;
    for (const auto& [id, state] : m_plugin_state_database) {
        all_data_str += id + ":" + state;

        if (m_param_mappings.count(id)) {
            all_data_str += "|";
            const auto& mapping = m_param_mappings[id];
            for (int i = 0; i < 4; ++i) {
                if (mapping[i] != -1) {
                    all_data_str += std::to_string(i) + "=" + std::to_string(mapping[i]) + ",";
                }
            }
        }
        all_data_str += ";";
    }
    return all_data_str;
}

void PluginManager::LoadProjectState(const std::string& data) {
    std::lock_guard<std::mutex> lock(m_states_mutex);
    m_plugin_state_database.clear();
    m_param_mappings.clear();

    std::string_view sv(data);
    size_t start = 0;
    while (start < sv.length()) {
        size_t end = sv.find(';', start);
        if (end == std::string_view::npos) break;

        std::string_view entry = sv.substr(start, end - start);

        size_t pipe_pos = entry.find('|');

        std::string_view id_state_part = (pipe_pos == std::string_view::npos) ? entry : entry.substr(0, pipe_pos);

        size_t colon_pos = id_state_part.find(':');
        if (colon_pos != std::string_view::npos) {
            std::string key(id_state_part.substr(0, colon_pos));
            std::string val(id_state_part.substr(colon_pos + 1));
            m_plugin_state_database[key] = val;

            if (pipe_pos != std::string_view::npos) {
                std::string_view map_part = entry.substr(pipe_pos + 1);
                ParamMapping mapping = { -1, -1, -1, -1 };

                size_t map_start = 0;
                while (map_start < map_part.length()) {
                    size_t map_end = map_part.find(',', map_start);
                    if (map_end == std::string_view::npos) map_end = map_part.length();

                    std::string_view kv = map_part.substr(map_start, map_end - map_start);
                    size_t eq = kv.find('=');
                    if (eq != std::string_view::npos) {
                        int idx = std::atoi(std::string(kv.substr(0, eq)).c_str());
                        int32_t pid = std::atoi(std::string(kv.substr(eq + 1)).c_str());
                        if (idx >= 0 && idx < 4) mapping[idx] = pid;
                    }
                    if (map_end == map_part.length()) break;
                    map_start = map_end + 1;
                }
                m_param_mappings[key] = mapping;
            }
        }
        start = end + 1;
    }
}

void PluginManager::RegisterOrUpdateInstance(std::string& instance_id, int64_t effect_id, bool& is_copy) {
    std::lock_guard<std::mutex> lock(m_instance_ownership_mutex);
    auto it = m_instance_id_to_effect_id_map.find(instance_id);

    is_copy = false;

    if (it == m_instance_id_to_effect_id_map.end()) {
        m_instance_id_to_effect_id_map[instance_id] = effect_id;
    }
    else if (it->second != effect_id) {
        is_copy = true;
        std::string old_instance_id = instance_id;
        std::string new_instance_id = StringUtils::GenerateUUID();

        {
            std::lock_guard<std::mutex> state_lock(m_states_mutex);
            if (m_plugin_state_database.count(old_instance_id)) {
                m_plugin_state_database[new_instance_id] = m_plugin_state_database[old_instance_id];
            }
        }

        m_instance_id_to_effect_id_map[new_instance_id] = effect_id;
        instance_id = new_instance_id;
    }
}

std::shared_ptr<IAudioPluginHost> PluginManager::GetHost(int64_t effect_id) {
    std::lock_guard<std::mutex> lock(m_states_mutex);
    auto it = m_hosts.find(effect_id);
    if (it != m_hosts.end()) {
        return it->second;
    }
    return nullptr;
}

void PluginManager::SetHost(int64_t effect_id, std::shared_ptr<IAudioPluginHost> host) {
    std::lock_guard<std::mutex> lock(m_states_mutex);
    if (host) {
        m_hosts[effect_id] = host;
    } else {
        m_hosts.erase(effect_id);
    }
}

void PluginManager::RemoveHost(int64_t effect_id) {
    std::lock_guard<std::mutex> lock(m_states_mutex);
    m_hosts.erase(effect_id);
}

std::string PluginManager::GetSavedState(const std::string& instance_id) {
    std::lock_guard<std::mutex> lock(m_states_mutex);
    auto it = m_plugin_state_database.find(instance_id);
    if (it != m_plugin_state_database.end()) {
        return it->second;
    }
    return "";
}

void PluginManager::SaveState(const std::string& instance_id, const std::string& state) {
    std::lock_guard<std::mutex> lock(m_states_mutex);
    m_plugin_state_database[instance_id] = state;
}

bool PluginManager::IsPendingReinitialization(int64_t effect_id) {
    std::lock_guard<std::mutex> lock(m_states_mutex);
    if (m_pending_reinitialization.count(effect_id)) {
        return m_pending_reinitialization[effect_id];
    }
    return false;
}

void PluginManager::SetPendingReinitialization(int64_t effect_id, bool pending) {
    std::lock_guard<std::mutex> lock(m_states_mutex);
    m_pending_reinitialization[effect_id] = pending;
}

bool PluginManager::ShouldReset(int64_t effect_id, int64_t current_sample_index, int current_sample_num) {
    std::lock_guard<std::mutex> lock(m_last_audio_state_mutex);
    auto it = m_last_audio_states.find(effect_id);
    bool needs_reset = false;
    if (it != m_last_audio_states.end()) {
        if (current_sample_index != it->second.sample_index + it->second.sample_num) {
            needs_reset = true;
        }
    } else {
        needs_reset = true;
    }
    return needs_reset;
}

void PluginManager::UpdateLastAudioState(int64_t effect_id, int64_t current_sample_index, int current_sample_num) {
    std::lock_guard<std::mutex> lock(m_last_audio_state_mutex);
    m_last_audio_states[effect_id] = { current_sample_index, current_sample_num };
}

void PluginManager::UpdateMapping(const std::string& instance_id, int sliderInfoIndex, int32_t vstParamID) {
    std::lock_guard<std::mutex> lock(m_states_mutex);
    if (m_param_mappings.find(instance_id) == m_param_mappings.end()) {
        m_param_mappings[instance_id] = { -1, -1, -1, -1 };
    }
    if (sliderInfoIndex >= 0 && sliderInfoIndex < 4) {
        m_param_mappings[instance_id][sliderInfoIndex] = vstParamID;
    }
}

int32_t PluginManager::GetMappedParamID(const std::string& instance_id, int sliderInfoIndex) {
    std::lock_guard<std::mutex> lock(m_states_mutex);
    if (m_param_mappings.count(instance_id)) {
        if (sliderInfoIndex >= 0 && sliderInfoIndex < 4) {
            return m_param_mappings[instance_id][sliderInfoIndex];
        }
    }
    return -1;
}

void PluginManager::ClearMapping(const std::string& instance_id) {
    std::lock_guard<std::mutex> lock(m_states_mutex);
    if (m_param_mappings.count(instance_id)) {
        m_param_mappings[instance_id] = { -1, -1, -1, -1 };
    }
}