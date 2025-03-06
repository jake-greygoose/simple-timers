#ifndef SETTINGS_H
#define SETTINGS_H

#include <string>
#include <vector>
#include <mutex>
#include <unordered_set>
#include <chrono>
#include <iomanip>
#include <sstream>
#include "imgui/imgui.h"
#include "nlohmann/json.hpp"
#include "resource.h"
#include "Sounds.h"  // Include the new Sound.h header
#include "TextToSpeech.h" 

using json = nlohmann::json;

// JSON serialization for ImVec4
namespace nlohmann {
    template<>
    struct adl_serializer<ImVec4> {
        static void to_json(json& j, const ImVec4& vec) {
            j = json{
                {"r", vec.x},
                {"g", vec.y},
                {"b", vec.z},
                {"a", vec.w}
            };
        }

        static void from_json(const json& j, ImVec4& vec) {
            vec.x = j.value("r", 1.0f);
            vec.y = j.value("g", 1.0f);
            vec.z = j.value("b", 1.0f);
            vec.w = j.value("a", 1.0f);
        }
    };
}

struct TimerData {
    std::string name;
    std::string id;
    float duration;  // in seconds

    // Sound settings updated to use SoundID
    SoundID endSound;            // Sound played when timer finishes
    float warningTime;           // seconds before end for warning notification
    bool useWarning;             // Whether to use a warning
    SoundID warningSound;        // Sound played for warning

    TimerData(const std::string& name = "", float duration = 0.0f);
    json toJson() const;
    static TimerData fromJson(const json& j);

protected:
    static std::string generateUniqueId(const std::string& prefix);
    friend class Settings;
};

struct WindowColors {
    ImVec4 background = ImVec4(0.2f, 0.2f, 0.2f, 1.0f);
    ImVec4 text = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
    ImVec4 timerActive = ImVec4(0.2f, 0.7f, 0.2f, 1.0f);
    ImVec4 timerPaused = ImVec4(0.7f, 0.7f, 0.2f, 1.0f);
    ImVec4 timerExpired = ImVec4(0.7f, 0.2f, 0.2f, 1.0f);
};

struct SoundSettings {
    float masterVolume = 1.0f;
    int audioDeviceIndex = -1;

    // Updated to store unified sound volumes and panning by SoundID string
    std::map<std::string, float> soundVolumes;  // SoundID.ToString() -> volume
    std::map<std::string, float> soundPans;     // SoundID.ToString() -> pan


    // Directory for custom sounds 
    std::string customSoundsDirectory;

    // Recently used sounds as SoundID strings
    std::vector<std::string> recentSounds;

    // Add a sound to recent sounds list (and prevent duplicates)
    void addRecentSound(const std::string& soundIdStr) {
        // Remove if already exists
        auto it = std::find(recentSounds.begin(), recentSounds.end(), soundIdStr);
        if (it != recentSounds.end()) {
            recentSounds.erase(it);
        }

        // Add to front of list (most recent)
        recentSounds.insert(recentSounds.begin(), soundIdStr);

        // Limit the list size
        if (recentSounds.size() > 10) {
            recentSounds.resize(10);
        }
    }

    struct TtsSoundInfo {
        std::string id;          // TTS sound ID string
        std::string name;        // Display name
        float volume;            // Volume level
        float pan;               // Pan position

        TtsSoundInfo(const std::string& soundId = "", const std::string& displayName = "",
            float soundVolume = 1.0f, float soundPan = 0.0f)
            : id(soundId), name(displayName), volume(soundVolume), pan(soundPan) {}
    };

    // List of TTS sounds to load at startup
    std::vector<TtsSoundInfo> ttsSounds;
};

// WebSocket Settings Structure
struct WebSocketSettings {
    std::string serverUrl = "ws://localhost:8080";  // Default server URL
    bool autoConnect = false;                       // Auto-connect on startup
    bool enabled = false;                           // WebSocket functionality enabled
    std::string connectionStatus = "Disconnected";  // Current connection status
    std::string clientId;                           // Unique client identifier

    // Ping interval in milliseconds (default: 30 seconds)
    int pingInterval = 30000;

    // Reconnect settings
    bool autoReconnect = true;
    int reconnectInterval = 5000;  // Time in ms to wait before reconnect attempt
    int maxReconnectAttempts = 5;  // Maximum number of reconnect attempts

    // Message log settings
    bool logMessages = true;       // Whether to log messages
    int maxLogEntries = 100;       // Maximum number of log entries to keep

    // TLS/SSL settings for secure WebSockets (wss://)
    struct TLSOptions {
        bool verifyPeer = true;         // Verify server certificate
        bool verifyHost = true;         // Verify hostname in certificate
        std::string caFile = "";        // Path to CA certificate file (optional)
        std::string caPath = "";        // Path to directory with CA certificates (optional)
        std::string certFile = "";      // Client certificate file (for client authentication)
        std::string keyFile = "";       // Client key file (for client authentication)
        bool enableServerCertAuth = true; // Enable server certificate authentication
    };

    TLSOptions tlsOptions;

    // Message logs
    struct LogEntry {
        std::string timestamp;
        std::string direction;  // "sent" or "received"
        std::string message;

        LogEntry(const std::string& ts, const std::string& dir, const std::string& msg)
            : timestamp(ts), direction(dir), message(msg) {}
    };

    std::vector<LogEntry> messageLog;

    // Add a message to the log
    void logMessage(const std::string& direction, const std::string& message) {
        if (!logMessages) return;

        // Get current timestamp
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        std::stringstream ss;
        ss << std::put_time(std::localtime(&time), "%H:%M:%S");

        // Add to log
        messageLog.emplace_back(ss.str(), direction, message);

        // Keep log size under limit
        if (messageLog.size() > maxLogEntries) {
            messageLog.erase(messageLog.begin());
        }
    }

    // Clear the message log
    void clearLog() {
        messageLog.clear();
    }

    // Generate a unique client ID if not already set
    void ensureClientId() {
        if (clientId.empty()) {
            // Use the same method as TimerData::generateUniqueId
            auto now = std::chrono::system_clock::now();
            auto micros = std::chrono::duration_cast<std::chrono::microseconds>(
                now.time_since_epoch()
                );

            std::stringstream ss;
            ss << "client_" << std::hex << micros.count();
            clientId = ss.str();
        }
    }
};

class Settings {
public:
    static std::mutex Mutex;
    static json SettingsData;

    // Window settings
    static ImVec2 windowPosition;
    static ImVec2 windowSize;
    static bool showTitle;
    static bool allowResize;
    static WindowColors colors;
    static std::vector<TimerData> timers;
    static std::unordered_set<std::string> usedIds;

    // Sound settings
    static SoundSettings sounds;
    static void SetAudioDeviceIndex(int index);
    static int GetAudioDeviceIndex();

    // Set custom sounds directory
    static void SetCustomSoundsDirectory(const std::string& directory);
    static std::string GetCustomSoundsDirectory();

    // Recent sounds management (updated for SoundID)
    static void AddRecentSound(const std::string& soundIdStr);
    static const std::vector<std::string>& GetRecentSounds();

    // Sound pan management unified with SoundID
    static void SetSoundPan(int resourceId, float pan);
    static float GetSoundPan(int resourceId);
    static void SetFileSoundPan(const std::string& filePath, float pan);
    static float GetFileSoundPan(const std::string& filePath);

    // Functions
    static void Load(const std::string& path);
    static void Save(const std::string& path);
    static void InitializeDefaults();

    // Timer management
    static TimerData& AddTimer(const std::string& name, float duration);
    static void RemoveTimer(const std::string& id);
    static TimerData* FindTimer(const std::string& id);

    // Sound management
    static void SetMasterVolume(float volume);
    static float GetMasterVolume();

    // Resource sound volume
    static void SetSoundVolume(int soundId, float volume);
    static float GetSoundVolume(int soundId);

    // File sound volume
    static void SetFileSoundVolume(const std::string& filePath, float volume);
    static float GetFileSoundVolume(const std::string& filePath);

    // TTS 
    static void AddTtsSound(const std::string& soundId, const std::string& name, float volume, float pan);
    static const std::vector<SoundSettings::TtsSoundInfo>& GetTtsSounds();
    static bool LoadSavedTtsSounds();

    // WebSocket settings
    static WebSocketSettings websocket;
    static void SetWebSocketServerUrl(const std::string& url);
    static std::string GetWebSocketServerUrl();
    static void SetWebSocketAutoConnect(bool autoConnect);
    static bool GetWebSocketAutoConnect();
    static void SetWebSocketEnabled(bool enabled);
    static bool GetWebSocketEnabled();
    static void SetWebSocketConnectionStatus(const std::string& status);
    static std::string GetWebSocketConnectionStatus();
    static void AddWebSocketLogEntry(const std::string& direction, const std::string& message);
    static const std::vector<WebSocketSettings::LogEntry>& GetWebSocketLog();
    static void ClearWebSocketLog();
    static std::string GetWebSocketClientId();

    static bool isInitializing;
    static std::mutex SaveMutex;
    static bool saveScheduled;
    static std::chrono::steady_clock::time_point lastSaveRequest;
    static const std::chrono::milliseconds saveCooldown;

    // Helper function to schedule a save
    static void ScheduleSave(const std::string& path);
};

#endif