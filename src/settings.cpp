#define NOMINMAX
#include "settings.h"
#include <fstream>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <algorithm>
#include <random>
#include <ctime>
#include "shared.h"
#include "Sounds.h"
#include "TextToSpeech.h" 

// ImGui vector serialization support for nlohmann::json
namespace nlohmann {
    template<>
    struct adl_serializer<ImVec4> {
        static void to_json(json& j, const ImVec4& v) {
            j = json{ {"x", v.x}, {"y", v.y}, {"z", v.z}, {"w", v.w} };
        }

        static void from_json(const json& j, ImVec4& v) {
            if (j.is_object()) {
                v.x = j.contains("x") ? j["x"].get<float>() : 0.0f;
                v.y = j.contains("y") ? j["y"].get<float>() : 0.0f;
                v.z = j.contains("z") ? j["z"].get<float>() : 0.0f;
                v.w = j.contains("w") ? j["w"].get<float>() : 0.0f;
            }
        }
    };

    template<>
    struct adl_serializer<ImVec2> {
        static void to_json(json& j, const ImVec2& v) {
            j = json{ {"x", v.x}, {"y", v.y} };
        }

        static void from_json(const json& j, ImVec2& v) {
            if (j.is_object()) {
                v.x = j.contains("x") ? j["x"].get<float>() : 0.0f;
                v.y = j.contains("y") ? j["y"].get<float>() : 0.0f;
            }
        }
    };
}


std::mutex Settings::Mutex;
json Settings::SettingsData = json::object();
ImVec2 Settings::windowPosition(100, 100);
ImVec2 Settings::windowSize(300, 400);
bool Settings::showTitle = true;
bool Settings::allowResize = true;
WindowColors Settings::colors;
std::vector<TimerData> Settings::timers;
std::unordered_set<std::string> Settings::usedIds;
SoundSettings Settings::sounds;
std::mutex Settings::SaveMutex;
bool Settings::saveScheduled = false;
std::chrono::steady_clock::time_point Settings::lastSaveRequest = std::chrono::steady_clock::now();
const std::chrono::milliseconds Settings::saveCooldown(500);
WebSocketSettings Settings::websocket;
bool Settings::isInitializing = false;

// Implementation of TimerData methods
TimerData::TimerData(const std::string& name, float duration)
    : name(name)
    , duration(duration)
    , endSound(SoundID(themes_chime_success))
    , warningTime(30.0f)
    , warningSound(SoundID(themes_chime_info))
    , useWarning(false)
    , isRoomTimer(false)
    , roomId("")
{
    id = generateUniqueId("timer_");
}

std::string TimerData::generateUniqueId(const std::string& prefix) {
    static std::mutex idMutex;
    std::lock_guard<std::mutex> lock(idMutex);

    // Get current time with microsecond precision
    auto now = std::chrono::system_clock::now();
    auto micros = std::chrono::duration_cast<std::chrono::microseconds>(
        now.time_since_epoch()
        );

    // Convert to hex string
    std::stringstream ss;
    ss << prefix << std::hex << micros.count();
    return ss.str();
}

json TimerData::toJson() const {
    json j;
    j["name"] = name;
    j["id"] = id;
    j["duration"] = duration;
    j["endSound"] = endSound.ToString();
    j["warningTime"] = warningTime;
    j["warningSound"] = warningSound.ToString();
    j["useWarning"] = useWarning;
    j["isRoomTimer"] = isRoomTimer;
    j["roomId"] = roomId;
    return j;
}

TimerData TimerData::fromJson(const json& j) {
    TimerData timer;

    // Using at() with default values to safely extract properties
    timer.name = j.contains("name") ? j["name"].get<std::string>() : "";
    timer.id = j.contains("id") ? j["id"].get<std::string>() : generateUniqueId("timer_");
    timer.duration = j.contains("duration") ? j["duration"].get<float>() : 0.0f;

    // Deserialize endSound
    std::string endSoundStr = j.contains("endSound") ? j["endSound"].get<std::string>() : "";
    if (!endSoundStr.empty()) {
        if (endSoundStr.find("res:") == 0 || endSoundStr.find("file:") == 0) {
            timer.endSound = SoundID::FromString(endSoundStr);
        }
        else {
            try {
                int resId = std::stoi(endSoundStr);
                timer.endSound = SoundID(resId);
            }
            catch (...) {
                timer.endSound = SoundID(themes_chime_success);
            }
        }
    }
    else {
        timer.endSound = SoundID(themes_chime_success);
    }

    // Deserialize warningSound and warningTime
    timer.warningTime = j.contains("warningTime") ? j["warningTime"].get<float>() : 30.0f;
    std::string warningSoundStr = j.contains("warningSound") ? j["warningSound"].get<std::string>() : "";
    if (!warningSoundStr.empty()) {
        if (warningSoundStr.find("res:") == 0 || warningSoundStr.find("file:") == 0) {
            timer.warningSound = SoundID::FromString(warningSoundStr);
        }
        else {
            try {
                int resId = std::stoi(warningSoundStr);
                timer.warningSound = SoundID(resId);
            }
            catch (...) {
                timer.warningSound = SoundID(themes_chime_info);
            }
        }
    }
    else {
        timer.warningSound = SoundID(themes_chime_info);
    }

    timer.useWarning = j.contains("useWarning") ? j["useWarning"].get<bool>() : false;

    // Read the new fields
    timer.isRoomTimer = j.contains("isRoomTimer") ? j["isRoomTimer"].get<bool>() : false;
    timer.roomId = j.contains("roomId") ? j["roomId"].get<std::string>() : "";

    return timer;
}

void Settings::Load(const std::string& path) {
    std::lock_guard<std::mutex> lock(Mutex);

    try {
        std::ifstream file(path);
        if (!file.is_open()) {
            InitializeDefaults();
            return;
        }

        SettingsData = json::parse(file);

        // Clear existing state
        timers.clear();
        usedIds.clear();

        // Load window settings
        if (SettingsData.contains("window")) {
            const auto& window = SettingsData["window"];
            windowPosition.x = window.contains("positionX") ? window["positionX"].get<float>() : 100.0f;
            windowPosition.y = window.contains("positionY") ? window["positionY"].get<float>() : 100.0f;
            windowSize.x = window.contains("sizeX") ? window["sizeX"].get<float>() : 300.0f;
            windowSize.y = window.contains("sizeY") ? window["sizeY"].get<float>() : 400.0f;
            showTitle = window.contains("showTitle") ? window["showTitle"].get<bool>() : true;
            allowResize = window.contains("allowResize") ? window["allowResize"].get<bool>() : true;
        }

        // Load colors
        if (SettingsData.contains("colors")) {
            const auto& colorsJson = SettingsData["colors"];
            if (colorsJson.contains("background")) colors.background = colorsJson["background"];
            if (colorsJson.contains("text")) colors.text = colorsJson["text"];
            if (colorsJson.contains("timerActive")) colors.timerActive = colorsJson["timerActive"];
            if (colorsJson.contains("timerPaused")) colors.timerPaused = colorsJson["timerPaused"];
            if (colorsJson.contains("timerExpired")) colors.timerExpired = colorsJson["timerExpired"];
        }

        // Load sound settings
        if (SettingsData.contains("sounds")) {
            const auto& soundsJson = SettingsData["sounds"];
            sounds.masterVolume = soundsJson.contains("masterVolume") ? soundsJson["masterVolume"].get<float>() : 1.0f;
            sounds.audioDeviceIndex = soundsJson.contains("audioDeviceIndex") ? soundsJson["audioDeviceIndex"].get<int>() : -1;
            sounds.customSoundsDirectory = soundsJson.contains("customSoundsDirectory") ? soundsJson["customSoundsDirectory"].get<std::string>() : "";

            // Load sound volumes with new format
            if (soundsJson.contains("soundVolumes") && soundsJson["soundVolumes"].is_object()) {
                for (auto it = soundsJson["soundVolumes"].begin(); it != soundsJson["soundVolumes"].end(); ++it) {
                    try {
                        std::string soundIdStr = it.key();
                        float volume = it.value().get<float>();
                        sounds.soundVolumes[soundIdStr] = volume;
                    }
                    catch (...) {
                        // Skip entries that can't be parsed correctly
                    }
                }
            }

            // For backwards compatibility - load old resource sound volumes
            if (soundsJson.contains("resourceSoundVolumes") && soundsJson["resourceSoundVolumes"].is_object()) {
                for (auto it = soundsJson["resourceSoundVolumes"].begin(); it != soundsJson["resourceSoundVolumes"].end(); ++it) {
                    try {
                        int soundId = std::stoi(it.key());
                        float volume = it.value().get<float>();
                        // Convert to new format
                        SoundID id(soundId);
                        sounds.soundVolumes[id.ToString()] = volume;
                    }
                    catch (...) {
                        // Skip entries that can't be parsed correctly
                    }
                }
            }

            // Load sound pans with new format
            if (soundsJson.contains("soundPans") && soundsJson["soundPans"].is_object()) {
                for (auto it = soundsJson["soundPans"].begin(); it != soundsJson["soundPans"].end(); ++it) {
                    try {
                        std::string soundIdStr = it.key();
                        float pan = it.value().get<float>();
                        sounds.soundPans[soundIdStr] = pan;
                    }
                    catch (...) {
                        // Skip entries that can't be parsed correctly
                    }
                }
            }

            // For backwards compatibility - load old resource sound pans
            if (soundsJson.contains("resourceSoundPans") && soundsJson["resourceSoundPans"].is_object()) {
                for (auto it = soundsJson["resourceSoundPans"].begin(); it != soundsJson["resourceSoundPans"].end(); ++it) {
                    try {
                        int soundId = std::stoi(it.key());
                        float pan = it.value().get<float>();
                        // Convert to new format
                        SoundID id(soundId);
                        sounds.soundPans[id.ToString()] = pan;
                    }
                    catch (...) {
                        // Skip entries that can't be parsed correctly
                    }
                }
            }

            // Load recent sounds
            if (soundsJson.contains("recentSounds") && soundsJson["recentSounds"].is_array()) {
                for (const auto& sound : soundsJson["recentSounds"]) {
                    try {
                        std::string soundIdStr = sound.get<std::string>();
                        sounds.recentSounds.push_back(soundIdStr);
                    }
                    catch (...) {
                        // Skip invalid entries
                    }
                }
            }

            // TTS
            if (soundsJson.contains("ttsSounds") && soundsJson["ttsSounds"].is_array()) {
                for (const auto& ttsJson : soundsJson["ttsSounds"]) {
                    try {
                        std::string id = ttsJson.contains("id") ? ttsJson["id"].get<std::string>() : "";
                        std::string name = ttsJson.contains("name") ? ttsJson["name"].get<std::string>() : "";
                        float volume = ttsJson.contains("volume") ? ttsJson["volume"].get<float>() : 1.0f;
                        float pan = ttsJson.contains("pan") ? ttsJson["pan"].get<float>() : 0.0f;
                        sounds.ttsSounds.emplace_back(id, name, volume, pan);
                    }
                    catch (...) {
                        // Skip invalid entries
                    }
                }
            }

            // Update the sound engine with the loaded settings
            if (g_SoundEngine) {
                g_SoundEngine->SetMasterVolume(sounds.masterVolume);

                // Update individual sound volumes and pans
                for (const auto& [soundIdStr, volume] : sounds.soundVolumes) {
                    SoundID id = SoundID::FromString(soundIdStr);
                    g_SoundEngine->SetSoundVolume(id, volume);
                }

                for (const auto& [soundIdStr, pan] : sounds.soundPans) {
                    SoundID id = SoundID::FromString(soundIdStr);
                    g_SoundEngine->SetSoundPan(id, pan);
                }
            }
        }

        // Load WebSockets
        if (SettingsData.contains("websocket")) {
            const auto& websocketJson = SettingsData["websocket"];
            websocket.serverUrl = websocketJson.contains("serverUrl") ? websocketJson["serverUrl"].get<std::string>() : "wss://simple-timers-wss.onrender.com";
            websocket.autoConnect = websocketJson.contains("autoConnect") ? websocketJson["autoConnect"].get<bool>() : false;
            websocket.enabled = websocketJson.contains("enabled") ? websocketJson["enabled"].get<bool>() : false;
            websocket.pingInterval = websocketJson.contains("pingInterval") ? websocketJson["pingInterval"].get<int>() : 30000;
            websocket.autoReconnect = websocketJson.contains("autoReconnect") ? websocketJson["autoReconnect"].get<bool>() : true;
            websocket.reconnectInterval = websocketJson.contains("reconnectInterval") ? websocketJson["reconnectInterval"].get<int>() : 5000;
            websocket.maxReconnectAttempts = websocketJson.contains("maxReconnectAttempts") ? websocketJson["maxReconnectAttempts"].get<int>() : 5;
            websocket.logMessages = websocketJson.contains("logMessages") ? websocketJson["logMessages"].get<bool>() : true;
            websocket.maxLogEntries = websocketJson.contains("maxLogEntries") ? websocketJson["maxLogEntries"].get<int>() : 100;

            // Load client ID if exists
            if (websocketJson.contains("clientId")) {
                websocket.clientId = websocketJson["clientId"].get<std::string>();
            }
            else {
                // Generate a new client ID
                websocket.ensureClientId();
            }

            // Load TLS settings if they exist
            if (websocketJson.contains("tlsOptions")) {
                const auto& tlsJson = websocketJson["tlsOptions"];
                websocket.tlsOptions.verifyPeer = tlsJson.contains("verifyPeer") ? tlsJson["verifyPeer"].get<bool>() : true;
                websocket.tlsOptions.verifyHost = tlsJson.contains("verifyHost") ? tlsJson["verifyHost"].get<bool>() : true;
                websocket.tlsOptions.caFile = tlsJson.contains("caFile") ? tlsJson["caFile"].get<std::string>() : "";
                websocket.tlsOptions.caPath = tlsJson.contains("caPath") ? tlsJson["caPath"].get<std::string>() : "";
                websocket.tlsOptions.certFile = tlsJson.contains("certFile") ? tlsJson["certFile"].get<std::string>() : "";
                websocket.tlsOptions.keyFile = tlsJson.contains("keyFile") ? tlsJson["keyFile"].get<std::string>() : "";
                websocket.tlsOptions.enableServerCertAuth = tlsJson.contains("enableServerCertAuth") ? tlsJson["enableServerCertAuth"].get<bool>() : true;
            }
        }

        if (SettingsData.contains("websocket")) {
            const auto& websocketJson = SettingsData["websocket"];

            if (websocketJson.contains("currentRoomId")) {
                websocket.currentRoomId = websocketJson["currentRoomId"].get<std::string>();
            }

            // Load room subscriptions
            if (websocketJson.contains("roomSubscriptions") && websocketJson["roomSubscriptions"].is_object()) {
                for (auto it = websocketJson["roomSubscriptions"].begin(); it != websocketJson["roomSubscriptions"].end(); ++it) {
                    std::string roomId = it.key();
                    if (it.value().is_array()) {
                        for (const auto& timerId : it.value()) {
                            try {
                                websocket.subscribeToTimer(timerId.get<std::string>(), roomId);
                            }
                            catch (...) {
                                // Skip invalid entries
                            }
                        }
                    }
                }
            }
        }

        // Load timers
        if (SettingsData.contains("timers") && SettingsData["timers"].is_array()) {
            for (const auto& timerJson : SettingsData["timers"]) {
                TimerData timer = TimerData::fromJson(timerJson);

                // If ID already exists, generate a new one
                while (usedIds.find(timer.id) != usedIds.end()) {
                    timer.id = TimerData::generateUniqueId("timer_");
                }

                usedIds.insert(timer.id);
                timers.push_back(timer);
            }
        }
    }
    catch (...) {
        InitializeDefaults();
    }
}

void Settings::ScheduleSave(const std::string& path) {
    {
        // Lock so we can safely update shared variables.
        std::lock_guard<std::mutex> lock(SaveMutex);

        // Record the time of this request.
        lastSaveRequest = std::chrono::steady_clock::now();

        // If we already have a save thread in progress, let it handle the new request.
        // It will see the updated lastSaveRequest and continue waiting.
        if (saveScheduled) {
            return;
        }

        saveScheduled = true;
    }

    // Launch a detached background thread to wait until the system is "quiet" long enough.
    std::thread([path]() {
        while (true) {
            // Sleep for the cooldown period.
            std::this_thread::sleep_for(saveCooldown);

            {
                // Check if another change arrived recently.
                std::lock_guard<std::mutex> lock(SaveMutex);
                auto timeSinceLastRequest = std::chrono::steady_clock::now() - lastSaveRequest;

                // If we've gone a full cooldown with no new changes, we're safe to save.
                if (timeSinceLastRequest >= saveCooldown) {
                    break;
                }
            }
        }

        // Perform the actual disk write once we know no new changes have arrived recently.
        Save(path);

        // Mark that we're finished, so the next call to ScheduleSave can start a new thread if needed.
        {
            std::lock_guard<std::mutex> lock(SaveMutex);
            saveScheduled = false;
        }
        }).detach();
}

void Settings::InitializeDefaults() {
    windowPosition = ImVec2(100, 100);
    windowSize = ImVec2(300, 400);
    showTitle = true;
    allowResize = true;
    colors = WindowColors();
    timers.clear();
    usedIds.clear();

    // Initialize sound settings
    sounds.masterVolume = 1.0f;
    sounds.soundVolumes.clear();
    sounds.soundPans.clear();
    sounds.recentSounds.clear();
    sounds.customSoundsDirectory = "";

    // Set default volumes for our standard sounds using new format
    sounds.soundVolumes[SoundID(themes_chime_success).ToString()] = 1.0f;
    sounds.soundVolumes[SoundID(themes_chime_info).ToString()] = 1.0f;
    sounds.soundVolumes[SoundID(themes_chime_warning).ToString()] = 1.0f;

    // Initialize websocket settings
    websocket.serverUrl = "wss://simple-timers-wss.onrender.com";
    websocket.autoConnect = false;
    websocket.enabled = false;
    websocket.pingInterval = 30000;
    websocket.autoReconnect = true;
    websocket.reconnectInterval = 5000;
    websocket.maxReconnectAttempts = 5;
    websocket.logMessages = true;
    websocket.maxLogEntries = 100;
    websocket.ensureClientId();
    websocket.tlsOptions.verifyPeer = false;
    websocket.tlsOptions.verifyHost = false;
    websocket.tlsOptions.enableServerCertAuth = false;
}

TimerData& Settings::AddTimer(const std::string& name, float duration) {
    std::lock_guard<std::mutex> lock(Mutex);
    TimerData timer(name, duration);

    // Ensure unique ID
    while (usedIds.find(timer.id) != usedIds.end()) {
        timer.id = TimerData::generateUniqueId("timer_");
    }

    usedIds.insert(timer.id);
    timers.emplace_back(std::move(timer));
    return timers.back();
}

void Settings::RemoveTimer(const std::string& id) {
    std::lock_guard<std::mutex> lock(Mutex);
    timers.erase(
        std::remove_if(timers.begin(), timers.end(),
            [id](const TimerData& timer) { return timer.id == id; }),
        timers.end()
    );
    usedIds.erase(id);
}

TimerData* Settings::FindTimer(const std::string& id) {
    std::lock_guard<std::mutex> lock(Mutex);
    auto it = std::find_if(timers.begin(), timers.end(),
        [id](const TimerData& timer) { return timer.id == id; });
    return it != timers.end() ? &(*it) : nullptr;
}

// Sound settings methods
void Settings::SetMasterVolume(float volume) {
    std::lock_guard<std::mutex> lock(Mutex);
    // Clamp to valid range
    sounds.masterVolume = std::max(0.0f, std::min(1.0f, volume));

    if (APIDefs) {
        char logMsg[128];
        sprintf_s(logMsg, "Setting master volume to %.2f and saving...", volume);
        APIDefs->Log(ELogLevel_DEBUG, ADDON_NAME, logMsg);
    }

    // Call the global Save method to ensure everything is saved
    if (!SettingsPath.empty()) {
        ScheduleSave(SettingsPath);
    }
    else if (APIDefs) {
        APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, "Cannot save - SettingsPath is empty");
    }
}

float Settings::GetMasterVolume() {
    std::lock_guard<std::mutex> lock(Mutex);
    return sounds.masterVolume;
}

void Settings::SetSoundVolume(int resourceId, float volume) {
    std::lock_guard<std::mutex> lock(Mutex);
    // Clamp volume between 0 and 1
    float clampedVolume = std::max(0.0f, std::min(1.0f, volume));

    // Convert to new format
    SoundID id(resourceId);
    sounds.soundVolumes[id.ToString()] = clampedVolume;

    // Update the sound engine for this sound
    if (g_SoundEngine) {
        try {
            g_SoundEngine->SetSoundVolume(id, clampedVolume);
        }
        catch (...) {
            if (APIDefs) {
                APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, "Error updating sound engine volume for sound ID");
            }
        }
    }
}

float Settings::GetSoundVolume(int resourceId) {
    std::lock_guard<std::mutex> lock(Mutex);
    // Convert to new format
    SoundID id(resourceId);
    std::string idStr = id.ToString();

    auto it = sounds.soundVolumes.find(idStr);
    if (it != sounds.soundVolumes.end()) {
        return it->second;
    }
    // Default volume if not specified
    return 1.0f;
}

void Settings::SetFileSoundVolume(const std::string& filePath, float volume) {
    std::lock_guard<std::mutex> lock(Mutex);
    // Clamp volume between 0 and 1
    float clampedVolume = std::max(0.0f, std::min(1.0f, volume));

    // Convert to new format
    SoundID id(filePath);
    sounds.soundVolumes[id.ToString()] = clampedVolume;

    // Update the sound engine for this sound
    if (g_SoundEngine) {
        try {
            g_SoundEngine->SetSoundVolume(id, clampedVolume);
        }
        catch (...) {
            if (APIDefs) {
                APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, "Error updating sound engine volume for file sound");
            }
        }
    }
}

float Settings::GetFileSoundVolume(const std::string& filePath) {
    std::lock_guard<std::mutex> lock(Mutex);
    // Convert to new format
    SoundID id(filePath);
    std::string idStr = id.ToString();

    auto it = sounds.soundVolumes.find(idStr);
    if (it != sounds.soundVolumes.end()) {
        return it->second;
    }
    // Default volume if not specified
    return 1.0f;
}

void Settings::SetAudioDeviceIndex(int index) {
    std::lock_guard<std::mutex> lock(Mutex);
    sounds.audioDeviceIndex = index;

    if (APIDefs) {
        char logMsg[128];
        sprintf_s(logMsg, "Setting audio device index to %d and saving...", index);
        APIDefs->Log(ELogLevel_DEBUG, ADDON_NAME, logMsg);
    }

    // Call the global Save method to ensure everything is saved
    if (!SettingsPath.empty()) {
        ScheduleSave(SettingsPath);
    }
    else if (APIDefs) {
        APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, "Cannot save - SettingsPath is empty");
    }
}

int Settings::GetAudioDeviceIndex() {
    std::lock_guard<std::mutex> lock(Mutex);
    return sounds.audioDeviceIndex;
}

void Settings::SetSoundPan(int soundId, float pan) {
    std::lock_guard<std::mutex> lock(Mutex);
    // Clamp pan between -1.0 (full left) and 1.0 (full right)
    float clampedPan = std::max(-1.0f, std::min(1.0f, pan));

    // Convert to new format
    SoundID id(soundId);
    sounds.soundPans[id.ToString()] = clampedPan;

    // Update the sound engine with the new panning
    if (g_SoundEngine) {
        try {
            g_SoundEngine->SetSoundPan(id, clampedPan);
        }
        catch (...) {
            if (APIDefs) {
                APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, "Error updating sound engine panning for sound ID");
            }
        }
    }
}

float Settings::GetSoundPan(int soundId) {
    std::lock_guard<std::mutex> lock(Mutex);
    // Convert to new format
    SoundID id(soundId);
    std::string idStr = id.ToString();

    auto it = sounds.soundPans.find(idStr);
    if (it != sounds.soundPans.end()) {
        return it->second;
    }
    return 0.0f; // Default to center
}

void Settings::SetFileSoundPan(const std::string& filePath, float pan) {
    std::lock_guard<std::mutex> lock(Mutex);
    // Clamp pan between -1.0 (full left) and 1.0 (full right)
    float clampedPan = std::max(-1.0f, std::min(1.0f, pan));

    // Convert to new format
    SoundID id(filePath);
    sounds.soundPans[id.ToString()] = clampedPan;

    // Update the sound engine with the new panning
    if (g_SoundEngine) {
        try {
            g_SoundEngine->SetSoundPan(id, clampedPan);
        }
        catch (...) {
            if (APIDefs) {
                APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, "Error updating sound engine panning for file sound");
            }
        }
    }
}

float Settings::GetFileSoundPan(const std::string& filePath) {
    std::lock_guard<std::mutex> lock(Mutex);
    // Convert to new format
    SoundID id(filePath);
    std::string idStr = id.ToString();

    auto it = sounds.soundPans.find(idStr);
    if (it != sounds.soundPans.end()) {
        return it->second;
    }
    return 0.0f; // Default to center
}

void Settings::SetCustomSoundsDirectory(const std::string& directory) {
    std::lock_guard<std::mutex> lock(Mutex);
    sounds.customSoundsDirectory = directory;

    // Call the global Save method to ensure everything is saved
    if (!SettingsPath.empty()) {
        ScheduleSave(SettingsPath);
    }
}

std::string Settings::GetCustomSoundsDirectory() {
    std::lock_guard<std::mutex> lock(Mutex);
    return sounds.customSoundsDirectory;
}

void Settings::AddRecentSound(const std::string& soundIdStr) {
    std::lock_guard<std::mutex> lock(Mutex);
    sounds.addRecentSound(soundIdStr);

    // Call the global Save method to ensure everything is saved
    if (!SettingsPath.empty()) {
        ScheduleSave(SettingsPath);
    }
}

const std::vector<std::string>& Settings::GetRecentSounds() {
    std::lock_guard<std::mutex> lock(Mutex);
    return sounds.recentSounds;
}

void Settings::AddTtsSound(const std::string& soundId, const std::string& name, float volume, float pan) {
    std::lock_guard<std::mutex> lock(Mutex);

    // Check if this sound already exists
    auto it = std::find_if(sounds.ttsSounds.begin(), sounds.ttsSounds.end(),
        [&soundId](const SoundSettings::TtsSoundInfo& info) { return info.id == soundId; });

    if (it != sounds.ttsSounds.end()) {
        // Update existing sound
        it->name = name;
        it->volume = volume;
        it->pan = pan;
    }
    else {
        // Add new sound
        sounds.ttsSounds.emplace_back(soundId, name, volume, pan);
    }

    // Only save if we're not during initialization
    if (!isInitializing && !SettingsPath.empty()) {
        try {
            ScheduleSave(SettingsPath);
        }
        catch (const std::exception& e) {
            if (APIDefs) {
                char errorMsg[256];
                sprintf_s(errorMsg, "Exception saving TTS sound: %s", e.what());
                APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, errorMsg);
            }
        }
    }
}

const std::vector<SoundSettings::TtsSoundInfo>& Settings::GetTtsSounds() {
    std::lock_guard<std::mutex> lock(Mutex);
    return sounds.ttsSounds;
}

bool Settings::LoadSavedTtsSounds() {
    if (!g_TextToSpeech || !g_SoundEngine) {
        return false;
    }

    // Set initialization flag to prevent saving during loading
    isInitializing = true;

    // Initialize TTS engine if needed
    if (!g_TextToSpeech->IsInitialized() && !g_TextToSpeech->Initialize()) {
        if (APIDefs) {
            APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, "Failed to initialize TTS engine for loading saved sounds");
        }
        isInitializing = false;
        return false;
    }

    bool success = true;

    // Load each saved TTS sound
    for (const auto& ttsInfo : sounds.ttsSounds) {
        try {
            // Extract the text from the ID string
            // Format is "tts:voiceId:text"
            std::string idStr = ttsInfo.id;
            size_t secondColon = idStr.find(':', 4);
            if (secondColon == std::string::npos) continue;

            std::string voiceStr = idStr.substr(4, secondColon - 4);
            std::string text = idStr.substr(secondColon + 1);

            // Set the voice if specified
            int voiceIndex = -1;
            if (voiceStr != "default") {
                try {
                    voiceIndex = std::stoi(voiceStr);
                    g_TextToSpeech->SetVoice(voiceIndex);
                }
                catch (...) {
                    // Use default voice
                }
            }

            // Let the TextToSpeech engine create the sound
            if (g_TextToSpeech->CreateTtsSound(text, ttsInfo.name, voiceIndex, ttsInfo.volume, ttsInfo.pan)) {
                if (APIDefs) {
                    char logMsg[256];
                    sprintf_s(logMsg, "Loaded saved TTS sound: %s", ttsInfo.name.c_str());
                    APIDefs->Log(ELogLevel_DEBUG, ADDON_NAME, logMsg);
                }
            }
            else {
                success = false;
                if (APIDefs) {
                    char logMsg[256];
                    sprintf_s(logMsg, "Failed to generate TTS audio for saved sound: %s", ttsInfo.name.c_str());
                    APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, logMsg);
                }
            }
        }
        catch (const std::exception& e) {
            success = false;
            if (APIDefs) {
                char logMsg[256];
                sprintf_s(logMsg, "Exception loading TTS sound: %s", e.what());
                APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, logMsg);
            }
        }
        catch (...) {
            success = false;
            if (APIDefs) {
                APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, "Unknown exception loading TTS sound");
            }
        }
    }

    // Clear initialization flag
    isInitializing = false;
    return success;
}

void Settings::Save(const std::string& path)
{
    try {
        if (path.empty()) {
            if (APIDefs) {
                APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, "Cannot save settings - path is empty");
            }
            return;
        }

        // Log that we're starting a save
        if (APIDefs) {
            char logMsg[256];
            sprintf_s(logMsg, "Saving settings to: %s", path.c_str());
            APIDefs->Log(ELogLevel_DEBUG, ADDON_NAME, logMsg);
        }

        // Create a JSON object to hold current data
        json localData = json::object();
        json windowJson = json::object();
        json colorsJson = json::object();
        json websocketJson = json::object();
        json tlsOptionsJson = json::object();
        json soundsJson = json::object();
        json timersJson = json::array();
        json roomSubscriptionsJson = json::object();


        {
            // Lock so we can safely read from shared variables
            std::lock_guard<std::mutex> lock(Mutex);

            // Update window settings
            windowJson["positionX"] = windowPosition.x;
            windowJson["positionY"] = windowPosition.y;
            windowJson["sizeX"] = windowSize.x;
            windowJson["sizeY"] = windowSize.y;
            windowJson["showTitle"] = showTitle;
            windowJson["allowResize"] = allowResize;
            localData["window"] = windowJson;

            // Update color settings
            colorsJson["background"] = colors.background;
            colorsJson["text"] = colors.text;
            colorsJson["timerActive"] = colors.timerActive;
            colorsJson["timerPaused"] = colors.timerPaused;
            colorsJson["timerExpired"] = colors.timerExpired;
            localData["colors"] = colorsJson;

            // WebSocket settings
            tlsOptionsJson["verifyPeer"] = websocket.tlsOptions.verifyPeer;
            tlsOptionsJson["verifyHost"] = websocket.tlsOptions.verifyHost;
            tlsOptionsJson["caFile"] = websocket.tlsOptions.caFile;
            tlsOptionsJson["caPath"] = websocket.tlsOptions.caPath;
            tlsOptionsJson["certFile"] = websocket.tlsOptions.certFile;
            tlsOptionsJson["keyFile"] = websocket.tlsOptions.keyFile;
            tlsOptionsJson["enableServerCertAuth"] = websocket.tlsOptions.enableServerCertAuth;

            websocketJson["serverUrl"] = websocket.serverUrl;
            websocketJson["autoConnect"] = websocket.autoConnect;
            websocketJson["enabled"] = websocket.enabled;
            websocketJson["pingInterval"] = websocket.pingInterval;
            websocketJson["autoReconnect"] = websocket.autoReconnect;
            websocketJson["reconnectInterval"] = websocket.reconnectInterval;
            websocketJson["maxReconnectAttempts"] = websocket.maxReconnectAttempts;
            websocketJson["logMessages"] = websocket.logMessages;
            websocketJson["maxLogEntries"] = websocket.maxLogEntries;
            websocketJson["clientId"] = websocket.clientId;
            websocketJson["tlsOptions"] = tlsOptionsJson;
            

            // Sound settings
            soundsJson["masterVolume"] = sounds.masterVolume;
            soundsJson["audioDeviceIndex"] = sounds.audioDeviceIndex;
            soundsJson["customSoundsDirectory"] = sounds.customSoundsDirectory;

            // Sound volumes
            json soundVolumesJson = json::object();
            for (const auto& [soundIdStr, volume] : sounds.soundVolumes) {
                try {
                    soundVolumesJson[soundIdStr] = volume;
                }
                catch (...) {
                    if (APIDefs) {
                        APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, "Error saving sound volume");
                    }
                }
            }
            soundsJson["soundVolumes"] = soundVolumesJson;

            // Sound pans
            json soundPansJson = json::object();
            for (const auto& [soundIdStr, pan] : sounds.soundPans) {
                try {
                    soundPansJson[soundIdStr] = pan;
                }
                catch (...) {
                    if (APIDefs) {
                        APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, "Error saving sound pan");
                    }
                }
            }
            soundsJson["soundPans"] = soundPansJson;

            // Recent sounds
            soundsJson["recentSounds"] = json(sounds.recentSounds);

            // TTS sounds
            json ttsSoundsJson = json::array();
            for (const auto& ttsSound : sounds.ttsSounds) {
                try {
                    json ttsSoundJson = json::object();
                    ttsSoundJson["id"] = ttsSound.id;
                    ttsSoundJson["name"] = ttsSound.name;
                    ttsSoundJson["volume"] = ttsSound.volume;
                    ttsSoundJson["pan"] = ttsSound.pan;
                    ttsSoundsJson.push_back(ttsSoundJson);
                }
                catch (...) {
                    if (APIDefs) {
                        APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, "Error saving TTS sound");
                    }
                }
            }
            soundsJson["ttsSounds"] = ttsSoundsJson;
            localData["sounds"] = soundsJson;

            for (const auto& [roomId, timerIds] : websocket.roomSubscriptions) {
                json timerIdsJson = json::array();
                for (const auto& timerId : timerIds) {
                    timerIdsJson.push_back(timerId);
                }
                roomSubscriptionsJson[roomId] = timerIdsJson;
            }
            websocketJson["roomSubscriptions"] = roomSubscriptionsJson;
            websocketJson["currentRoomId"] = websocket.currentRoomId;
            localData["websocket"] = websocketJson;

            // Timers
            for (const auto& timer : timers) {
                try {
                    timersJson.push_back(timer.toJson());
                }
                catch (...) {
                    if (APIDefs) {
                        APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, "Error saving timer");
                    }
                }
            }
            localData["timers"] = timersJson;
        }

        // Write to disk (no lock needed here)
        bool fileSaved = false;
        int retryCount = 0;
        const int maxRetries = 3;

        while (!fileSaved && retryCount < maxRetries) {
            try {
                std::ofstream file(path);
                if (file.is_open()) {
                    // Write the JSON with pretty formatting (indent=4)
                    file << localData.dump(4);
                    file.flush();
                    file.close();
                    fileSaved = true;

                    if (APIDefs) {
                        APIDefs->Log(ELogLevel_DEBUG, ADDON_NAME, "Settings saved successfully");
                    }

                    // Update the main SettingsData with what we wrote
                    {
                        std::lock_guard<std::mutex> lock(Mutex);
                        SettingsData = localData;
                    }
                }
                else {
                    if (APIDefs) {
                        char errorMsg[256];
                        sprintf_s(errorMsg, "Could not open settings file for writing (attempt %d): %s",
                            retryCount + 1, path.c_str());
                        APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, errorMsg);
                    }

                    // Wait briefly before retrying
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                }
            }
            catch (const std::exception& e) {
                if (APIDefs) {
                    char errorMsg[256];
                    sprintf_s(errorMsg, "File I/O error (attempt %d): %s", retryCount + 1, e.what());
                    APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, errorMsg);
                }
                // Wait briefly before retrying
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }

            retryCount++;
        }

        if (!fileSaved && APIDefs) {
            APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, "Failed to save settings after multiple attempts");
        }
    }
    catch (const std::exception& e) {
        if (APIDefs) {
            char errorMsg[256];
            sprintf_s(errorMsg, "Exception during settings save: %s", e.what());
            APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, errorMsg);
        }
    }
    catch (...) {
        if (APIDefs) {
            APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, "Unknown exception during settings save");
        }
    }
}

// WebSocket settings methods
void Settings::SetWebSocketServerUrl(const std::string& url) {
    std::lock_guard<std::mutex> lock(Mutex);
    websocket.serverUrl = url;

    // Save settings
    if (!SettingsPath.empty()) {
        ScheduleSave(SettingsPath);
    }
}

std::string Settings::GetWebSocketServerUrl() {
    std::lock_guard<std::mutex> lock(Mutex);
    return websocket.serverUrl;
}

void Settings::SetWebSocketAutoConnect(bool autoConnect) {
    std::lock_guard<std::mutex> lock(Mutex);
    websocket.autoConnect = autoConnect;

    // Save settings
    if (!SettingsPath.empty()) {
        ScheduleSave(SettingsPath);
    }
}

bool Settings::GetWebSocketAutoConnect() {
    std::lock_guard<std::mutex> lock(Mutex);
    return websocket.autoConnect;
}

void Settings::SetWebSocketEnabled(bool enabled) {
    std::lock_guard<std::mutex> lock(Mutex);
    websocket.enabled = enabled;

    // Save settings
    if (!SettingsPath.empty()) {
        ScheduleSave(SettingsPath);
    }
}

bool Settings::GetWebSocketEnabled() {
    std::lock_guard<std::mutex> lock(Mutex);
    return websocket.enabled;
}

void Settings::SetWebSocketConnectionStatus(const std::string& status) {
    std::lock_guard<std::mutex> lock(Mutex);
    websocket.connectionStatus = status;

    // Don't save this as it's transient
}

std::string Settings::GetWebSocketConnectionStatus() {
    std::lock_guard<std::mutex> lock(Mutex);
    return websocket.connectionStatus;
}

void Settings::AddWebSocketLogEntry(const std::string& direction, const std::string& message) {
    std::lock_guard<std::mutex> lock(Mutex);
    websocket.logMessage(direction, message);

    // Don't save for each log entry - that would be too frequent
}

const std::vector<WebSocketLogEntry>& Settings::GetWebSocketLog() {
    std::lock_guard<std::mutex> lock(Mutex);
    return websocket.messageLog;
}

void Settings::ClearWebSocketLog() {
    std::lock_guard<std::mutex> lock(Mutex);
    websocket.clearLog();
}

std::string Settings::GetWebSocketClientId() {
    std::lock_guard<std::mutex> lock(Mutex);
    websocket.ensureClientId();
    return websocket.clientId;
}

void Settings::SetCurrentRoom(const std::string& roomId) {
    std::lock_guard<std::mutex> lock(Mutex);
    websocket.currentRoomId = roomId;

    // Save settings after updating
    if (!SettingsPath.empty()) {
        ScheduleSave(SettingsPath);
    }
}

std::string Settings::GetCurrentRoom() {
    std::lock_guard<std::mutex> lock(Mutex);
    return websocket.currentRoomId;
}

void Settings::SetAvailableRooms(const std::vector<RoomInfo>& rooms) {
    std::lock_guard<std::mutex> lock(Mutex);
    websocket.availableRooms = rooms;

    // We don't save available rooms to disk as they're transient
    // and refreshed on connection
}

std::vector<RoomInfo> Settings::GetAvailableRooms() {
    std::lock_guard<std::mutex> lock(Mutex);
    return websocket.availableRooms;
}

bool Settings::IsSubscribedToTimer(const std::string& timerId, const std::string& roomId) {
    std::lock_guard<std::mutex> lock(Mutex);
    return websocket.isSubscribedToTimer(timerId, roomId.empty() ? websocket.currentRoomId : roomId);
}

void Settings::SubscribeToTimer(const std::string& timerId, const std::string& roomId) {
    std::lock_guard<std::mutex> lock(Mutex);
    websocket.subscribeToTimer(timerId, roomId.empty() ? websocket.currentRoomId : roomId);

    // Save settings after updating subscriptions
    if (!SettingsPath.empty()) {
        ScheduleSave(SettingsPath);
    }
}

void Settings::UnsubscribeFromTimer(const std::string& timerId, const std::string& roomId) {
    std::lock_guard<std::mutex> lock(Mutex);
    websocket.unsubscribeFromTimer(timerId, roomId.empty() ? websocket.currentRoomId : roomId);

    // Save settings after updating subscriptions
    if (!SettingsPath.empty()) {
        ScheduleSave(SettingsPath);
    }
}

std::unordered_set<std::string> Settings::GetSubscriptionsForRoom(const std::string& roomId) {
    std::lock_guard<std::mutex> lock(Mutex);
    return websocket.getSubscriptionsForRoom(roomId.empty() ? websocket.currentRoomId : roomId);
}

void Settings::CleanupSubscriptions() {
    std::lock_guard<std::mutex> lock(Mutex);

    // Use the available rooms list to determine which rooms still exist
    std::unordered_set<std::string> validRoomIds;
    for (const auto& room : websocket.availableRooms) {
        validRoomIds.insert(room.id);
    }

    // Remove subscriptions for rooms that no longer exist
    auto it = websocket.roomSubscriptions.begin();
    while (it != websocket.roomSubscriptions.end()) {
        if (validRoomIds.find(it->first) == validRoomIds.end()) {
            // Room doesn't exist anymore
            it = websocket.roomSubscriptions.erase(it);
        }
        else {
            ++it;
        }
    }

    // Save after cleanup
    if (!SettingsPath.empty()) {
        ScheduleSave(SettingsPath);
    }
}