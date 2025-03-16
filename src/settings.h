#pragma once

#include <string>
#include <vector>
#include <random>
#include <unordered_set>
#include <unordered_map>
#include <mutex>
#include <chrono>
#include <functional>
#include "imgui/imgui.h"
#include "nlohmann/json.hpp"
#include "resource.h"
#include "Sounds.h"

// For convenience
using json = nlohmann::json;


struct WebSocketLogEntry {
    std::string timestamp;
    std::string direction;
    std::string message;

    WebSocketLogEntry(const std::string& dir, const std::string& msg)
        : direction(dir), message(msg) {
        // Create timestamp
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        char timeStr[32];
        std::tm timeinfo;
        localtime_s(&timeinfo, &time);
        std::strftime(timeStr, sizeof(timeStr), "%H:%M:%S", &timeinfo);
        timestamp = timeStr;
    }
};

// Window colors structure
struct WindowColors {
    ImVec4 background = ImVec4(0.06f, 0.06f, 0.06f, 0.94f);
    ImVec4 text = ImVec4(0.90f, 0.90f, 0.90f, 1.00f);
    ImVec4 timerActive = ImVec4(0.00f, 0.80f, 0.20f, 1.00f);
    ImVec4 timerPaused = ImVec4(0.80f, 0.80f, 0.00f, 1.00f);
    ImVec4 timerExpired = ImVec4(0.80f, 0.00f, 0.00f, 1.00f);
};

// Timer data structure
struct TimerData {
    std::string id;
    std::string name;
    float duration;
    SoundID endSound;
    float warningTime;
    SoundID warningSound;
    bool useWarning;
    bool isRoomTimer;     // New field: indicates if timer is from a room
    std::string roomId;   // New field: stores the room ID for room timers

    TimerData()
        : name("")
        , duration(0.0f)
        , endSound(SoundID(themes_chime_success))
        , warningTime(30.0f)
        , warningSound(SoundID(themes_chime_info))
        , useWarning(false)
        , isRoomTimer(false)
        , roomId("")
    {
        id = generateUniqueId("timer_");
    }

    TimerData(const std::string& name, float duration);

    static std::string generateUniqueId(const std::string& prefix);
    json toJson() const;
    static TimerData fromJson(const json& j);
};

// Room information structure
struct RoomInfo {
    std::string id;
    std::string name;
    std::time_t createdAt;
    bool isPublic;
    int clientCount;

    RoomInfo() : id(""), name(""), createdAt(0), isPublic(true), clientCount(0) {}

    json toJson() const {
        json j;
        j["id"] = id;
        j["name"] = name;
        j["createdAt"] = createdAt;
        j["isPublic"] = isPublic;
        j["clientCount"] = clientCount;
        return j;
    }

    static RoomInfo fromJson(const json& j) {
        RoomInfo room;
        room.id = j.contains("id") ? j["id"].get<std::string>() : "";
        room.name = j.contains("name") ? j["name"].get<std::string>() : "";
        room.createdAt = j.contains("createdAt") ? j["createdAt"].get<std::time_t>() : 0;
        room.isPublic = j.contains("isPublic") ? j["isPublic"].get<bool>() : true;
        room.clientCount = j.contains("clientCount") ? j["clientCount"].get<int>() : 0;
        return room;
    }
};

// Sound settings structure
struct SoundSettings {
    float masterVolume;
    std::unordered_map<std::string, float> soundVolumes; // SoundID string -> volume
    std::unordered_map<std::string, float> soundPans; // SoundID string -> pan
    std::vector<std::string> recentSounds; // SoundID strings
    std::string customSoundsDirectory;
    int audioDeviceIndex;

    // TTS Sound Information
    struct TtsSoundInfo {
        std::string id;
        std::string name;
        float volume;
        float pan;

        TtsSoundInfo(const std::string& id, const std::string& name, float volume = 1.0f, float pan = 0.0f)
            : id(id), name(name), volume(volume), pan(pan) {}
    };
    std::vector<TtsSoundInfo> ttsSounds;

    SoundSettings()
        : masterVolume(1.0f)
        , customSoundsDirectory("")
        , audioDeviceIndex(-1)
    {}

    void addRecentSound(const std::string& soundIdStr) {
        // Remove if already exists
        auto it = std::find(recentSounds.begin(), recentSounds.end(), soundIdStr);
        if (it != recentSounds.end()) {
            recentSounds.erase(it);
        }

        // Add to front
        recentSounds.insert(recentSounds.begin(), soundIdStr);

        // Limit size
        const int maxRecentSounds = 10;
        if (recentSounds.size() > maxRecentSounds) {
            recentSounds.resize(maxRecentSounds);
        }
    }
};


// TLS options for WebSocket connections
struct TlsOptions {
    bool verifyPeer;
    bool verifyHost;
    std::string caFile;
    std::string caPath;
    std::string certFile;
    std::string keyFile;
    bool enableServerCertAuth;

    TlsOptions()
        : verifyPeer(true)
        , verifyHost(true)
        , caFile("")
        , caPath("")
        , certFile("")
        , keyFile("")
        , enableServerCertAuth(true)
    {}
};

// WebSocket settings
struct WebSocketSettings {
    std::string serverUrl;
    bool autoConnect;
    bool enabled;
    std::string connectionStatus;
    std::string clientId;
    int pingInterval;
    bool autoReconnect;
    int reconnectInterval;
    int maxReconnectAttempts;
    bool logMessages;
    int maxLogEntries;
    std::vector<WebSocketLogEntry> messageLog;
    TlsOptions tlsOptions;
    std::string oldRoomId;



    // Room management
    std::string currentRoomId;
    std::string selectedRoomId; // Used temporarily for UI operations
    std::vector<RoomInfo> availableRooms;
    std::unordered_map<std::string, std::unordered_set<std::string>> roomSubscriptions;

    // Methods for room management
    void setCurrentRoom(const std::string& roomId) {
        currentRoomId = roomId;
    }

    std::string getCurrentRoom() const {
        return currentRoomId;
    }

    void updateAvailableRooms(const std::vector<RoomInfo>& rooms) {
        availableRooms = rooms;
    }

    const std::vector<RoomInfo>& getAvailableRooms() const {
        return availableRooms;
    }

    // Subscription management
    void subscribeToTimer(const std::string& timerId, const std::string& roomId) {
        if (roomId.empty()) return;

        // Create set for room if it doesn't exist
        if (roomSubscriptions.find(roomId) == roomSubscriptions.end()) {
            roomSubscriptions[roomId] = std::unordered_set<std::string>();
        }

        // Add timer to the room's subscription set
        roomSubscriptions[roomId].insert(timerId);
    }

    void unsubscribeFromTimer(const std::string& timerId, const std::string& roomId) {
        if (roomId.empty()) return;

        // Check if room exists in subscriptions
        auto roomIt = roomSubscriptions.find(roomId);
        if (roomIt != roomSubscriptions.end()) {
            // Remove timer from the room's subscription set
            roomIt->second.erase(timerId);

            // If room has no more subscriptions, remove it
            if (roomIt->second.empty()) {
                roomSubscriptions.erase(roomIt);
            }
        }
    }

    bool isSubscribedToTimer(const std::string& timerId, const std::string& roomId) const {
        if (roomId.empty()) return false;

        // Check if room exists in subscriptions
        auto roomIt = roomSubscriptions.find(roomId);
        if (roomIt != roomSubscriptions.end()) {
            // Check if timer is in the room's subscription set
            return roomIt->second.find(timerId) != roomIt->second.end();
        }

        return false;
    }

    // Get all subscriptions for a room
    std::unordered_set<std::string> getSubscriptionsForRoom(const std::string& roomId) const {
        if (roomId.empty()) return std::unordered_set<std::string>();

        auto roomIt = roomSubscriptions.find(roomId);
        if (roomIt != roomSubscriptions.end()) {
            return roomIt->second;
        }

        return std::unordered_set<std::string>();
    }

    // Cleanup non-existent rooms and timers
    void cleanupSubscriptions(const std::vector<RoomInfo>& existingRooms,
        const std::function<bool(const std::string&, const std::string&)>& timerExistsCallback) {
        // Create set of existing room IDs for quick lookup
        std::unordered_set<std::string> existingRoomIds;
        for (const auto& room : existingRooms) {
            existingRoomIds.insert(room.id);
        }

        // Remove subscriptions for non-existent rooms
        auto roomIt = roomSubscriptions.begin();
        while (roomIt != roomSubscriptions.end()) {
            if (existingRoomIds.find(roomIt->first) == existingRoomIds.end()) {
                // Room doesn't exist anymore, remove all its subscriptions
                roomIt = roomSubscriptions.erase(roomIt);
            }
            else {
                // Room exists, check timers
                if (timerExistsCallback) {
                    auto timerIt = roomIt->second.begin();
                    while (timerIt != roomIt->second.end()) {
                        if (!timerExistsCallback(*timerIt, roomIt->first)) {
                            // Timer doesn't exist, remove it
                            timerIt = roomIt->second.erase(timerIt);
                        }
                        else {
                            ++timerIt;
                        }
                    }

                    // If room has no more subscriptions, remove it
                    if (roomIt->second.empty()) {
                        roomIt = roomSubscriptions.erase(roomIt);
                    }
                    else {
                        ++roomIt;
                    }
                }
                else {
                    ++roomIt;
                }
            }
        }
    }

    void logMessage(const std::string& direction, const std::string& message) {
        if (!logMessages)
            return;

        messageLog.emplace_back(direction, message);

        // Trim log if over maximum size
        if (messageLog.size() > static_cast<size_t>(maxLogEntries)) {
            messageLog.erase(messageLog.begin(), messageLog.begin() + (messageLog.size() - maxLogEntries));
        }
    }

    void clearLog() {
        messageLog.clear();
    }

    void ensureClientId() {
        if (clientId.empty()) {
            // Generate a random client ID
            std::random_device rd;
            std::mt19937 gen(rd());
            std::uniform_int_distribution<> dis(0, 15);

            const char* digits = "0123456789abcdef";
            std::string uuid = "client-";
            for (int i = 0; i < 24; i++) {
                uuid += digits[dis(gen)];
            }

            clientId = uuid;
        }
    }

    WebSocketSettings()
        : serverUrl("ws://localhost:8080")
        , autoConnect(false)
        , enabled(false)
        , connectionStatus("Disconnected")
        , pingInterval(30000)
        , autoReconnect(true)
        , reconnectInterval(5000)
        , maxReconnectAttempts(5)
        , logMessages(true)
        , maxLogEntries(100)
        , currentRoomId("")
        , selectedRoomId("")
    {
        ensureClientId();
    }
};

// Main settings class
class Settings {
public:
    static void Load(const std::string& path);
    static void Save(const std::string& path);
    static void ScheduleSave(const std::string& path);
    static void InitializeDefaults();

    // Timer management
    static TimerData& AddTimer(const std::string& name, float duration);
    static void RemoveTimer(const std::string& id);
    static TimerData* FindTimer(const std::string& id);

    // Sound settings
    static void SetMasterVolume(float volume);
    static float GetMasterVolume();
    static void SetSoundVolume(int resourceId, float volume);
    static float GetSoundVolume(int resourceId);
    static void SetFileSoundVolume(const std::string& filePath, float volume);
    static float GetFileSoundVolume(const std::string& filePath);
    static void SetSoundPan(int soundId, float pan);
    static float GetSoundPan(int soundId);
    static void SetFileSoundPan(const std::string& filePath, float pan);
    static float GetFileSoundPan(const std::string& filePath);
    static void SetAudioDeviceIndex(int index);
    static int GetAudioDeviceIndex();
    static void SetCustomSoundsDirectory(const std::string& directory);
    static std::string GetCustomSoundsDirectory();
    static void AddRecentSound(const std::string& soundIdStr);
    static const std::vector<std::string>& GetRecentSounds();
    static void AddTtsSound(const std::string& soundId, const std::string& name, float volume = 1.0f, float pan = 0.0f);
    static const std::vector<SoundSettings::TtsSoundInfo>& GetTtsSounds();
    static bool LoadSavedTtsSounds();

    // WebSocket settings
    static void SetWebSocketServerUrl(const std::string& url);
    static std::string GetWebSocketServerUrl();
    static void SetWebSocketAutoConnect(bool autoConnect);
    static bool GetWebSocketAutoConnect();
    static void SetWebSocketEnabled(bool enabled);
    static bool GetWebSocketEnabled();
    static void SetWebSocketConnectionStatus(const std::string& status);
    static std::string GetWebSocketConnectionStatus();
    static void AddWebSocketLogEntry(const std::string& direction, const std::string& message);
    static const std::vector<WebSocketLogEntry>& GetWebSocketLog();
    static void ClearWebSocketLog();
    static std::string GetWebSocketClientId();

    // Room management methods
    static void SetCurrentRoom(const std::string& roomId);
    static std::string GetCurrentRoom();
    static void SetAvailableRooms(const std::vector<RoomInfo>& rooms);
    static std::vector<RoomInfo> GetAvailableRooms();
    static bool IsSubscribedToTimer(const std::string& timerId, const std::string& roomId = "");
    static void SubscribeToTimer(const std::string& timerId, const std::string& roomId = "");
    static void UnsubscribeFromTimer(const std::string& timerId, const std::string& roomId = "");
    static std::unordered_set<std::string> GetSubscriptionsForRoom(const std::string& roomId = "");
    static void CleanupSubscriptions();

public:
    // Public properties for window
    static ImVec2 windowPosition;
    static ImVec2 windowSize;
    static bool showTitle;
    static bool allowResize;
    static WindowColors colors;
    static std::vector<TimerData> timers;
    static std::unordered_set<std::string> usedIds;
    static SoundSettings sounds;
    static WebSocketSettings websocket;

    // For initialization ordering
    static bool isInitializing;

    // Allow RenderRoomsTab to access these variables directly with lock
    static std::mutex Mutex;

private:
    static json SettingsData;
    static std::mutex SaveMutex;
    static bool saveScheduled;
    static std::chrono::steady_clock::time_point lastSaveRequest;
    static const std::chrono::milliseconds saveCooldown;
};

// Global settings file path
extern std::string SettingsPath;