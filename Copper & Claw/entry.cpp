#include <Windows.h>
#include <cstdio>

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
UINT CopperAndClawWndProc(
    HWND hWnd,
    UINT uMsg,
    WPARAM wParam,
    LPARAM lParam
);

AddonDefinition_t AddonDef = {};

HMODULE hSelf = nullptr;
AddonAPI_t* APIDefs = nullptr;
NexusLinkData_t* NexusLink = nullptr;

//
// Temporary raw mouse-wheel diagnostics.
//
// These counters let us compare the WM_MOUSEWHEEL messages Nexus sends to
// Copper&Claw against ImGui::GetIO().MouseWheel while the options tab is open.
//
volatile LONG g_RawWheelMessageCount = 0;
volatile LONG g_RawWheelAbsoluteDelta = 0;
volatile LONG g_RawWheelLastDelta = 0;


namespace
{
    void RenderItemIndexGameOverlay(
        bool updateNoticeVisible
    )
    {
        if (
            !TradingPostItemIndexManager::
            IsBuilding()
            )
        {
            return;
        }

        const ImVec2 displaySize =
            ImGui::GetIO().DisplaySize;

        if (
            displaySize.x <= 0.0f ||
            displaySize.y <= 0.0f
            )
        {
            return;
        }

        const size_t buildProcessed =
            TradingPostItemIndexManager::
            GetBuildProcessedCount();

        const size_t buildTotal =
            TradingPostItemIndexManager::
            GetBuildTotalCount();

        float progress = 0.0f;

        if (buildTotal > 0)
        {
            progress =
                static_cast<float>(
                    buildProcessed
                    ) /
                static_cast<float>(
                    buildTotal
                    );

            if (progress < 0.0f)
            {
                progress = 0.0f;
            }
            else if (progress > 1.0f)
            {
                progress = 1.0f;
            }
        }

        const float panelWidth =
            displaySize.x > 460.0f
            ? 420.0f
            : displaySize.x - 40.0f;

        const float panelHeight =
            64.0f;

        const float updateNoticeTop =
            displaySize.y * 0.14f;

        const float panelTop =
            updateNoticeVisible
            ? updateNoticeTop + 126.0f + 8.0f
            : updateNoticeTop;

        const ImVec2 panelMin(
            displaySize.x * 0.5f -
            panelWidth * 0.5f,
            panelTop
        );

        const ImVec2 panelMax(
            panelMin.x + panelWidth,
            panelMin.y + panelHeight
        );

        //
        // IMPORTANT: this uses ImGui's FOREGROUND draw list instead of an
        // ImGui child/window. It is therefore a true game-screen overlay and
        // is not part of Nexus' Copper&Claw options window.
        //
        ImDrawList* drawList =
            ImGui::GetForegroundDrawList();

        if (drawList == nullptr)
        {
            return;
        }

        // Soft shadow.
        drawList->AddRectFilled(
            ImVec2(
                panelMin.x + 3.0f,
                panelMin.y + 4.0f
            ),
            ImVec2(
                panelMax.x + 3.0f,
                panelMax.y + 4.0f
            ),
            IM_COL32(
                0,
                0,
                0,
                105
            ),
            7.0f
        );

        // Janthir charcoal body.
        drawList->AddRectFilled(
            panelMin,
            panelMax,
            IM_COL32(
                11,
                12,
                15,
                247
            ),
            7.0f
        );

        // Janthir blue border.
        drawList->AddRect(
            panelMin,
            panelMax,
            IM_COL32(
                66,
                148,
                209,
                232
            ),
            7.0f,
            0,
            1.2f
        );

        // Ice-blue top telemetry rail.
        drawList->AddRectFilledMultiColor(
            ImVec2(
                panelMin.x + 1.0f,
                panelMin.y + 1.0f
            ),
            ImVec2(
                panelMax.x - 1.0f,
                panelMin.y + 4.0f
            ),
            IM_COL32(66, 148, 209, 255),
            IM_COL32(140, 204, 235, 255),
            IM_COL32(140, 204, 235, 210),
            IM_COL32(66, 148, 209, 210)
        );

        drawList->AddRectFilled(
            ImVec2(
                panelMin.x + 1.0f,
                panelMin.y + 13.0f
            ),
            ImVec2(
                panelMin.x + 4.0f,
                panelMax.y - 9.0f
            ),
            IM_COL32(
                66,
                148,
                209,
                240
            )
        );

        const ImVec2 titlePos(
            panelMin.x + 14.0f,
            panelMin.y + 10.0f
        );

        drawList->AddText(
            titlePos,
            IM_COL32(
                140,
                204,
                235,
                255
            ),
            "ITEM INDEX // BUILDING"
        );

        char countText[96] = {};

        if (buildTotal > 0)
        {
            const int percent =
                static_cast<int>(
                    progress * 100.0f +
                    0.5f
                    );

            std::snprintf(
                countText,
                sizeof(countText),
                "%llu / %llu   %d%%",
                static_cast<unsigned long long>(
                    buildProcessed
                    ),
                static_cast<unsigned long long>(
                    buildTotal
                    ),
                percent
            );
        }
        else
        {
            std::snprintf(
                countText,
                sizeof(countText),
                "PREPARING CATALOG..."
            );
        }

        const ImVec2 countSize =
            ImGui::CalcTextSize(
                countText
            );

        drawList->AddText(
            ImVec2(
                panelMax.x -
                14.0f -
                countSize.x,
                panelMin.y + 10.0f
            ),
            IM_COL32(
                210,
                226,
                240,
                245
            ),
            countText
        );

        const float railLeft =
            panelMin.x + 14.0f;

        const float railRight =
            panelMax.x - 14.0f;

        const float railTop =
            panelMin.y + 33.0f;

        const float railBottom =
            railTop + 7.0f;

        drawList->AddRectFilled(
            ImVec2(
                railLeft,
                railTop
            ),
            ImVec2(
                railRight,
                railBottom
            ),
            IM_COL32(
                24,
                30,
                39,
                255
            ),
            3.0f
        );

        if (buildTotal > 0)
        {
            const float fillRight =
                railLeft +
                (railRight - railLeft) *
                progress;

            if (fillRight > railLeft)
            {
                drawList->AddRectFilledMultiColor(
                    ImVec2(
                        railLeft,
                        railTop
                    ),
                    ImVec2(
                        fillRight,
                        railBottom
                    ),
                    IM_COL32(55, 123, 168, 255),
                    IM_COL32(140, 204, 235, 255),
                    IM_COL32(140, 204, 235, 255),
                    IM_COL32(55, 123, 168, 255)
                );
            }
        }
        else
        {
            // Small moving telemetry segment until the API reports a total.
            const double time =
                ImGui::GetTime();

            const double wholeSeconds =
                static_cast<double>(
                    static_cast<long long>(
                        time
                        )
                    );

            const float sweep =
                static_cast<float>(
                    time - wholeSeconds
                    );

            const float railWidth =
                railRight - railLeft;

            const float segmentWidth =
                railWidth * 0.22f;

            const float segmentLeft =
                railLeft +
                (railWidth - segmentWidth) *
                sweep;

            drawList->AddRectFilled(
                ImVec2(
                    segmentLeft,
                    railTop
                ),
                ImVec2(
                    segmentLeft + segmentWidth,
                    railBottom
                ),
                IM_COL32(
                    140,
                    204,
                    235,
                    245
                ),
                3.0f
            );
        }

        drawList->AddText(
            ImVec2(
                panelMin.x + 14.0f,
                panelMin.y + 45.0f
            ),
            IM_COL32(
                154,
                166,
                180,
                225
            ),
            "Building Trading Post search catalog..."
        );
    }
}

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
    // Standalone/non-Raidcore signature.
    // Replace only if Copper&Claw later receives an assigned Raidcore signature.
    //
    AddonDef.Signature =
        (uint32_t)-26090301;

    AddonDef.APIVersion =
        NEXUS_API_VERSION;

    AddonDef.Name =
        "Copper&Claw";

    AddonDef.Version.Major = 0;
    AddonDef.Version.Minor = 1;
    AddonDef.Version.Build = 4;
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

#ifdef _DEBUG
    //
    // Debug-only Nexus update source.
    // Release builds intentionally keep the updater disabled for now.
    //
    AddonDef.Provider =
        UP_GitHub;

    AddonDef.UpdateLink =
        "https://github.com/deadwrong03-ops/Copper-Claw-GW2-nexus";
#else
    AddonDef.Provider =
        UP_None;

    AddonDef.UpdateLink =
        nullptr;
#endif

    return &AddonDef;
}

UINT CopperAndClawWndProc(
    HWND,
    UINT uMsg,
    WPARAM wParam,
    LPARAM
)
{
    if (uMsg == WM_MOUSEWHEEL)
    {
        const LONG wheelDelta =
            static_cast<LONG>(
                GET_WHEEL_DELTA_WPARAM(
                    wParam
                )
                );

        const LONG absoluteDelta =
            wheelDelta >= 0
            ? wheelDelta
            : -wheelDelta;

        InterlockedIncrement(
            &g_RawWheelMessageCount
        );

        InterlockedExchangeAdd(
            &g_RawWheelAbsoluteDelta,
            absoluteDelta
        );

        InterlockedExchange(
            &g_RawWheelLastDelta,
            wheelDelta
        );
    }

    //
    // Do not consume or alter the message. Nexus WndProc callbacks return the
    // message value that should continue through the chain, so return uMsg
    // unchanged for this diagnostic-only handler.
    //
    return uMsg;
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

    //
    // Rewrite the settings file once after loading so newly added
    // settings are materialized with their defaults immediately.
    // Existing saved values remain preserved.
    //
    Settings::Save(
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

    if (
        APIDefs->WndProc_Register !=
        nullptr
        )
    {
        APIDefs->WndProc_Register(
            CopperAndClawWndProc
        );
    }

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
        if (
            APIDefs->WndProc_Deregister !=
            nullptr
            )
        {
            APIDefs->WndProc_Deregister(
                CopperAndClawWndProc
            );
        }

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

        const bool updateNoticeVisible =
            currentVersionKey != 0 &&
            g_Settings
            .acknowledgedUpdateNoticeVersion !=
            currentVersionKey;

        TradingPostUI::
            RenderUpdateNotice(
                hSelf,
                currentVersionKey
            );

        //
        // Deliberately outside AddonOptions / RenderTab. This draws directly
        // onto ImGui's foreground layer over the game screen, in the same
        // global RT_Render pass that drives the update popup.
        //
        RenderItemIndexGameOverlay(
            updateNoticeVisible
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
    //
    // Temporary ImGui-side wheel diagnostics. Raw WM_MOUSEWHEEL events are
    // counted by CopperAndClawWndProc above. Comparing the two tells us whether
    // wheel input is being lost before it reaches ImGui or whether scrolling
    // is being routed incorrectly after ImGui receives it.
    //
    static unsigned long long
        imguiWheelEventFrameCount = 0ULL;

    static float
        imguiWheelAbsoluteTotal = 0.0f;

    static float
        imguiWheelLastNonZero = 0.0f;

    const float currentImGuiWheel =
        ImGui::GetIO().MouseWheel;

    if (currentImGuiWheel != 0.0f)
    {
        ++imguiWheelEventFrameCount;

        imguiWheelAbsoluteTotal +=
            currentImGuiWheel >= 0.0f
            ? currentImGuiWheel
            : -currentImGuiWheel;

        imguiWheelLastNonZero =
            currentImGuiWheel;
    }

    const LONG rawWheelMessageCount =
        InterlockedCompareExchange(
            &g_RawWheelMessageCount,
            0,
            0
        );

    const LONG rawWheelAbsoluteDelta =
        InterlockedCompareExchange(
            &g_RawWheelAbsoluteDelta,
            0,
            0
        );

    const LONG rawWheelLastDelta =
        InterlockedCompareExchange(
            &g_RawWheelLastDelta,
            0,
            0
        );

    ImGui::TextUnformatted(
        "Copper&Claw"
    );

    ImGui::Separator();

    ImGui::TextColored(
        ImVec4(
            1.00f,
            0.78f,
            0.25f,
            1.00f
        ),
        "WHEEL DEBUG"
    );

    ImGui::SameLine(
        0.0f,
        8.0f
    );

    ImGui::TextDisabled(
        "Raw msgs: %ld | Raw notches: %.1f | Raw last: %ld | ImGui frames: %llu | ImGui total: %.1f | ImGui last: %.2f",
        rawWheelMessageCount,
        static_cast<double>(
            rawWheelAbsoluteDelta
            ) /
        static_cast<double>(
            WHEEL_DELTA
            ),
        rawWheelLastDelta,
        imguiWheelEventFrameCount,
        imguiWheelAbsoluteTotal,
        imguiWheelLastNonZero
    );

    if (
        ImGui::SmallButton(
            "Reset Wheel Debug"
        )
        )
    {
        InterlockedExchange(
            &g_RawWheelMessageCount,
            0
        );

        InterlockedExchange(
            &g_RawWheelAbsoluteDelta,
            0
        );

        InterlockedExchange(
            &g_RawWheelLastDelta,
            0
        );

        imguiWheelEventFrameCount =
            0ULL;

        imguiWheelAbsoluteTotal =
            0.0f;

        imguiWheelLastNonZero =
            0.0f;
    }

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
        "Copper&Claw v0.1.4"
    );
}
