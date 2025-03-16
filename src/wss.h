#ifndef WEBSOCKET_CLIENT_H
#define WEBSOCKET_CLIENT_H

#include <string>
#include <functional>
#include <memory>
#include <mutex>
#include <atomic>
#include <unordered_set>
#include "nlohmann/json.hpp"

// Don't forward declare WebSocket++ types - we'll use the pimpl pattern fully
class WebSocketClientImpl;

using json = nlohmann::json;

class WebSocketClient {
public:
    WebSocketClient();
    ~WebSocketClient();

    // Connection management
    bool connect(const std::string& url);
    void disconnect();
    bool isConnected() const;

    // Safe shutdown method for addon unloading
    void safeShutdown();

    // Connection information
    bool isSecureConnection() const;
    std::string getConnectionDetails() const;
    std::string getConnectionUrl() const;

    // Send methods
    bool sendMessage(const std::string& message);
    bool sendJson(const json& jsonData);

    // Timer-specific message methods
    bool createTimer(const std::string& name, float duration);
    bool startTimer(const std::string& timerId);
    bool pauseTimer(const std::string& timerId);
    bool stopTimer(const std::string& timerId);

    // Ping to keep connection alive
    void ping();

    // Set connection status callback
    using StatusCallback = std::function<void(const std::string&)>;
    void setStatusCallback(StatusCallback callback);

    // Set message received callback
    using MessageCallback = std::function<void(const std::string&, const std::string&)>;
    void setMessageCallback(MessageCallback callback);

    // Room management
    bool joinRoom(const std::string& roomId, const std::string& password = "");
    bool createRoom(const std::string& name, bool isPublic, const std::string& password = "");
    bool leaveRoom();
    bool refreshRooms();

    // Timer subscription
    bool subscribeToTimer(const std::string& timerId, const std::string& roomId = "");
    bool unsubscribeFromTimer(const std::string& timerId, const std::string& roomId = "");
    void loadSubscribedTimersForRoom(const std::string& roomId, const std::unordered_set<std::string>& validTimerIds);
    void cleanupInvalidTimers(const std::unordered_set<std::string>& validTimerIds, const std::string& roomId);


private:
    // Private implementation (pimpl pattern)
    std::unique_ptr<WebSocketClientImpl> m_impl;

    // Connection information
    std::string m_url;
    std::atomic<bool> m_connected;
    std::atomic<bool> m_shuttingDown;
    std::atomic<bool> m_isShuttingDown;
    std::atomic<bool> m_isSecure;

    // Callbacks
    StatusCallback m_statusCallback;
    MessageCallback m_messageCallback;

    // Thread safety
    std::mutex m_sendMutex;
    std::mutex m_shutdownMutex;

    // Message handling
    void handleMessage(const std::string& message);

    // Set up event handlers
    void setupEventHandlers();


    // Helper for processing room-related messages
    void handleRoomMessage(const json& data);
    void handleTimerMessage(const json& data);

};

// Global WebSocket client instance
extern std::unique_ptr<WebSocketClient> g_WebSocketClient;

#endif // WEBSOCKET_CLIENT_H