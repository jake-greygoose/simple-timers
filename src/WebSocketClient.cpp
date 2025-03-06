#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "WebSocketClient.h"
#include "settings.h"
#include "shared.h"
#include <sstream>
#include <chrono>

// Need to include Windows.h before any Winsock headers to avoid conflicts
#include <Windows.h>

// WebSocket++ includes
#include <websocketpp/config/asio_client.hpp>
#include <websocketpp/client.hpp>

// For convenience, using the same namespace approach as the working example
namespace asio = websocketpp::lib::asio;

// Create a TLS client configuration that can be customized
struct TLS_Client_Config : public websocketpp::config::asio_tls_client {
    static bool verify_certificate(bool preverified, asio::ssl::verify_context& ctx) {
        // This will be controlled by our settings
        return preverified;
    }
};

// Define client types using the custom config
using ws_client = websocketpp::client<websocketpp::config::asio_client>;
using wss_client = websocketpp::client<TLS_Client_Config>;
using connection_hdl = websocketpp::connection_hdl;

// Implementation class to hide websocketpp details
class WebSocketClientImpl {
public:
    std::unique_ptr<ws_client> ws;
    std::unique_ptr<wss_client> wss;
    connection_hdl handle;
    websocketpp::lib::shared_ptr<websocketpp::lib::thread> thread;

    WebSocketClientImpl() :
        ws(new ws_client()),
        wss(new wss_client()) {
    }
};

// Global instance
std::unique_ptr<WebSocketClient> g_WebSocketClient;

WebSocketClient::WebSocketClient()
    : m_impl(new WebSocketClientImpl()),
    m_connected(false),
    m_shuttingDown(false),
    m_isSecure(false) {

    // Configure standard WebSocket client
    m_impl->ws->clear_access_channels(websocketpp::log::alevel::all);
    m_impl->ws->set_access_channels(websocketpp::log::alevel::connect);
    m_impl->ws->set_access_channels(websocketpp::log::alevel::disconnect);
    m_impl->ws->set_access_channels(websocketpp::log::alevel::app);
    m_impl->ws->init_asio();

    // Configure secure WebSocket client
    m_impl->wss->clear_access_channels(websocketpp::log::alevel::all);
    m_impl->wss->set_access_channels(websocketpp::log::alevel::connect);
    m_impl->wss->set_access_channels(websocketpp::log::alevel::disconnect);
    m_impl->wss->set_access_channels(websocketpp::log::alevel::app);
    m_impl->wss->init_asio();

    // Set up WS callbacks
    m_impl->ws->set_open_handler([this](connection_hdl hdl) {
        m_impl->handle = hdl;
        m_connected = true;
        m_isSecure = false;

        if (m_statusCallback) {
            m_statusCallback("Connected");
        }
        Settings::SetWebSocketConnectionStatus("Connected");

        if (APIDefs) {
            APIDefs->Log(ELogLevel_INFO, ADDON_NAME, "WebSocket connected");
        }
        });

    m_impl->ws->set_close_handler([this](connection_hdl hdl) {
        m_connected = false;

        if (m_statusCallback) {
            m_statusCallback("Disconnected");
        }
        Settings::SetWebSocketConnectionStatus("Disconnected");

        if (APIDefs) {
            APIDefs->Log(ELogLevel_INFO, ADDON_NAME, "WebSocket disconnected");
        }
        });

    m_impl->ws->set_fail_handler([this](connection_hdl hdl) {
        m_connected = false;

        ws_client::connection_ptr con = m_impl->ws->get_con_from_hdl(hdl);
        std::string errorMsg = "Error: " + con->get_ec().message();

        if (m_statusCallback) {
            m_statusCallback(errorMsg);
        }
        Settings::SetWebSocketConnectionStatus(errorMsg);

        if (APIDefs) {
            APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, errorMsg.c_str());
        }
        });

    m_impl->ws->set_message_handler([this](connection_hdl hdl, ws_client::message_ptr msg) {
        std::string payload = msg->get_payload();

        // Log received message
        if (m_messageCallback) {
            m_messageCallback("received", payload);
        }
        Settings::AddWebSocketLogEntry("received", payload);

        // Process the message
        handleMessage(payload);
        });

    // Set up WSS callbacks (similar to WS)
    m_impl->wss->set_open_handler([this](connection_hdl hdl) {
        m_impl->handle = hdl;
        m_connected = true;
        m_isSecure = true;

        if (m_statusCallback) {
            m_statusCallback("Connected (Secure)");
        }
        Settings::SetWebSocketConnectionStatus("Connected (Secure)");

        if (APIDefs) {
            APIDefs->Log(ELogLevel_INFO, ADDON_NAME, "Secure WebSocket connected");
        }
        });

    m_impl->wss->set_close_handler([this](connection_hdl hdl) {
        m_connected = false;

        if (m_statusCallback) {
            m_statusCallback("Disconnected");
        }
        Settings::SetWebSocketConnectionStatus("Disconnected");

        if (APIDefs) {
            APIDefs->Log(ELogLevel_INFO, ADDON_NAME, "WebSocket disconnected");
        }
        });

    m_impl->wss->set_fail_handler([this](connection_hdl hdl) {
        m_connected = false;

        wss_client::connection_ptr con = m_impl->wss->get_con_from_hdl(hdl);
        std::string errorMsg = "Error: " + con->get_ec().message();

        if (m_statusCallback) {
            m_statusCallback(errorMsg);
        }
        Settings::SetWebSocketConnectionStatus(errorMsg);

        if (APIDefs) {
            APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, errorMsg.c_str());
        }
        });

    m_impl->wss->set_message_handler([this](connection_hdl hdl, wss_client::message_ptr msg) {
        std::string payload = msg->get_payload();

        // Log received message
        if (m_messageCallback) {
            m_messageCallback("received", payload);
        }
        Settings::AddWebSocketLogEntry("received", payload);

        // Process the message
        handleMessage(payload);
        });

    // Set the TLS init handler for WSS - simplified approach
    m_impl->wss->set_tls_init_handler([](websocketpp::connection_hdl) {
        auto ctx = websocketpp::lib::make_shared<asio::ssl::context>(asio::ssl::context::tlsv12);

        try {
            // Basic secure defaults
            ctx->set_options(
                asio::ssl::context::default_workarounds |
                asio::ssl::context::no_sslv2 |
                asio::ssl::context::no_sslv3 |
                asio::ssl::context::single_dh_use
            );

            // Get TLS options from settings
            const auto& tlsOpts = Settings::websocket.tlsOptions;

            // Set verification mode based on settings
            ctx->set_verify_mode(tlsOpts.verifyPeer ?
                asio::ssl::verify_peer :
                asio::ssl::verify_none);

            // Load default verification paths
            ctx->set_default_verify_paths();

            // Optionally set custom certificate paths
            if (!tlsOpts.caFile.empty()) {
                ctx->load_verify_file(tlsOpts.caFile);
            }

            if (!tlsOpts.caPath.empty()) {
                ctx->add_verify_path(tlsOpts.caPath);
            }

            if (APIDefs) {
                APIDefs->Log(ELogLevel_INFO, ADDON_NAME, "TLS context initialized");
            }
        }
        catch (const std::exception& e) {
            if (APIDefs) {
                char errorMsg[256];
                sprintf_s(errorMsg, "TLS initialization error: %s", e.what());
                APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, errorMsg);
            }
        }

        return ctx;
        });
}

WebSocketClient::~WebSocketClient() {
    m_shuttingDown = true;
    disconnect();
}

bool WebSocketClient::connect(const std::string& url) {
    if (m_connected) {
        disconnect();
    }

    try {
        m_url = url;

        // Determine if we're using secure connection
        bool isSecure = (url.substr(0, 6) == "wss://");

        if (isSecure) {
            // For secure WebSocket
            websocketpp::lib::error_code ec;
            wss_client::connection_ptr con = m_impl->wss->get_connection(url, ec);

            if (ec) {
                if (m_statusCallback) {
                    m_statusCallback(std::string("Connection error: ") + ec.message());
                }
                Settings::SetWebSocketConnectionStatus(std::string("Connection error: ") + ec.message());

                if (APIDefs) {
                    char errorMsg[256];
                    sprintf_s(errorMsg, "Secure WebSocket connection error: %s", ec.message().c_str());
                    APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, errorMsg);
                }
                return false;
            }

            // Connect
            m_impl->wss->connect(con);

            // Start the ASIO io_service run loop in a separate thread
            m_impl->thread = websocketpp::lib::make_shared<websocketpp::lib::thread>([this]() {
                try {
                    m_impl->wss->run();
                }
                catch (const std::exception& e) {
                    if (APIDefs) {
                        char errorMsg[256];
                        sprintf_s(errorMsg, "WSS client thread error: %s", e.what());
                        APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, errorMsg);
                    }
                }
                });
        }
        else {
            // For standard WebSocket
            websocketpp::lib::error_code ec;
            ws_client::connection_ptr con = m_impl->ws->get_connection(url, ec);

            if (ec) {
                if (m_statusCallback) {
                    m_statusCallback(std::string("Connection error: ") + ec.message());
                }
                Settings::SetWebSocketConnectionStatus(std::string("Connection error: ") + ec.message());

                if (APIDefs) {
                    char errorMsg[256];
                    sprintf_s(errorMsg, "WebSocket connection error: %s", ec.message().c_str());
                    APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, errorMsg);
                }
                return false;
            }

            // Connect
            m_impl->ws->connect(con);

            // Start the ASIO io_service run loop in a separate thread
            m_impl->thread = websocketpp::lib::make_shared<websocketpp::lib::thread>([this]() {
                try {
                    m_impl->ws->run();
                }
                catch (const std::exception& e) {
                    if (APIDefs) {
                        char errorMsg[256];
                        sprintf_s(errorMsg, "WS client thread error: %s", e.what());
                        APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, errorMsg);
                    }
                }
                });
        }

        return true;
    }
    catch (const std::exception& e) {
        if (m_statusCallback) {
            m_statusCallback(std::string("Connection error: ") + e.what());
        }
        Settings::SetWebSocketConnectionStatus(std::string("Connection error: ") + e.what());

        if (APIDefs) {
            char errorMsg[256];
            sprintf_s(errorMsg, "WebSocket connection error: %s", e.what());
            APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, errorMsg);
        }

        return false;
    }
}

void WebSocketClient::disconnect() {
    try {
        if (m_connected) {
            if (m_isSecure) {
                // Close the secure connection
                websocketpp::lib::error_code ec;

                wss_client::connection_ptr con = m_impl->wss->get_con_from_hdl(m_impl->handle, ec);
                if (!ec) {
                    con->close(websocketpp::close::status::normal, "Client disconnecting", ec);
                }

                // Stop the client
                m_impl->wss->stop_perpetual();
            }
            else {
                // Close the standard connection
                websocketpp::lib::error_code ec;

                ws_client::connection_ptr con = m_impl->ws->get_con_from_hdl(m_impl->handle, ec);
                if (!ec) {
                    con->close(websocketpp::close::status::normal, "Client disconnecting", ec);
                }

                // Stop the client
                m_impl->ws->stop_perpetual();
            }

            m_connected = false;

            // Wait for thread to complete
            if (m_impl->thread && m_impl->thread->joinable()) {
                m_impl->thread->join();
            }
        }
    }
    catch (const std::exception& e) {
        if (APIDefs) {
            char errorMsg[256];
            sprintf_s(errorMsg, "Error during WebSocket disconnect: %s", e.what());
            APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, errorMsg);
        }
    }
}

bool WebSocketClient::isConnected() const {
    return m_connected;
}

bool WebSocketClient::isSecureConnection() const {
    return m_isSecure;
}

std::string WebSocketClient::getConnectionDetails() const {
    if (!m_connected) {
        return "Not connected";
    }

    if (m_isSecure) {
        return "Secure connection (WSS)";
    }
    else {
        return "Standard connection (WS)";
    }
}

std::string WebSocketClient::getConnectionUrl() const {
    return m_url;
}

bool WebSocketClient::sendMessage(const std::string& message) {
    if (!m_connected) return false;

    try {
        std::lock_guard<std::mutex> lock(m_sendMutex);

        // Log sent message
        if (m_messageCallback) {
            m_messageCallback("sent", message);
        }
        Settings::AddWebSocketLogEntry("sent", message);

        // Send the message using the appropriate client
        websocketpp::lib::error_code ec;

        if (m_isSecure) {
            m_impl->wss->send(m_impl->handle, message, websocketpp::frame::opcode::text, ec);
        }
        else {
            m_impl->ws->send(m_impl->handle, message, websocketpp::frame::opcode::text, ec);
        }

        if (ec) {
            if (APIDefs) {
                char errorMsg[256];
                sprintf_s(errorMsg, "WebSocket send error: %s", ec.message().c_str());
                APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, errorMsg);
            }
            return false;
        }

        return true;
    }
    catch (const std::exception& e) {
        if (APIDefs) {
            char errorMsg[256];
            sprintf_s(errorMsg, "WebSocket send error: %s", e.what());
            APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, errorMsg);
        }
        return false;
    }
}

bool WebSocketClient::sendJson(const json& jsonData) {
    try {
        std::string jsonStr = jsonData.dump();
        return sendMessage(jsonStr);
    }
    catch (const std::exception& e) {
        if (APIDefs) {
            char errorMsg[256];
            sprintf_s(errorMsg, "WebSocket JSON serialization error: %s", e.what());
            APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, errorMsg);
        }
        return false;
    }
}

bool WebSocketClient::createTimer(const std::string& name, float duration) {
    json message = {
        {"type", "create_timer"},
        {"name", name},
        {"duration", duration}
    };
    return sendJson(message);
}

bool WebSocketClient::startTimer(const std::string& timerId) {
    json message = {
        {"type", "start_timer"},
        {"timerId", timerId}
    };
    return sendJson(message);
}

bool WebSocketClient::pauseTimer(const std::string& timerId) {
    json message = {
        {"type", "pause_timer"},
        {"timerId", timerId}
    };
    return sendJson(message);
}

bool WebSocketClient::stopTimer(const std::string& timerId) {
    json message = {
        {"type", "stop_timer"},
        {"timerId", timerId}
    };
    return sendJson(message);
}

void WebSocketClient::ping() {
    json message = {
        {"type", "ping"},
        {"timestamp", std::chrono::system_clock::now().time_since_epoch().count()}
    };
    sendJson(message);
}

void WebSocketClient::setStatusCallback(StatusCallback callback) {
    m_statusCallback = callback;
}

void WebSocketClient::setMessageCallback(MessageCallback callback) {
    m_messageCallback = callback;
}

void WebSocketClient::handleMessage(const std::string& message) {
    try {
        json data = json::parse(message);
        std::string type = data.value("type", "");

        if (type == "init") {
            // Handle initial state with all timers
            if (APIDefs) {
                APIDefs->Log(ELogLevel_INFO, ADDON_NAME, "Received initial timer state from server");
            }
            // Process initial timers state
        }
        else if (type == "timer_created") {
            // Handle timer created
            if (APIDefs) {
                APIDefs->Log(ELogLevel_INFO, ADDON_NAME, "Timer created on server");
            }
            // Create the timer locally
        }
        else if (type == "timer_started") {
            // Handle timer started
            if (APIDefs) {
                APIDefs->Log(ELogLevel_INFO, ADDON_NAME, "Timer started on server");
            }
            // Start the timer locally
        }
        else if (type == "timer_paused") {
            // Handle timer paused
            if (APIDefs) {
                APIDefs->Log(ELogLevel_INFO, ADDON_NAME, "Timer paused on server");
            }
            // Pause the timer locally
        }
        else if (type == "timer_completed") {
            // Handle timer completed
            if (APIDefs) {
                APIDefs->Log(ELogLevel_INFO, ADDON_NAME, "Timer completed on server");
            }
            // Complete the timer locally
        }
        else if (type == "pong") {
            // Handle ping response
            if (APIDefs) {
                APIDefs->Log(ELogLevel_DEBUG, ADDON_NAME, "Received pong from server");
            }
        }
    }
    catch (const std::exception& e) {
        if (APIDefs) {
            char errorMsg[256];
            sprintf_s(errorMsg, "Error processing WebSocket message: %s", e.what());
            APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, errorMsg);
        }
    }
}