#define NOMINMAX
#include <Windows.h>
#include <string>
#include "resource.h"
#include "nexus/Nexus.h"
#include "mumble/Mumble.h"
#include "imgui/imgui.h"
#include "shared.h"
#include "settings.h"
#include "TextToSpeech.h" 
#include "Sounds.h"  // Include the new Sound.h header
#include "gui.h" 
//#include "WebSocketClient.h"

/* proto */
void AddonLoad(AddonAPI* aApi);
void AddonUnload();
void PreRender();
void AddonRender();
void AddonOptions();

///----------------------------------------------------------------------------------------------------
/// DllMain:
/// 	Main entry point for DLL.
/// 	We are not interested in this, all we get is our own HMODULE in case we need it.
///----------------------------------------------------------------------------------------------------
BOOL APIENTRY DllMain(HMODULE hModule, DWORD  ul_reason_for_call, LPVOID lpReserved)
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH: hSelf = hModule; break;
    case DLL_PROCESS_DETACH: break;
    case DLL_THREAD_ATTACH: break;
    case DLL_THREAD_DETACH: break;
    }
    return TRUE;
}

///----------------------------------------------------------------------------------------------------
/// GetAddonDef:
/// 	Export needed to give Nexus information about the addon.
///----------------------------------------------------------------------------------------------------
extern "C" __declspec(dllexport) AddonDefinition * GetAddonDef()
{
    AddonDef.Signature = -128765; // set to random unused negative integer
    AddonDef.APIVersion = NEXUS_API_VERSION;
    AddonDef.Name = "My First Nexus Addon";
    AddonDef.Version.Major = 1;
    AddonDef.Version.Minor = 0;
    AddonDef.Version.Build = 0;
    AddonDef.Version.Revision = 1;
    AddonDef.Author = "Unreal";
    AddonDef.Description = "This is my first Nexus addon.";
    AddonDef.Load = AddonLoad;
    AddonDef.Unload = AddonUnload;
    AddonDef.Flags = EAddonFlags_None;

    /* not necessary if hosted on Raidcore, but shown anyway for the example also useful as a backup resource */
    //AddonDef.Provider = EUpdateProvider_GitHub;
    //AddonDef.UpdateLink = "https://github.com/RaidcoreGG/GW2Nexus-AddonTemplate";

    return &AddonDef;
}

///----------------------------------------------------------------------------------------------------
/// AddonLoad:
/// 	Load function for the addon, will receive a pointer to the API.
/// 	(You probably want to store it.)
///----------------------------------------------------------------------------------------------------
void AddonLoad(AddonAPI* aApi)
{
    APIDefs = aApi; // store the api somewhere easily accessible
    ImGui::SetCurrentContext((ImGuiContext*)APIDefs->ImguiContext);
    ImGui::SetAllocatorFunctions((void* (*)(size_t, void*))APIDefs->ImguiMalloc, (void(*)(void*, void*))APIDefs->ImguiFree);
    NexusLink = (NexusLinkData*)APIDefs->DataLink.Get("DL_NEXUS_LINK");
    MumbleLink = (Mumble::Data*)APIDefs->DataLink.Get("DL_MUMBLE_LINK");
    // Add an options window and a regular render callback
    APIDefs->Renderer.Register(ERenderType_PreRender, PreRender);
    APIDefs->Renderer.Register(ERenderType_Render, AddonRender);
    APIDefs->Renderer.Register(ERenderType_OptionsRender, AddonOptions);
    // Initialize paths
    GW2Root = APIDefs->Paths.GetGameDirectory();
    AddonPath = APIDefs->Paths.GetAddonDirectory("SimpleTimers");
    SettingsPath = AddonPath + "/settings.json";
    std::filesystem::create_directory(AddonPath);
    Settings::Load(SettingsPath);
    APIDefs->Log(ELogLevel_DEBUG, "My First addon", "My <c=#00ff00>first addon</c> was loaded.");
    loadFont("SF FONT SMALL", 18, IDR_FONT1);
    loadFont("SF FONT LARGE", 25, IDR_FONT1);
    loadFont("SF FONT BIG", 35, IDR_FONT1);
    loadFont("SF FONT GIANT", 45, IDR_FONT1);
    // Initialize sound engine
    g_SoundEngine = new SoundEngine();
    if (g_SoundEngine->Initialize()) {
        APIDefs->Log(ELogLevel_DEBUG, ADDON_NAME, "Sound engine initialized successfully");
        // Load sound resources with default volume
        LoadSoundResource(themes_chime_success);
        LoadSoundResource(themes_chime_info);
        LoadSoundResource(themes_chime_warning);
        // Set the master volume from settings AFTER loading sounds
        try {
            float volume = Settings::GetMasterVolume();
            g_SoundEngine->SetMasterVolume(volume);
            APIDefs->Log(ELogLevel_DEBUG, ADDON_NAME, "Volume set from settings");
            // Scan custom sounds directory if exists
            std::string customSoundsDir = Settings::GetCustomSoundsDirectory();
            if (!customSoundsDir.empty() && std::filesystem::exists(customSoundsDir)) {
                g_SoundEngine->ScanSoundDirectory(customSoundsDir);
                APIDefs->Log(ELogLevel_DEBUG, ADDON_NAME, "Scanned custom sounds directory");
            }
        }
        catch (const std::exception& e) {
            APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, "Error setting volume.");
        }
        catch (...) {
            APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, "Unknown error setting volume");
        }

        // Initialize TTS engine
        if (g_SoundEngine) { // Only initialize TTS if the sound engine is available
            g_TextToSpeech = new TextToSpeech();
            if (g_TextToSpeech->Initialize()) {
                if (!Settings::LoadSavedTtsSounds()) {
                    if (APIDefs) {
                        APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, "Failed to load saved TTS sounds");
                    }
                }
            }
            else {
                APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, "Failed to initialize TTS engine");
                delete g_TextToSpeech;
                g_TextToSpeech = nullptr;
            }
        }
    }
    else {
        APIDefs->Log(ELogLevel_WARNING, ADDON_NAME, "Failed to initialize sound engine");
    }



    initializeActiveTimers();
}
///----------------------------------------------------------------------------------------------------
/// AddonUnload:
/// 	Everything you registered in AddonLoad, you should "undo" here.
///----------------------------------------------------------------------------------------------------
void AddonUnload() {

    APIDefs->Renderer.Deregister(AddonRender);
    APIDefs->Renderer.Deregister(AddonOptions);
    APIDefs->Fonts.Release("SF FONT SMALL", ReceiveFont);
    APIDefs->Fonts.Release("SF FONT LARGE", ReceiveFont);
    APIDefs->Fonts.Release("SF FONT BIG", ReceiveFont);
    APIDefs->Fonts.Release("SF FONT GIANT", ReceiveFont);

    // Unregister all keybinds
    for (const auto& timer : activeTimers) {
        UnregisterTimerKeybind(timer.id);
    }

    if (g_SoundEngine) {
        g_SoundEngine->Shutdown();
        delete g_SoundEngine;
        g_SoundEngine = nullptr;
    }



    APIDefs->Log(ELogLevel_DEBUG, "My First addon", "<c=#ff0000>Signing off</c>, it was an honor commander.");
}

void PreRender()
{
    if (g_SoundEngine) {
        g_SoundEngine->Update();
    }

}

///----------------------------------------------------------------------------------------------------
/// AddonRender:
/// 	Called every frame. Safe to render any ImGui.
/// 	You can control visibility on loading screens with NexusLink->IsGameplay.
///----------------------------------------------------------------------------------------------------
void AddonRender() {
    RenderMainTimersWindow();
    RenderCreateTimerWindow();
    RenderEditTimerWindow();
}

///----------------------------------------------------------------------------------------------------
/// AddonOptions:
/// 	Basically an ImGui callback that doesn't need its own Begin/End calls.
///----------------------------------------------------------------------------------------------------
// Update the AddonOptions function to handle potential exceptions
void AddonOptions() {
    RenderSettingsWindow();
}