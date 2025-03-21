#pragma once

#include <Windows.h>
#include <string>
#include <vector>
#include <map>
#include <memory>
#include "nexus/Nexus.h"
#include "mumble/Mumble.h"
#include "imgui/imgui.h"
#include "Sounds.h"  // Include the new Sound header

#define ADDON_NAME "SimpleTimers"

// Struct definitions
struct ActiveTimer {
    std::string id;
    float remainingTime;
    bool isPaused;
    bool warningPlayed;
    std::string roomId; // Empty for local timers, room ID for online timers

    // Default constructor - required for std::map
    ActiveTimer()
        : id(""), remainingTime(0.0f), isPaused(true), warningPlayed(false), roomId("") {}

    // Regular constructor - for local timers
    ActiveTimer(const std::string& timerId, float duration, bool paused)
        : id(timerId), remainingTime(duration), isPaused(paused), warningPlayed(false), roomId("") {}

    // Constructor for room timers
    ActiveTimer(const std::string& timerId, float duration, bool paused, const std::string& roomId)
        : id(timerId), remainingTime(duration), isPaused(paused), warningPlayed(false), roomId(roomId) {}

    // Helper to check if this is a room timer
    bool isRoomTimer() const {
        return !roomId.empty();
    }
};

// Globals declaration
extern AddonDefinition AddonDef;
extern HMODULE hSelf;
extern AddonAPI* APIDefs;
extern NexusLinkData* NexusLink;
extern Mumble::Data* MumbleLink;
extern std::vector<ActiveTimer> activeTimers;

// Paths
extern std::string GW2Root;
extern std::string AddonPath;
extern std::string SettingsPath;
extern bool firstInstall;

// Fonts
extern ImFont* SanFranSmall;
extern ImFont* SanFranLarge;
extern ImFont* SanFranBig;
extern ImFont* SanFranGiant;

// Textures
extern Texture* PlayButton;
extern Texture* PauseButton;
extern Texture* AddButton;
extern Texture* DeleteButton;
extern Texture* EditButton;
extern Texture* MuteButton;
extern Texture* SoundButton;
extern Texture* RepeatButton;

// Function declarations
void ReceiveFont(const char* aIdentifier, void* aFont);
void loadFont(std::string id, float size, int resource);
void initializeActiveTimers();
std::string FormatDuration(float seconds);

bool LoadSoundResource(int resourceId);
void PlaySoundEffect(const SoundID& soundId);

// For backward compatibility
inline void PlaySoundEffect(int resourceId) {
    PlaySoundEffect(SoundID(resourceId));
}

void ProcessKeybinds(const char* aIdentifier, bool aIsRelease);
void RegisterTimerKeybind(const std::string& timerId);
void UnregisterTimerKeybind(const std::string& timerId);

void LoadAddonIcons();
bool InitializeSoundEngine();
bool ScanCustomSoundsDirectory();

void addOrUpdateActiveTimer(const ActiveTimer& newTimer);
void removeRoomTimer(const std::string& timerId, const std::string& roomId);
void removeAllRoomTimers(const std::string& roomId);