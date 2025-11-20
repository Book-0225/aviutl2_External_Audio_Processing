#pragma once

#include "Eap2Common.h"
#include "IAudioPluginHost.h"
#include <map>
#include <set>
#include <mutex>
#include <string>
#include <memory>

class PluginManager {
public:
    static PluginManager& GetInstance();

    void CleanupResources();
    std::string PrepareProjectState(const std::set<std::string>& active_ids);
    void LoadProjectState(const std::string& data);
    void RegisterOrUpdateInstance(std::string& instance_id, int64_t effect_id, bool& is_copy);
    std::shared_ptr<IAudioPluginHost> GetHost(int64_t effect_id);
    void SetHost(int64_t effect_id, std::shared_ptr<IAudioPluginHost> host);
    void RemoveHost(int64_t effect_id);
    std::string GetSavedState(const std::string& instance_id);
    void SaveState(const std::string& instance_id, const std::string& state);
    bool IsPendingReinitialization(int64_t effect_id);
    void SetPendingReinitialization(int64_t effect_id, bool pending);
    bool ShouldReset(int64_t effect_id, int64_t current_sample_index, int current_sample_num);
    void UpdateLastAudioState(int64_t effect_id, int64_t current_sample_index, int current_sample_num);

private:
    PluginManager() = default;
    ~PluginManager() = default;
    PluginManager(const PluginManager&) = delete;
    PluginManager& operator=(const PluginManager&) = delete;

    struct LastAudioState {
        int64_t sample_index;
        int sample_num;
    };

    std::mutex m_states_mutex;
    std::map<int64_t, std::shared_ptr<IAudioPluginHost>> m_hosts;
    std::map<std::string, std::string> m_plugin_state_database;
    std::map<int64_t, bool> m_pending_reinitialization;

    std::mutex m_last_audio_state_mutex;
    std::map<int64_t, LastAudioState> m_last_audio_states;

    std::mutex m_instance_ownership_mutex;
    std::map<std::string, int64_t> m_instance_id_to_effect_id_map;
};