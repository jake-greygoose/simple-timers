#ifndef WEBSOCKET_CLIENT_H
#define WEBSOCKET_CLIENT_H

#include <string>
#include <functional>
#include <memory>
#include <mutex>
#include <atomic>
#include "nlohmann/json.hpp"

// Forward declaration for implementation class
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

private:
    // Pimpl idiom to hide WebSocket++ implementation details
    std::unique_ptr<WebSocketClientImpl> m_impl;

    // Basic connection info that doesn't depend on WebSocket++
    std::string m_url;
    std::atomic<bool> m_connected;
    std::atomic<bool> m_shuttingDown;
    std::atomic<bool> m_isSecure;

    // Callback storage
    StatusCallback m_statusCallback;
    MessageCallback m_messageCallback;

    // Thread safety
    std::mutex m_sendMutex;

    // Message handling
    void handleMessage(const std::string& message);
};

// Global WebSocket client instance
extern std::unique_ptr<WebSocketClient> g_WebSocketClient;

#endif // WEBSOCKET_CLIENT_H