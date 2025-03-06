#include "settings.h"
#include <fstream>
#include <chrono>
#include <iomanip>
#include <sstream>
#include "shared.h"
#include "Sounds.h"
#include "TextToSpeech.h" 

bool Settings::isInitializing = false;


TimerData::TimerData(const std::string& name, float duration)
    : name(name)
    , duration(duration)
    , endSound(SoundID(themes_chime_success))
    , warningTime(30.0f)
    , warningSound(SoundID(themes_chime_info))
    , useWarning(false)
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
    return {
        {"name", name},
        {"id", id},
        {"duration", duration},
        {"endSound", endSound.ToString()},
        {"warningTime", warningTime},
        {"warningSound", warningSound.ToString()},
        {"useWarning", useWarning}
    };
}

TimerData TimerData::fromJson(const json& j) {
    TimerData timer;
    timer.name = j.value("name", "");
    timer.id = j.value("id", generateUniqueId("timer_"));
    timer.duration = j.value("duration", 0.0f);

    // Deserialize endSound
    std::string endSoundStr = j.value("endSound", "");
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
    timer.warningTime = j.value("warningTime", 30.0f);
    std::string warningSoundStr = j.value("warningSound", "");
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

    timer.useWarning = j.value("useWarning", false);
    return timer;
}


// Static member initializations
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
            windowPosition.x = window.value("positionX", 100.0f);
            windowPosition.y = window.value("positionY", 100.0f);
            windowSize.x = window.value("sizeX", 300.0f);
            windowSize.y = window.value("sizeY", 400.0f);
            showTitle = window.value("showTitle", true);
            allowResize = window.value("allowResize", true);
        }

        // Load colors
        if (SettingsData.contains("colors")) {
            const auto& colorsJson = SettingsData["colors"];
            colors.background = colorsJson.value("background", colors.background);
            colors.text = colorsJson.value("text", colors.text);
            colors.timerActive = colorsJson.value("timerActive", colors.timerActive);
            colors.timerPaused = colorsJson.value("timerPaused", colors.timerPaused);
            colors.timerExpired = colorsJson.value("timerExpired", colors.timerExpired);
        }

        // Load sound settings
        if (SettingsData.contains("sounds")) {
            const auto& soundsJson = SettingsData["sounds"];
            sounds.masterVolume = soundsJson.value("masterVolume", 1.0f);
            sounds.audioDeviceIndex = soundsJson.value("audioDeviceIndex", -1);
            sounds.customSoundsDirectory = soundsJson.value("customSoundsDirectory", "");

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
                        std::string id = ttsJson.value("id", "");
                        std::string name = ttsJson.value("name", "");
                        float volume = ttsJson.value("volume", 1.0f);
                        float pan = ttsJson.value("pan", 0.0f);
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

        // Load Websockets
        if (SettingsData.contains("websocket")) {
            const auto& websocketJson = SettingsData["websocket"];
            websocket.serverUrl = websocketJson.value("serverUrl", "ws://localhost:8080");
            websocket.autoConnect = websocketJson.value("autoConnect", false);
            websocket.enabled = websocketJson.value("enabled", false);
            websocket.pingInterval = websocketJson.value("pingInterval", 30000);
            websocket.autoReconnect = websocketJson.value("autoReconnect", true);
            websocket.reconnectInterval = websocketJson.value("reconnectInterval", 5000);
            websocket.maxReconnectAttempts = websocketJson.value("maxReconnectAttempts", 5);
            websocket.logMessages = websocketJson.value("logMessages", true);
            websocket.maxLogEntries = websocketJson.value("maxLogEntries", 100);

            // Load client ID if exists
            if (websocketJson.contains("clientId")) {
                websocket.clientId = websocketJson.value("clientId", "");
            }
            else {
                // Generate a new client ID
                websocket.ensureClientId();
            }

            // Load TLS settings if they exist
            if (websocketJson.contains("tlsOptions")) {
                const auto& tlsJson = websocketJson["tlsOptions"];
                websocket.tlsOptions.verifyPeer = tlsJson.value("verifyPeer", true);
                websocket.tlsOptions.verifyHost = tlsJson.value("verifyHost", true);
                websocket.tlsOptions.caFile = tlsJson.value("caFile", "");
                websocket.tlsOptions.caPath = tlsJson.value("caPath", "");
                websocket.tlsOptions.certFile = tlsJson.value("certFile", "");
                websocket.tlsOptions.keyFile = tlsJson.value("keyFile", "");
                websocket.tlsOptions.enableServerCertAuth = tlsJson.value("enableServerCertAuth", true);
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

void Settings::ScheduleSave(const std::string& path)
{
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

                // If we've gone a full cooldown with no new changes, we’re safe to save.
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
    sounds.customSoundsDirectory = "";  // Will be set to a default location on first run

    // Set default volumes for our standard sounds using new format
    sounds.soundVolumes[SoundID(themes_chime_success).ToString()] = 1.0f;
    sounds.soundVolumes[SoundID(themes_chime_info).ToString()] = 1.0f;
    sounds.soundVolumes[SoundID(themes_chime_warning).ToString()] = 1.0f;
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
    sounds.masterVolume = (std::max)(0.0f, (std::min)(1.0f, volume));

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
    float clampedVolume = (std::max)(0.0f, (std::min)(1.0f, volume));

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
    float clampedVolume = (std::max)(0.0f, (std::min)(1.0f, volume));

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
    float clampedPan = (std::max)(-1.0f, (std::min)(1.0f, pan));

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
    float clampedPan = (std::max)(-1.0f, (std::min)(1.0f, pan));

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

        // Create local JSON objects to hold current data
        json localData;
        json ttsSoundsJson = json::array();
        json timersJson = json::array();
        json soundVolumesJson = json::object();
        json soundPansJson = json::object();

        {
            // Lock so we can safely read from SettingsData and other shared variables
            std::lock_guard<std::mutex> lock(Mutex);

            // Start with a copy of the current settings
            localData = SettingsData;

            // Update window settings
            localData["window"] = {
                {"positionX", windowPosition.x},
                {"positionY", windowPosition.y},
                {"sizeX",     windowSize.x},
                {"sizeY",     windowSize.y},
                {"showTitle", showTitle},
                {"allowResize", allowResize}
            };

            // Update color settings
            localData["colors"] = {
                {"background", colors.background},
                {"text", colors.text},
                {"timerActive", colors.timerActive},
                {"timerPaused", colors.timerPaused},
                {"timerExpired", colors.timerExpired}
            };

            json tlsOptionsJson = {
                {"verifyPeer", websocket.tlsOptions.verifyPeer},
                {"verifyHost", websocket.tlsOptions.verifyHost},
                {"caFile", websocket.tlsOptions.caFile},
                {"caPath", websocket.tlsOptions.caPath},
                {"certFile", websocket.tlsOptions.certFile},
                {"keyFile", websocket.tlsOptions.keyFile},
                {"enableServerCertAuth", websocket.tlsOptions.enableServerCertAuth}
            };

            json websocketJson = {
                {"serverUrl", websocket.serverUrl},
                {"autoConnect", websocket.autoConnect},
                {"enabled", websocket.enabled},
                {"pingInterval", websocket.pingInterval},
                {"autoReconnect", websocket.autoReconnect},
                {"reconnectInterval", websocket.reconnectInterval},
                {"maxReconnectAttempts", websocket.maxReconnectAttempts},
                {"logMessages", websocket.logMessages},
                {"maxLogEntries", websocket.maxLogEntries},
                {"clientId", websocket.clientId},
                {"tlsOptions", tlsOptionsJson}
            };

            localData["websocket"] = websocketJson;

            // Prepare sound volumes
            for (const auto& [soundIdStr, volume] : sounds.soundVolumes) {
                try {
                    soundVolumesJson[soundIdStr] = volume;
                }
                catch (const std::exception& e) {
                    if (APIDefs) {
                        char errorMsg[128];
                        sprintf_s(errorMsg, "Error converting sound volume: %s", e.what());
                        APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, errorMsg);
                    }
                }
                catch (...) {
                    if (APIDefs) {
                        APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, "Unknown error converting sound volume");
                    }
                }
            }

            // Prepare sound pans
            for (const auto& [soundIdStr, pan] : sounds.soundPans) {
                try {
                    soundPansJson[soundIdStr] = pan;

                    // Example log: saving the pan value
                    if (APIDefs) {
                        char logMsg[128];
                        sprintf_s(logMsg, "Saving pan for sound %s: %.2f", soundIdStr.c_str(), pan);
                        APIDefs->Log(ELogLevel_DEBUG, ADDON_NAME, logMsg);
                    }
                }
                catch (const std::exception& e) {
                    if (APIDefs) {
                        char errorMsg[128];
                        sprintf_s(errorMsg, "Error converting sound pan: %s", e.what());
                        APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, errorMsg);
                    }
                }
                catch (...) {
                    if (APIDefs) {
                        APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, "Unknown error converting sound pan");
                    }
                }
            }

            // Prepare TTS sounds
            for (const auto& ttsSound : sounds.ttsSounds) {
                try {
                    ttsSoundsJson.push_back({
                        {"id", ttsSound.id},
                        {"name", ttsSound.name},
                        {"volume", ttsSound.volume},
                        {"pan", ttsSound.pan}
                        });

                    // Example log
                    if (APIDefs) {
                        char logMsg[256];
                        sprintf_s(logMsg, "Saving TTS sound: %s", ttsSound.name.c_str());
                        APIDefs->Log(ELogLevel_DEBUG, ADDON_NAME, logMsg);
                    }
                }
                catch (const std::exception& e) {
                    if (APIDefs) {
                        char errorMsg[256];
                        sprintf_s(errorMsg, "Error converting TTS sound to JSON: %s", e.what());
                        APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, errorMsg);
                    }
                }
                catch (...) {
                    if (APIDefs) {
                        APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, "Unknown error converting TTS sound to JSON");
                    }
                }
            }

            // Prepare timers array
            for (const auto& timer : timers) {
                try {
                    timersJson.push_back(timer.toJson());
                }
                catch (const std::exception& e) {
                    if (APIDefs) {
                        char errorMsg[128];
                        sprintf_s(errorMsg, "Error converting timer: %s", e.what());
                        APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, errorMsg);
                    }
                }
                catch (...) {
                    if (APIDefs) {
                        APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, "Unknown error converting timer");
                    }
                }
            }

            // Update localData with all prepared data
            localData["sounds"] = {
                {"masterVolume", sounds.masterVolume},
                {"audioDeviceIndex", sounds.audioDeviceIndex},
                {"customSoundsDirectory", sounds.customSoundsDirectory},
                {"soundVolumes", soundVolumesJson},
                {"soundPans", soundPansJson},
                {"recentSounds", sounds.recentSounds},
                {"ttsSounds", ttsSoundsJson}
            };

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

const std::vector<WebSocketSettings::LogEntry>& Settings::GetWebSocketLog() {
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