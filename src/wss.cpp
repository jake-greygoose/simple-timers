#define WIN32_LEAN_AND_MEAN
#define NOMINMAX


// First include our header
#include "wss.h"
#include "settings.h"
#include "shared.h"

// Standard includes
#include <sstream>
#include <chrono>
#include <Windows.h>
#include <thread>
#include <future>

// Only after our header, include the WebSocket++ headers
#include <websocketpp/config/asio_client.hpp>
#include <websocketpp/client.hpp>

// Define client types
using ws_client = websocketpp::client<websocketpp::config::asio_client>;
using wss_client = websocketpp::client<websocketpp::config::asio_tls_client>;
using connection_hdl = websocketpp::connection_hdl;

// Implementation class
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
    m_isShuttingDown(false),
    m_isSecure(false) {

    if (APIDefs) {
        APIDefs->Log(ELogLevel_INFO, ADDON_NAME, "Creating WebSocket client");
    }

    try {
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

        // Set up event handlers
        setupEventHandlers();

        if (APIDefs) {
            APIDefs->Log(ELogLevel_INFO, ADDON_NAME, "WebSocket client initialized successfully");
        }
    }
    catch (const std::exception& e) {
        if (APIDefs) {
            char errorMsg[256];
            sprintf_s(errorMsg, "Exception during WebSocket client initialization: %s", e.what());
            APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, errorMsg);
        }
    }
    catch (...) {
        if (APIDefs) {
            APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, "Unknown exception during WebSocket client initialization");
        }
    }
}

static int sni_callback(SSL* ssl, int* ad, void* arg) {
    const char* host = static_cast<const char*>(arg);
    if (host && host[0] != '\0') {
        SSL_set_tlsext_host_name(ssl, host);
    }
    return SSL_TLSEXT_ERR_OK;
}


void WebSocketClient::setupEventHandlers() {
    if (APIDefs) {
        APIDefs->Log(ELogLevel_DEBUG, ADDON_NAME, "Setting up WebSocket event handlers");
    }

    // WS open handler
    m_impl->ws->set_open_handler([this](connection_hdl hdl) {
        // Skip if shutting down
        if (m_isShuttingDown) {
            return;
        }

        m_impl->handle = hdl;
        m_connected = true;
        m_isSecure = false;

        if (APIDefs) {
            APIDefs->Log(ELogLevel_INFO, ADDON_NAME, "WebSocket connection established successfully (WS)");
        }

        if (m_statusCallback) {
            m_statusCallback("Connected");
        }
        Settings::SetWebSocketConnectionStatus("Connected");
        });

    // WS close handler
    m_impl->ws->set_close_handler([this](connection_hdl hdl) {
        // Skip if shutting down
        if (m_isShuttingDown) {
            return;
        }

        m_connected = false;

        if (APIDefs) {
            APIDefs->Log(ELogLevel_INFO, ADDON_NAME, "WebSocket connection closed (WS)");
        }

        if (m_statusCallback) {
            m_statusCallback("Disconnected");
        }
        Settings::SetWebSocketConnectionStatus("Disconnected");
        });

    // ws fail handler

// Enhanced WSS fail handler with better diagnostics
    m_impl->wss->set_fail_handler([this](connection_hdl hdl) {
        // Skip if shutting down
        if (m_isShuttingDown) {
            return;
        }

        m_connected = false;

        try {
            wss_client::connection_ptr con = m_impl->wss->get_con_from_hdl(hdl);
            std::string errorMsg = con->get_ec().message();
            int errorCode = con->get_ec().value();

            // Get more details about the error
            std::string errorCategory = con->get_ec().category().name();

            if (APIDefs) {
                char detailedMsg[512];
                sprintf_s(detailedMsg, "WSS connection failure - Code: %d, Category: %s, Message: %s",
                    errorCode, errorCategory.c_str(), errorMsg.c_str());
                APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, detailedMsg);
            }

            // Detect certificate-related errors
            bool isCertificateError =
                errorMsg.find("certificate") != std::string::npos ||
                errorMsg.find("SSL") != std::string::npos ||
                errorMsg.find("TLS") != std::string::npos ||
                errorCategory.find("ssl") != std::string::npos;

            if (isCertificateError) {
                // Provide more specific error messages based on common SSL errors
                std::string friendlyMessage;

                if (errorMsg.find("certificate verify failed") != std::string::npos) {
                    friendlyMessage = "The server's certificate could not be verified. It might not be trusted by your system.";
                }
                else if (errorMsg.find("certificate has expired") != std::string::npos) {
                    friendlyMessage = "The server's certificate has expired.";
                }
                else if (errorMsg.find("certificate is not yet valid") != std::string::npos) {
                    friendlyMessage = "The server's certificate is not yet valid (future date).";
                }
                else if (errorMsg.find("self signed certificate") != std::string::npos) {
                    friendlyMessage = "The server is using a self-signed certificate which is not trusted.";
                }
                else if (errorMsg.find("certificate chain") != std::string::npos) {
                    friendlyMessage = "There's an issue with the server's certificate chain.";
                }
                else if (errorMsg.find("handshake") != std::string::npos) {
                    friendlyMessage = "TLS handshake failed. This could be due to protocol version mismatch or cipher suite incompatibility.";
                }
                else {
                    friendlyMessage = "A certificate/TLS error occurred: " + errorMsg;
                }

                // Add user guidance
                friendlyMessage += "\n\nPossible solutions:\n"
                    "- Check if the server URL is correct\n"
                    "- Try disabling certificate verification in settings (only for trusted connections)\n"
                    "- Try disabling hostname verification if you're using an IP address\n";

                if (APIDefs) {
                    APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, friendlyMessage.c_str());
                }

                // Update status
                std::string shortMsg = "Certificate Error: " + errorMsg;
                if (shortMsg.length() > 80) {
                    shortMsg = shortMsg.substr(0, 77) + "...";
                }

                if (m_statusCallback) {
                    m_statusCallback(shortMsg);
                }
                Settings::SetWebSocketConnectionStatus(shortMsg);

                // Set detailed message for display in UI
                //Settings::websocket.lastDetailedError = friendlyMessage;
            }
            else {
                // Standard error handling for non-certificate errors
                std::string errorMsg = "Secure connection failed: " + con->get_ec().message();

                if (APIDefs) {
                    APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, errorMsg.c_str());
                }

                if (m_statusCallback) {
                    m_statusCallback(errorMsg);
                }
                Settings::SetWebSocketConnectionStatus(errorMsg);
            }
        }
        catch (const std::exception& e) {
            // Log details if there's an exception in the error handler itself
            if (APIDefs) {
                char errorMsg[256];
                sprintf_s(errorMsg, "Error handling WSS failure: %s", e.what());
                APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, errorMsg);
            }

            // Use a generic message if we can't get detailed info
            if (m_statusCallback) {
                m_statusCallback("Secure connection failed with unspecified error");
            }
            Settings::SetWebSocketConnectionStatus("Secure connection failed with unspecified error");
        }
        catch (...) {
            // Avoid crashes if getting error message fails
            if (APIDefs) {
                APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, "Secure connection failed with unspecified error");
            }

            if (m_statusCallback) {
                m_statusCallback("Secure connection failed with unspecified error");
            }
            Settings::SetWebSocketConnectionStatus("Secure connection failed with unspecified error");
        }
        });

    // WS message handler
    m_impl->ws->set_message_handler([this](connection_hdl hdl, ws_client::message_ptr msg) {
        // Skip if shutting down
        if (m_isShuttingDown) {
            return;
        }

        std::string payload = msg->get_payload();

        if (APIDefs) {
            char logMsg[256];
            sprintf_s(logMsg, "Received message: %s",
                payload.length() > 100 ? (payload.substr(0, 97) + "...").c_str() : payload.c_str());
            APIDefs->Log(ELogLevel_DEBUG, ADDON_NAME, logMsg);
        }

        // Log received message
        if (m_messageCallback) {
            m_messageCallback("received", payload);
        }
        Settings::AddWebSocketLogEntry("received", payload);

        // Process the message
        handleMessage(payload);
        });

    // WSS open handler
    m_impl->wss->set_open_handler([this](connection_hdl hdl) {
        // Skip if shutting down
        if (m_isShuttingDown) {
            return;
        }

        m_impl->handle = hdl;
        m_connected = true;
        m_isSecure = true;

        if (APIDefs) {
            APIDefs->Log(ELogLevel_INFO, ADDON_NAME, "Secure WebSocket connection established successfully (WSS)");
        }

        if (m_statusCallback) {
            m_statusCallback("Connected (Secure)");
        }
        Settings::SetWebSocketConnectionStatus("Connected (Secure)");
        });

    // WSS close handler
    m_impl->wss->set_close_handler([this](connection_hdl hdl) {
        // Skip if shutting down
        if (m_isShuttingDown) {
            return;
        }

        m_connected = false;

        if (APIDefs) {
            APIDefs->Log(ELogLevel_INFO, ADDON_NAME, "Secure WebSocket connection closed (WSS)");
        }

        if (m_statusCallback) {
            m_statusCallback("Disconnected");
        }
        Settings::SetWebSocketConnectionStatus("Disconnected");
        });

    // WSS fail handler
    m_impl->wss->set_fail_handler([this](connection_hdl hdl) {
        // Skip if shutting down
        if (m_isShuttingDown) {
            return;
        }

        m_connected = false;

        try {
            wss_client::connection_ptr con = m_impl->wss->get_con_from_hdl(hdl);
            std::string errorMsg = "Secure connection failed: " + con->get_ec().message();

            if (APIDefs) {
                APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, errorMsg.c_str());
            }

            if (m_statusCallback) {
                m_statusCallback(errorMsg);
            }
            Settings::SetWebSocketConnectionStatus(errorMsg);
        }
        catch (...) {
            // Avoid crashes if getting error message fails
            if (APIDefs) {
                APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, "Secure connection failed with unspecified error");
            }
        }
        });

    // WSS message handler
    m_impl->wss->set_message_handler([this](connection_hdl hdl, wss_client::message_ptr msg) {
        // Skip if shutting down
        if (m_isShuttingDown) {
            return;
        }

        std::string payload = msg->get_payload();

        if (APIDefs) {
            char logMsg[256];
            sprintf_s(logMsg, "Received secure message: %s",
                payload.length() > 100 ? (payload.substr(0, 97) + "...").c_str() : payload.c_str());
            APIDefs->Log(ELogLevel_DEBUG, ADDON_NAME, logMsg);
        }

        // Log received message
        if (m_messageCallback) {
            m_messageCallback("received", payload);
        }
        Settings::AddWebSocketLogEntry("received", payload);

        // Process the message
        handleMessage(payload);
        });

    m_impl->wss->set_tls_init_handler(
        [this](websocketpp::connection_hdl hdl) -> websocketpp::lib::shared_ptr<asio::ssl::context> {
            // Use the client context type
            auto ctx = websocketpp::lib::make_shared<asio::ssl::context>(asio::ssl::context::tls_client);
            try {
                // Extract hostname from m_url
                std::string hostname = "localhost";
                if (!m_url.empty() && m_url.substr(0, 6) == "wss://") {
                    hostname = m_url.substr(6);
                    size_t pos = hostname.find('/');
                    if (pos != std::string::npos) {
                        hostname = hostname.substr(0, pos);
                    }
                    pos = hostname.find(':');
                    if (pos != std::string::npos) {
                        hostname = hostname.substr(0, pos);
                    }
                }

                // Set SNI using the static callback
                SSL_CTX_set_tlsext_servername_callback(ctx->native_handle(), sni_callback);
                // Use a static variable so that the pointer passed remains valid
                static std::string s_hostname = hostname;
                SSL_CTX_set_tlsext_servername_arg(ctx->native_handle(), (void*)s_hostname.c_str());

                // Set standard TLS options
                ctx->set_options(
                    asio::ssl::context::default_workarounds |
                    asio::ssl::context::no_sslv2 |
                    asio::ssl::context::no_sslv3 |
                    asio::ssl::context::single_dh_use
                );

                // Get TLS settings from global settings
                bool verifyPeer = Settings::websocket.tlsOptions.verifyPeer;
                bool verifyHost = Settings::websocket.tlsOptions.verifyHost;
                std::string caFile = Settings::websocket.tlsOptions.caFile;
                std::string caPath = Settings::websocket.tlsOptions.caPath;

                if (verifyPeer) {
                    if (APIDefs) {
                        char logMsg[256];
                        sprintf_s(logMsg, "Enabling certificate verification with CA file: %s",
                            !caFile.empty() ? caFile.c_str() : "NONE");
                        APIDefs->Log(ELogLevel_INFO, ADDON_NAME, logMsg);
                    }

                    // Enable certificate verification
                    ctx->set_verify_mode(asio::ssl::verify_peer);

                    // Load CA certificate if specified
                    if (!caFile.empty()) {
                        try {
                            ctx->load_verify_file(caFile);

                            if (APIDefs) {
                                APIDefs->Log(ELogLevel_INFO, ADDON_NAME, "CA certificate loaded successfully");
                            }
                        }
                        catch (const std::exception& e) {
                            if (APIDefs) {
                                char errorMsg[256];
                                sprintf_s(errorMsg, "Error loading CA certificate: %s", e.what());
                                APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, errorMsg);
                            }
                            // Fall back to no verification if CA file fails to load
                            ctx->set_verify_mode(asio::ssl::verify_none);
                        }
                    }

                    // Load CA path if specified
                    if (!caPath.empty()) {
                        try {
                            // Try the most common method name
                            ctx->add_verify_path(caPath);

                            if (APIDefs) {
                                APIDefs->Log(ELogLevel_INFO, ADDON_NAME, "CA path loaded successfully");
                            }
                        }
                        catch (const std::exception& e) {
                            if (APIDefs) {
                                char errorMsg[256];
                                sprintf_s(errorMsg, "Error loading CA path: %s", e.what());
                                APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, errorMsg);
                            }

                            // If the method fails, log a message but don't affect overall verification
                            if (APIDefs) {
                                APIDefs->Log(ELogLevel_WARNING, ADDON_NAME,
                                    "CA path directories not supported by this version of the library");
                            }
                        }
                    }

                    // Set hostname verification if enabled
                    if (verifyHost) {
                        auto verifier = [hostname](bool preverified, asio::ssl::verify_context& verify_ctx) -> bool {
                            if (!preverified)
                                return false;
                            asio::ssl::rfc2818_verification v(hostname);
                            return v(preverified, verify_ctx);
                        };
                        ctx->set_verify_callback(verifier);

                        if (APIDefs) {
                            APIDefs->Log(ELogLevel_INFO, ADDON_NAME, "Hostname verification enabled");
                        }
                    }
                }
                else {
                    // Disable certificate verification
                    ctx->set_verify_mode(asio::ssl::verify_none);

                    if (APIDefs) {
                        APIDefs->Log(ELogLevel_INFO, ADDON_NAME, "Certificate verification disabled");
                    }
                }

                return ctx;
            }
            catch (const std::exception& e) {
                if (APIDefs) {
                    char logMsg[256];
                    sprintf_s(logMsg, "TLS initialization error: %s", e.what());
                    APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, logMsg);
                }
                throw;
            }
        }
    );

    if (APIDefs) {
        APIDefs->Log(ELogLevel_DEBUG, ADDON_NAME, "WebSocket event handlers set up successfully");
    }
}

WebSocketClient::~WebSocketClient() {
    if (APIDefs) {
        APIDefs->Log(ELogLevel_INFO, ADDON_NAME, "Destroying WebSocket client");
    }

    safeShutdown();
}

void WebSocketClient::safeShutdown() {
    std::lock_guard<std::mutex> lock(m_shutdownMutex);

    // Prevent multiple shutdown attempts
    if (m_isShuttingDown) {
        if (APIDefs) {
            APIDefs->Log(ELogLevel_INFO, ADDON_NAME, "WebSocket shutdown already in progress");
        }
        return;
    }

    // Set shutdown flag before doing anything else
    m_isShuttingDown = true;

    try {
        if (APIDefs) {
            APIDefs->Log(ELogLevel_INFO, ADDON_NAME, "Starting safe WebSocket shutdown");
        }

        // Disconnect (this will close any active connections)
        disconnect();

        // Additional cleanup for any pending operations
        if (m_impl) {
            // Stop both client io_services to cancel any pending operations
            if (m_impl->ws) {
                try {
                    m_impl->ws->stop();
                    if (APIDefs) {
                        APIDefs->Log(ELogLevel_DEBUG, ADDON_NAME, "WS client stopped");
                    }
                }
                catch (...) {
                    if (APIDefs) {
                        APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, "Error stopping WS client");
                    }
                }
            }

            if (m_impl->wss) {
                try {
                    m_impl->wss->stop();
                    if (APIDefs) {
                        APIDefs->Log(ELogLevel_DEBUG, ADDON_NAME, "WSS client stopped");
                    }
                }
                catch (...) {
                    if (APIDefs) {
                        APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, "Error stopping WSS client");
                    }
                }
            }

            // Ensure thread is joined if it exists
            if (m_impl->thread && m_impl->thread->joinable()) {
                try {
                    if (APIDefs) {
                        APIDefs->Log(ELogLevel_DEBUG, ADDON_NAME, "Joining WebSocket thread");
                    }

                    // Use a future with timeout to safely join the thread
                    std::promise<void> threadFinished;
                    auto threadFinishedFuture = threadFinished.get_future();

                    std::thread joinThread([this, &threadFinished]() {
                        try {
                            m_impl->thread->join();
                            threadFinished.set_value();
                        }
                        catch (...) {
                            try {
                                threadFinished.set_exception(std::current_exception());
                            }
                            catch (...) {}
                        }
                        });

                    // Wait for thread to join with timeout
                    auto status = threadFinishedFuture.wait_for(std::chrono::seconds(3));

                    if (status == std::future_status::timeout) {
                        if (APIDefs) {
                            APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, "WebSocket thread join timed out");
                        }
                        // We can't safely detach here, but we've at least tried to join
                    }
                    else {
                        if (APIDefs) {
                            APIDefs->Log(ELogLevel_DEBUG, ADDON_NAME, "WebSocket thread joined successfully");
                        }
                    }

                    if (joinThread.joinable()) {
                        joinThread.join();
                    }

                }
                catch (const std::exception& e) {
                    if (APIDefs) {
                        char errorMsg[256];
                        sprintf_s(errorMsg, "Error joining WebSocket thread: %s", e.what());
                        APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, errorMsg);
                    }
                }
                catch (...) {
                    if (APIDefs) {
                        APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, "Unknown error joining WebSocket thread");
                    }
                }
            }
        }

        if (APIDefs) {
            APIDefs->Log(ELogLevel_INFO, ADDON_NAME, "WebSocket safe shutdown completed");
        }
    }
    catch (const std::exception& e) {
        if (APIDefs) {
            char errorMsg[256];
            sprintf_s(errorMsg, "Error during WebSocket safe shutdown: %s", e.what());
            APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, errorMsg);
        }
    }
    catch (...) {
        if (APIDefs) {
            APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, "Unknown error during WebSocket safe shutdown");
        }
    }
}

bool WebSocketClient::connect(const std::string& url) {
    if (m_isShuttingDown) {
        if (APIDefs) {
            APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, "Cannot connect: Client is shutting down");
        }
        return false;
    }

    if (m_connected) {
        if (APIDefs) {
            APIDefs->Log(ELogLevel_INFO, ADDON_NAME, "Already connected, disconnecting first");
        }
        disconnect();
    }

    try {
        m_url = url;

        // Log connection attempt
        if (APIDefs) {
            char logMsg[256];
            sprintf_s(logMsg, "Attempting to connect to %s", url.c_str());
            APIDefs->Log(ELogLevel_INFO, ADDON_NAME, logMsg);
        }

        // Determine if we're using secure connection
        bool isSecure = (url.substr(0, 6) == "wss://");
        if (APIDefs) {
            APIDefs->Log(ELogLevel_INFO, ADDON_NAME, isSecure ? "Using secure connection (WSS)" : "Using standard connection (WS)");
        }

        if (isSecure) {
            // Extract hostname for TLS verification
            std::string hostname = url.substr(6);
            size_t pathPos = hostname.find('/');
            if (pathPos != std::string::npos) {
                hostname = hostname.substr(0, pathPos);
            }

            // Check for port in hostname and remove if present
            size_t portPos = hostname.find(':');
            if (portPos != std::string::npos) {
                hostname = hostname.substr(0, portPos);
            }

            if (APIDefs) {
                char logMsg[256];
                sprintf_s(logMsg, "Extracted hostname for TLS: %s", hostname.c_str());
                APIDefs->Log(ELogLevel_DEBUG, ADDON_NAME, logMsg);
            }

            // For secure WebSocket
            websocketpp::lib::error_code ec;

            // Create connection with the hostname
            wss_client::connection_ptr con = m_impl->wss->get_connection(url, ec);
            if (ec) {
                std::string errorMsg = "Connection initialization error: " + ec.message();
                if (APIDefs) {
                    APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, errorMsg.c_str());
                }

                if (m_statusCallback) {
                    m_statusCallback(errorMsg);
                }
                Settings::SetWebSocketConnectionStatus(errorMsg);
                return false;
            }

            // Set additional connection options for debugging
            con->set_open_handshake_timeout(10000); // 10 seconds

            if (APIDefs) {
                APIDefs->Log(ELogLevel_DEBUG, ADDON_NAME, "Initiating secure connection...");
            }

            // Connect
            m_impl->wss->connect(con);

            // Update intermediate status
            Settings::SetWebSocketConnectionStatus("Connecting (Secure)...");
            if (m_statusCallback) {
                m_statusCallback("Connecting (Secure)...");
            }

            // Start the ASIO io_service run loop in a separate thread
            if (APIDefs) {
                APIDefs->Log(ELogLevel_DEBUG, ADDON_NAME, "Starting secure WebSocket thread");
            }

            m_impl->thread = websocketpp::lib::make_shared<websocketpp::lib::thread>([this]() {
                try {
                    if (APIDefs) {
                        APIDefs->Log(ELogLevel_DEBUG, ADDON_NAME, "WSS client thread started");
                    }
                    m_impl->wss->run();
                    if (APIDefs) {
                        APIDefs->Log(ELogLevel_DEBUG, ADDON_NAME, "WSS client thread completed normally");
                    }
                }
                catch (const std::exception& e) {
                    if (APIDefs) {
                        char errorMsg[256];
                        sprintf_s(errorMsg, "WSS client thread error: %s", e.what());
                        APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, errorMsg);
                    }
                }
                catch (...) {
                    if (APIDefs) {
                        APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, "Unknown exception in WSS client thread");
                    }
                }
                });
        }
        else {
            // For standard WebSocket
            websocketpp::lib::error_code ec;
            ws_client::connection_ptr con = m_impl->ws->get_connection(url, ec);

            if (ec) {
                std::string errorMsg = "Connection initialization error: " + ec.message();
                if (APIDefs) {
                    APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, errorMsg.c_str());
                }

                if (m_statusCallback) {
                    m_statusCallback(errorMsg);
                }
                Settings::SetWebSocketConnectionStatus(errorMsg);
                return false;
            }

            // Set additional connection options for debugging
            con->set_open_handshake_timeout(10000); // 10 seconds

            if (APIDefs) {
                APIDefs->Log(ELogLevel_DEBUG, ADDON_NAME, "Initiating standard connection...");
            }

            // Connect
            m_impl->ws->connect(con);

            // Update intermediate status
            Settings::SetWebSocketConnectionStatus("Connecting...");
            if (m_statusCallback) {
                m_statusCallback("Connecting...");
            }

            // Start the ASIO io_service run loop in a separate thread
            if (APIDefs) {
                APIDefs->Log(ELogLevel_DEBUG, ADDON_NAME, "Starting standard WebSocket thread");
            }

            m_impl->thread = websocketpp::lib::make_shared<websocketpp::lib::thread>([this]() {
                try {
                    if (APIDefs) {
                        APIDefs->Log(ELogLevel_DEBUG, ADDON_NAME, "WS client thread started");
                    }
                    m_impl->ws->run();
                    if (APIDefs) {
                        APIDefs->Log(ELogLevel_DEBUG, ADDON_NAME, "WS client thread completed normally");
                    }
                }
                catch (const std::exception& e) {
                    if (APIDefs) {
                        char errorMsg[256];
                        sprintf_s(errorMsg, "WS client thread error: %s", e.what());
                        APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, errorMsg);
                    }
                }
                catch (...) {
                    if (APIDefs) {
                        APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, "Unknown exception in WS client thread");
                    }
                }
                });
        }

        if (APIDefs) {
            APIDefs->Log(ELogLevel_INFO, ADDON_NAME, "Connection initiated, waiting for callbacks");
        }

        return true;
    }
    catch (const std::exception& e) {
        std::string errorMsg = std::string("Connection error: ") + e.what();

        if (APIDefs) {
            APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, errorMsg.c_str());
        }

        if (m_statusCallback) {
            m_statusCallback(errorMsg);
        }
        Settings::SetWebSocketConnectionStatus(errorMsg);

        return false;
    }
    catch (...) {
        std::string errorMsg = "Unknown connection error";

        if (APIDefs) {
            APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, errorMsg.c_str());
        }

        if (m_statusCallback) {
            m_statusCallback(errorMsg);
        }
        Settings::SetWebSocketConnectionStatus(errorMsg);

        return false;
    }
}

void WebSocketClient::disconnect() {
    try {
        if (m_connected || m_isShuttingDown) {  // Also disconnect if shutting down
            if (APIDefs) {
                APIDefs->Log(ELogLevel_INFO, ADDON_NAME, "Disconnecting WebSocket");
            }

            if (m_isSecure && m_impl && m_impl->wss) {
                // Close the secure connection
                websocketpp::lib::error_code ec;

                try {
                    wss_client::connection_ptr con = m_impl->wss->get_con_from_hdl(m_impl->handle, ec);
                    if (!ec) {
                        if (APIDefs) {
                            APIDefs->Log(ELogLevel_DEBUG, ADDON_NAME, "Closing secure connection");
                        }
                        con->close(websocketpp::close::status::normal, "Client disconnecting", ec);
                        if (ec) {
                            if (APIDefs) {
                                char errorMsg[256];
                                sprintf_s(errorMsg, "Error closing secure connection: %s", ec.message().c_str());
                                APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, errorMsg);
                            }
                        }
                    }
                }
                catch (...) {
                    if (APIDefs) {
                        APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, "Error getting secure connection handle");
                    }
                }

                // Stop the client
                if (APIDefs) {
                    APIDefs->Log(ELogLevel_DEBUG, ADDON_NAME, "Stopping WSS perpetual mode");
                }
                m_impl->wss->stop_perpetual();
            }
            else if (m_impl && m_impl->ws) {
                // Close the standard connection
                websocketpp::lib::error_code ec;

                try {
                    ws_client::connection_ptr con = m_impl->ws->get_con_from_hdl(m_impl->handle, ec);
                    if (!ec) {
                        if (APIDefs) {
                            APIDefs->Log(ELogLevel_DEBUG, ADDON_NAME, "Closing standard connection");
                        }
                        con->close(websocketpp::close::status::normal, "Client disconnecting", ec);
                        if (ec) {
                            if (APIDefs) {
                                char errorMsg[256];
                                sprintf_s(errorMsg, "Error closing standard connection: %s", ec.message().c_str());
                                APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, errorMsg);
                            }
                        }
                    }
                }
                catch (...) {
                    if (APIDefs) {
                        APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, "Error getting standard connection handle");
                    }
                }

                // Stop the client
                if (APIDefs) {
                    APIDefs->Log(ELogLevel_DEBUG, ADDON_NAME, "Stopping WS perpetual mode");
                }
                m_impl->ws->stop_perpetual();
            }

            m_connected = false;

            // Thread handling moved to safeShutdown to avoid deadlocks here

            if (APIDefs) {
                APIDefs->Log(ELogLevel_INFO, ADDON_NAME, "WebSocket disconnect initiated");
            }
        }
        else {
            if (APIDefs) {
                APIDefs->Log(ELogLevel_DEBUG, ADDON_NAME, "Not connected, nothing to disconnect");
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
    catch (...) {
        if (APIDefs) {
            APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, "Unknown error during WebSocket disconnect");
        }
    }
}

bool WebSocketClient::isConnected() const {
    return m_connected && !m_isShuttingDown;
}

bool WebSocketClient::isSecureConnection() const {
    return m_isSecure && m_connected && !m_isShuttingDown;
}

std::string WebSocketClient::getConnectionDetails() const {
    if (!m_connected || m_isShuttingDown) {
        return "Not connected";
    }

    if (m_isSecure) {
        std::string details = "Secure connection (WSS)";

        if (!m_url.empty()) {
            details += " to " + m_url;
        }

        return details;
    }
    else {
        std::string details = "Standard connection (WS)";

        if (!m_url.empty()) {
            details += " to " + m_url;
        }

        return details;
    }
}

std::string WebSocketClient::getConnectionUrl() const {
    return m_url;
}

bool WebSocketClient::sendMessage(const std::string& message) {
    if (!m_connected || m_isShuttingDown) {
        if (APIDefs) {
            APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, "Cannot send message: Not connected or shutting down");
        }
        return false;
    }

    try {
        std::lock_guard<std::mutex> lock(m_sendMutex);

        // Log message to send
        if (APIDefs) {
            char logMsg[256];
            sprintf_s(logMsg, "Sending message: %s",
                message.length() > 100 ? (message.substr(0, 97) + "...").c_str() : message.c_str());
            APIDefs->Log(ELogLevel_DEBUG, ADDON_NAME, logMsg);
        }

        // Log sent message
        if (m_messageCallback) {
            m_messageCallback("sent", message);
        }
        Settings::AddWebSocketLogEntry("sent", message);

        // Send the message using the appropriate client
        websocketpp::lib::error_code ec;

        if (m_isSecure && m_impl && m_impl->wss) {
            m_impl->wss->send(m_impl->handle, message, websocketpp::frame::opcode::text, ec);
        }
        else if (m_impl && m_impl->ws) {
            m_impl->ws->send(m_impl->handle, message, websocketpp::frame::opcode::text, ec);
        }
        else {
            if (APIDefs) {
                APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, "Cannot send: Client not initialized");
            }
            return false;
        }

        if (ec) {
            std::string errorMsg = "WebSocket send error: " + ec.message();
            if (APIDefs) {
                APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, errorMsg.c_str());
            }
            return false;
        }

        if (APIDefs) {
            APIDefs->Log(ELogLevel_DEBUG, ADDON_NAME, "Message sent successfully");
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
    catch (...) {
        if (APIDefs) {
            APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, "Unknown error sending WebSocket message");
        }
        return false;
    }
}

bool WebSocketClient::sendJson(const json& jsonData) {
    try {
        std::string jsonStr = jsonData.dump();

        if (APIDefs) {
            char logMsg[256];
            sprintf_s(logMsg, "Sending JSON: %s",
                jsonStr.length() > 100 ? (jsonStr.substr(0, 97) + "...").c_str() : jsonStr.c_str());
            APIDefs->Log(ELogLevel_DEBUG, ADDON_NAME, logMsg);
        }

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
    catch (...) {
        if (APIDefs) {
            APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, "Unknown error serializing JSON for WebSocket");
        }
        return false;
    }
}

bool WebSocketClient::createTimer(const std::string& name, float duration) {
    if (m_isShuttingDown) {
        if (APIDefs) {
            APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, "Cannot create timer: Client is shutting down");
        }
        return false;
    }

    if (APIDefs) {
        char logMsg[256];
        sprintf_s(logMsg, "Creating timer via WebSocket: %s (%.1f seconds)", name.c_str(), duration);
        APIDefs->Log(ELogLevel_INFO, ADDON_NAME, logMsg);
    }

    json message = {
        {"type", "create_timer"},
        {"name", name},
        {"duration", duration}
    };
    return sendJson(message);
}

bool WebSocketClient::startTimer(const std::string& timerId) {
    if (m_isShuttingDown) {
        if (APIDefs) {
            APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, "Cannot start timer: Client is shutting down");
        }
        return false;
    }

    if (APIDefs) {
        char logMsg[256];
        sprintf_s(logMsg, "Starting timer via WebSocket: %s", timerId.c_str());
        APIDefs->Log(ELogLevel_INFO, ADDON_NAME, logMsg);
    }

    json message = {
        {"type", "start_timer"},
        {"timerId", timerId}
    };
    return sendJson(message);
}

bool WebSocketClient::pauseTimer(const std::string& timerId) {
    if (m_isShuttingDown) {
        if (APIDefs) {
            APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, "Cannot pause timer: Client is shutting down");
        }
        return false;
    }

    if (APIDefs) {
        char logMsg[256];
        sprintf_s(logMsg, "Pausing timer via WebSocket: %s", timerId.c_str());
        APIDefs->Log(ELogLevel_INFO, ADDON_NAME, logMsg);
    }

    json message = {
        {"type", "pause_timer"},
        {"timerId", timerId}
    };
    return sendJson(message);
}

bool WebSocketClient::stopTimer(const std::string& timerId) {
    if (m_isShuttingDown) {
        if (APIDefs) {
            APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, "Cannot stop timer: Client is shutting down");
        }
        return false;
    }

    if (APIDefs) {
        char logMsg[256];
        sprintf_s(logMsg, "Stopping timer via WebSocket: %s", timerId.c_str());
        APIDefs->Log(ELogLevel_INFO, ADDON_NAME, logMsg);
    }

    json message = {
        {"type", "stop_timer"},
        {"timerId", timerId}
    };
    return sendJson(message);
}

void WebSocketClient::ping() {
    if (!m_connected || m_isShuttingDown) {
        if (APIDefs) {
            APIDefs->Log(ELogLevel_DEBUG, ADDON_NAME, "Cannot send ping: Not connected or shutting down");
        }
        return;
    }

    if (APIDefs) {
        APIDefs->Log(ELogLevel_DEBUG, ADDON_NAME, "Sending ping to server");
    }

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

// WebSocketClient::handleMessage function (in wss.cpp)

void WebSocketClient::handleMessage(const std::string& message) {
    // Skip processing if shutting down
    if (m_isShuttingDown) {
        return;
    }

    try {
        json data = json::parse(message);
        std::string type = data.value("type", "");

        if (APIDefs) {
            char logMsg[256];
            sprintf_s(logMsg, "Processing message of type: %s", type.c_str());
            APIDefs->Log(ELogLevel_DEBUG, ADDON_NAME, logMsg);
        }

        // Route to appropriate handler based on message type
        if (type == "available_rooms" || type == "room_joined" || type == "room_left" ||
            type == "room_created" || type == "client_joined" || type == "client_left") {
            // Room-related messages
            handleRoomMessage(data);
        }
        else if (type == "timer_created" || type == "timer_started" || type == "timer_paused" ||
            type == "timer_completed" || type == "timer_list") {
            // Timer-related messages
            handleTimerMessage(data);
        }
        else if (type == "timer_subscribed") {
            // Handle subscription confirmation
            if (data.contains("timerId")) {
                std::string timerId = data["timerId"].get<std::string>();

                if (APIDefs) {
                    char logMsg[256];
                    sprintf_s(logMsg, "Subscription confirmed for timer: %s", timerId.c_str());
                    APIDefs->Log(ELogLevel_INFO, ADDON_NAME, logMsg);
                }
            }
        }
        else if (type == "timer_unsubscribed") {
            // Handle unsubscription confirmation
            if (data.contains("timerId")) {
                std::string timerId = data["timerId"].get<std::string>();

                if (APIDefs) {
                    char logMsg[256];
                    sprintf_s(logMsg, "Unsubscription confirmed for timer: %s", timerId.c_str());
                    APIDefs->Log(ELogLevel_INFO, ADDON_NAME, logMsg);
                }
            }
        }
        else if (type == "ping") {
            // Handle ping request by sending a pong response
            if (APIDefs) {
                APIDefs->Log(ELogLevel_DEBUG, ADDON_NAME, "Received ping from server, sending pong");
            }

            // Respond with a pong message
            json pongMessage = {
                {"type", "pong"},
                {"timestamp", std::chrono::system_clock::now().time_since_epoch().count()}
            };
            sendJson(pongMessage);
        }
        else if (type == "pong") {
            // Handle ping response
            if (APIDefs) {
                APIDefs->Log(ELogLevel_DEBUG, ADDON_NAME, "Received pong from server");
            }
        }
        else if (type == "error") {
            // Handle error messages
            std::string errorMsg = data.value("message", "Unknown error");
            if (APIDefs) {
                char logMsg[256];
                sprintf_s(logMsg, "Received error from server: %s", errorMsg.c_str());
                APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, logMsg);
            }
        }
        else if (type == "init") {
            // Handle initial state with all timers
            if (APIDefs) {
                APIDefs->Log(ELogLevel_INFO, ADDON_NAME, "Received initial timer state from server");
            }
            // Process initial timers state if needed
        }
        else {
            // Unknown message type
            if (APIDefs) {
                char logMsg[256];
                sprintf_s(logMsg, "Received unknown message type: %s", type.c_str());
                APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, logMsg);
            }
        }
    }
    catch (const std::exception& e) {
        if (APIDefs) {
            char errorMsg[256];
            sprintf_s(errorMsg, "Error processing message: %s", e.what());
            APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, errorMsg);
        }
    }
    catch (...) {
        if (APIDefs) {
            APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, "Unknown error processing message");
        }
    }
}

bool WebSocketClient::joinRoom(const std::string& roomId, const std::string& password) {
    if (!m_connected || m_isShuttingDown) {
        if (APIDefs) {
            APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, "Cannot join room: Not connected or shutting down");
        }
        return false;
    }

    try {
        json message = {
            {"type", "join_room"},
            {"roomId", roomId}
        };

        // Only include password if provided
        if (!password.empty()) {
            message["password"] = password;
        }

        if (APIDefs) {
            char logMsg[256];
            sprintf_s(logMsg, "Joining room: %s", roomId.c_str());
            APIDefs->Log(ELogLevel_INFO, ADDON_NAME, logMsg);
        }

        return sendJson(message);
    }
    catch (const std::exception& e) {
        if (APIDefs) {
            char errorMsg[256];
            sprintf_s(errorMsg, "Error joining room: %s", e.what());
            APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, errorMsg);
        }
        return false;
    }
    catch (...) {
        if (APIDefs) {
            APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, "Unknown error joining room");
        }
        return false;
    }
}

bool WebSocketClient::createRoom(const std::string& name, bool isPublic, const std::string& password) {
    if (!m_connected || m_isShuttingDown) {
        if (APIDefs) {
            APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, "Cannot create room: Not connected or shutting down");
        }
        return false;
    }

    try {
        json message = {
            {"type", "create_room"},
            {"name", name},
            {"isPublic", isPublic}
        };

        // Only include password if provided
        if (!password.empty()) {
            message["password"] = password;
        }

        if (APIDefs) {
            char logMsg[256];
            sprintf_s(logMsg, "Creating room: %s", name.c_str());
            APIDefs->Log(ELogLevel_INFO, ADDON_NAME, logMsg);
        }

        return sendJson(message);
    }
    catch (const std::exception& e) {
        if (APIDefs) {
            char errorMsg[256];
            sprintf_s(errorMsg, "Error creating room: %s", e.what());
            APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, errorMsg);
        }
        return false;
    }
    catch (...) {
        if (APIDefs) {
            APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, "Unknown error creating room");
        }
        return false;
    }
}

bool WebSocketClient::leaveRoom() {
    if (!m_connected || m_isShuttingDown) {
        if (APIDefs) {
            APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, "Cannot leave room: Not connected or shutting down");
        }
        return false;
    }

    try {
        json message = {
            {"type", "leave_room"}
        };

        if (APIDefs) {
            APIDefs->Log(ELogLevel_INFO, ADDON_NAME, "Leaving current room");
        }

        return sendJson(message);
    }
    catch (const std::exception& e) {
        if (APIDefs) {
            char errorMsg[256];
            sprintf_s(errorMsg, "Error leaving room: %s", e.what());
            APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, errorMsg);
        }
        return false;
    }
    catch (...) {
        if (APIDefs) {
            APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, "Unknown error leaving room");
        }
        return false;
    }
}

bool WebSocketClient::refreshRooms() {
    if (!m_connected || m_isShuttingDown) {
        if (APIDefs) {
            APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, "Cannot refresh rooms: Not connected or shutting down");
        }
        return false;
    }

    try {
        // Use a static variable to track last refresh time
        static std::chrono::steady_clock::time_point lastRefresh = std::chrono::steady_clock::now();
        auto now = std::chrono::steady_clock::now();

        // Only allow refreshes every 2 seconds to prevent spamming
        if (std::chrono::duration_cast<std::chrono::milliseconds>(now - lastRefresh).count() < 2000) {
            if (APIDefs) {
                APIDefs->Log(ELogLevel_DEBUG, ADDON_NAME, "Skipping room refresh - too soon since last refresh");
            }
            return false;
        }

        // Update last refresh time
        lastRefresh = now;

        // The server sends available rooms on connect, but doesn't have a specific
        // refresh endpoint. We'll request the full list of timers which includes public rooms.
        json message = {
            {"type", "get_timers"}
        };

        if (APIDefs) {
            APIDefs->Log(ELogLevel_INFO, ADDON_NAME, "Refreshing rooms and timers");
        }

        return sendJson(message);
    }
    catch (const std::exception& e) {
        if (APIDefs) {
            char errorMsg[256];
            sprintf_s(errorMsg, "Error refreshing rooms: %s", e.what());
            APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, errorMsg);
        }
        return false;
    }
    catch (...) {
        if (APIDefs) {
            APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, "Unknown error refreshing rooms");
        }
        return false;
    }
}

bool WebSocketClient::subscribeToTimer(const std::string& timerId, const std::string& roomId) {
    if (!m_connected || m_isShuttingDown) {
        if (APIDefs) {
            APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, "Cannot subscribe to timer: Not connected or shutting down");
        }
        return false;
    }

    try {
        // Use provided roomId or currentRoomId
        std::string targetRoomId = roomId.empty() ? Settings::GetCurrentRoom() : roomId;

        if (targetRoomId.empty()) {
            if (APIDefs) {
                APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, "Cannot subscribe to timer: No room specified");
            }
            return false;
        }

        // Store subscription locally
        Settings::SubscribeToTimer(timerId, targetRoomId);

        // Send subscription to server
        json message = {
            {"type", "subscribe_to_timer"},
            {"timerId", timerId},
            {"roomId", targetRoomId}
        };

        if (APIDefs) {
            char logMsg[256];
            sprintf_s(logMsg, "Subscribing to timer %s in room %s", timerId.c_str(), targetRoomId.c_str());
            APIDefs->Log(ELogLevel_INFO, ADDON_NAME, logMsg);
        }

        return sendJson(message);
    }
    catch (const std::exception& e) {
        if (APIDefs) {
            char errorMsg[256];
            sprintf_s(errorMsg, "Error subscribing to timer: %s", e.what());
            APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, errorMsg);
        }
        return false;
    }
    catch (...) {
        if (APIDefs) {
            APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, "Unknown error subscribing to timer");
        }
        return false;
    }
}

bool WebSocketClient::unsubscribeFromTimer(const std::string& timerId, const std::string& roomId) {
    if (!m_connected || m_isShuttingDown) {
        if (APIDefs) {
            APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, "Cannot unsubscribe from timer: Not connected or shutting down");
        }
        return false;
    }

    try {
        // Use provided roomId or currentRoomId
        std::string targetRoomId = roomId.empty() ? Settings::GetCurrentRoom() : roomId;

        if (targetRoomId.empty()) {
            if (APIDefs) {
                APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, "Cannot unsubscribe from timer: No room specified");
            }
            return false;
        }

        // Remove subscription locally
        Settings::UnsubscribeFromTimer(timerId, targetRoomId);

        // Send unsubscription to server
        json message = {
            {"type", "unsubscribe_from_timer"},
            {"timerId", timerId},
            {"roomId", targetRoomId}
        };

        if (APIDefs) {
            char logMsg[256];
            sprintf_s(logMsg, "Unsubscribing from timer %s in room %s", timerId.c_str(), targetRoomId.c_str());
            APIDefs->Log(ELogLevel_INFO, ADDON_NAME, logMsg);
        }

        return sendJson(message);
    }
    catch (const std::exception& e) {
        if (APIDefs) {
            char errorMsg[256];
            sprintf_s(errorMsg, "Error unsubscribing from timer: %s", e.what());
            APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, errorMsg);
        }
        return false;
    }
    catch (...) {
        if (APIDefs) {
            APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, "Unknown error unsubscribing from timer");
        }
        return false;
    }
}

void WebSocketClient::handleRoomMessage(const json& data) {
    std::string type = data.value("type", "");

    if (type == "available_rooms") {
        if (data.contains("rooms") && data["rooms"].is_array()) {
            std::vector<RoomInfo> rooms;
            for (const auto& roomJson : data["rooms"]) {
                RoomInfo room;
                room.id = roomJson.value("id", "");
                room.name = roomJson.value("name", "");
                room.createdAt = roomJson.value("created_at", 0);
                room.isPublic = roomJson.value("is_public", 1) != 0;
                room.clientCount = roomJson.value("client_count", 0);
                rooms.push_back(room);
            }

            // Update settings with available rooms
            Settings::SetAvailableRooms(rooms);

            // Cleanup subscriptions based on available rooms
            Settings::CleanupSubscriptions();

            if (APIDefs) {
                char logMsg[256];
                sprintf_s(logMsg, "Received %zu available rooms", rooms.size());
                APIDefs->Log(ELogLevel_INFO, ADDON_NAME, logMsg);
            }
        }
    }
    else if (type == "room_joined") {
        if (data.contains("roomId")) {
            std::string roomId = data["roomId"].get<std::string>();

            // Update current room in settings
            Settings::SetCurrentRoom(roomId);

            if (APIDefs) {
                char logMsg[256];
                sprintf_s(logMsg, "Joined room: %s", roomId.c_str());
                APIDefs->Log(ELogLevel_INFO, ADDON_NAME, logMsg);
            }

            // Extract server time for synchronization
            int64_t serverTime = 0;
            if (data.contains("current_server_time")) {
                serverTime = data["current_server_time"].get<int64_t>();

                if (APIDefs) {
                    char logMsg[256];
                    sprintf_s(logMsg, "Server time received: %lld", serverTime);
                    APIDefs->Log(ELogLevel_DEBUG, ADDON_NAME, logMsg);
                }
            }

            // Build a map of valid timer IDs from the server's response
            std::unordered_set<std::string> validTimerIds;

            // Process timers if included
            if (data.contains("timers") && data["timers"].is_array()) {
                // First, collect all valid timer IDs from the server
                for (const auto& timerJson : data["timers"]) {
                    std::string timerId = timerJson.value("id", "");
                    validTimerIds.insert(timerId);
                }

                // Clean up any timers in settings from this room that don't exist on the server
                auto it = Settings::timers.begin();
                while (it != Settings::timers.end()) {
                    if (it->isRoomTimer && it->roomId == roomId &&
                        validTimerIds.find(it->id) == validTimerIds.end()) {
                        // This timer is from the current room but doesn't exist on the server
                        std::string timerId = it->id; // Save ID before erasing
                        it = Settings::timers.erase(it);

                        if (APIDefs) {
                            char logMsg[256];
                            sprintf_s(logMsg, "Removed invalid room timer from settings: %s", timerId.c_str());
                            APIDefs->Log(ELogLevel_INFO, ADDON_NAME, logMsg);
                        }
                    }
                    else {
                        ++it;
                    }
                }

                // Clean up any active timers from this room that don't exist on the server
                auto activeIt = activeTimers.begin();
                while (activeIt != activeTimers.end()) {
                    if (activeIt->isRoomTimer() && activeIt->roomId == roomId &&
                        validTimerIds.find(activeIt->id) == validTimerIds.end()) {
                        // This active timer is from the current room but doesn't exist on the server
                        activeIt = activeTimers.erase(activeIt);
                    }
                    else {
                        ++activeIt;
                    }
                }

                // Clean up any subscriptions to timers that don't exist on the server
                auto subscriptions = Settings::GetSubscriptionsForRoom(roomId);
                if (!subscriptions.empty()) {
                    std::vector<std::string> invalidSubscriptions;
                    for (const auto& timerId : subscriptions) {
                        if (validTimerIds.find(timerId) == validTimerIds.end()) {
                            // This timer subscription doesn't exist on the server anymore
                            invalidSubscriptions.push_back(timerId);
                        }
                    }

                    // Remove invalid subscriptions
                    for (const auto& timerId : invalidSubscriptions) {
                        Settings::UnsubscribeFromTimer(timerId, roomId);

                        if (APIDefs) {
                            char logMsg[256];
                            sprintf_s(logMsg, "Removed subscription to non-existent timer: %s", timerId.c_str());
                            APIDefs->Log(ELogLevel_INFO, ADDON_NAME, logMsg);
                        }
                    }

                    if (!invalidSubscriptions.empty()) {
                        Settings::ScheduleSave(SettingsPath);
                    }
                }

                // Now process all timers from the server
                for (const auto& timerJson : data["timers"]) {
                    std::string timerId = timerJson.value("id", "");
                    std::string name = timerJson.value("name", "");
                    float duration = timerJson.value("duration", 0.0f);
                    std::string status = timerJson.value("status", "created");

                    // Get remaining time from server (NEW)
                    float remaining = timerJson.value("remaining", duration);

                    // Calculate adjusted remaining time based on server time and local time (NEW)
                    float adjustedRemaining = remaining;
                    if (serverTime > 0 && status == "running") {
                        // Get current local time
                        auto now = std::chrono::system_clock::now();
                        int64_t localTime = std::chrono::duration_cast<std::chrono::seconds>(
                            now.time_since_epoch()).count();

                        // Calculate time offset (time elapsed since server sent the message)
                        int64_t offset = localTime - serverTime;

                        // Log the offset and adjustment for debugging
                        if (APIDefs) {
                            char logMsg[256];
                            sprintf_s(logMsg, "Timer sync: Server time: %lld, Local time: %lld, Offset: %lld, Original remaining: %.1f",
                                serverTime, localTime, offset, remaining);
                            APIDefs->Log(ELogLevel_DEBUG, ADDON_NAME, logMsg);
                        }

                        // Adjust remaining time based on offset (ensure it doesn't go below 0)
                        adjustedRemaining = std::max(0.0f, remaining - static_cast<float>(offset));

                        if (APIDefs) {
                            char logMsg[256];
                            sprintf_s(logMsg, "Adjusted remaining time: %.1f seconds", adjustedRemaining);
                            APIDefs->Log(ELogLevel_DEBUG, ADDON_NAME, logMsg);
                        }
                    }

                    // Create/update settings entry for room timers
                    TimerData* settingsTimer = Settings::FindTimer(timerId);
                    if (!settingsTimer) {
                        // Create a new timer entry
                        TimerData& newTimer = Settings::AddTimer(name, duration);
                        newTimer.id = timerId;
                        newTimer.isRoomTimer = true;
                        newTimer.roomId = roomId;

                        if (APIDefs) {
                            char logMsg[256];
                            sprintf_s(logMsg, "Created local entry for room timer: %s (%s)",
                                name.c_str(), timerId.c_str());
                            APIDefs->Log(ELogLevel_INFO, ADDON_NAME, logMsg);
                        }
                    }
                    else if (settingsTimer->isRoomTimer && settingsTimer->roomId == roomId) {
                        // Update existing timer
                        settingsTimer->name = name;
                        settingsTimer->duration = duration;
                    }

                    // For subscribed timers, add them to activeTimers for the main display
                    bool isSubscribed = Settings::IsSubscribedToTimer(timerId, roomId);
                    if (isSubscribed) {
                        bool isPaused = (status != "running");

                        // Check if already in activeTimers
                        bool found = false;
                        for (auto& timer : activeTimers) {
                            if (timer.id == timerId && timer.roomId == roomId) {
                                found = true;
                                // Update status and remaining time (now using adjusted time)
                                timer.isPaused = isPaused;
                                timer.remainingTime = adjustedRemaining; // Use adjusted time
                                timer.warningPlayed = false; // Reset warning state when syncing

                                if (APIDefs) {
                                    char logMsg[256];
                                    sprintf_s(logMsg, "Updated room timer %s with status %s, synced remaining time: %.1f s",
                                        name.c_str(), isPaused ? "paused" : "running", adjustedRemaining);
                                    APIDefs->Log(ELogLevel_INFO, ADDON_NAME, logMsg);
                                }

                                break;
                            }
                        }

                        if (!found) {
                            // Add to activeTimers with synced remaining time
                            ActiveTimer newTimer(timerId, duration, isPaused, roomId);
                            newTimer.remainingTime = adjustedRemaining; // Set to adjusted time
                            activeTimers.push_back(newTimer);

                            if (APIDefs) {
                                char logMsg[256];
                                sprintf_s(logMsg, "Added subscribed room timer to active list: %s, status: %s, synced remaining time: %.1f s",
                                    name.c_str(), isPaused ? "paused" : "running", adjustedRemaining);
                                APIDefs->Log(ELogLevel_INFO, ADDON_NAME, logMsg);
                            }
                        }
                    }
                }

                // Save settings to persist the room timer information
                Settings::ScheduleSave(SettingsPath);
            }
        }
    }
    else if (type == "room_left") {
        // Get the current room ID before clearing it
        std::string oldRoomId = Settings::GetCurrentRoom();

        // Clear current room
        Settings::SetCurrentRoom("");

        // Remove all timers for the old room
        removeAllRoomTimers(oldRoomId);

        if (APIDefs) {
            APIDefs->Log(ELogLevel_INFO, ADDON_NAME, "Left room and removed associated timers");
        }
    }
    else if (type == "room_created") {
        if (data.contains("room") && data["room"].is_object()) {
            const auto& roomJson = data["room"];
            RoomInfo room;
            room.id = roomJson.value("id", "");
            room.name = roomJson.value("name", "");
            room.createdAt = roomJson.value("created_at", 0);
            room.isPublic = roomJson.value("is_public", 1) != 0;
            room.clientCount = roomJson.value("client_count", 0);

            if (APIDefs) {
                char logMsg[256];
                sprintf_s(logMsg, "Created room: %s (%s)", room.name.c_str(), room.id.c_str());
                APIDefs->Log(ELogLevel_INFO, ADDON_NAME, logMsg);
            }

            // Refresh room list after room creation
            this->refreshRooms();
        }
    }
    else if (type == "client_joined" || type == "client_left") {
        // Update client count in rooms
        if (data.contains("roomId") && data.contains("clientCount")) {
            std::string roomId = data["roomId"].get<std::string>();
            int clientCount = data["clientCount"].get<int>();

            // Update the client count in our available rooms list
            auto rooms = Settings::GetAvailableRooms();
            for (auto& room : rooms) {
                if (room.id == roomId) {
                    room.clientCount = clientCount;
                    break;
                }
            }

            if (APIDefs) {
                char logMsg[256];
                sprintf_s(logMsg, "Room %s now has %d clients", roomId.c_str(), clientCount);
                APIDefs->Log(ELogLevel_INFO, ADDON_NAME, logMsg);
            }
        }
    }
}

void WebSocketClient::handleTimerMessage(const json& data) {
    // Skip processing if shutting down
    if (m_isShuttingDown) {
        return;
    }

    std::string type = data.value("type", "");

    if (APIDefs) {
        char logMsg[256];
        sprintf_s(logMsg, "Processing timer message of type: %s", type.c_str());
        APIDefs->Log(ELogLevel_DEBUG, ADDON_NAME, logMsg);
    }

    // Extract server time for synchronization
    int64_t serverTime = 0;
    if (data.contains("current_server_time")) {
        serverTime = data["current_server_time"].get<int64_t>();

        if (APIDefs) {
            char logMsg[256];
            sprintf_s(logMsg, "Server time received: %lld", serverTime);
            APIDefs->Log(ELogLevel_DEBUG, ADDON_NAME, logMsg);
        }
    }

    if (type == "timer_created") {
        if (data.contains("timer") && data["timer"].is_object()) {
            std::string timerId = data["timer"].value("id", "");
            std::string name = data["timer"].value("name", "");
            float duration = data["timer"].value("duration", 0.0f);
            std::string roomId = data["timer"].value("room_id", "");
            std::string status = data["timer"].value("status", "created");

            // If this is for our current room, create a local timer representation
            // (even if not subscribed, for display in room timers list)
            if (!roomId.empty() && roomId == Settings::GetCurrentRoom()) {
                // Create a local TimerData entry if it doesn't exist
                TimerData* settingsTimer = Settings::FindTimer(timerId);
                if (!settingsTimer) {
                    TimerData& newTimer = Settings::AddTimer(name, duration);
                    // Override the generated ID with the server's ID
                    newTimer.id = timerId;
                    // Mark as room timer
                    newTimer.isRoomTimer = true;
                    newTimer.roomId = roomId;
                    // Save settings
                    Settings::ScheduleSave(SettingsPath);

                    if (APIDefs) {
                        char logMsg[256];
                        sprintf_s(logMsg, "Created local entry for room timer: %s (%s)",
                            name.c_str(), timerId.c_str());
                        APIDefs->Log(ELogLevel_INFO, ADDON_NAME, logMsg);
                    }
                }

                // Check if we're subscribed to this timer
                auto subscriptions = Settings::GetSubscriptionsForRoom(roomId);
                bool isSubscribed = subscriptions.find(timerId) != subscriptions.end();

                // Create a room timer in our active timers list ONLY if subscribed
                if (isSubscribed) {
                    // Add to activeTimers if not already there
                    bool found = false;
                    for (const auto& timer : activeTimers) {
                        if (timer.id == timerId && timer.roomId == roomId) {
                            found = true;
                            break;
                        }
                    }

                    if (!found) {
                        activeTimers.push_back(ActiveTimer(timerId, duration, true, roomId));

                        if (APIDefs) {
                            char logMsg[256];
                            sprintf_s(logMsg, "Added subscribed room timer to active list: %s", name.c_str());
                            APIDefs->Log(ELogLevel_INFO, ADDON_NAME, logMsg);
                        }
                    }
                }

                // Log that the room timer list should be refreshed in UI
                if (APIDefs) {
                    APIDefs->Log(ELogLevel_DEBUG, ADDON_NAME, "Timer created - room timer list should be updated in UI");
                }
            }
        }
    }
    else if (type == "timer_started" || type == "timer_paused" || type == "timer_completed") {
        if (data.contains("timer") && data["timer"].is_object()) {
            std::string timerId = data["timer"].value("id", "");
            std::string roomId = data["timer"].value("room_id", "");
            std::string status = data["timer"].value("status", "");
            std::string name = data["timer"].value("name", ""); // Get name for local entries
            float remaining = data["timer"].value("remaining", 0.0f); // Get remaining time (NEW)

            // Calculate adjusted remaining time based on server time and local time (NEW)
            float adjustedRemaining = remaining;
            if (serverTime > 0 && status == "running") {
                // Get current local time
                auto now = std::chrono::system_clock::now();
                int64_t localTime = std::chrono::duration_cast<std::chrono::seconds>(
                    now.time_since_epoch()).count();

                // Calculate time offset (time elapsed since server sent the message)
                int64_t offset = localTime - serverTime;

                // Log the offset and adjustment for debugging
                if (APIDefs) {
                    char logMsg[256];
                    sprintf_s(logMsg, "Timer sync: Server time: %lld, Local time: %lld, Offset: %lld, Original remaining: %.1f",
                        serverTime, localTime, offset, remaining);
                    APIDefs->Log(ELogLevel_DEBUG, ADDON_NAME, logMsg);
                }

                // Adjust remaining time based on offset (ensure it doesn't go below 0)
                adjustedRemaining = std::max(0.0f, remaining - static_cast<float>(offset));

                if (APIDefs) {
                    char logMsg[256];
                    sprintf_s(logMsg, "Adjusted remaining time: %.1f seconds", adjustedRemaining);
                    APIDefs->Log(ELogLevel_DEBUG, ADDON_NAME, logMsg);
                }
            }

            // Only process if for our current room and we're subscribed (or no subscriptions)
            if (!roomId.empty() && roomId == Settings::GetCurrentRoom()) {
                auto subscriptions = Settings::GetSubscriptionsForRoom(roomId);
                bool hasSubscriptions = !subscriptions.empty();

                if (!hasSubscriptions || subscriptions.find(timerId) != subscriptions.end()) {
                    // Find or create local TimerData entry
                    TimerData* settingsTimer = Settings::FindTimer(timerId);
                    if (!settingsTimer && !name.empty()) {
                        // If we received a timer update but don't have a local entry, create one
                        float duration = data["timer"].value("duration", 0.0f);
                        if (duration > 0) {
                            TimerData& newTimer = Settings::AddTimer(name, duration);
                            // Replace the auto-generated ID with the server's timer ID
                            newTimer.id = timerId;
                            // Mark as room timer and set room ID
                            newTimer.isRoomTimer = true;
                            newTimer.roomId = roomId;
                            // Save settings
                            Settings::ScheduleSave(SettingsPath);

                            if (APIDefs) {
                                char logMsg[256];
                                sprintf_s(logMsg, "Created local entry for room timer update: %s (%s)",
                                    name.c_str(), timerId.c_str());
                                APIDefs->Log(ELogLevel_INFO, ADDON_NAME, logMsg);
                            }

                            // The updated settingsTimer pointer
                            settingsTimer = Settings::FindTimer(timerId);
                        }
                    }

                    // Find matching timer in our list
                    for (auto& timer : activeTimers) {
                        if (timer.id == timerId && timer.roomId == roomId) {
                            // Update status and remaining time
                            if (status == "running") {
                                timer.isPaused = false;
                                timer.remainingTime = adjustedRemaining; // Use adjusted time (NEW)

                                if (APIDefs) {
                                    char logMsg[256];
                                    sprintf_s(logMsg, "Timer %s is running with synced remaining time: %.1f s",
                                        timerId.c_str(), adjustedRemaining);
                                    APIDefs->Log(ELogLevel_DEBUG, ADDON_NAME, logMsg);
                                }
                            }
                            else if (status == "paused") {
                                timer.isPaused = true;
                                timer.remainingTime = remaining; // Use exact time for paused timers

                                if (APIDefs) {
                                    char logMsg[256];
                                    sprintf_s(logMsg, "Timer %s is paused with remaining time: %.1f s",
                                        timerId.c_str(), remaining);
                                    APIDefs->Log(ELogLevel_DEBUG, ADDON_NAME, logMsg);
                                }
                            }
                            else if (status == "completed") {
                                // Reset timer
                                if (settingsTimer) {
                                    timer.remainingTime = settingsTimer->duration;
                                    timer.isPaused = true;
                                    timer.warningPlayed = false;

                                    if (APIDefs) {
                                        char logMsg[256];
                                        sprintf_s(logMsg, "Timer %s completed and reset to %.1f s",
                                            timerId.c_str(), settingsTimer->duration);
                                        APIDefs->Log(ELogLevel_DEBUG, ADDON_NAME, logMsg);
                                    }
                                }
                            }

                            if (APIDefs) {
                                char logMsg[256];
                                sprintf_s(logMsg, "Updated room timer %s to status: %s, synced remaining: %.1f s",
                                    timerId.c_str(), status.c_str(), timer.remainingTime);
                                APIDefs->Log(ELogLevel_INFO, ADDON_NAME, logMsg);
                            }

                            break;
                        }
                    }
                }
            }
        }
    }
    else if (type == "timer_list") {
        // Process timer list with synced remaining times
        if (data.contains("timers") && data["timers"].is_array()) {
            std::string roomId = Settings::GetCurrentRoom();

            if (!roomId.empty()) {
                // Get current subscriptions
                auto subscriptions = Settings::GetSubscriptionsForRoom(roomId);

                // Build a map of valid timer IDs 
                std::unordered_set<std::string> validTimerIds;

                // Process all timers from the server
                for (const auto& timerJson : data["timers"]) {
                    std::string timerId = timerJson.value("id", "");
                    std::string name = timerJson.value("name", "");
                    float duration = timerJson.value("duration", 0.0f);
                    std::string status = timerJson.value("status", "created");
                    float remaining = timerJson.value("remaining", duration); // Get remaining time (NEW)

                    // Add to valid timer IDs
                    validTimerIds.insert(timerId);

                    // Calculate adjusted remaining time (NEW)
                    float adjustedRemaining = remaining;
                    if (serverTime > 0 && status == "running") {
                        // Get current local time
                        auto now = std::chrono::system_clock::now();
                        int64_t localTime = std::chrono::duration_cast<std::chrono::seconds>(
                            now.time_since_epoch()).count();

                        // Calculate time offset
                        int64_t offset = localTime - serverTime;

                        // Adjust remaining time based on offset
                        adjustedRemaining = std::max(0.0f, remaining - static_cast<float>(offset));

                        if (APIDefs) {
                            char logMsg[256];
                            sprintf_s(logMsg, "Timer list sync: Timer %s adjusted from %.1f to %.1f seconds",
                                timerId.c_str(), remaining, adjustedRemaining);
                            APIDefs->Log(ELogLevel_DEBUG, ADDON_NAME, logMsg);
                        }
                    }

                    // Create/update settings entry for ALL room timers
                    TimerData* settingsTimer = Settings::FindTimer(timerId);
                    if (!settingsTimer) {
                        // Create a new timer entry
                        TimerData& newTimer = Settings::AddTimer(name, duration);
                        newTimer.id = timerId;
                        newTimer.isRoomTimer = true;
                        newTimer.roomId = roomId;
                    }
                    else if (settingsTimer->isRoomTimer && settingsTimer->roomId == roomId) {
                        // Update existing timer
                        settingsTimer->name = name;
                        settingsTimer->duration = duration;
                    }

                    // Only add subscribed timers to activeTimers
                    bool isSubscribed = subscriptions.find(timerId) != subscriptions.end();
                    if (isSubscribed) {
                        bool isPaused = (status != "running");

                        // Check if already in activeTimers
                        bool found = false;
                        for (auto& timer : activeTimers) {
                            if (timer.id == timerId && timer.roomId == roomId) {
                                found = true;
                                // Update status and remaining time
                                timer.isPaused = isPaused;
                                timer.remainingTime = isPaused ? remaining : adjustedRemaining; // Use adjusted time for running timers

                                if (APIDefs) {
                                    char logMsg[256];
                                    sprintf_s(logMsg, "Updated room timer %s with status: %s, synced remaining time: %.1f s",
                                        name.c_str(), isPaused ? "paused" : "running", timer.remainingTime);
                                    APIDefs->Log(ELogLevel_INFO, ADDON_NAME, logMsg);
                                }

                                break;
                            }
                        }

                        if (!found) {
                            // Create a new ActiveTimer with the correct remaining time
                            ActiveTimer newTimer(timerId, duration, isPaused, roomId);
                            newTimer.remainingTime = isPaused ? remaining : adjustedRemaining; // Set appropriate time
                            activeTimers.push_back(newTimer);

                            if (APIDefs) {
                                char logMsg[256];
                                sprintf_s(logMsg, "Added synced room timer to active list: %s with status: %s, remaining time: %.1f s",
                                    name.c_str(), isPaused ? "paused" : "running", newTimer.remainingTime);
                                APIDefs->Log(ELogLevel_INFO, ADDON_NAME, logMsg);
                            }
                        }
                    }
                }

                // Clean up any timers that are no longer valid
                cleanupInvalidTimers(validTimerIds, roomId);

                // Save settings to persist room timer information
                Settings::ScheduleSave(SettingsPath);
            }
        }
    }
}

void WebSocketClient::cleanupInvalidTimers(const std::unordered_set<std::string>& validTimerIds, const std::string& roomId) {
    // Remove any timers from settings that aren't in the validTimerIds set
    auto it = Settings::timers.begin();
    while (it != Settings::timers.end()) {
        if (it->isRoomTimer && it->roomId == roomId &&
            validTimerIds.find(it->id) == validTimerIds.end()) {
            // This timer no longer exists on the server
            std::string timerId = it->id; // Save before erasing
            it = Settings::timers.erase(it);

            if (APIDefs) {
                char logMsg[256];
                sprintf_s(logMsg, "Removed invalid room timer from settings: %s", timerId.c_str());
                APIDefs->Log(ELogLevel_INFO, ADDON_NAME, logMsg);
            }
        }
        else {
            ++it;
        }
    }

    // Also remove any active timers that no longer exist
    auto activeIt = activeTimers.begin();
    while (activeIt != activeTimers.end()) {
        if (activeIt->isRoomTimer() && activeIt->roomId == roomId &&
            validTimerIds.find(activeIt->id) == validTimerIds.end()) {
            // This active timer no longer exists on the server
            activeIt = activeTimers.erase(activeIt);
        }
        else {
            ++activeIt;
        }
    }
}


void WebSocketClient::loadSubscribedTimersForRoom(const std::string& roomId, const std::unordered_set<std::string>& validTimerIds) {
    if (!this->m_connected || this->m_isShuttingDown) {
        if (APIDefs) {
            APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, "Cannot load subscribed timers: Not connected or shutting down");
        }
        return;
    }

    try {
        if (APIDefs) {
            char logMsg[256];
            sprintf_s(logMsg, "Loading subscribed timers for room: %s", roomId.c_str());
            APIDefs->Log(ELogLevel_INFO, ADDON_NAME, logMsg);
        }

        // Get subscriptions for this room
        auto subscriptions = Settings::GetSubscriptionsForRoom(roomId);
        if (subscriptions.empty()) {
            if (APIDefs) {
                APIDefs->Log(ELogLevel_INFO, ADDON_NAME, "No subscriptions found for room");
            }
            return;
        }

        // For each subscription, check if it's valid and add to activeTimers if not already there
        for (const auto& timerId : subscriptions) {
            // Check if this timer is valid (exists on server)
            if (validTimerIds.find(timerId) != validTimerIds.end()) {
                TimerData* settingsTimer = Settings::FindTimer(timerId);
                if (settingsTimer && settingsTimer->isRoomTimer && settingsTimer->roomId == roomId) {
                    // Check if this timer already exists in activeTimers
                    bool found = false;
                    for (const auto& timer : activeTimers) {
                        if (timer.id == timerId && timer.roomId == roomId) {
                            found = true;
                            break;
                        }
                    }

                    if (!found) {
                        // Add to active timers (initially paused)
                        addOrUpdateActiveTimer(ActiveTimer(timerId, settingsTimer->duration, true, roomId));

                        if (APIDefs) {
                            char logMsg[256];
                            sprintf_s(logMsg, "Added subscribed timer to active list: %s",
                                settingsTimer->name.c_str());
                            APIDefs->Log(ELogLevel_INFO, ADDON_NAME, logMsg);
                        }
                    }

                    // Send subscription to server
                    this->subscribeToTimer(timerId, roomId);
                }
            }
            else {
                if (APIDefs) {
                    char logMsg[256];
                    sprintf_s(logMsg, "Subscription exists for timer that's not valid: %s", timerId.c_str());
                    APIDefs->Log(ELogLevel_INFO, ADDON_NAME, logMsg);
                }
            }
        }
    }
    catch (const std::exception& e) {
        if (APIDefs) {
            char errorMsg[256];
            sprintf_s(errorMsg, "Error loading subscribed timers: %s", e.what());
            APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, errorMsg);
        }
    }
    catch (...) {
        if (APIDefs) {
            APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, "Unknown error loading subscribed timers");
        }
    }
}