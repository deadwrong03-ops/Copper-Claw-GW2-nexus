#include <Windows.h>

#include "nexus/Nexus.h"
#include "imgui/imgui.h"

#include "BackupManager.h"
#include "BackupUI.h"
#include "Settings.h"
#include "TradingPostHistoryManager.h"
#include "TradingPostItemIndexManager.h"
#include "TradingPostPriceManager.h"
#include "TradingPostUI.h"
#include "TradingPostWatchManager.h"

void AddonLoad(AddonAPI_t* aApi);
void AddonUnload();
void AddonRender();
void AddonOptions();

AddonDefinition_t AddonDef = {};

HMODULE hSelf = nullptr;
AddonAPI_t* APIDefs = nullptr;
NexusLinkData_t* NexusLink = nullptr;

BOOL APIENTRY DllMain(
    HMODULE hModule,
    DWORD ul_reason_for_call,
    LPVOID
)
{
    if (
        ul_reason_for_call ==
        DLL_PROCESS_ATTACH
        )
    {
        hSelf = hModule;
    }

    return TRUE;
}

extern "C"
__declspec(dllexport)
AddonDefinition_t* GetAddonDef()
{
    //
    // Provisional development signature.
    // Replace/register before public release.
    //
    AddonDef.Signature =
        (uint32_t)-26090301;

    AddonDef.APIVersion =
        NEXUS_API_VERSION;

    AddonDef.Name =
        "Copper&Claw";

    AddonDef.Version.Major = 0;
    AddonDef.Version.Minor = 1;
    AddonDef.Version.Build = 2;
    AddonDef.Version.Revision = 0;

    AddonDef.Author =
        "spectre9510";

    AddonDef.Description =
        "Trading Post watch lists, price history, trends, and target alerts for Guild Wars 2.";

    AddonDef.Load =
        AddonLoad;

    AddonDef.Unload =
        AddonUnload;

    AddonDef.Flags =
        AF_None;

    return &AddonDef;
}

void AddonLoad(
    AddonAPI_t* aApi
)
{
    APIDefs = aApi;

    ImGui::SetCurrentContext(
        (ImGuiContext*)
        APIDefs->ImguiContext
    );

    ImGui::SetAllocatorFunctions(
        (void* (*)(size_t, void*))
        APIDefs->ImguiMalloc,
        (void (*)(void*, void*))
        APIDefs->ImguiFree
    );

    //
    // Apply any staged restore before Settings or any manager
    // opens the live Copper&Claw data files.
    //
    BackupManager::
        ApplyPendingRestore(
            hSelf
        );

    Settings::Load(
        hSelf
    );

    BackupManager::
        Start(
            hSelf
        );

    TradingPostPriceManager::
        Start();

    TradingPostHistoryManager::
        Start(
            hSelf
        );

    TradingPostItemIndexManager::
        Start(
            hSelf
        );

    TradingPostWatchManager::
        Start(
            hSelf
        );

    NexusLink =
        (NexusLinkData_t*)
        APIDefs->DataLink_Get(
            "DL_NEXUS_LINK"
        );

    APIDefs->GUI_Register(
        RT_Render,
        AddonRender
    );

    APIDefs->GUI_Register(
        RT_OptionsRender,
        AddonOptions
    );

    APIDefs->Log(
        LOGL_INFO,
        "Copper&Claw",
        "Copper&Claw loaded."
    );
}

void AddonUnload()
{
    Settings::Save(
        hSelf
    );

    BackupManager::
        Shutdown();

    TradingPostWatchManager::
        Shutdown();

    TradingPostItemIndexManager::
        Shutdown();

    TradingPostItemIndexManager::
        Reset();

    TradingPostPriceManager::
        Shutdown();

    TradingPostPriceManager::
        Reset();

    TradingPostHistoryManager::
        Shutdown();

    if (APIDefs != nullptr)
    {
        APIDefs->GUI_Deregister(
            AddonRender
        );

        APIDefs->GUI_Deregister(
            AddonOptions
        );

        APIDefs->Log(
            LOGL_INFO,
            "Copper&Claw",
            "Copper&Claw unloaded."
        );
    }

    NexusLink = nullptr;
    APIDefs = nullptr;
}

void AddonRender()
{
    TradingPostWatchManager::
        Update();

    BackupManager::
        Update();

    const bool isGameplay =
        NexusLink != nullptr &&
        NexusLink->IsGameplay;

    const bool tradingPostTabVisible =
        TradingPostUI::
        WasTabVisibleRecently();

    if (isGameplay)
    {
        const uint64_t currentVersionKey =
            (
                static_cast<uint64_t>(
                    AddonDef.Version.Major
                    ) << 48
                ) |
            (
                static_cast<uint64_t>(
                    AddonDef.Version.Minor
                    ) << 32
                ) |
            (
                static_cast<uint64_t>(
                    AddonDef.Version.Build
                    ) << 16
                ) |
            static_cast<uint64_t>(
                AddonDef.Version.Revision
                );

        TradingPostUI::
            RenderUpdateNotice(
                hSelf,
                currentVersionKey
            );
    }

    if (
        isGameplay &&
        !tradingPostTabVisible
        )
    {
        TradingPostUI::
            RenderTargetOverlay();
    }
}

void AddonOptions()
{
    ImGui::TextUnformatted(
        "Copper&Claw"
    );

    ImGui::Separator();

    if (
        ImGui::BeginTabBar(
            "##CopperAndClawOptionsTabs"
        )
        )
    {
        if (
            ImGui::BeginTabItem(
                "Trading Post"
            )
            )
        {
            TradingPostUI::
                RenderTab(
                    hSelf
                );

            ImGui::EndTabItem();
        }

        if (
            ImGui::BeginTabItem(
                "Backup & Restore"
            )
            )
        {
            BackupUI::
                RenderTab(
                    hSelf
                );

            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    ImGui::Spacing();
    ImGui::Separator();

    ImGui::TextDisabled(
        "Copper&Claw v0.1.2 - Development Build"
    );
}
