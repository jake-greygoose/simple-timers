///----------------------------------------------------------------------------------------------------
/// Copyright (c) Raidcore.GG - All rights reserved.
///
/// This code is licensed under the MIT license.
/// You should have received a copy of the license along with this source file.
/// You may obtain a copy of the license at: https://opensource.org/license/MIT
/// 
/// Name         :  entry.cpp
/// Description  :  Simple example of a Nexus addon implementation.
///----------------------------------------------------------------------------------------------------

#include <Windows.h>

#include "nexus/Nexus.h"
#include "mumble/Mumble.h"
#include "imgui/imgui.h"

/* proto */
void AddonLoad(AddonAPI* aApi);
void AddonUnload();
void AddonRender();
void AddonOptions();

/* globals */
AddonDefinition AddonDef = {};
HMODULE hSelf            = nullptr;
AddonAPI* APIDefs        = nullptr;
NexusLinkData* NexusLink = nullptr;
Mumble::Data* MumbleLink = nullptr;

bool someSetting         = false;

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
extern "C" __declspec(dllexport) AddonDefinition* GetAddonDef()
{
	AddonDef.Signature = -12345; // set to random unused negative integer
	AddonDef.APIVersion = NEXUS_API_VERSION;
	AddonDef.Name = "My First Nexus Addon";
	AddonDef.Version.Major = 1;
	AddonDef.Version.Minor = 0;
	AddonDef.Version.Build = 0;
	AddonDef.Version.Revision = 1;
	AddonDef.Author = "Me, Myself and I";
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

	ImGui::SetCurrentContext((ImGuiContext*)APIDefs->ImguiContext); // cast to ImGuiContext*
	ImGui::SetAllocatorFunctions((void* (*)(size_t, void*))APIDefs->ImguiMalloc, (void(*)(void*, void*))APIDefs->ImguiFree); // on imgui 1.80+

	NexusLink = (NexusLinkData*)APIDefs->DataLink.Get("DL_NEXUS_LINK");
	MumbleLink = (Mumble::Data*)APIDefs->DataLink.Get("DL_MUMBLE_LINK");

	// Add an options window and a regular render callback
	APIDefs->Renderer.Register(ERenderType_Render, AddonRender);
	APIDefs->Renderer.Register(ERenderType_OptionsRender, AddonOptions);

	APIDefs->Log(ELogLevel_DEBUG, "My First addon", "My <c=#00ff00>first addon</c> was loaded.");
}

///----------------------------------------------------------------------------------------------------
/// AddonUnload:
/// 	Everything you registered in AddonLoad, you should "undo" here.
///----------------------------------------------------------------------------------------------------
void AddonUnload()
{
	/* let's clean up after ourselves */
	APIDefs->Renderer.Deregister(AddonRender);
	APIDefs->Renderer.Deregister(AddonOptions);

	APIDefs->Log(ELogLevel_DEBUG, "My First addon", "<c=#ff0000>Signing off</c>, it was an honor commander.");
}

///----------------------------------------------------------------------------------------------------
/// AddonRender:
/// 	Called every frame. Safe to render any ImGui.
/// 	You can control visibility on loading screens with NexusLink->IsGameplay.
///----------------------------------------------------------------------------------------------------
void AddonRender()
{
	ImGuiIO& io = ImGui::GetIO();

	if (ImGui::Begin("MyFirstImGuiWindow"))
	{
		ImGui::Text("Hello Tyria!");
		ImGui::Text("UI Tick: %u",
			nullptr != MumbleLink
			? MumbleLink->UITick
			: 0
		);

		ImGui::Text("%s",
			nullptr != NexusLink
			? NexusLink->IsMoving
				? "Currently moving!"
				: "Currently standing still."
			: "We don't know whether we are standing or moving? NexusLink seems to be empty."
		);
	}
	ImGui::End();
}

///----------------------------------------------------------------------------------------------------
/// AddonOptions:
/// 	Basically an ImGui callback that doesn't need its own Begin/End calls.
///----------------------------------------------------------------------------------------------------
void AddonOptions()
{
	ImGui::Separator();
	ImGui::Text("My first Nexus addon");
	ImGui::Checkbox("Some setting", &someSetting);
}
