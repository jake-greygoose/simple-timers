#pragma once
#pragma warning(disable: 4996)

#include <string>
#include <vector>
#include <sapi.h>           // Speech API
#include <sphelper.h>       // Speech API helpers
#include <atlbase.h>        // For CComPtr
#include "Sounds.h"         // Include this for SoundID class

#pragma comment(lib, "sapi.lib")

// Forward declarations
class SoundEngine;
struct SoundData;

// Simplified voice information structure
struct VoiceInfo {
    std::wstring id;        // Voice ID
    std::wstring name;      // Voice name
    int gender;             // Voice gender (0=unknown, 1=male, 2=female)

    // Convert to display name
    std::string displayName() const {
        // Convert wide string to string for display
        int size_needed = WideCharToMultiByte(CP_UTF8, 0, name.c_str(), -1, NULL, 0, NULL, NULL);
        std::string result(size_needed, 0);
        WideCharToMultiByte(CP_UTF8, 0, name.c_str(), -1, &result[0], size_needed, NULL, NULL);
        // Remove null terminator
        if (!result.empty() && result.back() == 0) {
            result.pop_back();
        }

        // Add gender if available
        std::string genderStr;
        switch (gender) {
        case 1: genderStr = " (Male)"; break;
        case 2: genderStr = " (Female)"; break;
        default: break;
        }

        return result + genderStr;
    }
};

class TtsSoundID : public SoundID {
private:
    std::string text;        // The text that was spoken
    std::string voiceId;     // Voice identifier

public:
    TtsSoundID(const std::string& speechText, const std::string& voice = "default")
        : SoundID("tts:" + voice + ":" + speechText), text(speechText), voiceId(voice)
    {
        // Mark this as a TTS sound
        SetTts(true);
    }

    const std::string& GetText() const { return text; }
    const std::string& GetVoiceId() const { return voiceId; }

    // Override string representation for storage
    std::string ToString() const override {
        return "tts:" + voiceId + ":" + text;
    }

    // Static method to create from string
    static TtsSoundID FromString(const std::string& str) {
        if (str.compare(0, 4, "tts:") == 0) {
            size_t firstColon = str.find(':', 4);
            if (firstColon != std::string::npos) {
                std::string voice = str.substr(4, firstColon - 4);
                std::string text = str.substr(firstColon + 1);
                return TtsSoundID(text, voice);
            }
        }
        return TtsSoundID("error");
    }
};

// Class to handle text-to-speech operations
class TextToSpeech {
private:
    bool initialized;
    CComPtr<ISpVoice> pVoice;                  // SAPI voice
    std::vector<VoiceInfo> availableVoices;    // Available voices

    // Memory stream for audio data
    CComPtr<ISpStream> pStream;
    CComPtr<IStream> pMemStream;

    // Last generated speech data
    BYTE* pSpeechData;
    ULONG speechDataSize;

    // Helper methods
    bool EnumerateVoices();

public:
    TextToSpeech();
    ~TextToSpeech();

    bool Initialize();
    void Shutdown();
    bool IsInitialized() const { return initialized; }

    // Voice management
    const std::vector<VoiceInfo>& GetAvailableVoices() const { return availableVoices; }
    bool SetVoice(int voiceIndex);

    // Speech generation
    bool TextToWav(const std::wstring& text, BYTE** ppData, ULONG* pDataSize, WAVEFORMATEX* pWaveFormat);

    // Helper for converting between string types
    static std::wstring StringToWString(const std::string& text);
    static std::string WStringToString(const std::wstring& text);

    // Play text directly (convenience method that uses SoundEngine)
    bool SpeakText(const std::string& text, float volume = 1.0f, float pan = 0.0f);

    // Add TTS sound to the sound engine (for selecting in UI)
    bool CreateTtsSound(const std::string& text, const std::string& name,
        int voiceIndex = -1, float volume = 1.0f, float pan = 0.0f);
};

// Global TTS engine
extern TextToSpeech* g_TextToSpeech;

// Helper function to speak text
bool PlayTtsNotification(const std::string& text, float volume = 1.0f, float pan = 0.0f);