#include "shared.h"
#include "settings.h"
#include "Sounds.h"
#include <Functiondiscoverykeys_devpkey.h>

// Global definitions
AddonDefinition AddonDef = {};
HMODULE hSelf = nullptr;
AddonAPI* APIDefs = nullptr;
NexusLinkData* NexusLink = nullptr;
Mumble::Data* MumbleLink = nullptr;
std::vector<ActiveTimer> activeTimers;

// Paths
std::string GW2Root;
std::string AddonPath;
std::string SettingsPath;
bool firstInstall = false;

// Fonts
ImFont* SanFranSmall = nullptr;
ImFont* SanFranLarge = nullptr;
ImFont* SanFranBig = nullptr;
ImFont* SanFranGiant = nullptr;

// Textures
Texture* PlayButton = nullptr;
Texture* PauseButton = nullptr;
Texture* AddButton = nullptr;
Texture* DeleteButton = nullptr;
Texture* EditButton = nullptr;
Texture* MuteButton = nullptr;
Texture* SoundButton = nullptr;
Texture* RepeatButton = nullptr;

// Function implementations
void ReceiveFont(const char* aIdentifier, void* aFont)
{
    if (strcmp(aIdentifier, "SF FONT SMALL") == 0)
    {
        SanFranSmall = (ImFont*)aFont;
    }
    else if (strcmp(aIdentifier, "SF FONT LARGE") == 0)
    {
        SanFranLarge = (ImFont*)aFont;
    }
    else if (strcmp(aIdentifier, "SF FONT BIG") == 0)
    {
        SanFranBig = (ImFont*)aFont;
    }
    else if (strcmp(aIdentifier, "SF FONT GIANT") == 0)
    {
        SanFranGiant = (ImFont*)aFont;
    }
}

void loadFont(std::string id, float size, int resource) {
    APIDefs->Fonts.AddFromResource(id.c_str(), size > 0 ? size : 10, resource, hSelf, ReceiveFont, nullptr);
}

void ProcessKeybinds(const char* aIdentifier, bool aIsRelease) {
    if (aIsRelease) return;

    std::string str = aIdentifier;

    // Find and toggle the corresponding timer
    for (auto& timer : activeTimers) {
        if (str == "timer_" + timer.id) {
            timer.isPaused = !timer.isPaused;
            if (!timer.isPaused) {
                // If timer was expired, reset it when starting
                auto settingsTimer = Settings::FindTimer(timer.id);
                if (settingsTimer && timer.remainingTime <= 0) {
                    timer.remainingTime = settingsTimer->duration;
                }
            }
            break;
        }
    }
}

void RegisterTimerKeybind(const std::string& timerId) {
    std::string keybindId = "timer_" + timerId;
    APIDefs->InputBinds.RegisterWithString(keybindId.c_str(), ProcessKeybinds, "(null)");
}

void UnregisterTimerKeybind(const std::string& timerId) {
    std::string keybindId = "timer_" + timerId;
    APIDefs->InputBinds.Deregister(keybindId.c_str());
}

// Updated to only load local timers during initialization
void initializeActiveTimers() {
    // Create a map of existing timer states to preserve their values
    std::map<std::string, ActiveTimer> existingStates;
    for (const auto& active : activeTimers) {
        existingStates[active.id] = active;
        // Unregister existing keybinds
        UnregisterTimerKeybind(active.id);
    }

    // Clear the active timers list
    activeTimers.clear();

    // For each settings timer, only load local (non-room) timers
    for (const auto& timer : Settings::timers) {
        if (!timer.isRoomTimer) {
            // Regular local timer - include these
            if (existingStates.count(timer.id) > 0) {
                // Use addOrUpdateActiveTimer to add with preserved state
                addOrUpdateActiveTimer(existingStates[timer.id]);
            }
            else {
                // Create new timer state
                addOrUpdateActiveTimer(ActiveTimer(timer.id, timer.duration, true));
            }
            // Register keybind for this timer
            RegisterTimerKeybind(timer.id);
        }
        // Skip all room timers during initialization
    }

    if (APIDefs) {
        char logMsg[256];
        sprintf_s(logMsg, "Initialized %zu local timers", activeTimers.size());
        APIDefs->Log(ELogLevel_INFO, ADDON_NAME, logMsg);
    }
}



std::string FormatDuration(float seconds) {
    int totalSeconds = static_cast<int>(seconds);

    int hours = totalSeconds / 3600;
    int minutes = (totalSeconds % 3600) / 60;
    int secs = totalSeconds % 60;

    std::vector<std::string> parts;

    if (hours > 0) {
        parts.push_back(std::to_string(hours) + (hours == 1 ? " hr" : " hrs"));
    }

    if (minutes > 0) {
        parts.push_back(std::to_string(minutes) + " min");
    }

    if (secs > 0 && (parts.empty() || hours == 0)) {
        parts.push_back(std::to_string(secs) + (secs == 1 ? " sec" : " secs"));
    }

    if (parts.empty()) {
        return "0 secs";
    }

    std::string result;
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i > 0) {
            result += ", ";
        }
        result += parts[i];
    }

    return result;
}


// Helper function to scan a directory for custom sounds
bool ScanCustomSoundsDirectory() {
    // Get custom sounds directory from settings
    std::string customSoundsDir = Settings::GetCustomSoundsDirectory();

    if (customSoundsDir.empty() || !std::filesystem::exists(customSoundsDir)) {
        return false;
    }

    if (g_SoundEngine) {
        g_SoundEngine->ScanSoundDirectory(customSoundsDir);
        if (APIDefs) {
            char logMsg[256];
            sprintf_s(logMsg, "Scanned custom sounds directory: %s", customSoundsDir.c_str());
            APIDefs->Log(ELogLevel_DEBUG, ADDON_NAME, logMsg);
        }
        return true;
    }

    return false;
}

void addOrUpdateActiveTimer(const ActiveTimer& newTimer) {
    // Look for an existing timer with the same id
    auto it = std::find_if(activeTimers.begin(), activeTimers.end(),
        [&newTimer](const ActiveTimer& t) {
            return t.id == newTimer.id &&
                (t.roomId == newTimer.roomId || newTimer.roomId.empty() || t.roomId.empty());
        });

    if (it != activeTimers.end()) {
        // Timer already exists; update its state
        it->remainingTime = newTimer.remainingTime;
        it->isPaused = newTimer.isPaused;
        it->warningPlayed = newTimer.warningPlayed;

        // Only update roomId if the new timer is from a room
        if (!newTimer.roomId.empty()) {
            it->roomId = newTimer.roomId;
        }

        if (APIDefs) {
            APIDefs->Log(ELogLevel_DEBUG, ADDON_NAME, "Updated existing timer in active timers list");
        }
    }
    else {
        // Timer doesn't exist; add it
        activeTimers.push_back(newTimer);

        // Only register keybind for non-room timers or if we're explicitly adding a room timer
        if (!newTimer.isRoomTimer() || !newTimer.roomId.empty()) {
            RegisterTimerKeybind(newTimer.id);
        }

        if (APIDefs) {
            char logMsg[256];
            sprintf_s(logMsg, "Added new timer to active timers list: %s", newTimer.id.c_str());
            APIDefs->Log(ELogLevel_DEBUG, ADDON_NAME, logMsg);
        }
    }
}

// Function to remove a room timer from the active timers list
void removeRoomTimer(const std::string& timerId, const std::string& roomId) {
    auto it = std::find_if(activeTimers.begin(), activeTimers.end(),
        [&](const ActiveTimer& timer) {
            return timer.id == timerId && timer.roomId == roomId;
        });

    if (it != activeTimers.end()) {
        // Unregister any keybind associated with this timer
        UnregisterTimerKeybind(it->id);

        // Remove the timer from the active list
        activeTimers.erase(it);

        if (APIDefs) {
            char logMsg[256];
            sprintf_s(logMsg, "Removed room timer %s from active timers", timerId.c_str());
            APIDefs->Log(ELogLevel_INFO, ADDON_NAME, logMsg);
        }
    }
}

// Function to remove all timers for a specific room
void removeAllRoomTimers(const std::string& roomId) {
    auto it = activeTimers.begin();
    while (it != activeTimers.end()) {
        if (it->isRoomTimer() && it->roomId == roomId) {
            // Unregister any keybind associated with this timer
            UnregisterTimerKeybind(it->id);

            // Remove the timer and get the next iterator
            it = activeTimers.erase(it);
        }
        else {
            // Move to next timer
            ++it;
        }
    }

    if (APIDefs) {
        char logMsg[256];
        sprintf_s(logMsg, "Removed all timers for room %s", roomId.c_str());
        APIDefs->Log(ELogLevel_INFO, ADDON_NAME, logMsg);
    }
}


// Helper function to initialize the sound engine and load default sounds
bool InitializeSoundEngine() {
    // Create sound engine if it doesn't exist
    if (!g_SoundEngine) {
        g_SoundEngine = new SoundEngine();
    }

    // Initialize the engine
    if (!g_SoundEngine->Initialize()) {
        if (APIDefs) {
            APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, "Failed to initialize sound engine");
        }
        return false;
    }

    // Load built-in sound resources with default volumes
    if (!LoadSoundResource(themes_chime_success)) {
        if (APIDefs) {
            APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, "Failed to load success chime");
        }
    }

    if (!LoadSoundResource(themes_chime_info)) {
        if (APIDefs) {
            APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, "Failed to load info chime");
        }
    }

    if (!LoadSoundResource(themes_chime_warning)) {
        if (APIDefs) {
            APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, "Failed to load warning chime");
        }
    }

    // Set master volume from settings
    try {
        float volume = Settings::GetMasterVolume();
        g_SoundEngine->SetMasterVolume(volume);

        if (APIDefs) {
            char logMsg[64];
            sprintf_s(logMsg, "Master volume set to %.2f", volume);
            APIDefs->Log(ELogLevel_DEBUG, ADDON_NAME, logMsg);
        }
    }
    catch (const std::exception& e) {
        if (APIDefs) {
            char errorMsg[128];
            sprintf_s(errorMsg, "Error setting volume: %s", e.what());
            APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, errorMsg);
        }
    }
    catch (...) {
        if (APIDefs) {
            APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, "Unknown error setting volume");
        }
    }

    // Scan for custom sounds
    ScanCustomSoundsDirectory();

    return true;
}

// Function to handle loading the addon icons
void LoadAddonIcons() {
    if (!AddButton || !AddButton->Resource) {
        AddButton = APIDefs->Textures.GetOrCreateFromResource("ADD_ICON", ADD_ICON, hSelf);
    }

    if (!PlayButton || !PlayButton->Resource) {
        PlayButton = APIDefs->Textures.GetOrCreateFromResource("PLAY_ICON", PLAY_ICON, hSelf);
    }

    if (!PauseButton || !PauseButton->Resource) {
        PauseButton = APIDefs->Textures.GetOrCreateFromResource("PAUSE_ICON", PAUSE_ICON, hSelf);
    }

    if (!RepeatButton || !RepeatButton->Resource) {
        RepeatButton = APIDefs->Textures.GetOrCreateFromResource("REPEAT_ICON", REPEAT_ICON, hSelf);
    }

    if (!DeleteButton || !DeleteButton->Resource) {
        DeleteButton = APIDefs->Textures.GetOrCreateFromResource("DELETE_ICON", DELETE_ICON, hSelf);
    }

    if (!EditButton || !EditButton->Resource) {
        EditButton = APIDefs->Textures.GetOrCreateFromResource("EDIT_ICON", EDIT_ICON, hSelf);
    }

    if (!SoundButton || !SoundButton->Resource) {
        SoundButton = APIDefs->Textures.GetOrCreateFromResource("SOUND_ICON", SOUND_ICON, hSelf);
    }

    if (!MuteButton || !MuteButton->Resource) {
        MuteButton = APIDefs->Textures.GetOrCreateFromResource("MUTE_ICON", MUTE_ICON, hSelf);
    }
}