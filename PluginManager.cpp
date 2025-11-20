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
        all_data_str += id + ":" + state + ";";
    }
    return all_data_str;
}

void PluginManager::LoadProjectState(const std::string& data) {
    std::lock_guard<std::mutex> lock(m_states_mutex);
    m_plugin_state_database.clear();

    std::string_view sv(data);
    size_t start = 0;
    while (start < sv.length()) {
        size_t end = sv.find(';', start);
        if (end == std::string_view::npos) break;

        std::string_view pair_sv = sv.substr(start, end - start);
        size_t colon_pos = pair_sv.find(':');

        if (colon_pos != std::string_view::npos) {
            std::string key(pair_sv.substr(0, colon_pos));
            std::string val(pair_sv.substr(colon_pos + 1));
            m_plugin_state_database[key] = val;
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