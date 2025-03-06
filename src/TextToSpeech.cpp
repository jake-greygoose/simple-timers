#include "TextToSpeech.h"
#include "Sounds.h"
#include "shared.h"
#include "settings.h"
#include <shlwapi.h>
#include <algorithm>

// Global TTS engine instance
TextToSpeech* g_TextToSpeech = nullptr;

TextToSpeech::TextToSpeech()
    : initialized(false),
    pVoice(nullptr),
    pStream(nullptr),
    pMemStream(nullptr),
    pSpeechData(nullptr),
    speechDataSize(0)
{
}

TextToSpeech::~TextToSpeech() {
    Shutdown();
}

bool TextToSpeech::Initialize() {
    if (initialized)
        return true;

    // Initialize COM if not already initialized
    HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (FAILED(hr) && hr != S_FALSE && hr != RPC_E_CHANGED_MODE) {
        if (APIDefs) {
            APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, "Failed to initialize COM for TTS");
        }
        return false;
    }

    // Create SpVoice object
    hr = pVoice.CoCreateInstance(CLSID_SpVoice);
    if (FAILED(hr)) {
        if (APIDefs) {
            APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, "Failed to create SpVoice object");
        }
        return false;
    }

    // Enumerate available voices
    if (!EnumerateVoices()) {
        if (APIDefs) {
            APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, "Failed to enumerate TTS voices");
        }
        pVoice.Release();
        return false;
    }

    initialized = true;

    if (APIDefs) {
        APIDefs->Log(ELogLevel_DEBUG, ADDON_NAME, "Text-to-speech engine initialized successfully");
    }
    return true;
}

void TextToSpeech::Shutdown() {
    if (!initialized)
        return;

    // Clean up any speech data
    if (pSpeechData) {
        delete[] pSpeechData;
        pSpeechData = nullptr;
        speechDataSize = 0;
    }

    // Release SAPI objects
    pStream.Release();
    pMemStream.Release();
    pVoice.Release();

    initialized = false;

    if (APIDefs) {
        APIDefs->Log(ELogLevel_DEBUG, ADDON_NAME, "Text-to-speech engine shutdown complete");
    }
}

bool TextToSpeech::EnumerateVoices() {
    if (!pVoice)
        return false;

    availableVoices.clear();

    // Get the current voice token
    CComPtr<ISpObjectToken> pCurrentVoiceToken;
    HRESULT hr = pVoice->GetVoice(&pCurrentVoiceToken);
    if (FAILED(hr)) {
        if (APIDefs) {
            APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, "Failed to get current voice token");
        }
        return false;
    }

    // Create voice token category
    CComPtr<ISpObjectTokenCategory> pCategory;
    hr = SpGetCategoryFromId(SPCAT_VOICES, &pCategory);
    if (FAILED(hr)) {
        if (APIDefs) {
            APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, "Failed to get voice category");
        }
        return false;
    }

    // Enumerate voice tokens
    CComPtr<IEnumSpObjectTokens> pTokenEnum;
    hr = pCategory->EnumTokens(NULL, NULL, &pTokenEnum);
    if (FAILED(hr)) {
        if (APIDefs) {
            APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, "Failed to enumerate voice tokens");
        }
        return false;
    }

    // Get voice count
    ULONG voiceCount = 0;
    hr = pTokenEnum->GetCount(&voiceCount);
    if (FAILED(hr)) {
        if (APIDefs) {
            APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, "Failed to get voice count");
        }
        return false;
    }

    // Loop through each voice
    for (ULONG i = 0; i < voiceCount; i++) {
        CComPtr<ISpObjectToken> pVoiceToken;
        hr = pTokenEnum->Item(i, &pVoiceToken);
        if (SUCCEEDED(hr)) {
            // Get voice ID
            LPWSTR pwszVoiceId = nullptr;
            hr = pVoiceToken->GetId(&pwszVoiceId);
            if (SUCCEEDED(hr)) {
                VoiceInfo voiceInfo;
                voiceInfo.id = pwszVoiceId;
                voiceInfo.gender = 0; // Default to unknown
                CoTaskMemFree(pwszVoiceId);

                // Get voice name
                CSpDynamicString dstrVoiceName;
                hr = SpGetDescription(pVoiceToken, &dstrVoiceName);
                if (SUCCEEDED(hr)) {
                    voiceInfo.name = dstrVoiceName.Copy();
                }

                // Get voice attributes
                CComPtr<ISpDataKey> pAttributes;
                hr = pVoiceToken->OpenKey(L"Attributes", &pAttributes);
                if (SUCCEEDED(hr)) {
                    // Get gender
                    CSpDynamicString dstrGender;
                    hr = pAttributes->GetStringValue(L"Gender", &dstrGender);
                    if (SUCCEEDED(hr)) {
                        if (wcscmp(dstrGender.Copy(), L"Male") == 0) {
                            voiceInfo.gender = 1; // Male
                        }
                        else if (wcscmp(dstrGender.Copy(), L"Female") == 0) {
                            voiceInfo.gender = 2; // Female
                        }
                    }
                }

                // Add to available voices
                availableVoices.push_back(voiceInfo);
            }
        }
    }

    // Report the voices we found
    if (APIDefs) {
        char logMsg[128];
        sprintf_s(logMsg, "Found %zu TTS voices", availableVoices.size());
        APIDefs->Log(ELogLevel_DEBUG, ADDON_NAME, logMsg);
    }

    return !availableVoices.empty();
}

bool TextToSpeech::SetVoice(int voiceIndex) {
    if (!initialized || !pVoice) {
        return false;
    }

    // Validate voice index
    if (voiceIndex < 0 || voiceIndex >= static_cast<int>(availableVoices.size())) {
        return false;
    }

    // Create voice token category
    CComPtr<ISpObjectTokenCategory> pCategory;
    HRESULT hr = SpGetCategoryFromId(SPCAT_VOICES, &pCategory);
    if (FAILED(hr)) {
        if (APIDefs) {
            APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, "Failed to get voice category");
        }
        return false;
    }

    // Enumerate voice tokens
    CComPtr<IEnumSpObjectTokens> pTokenEnum;
    hr = pCategory->EnumTokens(NULL, NULL, &pTokenEnum);
    if (FAILED(hr)) {
        if (APIDefs) {
            APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, "Failed to enumerate voice tokens");
        }
        return false;
    }

    // Get the selected voice token
    CComPtr<ISpObjectToken> pVoiceToken;
    hr = pTokenEnum->Item(voiceIndex, &pVoiceToken);
    if (FAILED(hr)) {
        if (APIDefs) {
            APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, "Failed to get selected voice token");
        }
        return false;
    }

    // Set the voice
    hr = pVoice->SetVoice(pVoiceToken);
    if (FAILED(hr)) {
        if (APIDefs) {
            APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, "Failed to set voice");
        }
        return false;
    }

    // Success
    if (APIDefs) {
        std::string voiceName = availableVoices[voiceIndex].displayName();
        std::string logMsg = "TTS voice set to: " + voiceName;
        APIDefs->Log(ELogLevel_DEBUG, ADDON_NAME, logMsg.c_str());
    }
    return true;
}

bool TextToSpeech::TextToWav(const std::wstring& text, BYTE** ppData, ULONG* pDataSize, WAVEFORMATEX* pWaveFormat) {
    if (!initialized || !pVoice) {
        return false;
    }

    // Clean up previous data if any
    if (pSpeechData) {
        delete[] pSpeechData;
        pSpeechData = nullptr;
        speechDataSize = 0;
    }

    // Release previous streams
    pStream.Release();
    pMemStream.Release();

    // Create a new memory stream
    HRESULT hr = CreateStreamOnHGlobal(NULL, TRUE, &pMemStream);
    if (FAILED(hr)) {
        if (APIDefs) {
            APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, "Failed to create memory stream for TTS");
        }
        return false;
    }

    // Define the output format as PCM 16-bit, 22kHz, mono
    WAVEFORMATEX format = { 0 };
    format.wFormatTag = WAVE_FORMAT_PCM;
    format.nChannels = 1;
    format.nSamplesPerSec = 22050;
    format.wBitsPerSample = 16;
    format.nBlockAlign = format.nChannels * format.wBitsPerSample / 8;
    format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;
    format.cbSize = 0;

    // Copy format info if requested
    if (pWaveFormat) {
        memcpy(pWaveFormat, &format, sizeof(WAVEFORMATEX));
    }

    // Create a stream that will write to our memory stream
    hr = CoCreateInstance(CLSID_SpStream, NULL, CLSCTX_ALL, __uuidof(ISpStream), (void**)&pStream);
    if (FAILED(hr)) {
        if (APIDefs) {
            APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, "Failed to create SpStream for TTS");
        }
        pMemStream.Release();
        return false;
    }

    // Initialize the stream
    hr = pStream->SetBaseStream(pMemStream, SPDFID_WaveFormatEx, &format);
    if (FAILED(hr)) {
        if (APIDefs) {
            APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, "Failed to initialize TTS stream");
        }
        pStream.Release();
        pMemStream.Release();
        return false;
    }

    // Set output to audio stream
    hr = pVoice->SetOutput(pStream, TRUE);
    if (FAILED(hr)) {
        if (APIDefs) {
            APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, "Failed to set TTS output to stream");
        }
        pStream.Release();
        pMemStream.Release();
        return false;
    }

    // Speak the text to the stream
    hr = pVoice->Speak(text.c_str(), SPF_IS_NOT_XML | SPF_PURGEBEFORESPEAK, NULL);
    if (FAILED(hr)) {
        if (APIDefs) {
            APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, "Failed to speak text to stream");
        }
        pVoice->SetOutput(NULL, FALSE);
        pStream.Release();
        pMemStream.Release();
        return false;
    }

    // Make sure the stream is done
    pVoice->WaitUntilDone(INFINITE);

    // Reset voice output
    pVoice->SetOutput(NULL, FALSE);

    // Get the data from the memory stream
    HGLOBAL hGlobal = NULL;
    hr = GetHGlobalFromStream(pMemStream, &hGlobal);
    if (SUCCEEDED(hr) && hGlobal) {
        // Lock the memory
        LPVOID ptr = GlobalLock(hGlobal);
        if (ptr) {
            // Get the size
            speechDataSize = (ULONG)GlobalSize(hGlobal);

            // Copy the data
            pSpeechData = new BYTE[speechDataSize];
            memcpy(pSpeechData, ptr, speechDataSize);

            // Return the data to the caller
            *ppData = pSpeechData;
            *pDataSize = speechDataSize;

            // Unlock the memory
            GlobalUnlock(hGlobal);

            if (APIDefs) {
                char logMsg[128];
                sprintf_s(logMsg, "Generated TTS audio (%d bytes)", speechDataSize);
                APIDefs->Log(ELogLevel_DEBUG, ADDON_NAME, logMsg);
            }

            return true;
        }
    }

    if (APIDefs) {
        APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, "Failed to get TTS data from stream");
    }
    return false;
}

std::wstring TextToSpeech::StringToWString(const std::string& text) {
    if (text.empty()) {
        return L"";
    }

    // Calculate the length of the resulting wchar_t string
    int count = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, NULL, 0);

    // Create a wchar_t buffer of the right size
    std::wstring result(count, 0);

    // Convert the string
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, &result[0], count);

    // Remove the null terminator
    if (!result.empty() && result.back() == 0) {
        result.pop_back();
    }

    return result;
}

std::string TextToSpeech::WStringToString(const std::wstring& text) {
    if (text.empty()) {
        return "";
    }

    // Calculate the length of the resulting char string
    int count = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, NULL, 0, NULL, NULL);

    // Create a char buffer of the right size
    std::string result(count, 0);

    // Convert the string
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, &result[0], count, NULL, NULL);

    // Remove the null terminator
    if (!result.empty() && result.back() == 0) {
        result.pop_back();
    }

    return result;
}

bool TextToSpeech::SpeakText(const std::string& text, float volume, float pan) {
    // Initialize if needed
    if (!initialized && !Initialize()) {
        return false;
    }

    if (text.empty()) {
        return false;
    }

    // Generate the TTS data
    BYTE* pData = nullptr;
    ULONG dataSize = 0;
    WAVEFORMATEX format = { 0 };

    if (!TextToWav(StringToWString(text), &pData, &dataSize, &format)) {
        return false;
    }

    // Check if we have a sound engine
    if (!g_SoundEngine) {
        if (APIDefs) {
            APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, "Sound engine not available for TTS");
        }
        return false;
    }

    // Create a unique sound ID for this text
    std::string idStr = "tts:" + text;
    SoundID cacheId(idStr);

    // Create a sound data struct
    SoundData soundData = { 0 };
    soundData.pDataBuffer = new BYTE[dataSize];
    soundData.bufferSize = dataSize;
    memcpy(soundData.pDataBuffer, pData, dataSize);
    memcpy(&soundData.wfx, &format, sizeof(WAVEFORMATEX));
    soundData.baseVolume = volume;
    soundData.pan = pan;

    // Add to the sound engine's cache as a temporary sound
    // We don't need the AddTtsSound method; we can use the normal sound system
    g_SoundEngine->AddTempSound(cacheId, soundData);

    // Play the sound
    bool result = g_SoundEngine->PlaySound(cacheId);

    return result;
}

bool TextToSpeech::CreateTtsSound(const std::string& text, const std::string& name,
    int voiceIndex, float volume, float pan) {
    // Initialize if needed
    if (!initialized && !Initialize()) {
        return false;
    }

    if (text.empty() || name.empty()) {
        return false;
    }

    // Set voice if specified
    if (voiceIndex >= 0 && voiceIndex < static_cast<int>(availableVoices.size())) {
        SetVoice(voiceIndex);
    }

    // Generate the TTS data
    BYTE* pData = nullptr;
    ULONG dataSize = 0;
    WAVEFORMATEX format = { 0 };

    if (!TextToWav(StringToWString(text), &pData, &dataSize, &format)) {
        return false;
    }

    // Check if we have a sound engine
    if (!g_SoundEngine) {
        if (APIDefs) {
            APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, "Sound engine not available for TTS");
        }
        return false;
    }

    // Create a unique sound ID for this text and voice combo
    std::string voiceStr = (voiceIndex >= 0) ? std::to_string(voiceIndex) : "default";
    std::string idStr = "tts:" + voiceStr + ":" + text;
    SoundID cacheId(idStr);

    // Create a sound data struct
    SoundData soundData = { 0 };
    soundData.pDataBuffer = new BYTE[dataSize];
    soundData.bufferSize = dataSize;
    memcpy(soundData.pDataBuffer, pData, dataSize);
    memcpy(&soundData.wfx, &format, sizeof(WAVEFORMATEX));
    soundData.baseVolume = volume;
    soundData.pan = pan;

    // Add to the sound engine's cache as a permanent sound
    g_SoundEngine->AddPermanentSound(cacheId, soundData, name, "Text-to-Speech");

    // Log success before attempting to save to settings
    if (APIDefs) {
        char logMsg[256];
        sprintf_s(logMsg, "Created TTS sound: %s", name.c_str());
        APIDefs->Log(ELogLevel_DEBUG, ADDON_NAME, logMsg);
    }

    // Save the TTS sound to settings in a separate step
    // This way, even if settings save fails, the sound is still usable in the current session
    try {
        // Use a sanitized ID for the settings
        std::string safeId = idStr;
        // Basic sanitization - remove any characters that might cause JSON issues
        safeId.erase(std::remove_if(safeId.begin(), safeId.end(),
            [](unsigned char c) { return c == '"' || c == '\\' || c == '\n' || c == '\r'; }),
            safeId.end());

        Settings::AddTtsSound(safeId, name, volume, pan);
    }
    catch (const std::exception& e) {
        if (APIDefs) {
            char errorMsg[256];
            sprintf_s(errorMsg, "Error saving TTS sound to settings: %s", e.what());
            APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, errorMsg);
        }
        // Don't return false; the sound is still created and usable
    }

    return true;
}

// Helper to play TTS notification
bool PlayTtsNotification(const std::string& text, float volume, float pan) {
    if (!g_TextToSpeech) {
        g_TextToSpeech = new TextToSpeech();
        if (!g_TextToSpeech->Initialize()) {
            delete g_TextToSpeech;
            g_TextToSpeech = nullptr;
            if (APIDefs) {
                APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, "Failed to initialize TTS engine");
            }
            return false;
        }
    }

    return g_TextToSpeech->SpeakText(text, volume, pan);
}