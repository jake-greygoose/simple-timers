#ifndef GUI_H
#define GUI_H

#include <string>

class WebSocketClient;

// Global variables shared by GUI components.
extern bool showCreateTimerWindow;
extern bool showEditTimerWindow;
extern bool showSettingsWindow;
extern std::string editTimerId;

// Declarations for GUI rendering functions.
void RenderMainTimersWindow();
void RenderCreateTimerWindow();
void RenderEditTimerWindow();
void RenderSettingsWindow();
void RenderWebSocketTab();

void RenderOptions();
void InitializeWebSocketClient(const std::string& serverUrl, bool autoConnect);
bool SafeConnect(WebSocketClient* client, const std::string& url);

#endif // GUI_H
