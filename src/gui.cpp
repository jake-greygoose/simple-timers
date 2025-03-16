#define NOMINMAX
#include "gui.h"
#include "imgui/imgui.h"
#include "shared.h"
#include "settings.h"
#include "resource.h"
#include "TextToSpeech.h"
#include "wss.h"
#include <vector>
#include <string>
#include <filesystem>
#include <map>
#include <algorithm>


bool showCreateTimerWindow = false;
bool showEditTimerWindow = false;
bool showSettingsWindow = false;
bool createInRoom = false;
std::string editTimerId = "";
bool editMode = false;
static bool g_isInitializing = false;
static bool g_connectionPending = false;
static std::chrono::steady_clock::time_point g_nextConnectionAttempt;


//-----------------------------------------------------------------
// Helper: Render the header section with the title and add button.
static void RenderTimersHeader()
{
    float buttonSize = 35.0f;

    // Begin with horizontal layout
    ImGui::BeginGroup();

    // Save initial cursor position
    ImVec2 startPos = ImGui::GetCursorPos();

    // Pre-calculate text size
    ImGui::PushFont(SanFranLarge);
    ImVec2 textSize = ImGui::CalcTextSize("Timers ");
    ImGui::PopFont();

    // Calculate vertical offset to center text with button, then add extra nudge
    float verticalOffset = (buttonSize - textSize.y) / 2.0f;
    float extraNudge = 5.0f; // Adjust this value as needed to get the alignment you want

    // Position cursor for the text (shifted down with extra nudge)
    ImGui::SetCursorPos(ImVec2(startPos.x, startPos.y + verticalOffset + extraNudge));

    // Display the title
    ImGui::PushFont(SanFranLarge);
    ImGui::Text("Timers ");
    ImGui::PopFont();

    // Position cursor for the button (at the original height)
    ImGui::SetCursorPos(ImVec2(startPos.x + textSize.x + ImGui::GetStyle().ItemSpacing.x, startPos.y));

    // Render the button
    if (AddButton && AddButton->Resource)
    {
        if (ImGui::ImageButton(AddButton->Resource, ImVec2(buttonSize, buttonSize)))
        {
            showCreateTimerWindow = true;
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::BeginTooltip();
            ImGui::Text("Add New Timer");
            ImGui::EndTooltip();
        }
    }
    else
    {
        AddButton = APIDefs->Textures.GetOrCreateFromResource("ADD_ICON", ADD_ICON, hSelf);
    }

    ImGui::EndGroup();
}

//-----------------------------------------------------------------
// Helper: Render a single timer item.
// Start a group for the timer row (containing timer and buttons)
static void RenderTimerItem(size_t index)
{
    auto& activeTimer = activeTimers[index];
    TimerData* settingsTimer = Settings::FindTimer(activeTimer.id);
    if (!settingsTimer)
        return;

    ImGui::PushID(activeTimer.id.c_str());

    int minutes = static_cast<int>(activeTimer.remainingTime) / 60;
    int seconds = static_cast<int>(activeTimer.remainingTime) % 60;

    // Determine timer color based on state.
    ImVec4 timerColor;
    if (activeTimer.isPaused)
        timerColor = Settings::colors.timerPaused;
    else if (activeTimer.remainingTime <= 0)
        timerColor = Settings::colors.timerExpired;
    else
        timerColor = Settings::colors.timerActive;

    // Display timer name on top
    ImGui::PushStyleColor(ImGuiCol_Text, timerColor);
    ImGui::Text("%s", settingsTimer->name.c_str());

    // Add room indicator if this is a room timer
    if (activeTimer.isRoomTimer()) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.5f, 0.7f, 1.0f, 1.0f), "[Room]");

        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::Text("Timer from room: %s", activeTimer.roomId.c_str());
            ImGui::EndTooltip();
        }
    }

    ImGui::PopStyleColor();

    // Begin with horizontal layout for timer display and buttons
    ImGui::BeginGroup();

    // Save initial cursor position
    ImVec2 startPos = ImGui::GetCursorPos();

    // Calculate button size based on font
    ImGui::PushFont(SanFranBig);
    const float buttonSize = ImGui::GetFontSize();

    // Pre-calculate timer text size
    ImVec2 timerTextSize = ImGui::CalcTextSize("00:00");
    ImGui::PopFont();

    // Calculate vertical offset to center buttons with timer text
    float verticalOffset = (buttonSize - timerTextSize.y) / 2.0f;
    float extraNudge = 3.0f; // Adjust this value as needed for fine-tuning

    // Position cursor for the timer text
    ImGui::SetCursorPos(ImVec2(startPos.x, startPos.y + verticalOffset + extraNudge));

    // Draw timer text with big font
    ImGui::PushStyleColor(ImGuiCol_Text, timerColor);
    ImGui::PushFont(SanFranBig);
    ImGui::Text("%02d:%02d", minutes, seconds);
    ImGui::PopFont();
    ImGui::PopStyleColor();

    // Calculate fixed position for buttons (timer width + spacing)
    float buttonStartX = startPos.x + 100.0f; // Fixed position, adjust as needed

    // Position cursor for the buttons
    ImGui::SetCursorPos(ImVec2(buttonStartX, startPos.y));

    // Group for play/pause and edit/reset buttons.
    ImGui::BeginGroup();
    if (activeTimer.isPaused)
    {
        if (PlayButton && PlayButton->Resource)
        {
            if (ImGui::ImageButton(PlayButton->Resource, ImVec2(buttonSize, buttonSize)))
            {
                activeTimer.isPaused = false;

                // NEW CODE: Send to server if it's a room timer
                if (activeTimer.isRoomTimer() && g_WebSocketClient && g_WebSocketClient->isConnected()) {
                    g_WebSocketClient->startTimer(activeTimer.id);

                    if (APIDefs) {
                        APIDefs->Log(ELogLevel_INFO, ADDON_NAME, "Sent timer start to server");
                    }
                }
            }
            ImGui::SameLine(0, 10); // 10px spacing
        }
        else
        {
            PlayButton = APIDefs->Textures.GetOrCreateFromResource("PLAY_ICON", PLAY_ICON, hSelf);
        }

        if (EditButton && EditButton->Resource)
        {
            if (ImGui::ImageButton(EditButton->Resource, ImVec2(buttonSize, buttonSize)))
            {
                editTimerId = activeTimer.id;
                showEditTimerWindow = true;
            }
            ImGui::SameLine(0, 10);
        }
        else
        {
            EditButton = APIDefs->Textures.GetOrCreateFromResource("EDIT_ICON", EDIT_ICON, hSelf);
        }
    }
    else
    {
        if (PauseButton && PauseButton->Resource)
        {
            if (ImGui::ImageButton(PauseButton->Resource, ImVec2(buttonSize, buttonSize)))
            {
                activeTimer.isPaused = true;

                // NEW CODE: Send to server if it's a room timer
                if (activeTimer.isRoomTimer() && g_WebSocketClient && g_WebSocketClient->isConnected()) {
                    g_WebSocketClient->pauseTimer(activeTimer.id);

                    if (APIDefs) {
                        APIDefs->Log(ELogLevel_INFO, ADDON_NAME, "Sent timer pause to server");
                    }
                }
            }
            ImGui::SameLine(0, 10);
        }
        else
        {
            PauseButton = APIDefs->Textures.GetOrCreateFromResource("PAUSE_ICON", PAUSE_ICON, hSelf);
        }

        if (RepeatButton && RepeatButton->Resource)
        {
            if (ImGui::ImageButton(RepeatButton->Resource, ImVec2(buttonSize, buttonSize)))
            {
                // Local timer update
                activeTimer.remainingTime = settingsTimer->duration;
                activeTimer.isPaused = true;
                activeTimer.warningPlayed = false;

                // NEW CODE: Send to server if it's a room timer
                if (activeTimer.isRoomTimer() && g_WebSocketClient && g_WebSocketClient->isConnected()) {
                    g_WebSocketClient->stopTimer(activeTimer.id);

                    if (APIDefs) {
                        APIDefs->Log(ELogLevel_INFO, ADDON_NAME, "Sent timer stop to server");
                    }
                }
            }
            ImGui::SameLine(0, 10);
        }
        else
        {
            RepeatButton = APIDefs->Textures.GetOrCreateFromResource("REPEAT_ICON", REPEAT_ICON, hSelf);
        }
    }
    if (DeleteButton && DeleteButton->Resource)
    {
        if (ImGui::ImageButton(DeleteButton->Resource, ImVec2(buttonSize, buttonSize)))
        {
            // NEW CODE: If this is a room timer, unsubscribe from it
            if (activeTimer.isRoomTimer() && g_WebSocketClient && g_WebSocketClient->isConnected()) {
                // Unsubscribe from the timer before removing
                g_WebSocketClient->unsubscribeFromTimer(activeTimer.id, activeTimer.roomId);

                // We don't actually delete room timers from the server,
                // just unsubscribe and remove locally
                if (APIDefs) {
                    APIDefs->Log(ELogLevel_INFO, ADDON_NAME, "Unsubscribed from room timer");
                }
            }

            // Continue with the normal delete process
            UnregisterTimerKeybind(activeTimer.id);
            Settings::RemoveTimer(activeTimer.id);
            Settings::ScheduleSave(SettingsPath);
            activeTimers.erase(activeTimers.begin() + index);
            ImGui::PopID();
            ImGui::Separator();
            return;
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::BeginTooltip();
            ImGui::Text("Delete Timer");
            ImGui::EndTooltip();
        }
    }
    else
    {
        DeleteButton = APIDefs->Textures.GetOrCreateFromResource("DELETE_ICON", DELETE_ICON, hSelf);
    }
    ImGui::EndGroup();
    ImGui::EndGroup();

    // Show timer duration text.
    ImGui::PushFont(SanFranSmall);
    std::string durationText = FormatDuration(settingsTimer->duration);
    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "%s", durationText.c_str());
    ImGui::PopFont();

    // Show sound icon with tooltip.
    if (SoundButton && SoundButton->Resource)
    {
        ImGui::SameLine();
        ImGui::Image(SoundButton->Resource, ImVec2(16, 16));
        if (ImGui::IsItemHovered())
        {
            ImGui::BeginTooltip();
            std::string endSoundName = "Sound";
            std::string warningSoundName = "Warning Sound";
            if (g_SoundEngine)
            {
                const auto& availableSounds = g_SoundEngine->GetAvailableSounds();
                for (const auto& sound : availableSounds)
                {
                    if (sound.id.ToString() == settingsTimer->endSound.ToString())
                        endSoundName = sound.name;
                    if (settingsTimer->useWarning &&
                        sound.id.ToString() == settingsTimer->warningSound.ToString())
                        warningSoundName = sound.name;
                }
            }
            ImGui::Text("End Sound: %s", endSoundName.c_str());
            if (settingsTimer->useWarning)
                ImGui::Text("Warning at %.0f seconds: %s", settingsTimer->warningTime, warningSoundName.c_str());
            ImGui::EndTooltip();
        }
    }
    else
    {
        SoundButton = APIDefs->Textures.GetOrCreateFromResource("SOUND_ICON", SOUND_ICON, hSelf);
    }

    // Update timer logic if not paused.
    if (!activeTimer.isPaused)
    {
        activeTimer.remainingTime -= ImGui::GetIO().DeltaTime;
        if (settingsTimer->useWarning && !activeTimer.warningPlayed &&
            activeTimer.remainingTime <= settingsTimer->warningTime)
        {
            PlaySoundEffect(settingsTimer->warningSound);
            activeTimer.warningPlayed = true;
        }
        if (activeTimer.remainingTime <= 0.0f)
        {
            PlaySoundEffect(settingsTimer->endSound);
            activeTimer.remainingTime = settingsTimer->duration;
            activeTimer.isPaused = true;
            activeTimer.warningPlayed = false;

            // NEW CODE: Send to server if it's a room timer
            if (activeTimer.isRoomTimer() && g_WebSocketClient && g_WebSocketClient->isConnected()) {
                g_WebSocketClient->stopTimer(activeTimer.id);

                if (APIDefs) {
                    APIDefs->Log(ELogLevel_INFO, ADDON_NAME, "Sent timer completed to server");
                }
            }
        }
    }

    ImGui::PopID();
    ImGui::Separator();
}



void InitializeWebSocketClient(const std::string& serverUrl, bool autoConnect) {
    // Prevent multiple simultaneous initialization attempts
    if (g_isInitializing) {
        if (APIDefs) {
            APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, "WebSocket client initialization already in progress");
        }
        return;
    }
    g_isInitializing = true;

    // Make sure old client is fully cleaned up first
    if (g_WebSocketClient) {
        try {
            g_WebSocketClient->safeShutdown();
            g_WebSocketClient.reset();
            if (APIDefs) {
                APIDefs->Log(ELogLevel_INFO, ADDON_NAME, "Previous WebSocket client cleaned up");
            }
        }
        catch (...) {
            if (APIDefs) {
                APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, "Error during WebSocket client cleanup");
            }
        }
    }

    try {
        // Create a new client
        g_WebSocketClient = std::make_unique<WebSocketClient>();
        if (APIDefs) {
            APIDefs->Log(ELogLevel_INFO, ADDON_NAME, "New WebSocket client created");
        }

        // Set callbacks
        g_WebSocketClient->setStatusCallback([](const std::string& status) {
            Settings::SetWebSocketConnectionStatus(status);

            // When connection is established, schedule a timer sync
            if (status == "Connected" || status == "Connected (Secure)") {
                std::string currentRoomId = Settings::GetCurrentRoom();
                if (!currentRoomId.empty()) {
                    // Schedule a delayed timer sync to ensure room join is complete
                    std::thread([currentRoomId]() {
                        // Wait a bit for room join to complete
                        std::this_thread::sleep_for(std::chrono::milliseconds(2000));

                        if (g_WebSocketClient && g_WebSocketClient->isConnected()) {
                            // First request a refresh of room timers
                            g_WebSocketClient->refreshRooms();

                            // Then schedule loading subscribed timers with a slight delay
                            std::this_thread::sleep_for(std::chrono::milliseconds(500));

                            // Get all subscribed timer IDs for the current room
                            auto subscriptions = Settings::GetSubscriptionsForRoom(currentRoomId);
                            for (const auto& timerId : subscriptions) {
                                TimerData* settingsTimer = Settings::FindTimer(timerId);
                                if (settingsTimer && settingsTimer->isRoomTimer && settingsTimer->roomId == currentRoomId) {
                                    // Create a room timer in our active timers list if not already there
                                    bool found = false;
                                    for (const auto& timer : activeTimers) {
                                        if (timer.id == timerId && timer.roomId == currentRoomId) {
                                            found = true;
                                            break;
                                        }
                                    }

                                    if (!found) {
                                        // Add to active timers and subscribe on server
                                        addOrUpdateActiveTimer(ActiveTimer(timerId, settingsTimer->duration, true, currentRoomId));
                                        g_WebSocketClient->subscribeToTimer(timerId, currentRoomId);

                                        if (APIDefs) {
                                            char logMsg[256];
                                            sprintf_s(logMsg, "Auto-added subscribed timer: %s", settingsTimer->name.c_str());
                                            APIDefs->Log(ELogLevel_INFO, ADDON_NAME, logMsg);
                                        }
                                    }
                                }
                            }
                        }
                        }).detach();
                }
            }
            });

        g_WebSocketClient->setMessageCallback([](const std::string& direction, const std::string& message) {
            Settings::AddWebSocketLogEntry(direction, message);
            });

        // Auto-connect if configured but with a slight delay
        if (autoConnect) {
            g_connectionPending = true;
            g_nextConnectionAttempt = std::chrono::steady_clock::now() + std::chrono::milliseconds(300);
            if (APIDefs) {
                APIDefs->Log(ELogLevel_INFO, ADDON_NAME, "WebSocket connection scheduled");
            }
        }
    }
    catch (const std::exception& e) {
        if (APIDefs) {
            char errorMsg[256];
            sprintf_s(errorMsg, "Error initializing WebSocket client: %s", e.what());
            APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, errorMsg);
        }
    }
    catch (...) {
        if (APIDefs) {
            APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, "Unknown error initializing WebSocket client");
        }
    }

    g_isInitializing = false;
}

// Function to safely connect
bool SafeConnect(WebSocketClient* client, const std::string& url) {
    if (!client) {
        if (APIDefs) {
            APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, "Cannot connect: WebSocket client is null");
        }
        return false;
    }

    try {
        if (APIDefs) {
            APIDefs->Log(ELogLevel_INFO, ADDON_NAME, "Attempting WebSocket connection");
        }

        bool result = client->connect(url);

        if (result) {
            Settings::SetWebSocketConnectionStatus("Connecting...");
            if (APIDefs) {
                APIDefs->Log(ELogLevel_INFO, ADDON_NAME, "WebSocket connection initiated");
            }
        }
        else {
            if (APIDefs) {
                APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, "WebSocket connection failed to initiate");
            }
        }

        return result;
    }
    catch (const std::exception& e) {
        if (APIDefs) {
            char errorMsg[256];
            sprintf_s(errorMsg, "Error connecting WebSocket: %s", e.what());
            APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, errorMsg);
        }
        return false;
    }
    catch (...) {
        if (APIDefs) {
            APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, "Unknown error connecting WebSocket");
        }
        return false;
    }
}

void refreshRoomTimers() {
    if (g_WebSocketClient && g_WebSocketClient->isConnected()) {
        std::string currentRoomId = Settings::GetCurrentRoom();
        if (!currentRoomId.empty()) {
            // Request refresh of room timers
            g_WebSocketClient->refreshRooms();

            if (APIDefs) {
                APIDefs->Log(ELogLevel_INFO, ADDON_NAME, "Refreshing room timers list");
            }
        }
    }
}


//-----------------------------------------------------------------
// Render the main timers window.
void RenderMainTimersWindow()
{
    ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize;
    windowFlags |= (Settings::showTitle ? 0 : ImGuiWindowFlags_NoTitleBar);
    windowFlags |= (Settings::allowResize ? 0 : ImGuiWindowFlags_NoResize);

    ImGui::PushStyleColor(ImGuiCol_WindowBg, Settings::colors.background);
    ImGui::PushStyleColor(ImGuiCol_Text, Settings::colors.text);
    ImGui::Begin("Timers", nullptr, windowFlags);

    // Update window position and size in settings.
    Settings::windowPosition = ImGui::GetWindowPos();
    Settings::windowSize = ImGui::GetWindowSize();

    RenderTimersHeader();
    ImGui::Separator();

    if (activeTimers.empty())
    {
        ImGui::TextColored(ImVec4(0.75f, 0.75f, 0.75f, 1.0f), "No timers found.");
    }
    else
    {
        std::string currentRoomId = Settings::GetCurrentRoom();
        auto subscriptions = Settings::GetSubscriptionsForRoom(currentRoomId);

        for (size_t i = 0; i < activeTimers.size(); i++) {
            const auto& timer = activeTimers[i];
            // Only render local timers or subscribed room timers
            if (!timer.isRoomTimer() ||
                (timer.isRoomTimer() && std::find(subscriptions.begin(), subscriptions.end(), timer.id) != subscriptions.end())) {
                RenderTimerItem(i);
            }
        }
    }

    ImGui::PopStyleColor(2);
    ImGui::End();
}

//-----------------------------------------------------------------
// Render the Create Timer window (full implementation with fixes).
void RenderCreateTimerWindow()
{
    if (!showCreateTimerWindow)
        return;

    ImGui::SetNextWindowSize(ImVec2(380, 450), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Create Timer", &showCreateTimerWindow))
    {
        // Static variables for controlling the create timer form.
        static char timerName[128] = "New Timer";
        static int hours = 0;
        static int minutes = 0;
        static int seconds = 40;
        static bool useWarning = false;
        static int warningSeconds = 10;
        static int selectedSoundIndex = 0;
        static int selectedWarningSoundIndex = 1;
        static bool initialized = false;

        if (!initialized)
        {
            strcpy_s(timerName, sizeof(timerName), "New Timer");
            hours = 0;
            minutes = 5;
            seconds = 0;
            useWarning = false;
            warningSeconds = 30;
            selectedSoundIndex = 0;
            selectedWarningSoundIndex = 1;
            createInRoom = false;
            initialized = true;
        }

        // Get available sounds.
        std::vector<std::string> soundNames;
        std::vector<SoundID> soundIds;
        if (g_SoundEngine)
        {
            const auto& availableSounds = g_SoundEngine->GetAvailableSounds();
            for (const auto& sound : availableSounds)
            {
                if (sound.category == "Built-in")
                {
                    soundNames.push_back(sound.name);
                    soundIds.push_back(sound.id);
                }
            }
            for (const auto& sound : availableSounds)
            {
                if (sound.category == "Custom")
                {
                    soundNames.push_back(sound.name + " (Custom)");
                    soundIds.push_back(sound.id);
                }
            }
            for (const auto& sound : availableSounds)
            {
                if (sound.category == "Text-to-Speech")
                {
                    soundNames.push_back(sound.name + " (TTS)");
                    soundIds.push_back(sound.id);
                }
            }
        }

        if (soundNames.empty())
        {
            soundNames = { "Success Chime", "Info Chime", "Warning Chime" };
            soundIds = {
                SoundID(themes_chime_success),
                SoundID(themes_chime_info),
                SoundID(themes_chime_warning)
            };
        }
        if (selectedSoundIndex >= soundNames.size())
            selectedSoundIndex = 0;

        ImGui::SetNextWindowSize(ImVec2(380, 450), ImGuiCond_FirstUseEver);
        // Timer Name
        ImGui::Text("Timer Name");
        ImGui::PushItemWidth(240);
        ImGui::InputText("##TimerName", timerName, IM_ARRAYSIZE(timerName));
        ImGui::PopItemWidth();
        ImGui::Spacing();

        // Time inputs with arrow buttons.
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(2, 4));
        ImGui::SetNextItemWidth(60.0f);
        ImGui::Text("Hours  ");
        ImGui::SameLine();
        if (ImGui::ArrowButton("##hour_down", ImGuiDir_Left)) { if (hours > 0) hours--; }
        ImGui::SameLine(0, 2);
        ImGui::PushItemWidth(50);
        if (ImGui::InputInt("##hours", &hours, 0))
            hours = std::max(0, std::min(hours, 23));
        ImGui::PopItemWidth();
        ImGui::SameLine(0, 2);
        if (ImGui::ArrowButton("##hour_up", ImGuiDir_Right)) { if (hours < 23) hours++; }

        ImGui::SetNextItemWidth(60.0f);
        ImGui::Text("Minutes");
        ImGui::SameLine();
        if (ImGui::ArrowButton("##min_down", ImGuiDir_Left)) { if (minutes > 0) minutes--; }
        ImGui::SameLine(0, 2);
        ImGui::PushItemWidth(50);
        if (ImGui::InputInt("##minutes", &minutes, 0))
            minutes = std::max(0, std::min(minutes, 59));
        ImGui::PopItemWidth();
        ImGui::SameLine(0, 2);
        if (ImGui::ArrowButton("##min_up", ImGuiDir_Right)) { if (minutes < 59) minutes++; }

        ImGui::SetNextItemWidth(60.0f);
        ImGui::Text("Seconds");
        ImGui::SameLine();
        if (ImGui::ArrowButton("##sec_down", ImGuiDir_Left)) { if (seconds > 0) seconds--; }
        ImGui::SameLine(0, 2);
        ImGui::PushItemWidth(50);
        if (ImGui::InputInt("##seconds", &seconds, 0))
            seconds = std::max(0, std::min(seconds, 59));
        ImGui::PopItemWidth();
        ImGui::SameLine(0, 2);
        if (ImGui::ArrowButton("##sec_up", ImGuiDir_Right)) { if (seconds < 59) seconds++; }
        ImGui::PopStyleVar();
        ImGui::Spacing();

        // Calculate and display total duration.
        float totalDuration = hours * 3600.0f + minutes * 60.0f + seconds;
        ImGui::Text("Total Duration: %s", FormatDuration(totalDuration).c_str());
        ImGui::Separator();

        // End Sound selection.
        ImGui::Text("End Sound");
        ImGui::PushItemWidth(240);
        if (ImGui::BeginCombo("##EndSound", soundNames[selectedSoundIndex].c_str()))
        {
            for (int i = 0; i < static_cast<int>(soundNames.size()); i++)
            {
                bool isSelected = (selectedSoundIndex == i);
                if (ImGui::Selectable(soundNames[i].c_str(), isSelected))
                    selectedSoundIndex = i;
                if (isSelected)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        ImGui::PopItemWidth();
        ImGui::SameLine();
        if (ImGui::Button("Test"))
        {
            if (g_SoundEngine && selectedSoundIndex < static_cast<int>(soundIds.size()))
                g_SoundEngine->PlaySound(soundIds[selectedSoundIndex]);
        }
        ImGui::Spacing();

        // Warning Notification section.
        ImGui::Checkbox("Use Warning Notification", &useWarning);
        if (useWarning)
        {
            ImGui::Text("Warn before end");
            ImGui::PushItemWidth(170);
            if (ImGui::InputInt("##WarningTime", &warningSeconds, 1, 5))
            {
                warningSeconds = std::max(1, warningSeconds);
                warningSeconds = std::min(warningSeconds, static_cast<int>(totalDuration - 1));
            }
            ImGui::PopItemWidth();
            ImGui::SameLine();
            ImGui::Text("seconds");
            ImGui::Text("Warning Sound");
            ImGui::PushItemWidth(240);
            if (selectedWarningSoundIndex >= static_cast<int>(soundNames.size()))
                selectedWarningSoundIndex = 0;
            if (ImGui::BeginCombo("##WarningSound", soundNames[selectedWarningSoundIndex].c_str()))
            {
                for (int i = 0; i < static_cast<int>(soundNames.size()); i++)
                {
                    bool isSelected = (selectedWarningSoundIndex == i);
                    if (ImGui::Selectable(soundNames[i].c_str(), isSelected))
                        selectedWarningSoundIndex = i;
                    if (isSelected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            ImGui::PopItemWidth();
            ImGui::SameLine();
            if (ImGui::Button("Test##warn"))
            {
                if (g_SoundEngine && selectedWarningSoundIndex < static_cast<int>(soundIds.size()))
                    g_SoundEngine->PlaySound(soundIds[selectedWarningSoundIndex]);
            }
        }
        ImGui::Spacing();

        // Room timer option (only if in a room)
        std::string currentRoomId = Settings::GetCurrentRoom();
        bool inRoom = !currentRoomId.empty();

        if (inRoom)
        {
            // Option to create timer in room
            ImGui::Separator();
            ImGui::Checkbox("Create as room timer", &createInRoom);

            if (createInRoom)
            {
                ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.0f, 1.0f),
                    "This timer will be shared with all users in the room");
            }
            else
            {
                ImGui::TextColored(ImVec4(0.0f, 0.9f, 0.9f, 1.0f),
                    "This timer will be local only");
            }
        }
        else
        {
            createInRoom = false;
        }

        ImGui::Separator();

        // Action buttons.
        bool actionEnabled = (totalDuration > 0 && strlen(timerName) > 0);
        if (!actionEnabled)
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.5f);
        static int selectedTimerIdx = -1;
        static bool editMode = false;
        if (editMode)
        {
            if (ImGui::Button("Update Timer", ImVec2(120, 0)) && actionEnabled)
            {
                TimerData* timer = Settings::FindTimer(editTimerId);
                if (timer)
                {
                    timer->name = timerName;
                    timer->duration = totalDuration;
                    timer->endSound = soundIds[selectedSoundIndex];
                    timer->useWarning = useWarning;
                    if (useWarning)
                    {
                        timer->warningTime = static_cast<float>(warningSeconds);
                        timer->warningSound = soundIds[selectedWarningSoundIndex];
                    }
                    for (auto& activeTimer : activeTimers)
                    {
                        if (activeTimer.id == editTimerId)
                        {
                            if (activeTimer.isPaused)
                                activeTimer.remainingTime = totalDuration;
                            activeTimer.warningPlayed = false;
                            break;
                        }
                    }

                    // Save the settings after updating the timer
                    Settings::ScheduleSave(SettingsPath);

                    // Close the window
                    showCreateTimerWindow = false;
                    initialized = false;  // Reset the initialization flag
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel Edit", ImVec2(120, 0)))
            {
                selectedTimerIdx = -1;
                editMode = false;
            }
        }
        else
        {
            if (ImGui::Button("Create Timer", ImVec2(120, 0)) && actionEnabled)
            {
                if (createInRoom && inRoom && g_WebSocketClient && g_WebSocketClient->isConnected())
                {
                    // Create a timer on the server
                    if (g_WebSocketClient->createTimer(timerName, totalDuration))
                    {
                        // The timer will be added to activeTimers when the server confirms
                        if (APIDefs)
                        {
                            APIDefs->Log(ELogLevel_INFO, ADDON_NAME, "Creating timer on server");
                        }
                    }
                }
                else
                {
                    // Create a local timer
                    TimerData& newTimer = Settings::AddTimer(timerName, totalDuration);
                    newTimer.endSound = soundIds[selectedSoundIndex];
                    newTimer.useWarning = useWarning;
                    if (useWarning)
                    {
                        newTimer.warningTime = static_cast<float>(warningSeconds);
                        newTimer.warningSound = soundIds[selectedWarningSoundIndex];
                    }
                    activeTimers.push_back(ActiveTimer(newTimer.id, newTimer.duration, true));
                    RegisterTimerKeybind(newTimer.id);
                }

                // Save the settings after creating the timer
                Settings::ScheduleSave(SettingsPath);

                // Reset the form
                strcpy_s(timerName, sizeof(timerName), "New Timer");
                hours = 0;
                minutes = 5;
                seconds = 0;
                createInRoom = false;

                // Close the window instead of entering edit mode
                showCreateTimerWindow = false;
                initialized = false;  // Reset the initialization flag
            }
            ImGui::SameLine();
            if (ImGui::Button("Reset Form", ImVec2(120, 0)))
            {
                strcpy_s(timerName, sizeof(timerName), "New Timer");
                hours = 0;
                minutes = 5;
                seconds = 0;
                useWarning = false;
                warningSeconds = 30;
                selectedSoundIndex = 0;
                selectedWarningSoundIndex = 0;
                createInRoom = false;
            }
        }
        if (!actionEnabled)
            ImGui::PopStyleVar();
    }
    ImGui::End();
}

void RenderRoomsTab() {
    bool changed = false;

    // Check if we're connected to a WebSocket server
    bool isConnected = g_WebSocketClient && g_WebSocketClient->isConnected();

    if (!isConnected) {
        ImGui::TextColored(ImVec4(0.9f, 0.4f, 0.3f, 1.0f),
            "You must connect to a WebSocket server to use rooms");
        ImGui::Text("Go to the WebSocket tab to connect.");

        // If we have saved rooms or subscriptions, show information about them
        int roomCount = 0;
        int subscriptionCount = 0;

        {
            std::lock_guard<std::mutex> lock(Settings::Mutex);
            roomCount = Settings::websocket.roomSubscriptions.size();

            // Count total subscriptions across all rooms
            for (const auto& [roomId, timers] : Settings::websocket.roomSubscriptions) {
                subscriptionCount += timers.size();
            }
        }

        if (roomCount > 0) {
            ImGui::Separator();
            ImGui::Text("You have saved subscriptions in %d rooms", roomCount);
            ImGui::Text("Total timer subscriptions: %d", subscriptionCount);
            ImGui::Text("Connect to a server to access these rooms");
        }

        return;
    }

    // Current room info
    std::string currentRoomId = Settings::GetCurrentRoom();
    bool inRoom = !currentRoomId.empty();

    if (inRoom) {
        // Display current room info
        ImGui::TextColored(ImVec4(0.0f, 0.8f, 0.0f, 1.0f), "Currently in room:");

        // Find room name from the available rooms
        const auto& rooms = Settings::GetAvailableRooms();
        std::string roomName = "Unknown";
        int clientCount = 0;
        for (const auto& room : rooms) {
            if (room.id == currentRoomId) {
                roomName = room.name;
                clientCount = room.clientCount;
                break;
            }
        }

        ImGui::Text("Room: %s (%d clients)", roomName.c_str(), clientCount);

        // Leave room button
        if (ImGui::Button("Leave Room")) {
            if (g_WebSocketClient) {
                // Store the room ID before leaving
                std::string leavingRoomId = currentRoomId;

                // Send leave room request
                g_WebSocketClient->leaveRoom();

                // Remove all timers for this room
                removeAllRoomTimers(leavingRoomId);
            }
        }
    }
    else {
        ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.0f, 1.0f), "Not currently in a room");
    }

    ImGui::Separator();

    // Available rooms section
    ImGui::Text("Available Rooms");

    const auto& rooms = Settings::GetAvailableRooms();
    if (rooms.empty()) {
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "No rooms available");
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Create a room or refresh the list");
    }
    else {
        // Display room list
        ImGui::BeginChild("RoomsList", ImVec2(ImGui::GetContentRegionAvail().x, 150), true);
        for (const auto& room : rooms) {
            ImGui::PushID(room.id.c_str());

            // Room name with client count
            std::string roomDisplay = room.name + " (" + std::to_string(room.clientCount) + " clients)";

            // Add lock icon for password-protected rooms
            if (!room.isPublic) {
                roomDisplay += " [LOCKED]";

            }

            // Add badge for rooms with subscriptions
            {
                std::lock_guard<std::mutex> lock(Settings::Mutex);
                auto it = Settings::websocket.roomSubscriptions.find(room.id);
                if (it != Settings::websocket.roomSubscriptions.end() && !it->second.empty()) {
                    roomDisplay += " [" + std::to_string(it->second.size()) + " subscriptions]";
                }
            }

            bool isSelected = (room.id == currentRoomId);
            if (ImGui::Selectable(roomDisplay.c_str(), isSelected)) {
                // Store the old room ID before joining a new one
                std::string oldRoomId = Settings::GetCurrentRoom();

                // If not password protected, join immediately
                if (room.isPublic) {
                    if (g_WebSocketClient) {
                        // First remove timers from the old room
                        if (!oldRoomId.empty()) {
                            removeAllRoomTimers(oldRoomId);
                        }

                        // Now join the new room
                        g_WebSocketClient->joinRoom(room.id);
                    }
                }
                else {
                    // For password protected rooms, show password dialog
                    // Store the old room ID for later use when actually joining
                    Settings::websocket.oldRoomId = oldRoomId;
                    ImGui::OpenPopup("Enter Room Password");
                    // Store the selected room ID for the password dialog
                    Settings::websocket.selectedRoomId = room.id;
                }
            }

            ImGui::PopID();
        }
        ImGui::EndChild();
    }

    // Password dialog
    static char password[64] = "";
    // Password dialog
    if (ImGui::BeginPopupModal("Enter Room Password", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Enter password for the selected room:");
        ImGui::InputText("##RoomPassword", password, IM_ARRAYSIZE(password), ImGuiInputTextFlags_Password);

        if (ImGui::Button("Join", ImVec2(120, 0))) {
            if (g_WebSocketClient) {
                // First remove timers from the old room
                if (!Settings::websocket.oldRoomId.empty()) {
                    removeAllRoomTimers(Settings::websocket.oldRoomId);
                }

                // Now join the new room
                g_WebSocketClient->joinRoom(Settings::websocket.selectedRoomId, password);
            }
            ImGui::CloseCurrentPopup();
            memset(password, 0, sizeof(password)); // Clear password
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) {
            ImGui::CloseCurrentPopup();
            memset(password, 0, sizeof(password)); // Clear password
        }
        ImGui::EndPopup();
    }

    // Button row
    ImGui::BeginGroup();
    // Refresh button for room list
    if (ImGui::Button("Refresh Rooms")) {
        if (g_WebSocketClient) {
            // Request updated room list
            g_WebSocketClient->refreshRooms();
        }
    }

    ImGui::SameLine();

    // Create room button
    if (ImGui::Button("Create New Room")) {
        ImGui::OpenPopup("Create Room");
    }

    // Add a button to clean up subscriptions
    ImGui::SameLine();
    if (ImGui::Button("Clean Subscriptions")) {
        Settings::CleanupSubscriptions();
        ImGui::OpenPopup("Subscription Cleanup");
    }
    ImGui::EndGroup();

    // Cleanup result popup
    if (ImGui::BeginPopupModal("Subscription Cleanup", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Your timer subscriptions have been cleaned.");
        ImGui::Text("Subscriptions to rooms or timers that no longer exist");
        ImGui::Text("have been removed from your settings.");

        if (ImGui::Button("OK", ImVec2(120, 0))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    // Create room dialog
    static char roomName[128] = "My Room";
    static bool isPublic = true;
    static char roomPassword[64] = "";

    if (ImGui::BeginPopupModal("Create Room", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Room Name:");
        ImGui::InputText("##RoomName", roomName, IM_ARRAYSIZE(roomName));

        ImGui::Checkbox("Public Room", &isPublic);

        if (!isPublic) {
            ImGui::Text("Room Password:");
            ImGui::InputText("##RoomPassword", roomPassword, IM_ARRAYSIZE(roomPassword), ImGuiInputTextFlags_Password);
        }

        if (ImGui::Button("Create", ImVec2(120, 0))) {
            if (g_WebSocketClient) {
                g_WebSocketClient->createRoom(roomName, isPublic, isPublic ? "" : roomPassword);
            }
            ImGui::CloseCurrentPopup();
            // Reset fields
            strcpy_s(roomName, sizeof(roomName), "My Room");
            isPublic = true;
            memset(roomPassword, 0, sizeof(roomPassword));
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    // Timer subscription section
// In the Timer subscription section of RenderRoomsTab
    if (inRoom) {
        ImGui::Separator();
        ImGui::Text("Room Timers");

        // Begin the room timers list
        ImGui::BeginChild("RoomTimers", ImVec2(ImGui::GetContentRegionAvail().x, 150), true);
        bool hasRoomTimers = false;

        if (g_WebSocketClient && g_WebSocketClient->isConnected()) {
            // Get all timers that belong to this room from Settings
            std::vector<TimerData*> roomTimers;
            for (auto& timer : Settings::timers) {
                if (timer.isRoomTimer && timer.roomId == currentRoomId) {
                    roomTimers.push_back(&timer);
                    hasRoomTimers = true;
                }
            }

            if (hasRoomTimers) {
                // Sort timers by name for better display
                std::sort(roomTimers.begin(), roomTimers.end(), [](const TimerData* a, const TimerData* b) {
                    return a->name < b->name;
                    });

                for (const auto* timerData : roomTimers) {
                    ImGui::PushID(timerData->id.c_str());

                    // Begin a group for this timer row
                    ImGui::BeginGroup();

                    // Check if we're subscribed to this timer
                    bool isSubscribed = Settings::IsSubscribedToTimer(timerData->id, currentRoomId);

                    // Find timer status (running/paused)
                    bool isPaused = true;
                    for (const auto& activeTimer : activeTimers) {
                        if (activeTimer.id == timerData->id && activeTimer.roomId == currentRoomId) {
                            isPaused = activeTimer.isPaused;
                            break;
                        }
                    }

                    // Use colored text for subscribed timers
                    if (isSubscribed) {
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 0.9f, 0.0f, 1.0f)); // Green for subscribed
                    }
                    else {
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.7f, 0.7f, 1.0f)); // Gray for non-subscribed
                    }

                    // Display timer name and subscription checkbox (using a smaller width for the checkbox)
                    float checkboxWidth = 20.0f; // Width of the checkbox
                    ImGui::SetNextItemWidth(checkboxWidth);
                    if (ImGui::Checkbox("##sub", &isSubscribed)) {
                        if (isSubscribed) {
                            // Subscribe
                            Settings::SubscribeToTimer(timerData->id, currentRoomId);
                            if (g_WebSocketClient) {
                                g_WebSocketClient->subscribeToTimer(timerData->id, currentRoomId);
                            }

                            // Add to active timers if not already there
                            bool found = false;
                            for (const auto& activeTimer : activeTimers) {
                                if (activeTimer.id == timerData->id && activeTimer.roomId == currentRoomId) {
                                    found = true;
                                    break;
                                }
                            }

                            if (!found) {
                                addOrUpdateActiveTimer(ActiveTimer(timerData->id, timerData->duration, isPaused, currentRoomId));
                            }
                        }
                        else {
                            // Unsubscribe
                            Settings::UnsubscribeFromTimer(timerData->id, currentRoomId);
                            if (g_WebSocketClient) {
                                g_WebSocketClient->unsubscribeFromTimer(timerData->id, currentRoomId);
                            }

                            // Remove from active timers
                            removeRoomTimer(timerData->id, currentRoomId);
                        }
                        changed = true;
                    }

                    // Display timer name next to checkbox
                    ImGui::SameLine();
                    ImGui::Text("%s", timerData->name.c_str());

                    // Display total time on the same line
                    ImGui::SameLine();
                    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "(%s)", FormatDuration(timerData->duration).c_str());

                    // Display timer status
                    ImGui::SameLine();
                    if (!isPaused) {
                        ImGui::TextColored(ImVec4(0.0f, 0.9f, 0.0f, 1.0f), "[Running]");
                    }
                    else {
                        ImGui::TextColored(ImVec4(0.9f, 0.7f, 0.0f, 1.0f), "[Paused]");
                    }

                    // Calculate remaining space for buttons
                    float availWidth = ImGui::GetContentRegionAvail().x;
                    float buttonSize = 24.0f; // Size of the icon buttons
                    float rightAlignPos = ImGui::GetCursorPosX() + availWidth - (buttonSize * 2 + 8); // 2 buttons with spacing

                    // Right-aligned edit and delete buttons
                    ImGui::SameLine(rightAlignPos);

                    // Edit button
                    if (EditButton && EditButton->Resource) {
                        if (ImGui::ImageButton(EditButton->Resource, ImVec2(buttonSize, buttonSize))) {
                            // Set the edit timer ID and show edit window
                            editTimerId = timerData->id;
                            showEditTimerWindow = true;
                        }
                        if (ImGui::IsItemHovered()) {
                            ImGui::BeginTooltip();
                            ImGui::Text("Edit Timer");
                            ImGui::EndTooltip();
                        }
                    }

                    // Delete button
                    ImGui::SameLine();
                    if (DeleteButton && DeleteButton->Resource) {
                        if (ImGui::ImageButton(DeleteButton->Resource, ImVec2(buttonSize, buttonSize))) {
                            // Only show confirmation for room timers you own or have permission to delete
                            // For now, allow deletion of any room timer
                            ImGui::OpenPopup("Delete Timer##RoomTimer");
                        }
                        if (ImGui::IsItemHovered()) {
                            ImGui::BeginTooltip();
                            ImGui::Text("Delete Timer");
                            ImGui::EndTooltip();
                        }
                    }

                    // Delete confirmation popup
                    if (ImGui::BeginPopupModal("Delete Timer##RoomTimer", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
                        ImGui::Text("Are you sure you want to delete the timer \"%s\"?", timerData->name.c_str());
                        ImGui::Text("This action cannot be undone.");

                        if (ImGui::Button("Delete", ImVec2(120, 0))) {
                            // Send delete timer request to server
                            if (g_WebSocketClient) {
                                // This would require a new method in WebSocketClient
                                // g_WebSocketClient->deleteTimer(timerData->id, currentRoomId);

                                // For now, just unsubscribe from the timer
                                Settings::UnsubscribeFromTimer(timerData->id, currentRoomId);
                                if (g_WebSocketClient) {
                                    g_WebSocketClient->unsubscribeFromTimer(timerData->id, currentRoomId);
                                }

                                // Remove from active timers
                                removeRoomTimer(timerData->id, currentRoomId);
                            }

                            ImGui::CloseCurrentPopup();
                            changed = true;
                        }

                        ImGui::SameLine();

                        if (ImGui::Button("Cancel", ImVec2(120, 0))) {
                            ImGui::CloseCurrentPopup();
                        }

                        ImGui::EndPopup();
                    }

                    ImGui::PopStyleColor(); // Pop the text color
                    ImGui::EndGroup(); // End the timer row group
                    ImGui::PopID();
                }
            }
            else {
                // If no timers were found for this room
                ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "No timers in this room.");
                ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Click 'Refresh Room Timers' or create a new timer.");
            }

            // Add a refresh button at the bottom
            if (ImGui::Button("Refresh Room Timers")) {
                g_WebSocketClient->refreshRooms();
            }
        }
        else {
            ImGui::TextColored(ImVec4(0.9f, 0.4f, 0.3f, 1.0f), "WebSocket not connected");
            ImGui::TextColored(ImVec4(0.9f, 0.4f, 0.3f, 1.0f), "Connect to see room timers");
        }

        ImGui::EndChild();

        // Add timer to room button
        if (ImGui::Button("Create Room Timer")) {
            createInRoom = true;  // Preselect room timer option
            showCreateTimerWindow = true;
            editMode = false;
        }
    }

    // Show saved room subscriptions section
    ImGui::Separator();

    if (ImGui::CollapsingHeader("Saved Room Subscriptions")) {
        // The map holding room subscriptions
        const auto& roomSubscriptions = Settings::websocket.roomSubscriptions;

        if (roomSubscriptions.empty()) {
            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "No saved room subscriptions");
        }
        else {
            ImGui::BeginChild("SavedSubscriptions", ImVec2(ImGui::GetContentRegionAvail().x, 150), true);

            for (const auto& [roomId, timerIds] : roomSubscriptions) {
                // Find room name if available
                std::string roomName = "Unknown Room";
                for (const auto& room : rooms) {
                    if (room.id == roomId) {
                        roomName = room.name;
                        break;
                    }
                }

                // Display room with subscription count
                bool isOpen = ImGui::TreeNode(roomId.c_str(), "%s (%s): %zu subscriptions",
                    roomName.c_str(), roomId.c_str(), timerIds.size());

                if (isOpen) {
                    for (const auto& timerId : timerIds) {
                        ImGui::BulletText("Timer ID: %s", timerId.c_str());
                    }
                    ImGui::TreePop();
                }
            }

            ImGui::EndChild();
        }
    }

    if (changed) {
        Settings::ScheduleSave(SettingsPath);
    }
}

void RenderWebSocketTab() {
    bool changed = false;

    // Get current settings
    std::string serverUrl = Settings::GetWebSocketServerUrl();
    bool autoConnect = Settings::GetWebSocketAutoConnect();
    bool enabled = Settings::GetWebSocketEnabled();
    std::string status = Settings::GetWebSocketConnectionStatus();

    // Server URL
    ImGui::Text("WebSocket Server URL");
    char urlBuffer[256];
    strncpy_s(urlBuffer, sizeof(urlBuffer), serverUrl.c_str(), sizeof(urlBuffer) - 1);
    ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * 0.7f);
    if (ImGui::InputText("##ServerURL", urlBuffer, sizeof(urlBuffer))) {
        Settings::SetWebSocketServerUrl(urlBuffer);
        changed = true;
    }
    ImGui::PopItemWidth();

    // Connection controls
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Text("Connection Status: %s", status.c_str());

    // Create a colored indicator based on status
    ImVec4 statusColor;
    if (status == "Connected" || status == "Connected (Secure)") {
        statusColor = ImVec4(0.0f, 0.8f, 0.0f, 1.0f); // Green
    }
    else if (status.find("Error") != std::string::npos) {
        statusColor = ImVec4(0.8f, 0.0f, 0.0f, 1.0f); // Red
    }
    else {
        statusColor = ImVec4(0.8f, 0.8f, 0.0f, 1.0f); // Yellow
    }

    ImGui::SameLine();
    ImGui::ColorButton("##StatusColor", statusColor, 0, ImVec2(16, 16));

    // Display connection security information if connected
    if (g_WebSocketClient && g_WebSocketClient->isConnected()) {
        bool isSecure = g_WebSocketClient->isSecureConnection();
        std::string securityInfo = g_WebSocketClient->getConnectionDetails();

        ImGui::Text("Security: ");
        ImGui::SameLine();

        if (isSecure) {
            ImGui::TextColored(ImVec4(0.0f, 0.8f, 0.0f, 1.0f), "%s", securityInfo.c_str());
            ImGui::SameLine();
            if (ImGui::Button("Details##SecurityDetails")) {
                ImGui::OpenPopup("SecurityDetailsPopup");
            }

            if (ImGui::BeginPopup("SecurityDetailsPopup")) {
                ImGui::Text("Secure WebSocket Connection (WSS)");
                ImGui::Separator();

                ImGui::TextWrapped("This connection is secured using TLS/SSL encryption similar to HTTPS.");
                ImGui::Spacing();

                ImGui::TextWrapped("WebSocket URL: %s", g_WebSocketClient->getConnectionUrl().c_str());

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();

                ImGui::TextWrapped("TLS Configuration:");
                ImGui::Bullet(); ImGui::TextWrapped("Certificate verification: %s",
                    Settings::websocket.tlsOptions.verifyPeer ? "Enabled" : "Disabled");
                ImGui::Bullet(); ImGui::TextWrapped("Hostname verification: %s",
                    Settings::websocket.tlsOptions.verifyHost ? "Enabled" : "Disabled");

                ImGui::EndPopup();
            }
        }
        else {
            ImGui::TextColored(ImVec4(0.9f, 0.4f, 0.0f, 1.0f), "%s", securityInfo.c_str());
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Warning: Data is sent unencrypted. Consider using wss:// for secure connections.");
            }
        }
    }

    // Handle any pending connection attempts
    if (g_connectionPending && !g_isInitializing) {
        auto now = std::chrono::steady_clock::now();
        if (now >= g_nextConnectionAttempt) {
            g_connectionPending = false;

            if (APIDefs) {
                APIDefs->Log(ELogLevel_INFO, ADDON_NAME, "Executing scheduled WebSocket connection");
            }

            SafeConnect(g_WebSocketClient.get(), serverUrl);
        }
    }

    // Connect/Disconnect buttons
    bool isConnected = g_WebSocketClient && g_WebSocketClient->isConnected();

    if (!isConnected) {
        if (ImGui::Button("Connect")) {
            // Just use the same reliable method as reconnect
            std::string serverUrl = Settings::GetWebSocketServerUrl();

            if (g_WebSocketClient) {
                try {
                    // Ensure we're fully disconnected first
                    g_WebSocketClient->disconnect();
                }
                catch (...) {
                    if (APIDefs) {
                        APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, "Error during disconnect before connect");
                    }
                }
            }

            // Schedule a complete reinitialization with connection after a delay
            std::thread([serverUrl]() {
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                InitializeWebSocketClient(serverUrl, true);
                }).detach();

                Settings::SetWebSocketConnectionStatus("Connecting...");
        }
    }
    else {
        if (ImGui::Button("Disconnect")) {
            if (g_WebSocketClient) {
                try {
                    if (APIDefs) {
                        APIDefs->Log(ELogLevel_INFO, ADDON_NAME, "Disconnecting WebSocket");
                    }

                    g_WebSocketClient->disconnect();
                    Settings::SetWebSocketConnectionStatus("Disconnected");

                    if (APIDefs) {
                        APIDefs->Log(ELogLevel_INFO, ADDON_NAME, "WebSocket disconnected");
                    }
                }
                catch (...) {
                    if (APIDefs) {
                        APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, "Error during WebSocket disconnect");
                    }
                }
            }
        }
    }

    // Add a Reconnect button
    if (ImGui::Button("Reconnect")) {
        if (g_WebSocketClient) {
            try {
                g_WebSocketClient->disconnect();
                Settings::SetWebSocketConnectionStatus("Disconnecting for reconnect...");

                if (APIDefs) {
                    APIDefs->Log(ELogLevel_INFO, ADDON_NAME, "Scheduling reconnect");
                }

                // Schedule a complete reinitialization with connection
                // after a reasonable delay
                std::thread([serverUrl]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(500));
                    InitializeWebSocketClient(serverUrl, true);
                    }).detach();

            }
            catch (...) {
                if (APIDefs) {
                    APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, "Error during WebSocket reconnect");
                }
            }
        }
        else {
            // If client doesn't exist, just create a new one
            InitializeWebSocketClient(serverUrl, true);
        }
    }

    // Auto-connect and enabled checkboxes
    ImGui::SameLine();
    if (ImGui::Checkbox("Auto-connect on startup", &autoConnect)) {
        Settings::SetWebSocketAutoConnect(autoConnect);
        changed = true;
    }

    if (ImGui::Checkbox("Enable WebSocket functionality", &enabled)) {
        Settings::SetWebSocketEnabled(enabled);
        changed = true;

        // If enabling, initialize WebSocket client if needed
        if (enabled) {
            if (!g_WebSocketClient) {
                InitializeWebSocketClient(serverUrl, autoConnect);
            }
        }
        // If disabling, destroy the client
        else if (g_WebSocketClient) {
            try {
                g_WebSocketClient->safeShutdown();
                g_WebSocketClient.reset();
            }
            catch (...) {
                if (APIDefs) {
                    APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, "Error disabling WebSocket client");
                }
            }
        }
    }

    // Advanced settings
    ImGui::Spacing();
    ImGui::Separator();
    if (ImGui::CollapsingHeader("Advanced Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
        int pingInterval = Settings::websocket.pingInterval;
        if (ImGui::SliderInt("Ping interval (ms)", &pingInterval, 5000, 60000)) {
            Settings::websocket.pingInterval = pingInterval;
            changed = true;
        }

        bool autoReconnect = Settings::websocket.autoReconnect;
        if (ImGui::Checkbox("Auto-reconnect on disconnect", &autoReconnect)) {
            Settings::websocket.autoReconnect = autoReconnect;
            changed = true;
        }

        if (autoReconnect) {
            int reconnectInterval = Settings::websocket.reconnectInterval;
            if (ImGui::SliderInt("Reconnect interval (ms)", &reconnectInterval, 1000, 30000)) {
                Settings::websocket.reconnectInterval = reconnectInterval;
                changed = true;
            }

            int maxReconnects = Settings::websocket.maxReconnectAttempts;
            if (ImGui::SliderInt("Max reconnect attempts", &maxReconnects, 1, 20)) {
                Settings::websocket.maxReconnectAttempts = maxReconnects;
                changed = true;
            }
        }

        // TLS/SSL Security Settings
        if (ImGui::TreeNode("Secure WebSocket Settings (WSS)")) {
            // Check if server URL starts with wss://
            bool isWss = serverUrl.substr(0, 6) == "wss://";

            if (!isWss) {
                ImGui::TextColored(ImVec4(0.9f, 0.4f, 0.3f, 1.0f),
                    "Note: URL does not start with 'wss://' - these settings won't apply.");
                ImGui::Spacing();
            }

            // Get CA file string and check if it exists
            std::string caFileStr = Settings::websocket.tlsOptions.caFile;
            bool hasCaFile = !caFileStr.empty();

            // CA file path (moved up from below for dependency)
            char caFileBuffer[256];
            strncpy_s(caFileBuffer, sizeof(caFileBuffer),
                Settings::websocket.tlsOptions.caFile.c_str(), sizeof(caFileBuffer) - 1);
            ImGui::Text("CA Certificate File (required for verification)");
            ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * 0.7f);
            if (ImGui::InputText("##CAFile", caFileBuffer, sizeof(caFileBuffer))) {
                Settings::websocket.tlsOptions.caFile = caFileBuffer;
                hasCaFile = !Settings::websocket.tlsOptions.caFile.empty();

                // If CA file is removed, disable certificate verification
                if (!hasCaFile && Settings::websocket.tlsOptions.verifyPeer) {
                    Settings::websocket.tlsOptions.verifyPeer = false;
                }
                changed = true;
            }
            ImGui::PopItemWidth();
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Path to certificate authority file (required for certificate verification)");
            }

            // Add a visual indicator for file validity
            ImGui::SameLine();
            if (hasCaFile) {
                // Check if the file exists
                FILE* f = nullptr;
                fopen_s(&f, Settings::websocket.tlsOptions.caFile.c_str(), "r");
                bool fileExists = (f != nullptr);
                if (f) fclose(f);

                if (fileExists) {
                    ImGui::TextColored(ImVec4(0.0f, 0.8f, 0.0f, 1.0f), "File found");
                }
                else {
                    ImGui::TextColored(ImVec4(0.8f, 0.0f, 0.0f, 1.0f), "File not found");
                    hasCaFile = false;
                }
            }

            // Now the verify peer checkbox - with compatibility approach for older ImGui versions
            bool verifyPeer = Settings::websocket.tlsOptions.verifyPeer;

            // Store the original style color
            ImVec4 origCheckboxColor = ImGui::GetStyle().Colors[ImGuiCol_CheckMark];
            ImVec4 origFrameColor = ImGui::GetStyle().Colors[ImGuiCol_FrameBg];
            ImVec4 origActiveColor = ImGui::GetStyle().Colors[ImGuiCol_FrameBgActive];
            ImVec4 origHoveredColor = ImGui::GetStyle().Colors[ImGuiCol_FrameBgHovered];

            // If the checkbox should be disabled, make it appear grayed out
            if (!hasCaFile) {
                ImGui::GetStyle().Colors[ImGuiCol_CheckMark] = ImVec4(origCheckboxColor.x * 0.5f,
                    origCheckboxColor.y * 0.5f,
                    origCheckboxColor.z * 0.5f,
                    origCheckboxColor.w * 0.6f);
                ImGui::GetStyle().Colors[ImGuiCol_FrameBg] = ImVec4(origFrameColor.x * 0.5f,
                    origFrameColor.y * 0.5f,
                    origFrameColor.z * 0.5f,
                    origFrameColor.w * 0.6f);
                ImGui::GetStyle().Colors[ImGuiCol_FrameBgActive] = ImVec4(origActiveColor.x * 0.5f,
                    origActiveColor.y * 0.5f,
                    origActiveColor.z * 0.5f,
                    origActiveColor.w * 0.6f);
                ImGui::GetStyle().Colors[ImGuiCol_FrameBgHovered] = ImVec4(origHoveredColor.x * 0.5f,
                    origHoveredColor.y * 0.5f,
                    origHoveredColor.z * 0.5f,
                    origHoveredColor.w * 0.6f);
            }

            // Render the checkbox
            bool checkedBefore = verifyPeer;
            if (ImGui::Checkbox("Verify server certificate", &verifyPeer)) {
                // Only allow changes if we have a CA file
                if (hasCaFile) {
                    Settings::websocket.tlsOptions.verifyPeer = verifyPeer;
                    changed = true;
                }
                else {
                    // Revert the change if we don't have a CA file
                    verifyPeer = checkedBefore;
                }
            }

            // Show appropriate tooltip
            if (ImGui::IsItemHovered()) {
                if (hasCaFile) {
                    ImGui::SetTooltip("Validates that the server's certificate is signed by a trusted authority");
                }
                else {
                    ImGui::SetTooltip("Requires a valid CA certificate file");
                }
            }

            // Restore original style colors
            ImGui::GetStyle().Colors[ImGuiCol_CheckMark] = origCheckboxColor;
            ImGui::GetStyle().Colors[ImGuiCol_FrameBg] = origFrameColor;
            ImGui::GetStyle().Colors[ImGuiCol_FrameBgActive] = origActiveColor;
            ImGui::GetStyle().Colors[ImGuiCol_FrameBgHovered] = origHoveredColor;

            bool verifyHost = Settings::websocket.tlsOptions.verifyHost;
            if (ImGui::Checkbox("Verify certificate hostname", &verifyHost)) {
                Settings::websocket.tlsOptions.verifyHost = verifyHost;
                changed = true;
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Checks that the hostname in the certificate matches the server's hostname");
            }

            bool enableServerCertAuth = Settings::websocket.tlsOptions.enableServerCertAuth;
            if (ImGui::Checkbox("Enable server certificate authentication", &enableServerCertAuth)) {
                Settings::websocket.tlsOptions.enableServerCertAuth = enableServerCertAuth;
                changed = true;
            }

            ImGui::Spacing();
            ImGui::TextDisabled("Client certificate (for mutual TLS authentication)");

            // Client certificate file
            char certFileBuffer[256];
            strncpy_s(certFileBuffer, sizeof(certFileBuffer),
                Settings::websocket.tlsOptions.certFile.c_str(), sizeof(certFileBuffer) - 1);
            ImGui::Text("Client Certificate File");
            ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * 0.7f);
            if (ImGui::InputText("##CertFile", certFileBuffer, sizeof(certFileBuffer))) {
                Settings::websocket.tlsOptions.certFile = certFileBuffer;
                changed = true;
            }
            ImGui::PopItemWidth();

            // Client key file
            char keyFileBuffer[256];
            strncpy_s(keyFileBuffer, sizeof(keyFileBuffer),
                Settings::websocket.tlsOptions.keyFile.c_str(), sizeof(keyFileBuffer) - 1);
            ImGui::Text("Client Key File");
            ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * 0.7f);
            if (ImGui::InputText("##KeyFile", keyFileBuffer, sizeof(keyFileBuffer))) {
                Settings::websocket.tlsOptions.keyFile = keyFileBuffer;
                changed = true;
            }
            ImGui::PopItemWidth();

            ImGui::TreePop();
        }
    }

    // Message log
    ImGui::Spacing();
    ImGui::Separator();
    if (ImGui::CollapsingHeader("Message Log", ImGuiTreeNodeFlags_DefaultOpen)) {
        bool logMessages = Settings::websocket.logMessages;
        if (ImGui::Checkbox("Log messages", &logMessages)) {
            Settings::websocket.logMessages = logMessages;
            changed = true;
        }

        if (logMessages) {
            int maxLogEntries = Settings::websocket.maxLogEntries;
            if (ImGui::SliderInt("Max log entries", &maxLogEntries, 10, 1000)) {
                Settings::websocket.maxLogEntries = maxLogEntries;
                changed = true;
            }

            if (ImGui::Button("Clear Log")) {
                Settings::ClearWebSocketLog();
            }

            // Display log entries
            const auto& logEntries = Settings::GetWebSocketLog();
            if (!logEntries.empty()) {
                ImGui::BeginChild("MessageLog", ImVec2(0, 200), true);
                for (const auto& entry : logEntries) {
                    // Color-code the messages
                    ImVec4 color;
                    if (entry.direction == "sent") {
                        color = ImVec4(0.0f, 0.7f, 0.0f, 1.0f); // Green for sent
                    }
                    else {
                        color = ImVec4(0.0f, 0.5f, 0.9f, 1.0f); // Blue for received
                    }

                    ImGui::TextColored(color, "[%s] %s: %s",
                        entry.timestamp.c_str(),
                        entry.direction.c_str(),
                        entry.message.c_str());
                }

                // Auto-scroll to bottom
                if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 20) {
                    ImGui::SetScrollHereY(1.0f);
                }
                ImGui::EndChild();
            }
            else {
                ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "No messages logged yet");
            }
        }
    }

    if (changed) {
        Settings::ScheduleSave(SettingsPath);
    }
}

//-----------------------------------------------------------------
// Render the Edit Timer window (full implementation with fixes).
void RenderEditTimerWindow()
{
    if (!showEditTimerWindow || editTimerId.empty())
        return;

    // Move all static variables to function scope so they can be accessed 
    // throughout the function
    static char editTimerName[128] = "";
    static int editHours = 0;
    static int editMinutes = 0;
    static int editSeconds = 0;
    static bool editUseWarning = false;
    static int editWarningSeconds = 30;
    static int editSelectedSoundIndex = 0;
    static int editSelectedWarningSoundIndex = 1;
    static bool editInitialized = false;
    static std::string lastEditTimerId = "";  // Add this to track which timer we're editing

    TimerData* timer = Settings::FindTimer(editTimerId);
    if (timer)
    {
        // Initialize form if it's a new timer or editInitialized is false
        if (!editInitialized || lastEditTimerId != editTimerId)
        {
            strcpy_s(editTimerName, sizeof(editTimerName), timer->name.c_str());
            int totalSeconds = static_cast<int>(timer->duration);
            editHours = totalSeconds / 3600;
            editMinutes = (totalSeconds % 3600) / 60;
            editSeconds = totalSeconds % 60;
            editUseWarning = timer->useWarning;
            editWarningSeconds = static_cast<int>(timer->warningTime);
            if (g_SoundEngine)
            {
                const auto& availableSounds = g_SoundEngine->GetAvailableSounds();
                for (size_t i = 0; i < availableSounds.size(); i++)
                {
                    if (availableSounds[i].id.ToString() == timer->endSound.ToString())
                        editSelectedSoundIndex = static_cast<int>(i);
                    if (availableSounds[i].id.ToString() == timer->warningSound.ToString())
                        editSelectedWarningSoundIndex = static_cast<int>(i);
                }
            }
            editInitialized = true;
            lastEditTimerId = editTimerId;  // Update the lastEditTimerId
        }

        ImGui::SetNextWindowSize(ImVec2(380, 450), ImGuiCond_FirstUseEver);
        bool windowOpen = true;
        if (ImGui::Begin("Edit Timer", &windowOpen, ImGuiWindowFlags_None))
        {
            std::vector<std::string> soundNames;
            std::vector<SoundID> soundIds;
            if (g_SoundEngine)
            {
                const auto& availableSounds = g_SoundEngine->GetAvailableSounds();
                for (const auto& sound : availableSounds)
                {
                    if (sound.category == "Built-in")
                    {
                        soundNames.push_back(sound.name);
                        soundIds.push_back(sound.id);
                    }
                }
                for (const auto& sound : availableSounds)
                {
                    if (sound.category == "Custom")
                    {
                        soundNames.push_back(sound.name + " (Custom)");
                        soundIds.push_back(sound.id);
                    }
                }
                for (const auto& sound : availableSounds)
                {
                    if (sound.category == "Text-to-Speech")
                    {
                        soundNames.push_back(sound.name + " (TTS)");
                        soundIds.push_back(sound.id);
                    }
                }
            }

            if (soundNames.empty())
            {
                soundNames = { "Success Chime", "Info Chime", "Warning Chime" };
                soundIds = {
                    SoundID(themes_chime_success),
                    SoundID(themes_chime_info),
                    SoundID(themes_chime_warning)
                };
            }
            if (editSelectedSoundIndex >= static_cast<int>(soundNames.size()))
                editSelectedSoundIndex = 0;
            if (editSelectedWarningSoundIndex >= static_cast<int>(soundNames.size()))
                editSelectedWarningSoundIndex = 0;

            ImGui::Text("Timer Name");
            ImGui::PushItemWidth(240);
            ImGui::InputText("##EditTimerName", editTimerName, IM_ARRAYSIZE(editTimerName));
            ImGui::PopItemWidth();
            ImGui::Spacing();

            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(2, 4));
            ImGui::SetNextItemWidth(60.0f);
            ImGui::Text("Hours  ");
            ImGui::SameLine();
            if (ImGui::ArrowButton("##edit_hour_down", ImGuiDir_Left)) { if (editHours > 0) editHours--; }
            ImGui::SameLine(0, 2);
            ImGui::PushItemWidth(50);
            if (ImGui::InputInt("##edit_hours", &editHours, 0))
                editHours = std::max(0, std::min(editHours, 23));
            ImGui::PopItemWidth();
            ImGui::SameLine(0, 2);
            if (ImGui::ArrowButton("##edit_hour_up", ImGuiDir_Right)) { if (editHours < 23) editHours++; }

            ImGui::SetNextItemWidth(60.0f);
            ImGui::Text("Minutes");
            ImGui::SameLine();
            if (ImGui::ArrowButton("##edit_min_down", ImGuiDir_Left)) { if (editMinutes > 0) editMinutes--; }
            ImGui::SameLine(0, 2);
            ImGui::PushItemWidth(50);
            if (ImGui::InputInt("##edit_minutes", &editMinutes, 0))
                editMinutes = std::max(0, std::min(editMinutes, 59));
            ImGui::PopItemWidth();
            ImGui::SameLine(0, 2);
            if (ImGui::ArrowButton("##edit_min_up", ImGuiDir_Right)) { if (editMinutes < 59) editMinutes++; }

            ImGui::SetNextItemWidth(60.0f);
            ImGui::Text("Seconds");
            ImGui::SameLine();
            if (ImGui::ArrowButton("##edit_sec_down", ImGuiDir_Left)) { if (editSeconds > 0) editSeconds--; }
            ImGui::SameLine(0, 2);
            ImGui::PushItemWidth(50);
            if (ImGui::InputInt("##edit_seconds", &editSeconds, 0))
                editSeconds = std::max(0, std::min(editSeconds, 59));
            ImGui::PopItemWidth();
            ImGui::SameLine(0, 2);
            if (ImGui::ArrowButton("##edit_sec_up", ImGuiDir_Right)) { if (editSeconds < 59) editSeconds++; }
            ImGui::PopStyleVar();
            ImGui::Spacing();

            float totalDuration = editHours * 3600.0f + editMinutes * 60.0f + editSeconds;
            ImGui::Text("Total Duration: %s", FormatDuration(totalDuration).c_str());
            ImGui::Separator();

            ImGui::Text("End Sound");
            ImGui::PushItemWidth(240);
            if (ImGui::BeginCombo("##EditEndSound", soundNames[editSelectedSoundIndex].c_str()))
            {
                for (int i = 0; i < static_cast<int>(soundNames.size()); i++)
                {
                    bool isSelected = (editSelectedSoundIndex == i);
                    if (ImGui::Selectable(soundNames[i].c_str(), isSelected))
                        editSelectedSoundIndex = i;
                    if (isSelected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            ImGui::PopItemWidth();
            ImGui::SameLine();
            if (ImGui::Button("Test##edit"))
            {
                if (g_SoundEngine && editSelectedSoundIndex < static_cast<int>(soundIds.size()))
                    g_SoundEngine->PlaySound(soundIds[editSelectedSoundIndex]);
            }
            ImGui::Spacing();

            ImGui::Checkbox("Use Warning Notification##edit", &editUseWarning);
            if (editUseWarning)
            {
                ImGui::Text("Warn before end");
                ImGui::PushItemWidth(170);
                if (ImGui::InputInt("##EditWarningTime", &editWarningSeconds, 1, 5))
                {
                    editWarningSeconds = std::max(1, editWarningSeconds);
                    editWarningSeconds = std::min(editWarningSeconds, static_cast<int>(totalDuration - 1));
                }
                ImGui::PopItemWidth();
                ImGui::SameLine();
                ImGui::Text("seconds");
                ImGui::Text("Warning Sound");
                ImGui::PushItemWidth(240);
                if (ImGui::BeginCombo("##EditWarningSound", soundNames[editSelectedWarningSoundIndex].c_str()))
                {
                    for (int i = 0; i < static_cast<int>(soundNames.size()); i++)
                    {
                        bool isSelected = (editSelectedWarningSoundIndex == i);
                        if (ImGui::Selectable(soundNames[i].c_str(), isSelected))
                            editSelectedWarningSoundIndex = i;
                        if (isSelected)
                            ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
                ImGui::PopItemWidth();
                ImGui::SameLine();
                if (ImGui::Button("Test##edit_warn"))
                {
                    if (g_SoundEngine && editSelectedWarningSoundIndex < static_cast<int>(soundIds.size()))
                        g_SoundEngine->PlaySound(soundIds[editSelectedWarningSoundIndex]);
                }
            }
            ImGui::Separator();

            bool updateEnabled = (totalDuration > 0 && strlen(editTimerName) > 0);
            if (!updateEnabled)
                ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.5f);

            if (ImGui::Button("Update Timer", ImVec2(120, 0)) && updateEnabled)
            {
                timer->name = editTimerName;
                timer->duration = totalDuration;
                timer->endSound = soundIds[editSelectedSoundIndex];
                timer->useWarning = editUseWarning;
                if (editUseWarning)
                {
                    timer->warningTime = static_cast<float>(editWarningSeconds);
                    timer->warningSound = soundIds[editSelectedWarningSoundIndex];
                }
                for (auto& activeTimer : activeTimers)
                {
                    if (activeTimer.id == editTimerId)
                    {
                        if (activeTimer.isPaused)
                            activeTimer.remainingTime = totalDuration;
                        activeTimer.warningPlayed = false;
                        break;
                    }
                }

                // Save the settings after updating
                Settings::ScheduleSave(SettingsPath);

                // Close the window
                showEditTimerWindow = false;
                editInitialized = false;
                windowOpen = false;
            }
            if (!updateEnabled)
                ImGui::PopStyleVar();

            ImGui::SameLine();
            if (ImGui::Button("Close", ImVec2(120, 0)))  // Changed from "Cancel" to "Close"
            {
                // Just close the window without saving changes
                showEditTimerWindow = false;
                editInitialized = false;
                windowOpen = false;
            }
        }
        ImGui::End();

        // Handle window close via X button
        if (!windowOpen)
        {
            showEditTimerWindow = false;
            editInitialized = false;
        }
    }
    else
    {
        showEditTimerWindow = false;
        editInitialized = false;
    }
}

//-----------------------------------------------------------------
// RenderOptions: Full implementation of the options UI (formerly AddonOptions).
void RenderOptions()
{
    try {
        bool changed = false;
        if (ImGui::BeginTabBar("SettingsTabBar"))
        {
            // Timers Tab (Timer Management)
            if (ImGui::BeginTabItem("Timers"))
            {
                static int selectedTimerIdx = -1;
                static bool editMode = false;
                // Note: In the Create/Edit sections above we use our own local editTimerId.
                // Here, for the settings UI, we reuse the same concept.
                static std::string localEditTimerId = "";
                static int hours = 0;
                static int minutes = 0;
                static int seconds = 0;
                static char timerName[128] = "New Timer";
                static bool useWarning = false;
                static int warningSeconds = 30;
                static int selectedSoundIndex = 0;
                static int selectedWarningSoundIndex = 0;

                ImGui::BeginGroup();
                ImGui::Text("Existing Timers");
                ImGui::Separator();

                if (Settings::timers.empty()) {
                    ImGui::TextColored(ImVec4(0.75f, 0.75f, 0.75f, 1.0f), "No timers found.");
                }
                else {
                    ImGui::BeginChild("TimersList", ImVec2(ImGui::GetContentRegionAvail().x * 0.4f, 250), true);
                    for (size_t i = 0; i < Settings::timers.size(); i++) {
                        const auto& timer = Settings::timers[i];
                        ImGui::PushID(timer.id.c_str());
                        if (ImGui::Selectable(timer.name.c_str(), selectedTimerIdx == static_cast<int>(i))) {
                            selectedTimerIdx = static_cast<int>(i);
                            editMode = true;
                            localEditTimerId = timer.id;
                            strcpy_s(timerName, sizeof(timerName), timer.name.c_str());
                            int totalSeconds = static_cast<int>(timer.duration);
                            hours = totalSeconds / 3600;
                            minutes = (totalSeconds % 3600) / 60;
                            seconds = totalSeconds % 60;
                            useWarning = timer.useWarning;
                            warningSeconds = static_cast<int>(timer.warningTime);
                            selectedSoundIndex = 0;
                            selectedWarningSoundIndex = 0;
                            if (g_SoundEngine) {
                                std::vector<SoundInfo> availableSounds = g_SoundEngine->GetAvailableSounds();
                                for (size_t s = 0; s < availableSounds.size(); s++) {
                                    if (availableSounds[s].id.ToString() == timer.endSound.ToString())
                                        selectedSoundIndex = static_cast<int>(s);
                                    if (availableSounds[s].id.ToString() == timer.warningSound.ToString())
                                        selectedWarningSoundIndex = static_cast<int>(s);
                                }
                            }
                        }
                        ImGui::PopID();
                    }
                    ImGui::EndChild();
                }
                if (ImGui::Button("Create New Timer")) {
                    selectedTimerIdx = -1;
                    editMode = false;
                    localEditTimerId = "";
                    strcpy_s(timerName, sizeof(timerName), "New Timer");
                    hours = 0;
                    minutes = 5;
                    seconds = 0;
                    useWarning = false;
                    warningSeconds = 30;
                    selectedSoundIndex = 0;
                    selectedWarningSoundIndex = 0;
                }
                ImGui::EndGroup();

                ImGui::SameLine();
                ImGui::BeginGroup();
                ImGui::Text(editMode ? "Edit Timer" : "Create New Timer");
                ImGui::Separator();

                const float labelWidth = 60.0f;
                const float inputWidth = 180.0f;
                ImGui::Text("Timer Name");
                ImGui::PushItemWidth(inputWidth);
                ImGui::InputText("##TimerName", timerName, IM_ARRAYSIZE(timerName));
                ImGui::PopItemWidth();
                ImGui::Spacing();

                ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(2, 4));
                ImGui::SetNextItemWidth(labelWidth);
                ImGui::Text("Hours  ");
                ImGui::SameLine();
                if (ImGui::ArrowButton("##hour_down", ImGuiDir_Left)) { if (hours > 0) hours--; }
                ImGui::SameLine(0, 2);
                ImGui::PushItemWidth(50);
                if (ImGui::InputInt("##hours", &hours, 0))
                    hours = std::max(0, std::min(hours, 23));
                ImGui::PopItemWidth();
                ImGui::SameLine(0, 2);
                if (ImGui::ArrowButton("##hour_up", ImGuiDir_Right)) { if (hours < 23) hours++; }

                ImGui::SetNextItemWidth(labelWidth);
                ImGui::Text("Minutes");
                ImGui::SameLine();
                if (ImGui::ArrowButton("##min_down", ImGuiDir_Left)) { if (minutes > 0) minutes--; }
                ImGui::SameLine(0, 2);
                ImGui::PushItemWidth(50);
                if (ImGui::InputInt("##minutes", &minutes, 0))
                    minutes = std::max(0, std::min(minutes, 59));
                ImGui::PopItemWidth();
                ImGui::SameLine(0, 2);
                if (ImGui::ArrowButton("##min_up", ImGuiDir_Right)) { if (minutes < 59) minutes++; }

                ImGui::SetNextItemWidth(labelWidth);
                ImGui::Text("Seconds");
                ImGui::SameLine();
                if (ImGui::ArrowButton("##sec_down", ImGuiDir_Left)) { if (seconds > 0) seconds--; }
                ImGui::SameLine(0, 2);
                ImGui::PushItemWidth(50);
                if (ImGui::InputInt("##seconds", &seconds, 0))
                    seconds = std::max(0, std::min(seconds, 59));
                ImGui::PopItemWidth();
                ImGui::SameLine(0, 2);
                if (ImGui::ArrowButton("##sec_up", ImGuiDir_Right)) { if (seconds < 59) seconds++; }
                ImGui::PopStyleVar();
                ImGui::Spacing();

                float totalDuration = hours * 3600.0f + minutes * 60.0f + seconds;
                ImGui::Text("Total Duration: %s", FormatDuration(totalDuration).c_str());
                ImGui::Separator();

                ImGui::Text("End Sound");
                std::vector<std::string> soundNames;
                std::vector<SoundID> soundIds;
                if (g_SoundEngine) {
                    const auto& availableSounds = g_SoundEngine->GetAvailableSounds();
                    for (const auto& sound : availableSounds) {
                        if (sound.category == "Built-in") {
                            soundNames.push_back(sound.name);
                            soundIds.push_back(sound.id);
                        }
                    }
                    for (const auto& sound : availableSounds) {
                        if (sound.category == "Custom") {
                            soundNames.push_back(sound.name + " (Custom)");
                            soundIds.push_back(sound.id);
                        }
                    }
                    for (const auto& sound : availableSounds) {
                        if (sound.category == "Text-to-Speech") {
                            soundNames.push_back(sound.name + " (TTS)");
                            soundIds.push_back(sound.id);
                        }
                    }
                }
                if (soundNames.empty()) {
                    soundNames = { "Success Chime", "Info Chime", "Warning Chime" };
                    soundIds = {
                        SoundID(themes_chime_success),
                        SoundID(themes_chime_info),
                        SoundID(themes_chime_warning)
                    };
                }
                if (selectedSoundIndex >= soundNames.size())
                    selectedSoundIndex = 0;
                ImGui::PushItemWidth(inputWidth);
                if (ImGui::BeginCombo("##EndSound", soundNames[selectedSoundIndex].c_str()))
                {
                    for (int i = 0; i < static_cast<int>(soundNames.size()); i++)
                    {
                        bool isSelected = (selectedSoundIndex == i);
                        if (ImGui::Selectable(soundNames[i].c_str(), isSelected))
                            selectedSoundIndex = i;
                        if (isSelected)
                            ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
                ImGui::PopItemWidth();
                ImGui::SameLine();
                if (ImGui::Button("Test")) {
                    if (g_SoundEngine && selectedSoundIndex < soundIds.size())
                        g_SoundEngine->PlaySound(soundIds[selectedSoundIndex]);
                }
                ImGui::Spacing();

                ImGui::Checkbox("Use Warning Notification", &useWarning);
                if (useWarning) {
                    ImGui::Text("Warn before end");
                    ImGui::PushItemWidth(inputWidth - 70);
                    if (ImGui::InputInt("##WarningTime", &warningSeconds, 1, 5)) {
                        warningSeconds = std::max(1, warningSeconds);
                        warningSeconds = std::min(warningSeconds, static_cast<int>(totalDuration - 1));
                    }
                    ImGui::PopItemWidth();
                    ImGui::SameLine();
                    ImGui::Text("seconds");
                    ImGui::Text("Warning Sound");
                    ImGui::PushItemWidth(inputWidth);
                    if (selectedWarningSoundIndex >= soundNames.size())
                        selectedWarningSoundIndex = 0;
                    if (ImGui::BeginCombo("##WarningSound", soundNames[selectedWarningSoundIndex].c_str()))
                    {
                        for (int i = 0; i < static_cast<int>(soundNames.size()); i++)
                        {
                            bool isSelected = (selectedWarningSoundIndex == i);
                            if (ImGui::Selectable(soundNames[i].c_str(), isSelected))
                                selectedWarningSoundIndex = i;
                            if (isSelected)
                                ImGui::SetItemDefaultFocus();
                        }
                        ImGui::EndCombo();
                    }
                    ImGui::PopItemWidth();
                    ImGui::SameLine();
                    if (ImGui::Button("Test##warn")) {
                        if (g_SoundEngine && selectedWarningSoundIndex < soundIds.size())
                            g_SoundEngine->PlaySound(soundIds[selectedWarningSoundIndex]);
                    }
                }
                ImGui::Spacing();
                ImGui::Separator();

                bool actionEnabled = (totalDuration > 0 && strlen(timerName) > 0);
                if (!actionEnabled)
                    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.5f);
                if (editMode) {
                    if (ImGui::Button("Update Timer", ImVec2(120, 0)) && actionEnabled) {
                        TimerData* timer = Settings::FindTimer(editTimerId);
                        if (timer) {
                            timer->name = timerName;
                            timer->duration = totalDuration;
                            timer->endSound = soundIds[selectedSoundIndex];
                            timer->useWarning = useWarning;
                            if (useWarning) {
                                timer->warningTime = static_cast<float>(warningSeconds);
                                timer->warningSound = soundIds[selectedWarningSoundIndex];
                            }
                            for (auto& activeTimer : activeTimers) {
                                if (activeTimer.id == editTimerId) {
                                    if (activeTimer.isPaused)
                                        activeTimer.remainingTime = totalDuration;
                                    activeTimer.warningPlayed = false;
                                    break;
                                }
                            }
                            changed = true;
                        }
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Cancel Edit", ImVec2(120, 0))) {
                        selectedTimerIdx = -1;
                        editMode = false;
                    }
                }
                else {
                    if (ImGui::Button("Create Timer", ImVec2(120, 0)) && actionEnabled) {
                        TimerData& newTimer = Settings::AddTimer(timerName, totalDuration);
                        newTimer.endSound = soundIds[selectedSoundIndex];
                        newTimer.useWarning = useWarning;
                        if (useWarning) {
                            newTimer.warningTime = static_cast<float>(warningSeconds);
                            newTimer.warningSound = soundIds[selectedWarningSoundIndex];
                        }
                        activeTimers.push_back(ActiveTimer(newTimer.id, newTimer.duration, true));
                        RegisterTimerKeybind(newTimer.id);
                        strcpy_s(timerName, sizeof(timerName), "New Timer");
                        hours = 0;
                        minutes = 5;
                        seconds = 0;
                        changed = true;
                        selectedTimerIdx = static_cast<int>(Settings::timers.size()) - 1;
                        editMode = true;
                        editTimerId = newTimer.id;
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Reset Form", ImVec2(120, 0))) {
                        strcpy_s(timerName, sizeof(timerName), "New Timer");
                        hours = 0;
                        minutes = 5;
                        seconds = 0;
                        useWarning = false;
                        warningSeconds = 30;
                        selectedSoundIndex = 0;
                        selectedWarningSoundIndex = 0;
                    }
                }
                if (!actionEnabled)
                    ImGui::PopStyleVar();
                ImGui::EndGroup();
                ImGui::EndTabItem();
            }

            // Sound Settings Tab
            if (ImGui::BeginTabItem("Sound Settings")) {
                float volume = g_MasterVolume;
                if (ImGui::SliderFloat("Sound Volume", &volume, 0.0f, 1.0f, "%.2f")) {
                    if (g_SoundEngine) {
                        try {
                            g_SoundEngine->SetMasterVolume(volume);
                            changed = true;
                            if (APIDefs) {
                                APIDefs->Log(ELogLevel_DEBUG, ADDON_NAME, "Volume changed via slider");
                            }
                        }
                        catch (const std::exception& e) {
                            if (APIDefs) {
                                char errorMsg[256];
                                sprintf_s(errorMsg, "Exception setting master volume: %s", e.what());
                                APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, errorMsg);
                            }
                        }
                        catch (...) {
                            if (APIDefs) {
                                APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, "Unknown exception setting master volume");
                            }
                        }
                    }
                }
                if (ImGui::Button("Test Sound")) {
                    try {
                        PlaySoundEffect(SoundID(themes_chime_success));
                    }
                    catch (...) {
                        if (APIDefs) {
                            APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, "Exception playing test sound");
                        }
                    }
                }
                ImGui::SameLine();
                bool isMuted = (g_MasterVolume <= 0.0f);
                if (ImGui::Checkbox("Mute Sounds", &isMuted)) {
                    if (g_SoundEngine) {
                        try {
                            static float prevVolume = 1.0f;
                            if (!isMuted && g_MasterVolume > 0.0f) {
                                prevVolume = g_MasterVolume;
                            }
                            g_SoundEngine->SetMasterVolume(isMuted ? 0.0f : prevVolume);
                            changed = true;
                        }
                        catch (...) {
                            if (APIDefs) {
                                APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, "Exception toggling mute");
                            }
                        }
                    }
                }
                ImGui::Separator();
                if (g_SoundEngine) {
                    try {
                        const auto& devices = g_SoundEngine->GetAudioDevices();
                        int currentDevice = g_SoundEngine->GetCurrentDeviceIndex();
                        if (!devices.empty()) {
                            ImGui::Text("Audio Output Device");
                            const char* currentDeviceName = (currentDevice >= 0 && currentDevice < static_cast<int>(devices.size()))
                                ? devices[currentDevice].displayName().c_str() : "Default";
                            if (ImGui::BeginCombo("##AudioDeviceSelect", currentDeviceName)) {
                                for (int i = 0; i < static_cast<int>(devices.size()); i++) {
                                    bool isSelected = (currentDevice == i);
                                    if (ImGui::Selectable(devices[i].displayName().c_str(), isSelected)) {
                                        try {
                                            g_SoundEngine->SetAudioDevice(i);
                                            PlaySoundEffect(SoundID(themes_chime_info));
                                            changed = true;
                                        }
                                        catch (...) {
                                            if (APIDefs) {
                                                APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, "Exception setting audio device");
                                            }
                                        }
                                    }
                                    if (isSelected)
                                        ImGui::SetItemDefaultFocus();
                                }
                                ImGui::EndCombo();
                            }
                            if (ImGui::Button("Refresh Devices")) {
                                try {
                                    g_SoundEngine->RefreshAudioDevices();
                                    changed = true;
                                }
                                catch (...) {
                                    if (APIDefs) {
                                        APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, "Exception refreshing audio devices");
                                    }
                                }
                            }
                        }
                    }
                    catch (...) {
                        if (APIDefs) {
                            APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, "Exception in audio device section");
                        }
                    }
                }
                ImGui::Separator();
                if (g_SoundEngine) {
                    const auto& allSounds = g_SoundEngine->GetAvailableSounds();
                    std::map<std::string, std::vector<SoundInfo>> categorizedSounds;
                    for (const auto& sound : allSounds)
                        categorizedSounds[sound.category].push_back(sound);
                    if (ImGui::CollapsingHeader("Built-in Sounds", ImGuiTreeNodeFlags_DefaultOpen)) {
                        auto it = categorizedSounds.find("Built-in");
                        if (it != categorizedSounds.end()) {
                            for (int i = 0; i < static_cast<int>(it->second.size()); i++) {
                                const auto& sound = it->second[i];
                                float soundVolume = g_SoundEngine->GetSoundVolume(sound.id);
                                float soundPan = g_SoundEngine->GetSoundPan(sound.id);
                                ImGui::PushID(sound.name.c_str());
                                ImGui::Text("%s", sound.name.c_str());
                                ImGui::SameLine(ImGui::GetWindowWidth() * 0.7f);
                                if (ImGui::Button("Test"))
                                    g_SoundEngine->PlaySound(sound.id);
                                if (ImGui::SliderFloat("Volume", &soundVolume, 0.0f, 1.0f, "%.2f")) {
                                    g_SoundEngine->SetSoundVolume(sound.id, soundVolume);
                                    changed = true;
                                }
                                if (ImGui::SliderFloat("Panning", &soundPan, -1.0f, 1.0f, "%.2f")) {
                                    g_SoundEngine->SetSoundPan(sound.id, soundPan);
                                    changed = true;
                                }
                                ImGui::Separator();
                                ImGui::PopID();
                            }
                        }
                    }
                    if (ImGui::CollapsingHeader("Custom Sounds", ImGuiTreeNodeFlags_DefaultOpen)) {
                        static char customSoundsDir[256] = "";
                        std::string currentDir = Settings::GetCustomSoundsDirectory();
                        if (strlen(customSoundsDir) == 0 && !currentDir.empty()) {
                            strncpy_s(customSoundsDir, sizeof(customSoundsDir), currentDir.c_str(), sizeof(customSoundsDir) - 1);
                        }
                        ImGui::Text("Custom Sounds Directory");
                        ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * 0.7f);
                        ImGui::InputText("##CustomSoundsDir", customSoundsDir, sizeof(customSoundsDir));
                        ImGui::PopItemWidth();
                        ImGui::SameLine();
                        if (ImGui::Button("Set")) {
                            try {
                                Settings::SetCustomSoundsDirectory(customSoundsDir);
                                if (g_SoundEngine && std::string(customSoundsDir) != "") {
                                    bool dirExists = std::filesystem::exists(customSoundsDir);
                                    if (dirExists) {
                                        try {
                                            g_SoundEngine->ScanSoundDirectory(customSoundsDir);
                                        }
                                        catch (...) {
                                            if (APIDefs) {
                                                APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, "Error scanning sound directory");
                                            }
                                        }
                                    }
                                }
                                changed = true;
                            }
                            catch (...) {
                                if (APIDefs) {
                                    APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, "Exception setting custom sounds directory");
                                }
                            }
                        }
                        ImGui::SameLine();
                        if (ImGui::Button("Refresh")) {
                            if (g_SoundEngine && std::filesystem::exists(customSoundsDir)) {
                                g_SoundEngine->ScanSoundDirectory(customSoundsDir);
                            }
                        }
                        ImGui::Separator();
                        auto it = categorizedSounds.find("Custom");
                        if (it != categorizedSounds.end() && !it->second.empty()) {
                            for (int i = 0; i < static_cast<int>(it->second.size()); i++) {
                                const auto& sound = it->second[i];
                                float soundVolume = g_SoundEngine->GetSoundVolume(sound.id);
                                float soundPan = g_SoundEngine->GetSoundPan(sound.id);
                                ImGui::PushID(sound.name.c_str());
                                ImGui::Text("%s", sound.name.c_str());
                                ImGui::SameLine(ImGui::GetWindowWidth() * 0.7f);
                                if (ImGui::Button("Test"))
                                    g_SoundEngine->PlaySound(sound.id);
                                if (ImGui::SliderFloat("Volume", &soundVolume, 0.0f, 1.0f, "%.2f")) {
                                    g_SoundEngine->SetSoundVolume(sound.id, soundVolume);
                                    changed = true;
                                }
                                if (ImGui::SliderFloat("Panning", &soundPan, -1.0f, 1.0f, "%.2f")) {
                                    g_SoundEngine->SetSoundPan(sound.id, soundPan);
                                    changed = true;
                                }
                                ImGui::Separator();
                                ImGui::PopID();
                            }
                        }
                        else {
                            ImGui::TextColored(ImVec4(0.75f, 0.75f, 0.75f, 1.0f), "No custom sounds found.");
                            ImGui::TextColored(ImVec4(0.75f, 0.75f, 0.75f, 1.0f), "Add WAV files to your custom sounds directory and click Refresh.");
                        }
                        ImGui::Spacing();
                        ImGui::TextDisabled("(?) How to add custom sounds");
                        if (ImGui::IsItemHovered()) {
                            ImGui::BeginTooltip();
                            ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
                            ImGui::Text("1. Create a folder on your computer for your sound files");
                            ImGui::Text("2. Add WAV audio files to this folder");
                            ImGui::Text("3. Enter the full path to this folder above");
                            ImGui::Text("4. Click 'Set' and then 'Refresh'");
                            ImGui::Text("5. Your custom sounds will appear here and in timer sound dropdowns");
                            ImGui::PopTextWrapPos();
                            ImGui::EndTooltip();
                        }
                    }
                    if (ImGui::CollapsingHeader("Text-to-Speech Sounds", ImGuiTreeNodeFlags_DefaultOpen)) {
                        static char ttsText[256] = "Timer complete";
                        static char ttsSoundName[128] = "New TTS Sound";
                        static int selectedVoiceIndex = -1;
                        static float ttsVolume = 1.0f;
                        static float ttsPan = 0.0f;

                        ImGui::Text("Create TTS Sound");
                        ImGui::Separator();

                        // Sound name input
                        ImGui::Text("Sound Name");
                        ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * 0.7f);
                        ImGui::InputText("##TTSSoundName", ttsSoundName, sizeof(ttsSoundName));
                        ImGui::PopItemWidth();

                        // Text to speak input
                        ImGui::Text("Text to Speak");
                        ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * 0.7f);
                        ImGui::InputText("##TTSText", ttsText, sizeof(ttsText));
                        ImGui::PopItemWidth();

                        // Voice selection
                        if (g_TextToSpeech) {
                            // Initialize TTS engine if needed
                            if (!g_TextToSpeech->IsInitialized() && !g_TextToSpeech->Initialize()) {
                                if (APIDefs) {
                                    APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, "Failed to initialize TTS engine");
                                }
                            }

                            const auto& voices = g_TextToSpeech->GetAvailableVoices();
                            if (!voices.empty()) {
                                ImGui::Text("Voice");
                                ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * 0.7f);

                                // Format voice names for combo
                                std::vector<std::string> voiceNames;
                                voiceNames.push_back("Default voice");
                                for (const auto& voice : voices) {
                                    voiceNames.push_back(voice.displayName());
                                }

                                // Display combo box
                                const char* currentVoiceName = (selectedVoiceIndex < 0) ?
                                    voiceNames[0].c_str() :
                                    voiceNames[selectedVoiceIndex + 1].c_str();

                                if (ImGui::BeginCombo("##TTSVoice", currentVoiceName)) {
                                    for (int i = 0; i < (int)voiceNames.size(); i++) {
                                        bool isSelected = ((i - 1) == selectedVoiceIndex);
                                        if (ImGui::Selectable(voiceNames[i].c_str(), isSelected)) {
                                            selectedVoiceIndex = (i == 0) ? -1 : (i - 1);
                                        }
                                        if (isSelected)
                                            ImGui::SetItemDefaultFocus();
                                    }
                                    ImGui::EndCombo();
                                }
                                ImGui::PopItemWidth();
                            }
                            else {
                                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "No TTS voices found on system");
                            }
                        }
                        else {
                            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "TTS engine not available");
                        }

                        // Volume and panning controls
                        ImGui::SliderFloat("Volume", &ttsVolume, 0.0f, 1.0f, "%.2f");
                        ImGui::SliderFloat("Panning", &ttsPan, -1.0f, 1.0f, "%.2f");

                        ImGui::Spacing();

                        // Test and create buttons
                        if (ImGui::Button("Test TTS")) {
                            if (g_TextToSpeech && !std::string(ttsText).empty()) {
                                // Set the voice if not default
                                if (selectedVoiceIndex >= 0) {
                                    g_TextToSpeech->SetVoice(selectedVoiceIndex);
                                }

                                // Play the TTS
                                g_TextToSpeech->SpeakText(ttsText, ttsVolume, ttsPan);
                            }
                        }

                        ImGui::SameLine();

                        if (ImGui::Button("Create TTS Sound")) {
                            if (g_TextToSpeech && !std::string(ttsText).empty() && !std::string(ttsSoundName).empty()) {
                                // Create the TTS sound
                                if (g_TextToSpeech->CreateTtsSound(ttsText, ttsSoundName, selectedVoiceIndex, ttsVolume, ttsPan)) {
                                    // Reset the name field for convenience
                                    strcpy_s(ttsSoundName, sizeof(ttsSoundName), "New TTS Sound");
                                }
                            }
                        }

                        // Display existing TTS sounds from the sound engine
                        ImGui::Separator();
                        ImGui::Text("Existing TTS Sounds");
                        ImGui::Separator();

                        if (g_SoundEngine) {
                            const auto& sounds = g_SoundEngine->GetAvailableSounds();
                            bool hasTtsSounds = false;

                            for (const auto& sound : sounds) {
                                // Check if this is a TTS sound
                                if (sound.category == "Text-to-Speech") {
                                    hasTtsSounds = true;
                                    ImGui::PushID(sound.name.c_str());

                                    ImGui::Text("%s", sound.name.c_str());
                                    ImGui::SameLine(ImGui::GetWindowWidth() * 0.7f);
                                    if (ImGui::Button("Test##tts")) {
                                        g_SoundEngine->PlaySound(sound.id);
                                    }

                                    float soundVolume = g_SoundEngine->GetSoundVolume(sound.id);
                                    if (ImGui::SliderFloat("Volume##tts", &soundVolume, 0.0f, 1.0f, "%.2f")) {
                                        g_SoundEngine->SetSoundVolume(sound.id, soundVolume);
                                        changed = true;
                                    }

                                    float soundPan = g_SoundEngine->GetSoundPan(sound.id);
                                    if (ImGui::SliderFloat("Panning##tts", &soundPan, -1.0f, 1.0f, "%.2f")) {
                                        g_SoundEngine->SetSoundPan(sound.id, soundPan);
                                        changed = true;
                                    }

                                    ImGui::Separator();
                                    ImGui::PopID();
                                }
                            }

                            if (!hasTtsSounds) {
                                ImGui::TextColored(ImVec4(0.75f, 0.75f, 0.75f, 1.0f), "No TTS sounds created yet.");
                                ImGui::TextColored(ImVec4(0.75f, 0.75f, 0.75f, 1.0f), "Create one using the form above.");
                            }
                        }
                    }
                }
                ImGui::EndTabItem();
            }

            // UI Settings Tab
            if (ImGui::BeginTabItem("UI Settings"))
            {
                if (ImGui::Checkbox("Show Title Bar", &Settings::showTitle))
                    changed = true;
                if (ImGui::Checkbox("Allow Window Resize", &Settings::allowResize))
                    changed = true;
                ImGui::Text("Color Settings");
                if (ImGui::ColorEdit4("Background Color", (float*)&Settings::colors.background, ImGuiColorEditFlags_AlphaBar))
                    changed = true;
                if (ImGui::ColorEdit4("Text Color", (float*)&Settings::colors.text, ImGuiColorEditFlags_AlphaBar))
                    changed = true;
                if (ImGui::ColorEdit4("Active Timer Color", (float*)&Settings::colors.timerActive, ImGuiColorEditFlags_AlphaBar))
                    changed = true;
                if (ImGui::ColorEdit4("Paused Timer Color", (float*)&Settings::colors.timerPaused, ImGuiColorEditFlags_AlphaBar))
                    changed = true;
                if (ImGui::ColorEdit4("Expired Timer Color", (float*)&Settings::colors.timerExpired, ImGuiColorEditFlags_AlphaBar))
                    changed = true;
                if (ImGui::Button("Reset Colors to Default"))
                {
                    Settings::colors = WindowColors();
                    changed = true;
                }
                ImGui::EndTabItem();
            }

            // About Tab
            if (ImGui::BeginTabItem("WebSocket"))
            {
                RenderWebSocketTab();
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Rooms")) {
                RenderRoomsTab();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
            if (changed)
                Settings::ScheduleSave(SettingsPath);
        }
    }
    catch (...)
    {
        if (APIDefs)
            APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, "Exception in RenderOptions");
    }
}



//-----------------------------------------------------------------
// Render the Settings window.
void RenderSettingsWindow()
{

    RenderOptions();
}