#include "Sounds.h"
#include "settings.h"
#include "shared.h"
#include "TextToSpeech.h"
#include <algorithm>
#include <Functiondiscoverykeys_devpkey.h>


// Global sound engine instance
SoundEngine* g_SoundEngine = nullptr;
float g_MasterVolume = 1.0f;

// Helper functions for file operations
std::string GetFileExtension(const std::string& filePath) {
    size_t pos = filePath.find_last_of('.');
    if (pos == std::string::npos) return "";
    return filePath.substr(pos + 1);
}

bool SoundEngine::EnumerateAudioDevices() {
    // Clear the existing device list
    audioDevices.clear();
    currentDeviceIndex = 0;

    // Initialize COM if needed
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    bool needsUninitialize = SUCCEEDED(hr);

    // Create device enumerator
    IMMDeviceEnumerator* pEnumerator = nullptr;
    hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
        __uuidof(IMMDeviceEnumerator), (void**)&pEnumerator);

    if (SUCCEEDED(hr)) {
        // Get the default device ID
        IMMDevice* pDefaultDevice = nullptr;
        std::wstring defaultDeviceId;

        hr = pEnumerator->GetDefaultAudioEndpoint(eRender, eConsole, &pDefaultDevice);
        if (SUCCEEDED(hr)) {
            LPWSTR deviceId = nullptr;
            hr = pDefaultDevice->GetId(&deviceId);
            if (SUCCEEDED(hr)) {
                defaultDeviceId = deviceId;
                CoTaskMemFree(deviceId);
            }
            pDefaultDevice->Release();
        }

        // Enumerate all devices
        IMMDeviceCollection* pDevices = nullptr;
        hr = pEnumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &pDevices);

        if (SUCCEEDED(hr)) {
            UINT count;
            hr = pDevices->GetCount(&count);

            if (SUCCEEDED(hr)) {
                for (UINT i = 0; i < count; i++) {
                    IMMDevice* pDevice = nullptr;
                    hr = pDevices->Item(i, &pDevice);

                    if (SUCCEEDED(hr)) {
                        // Get device ID
                        LPWSTR deviceId = nullptr;
                        hr = pDevice->GetId(&deviceId);

                        if (SUCCEEDED(hr)) {
                            // Get device properties
                            IPropertyStore* pProps = nullptr;
                            hr = pDevice->OpenPropertyStore(STGM_READ, &pProps);

                            if (SUCCEEDED(hr)) {
                                // Get friendly name
                                PROPVARIANT varName;
                                PropVariantInit(&varName);

                                hr = pProps->GetValue(PKEY_Device_FriendlyName, &varName);
                                if (SUCCEEDED(hr)) {
                                    AudioDevice device;
                                    device.id = deviceId;
                                    device.name = varName.pwszVal;
                                    device.isDefault = (defaultDeviceId == deviceId);

                                    audioDevices.push_back(device);

                                    // Remember the index of the default device
                                    if (device.isDefault) {
                                        currentDeviceIndex = static_cast<int>(audioDevices.size() - 1);
                                    }

                                    PropVariantClear(&varName);
                                }

                                pProps->Release();
                            }

                            CoTaskMemFree(deviceId);
                        }

                        pDevice->Release();
                    }
                }
            }

            pDevices->Release();
        }

        pEnumerator->Release();
    }

    // Uninitialize COM if we initialized it
    if (needsUninitialize) {
        CoUninitialize();
    }

    // Log the found devices
    if (APIDefs) {
        char logMsg[128];
        sprintf_s(logMsg, "Found %zu audio devices", audioDevices.size());
        APIDefs->Log(ELogLevel_DEBUG, ADDON_NAME, logMsg);

        for (size_t i = 0; i < audioDevices.size(); i++) {
            std::string deviceName = audioDevices[i].displayName();
            sprintf_s(logMsg, "Device %zu: %s", i, deviceName.c_str());
            APIDefs->Log(ELogLevel_DEBUG, ADDON_NAME, logMsg);
        }
    }

    return !audioDevices.empty();
}

bool SoundEngine::SetAudioDevice(int deviceIndex) {
    if (!initialized || !pXAudio2) {
        return false;
    }

    // Validate index
    if (deviceIndex < 0 || deviceIndex >= static_cast<int>(audioDevices.size())) {
        return false;
    }

    // No change needed if it's the same device
    if (deviceIndex == currentDeviceIndex) {
        return true;
    }

    // Stop all sounds
    StopAllSounds();

    // Destroy existing mastering voice
    if (pMasteringVoice) {
        pMasteringVoice->DestroyVoice();
        pMasteringVoice = nullptr;
    }

    // Get device ID
    LPCWSTR deviceId = audioDevices[deviceIndex].id.c_str();

    // Create new mastering voice
    HRESULT hr = pXAudio2->CreateMasteringVoice(&pMasteringVoice, XAUDIO2_DEFAULT_CHANNELS,
        XAUDIO2_DEFAULT_SAMPLERATE, 0, deviceId, nullptr);

    // If specific device fails, try default
    if (FAILED(hr)) {
        hr = pXAudio2->CreateMasteringVoice(&pMasteringVoice, XAUDIO2_DEFAULT_CHANNELS,
            XAUDIO2_DEFAULT_SAMPLERATE, 0, nullptr, nullptr);
        if (FAILED(hr)) {
            if (APIDefs) {
                APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, "Failed to create mastering voice for new device");
            }
            return false;
        }
    }

    // Update current device index
    currentDeviceIndex = deviceIndex;

    // Save device selection to settings
    try {
        if (APIDefs) {
            Settings::SetAudioDeviceIndex(currentDeviceIndex);
        }
    }
    catch (...) {
        // Continue even if saving fails
    }

    if (APIDefs) {
        std::string deviceName = audioDevices[deviceIndex].displayName();
        std::string logMsg = "Audio device changed to: " + deviceName;
        APIDefs->Log(ELogLevel_DEBUG, ADDON_NAME, logMsg.c_str());
    }

    return true;
}

bool SoundEngine::RefreshAudioDevices() {
    std::wstring currentDeviceId;

    // Store the current device ID if we have one
    if (!audioDevices.empty() && currentDeviceIndex >= 0 &&
        currentDeviceIndex < static_cast<int>(audioDevices.size())) {
        currentDeviceId = audioDevices[currentDeviceIndex].id;
    }

    // Enumerate devices
    if (!EnumerateAudioDevices()) {
        return false;
    }

    // Try to find the previously selected device
    if (!currentDeviceId.empty()) {
        for (size_t i = 0; i < audioDevices.size(); i++) {
            if (audioDevices[i].id == currentDeviceId) {
                currentDeviceIndex = static_cast<int>(i);
                break;
            }
        }
    }

    return true;
}

// Global helper functions

bool LoadSoundResource(int resourceId) {
    // Initialize sound engine if needed
    if (!g_SoundEngine) {
        g_SoundEngine = new SoundEngine();
        if (!g_SoundEngine->Initialize()) {
            delete g_SoundEngine;
            g_SoundEngine = nullptr;
            if (APIDefs) APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, "Failed to initialize XAudio2 sound engine");
            return false;
        }
    }

    // Load the sound with the default volume from settings
    float volume = 1.0f;
    try {
        volume = Settings::GetSoundVolume(resourceId);
    }
    catch (...) {
        volume = 1.0f;
    }

    return g_SoundEngine->LoadSound(SoundID(resourceId), hSelf, volume);
}

void PlaySoundEffect(const SoundID& soundId) {
    if (g_SoundEngine) {
        g_SoundEngine->PlaySound(soundId);
    }
}

std::string GetFileName(const std::string& filePath) {
    size_t pos = filePath.find_last_of("/\\");
    if (pos == std::string::npos) return filePath;
    return filePath.substr(pos + 1);
}

bool IsSupportedAudioFile(const std::string& filePath) {
    std::string ext = GetFileExtension(filePath);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    // Formats supported by XAudio2 directly
    static const std::set<std::string> supportedFormats = {
        "wav", "mp3"
    };

    return supportedFormats.find(ext) != supportedFormats.end();
}

SoundEngine::SoundEngine()
    : pXAudio2(nullptr),
    pMasteringVoice(nullptr),
    initialized(false),
    masterVolume(1.0f),
    currentDeviceIndex(0)
{
}

SoundEngine::~SoundEngine() {
    Shutdown();
}

bool SoundEngine::Initialize() {
    // Return if already initialized
    if (initialized)
        return true;

    HRESULT hr = S_OK;

    // Enumerate available audio devices
    EnumerateAudioDevices();

    // Initialize XAudio2
    hr = XAudio2Create(&pXAudio2, 0, XAUDIO2_DEFAULT_PROCESSOR);
    if (FAILED(hr)) {
        if (APIDefs) {
            APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, "Failed to initialize XAudio2");
        }
        return false;
    }

    // Get selected device ID (or use default if not available)
    LPCWSTR deviceId = nullptr;
    try {
        if (APIDefs) {
            int savedDeviceIndex = Settings::GetAudioDeviceIndex();
            if (savedDeviceIndex >= 0 && savedDeviceIndex < static_cast<int>(audioDevices.size())) {
                currentDeviceIndex = savedDeviceIndex;
                deviceId = audioDevices[currentDeviceIndex].id.c_str();
            }
        }
    }
    catch (...) {
        // Use default if settings access fails
        deviceId = nullptr;
    }

    // Create mastering voice with the selected device
    hr = pXAudio2->CreateMasteringVoice(&pMasteringVoice, XAUDIO2_DEFAULT_CHANNELS,
        XAUDIO2_DEFAULT_SAMPLERATE, 0, deviceId, nullptr);
    if (FAILED(hr)) {
        // If failed with specific device, try with default
        if (deviceId != nullptr) {
            hr = pXAudio2->CreateMasteringVoice(&pMasteringVoice, XAUDIO2_DEFAULT_CHANNELS,
                XAUDIO2_DEFAULT_SAMPLERATE, 0, nullptr, nullptr);
        }

        // If still failed, bail out
        if (FAILED(hr)) {
            pXAudio2->Release();
            pXAudio2 = nullptr;
            if (APIDefs) {
                APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, "Failed to create mastering voice");
            }
            return false;
        }
    }

    initialized = true;

    // Get saved volume from settings if possible, safely
    try {
        if (APIDefs) {
            masterVolume = Settings::GetMasterVolume();
        }
    }
    catch (...) {
        // Use default value if settings access fails
        masterVolume = 1.0f;
    }

    g_MasterVolume = masterVolume;

    // Add built-in sounds to available sounds list
    AddSoundInfo(SoundInfo(SoundID(themes_chime_success), "Success Chime", "Built-in"));
    AddSoundInfo(SoundInfo(SoundID(themes_chime_info), "Info Chime", "Built-in"));
    AddSoundInfo(SoundInfo(SoundID(themes_chime_warning), "Warning Chime", "Built-in"));

    if (APIDefs) {
        APIDefs->Log(ELogLevel_DEBUG, ADDON_NAME, "Sound engine initialized successfully");
    }
    return true;
}

void SoundEngine::Shutdown() {
    if (!initialized)
        return;

    // Stop and release all active voices
    StopAllSounds();

    // Clean up sound cache
    for (auto& pair : soundCache) {
        if (pair.second.pDataBuffer) {
            delete[] pair.second.pDataBuffer;
            pair.second.pDataBuffer = nullptr;
        }
    }
    soundCache.clear();
    availableSounds.clear();

    // Release XAudio2 resources
    if (pMasteringVoice) {
        pMasteringVoice->DestroyVoice();
        pMasteringVoice = nullptr;
    }

    if (pXAudio2) {
        pXAudio2->Release();
        pXAudio2 = nullptr;
    }

    initialized = false;

    if (APIDefs) {
        APIDefs->Log(ELogLevel_DEBUG, ADDON_NAME, "XAudio2 shutdown complete");
    }
}

bool SoundEngine::LoadSound(const SoundID& soundId, HMODULE hModule, float baseVolume) {
    if (soundId.IsResource()) {
        return LoadResourceSound(soundId.GetResourceId(), hModule, baseVolume);
    }
    else {
        return LoadFileSound(soundId.GetFilePath(), baseVolume);
    }
}

bool SoundEngine::LoadResourceSound(int resourceId, HMODULE hModule, float baseVolume) {
    if (!hModule) {
        hModule = hSelf;  // Use addon module by default
    }

    // Check if already loaded
    SoundID id(resourceId);
    auto it = soundCache.find(id);
    if (it != soundCache.end()) {
        it->second.baseVolume = baseVolume;
        return true;
    }

    // Try different resource types
    const char* resourceTypes[] = { "WAVE", "BINARY", "RCDATA" };
    HRSRC hResource = NULL;

    // Log attempt to load
    char loadMsg[64];
    sprintf_s(loadMsg, "Attempting to load sound resource ID: %d", resourceId);
    if (APIDefs) APIDefs->Log(ELogLevel_DEBUG, ADDON_NAME, loadMsg);

    // Try each resource type
    for (const char* resType : resourceTypes) {
        hResource = FindResource(hModule, MAKEINTRESOURCE(resourceId), resType);
        if (hResource) {
            char foundMsg[128];
            sprintf_s(foundMsg, "Found sound resource ID: %d with type: %s", resourceId, resType);
            if (APIDefs) APIDefs->Log(ELogLevel_DEBUG, ADDON_NAME, foundMsg);
            break;
        }
    }

    if (!hResource) {
        char errorMsg[128];
        sprintf_s(errorMsg, "Failed to find sound resource ID: %d (tried multiple resource types)", resourceId);
        if (APIDefs) APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, errorMsg);
        return false;
    }

    // Load resource
    HGLOBAL hGlobal = LoadResource(hModule, hResource);
    if (!hGlobal) {
        char errorMsg[64];
        sprintf_s(errorMsg, "Failed to load sound resource ID: %d", resourceId);
        if (APIDefs) APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, errorMsg);
        return false;
    }

    // Lock resource
    LPVOID lpData = LockResource(hGlobal);
    DWORD dwSize = SizeofResource(hModule, hResource);
    if (!lpData || dwSize == 0) {
        char errorMsg[64];
        sprintf_s(errorMsg, "Failed to lock sound resource ID: %d", resourceId);
        if (APIDefs) APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, errorMsg);
        return false;
    }

    // Parse WAV data
    SoundData soundData = {};
    soundData.baseVolume = baseVolume;

    // WAV files start with "RIFF" header
    DWORD* pdwChunkId = (DWORD*)lpData;
    if (*pdwChunkId != 'FFIR') { // 'RIFF' in little-endian
        if (APIDefs) APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, "Invalid WAV format - missing RIFF header");
        return false;
    }

    // Find 'fmt ' chunk
    bool foundFmt = false;
    WAVEFORMATEX* pwfx = nullptr;
    DWORD dataOffset = 0;
    DWORD dataSize = 0;

    BYTE* pCurrentPos = (BYTE*)lpData + 12; // Skip RIFF header
    while (pCurrentPos < (BYTE*)lpData + dwSize - 8) {
        DWORD chunkId = *(DWORD*)pCurrentPos;
        DWORD chunkSize = *(DWORD*)(pCurrentPos + 4);

        if (chunkId == ' tmf') { // 'fmt ' in little-endian
            pwfx = (WAVEFORMATEX*)(pCurrentPos + 8);
            foundFmt = true;
        }
        else if (chunkId == 'atad') { // 'data' in little-endian
            dataOffset = (DWORD)(pCurrentPos + 8 - (BYTE*)lpData);
            dataSize = chunkSize;
        }

        pCurrentPos += 8 + chunkSize;
        // Align to word boundary
        pCurrentPos = (BYTE*)(((UINT_PTR)pCurrentPos + 1) & ~1);
    }

    if (!foundFmt || dataOffset == 0 || dataSize == 0) {
        if (APIDefs) APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, "Invalid WAV format - missing fmt or data chunk");
        return false;
    }

    // Copy format info
    memcpy(&soundData.wfx, pwfx, sizeof(WAVEFORMATEX));

    // Copy sound data
    soundData.pDataBuffer = new BYTE[dataSize];
    soundData.bufferSize = dataSize;
    memcpy(soundData.pDataBuffer, (BYTE*)lpData + dataOffset, dataSize);

    // Set pan from settings
    if (APIDefs) {
        try {
            soundData.pan = Settings::GetSoundPan(resourceId);
        }
        catch (...) {
            soundData.pan = 0.0f;  // Default to center pan
        }
    }

    // Add to cache
    soundCache[id] = soundData;

    char logMsg[128];
    sprintf_s(logMsg, "Loaded sound resource ID: %d, format: %dHz, %d channels",
        resourceId, soundData.wfx.nSamplesPerSec, soundData.wfx.nChannels);
    if (APIDefs) APIDefs->Log(ELogLevel_DEBUG, ADDON_NAME, logMsg);

    return true;
}

bool SoundEngine::LoadFileSound(const std::string& filePath, float baseVolume) {
    if (APIDefs) {
        char logMsg[256];
        sprintf_s(logMsg, "Loading sound file: %s", filePath.c_str());
        APIDefs->Log(ELogLevel_DEBUG, ADDON_NAME, logMsg);
    }

    // Check if already loaded
    SoundID id(filePath);
    auto it = soundCache.find(id);
    if (it != soundCache.end()) {
        it->second.baseVolume = baseVolume;
        return true;
    }

    // Check if file exists
    FILE* file = nullptr;
    fopen_s(&file, filePath.c_str(), "rb");
    if (!file) {
        if (APIDefs) {
            char errorMsg[256];
            sprintf_s(errorMsg, "Failed to open file: %s", filePath.c_str());
            APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, errorMsg);
        }
        return false;
    }

    // Check file extension (basic validation)
    if (!IsSupportedAudioFile(filePath)) {
        fclose(file);
        if (APIDefs) {
            char errorMsg[256];
            sprintf_s(errorMsg, "Unsupported audio file format: %s", filePath.c_str());
            APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, errorMsg);
        }
        return false;
    }

    // Read file into memory
    fseek(file, 0, SEEK_END);
    long fileSize = ftell(file);
    fseek(file, 0, SEEK_SET);

    std::vector<BYTE> fileData(fileSize);
    fread(fileData.data(), 1, fileSize, file);
    fclose(file);

    // Parse WAV data (this is simplified - only handles standard WAV)
    SoundData soundData = {};
    soundData.baseVolume = baseVolume;

    // WAV files start with "RIFF" header
    if (fileSize < 12 || memcmp(fileData.data(), "RIFF", 4) != 0) {
        if (APIDefs) {
            APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, "Invalid WAV format - missing RIFF header");
        }
        return false;
    }

    // Find 'fmt ' and 'data' chunks
    bool foundFmt = false;
    WAVEFORMATEX* pwfx = nullptr;
    DWORD dataOffset = 0;
    DWORD dataSize = 0;

    BYTE* pCurrentPos = fileData.data() + 12; // Skip RIFF header
    while (pCurrentPos < fileData.data() + fileSize - 8) {
        DWORD chunkId = *(DWORD*)pCurrentPos;
        DWORD chunkSize = *(DWORD*)(pCurrentPos + 4);

        if (chunkId == ' tmf') { // 'fmt ' in little-endian
            pwfx = (WAVEFORMATEX*)(pCurrentPos + 8);
            foundFmt = true;
        }
        else if (chunkId == 'atad') { // 'data' in little-endian
            dataOffset = (DWORD)(pCurrentPos + 8 - fileData.data());
            dataSize = chunkSize;
        }

        pCurrentPos += 8 + chunkSize;
        // Align to word boundary
        pCurrentPos = (BYTE*)(((UINT_PTR)pCurrentPos + 1) & ~1);
    }

    if (!foundFmt || dataOffset == 0 || dataSize == 0) {
        if (APIDefs) {
            APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, "Invalid WAV format - missing fmt or data chunk");
        }
        return false;
    }

    // Copy format info
    memcpy(&soundData.wfx, pwfx, sizeof(WAVEFORMATEX));

    // Copy sound data
    soundData.pDataBuffer = new BYTE[dataSize];
    soundData.bufferSize = dataSize;
    memcpy(soundData.pDataBuffer, fileData.data() + dataOffset, dataSize);

    // Set pan from settings if available
    try {
        soundData.pan = Settings::GetFileSoundPan(filePath);
    }
    catch (...) {
        soundData.pan = 0.0f;  // Default to center pan
    }

    // Store in cache
    soundCache[id] = soundData;

    // Add to available sounds if not already present
    AddSoundInfo(SoundInfo(id, GetFileName(filePath), "Custom"));

    if (APIDefs) {
        char logMsg[256];
        sprintf_s(logMsg, "Loaded file sound: %s, format: %dHz, %d channels",
            filePath.c_str(), soundData.wfx.nSamplesPerSec, soundData.wfx.nChannels);
        APIDefs->Log(ELogLevel_DEBUG, ADDON_NAME, logMsg);
    }

    return true;
}

void SoundEngine::Update() {
    CleanupFinishedVoices();
}

void SoundEngine::CleanupFinishedVoices() {
    // Remove finished voices from our tracking vector
    auto it = activeVoices.begin();
    while (it != activeVoices.end()) {
        if (it->pCallback && ((VoiceCallback*)it->pCallback)->isFinished) {
            if (it->pSourceVoice) {
                it->pSourceVoice->DestroyVoice();
                it->pSourceVoice = nullptr;
            }

            if (it->pCallback) {
                delete it->pCallback;
                it->pCallback = nullptr;
            }

            it = activeVoices.erase(it);
        }
        else {
            ++it;
        }
    }
}

void SoundEngine::StopAllSounds() {
    for (auto& voice : activeVoices) {
        if (voice.pSourceVoice) {
            voice.pSourceVoice->Stop(0);
            voice.pSourceVoice->FlushSourceBuffers();
            ((VoiceCallback*)voice.pCallback)->isFinished = true;
        }
    }

    // Cleanup all voices
    CleanupFinishedVoices();
}

bool SoundEngine::PlaySound(const SoundID& soundId) {
    if (!initialized && !Initialize()) {
        return false;
    }

    // Find sound in cache
    auto it = soundCache.find(soundId);
    if (it == soundCache.end()) {
        // Try to load it first
        if (!LoadSound(soundId)) {
            if (APIDefs) {
                char errorMsg[128];
                if (soundId.IsResource()) {
                    sprintf_s(errorMsg, "Sound ID %d not loaded, cannot play", soundId.GetResourceId());
                }
                else {
                    sprintf_s(errorMsg, "Sound file not loaded: %s", soundId.GetFilePath().c_str());
                }
                APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, errorMsg);
            }
            return false;
        }
        // Check again after load attempt
        it = soundCache.find(soundId);
        if (it == soundCache.end()) {
            return false;
        }
    }

    // Create voice callback
    VoiceCallback* pCallback = new VoiceCallback();

    // Create source voice
    IXAudio2SourceVoice* pSourceVoice = nullptr;
    HRESULT hr = pXAudio2->CreateSourceVoice(&pSourceVoice, &it->second.wfx,
        0, XAUDIO2_DEFAULT_FREQ_RATIO,
        pCallback);
    if (FAILED(hr)) {
        delete pCallback;
        if (APIDefs) {
            APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, "Failed to create source voice");
        }
        return false;
    }

    // Set the volume (master volume * sound-specific volume)
    pSourceVoice->SetVolume(masterVolume * it->second.baseVolume);

    // Apply panning
    ApplyPanning(pSourceVoice, it->second.pan);

    // Submit buffer
    XAUDIO2_BUFFER buffer = { 0 };
    buffer.pAudioData = it->second.pDataBuffer;
    buffer.AudioBytes = it->second.bufferSize;
    buffer.Flags = XAUDIO2_END_OF_STREAM;

    hr = pSourceVoice->SubmitSourceBuffer(&buffer);
    if (FAILED(hr)) {
        pSourceVoice->DestroyVoice();
        delete pCallback;
        if (APIDefs) {
            APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, "Failed to submit buffer");
        }
        return false;
    }

    // Start playing
    hr = pSourceVoice->Start(0);
    if (FAILED(hr)) {
        pSourceVoice->DestroyVoice();
        delete pCallback;
        if (APIDefs) {
            APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, "Failed to start playback");
        }
        return false;
    }

    // Track the voice for cleanup
    ActiveVoice activeVoice;
    activeVoice.pSourceVoice = pSourceVoice;
    activeVoice.pCallback = pCallback;
    activeVoice.soundId = soundId;
    activeVoices.push_back(activeVoice);

    // Add to recent sounds in settings
    try {
        if (APIDefs) {
            if (soundId.IsResource()) {
                Settings::AddRecentSound(soundId.ToString());
            }
            else {
                Settings::AddRecentSound(soundId.ToString());
            }
        }
    }
    catch (...) {
        // Continue even if this fails
    }

    return true;
}

void SoundEngine::SetMasterVolume(float volume) {
    // Clamp volume to valid range
    masterVolume = (std::max)(0.0f, (std::min)(1.0f, volume));
    g_MasterVolume = masterVolume;

    // Update all active voices
    for (auto& voice : activeVoices) {
        if (voice.pSourceVoice) {
            // Apply both master volume and sound-specific volume
            float soundVolume = 1.0f; // Default
            auto it = soundCache.find(voice.soundId);
            if (it != soundCache.end()) {
                soundVolume = it->second.baseVolume;
            }
            voice.pSourceVoice->SetVolume(masterVolume * soundVolume);
        }
    }

    // Only save settings if APIDefs is valid to avoid crash
    if (APIDefs) {
        try {
            // We need to update the settings value but AVOID circular calls
            // Use a direct approach to update and save
            {
                std::lock_guard<std::mutex> lock(Settings::Mutex);
                Settings::sounds.masterVolume = masterVolume;

                // Save directly if possible
                if (!SettingsPath.empty()) {
                    Settings::ScheduleSave(SettingsPath);
                }
            }
        }
        catch (const std::exception& e) {
            // Log specific exception details
            char errorMsg[256];
            sprintf_s(errorMsg, "Exception setting master volume: %s", e.what());
            APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, errorMsg);
        }
        catch (...) {
            APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, "Unknown exception setting master volume");
        }
    }

    char logMsg[64];
    sprintf_s(logMsg, "Master volume set to %.2f", masterVolume);
    if (APIDefs) {
        APIDefs->Log(ELogLevel_DEBUG, ADDON_NAME, logMsg);
    }
}

void SoundEngine::SetSoundVolume(const SoundID& soundId, float volume) {
    // Clamp volume to valid range
    volume = (std::max)(0.0f, (std::min)(1.0f, volume));

    // Try to find the sound in cache
    auto it = soundCache.find(soundId);

    // If the sound is not in cache, try to load it first
    if (it == soundCache.end()) {
        // Load the sound
        if (!LoadSound(soundId)) {
            // Even if loading fails, still update the settings
            if (APIDefs) {
                try {
                    if (soundId.IsResource()) {
                        Settings::SetSoundVolume(soundId.GetResourceId(), volume);
                    }
                    else {
                        Settings::SetFileSoundVolume(soundId.GetFilePath(), volume);
                    }
                }
                catch (...) {
                    // Continue even if settings update fails
                }
            }
            return;
        }

        // Try again after loading
        it = soundCache.find(soundId);
    }

    // Update the base volume for this sound
    if (it != soundCache.end()) {
        it->second.baseVolume = volume;

        // Update any active voices playing this sound
        for (auto& voice : activeVoices) {
            if (voice.soundId == soundId && voice.pSourceVoice) {
                voice.pSourceVoice->SetVolume(masterVolume * volume);
            }
        }

        // Update settings
        if (APIDefs) {
            try {
                if (soundId.IsResource()) {
                    Settings::SetSoundVolume(soundId.GetResourceId(), volume);
                }
                else {
                    Settings::SetFileSoundVolume(soundId.GetFilePath(), volume);
                }
            }
            catch (...) {
                // Continue even if settings update fails
            }
        }
    }
}

float SoundEngine::GetSoundVolume(const SoundID& soundId) const {
    auto it = soundCache.find(soundId);
    if (it != soundCache.end()) {
        return it->second.baseVolume;
    }

    // If not in cache, try to get from settings
    if (APIDefs) {
        try {
            if (soundId.IsResource()) {
                return Settings::GetSoundVolume(soundId.GetResourceId());
            }
            else {
                return Settings::GetFileSoundVolume(soundId.GetFilePath());
            }
        }
        catch (...) {
            // Fall back to default
        }
    }

    return 1.0f; // Default volume
}

void SoundEngine::ApplyPanning(IXAudio2SourceVoice* pVoice, float pan) {
    if (!pVoice) return;

    // Clamp pan value between -1 and 1
    pan = (std::max)(-1.0f, (std::min)(1.0f, pan));

    // Get channel count from the voice
    XAUDIO2_VOICE_DETAILS details;
    pVoice->GetVoiceDetails(&details);

    // Create output matrix
    float matrix[8] = {}; // Maximum 8 channels for safety

    if (details.InputChannels == 1) {
        // Mono source
        // Left output = cos(angle) * source
        // Right output = sin(angle) * source
        float angle = (pan + 1.0f) * 3.14159f / 4.0f; // Convert -1..1 to 0..pi/2
        matrix[0] = cosf(angle) * 1.5f; // Left
        matrix[1] = sinf(angle) * 1.5f; // Right
    }
    else if (details.InputChannels == 2) {
        // Stereo source
        // For stereo, we'll crossfade between channels
        float leftGain = (1.0f - pan) * 0.5f + 0.5f;
        float rightGain = (1.0f + pan) * 0.5f + 0.5f;

        // Left source -> Left output
        matrix[0] = leftGain;
        // Left source -> Right output
        matrix[1] = 1.0f - leftGain;
        // Right source -> Left output
        matrix[2] = 1.0f - rightGain;
        // Right source -> Right output
        matrix[3] = rightGain;
    }

    // Apply the output matrix
    HRESULT hr = pVoice->SetOutputMatrix(nullptr, details.InputChannels, 2, matrix);
    if (FAILED(hr) && APIDefs) {
        APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, "Failed to set output matrix for panning");
    }
}

void SoundEngine::SetSoundPan(const SoundID& soundId, float pan) {
    // Clamp pan value between -1 and 1
    pan = (std::max)(-1.0f, (std::min)(1.0f, pan));

    // Try to find the sound in cache
    auto it = soundCache.find(soundId);

    // If the sound is not in cache, try to load it first
    if (it == soundCache.end()) {
        // Load the sound
        if (!LoadSound(soundId)) {
            // Even if loading fails, still update the settings
            if (APIDefs) {
                try {
                    if (soundId.IsResource()) {
                        Settings::SetSoundPan(soundId.GetResourceId(), pan);
                    }
                    else {
                        Settings::SetFileSoundPan(soundId.GetFilePath(), pan);
                    }
                }
                catch (...) {
                    // Continue even if settings update fails
                }
            }
            return;
        }

        // Try again after loading
        it = soundCache.find(soundId);
    }

    // Store the pan value
    if (it != soundCache.end()) {
        // Store the pan value
        it->second.pan = pan;

        // Apply panning to any active voices playing this sound
        for (auto& voice : activeVoices) {
            if (voice.soundId == soundId && voice.pSourceVoice) {
                ApplyPanning(voice.pSourceVoice, pan);
            }
        }

        // Save to settings
        if (APIDefs) {
            try {
                if (soundId.IsResource()) {
                    Settings::SetSoundPan(soundId.GetResourceId(), pan);
                }
                else {
                    Settings::SetFileSoundPan(soundId.GetFilePath(), pan);
                }
            }
            catch (...) {
                // Continue even if settings update fails
            }
        }

        // Log the change
        if (APIDefs) {
            char logMsg[128];
            if (soundId.IsResource()) {
                sprintf_s(logMsg, "Set pan for sound resource %d to %.2f",
                    soundId.GetResourceId(), pan);
            }
            else {
                sprintf_s(logMsg, "Set pan for sound file to %.2f", pan);
            }
            APIDefs->Log(ELogLevel_DEBUG, ADDON_NAME, logMsg);
        }
    }
}

float SoundEngine::GetSoundPan(const SoundID& soundId) const {
    auto it = soundCache.find(soundId);
    if (it != soundCache.end()) {
        return it->second.pan;
    }

    // If not in cache, try to get from settings
    if (APIDefs) {
        try {
            if (soundId.IsResource()) {
                return Settings::GetSoundPan(soundId.GetResourceId());
            }
            else {
                return Settings::GetFileSoundPan(soundId.GetFilePath());
            }
        }
        catch (...) {
            // Fall back to default
        }
    }

    return 0.0f; // Default to center
}

void SoundEngine::AddSoundInfo(const SoundInfo& info) {
    // Check if this sound is already in our list
    for (const auto& existing : availableSounds) {
        if (existing.id == info.id) {
            return; // Already exists
        }
    }

    // Add to the list
    availableSounds.push_back(info);
}

// In Sounds.cpp, modify the ScanSoundDirectory method
void SoundEngine::ScanSoundDirectory(const std::string& directory) {
    if (directory.empty() || !std::filesystem::exists(directory)) {
        if (APIDefs) {
            APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, "Sound directory does not exist");
        }
        return;
    }

    if (APIDefs) {
        char logMsg[256];
        sprintf_s(logMsg, "Scanning sound directory: %s", directory.c_str());
        APIDefs->Log(ELogLevel_DEBUG, ADDON_NAME, logMsg);
    }

    try {
        for (const auto& entry : std::filesystem::directory_iterator(directory)) {
            if (entry.is_regular_file()) {
                std::string filepath = entry.path().string();

                // Check if this is a supported audio file
                if (IsSupportedAudioFile(filepath)) {
                    // Create a SoundID for this file
                    SoundID id(filepath);

                    // Add to available sounds list
                    std::string filename = GetFileName(filepath);
                    AddSoundInfo(SoundInfo(id, filename, "Custom"));

                    // Preload the sound into the cache with default volume
                    // This ensures it's in the cache for volume/pan adjustments
                    if (!LoadSound(id)) {
                        if (APIDefs) {
                            char errorMsg[256];
                            sprintf_s(errorMsg, "Failed to preload sound file: %s", filename.c_str());
                            APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, errorMsg);
                        }
                    }

                    if (APIDefs) {
                        char logMsg[256];
                        sprintf_s(logMsg, "Found sound file: %s", filename.c_str());
                        APIDefs->Log(ELogLevel_DEBUG, ADDON_NAME, logMsg);
                    }
                }
            }
        }
    }
    catch (const std::exception& e) {
        if (APIDefs) {
            char errorMsg[256];
            sprintf_s(errorMsg, "Error scanning sound directory: %s", e.what());
            APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, errorMsg);
        }
    }
}

void SoundEngine::AddTempSound(const SoundID& soundId, const SoundData& soundData) {
    // Add to our cache without adding to the available sounds list
    soundCache[soundId] = soundData;

    if (APIDefs) {
        APIDefs->Log(ELogLevel_DEBUG, ADDON_NAME, "Added temporary sound to cache");
    }
}

void SoundEngine::AddPermanentSound(const SoundID& soundId, const SoundData& soundData,
    const std::string& displayName, const std::string& category) {
    // Add to our cache
    soundCache[soundId] = soundData;

    // Add to the available sounds list
    std::string actualCategory = category.empty() ? "Custom" : category;
    AddSoundInfo(SoundInfo(soundId, displayName, actualCategory));

    if (APIDefs) {
        char logMsg[256];
        sprintf_s(logMsg, "Added permanent sound to cache: %s", displayName.c_str());
        APIDefs->Log(ELogLevel_DEBUG, ADDON_NAME, logMsg);
    }
}


void SoundEngine::AddTtsSound(const TtsSoundID& soundId, const SoundData& soundData, const std::string& displayName) {
    // Convert to regular SoundID for the sound cache (it's already a SoundID subclass)
    const SoundID& baseId = soundId;

    // Add to our cache
    soundCache[baseId] = soundData;

    // Add to the available sounds list
    std::string name = displayName.empty() ?
        ("TTS: " + soundId.GetText().substr(0, 20) + (soundId.GetText().length() > 20 ? "..." : "")) :
        displayName;

    AddSoundInfo(SoundInfo(baseId, name, "Text-to-Speech"));

    if (APIDefs) {
        char logMsg[256];
        sprintf_s(logMsg, "Added TTS sound to cache: %s", name.c_str());
        APIDefs->Log(ELogLevel_DEBUG, ADDON_NAME, logMsg);
    }
}