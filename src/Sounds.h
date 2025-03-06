#pragma once

#include <string>
#include <vector>
#include <map>
#include <set>
#include <memory>
#include <filesystem>
#include <xaudio2.h>
#include <mmdeviceapi.h>
#include "resource.h"

// Forward declarations
class SoundEngine;
class TextToSpeech;
class TtsSoundID;
extern SoundEngine* g_SoundEngine;
extern float g_MasterVolume;

// Sound identifier that can represent both resource sounds and file-based sounds
class SoundID {
private:
    bool isResource;
    int resourceId;           // Used when isResource = true
    std::string filePath;     // Used when isResource = false
    bool isTts;               // Whether this is a TTS sound ID

public:
    // Constructor for resource sounds
    explicit SoundID(int resId) : isResource(true), resourceId(resId), filePath(), isTts(false) {}

    // Constructor for file sounds
    explicit SoundID(const std::string& path) : isResource(false), resourceId(0), filePath(path), isTts(false) {}

    // Default constructor
    SoundID() : isResource(true), resourceId(0), filePath(), isTts(false) {}

    // Getters
    bool IsResource() const { return isResource; }
    bool IsTts() const { return isTts; }
    int GetResourceId() const { return resourceId; }
    const std::string& GetFilePath() const { return filePath; }

    // String representation for storage
    virtual std::string ToString() const {
        if (isResource) {
            return "res:" + std::to_string(resourceId);
        }
        else {
            return "file:" + filePath;
        }
    }

    // Create from string representation
    static SoundID FromString(const std::string& str) {
        if (str.compare(0, 4, "res:") == 0) {
            try {
                int resId = std::stoi(str.substr(4));
                return SoundID(resId);
            }
            catch (...) {
                return SoundID(); // Default on error
            }
        }
        else if (str.compare(0, 5, "file:") == 0) {
            return SoundID(str.substr(5));
        }
        else if (str.compare(0, 4, "tts:") == 0) {
            // Will be handled by derived TtsSoundID class
            return SoundID();
        }

        // Default if invalid string
        return SoundID();
    }

    // Comparison operators for map/set usage
    virtual bool operator==(const SoundID& other) const {
        if (isResource != other.isResource) return false;
        if (isTts != other.isTts) return false;
        if (isResource) {
            return resourceId == other.resourceId;
        }
        else {
            return filePath == other.filePath;
        }
    }

    virtual bool operator<(const SoundID& other) const {
        if (isTts != other.isTts) {
            return !isTts; // Resource and File IDs come before TTS
        }
        if (isResource != other.isResource) {
            return isResource; // Resource IDs come before file paths
        }

        if (isResource) {
            return resourceId < other.resourceId;
        }
        else {
            return filePath < other.filePath;
        }
    }

protected:
    // Allow derived classes to set flags
    void SetTts(bool value) { isTts = value; }
};

// Sound metadata for UI display and organization
struct SoundInfo {
    SoundID id;                     // Sound identifier
    std::string name;               // Display name
    std::string category;           // Optional category for organization

    SoundInfo(const SoundID& soundId, const std::string& displayName, const std::string& soundCategory = "")
        : id(soundId), name(displayName), category(soundCategory) {}

    SoundInfo() {}
};

// Audio device information structure
struct AudioDevice {
    std::wstring id;           // Device ID
    std::wstring name;         // Friendly name of the device
    bool isDefault;            // Is this the default device?

    // Simplified name for display purposes
    std::string displayName() const {
        // Convert wide string to string for display
        int size_needed = WideCharToMultiByte(CP_UTF8, 0, name.c_str(), -1, NULL, 0, NULL, NULL);
        std::string result(size_needed, 0);
        WideCharToMultiByte(CP_UTF8, 0, name.c_str(), -1, &result[0], size_needed, NULL, NULL);
        // Remove null terminator from the end
        if (!result.empty() && result.back() == 0) {
            result.pop_back();
        }
        return result + (isDefault ? " (Default)" : "");
    }
};

// XAudio2 Voice callback implementation
class VoiceCallback : public IXAudio2VoiceCallback {
public:
    VoiceCallback() : isFinished(false) {}

    // Voice events we care about
    void STDMETHODCALLTYPE OnStreamEnd() override { isFinished = true; }

    // Required but unused callbacks
    void STDMETHODCALLTYPE OnVoiceProcessingPassEnd() override {}
    void STDMETHODCALLTYPE OnVoiceProcessingPassStart(UINT32 SamplesRequired) override {}
    void STDMETHODCALLTYPE OnBufferEnd(void* pBufferContext) override {}
    void STDMETHODCALLTYPE OnBufferStart(void* pBufferContext) override {}
    void STDMETHODCALLTYPE OnLoopEnd(void* pBufferContext) override {}
    void STDMETHODCALLTYPE OnVoiceError(void* pBufferContext, HRESULT Error) override {}

    bool isFinished;
};

// Internal sound data structure
struct SoundData {
    BYTE* pDataBuffer = nullptr;    // Sound data buffer
    UINT32 bufferSize = 0;          // Buffer size in bytes
    WAVEFORMATEX wfx = {};          // Wave format info
    float baseVolume = 1.0f;        // Base volume (0.0f to 1.0f)
    float pan = 0.0f;               // Pan position (-1.0f = left, 0.0f = center, 1.0f = right)
};

// Voice tracking structure
struct ActiveVoice {
    IXAudio2SourceVoice* pSourceVoice = nullptr;
    VoiceCallback* pCallback = nullptr;
    bool isFinished = false;
    SoundID soundId;                    // Which sound is playing
};

// Forward declare TtsSoundID to avoid circular dependency
class TtsSoundID;

// Main sound engine class
class SoundEngine {
private:
    IXAudio2* pXAudio2 = nullptr;                   // XAudio2 engine
    IXAudio2MasteringVoice* pMasteringVoice = nullptr; // Mastering voice
    std::map<SoundID, SoundData> soundCache;        // Cache of loaded sounds
    std::vector<ActiveVoice> activeVoices;          // Currently playing voices
    std::vector<SoundInfo> availableSounds;         // Sounds available for UI selection
    std::vector<AudioDevice> audioDevices;          // Available audio devices
    int currentDeviceIndex = 0;                     // Index of the current audio device

    bool initialized = false;
    float masterVolume = 1.0f;                      // Master volume (0.0f to 1.0f)

    // Private helper methods
    bool EnumerateAudioDevices();
    void ApplyPanning(IXAudio2SourceVoice* pVoice, float pan);
    bool LoadResourceSound(int resourceId, HMODULE hModule, float baseVolume = 1.0f);
    bool LoadFileSound(const std::string& filePath, float baseVolume = 1.0f);

public:
    SoundEngine();
    ~SoundEngine();

    bool Initialize();
    void Shutdown();
    void Update();  // Call this every frame to clean up finished voices

    // Unified sound loading method
    bool LoadSound(const SoundID& soundId, HMODULE hModule = nullptr, float baseVolume = 1.0f);

    // TTS support - add a TTS sound to the cache
    void AddTtsSound(const TtsSoundID& soundId, const SoundData& soundData, const std::string& displayName = "");

    // Unified playback method
    bool PlaySound(const SoundID& soundId);
    void StopAllSounds();
    void CleanupFinishedVoices();

    // Volume and pan control
    float GetMasterVolume() const { return masterVolume; }
    void SetMasterVolume(float volume);

    void SetSoundVolume(const SoundID& soundId, float volume);
    float GetSoundVolume(const SoundID& soundId) const;

    void SetSoundPan(const SoundID& soundId, float pan);
    float GetSoundPan(const SoundID& soundId) const;

    // Sound library management
    void ScanSoundDirectory(const std::string& directory);
    const std::vector<SoundInfo>& GetAvailableSounds() const { return availableSounds; }
    void AddSoundInfo(const SoundInfo& info);

    // Audio device selection
    const std::vector<AudioDevice>& GetAudioDevices() const { return audioDevices; }
    int GetCurrentDeviceIndex() const { return currentDeviceIndex; }
    bool SetAudioDevice(int deviceIndex);
    bool RefreshAudioDevices();

    // Add methods that were missing
    void AddTempSound(const SoundID& soundId, const SoundData& soundData);
    void AddPermanentSound(const SoundID& soundId, const SoundData& soundData,
        const std::string& displayName, const std::string& category = "");
};

// Helper functions for sound management
std::string GetFileExtension(const std::string& filePath);
std::string GetFileName(const std::string& filePath);
bool IsSupportedAudioFile(const std::string& filePath);

// Function to load a resource sound with default settings
bool LoadSoundResource(int resourceId);

// Function to play a sound by ID
void PlaySoundEffect(const SoundID& soundId);