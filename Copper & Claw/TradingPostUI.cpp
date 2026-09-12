#include "TradingPostUI.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "imgui/imgui.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "nexus/Nexus.h"

#include "Settings.h"
#include "TradingPostHistoryManager.h"
#include "TradingPostItemIndexManager.h"
#include "TradingPostPriceManager.h"
#include "TradingPostWatchManager.h"

//
// Defined in entry.cpp. Nexus owns the pointed-to API table for the lifetime
// of the loaded addon.
//
extern AddonAPI_t* APIDefs;

namespace
{
    int g_TradingPostTabLastVisibleFrame =
        -1000;

    Texture_t* GetTradingPostItemIcon(
        uint32_t itemID
    )
    {
        if (
            APIDefs == nullptr ||
            APIDefs->Textures_GetOrCreateFromURL ==
            nullptr
            )
        {
            return nullptr;
        }

        TradingPostIndexedItem indexedItem;

        if (
            !TradingPostItemIndexManager::
            TryGetItem(
                itemID,
                indexedItem
            ) ||
            indexedItem.iconUrl.empty()
            )
        {
            return nullptr;
        }

        //
        // Guild Wars 2 item metadata currently supplies render URLs in this
        // form:
        // https://render.guildwars2.com/file/.../...png
        //
        // Nexus wants the remote/base URL and endpoint separately.
        //
        static const std::string renderRemote =
            "https://render.guildwars2.com";

        if (
            indexedItem.iconUrl.rfind(
                renderRemote,
                0
            ) != 0
            )
        {
            return nullptr;
        }

        const std::string endpoint =
            indexedItem.iconUrl.substr(
                renderRemote.size()
            );

        if (endpoint.empty())
        {
            return nullptr;
        }

        const std::string textureIdentifier =
            "CopperAndClaw_ItemIcon_" +
            std::to_string(
                itemID
            );

        return
            APIDefs->
            Textures_GetOrCreateFromURL(
                textureIdentifier.c_str(),
                renderRemote.c_str(),
                endpoint.c_str()
            );
    }

    std::string FormatGoldWithCommas(
        uint64_t gold
    )
    {
        std::string value =
            std::to_string(
                gold
            );

        for (
            int i =
            static_cast<int>(
                value.size()
                ) - 3;
            i > 0;
            i -= 3
            )
        {
            value.insert(
                static_cast<size_t>(
                    i
                    ),
                ","
            );
        }

        return value;
    }

    std::string FormatCoinValue(
        uint64_t copper
    )
    {
        const uint64_t gold =
            copper / 10000ULL;

        const uint64_t silver =
            (copper % 10000ULL) / 100ULL;

        const uint64_t copperOnly =
            copper % 100ULL;

        return
            FormatGoldWithCommas(
                gold
            ) +
            "g " +
            std::to_string(
                silver
            ) +
            "s " +
            std::to_string(
                copperOnly
            ) +
            "c";
    }
}

bool TradingPostUI::WasTabVisibleRecently()
{
    const int currentImGuiFrame =
        ImGui::GetFrameCount();

    return
        currentImGuiFrame -
        g_TradingPostTabLastVisibleFrame <= 1;
}

void TradingPostUI::RenderUpdateNotice(
    void* moduleHandle,
    uint64_t currentVersionKey
)
{
    if (
        currentVersionKey == 0 ||
        g_Settings
        .acknowledgedUpdateNoticeVersion ==
        currentVersionKey
        )
    {
        return;
    }

    const ImVec2 displaySize =
        ImGui::GetIO().
        DisplaySize;

    const float noticeWidth =
        460.0f;

    const float noticeHeight =
        126.0f;

    ImGui::SetNextWindowPos(
        ImVec2(
            displaySize.x * 0.5f,
            displaySize.y * 0.14f
        ),
        ImGuiCond_Always,
        ImVec2(
            0.5f,
            0.0f
        )
    );

    ImGui::SetNextWindowSize(
        ImVec2(
            noticeWidth,
            noticeHeight
        ),
        ImGuiCond_Always
    );

    ImGui::PushStyleVar(
        ImGuiStyleVar_WindowPadding,
        ImVec2(
            18.0f,
            14.0f
        )
    );

    ImGui::PushStyleVar(
        ImGuiStyleVar_WindowRounding,
        7.0f
    );

    ImGui::PushStyleVar(
        ImGuiStyleVar_WindowBorderSize,
        1.0f
    );

    ImGui::PushStyleColor(
        ImGuiCol_WindowBg,
        ImVec4(
            0.055f,
            0.060f,
            0.070f,
            0.97f
        )
    );

    ImGui::PushStyleColor(
        ImGuiCol_Border,
        ImVec4(
            0.62f,
            0.43f,
            0.20f,
            0.92f
        )
    );

    const ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoDecoration |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoScrollWithMouse;

    if (
        ImGui::Begin(
            "##CopperAndClawUpdateNotice",
            nullptr,
            flags
        )
        )
    {
        ImDrawList* drawList =
            ImGui::GetWindowDrawList();

        const ImVec2 windowPos =
            ImGui::GetWindowPos();

        //
        // Thin copper accent across the top keeps the notice visually tied
        // to Copper&Claw without turning it into a large alert banner.
        //
        drawList->AddRectFilled(
            ImVec2(
                windowPos.x + 1.0f,
                windowPos.y + 1.0f
            ),
            ImVec2(
                windowPos.x +
                noticeWidth - 1.0f,
                windowPos.y + 4.0f
            ),
            ImGui::GetColorU32(
                ImVec4(
                    0.82f,
                    0.55f,
                    0.24f,
                    1.00f
                )
            ),
            6.0f
        );

        ImGui::SetCursorPosY(
            15.0f
        );

        ImGui::SetWindowFontScale(
            1.16f
        );

        ImGui::TextColored(
            ImVec4(
                0.95f,
                0.72f,
                0.38f,
                1.00f
            ),
            "Copper&Claw Updated"
        );

        ImGui::SetWindowFontScale(
            1.00f
        );

        ImGui::SetCursorPosY(
            43.0f
        );

        ImGui::TextColored(
            ImVec4(
                0.86f,
                0.86f,
                0.88f,
                1.00f
            ),
            "Some updates may rebuild the Trading Post item index."
        );

        ImGui::TextColored(
            ImVec4(
                0.66f,
                0.67f,
                0.70f,
                1.00f
            ),
            "This happens automatically and may take a couple minutes."
        );

        const float buttonWidth =
            96.0f;

        const float buttonHeight =
            27.0f;

        ImGui::SetCursorPosX(
            noticeWidth -
            18.0f -
            buttonWidth
        );

        ImGui::SetCursorPosY(
            noticeHeight -
            14.0f -
            buttonHeight
        );

        ImGui::PushStyleVar(
            ImGuiStyleVar_FrameRounding,
            4.0f
        );

        ImGui::PushStyleColor(
            ImGuiCol_Button,
            ImVec4(
                0.24f,
                0.17f,
                0.09f,
                1.00f
            )
        );

        ImGui::PushStyleColor(
            ImGuiCol_ButtonHovered,
            ImVec4(
                0.36f,
                0.25f,
                0.12f,
                1.00f
            )
        );

        ImGui::PushStyleColor(
            ImGuiCol_ButtonActive,
            ImVec4(
                0.46f,
                0.31f,
                0.14f,
                1.00f
            )
        );

        ImGui::PushStyleColor(
            ImGuiCol_Text,
            ImVec4(
                0.96f,
                0.82f,
                0.58f,
                1.00f
            )
        );

        if (
            ImGui::Button(
                "Got it",
                ImVec2(
                    buttonWidth,
                    buttonHeight
                )
            )
            )
        {
            g_Settings
                .acknowledgedUpdateNoticeVersion =
                currentVersionKey;

            Settings::Save(
                moduleHandle
            );
        }

        ImGui::PopStyleColor(
            4
        );

        ImGui::PopStyleVar();
    }

    ImGui::End();

    ImGui::PopStyleColor(
        2
    );

    ImGui::PopStyleVar(
        3
    );
}

void TradingPostUI::RenderTargetOverlay()
{
    TradingPostTargetAlert alert;

    if (
        !TradingPostWatchManager::
        TryGetActiveTargetAlert(
            alert
        )
        )
    {
        return;
    }

    const ImVec2 displaySize =
        ImGui::GetIO().
        DisplaySize;

    const float overlayWidth =
        displaySize.x > 800.0f
        ? 720.0f
        : displaySize.x - 40.0f;

    const float overlayHeight =
        230.0f;

    const ImVec2 overlayPosition(
        displaySize.x * 0.5f,
        displaySize.y * 0.08f
    );

    ImGui::SetNextWindowPos(
        overlayPosition,
        ImGuiCond_Always,
        ImVec2(
            0.5f,
            0.0f
        )
    );

    ImGui::SetNextWindowSize(
        ImVec2(
            overlayWidth,
            overlayHeight
        ),
        ImGuiCond_Always
    );

    const float celebrationTime =
        static_cast<float>(
            ImGui::GetTime()
            );

    const float pulse =
        0.5f +
        0.5f *
        std::sin(
            celebrationTime *
            5.0f
        );

    ImGui::PushStyleVar(
        ImGuiStyleVar_WindowPadding,
        ImVec2(
            24.0f,
            18.0f
        )
    );

    ImGui::PushStyleVar(
        ImGuiStyleVar_WindowRounding,
        8.0f
    );

    ImGui::PushStyleVar(
        ImGuiStyleVar_WindowBorderSize,
        2.0f +
        1.25f * pulse
    );

    ImGui::PushStyleColor(
        ImGuiCol_WindowBg,
        ImVec4(
            0.055f,
            0.075f,
            0.045f,
            0.97f
        )
    );

    ImGui::PushStyleColor(
        ImGuiCol_Border,
        ImVec4(
            0.70f +
            0.22f * pulse,
            0.82f +
            0.13f * pulse,
            0.16f,
            1.00f
        )
    );

    const ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoDecoration |
        ImGuiWindowFlags_NoFocusOnAppearing |
        ImGuiWindowFlags_NoNav |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoScrollWithMouse;

    if (
        ImGui::Begin(
            "##TradingPostTargetOverlay",
            nullptr,
            flags
        )
        )
    {
        ImDrawList* drawList =
            ImGui::GetWindowDrawList();

        const ImVec2 windowMin =
            ImGui::GetWindowPos();

        const ImVec2 windowSize =
            ImGui::GetWindowSize();

        const ImVec2 windowMax(
            windowMin.x +
            windowSize.x,
            windowMin.y +
            windowSize.y
        );

        drawList->PushClipRect(
            windowMin,
            windowMax,
            true
        );

        const ImU32 confettiColors[] =
        {
            IM_COL32(255, 196, 40, 235),
            IM_COL32(80, 220, 110, 220),
            IM_COL32(90, 170, 255, 220),
            IM_COL32(220, 90, 255, 220),
            IM_COL32(255, 90, 90, 220),
            IM_COL32(255, 145, 35, 220)
        };

        constexpr int CONFETTI_COUNT =
            44;

        for (
            int i = 0;
            i < CONFETTI_COUNT;
            ++i
            )
        {
            const float seed =
                static_cast<float>(
                    i
                    );

            const float xFraction =
                std::fmod(
                    seed *
                    0.6180339f,
                    1.0f
                );

            const float fallSpeed =
                24.0f +
                std::fmod(
                    seed *
                    11.0f,
                    34.0f
                );

            const float y =
                windowMin.y +
                std::fmod(
                    celebrationTime *
                    fallSpeed +
                    seed *
                    19.0f,
                    windowSize.y +
                    18.0f
                ) -
                9.0f;

            const float sway =
                std::sin(
                    celebrationTime *
                    2.0f +
                    seed
                ) *
                8.0f;

            const float x =
                windowMin.x +
                xFraction *
                windowSize.x +
                sway;

            const float pieceSize =
                2.5f +
                std::fmod(
                    seed,
                    3.0f
                );

            drawList->AddRectFilled(
                ImVec2(
                    x -
                    pieceSize,
                    y -
                    1.5f
                ),
                ImVec2(
                    x +
                    pieceSize,
                    y +
                    1.5f
                ),
                confettiColors[
                    i %
                        (
                            sizeof(
                                confettiColors
                                ) /
                            sizeof(
                                confettiColors[0]
                                )
                            )
                ],
                1.0f
            );
        }

        for (
            int burstIndex = 0;
            burstIndex < 3;
            ++burstIndex
            )
        {
            const float centerX =
                windowMin.x +
                windowSize.x *
                (
                    0.18f +
                    0.32f *
                    static_cast<float>(
                        burstIndex
                        )
                    );

            const float centerY =
                windowMin.y +
                38.0f +
                6.0f *
                std::sin(
                    celebrationTime *
                    2.4f +
                    static_cast<float>(
                        burstIndex
                        )
                );

            const float radius =
                9.0f +
                5.0f *
                pulse;

            for (
                int ray = 0;
                ray < 8;
                ++ray
                )
            {
                const float angle =
                    static_cast<float>(
                        ray
                        ) *
                    0.78539816f +
                    celebrationTime *
                    0.20f;

                const ImVec2 rayEnd(
                    centerX +
                    std::cos(
                        angle
                    ) *
                    radius,
                    centerY +
                    std::sin(
                        angle
                    ) *
                    radius
                );

                drawList->AddLine(
                    ImVec2(
                        centerX,
                        centerY
                    ),
                    rayEnd,
                    IM_COL32(
                        255,
                        215,
                        70,
                        190
                    ),
                    1.3f
                );
            }

            drawList->AddCircleFilled(
                ImVec2(
                    centerX,
                    centerY
                ),
                2.3f +
                1.5f *
                pulse,
                IM_COL32(
                    255,
                    245,
                    170,
                    255
                )
            );
        }

        drawList->AddRect(
            ImVec2(
                windowMin.x +
                2.0f,
                windowMin.y +
                2.0f
            ),
            ImVec2(
                windowMax.x -
                2.0f,
                windowMax.y -
                2.0f
            ),
            ImGui::GetColorU32(
                ImVec4(
                    0.64f +
                    0.30f * pulse,
                    0.82f +
                    0.14f * pulse,
                    0.10f,
                    0.96f
                )
            ),
            7.0f,
            0,
            2.0f +
            1.25f * pulse
        );

        const float innerTop =
            windowMin.y +
            104.0f;

        const float innerBottom =
            windowMin.y +
            174.0f;

        const float innerLeft =
            windowMin.x +
            72.0f;

        const float innerRight =
            windowMax.x -
            72.0f;

        drawList->AddRectFilled(
            ImVec2(
                innerLeft,
                innerTop
            ),
            ImVec2(
                innerRight,
                innerBottom
            ),
            IM_COL32(
                14,
                22,
                13,
                210
            ),
            6.0f
        );

        drawList->AddRect(
            ImVec2(
                innerLeft,
                innerTop
            ),
            ImVec2(
                innerRight,
                innerBottom
            ),
            IM_COL32(
                120,
                145,
                52,
                105
            ),
            6.0f,
            0,
            1.0f
        );

        drawList->AddLine(
            ImVec2(
                windowMin.x +
                windowSize.x *
                0.50f,
                innerTop +
                12.0f
            ),
            ImVec2(
                windowMin.x +
                windowSize.x *
                0.50f,
                innerBottom -
                12.0f
            ),
            IM_COL32(
                170,
                185,
                100,
                85
            ),
            1.0f
        );

        drawList->PopClipRect();

        ImGui::SetWindowFontScale(
            1.55f
        );

        const char* title =
            "TARGET REACHED!";

        const float titleWidth =
            ImGui::CalcTextSize(
                title
            ).x;

        ImGui::SetCursorPosX(
            (
                ImGui::GetWindowSize().x -
                titleWidth
                ) *
            0.5f
        );

        ImGui::TextColored(
            ImVec4(
                1.00f,
                0.82f +
                0.12f * pulse,
                0.18f,
                1.00f
            ),
            "%s",
            title
        );

        ImGui::SetWindowFontScale(
            1.00f
        );

        ImGui::Spacing();

        ImGui::SetWindowFontScale(
            1.16f
        );

        const float itemWidth =
            ImGui::CalcTextSize(
                alert.name.c_str()
            ).x;

        ImGui::SetCursorPosX(
            (
                ImGui::GetWindowSize().x -
                itemWidth
                ) *
            0.5f
        );

        ImGui::TextColored(
            ImVec4(
                0.94f,
                0.82f,
                1.00f,
                1.00f
            ),
            "%s",
            alert.name.c_str()
        );

        ImGui::SetWindowFontScale(
            1.00f
        );

        const bool isBuyTargetAlert =
            alert.type ==
            TradingPostTargetAlertType::Buy;

        const uint64_t currentPriceCopper =
            isBuyTargetAlert
            ? alert.buyUnitPrice
            : alert.sellUnitPrice;

        const uint64_t currentTargetCopper =
            isBuyTargetAlert
            ? alert.targetBuyCopper
            : alert.targetSellCopper;

        const std::string currentPriceText =
            FormatCoinValue(
                currentPriceCopper
            );

        const std::string targetText =
            FormatCoinValue(
                currentTargetCopper
            );

        ImGui::SetCursorPosY(
            112.0f
        );

        if (
            ImGui::BeginTable(
                "##TargetReachedPriceCards",
                2,
                ImGuiTableFlags_SizingStretchSame |
                ImGuiTableFlags_NoSavedSettings
            )
            )
        {
            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(
                0
            );

            const char* currentPriceLabel =
                isBuyTargetAlert
                ? "CURRENT BUY"
                : "CURRENT SELL";

            const float currentPriceLabelWidth =
                ImGui::CalcTextSize(
                    currentPriceLabel
                ).x;

            ImGui::SetCursorPosX(
                ImGui::GetCursorPosX() +
                (
                    ImGui::GetColumnWidth() -
                    currentPriceLabelWidth
                    ) *
                0.5f
            );

            ImGui::TextDisabled(
                "%s",
                currentPriceLabel
            );

            ImGui::SetWindowFontScale(
                1.12f
            );

            const float currentPriceWidth =
                ImGui::CalcTextSize(
                    currentPriceText.c_str()
                ).x;

            ImGui::SetCursorPosX(
                ImGui::GetCursorPosX() +
                (
                    ImGui::GetColumnWidth() -
                    currentPriceWidth
                    ) *
                0.5f
            );

            ImGui::TextColored(
                isBuyTargetAlert
                ? ImVec4(
                    0.45f,
                    0.75f,
                    1.00f,
                    1.00f
                )
                : ImVec4(
                    0.45f,
                    0.92f,
                    0.56f,
                    1.00f
                ),
                "%s",
                currentPriceText.c_str()
            );

            ImGui::SetWindowFontScale(
                1.00f
            );

            ImGui::TableSetColumnIndex(
                1
            );

            const char* targetLabel =
                "YOUR TARGET";

            const float targetLabelWidth =
                ImGui::CalcTextSize(
                    targetLabel
                ).x;

            ImGui::SetCursorPosX(
                ImGui::GetCursorPosX() +
                (
                    ImGui::GetColumnWidth() -
                    targetLabelWidth
                    ) *
                0.5f
            );

            ImGui::TextDisabled(
                "%s",
                targetLabel
            );

            ImGui::SetWindowFontScale(
                1.12f
            );

            const float targetWidth =
                ImGui::CalcTextSize(
                    targetText.c_str()
                ).x;

            ImGui::SetCursorPosX(
                ImGui::GetCursorPosX() +
                (
                    ImGui::GetColumnWidth() -
                    targetWidth
                    ) *
                0.5f
            );

            ImGui::TextColored(
                ImVec4(
                    1.00f,
                    0.78f,
                    0.24f,
                    1.00f
                ),
                "%s",
                targetText.c_str()
            );

            ImGui::SetWindowFontScale(
                1.00f
            );

            ImGui::EndTable();
        }

        ImGui::SetCursorPosY(
            overlayHeight -
            42.0f
        );

        const float dismissWidth =
            138.0f;

        ImGui::SetCursorPosX(
            (
                ImGui::GetWindowSize().x -
                dismissWidth
                ) *
            0.5f
        );

        ImGui::PushStyleVar(
            ImGuiStyleVar_FrameRounding,
            5.0f
        );

        ImGui::PushStyleColor(
            ImGuiCol_Button,
            ImVec4(
                0.23f,
                0.30f,
                0.10f,
                0.95f
            )
        );

        ImGui::PushStyleColor(
            ImGuiCol_ButtonHovered,
            ImVec4(
                0.38f,
                0.48f,
                0.14f,
                1.00f
            )
        );

        ImGui::PushStyleColor(
            ImGuiCol_ButtonActive,
            ImVec4(
                0.48f,
                0.58f,
                0.18f,
                1.00f
            )
        );

        if (
            ImGui::Button(
                "Dismiss Party",
                ImVec2(
                    dismissWidth,
                    28.0f
                )
            )
            )
        {
            TradingPostWatchManager::
                DismissActiveTargetAlert();
        }

        ImGui::PopStyleColor(
            3
        );

        ImGui::PopStyleVar();
    }

    ImGui::End();

    ImGui::PopStyleColor(
        2
    );

    ImGui::PopStyleVar(
        3
    );
}

void TradingPostUI::RenderTab(
    void* moduleHandle
)
{
    g_TradingPostTabLastVisibleFrame =
        ImGui::GetFrameCount();

    const ImVec4 goodColor(
        0.35f,
        0.90f,
        0.45f,
        1.00f
    );

    const ImVec4 attentionColor(
        1.00f,
        0.78f,
        0.25f,
        1.00f
    );

    const ImVec4 sellColor(
        0.45f,
        0.90f,
        0.55f,
        1.00f
    );

    const ImVec4 buyColor(
        0.45f,
        0.75f,
        1.00f,
        1.00f
    );

    const ImVec4 trendDownColor(
        1.00f,
        0.40f,
        0.40f,
        1.00f
    );

    const ImVec4 mutedColor(
        0.65f,
        0.65f,
        0.65f,
        1.00f
    );

    const ImVec4 itemNameColor(
        0.92f,
        0.78f,
        1.00f,
        1.00f
    );

    enum class PriceTrend
    {
        Neutral,
        Up,
        Down
    };

    const uint64_t trendWindowSecondsOptions[] =
    {
        15ULL * 60ULL,
        30ULL * 60ULL,
        60ULL * 60ULL,
        6ULL * 60ULL * 60ULL,
        24ULL * 60ULL * 60ULL,
        3ULL * 24ULL * 60ULL * 60ULL,
        7ULL * 24ULL * 60ULL * 60ULL,
        30ULL * 24ULL * 60ULL * 60ULL,
        90ULL * 24ULL * 60ULL * 60ULL
    };

    const char* trendWindowLabels[] =
    {
        "15m",
        "30m",
        "1h",
        "6h",
        "24h",
        "3d",
        "7d",
        "30d",
        "90d"
    };

    const char* watchSortLabels[] =
    {
        "Added Order",
        "Name A-Z",
        "Closest to Target",
        "Biggest Price Drop",
        "Biggest Price Rise",
        "Most Volatile",
        "Least Volatile"
    };

    const char* viewDensityLabels[] =
    {
        "Normal",
        "Slim",
        "Ultra Slim"
    };

    const uint64_t trendWindowSeconds =
        trendWindowSecondsOptions[
            g_Settings.tradingPostTrendWindowIndex
        ];

    const char* trendWindowLabel =
        trendWindowLabels[
            g_Settings.tradingPostTrendWindowIndex
        ];

    struct TrendInfo
    {
        PriceTrend trend =
            PriceTrend::Neutral;

        int64_t copperChange =
            0;

        double percentChange =
            0.0;

        bool available =
            false;
    };

    auto CalculateTrend =
        [](
            const std::vector<
            TradingPostHistoryPoint
            >& points,
            bool useSellPrice,
            uint64_t windowSeconds
            )
        {
            TrendInfo result;

            if (points.size() < 2)
            {
                return
                    result;
            }

            const TradingPostHistoryPoint&
                newestPoint =
                points.back();

            if (
                newestPoint.timestampUnixSeconds <
                windowSeconds
                )
            {
                return
                    result;
            }

            const uint64_t targetTimestamp =
                newestPoint.timestampUnixSeconds -
                windowSeconds;

            if (
                points.front().
                timestampUnixSeconds >
                targetTimestamp
                )
            {
                //
                // The stored history does not reach the requested
                // trend-window boundary yet.
                //
                return
                    result;
            }

            const TradingPostHistoryPoint*
                olderPoint =
                nullptr;

            uint64_t closestDistanceSeconds =
                UINT64_MAX;

            //
            // Use the observation closest in real time to the requested
            // window boundary. The previous implementation always chose
            // the newest point at-or-before the boundary, which could make
            // a displayed "30m" trend represent substantially more than
            // 30 minutes when observations were sparse.
            //
            // Requiring points.front() <= targetTimestamp above prevents
            // a short history span from pretending to cover the full window.
            //
            for (
                size_t i = 0;
                i + 1 < points.size();
                ++i
                )
            {
                const TradingPostHistoryPoint&
                    candidate =
                    points[i];

                const uint64_t distanceSeconds =
                    candidate.timestampUnixSeconds >=
                    targetTimestamp
                    ? candidate.timestampUnixSeconds -
                    targetTimestamp
                    : targetTimestamp -
                    candidate.timestampUnixSeconds;

                if (
                    olderPoint == nullptr ||
                    distanceSeconds <
                    closestDistanceSeconds
                    )
                {
                    olderPoint =
                        &candidate;

                    closestDistanceSeconds =
                        distanceSeconds;
                }
            }

            if (olderPoint == nullptr)
            {
                return
                    result;
            }

            const uint32_t olderPrice =
                useSellPrice
                ? olderPoint->
                sellUnitPrice
                : olderPoint->
                buyUnitPrice;

            const uint32_t newerPrice =
                useSellPrice
                ? newestPoint.
                sellUnitPrice
                : newestPoint.
                buyUnitPrice;

            if (olderPrice == 0)
            {
                return
                    result;
            }

            result.available =
                true;

            result.copperChange =
                static_cast<int64_t>(
                    newerPrice
                    ) -
                static_cast<int64_t>(
                    olderPrice
                    );

            result.percentChange =
                static_cast<double>(
                    result.copperChange
                    ) /
                static_cast<double>(
                    olderPrice
                    ) *
                100.0;

            if (
                result.copperChange > 0
                )
            {
                result.trend =
                    PriceTrend::Up;
            }
            else if (
                result.copperChange < 0
                )
            {
                result.trend =
                    PriceTrend::Down;
            }
            else
            {
                result.trend =
                    PriceTrend::Neutral;
            }

            return
                result;
        };


    auto CalculateObservedTrend =
        [](
            const std::vector<
            TradingPostHistoryPoint
            >& points,
            bool useSellPrice
            )
        {
            TrendInfo result;

            if (points.size() < 2)
            {
                return result;
            }

            const TradingPostHistoryPoint&
                olderPoint =
                points.front();

            const TradingPostHistoryPoint&
                newerPoint =
                points.back();

            const uint32_t olderPrice =
                useSellPrice
                ? olderPoint.sellUnitPrice
                : olderPoint.buyUnitPrice;

            const uint32_t newerPrice =
                useSellPrice
                ? newerPoint.sellUnitPrice
                : newerPoint.buyUnitPrice;

            if (olderPrice == 0)
            {
                return result;
            }

            result.available =
                true;

            result.copperChange =
                static_cast<int64_t>(
                    newerPrice
                    ) -
                static_cast<int64_t>(
                    olderPrice
                    );

            result.percentChange =
                static_cast<double>(
                    result.copperChange
                    ) /
                static_cast<double>(
                    olderPrice
                    ) *
                100.0;

            if (result.copperChange > 0)
            {
                result.trend =
                    PriceTrend::Up;
            }
            else if (result.copperChange < 0)
            {
                result.trend =
                    PriceTrend::Down;
            }
            else
            {
                result.trend =
                    PriceTrend::Neutral;
            }

            return result;
        };

    auto BuildCollectorSampledHistory =
        [](
            const std::vector<
            TradingPostHistoryPoint
            >& history,
            const std::vector<uint64_t>&
            completedSnapshotTimestamps
            )
        {
            std::vector<
                TradingPostHistoryPoint
            > sampledHistory;

            if (
                history.empty() ||
                completedSnapshotTimestamps.empty()
                )
            {
                return sampledHistory;
            }

            sampledHistory.reserve(
                completedSnapshotTimestamps.size()
            );

            size_t historyIndex = 0;
            bool hasKnownPrice = false;

            TradingPostHistoryPoint currentState;

            for (
                const uint64_t snapshotTimestamp :
            completedSnapshotTimestamps
                )
            {
                while (
                    historyIndex < history.size() &&
                    history[
                        historyIndex
                    ].timestampUnixSeconds <=
                    snapshotTimestamp
                            )
                {
                    currentState =
                        history[
                            historyIndex
                        ];

                    hasKnownPrice =
                        currentState.buyUnitPrice > 0 ||
                        currentState.sellUnitPrice > 0;

                    ++historyIndex;
                }

                if (!hasKnownPrice)
                {
                    continue;
                }

                TradingPostHistoryPoint sampledPoint;

                sampledPoint.timestampUnixSeconds =
                    snapshotTimestamp;

                sampledPoint.buyUnitPrice =
                    currentState.buyUnitPrice;

                sampledPoint.sellUnitPrice =
                    currentState.sellUnitPrice;

                sampledHistory.push_back(
                    sampledPoint
                );
            }

            return sampledHistory;
        };

    auto FormatTrendCoinDelta =
        [](
            int64_t copperChange
            )
        {
            const bool negative =
                copperChange < 0;

            uint64_t magnitude =
                negative
                ? static_cast<uint64_t>(
                    -copperChange
                    )
                : static_cast<uint64_t>(
                    copperChange
                    );

            const uint64_t gold =
                magnitude /
                10000ULL;

            const uint64_t silver =
                (
                    magnitude %
                    10000ULL
                    ) /
                100ULL;

            const uint64_t copper =
                magnitude %
                100ULL;

            std::string value =
                negative
                ? "-"
                : "+";

            if (gold > 0)
            {
                value +=
                    FormatGoldWithCommas(
                        gold
                    ) +
                    "g ";

                value +=
                    std::to_string(
                        silver
                    ) +
                    "s ";

                value +=
                    std::to_string(
                        copper
                    ) +
                    "c";

                return
                    value;
            }

            if (silver > 0)
            {
                value +=
                    std::to_string(
                        silver
                    ) +
                    "s ";

                value +=
                    std::to_string(
                        copper
                    ) +
                    "c";

                return
                    value;
            }

            value +=
                std::to_string(
                    copper
                ) +
                "c";

            return
                value;
        };

    struct DealWindowStats
    {
        uint64_t averageSell = 0;
        uint64_t lowerQuartileSell = 0;
        uint64_t upperQuartileSell = 0;
        size_t sampleCount = 0;
        bool available = false;
    };

    auto CalculateDealWindowStats =
        [](
            const std::vector<
            TradingPostHistoryPoint
            >& points,
            uint64_t windowSeconds
            )
        {
            DealWindowStats result;

            if (points.size() < 2)
            {
                return
                    result;
            }

            const uint64_t newestTimestamp =
                points.back().
                timestampUnixSeconds;

            if (
                newestTimestamp <
                windowSeconds
                )
            {
                return
                    result;
            }

            const uint64_t startTimestamp =
                newestTimestamp -
                windowSeconds;

            //
            // Use the same historical anchor concept as the trend:
            // find the newest observation at or before the requested
            // window boundary. This lets Deal/Signal remain useful
            // after the game has been closed for part of the window.
            //
            size_t firstSampleIndex =
                points.size();

            for (
                size_t i =
                points.size();
                i > 0;
                --i
                )
            {
                const size_t index =
                    i - 1;

                if (
                    points[
                        index
                    ].timestampUnixSeconds <=
                    startTimestamp
                            )
                {
                    firstSampleIndex =
                        index;

                    break;
                }
            }

            if (
                firstSampleIndex ==
                points.size()
                )
            {
                return
                    result;
            }

            std::vector<uint32_t>
                sellSamples;

            uint64_t totalSell = 0;

            for (
                size_t i =
                firstSampleIndex;
                i <
                points.size();
                ++i
                )
            {
                const uint32_t sellPrice =
                    points[
                        i
                    ].sellUnitPrice;

                if (sellPrice == 0)
                {
                    continue;
                }

                sellSamples.push_back(
                    sellPrice
                );

                totalSell +=
                    sellPrice;
            }

            //
            // Two observations are enough to make a useful
            // beginning-to-end assessment. More observations improve
            // the quartile estimate naturally when available.
            //
            if (
                sellSamples.size() < 2
                )
            {
                return
                    result;
            }

            std::sort(
                sellSamples.begin(),
                sellSamples.end()
            );

            result.sampleCount =
                sellSamples.size();

            result.averageSell =
                totalSell /
                static_cast<uint64_t>(
                    sellSamples.size()
                    );

            if (
                sellSamples.size() == 2
                )
            {
                result.lowerQuartileSell =
                    sellSamples.front();

                result.upperQuartileSell =
                    sellSamples.back();
            }
            else
            {
                const size_t lowerIndex =
                    (
                        sellSamples.size() -
                        1
                        ) /
                    4;

                const size_t upperIndex =
                    (
                        (
                            sellSamples.size() -
                            1
                            ) *
                        3 +
                        3
                        ) /
                    4;

                result.lowerQuartileSell =
                    sellSamples[
                        lowerIndex
                    ];

                result.upperQuartileSell =
                    sellSamples[
                        upperIndex
                    ];
            }

            result.available =
                result.averageSell > 0;

            return
                result;
        };



    auto CalculateBuyWindowStats =
        [](
            const std::vector<
            TradingPostHistoryPoint
            >& points,
            uint64_t windowSeconds
            )
        {
            DealWindowStats result;

            if (points.size() < 2)
            {
                return result;
            }

            const uint64_t newestTimestamp =
                points.back().
                timestampUnixSeconds;

            if (newestTimestamp < windowSeconds)
            {
                return result;
            }

            const uint64_t startTimestamp =
                newestTimestamp -
                windowSeconds;

            size_t firstSampleIndex =
                points.size();

            for (
                size_t i = points.size();
                i > 0;
                --i
                )
            {
                const size_t index =
                    i - 1;

                if (
                    points[index].
                    timestampUnixSeconds <=
                    startTimestamp
                    )
                {
                    firstSampleIndex =
                        index;

                    break;
                }
            }

            if (
                firstSampleIndex ==
                points.size()
                )
            {
                return result;
            }

            std::vector<uint32_t>
                buySamples;

            uint64_t totalBuy = 0;

            for (
                size_t i = firstSampleIndex;
                i < points.size();
                ++i
                )
            {
                const uint32_t buyPrice =
                    points[i].
                    buyUnitPrice;

                if (buyPrice == 0)
                {
                    continue;
                }

                buySamples.push_back(
                    buyPrice
                );

                totalBuy +=
                    buyPrice;
            }

            if (buySamples.size() < 2)
            {
                return result;
            }

            std::sort(
                buySamples.begin(),
                buySamples.end()
            );

            result.sampleCount =
                buySamples.size();

            result.averageSell =
                totalBuy /
                static_cast<uint64_t>(
                    buySamples.size()
                    );

            if (buySamples.size() == 2)
            {
                result.lowerQuartileSell =
                    buySamples.front();

                result.upperQuartileSell =
                    buySamples.back();
            }
            else
            {
                const size_t lowerIndex =
                    (
                        buySamples.size() -
                        1
                        ) /
                    4;

                const size_t upperIndex =
                    (
                        (
                            buySamples.size() -
                            1
                            ) *
                        3 +
                        3
                        ) /
                    4;

                result.lowerQuartileSell =
                    buySamples[
                        lowerIndex
                    ];

                result.upperQuartileSell =
                    buySamples[
                        upperIndex
                    ];
            }

            result.available =
                result.averageSell > 0;

            return result;
        };

    auto FormatHistorySpan =
        [](
            uint64_t seconds
            )
        {
            constexpr uint64_t secondsPerMinute =
                60ULL;

            constexpr uint64_t secondsPerHour =
                60ULL * secondsPerMinute;

            constexpr uint64_t secondsPerDay =
                24ULL * secondsPerHour;

            if (seconds >= secondsPerDay)
            {
                const uint64_t days =
                    seconds /
                    secondsPerDay;

                const uint64_t hours =
                    (
                        seconds %
                        secondsPerDay
                        ) /
                    secondsPerHour;

                std::string value =
                    std::to_string(
                        days
                    ) +
                    "d";

                if (hours > 0)
                {
                    value +=
                        " " +
                        std::to_string(
                            hours
                        ) +
                        "h";
                }

                return value;
            }

            if (seconds >= secondsPerHour)
            {
                const uint64_t hours =
                    seconds /
                    secondsPerHour;

                const uint64_t minutes =
                    (
                        seconds %
                        secondsPerHour
                        ) /
                    secondsPerMinute;

                std::string value =
                    std::to_string(
                        hours
                    ) +
                    "h";

                if (minutes > 0)
                {
                    value +=
                        " " +
                        std::to_string(
                            minutes
                        ) +
                        "m";
                }

                return value;
            }

            const uint64_t minutes =
                seconds /
                secondsPerMinute;

            return
                std::to_string(
                    minutes
                ) +
                "m";
        };

    const std::string halfTrendWindowLabel =
        FormatHistorySpan(
            trendWindowSeconds /
            2ULL
        );

    struct WindowCoverageInfo
    {
        uint64_t coveredSeconds = 0;
        double coverage = 0.0;
        size_t sampleCount = 0;
    };

    auto CalculateGapAwareCoverage =
        [](
            const std::vector<
            TradingPostHistoryPoint
            >& points,
            uint64_t windowSeconds
            )
        {
            WindowCoverageInfo result;

            if (
                points.size() < 2 ||
                windowSeconds == 0
                )
            {
                return result;
            }

            const uint64_t newestTimestamp =
                points.back().
                timestampUnixSeconds;

            if (newestTimestamp < windowSeconds)
            {
                return result;
            }

            const uint64_t startTimestamp =
                newestTimestamp -
                windowSeconds;

            //
            // Count samples actually inside the selected analysis window.
            // The older trend-boundary anchor is intentionally excluded.
            //
            for (
                const TradingPostHistoryPoint& point :
                points
                )
            {
                if (
                    point.timestampUnixSeconds >=
                    startTimestamp &&
                    point.timestampUnixSeconds <=
                    newestTimestamp
                    )
                {
                    ++result.sampleCount;
                }
            }

            //
            // Estimate actual observed continuity instead of assuming that
            // oldest-to-newest span means continuous coverage.
            //
            // Older history is intentionally compacted, so tolerated gaps
            // scale with the retention tier:
            //
            // newest 24h:      live cadence, tolerate up to 3 minutes
            // 24h through 7d:  5-minute retention, tolerate up to 15 minutes
            // older than 7d:   30-minute retention, tolerate up to 90 minutes
            //
            // Anything larger is treated as an offline/unobserved gap and
            // contributes only the tolerated portion.
            //
            uint64_t coveredSeconds = 0;

            for (
                size_t i = 1;
                i < points.size();
                ++i
                )
            {
                const TradingPostHistoryPoint& previous =
                    points[i - 1];

                const TradingPostHistoryPoint& current =
                    points[i];

                if (
                    current.timestampUnixSeconds <=
                    previous.timestampUnixSeconds ||
                    current.timestampUnixSeconds <=
                    startTimestamp ||
                    previous.timestampUnixSeconds >=
                    newestTimestamp
                    )
                {
                    continue;
                }

                const uint64_t segmentStart =
                    std::max(
                        previous.timestampUnixSeconds,
                        startTimestamp
                    );

                const uint64_t segmentEnd =
                    std::min(
                        current.timestampUnixSeconds,
                        newestTimestamp
                    );

                if (segmentEnd <= segmentStart)
                {
                    continue;
                }

                const uint64_t segmentSeconds =
                    segmentEnd -
                    segmentStart;

                const uint64_t ageAtSegmentEnd =
                    newestTimestamp >= segmentEnd
                    ? newestTimestamp - segmentEnd
                    : 0;

                uint64_t toleratedGapSeconds =
                    3ULL * 60ULL;

                if (
                    ageAtSegmentEnd >
                    7ULL * 24ULL * 60ULL * 60ULL
                    )
                {
                    toleratedGapSeconds =
                        90ULL * 60ULL;
                }
                else if (
                    ageAtSegmentEnd >
                    24ULL * 60ULL * 60ULL
                    )
                {
                    toleratedGapSeconds =
                        15ULL * 60ULL;
                }

                coveredSeconds +=
                    std::min(
                        segmentSeconds,
                        toleratedGapSeconds
                    );
            }

            result.coveredSeconds =
                std::min(
                    coveredSeconds,
                    windowSeconds
                );

            result.coverage =
                static_cast<double>(
                    result.coveredSeconds
                    ) /
                static_cast<double>(
                    windowSeconds
                    );

            return result;
        };

    auto CalculateCollectorSnapshotCoverage =
        [](
            const std::vector<
            TradingPostHistoryPoint
            >& sampledHistory,
            uint64_t windowSeconds
            )
        {
            WindowCoverageInfo result;

            if (
                sampledHistory.empty() ||
                windowSeconds == 0
                )
            {
                return result;
            }

            constexpr uint64_t
                COLLECTOR_SCAN_INTERVAL_SECONDS =
                5ULL * 60ULL;

            const uint64_t newestTimestamp =
                sampledHistory.back().
                timestampUnixSeconds;

            if (newestTimestamp < windowSeconds)
            {
                return result;
            }

            const uint64_t startTimestamp =
                newestTimestamp -
                windowSeconds;

            for (
                const TradingPostHistoryPoint& point :
                sampledHistory
                )
            {
                if (
                    point.timestampUnixSeconds >=
                    startTimestamp &&
                    point.timestampUnixSeconds <=
                    newestTimestamp
                    )
                {
                    ++result.sampleCount;
                }
            }

            const uint64_t representedSeconds =
                static_cast<uint64_t>(
                    result.sampleCount
                    ) *
                COLLECTOR_SCAN_INTERVAL_SECONDS;

            result.coveredSeconds =
                std::min(
                    representedSeconds,
                    windowSeconds
                );

            result.coverage =
                static_cast<double>(
                    result.coveredSeconds
                    ) /
                static_cast<double>(
                    windowSeconds
                    );

            if (result.coverage > 1.0)
            {
                result.coverage = 1.0;
            }

            return result;
        };

    auto CalculateWindowAverageSell =
        [](
            const std::vector<
            TradingPostHistoryPoint
            >& points,
            uint64_t windowSeconds,
            uint64_t& outAverageSell
            )
        {
            outAverageSell = 0;

            if (points.size() < 2)
            {
                return false;
            }

            const uint64_t newestTimestamp =
                points.back().
                timestampUnixSeconds;

            if (
                newestTimestamp <
                windowSeconds
                )
            {
                return false;
            }

            const uint64_t startTimestamp =
                newestTimestamp -
                windowSeconds;

            uint64_t totalSell = 0;
            uint64_t sampleCount = 0;

            for (
                const TradingPostHistoryPoint&
                point :
                points
                )
            {
                if (
                    point.timestampUnixSeconds <
                    startTimestamp
                    )
                {
                    continue;
                }

                if (
                    point.sellUnitPrice == 0
                    )
                {
                    continue;
                }

                totalSell +=
                    point.sellUnitPrice;

                ++sampleCount;
            }

            if (sampleCount == 0)
            {
                return false;
            }

            outAverageSell =
                totalSell /
                sampleCount;

            return
                outAverageSell > 0;
        };

    enum class VolatilityLevel
    {
        Developing,
        Calm,
        Normal,
        Active,
        Wild
    };

    struct VolatilityInfo
    {
        VolatilityLevel level =
            VolatilityLevel::Developing;

        //
        // Overall market volatility for the selected Trend Window.
        // We calculate sell-side and buy-side RMS scan-to-scan movement
        // separately, then keep whichever side is moving more.
        //
        double movementRmsPercent =
            0.0;

        bool available =
            false;
    };

    auto CalculateVolatility =
        [](
            const std::vector<
            TradingPostHistoryPoint
            >& points,
            uint64_t windowSeconds,
            bool analysisReady
            )
        {
            VolatilityInfo result;

            if (
                !analysisReady ||
                points.size() < 2 ||
                windowSeconds == 0
                )
            {
                return result;
            }

            const uint64_t newestTimestamp =
                points.back().
                timestampUnixSeconds;

            if (newestTimestamp < windowSeconds)
            {
                return result;
            }

            const uint64_t startTimestamp =
                newestTimestamp -
                windowSeconds;

            auto CalculateSideMovementRms =
                [&](
                    bool useSellPrice,
                    double& outRmsPercent
                    )
                {
                    outRmsPercent = 0.0;

                    bool hasPreviousPrice =
                        false;

                    uint32_t previousPrice =
                        0;

                    double squaredPercentMoveTotal =
                        0.0;

                    size_t moveCount =
                        0;

                    for (
                        const TradingPostHistoryPoint& point :
                        points
                        )
                    {
                        if (
                            point.timestampUnixSeconds <
                            startTimestamp ||
                            point.timestampUnixSeconds >
                            newestTimestamp
                            )
                        {
                            continue;
                        }

                        const uint32_t currentPrice =
                            useSellPrice
                            ? point.sellUnitPrice
                            : point.buyUnitPrice;

                        if (currentPrice == 0)
                        {
                            continue;
                        }

                        if (hasPreviousPrice)
                        {
                            const double previous =
                                static_cast<double>(
                                    previousPrice
                                    );

                            const double current =
                                static_cast<double>(
                                    currentPrice
                                    );

                            if (previous > 0.0)
                            {
                                const double percentMove =
                                    (
                                        current -
                                        previous
                                        ) /
                                    previous *
                                    100.0;

                                squaredPercentMoveTotal +=
                                    percentMove *
                                    percentMove;

                                ++moveCount;
                            }
                        }

                        previousPrice =
                            currentPrice;

                        hasPreviousPrice =
                            true;
                    }

                    if (moveCount == 0)
                    {
                        return false;
                    }

                    outRmsPercent =
                        std::sqrt(
                            squaredPercentMoveTotal /
                            static_cast<double>(
                                moveCount
                                )
                        );

                    return true;
                };

            double sellMovementRmsPercent =
                0.0;

            double buyMovementRmsPercent =
                0.0;

            const bool hasSellMovement =
                CalculateSideMovementRms(
                    true,
                    sellMovementRmsPercent
                );

            const bool hasBuyMovement =
                CalculateSideMovementRms(
                    false,
                    buyMovementRmsPercent
                );

            if (
                !hasSellMovement &&
                !hasBuyMovement
                )
            {
                return result;
            }

            if (
                hasSellMovement &&
                hasBuyMovement
                )
            {
                result.movementRmsPercent =
                    std::max(
                        sellMovementRmsPercent,
                        buyMovementRmsPercent
                    );
            }
            else
            {
                result.movementRmsPercent =
                    hasSellMovement
                    ? sellMovementRmsPercent
                    : buyMovementRmsPercent;
            }

            result.available =
                true;

            //
            // A single overall market-volatility badge keeps the UI simple.
            // The more active side of the market controls the label.
            //
            if (
                result.movementRmsPercent <
                0.25
                )
            {
                result.level =
                    VolatilityLevel::Calm;
            }
            else if (
                result.movementRmsPercent <
                0.75
                )
            {
                result.level =
                    VolatilityLevel::Normal;
            }
            else if (
                result.movementRmsPercent <
                2.0
                )
            {
                result.level =
                    VolatilityLevel::Active;
            }
            else
            {
                result.level =
                    VolatilityLevel::Wild;
            }

            return result;
        };

    auto DrawStatusBadge =
        [](
            const char* label,
            const ImVec4& color
            )
        {
            const ImVec2 textSize =
                ImGui::CalcTextSize(
                    label
                );

            const float paddingX =
                7.0f;

            const float paddingY =
                2.0f;

            const ImVec2 start =
                ImGui::GetCursorScreenPos();

            const ImVec2 badgeSize(
                textSize.x +
                paddingX * 2.0f,
                textSize.y +
                paddingY * 2.0f
            );

            ImDrawList* drawList =
                ImGui::GetWindowDrawList();

            const ImVec4 backgroundColor(
                color.x * 0.28f,
                color.y * 0.28f,
                color.z * 0.28f,
                0.95f
            );

            const ImVec4 borderColor(
                color.x,
                color.y,
                color.z,
                0.85f
            );

            drawList->AddRectFilled(
                start,
                ImVec2(
                    start.x +
                    badgeSize.x,
                    start.y +
                    badgeSize.y
                ),
                ImGui::GetColorU32(
                    backgroundColor
                ),
                4.0f
            );

            drawList->AddRect(
                start,
                ImVec2(
                    start.x +
                    badgeSize.x,
                    start.y +
                    badgeSize.y
                ),
                ImGui::GetColorU32(
                    borderColor
                ),
                4.0f,
                0,
                1.0f
            );

            drawList->AddText(
                ImVec2(
                    start.x +
                    paddingX,
                    start.y +
                    paddingY
                ),
                ImGui::GetColorU32(
                    color
                ),
                label
            );

            ImGui::Dummy(
                badgeSize
            );
        };

    auto DrawTrendText =
        [&](
            const char* label,
            const TrendInfo& trend,
            const ImVec4& labelColor,
            const char* windowLabel
            )
        {
            ImGui::TextColored(
                labelColor,
                "%s",
                label
            );

            ImGui::SameLine(
                0.0f,
                5.0f
            );

            if (!trend.available)
            {
                ImGui::TextDisabled(
                    "- collecting %s history",
                    windowLabel
                );

                return;
            }

            const char* arrowText =
                "-";

            ImVec4 stateColor =
                attentionColor;

            if (
                trend.trend ==
                PriceTrend::Up
                )
            {
                arrowText =
                    "^";

                stateColor =
                    goodColor;
            }
            else if (
                trend.trend ==
                PriceTrend::Down
                )
            {
                arrowText =
                    "v";

                stateColor =
                    trendDownColor;
            }

            if (
                trend.trend ==
                PriceTrend::Neutral
                )
            {
                ImGui::TextColored(
                    stateColor,
                    "%s 0c (0.00%%) over %s",
                    arrowText,
                    windowLabel
                );

                return;
            }

            const std::string deltaText =
                FormatTrendCoinDelta(
                    trend.copperChange
                );

            ImGui::TextColored(
                stateColor,
                "%s %s (%+.2f%%) over %s",
                arrowText,
                deltaText.c_str(),
                trend.percentChange,
                windowLabel
            );
        };

    struct SparklinePoint
    {
        uint64_t timestampUnixSeconds = 0;
        float value = 0.0f;
    };

    auto DrawSparkline =
        [&](
            const char* id,
            const std::vector<
            SparklinePoint
            >& values,
            uint64_t rangeStartTimestamp,
            uint64_t rangeEndTimestamp,
            const ImVec4& lineColor,
            const ImVec4& changeColor,
            const char* fullWindowLabel,
            const char* halfWindowLabel,
            const TrendInfo& trend,
            uint64_t currentPriceCopper,
            float width,
            float height
            )
        {
            if (
                values.size() < 2 ||
                rangeEndTimestamp <=
                rangeStartTimestamp ||
                width <= 320.0f ||
                height <= 60.0f
                )
            {
                return;
            }

            //
            // Keep the stored/analysis history untouched. The following
            // smoothing is display-only and removes isolated one-scan spikes
            // that can make dense Collector history look visually broken.
            //
            std::vector<
                SparklinePoint
            > visibleValues;

            visibleValues.reserve(
                values.size()
            );

            for (
                const SparklinePoint& point :
                values
                )
            {
                if (
                    point.timestampUnixSeconds <
                    rangeStartTimestamp ||
                    point.timestampUnixSeconds >
                    rangeEndTimestamp
                    )
                {
                    continue;
                }

                visibleValues.push_back(
                    point
                );
            }

            if (visibleValues.size() < 2)
            {
                return;
            }

            std::vector<
                SparklinePoint
            > displayValues =
                visibleValues;

            if (visibleValues.size() >= 3)
            {
                for (
                    size_t i = 1;
                    i + 1 < visibleValues.size();
                    ++i
                    )
                {
                    float a =
                        visibleValues[
                            i - 1
                        ].value;

                    float b =
                        visibleValues[
                            i
                        ].value;

                    float c =
                        visibleValues[
                            i + 1
                        ].value;

                    if (a > b)
                    {
                        std::swap(
                            a,
                            b
                        );
                    }

                    if (b > c)
                    {
                        std::swap(
                            b,
                            c
                        );
                    }

                    if (a > b)
                    {
                        std::swap(
                            a,
                            b
                        );
                    }

                    displayValues[
                        i
                    ].value =
                        b;
                }
            }

            const float statsWidth =
                145.0f;

            const float statsGap =
                14.0f;

            const float plotTopPadding =
                3.0f;

            const float xLabelHeight =
                19.0f;

            const float plotHeight =
                height -
                xLabelHeight -
                plotTopPadding;

            const ImVec2 start =
                ImGui::
                GetCursorScreenPos();

            ImGui::InvisibleButton(
                id,
                ImVec2(
                    width,
                    height
                )
            );

            ImDrawList* drawList =
                ImGui::
                GetWindowDrawList();

            float minValue =
                displayValues.front().value;

            float maxValue =
                displayValues.front().value;

            for (
                const SparklinePoint& point :
                displayValues
                )
            {
                minValue =
                    std::min(
                        minValue,
                        point.value
                    );

                maxValue =
                    std::max(
                        maxValue,
                        point.value
                    );
            }

            const float rawRange =
                maxValue -
                minValue;

            const float padding =
                rawRange > 0.0f
                ? rawRange * 0.10f
                : (
                    maxValue > 0.0f
                    ? maxValue * 0.005f
                    : 1.0f
                    );

            const float scaleMin =
                minValue -
                padding;

            const float scaleMax =
                maxValue +
                padding;

            const float scaleMid =
                (
                    scaleMin +
                    scaleMax
                    ) *
                0.5f;

            const ImU32 axisColor =
                IM_COL32(
                    128,
                    132,
                    140,
                    120
                );

            const ImU32 guideColor =
                IM_COL32(
                    128,
                    132,
                    140,
                    62
                );

            const ImU32 mutedTextColor =
                IM_COL32(
                    180,
                    183,
                    190,
                    220
                );

            const auto GoldToCopper =
                [](
                    float gold
                    ) -> uint64_t
                {
                    if (gold <= 0.0f)
                    {
                        return 0ULL;
                    }

                    return
                        static_cast<uint64_t>(
                            gold *
                            10000.0f +
                            0.5f
                            );
                };

            const std::string maxPriceLabel =
                FormatCoinValue(
                    GoldToCopper(
                        scaleMax
                    )
                );

            const std::string midPriceLabel =
                FormatCoinValue(
                    GoldToCopper(
                        scaleMid
                    )
                );

            const std::string minPriceLabel =
                FormatCoinValue(
                    GoldToCopper(
                        scaleMin
                    )
                );

            //
            // Size the Y-axis gutter from the actual widest formatted price.
            // This prevents expensive items (for example 1,000g+) from
            // overlapping the chart line.
            //
            const float maxYAxisLabelTextWidth =
                std::max(
                    ImGui::CalcTextSize(
                        maxPriceLabel.c_str()
                    ).x,
                    std::max(
                        ImGui::CalcTextSize(
                            midPriceLabel.c_str()
                        ).x,
                        ImGui::CalcTextSize(
                            minPriceLabel.c_str()
                        ).x
                    )
                );

            const float yLabelWidth =
                maxYAxisLabelTextWidth +
                12.0f;

            const float plotWidth =
                std::max(
                    130.0f,
                    width -
                    yLabelWidth -
                    statsWidth -
                    statsGap
                );

            const float plotLeft =
                start.x +
                yLabelWidth;

            const float plotRight =
                plotLeft +
                plotWidth;

            const float plotTop =
                start.y +
                plotTopPadding;

            const float plotBottom =
                plotTop +
                plotHeight;

            const float plotMidY =
                (
                    plotTop +
                    plotBottom
                    ) *
                0.5f;

            const float plotMidX =
                (
                    plotLeft +
                    plotRight
                    ) *
                0.5f;

            //
            // Axes and lightweight guides.
            //
            drawList->AddLine(
                ImVec2(
                    plotLeft,
                    plotTop
                ),
                ImVec2(
                    plotLeft,
                    plotBottom
                ),
                axisColor,
                1.0f
            );

            drawList->AddLine(
                ImVec2(
                    plotLeft,
                    plotBottom
                ),
                ImVec2(
                    plotRight,
                    plotBottom
                ),
                axisColor,
                1.0f
            );

            drawList->AddLine(
                ImVec2(
                    plotMidX,
                    plotTop
                ),
                ImVec2(
                    plotMidX,
                    plotBottom
                ),
                guideColor,
                1.0f
            );

            drawList->AddLine(
                ImVec2(
                    plotLeft,
                    plotMidY
                ),
                ImVec2(
                    plotRight,
                    plotMidY
                ),
                IM_COL32(
                    128,
                    132,
                    140,
                    34
                ),
                1.0f
            );

            drawList->AddText(
                ImVec2(
                    start.x,
                    plotTop - 2.0f
                ),
                mutedTextColor,
                maxPriceLabel.c_str()
            );

            drawList->AddText(
                ImVec2(
                    start.x,
                    plotMidY -
                    ImGui::GetTextLineHeight() *
                    0.5f
                ),
                mutedTextColor,
                midPriceLabel.c_str()
            );

            drawList->AddText(
                ImVec2(
                    start.x,
                    plotBottom -
                    ImGui::GetTextLineHeight()
                ),
                mutedTextColor,
                minPriceLabel.c_str()
            );

            //
            // If Collector history is denser than the available screen width,
            // average it into horizontal screen-space buckets. This keeps the
            // line readable while retaining the real timestamp spacing.
            //
            const size_t maxDisplayPoints =
                std::max<size_t>(
                    2,
                    static_cast<size_t>(
                        plotWidth
                        )
                );

            if (
                displayValues.size() >
                maxDisplayPoints
                )
            {
                std::vector<
                    SparklinePoint
                > bucketedValues;

                bucketedValues.reserve(
                    maxDisplayPoints
                );

                const double pointsPerBucket =
                    static_cast<double>(
                        displayValues.size()
                        ) /
                    static_cast<double>(
                        maxDisplayPoints
                        );

                for (
                    size_t bucketIndex = 0;
                    bucketIndex <
                    maxDisplayPoints;
                    ++bucketIndex
                    )
                {
                    size_t firstIndex =
                        static_cast<size_t>(
                            static_cast<double>(
                                bucketIndex
                                ) *
                            pointsPerBucket
                            );

                    size_t endIndex =
                        static_cast<size_t>(
                            static_cast<double>(
                                bucketIndex + 1
                                ) *
                            pointsPerBucket
                            );

                    if (
                        endIndex <=
                        firstIndex
                        )
                    {
                        endIndex =
                            firstIndex + 1;
                    }

                    endIndex =
                        std::min(
                            endIndex,
                            displayValues.size()
                        );

                    if (
                        firstIndex >=
                        displayValues.size()
                        )
                    {
                        break;
                    }

                    double valueTotal =
                        0.0;

                    long double timestampTotal =
                        0.0L;

                    size_t count =
                        0;

                    for (
                        size_t i = firstIndex;
                        i < endIndex;
                        ++i
                        )
                    {
                        valueTotal +=
                            static_cast<double>(
                                displayValues[
                                    i
                                ].value
                                );

                        timestampTotal +=
                            static_cast<
                            long double
                            >(
                                displayValues[
                                    i
                                ].
                                timestampUnixSeconds
                                        );

                        ++count;
                    }

                    if (count == 0)
                    {
                        continue;
                    }

                    SparklinePoint point;

                    point.value =
                        static_cast<float>(
                            valueTotal /
                            static_cast<double>(
                                count
                                )
                            );

                    point.timestampUnixSeconds =
                        static_cast<uint64_t>(
                            timestampTotal /
                            static_cast<
                            long double
                            >(
                                count
                                )
                            );

                    bucketedValues.push_back(
                        point
                    );
                }

                displayValues =
                    bucketedValues;
            }

            std::vector<
                ImVec2
            > drawPoints;

            drawPoints.reserve(
                displayValues.size()
            );

            const float valueDenominator =
                scaleMax -
                scaleMin;

            const double timeDenominator =
                static_cast<double>(
                    rangeEndTimestamp -
                    rangeStartTimestamp
                    );

            for (
                const SparklinePoint& point :
                displayValues
                )
            {
                const uint64_t clampedTimestamp =
                    std::min(
                        std::max(
                            point.timestampUnixSeconds,
                            rangeStartTimestamp
                        ),
                        rangeEndTimestamp
                    );

                const double timeFraction =
                    static_cast<double>(
                        clampedTimestamp -
                        rangeStartTimestamp
                        ) /
                    timeDenominator;

                const float x =
                    plotLeft +
                    static_cast<float>(
                        timeFraction
                        ) *
                    plotWidth;

                float normalized =
                    0.5f;

                if (
                    valueDenominator >
                    0.0f
                    )
                {
                    normalized =
                        (
                            point.value -
                            scaleMin
                            ) /
                        valueDenominator;
                }

                const float y =
                    plotBottom -
                    normalized *
                    plotHeight;

                drawPoints.emplace_back(
                    x,
                    y
                );
            }

            const ImVec4 brighterLineColor(
                lineColor.x +
                (
                    1.0f -
                    lineColor.x
                    ) *
                0.18f,
                lineColor.y +
                (
                    1.0f -
                    lineColor.y
                    ) *
                0.18f,
                lineColor.z +
                (
                    1.0f -
                    lineColor.z
                    ) *
                0.18f,
                1.0f
            );

            const ImU32 lineU32 =
                ImGui::
                ColorConvertFloat4ToU32(
                    brighterLineColor
                );

            drawList->AddPolyline(
                drawPoints.data(),
                static_cast<int>(
                    drawPoints.size()
                    ),
                lineU32,
                false,
                2.0f
            );

            if (!drawPoints.empty())
            {
                const ImVec2 latestPoint =
                    drawPoints.back();

                //
                // Keep the newest-price marker visible at all times.
                //
                drawList->AddCircleFilled(
                    latestPoint,
                    3.3f,
                    lineU32
                );

                //
                // Subtle "live price" sparkle:
                // every ~4 seconds, briefly expand a faint ring and glint
                // around the newest-price dot. The effect is short-lived so
                // it draws attention without constantly pulsing.
                //
                const float sparkleCycleSeconds =
                    4.0f;

                const float sparkleDurationSeconds =
                    0.85f;

                const float sparklePhase =
                    std::fmod(
                        static_cast<float>(
                            ImGui::GetTime()
                            ),
                        sparkleCycleSeconds
                    );

                if (
                    sparklePhase <
                    sparkleDurationSeconds
                    )
                {
                    const float sparkleProgress =
                        sparklePhase /
                        sparkleDurationSeconds;

                    const float sparkleFade =
                        1.0f -
                        sparkleProgress;

                    const float ringRadius =
                        4.6f +
                        sparkleProgress *
                        7.0f;

                    const ImU32 ringColor =
                        ImGui::
                        ColorConvertFloat4ToU32(
                            ImVec4(
                                brighterLineColor.x,
                                brighterLineColor.y,
                                brighterLineColor.z,
                                0.62f *
                                sparkleFade
                            )
                        );

                    drawList->AddCircle(
                        latestPoint,
                        ringRadius,
                        ringColor,
                        24,
                        1.4f
                    );

                    //
                    // Small four-point glint that fades faster than the ring.
                    //
                    const float glintFade =
                        sparkleFade *
                        sparkleFade;

                    const float glintLength =
                        3.0f +
                        sparkleProgress *
                        3.0f;

                    const ImU32 glintColor =
                        ImGui::
                        ColorConvertFloat4ToU32(
                            ImVec4(
                                brighterLineColor.x,
                                brighterLineColor.y,
                                brighterLineColor.z,
                                0.90f *
                                glintFade
                            )
                        );

                    drawList->AddLine(
                        ImVec2(
                            latestPoint.x -
                            glintLength,
                            latestPoint.y
                        ),
                        ImVec2(
                            latestPoint.x +
                            glintLength,
                            latestPoint.y
                        ),
                        glintColor,
                        1.0f
                    );

                    drawList->AddLine(
                        ImVec2(
                            latestPoint.x,
                            latestPoint.y -
                            glintLength
                        ),
                        ImVec2(
                            latestPoint.x,
                            latestPoint.y +
                            glintLength
                        ),
                        glintColor,
                        1.0f
                    );
                }
            }

            //
            // X-axis labels: full selected Trend Window, exact half-window,
            // and Now.
            //
            const std::string leftLabel =
                std::string(
                    fullWindowLabel
                ) +
                " ago";

            const std::string middleLabel =
                std::string(
                    halfWindowLabel
                ) +
                " ago";

            const char* rightLabel =
                "Now";

            drawList->AddText(
                ImVec2(
                    plotLeft,
                    plotBottom + 3.0f
                ),
                mutedTextColor,
                leftLabel.c_str()
            );

            const float middleWidth =
                ImGui::CalcTextSize(
                    middleLabel.c_str()
                ).x;

            drawList->AddText(
                ImVec2(
                    plotMidX -
                    middleWidth *
                    0.5f,
                    plotBottom + 3.0f
                ),
                mutedTextColor,
                middleLabel.c_str()
            );

            const float rightWidth =
                ImGui::CalcTextSize(
                    rightLabel
                ).x;

            drawList->AddText(
                ImVec2(
                    plotRight -
                    rightWidth,
                    plotBottom + 3.0f
                ),
                mutedTextColor,
                rightLabel
            );

            //
            // Current price and selected-window change.
            //
            const float statsX =
                plotRight +
                statsGap;

            drawList->AddText(
                ImVec2(
                    statsX,
                    plotTop
                ),
                mutedTextColor,
                "Current Price"
            );

            const std::string currentPriceText =
                FormatCoinValue(
                    currentPriceCopper
                );

            drawList->AddText(
                ImVec2(
                    statsX,
                    plotTop +
                    ImGui::GetTextLineHeight() +
                    1.0f
                ),
                lineU32,
                currentPriceText.c_str()
            );

            const std::string changeHeader =
                "Change (" +
                std::string(
                    fullWindowLabel
                ) +
                ")";

            drawList->AddText(
                ImVec2(
                    statsX,
                    plotTop +
                    ImGui::GetTextLineHeight() *
                    2.40f
                ),
                mutedTextColor,
                changeHeader.c_str()
            );

            const ImU32 changeU32 =
                ImGui::
                ColorConvertFloat4ToU32(
                    changeColor
                );

            if (trend.available)
            {
                const std::string deltaText =
                    FormatTrendCoinDelta(
                        trend.copperChange
                    );

                char percentText[32] = {};

                std::snprintf(
                    percentText,
                    sizeof(
                        percentText
                        ),
                    "%+.2f%%",
                    trend.percentChange
                );

                drawList->AddText(
                    ImVec2(
                        statsX,
                        plotTop +
                        ImGui::GetTextLineHeight() *
                        3.40f
                    ),
                    changeU32,
                    deltaText.c_str()
                );

                drawList->AddText(
                    ImVec2(
                        statsX,
                        plotTop +
                        ImGui::GetTextLineHeight() *
                        4.40f
                    ),
                    changeU32,
                    percentText
                );
            }
            else
            {
                drawList->AddText(
                    ImVec2(
                        statsX,
                        plotTop +
                        ImGui::GetTextLineHeight() *
                        3.40f
                    ),
                    mutedTextColor,
                    "Developing"
                );
            }
        };

    ImGui::TextUnformatted(
        "Copper&Claw"
    );

    ImGui::TextDisabled(
        "Watch sell listings and buy orders, and alert when either target is reached."
    );

    TradingPostTargetAlert activeTargetAlert;

    const bool hasActiveTargetAlert =
        TradingPostWatchManager::
        TryGetActiveTargetAlert(
            activeTargetAlert
        );

    if (hasActiveTargetAlert)
    {
        ImGui::Spacing();

        const bool isBuyTargetAlert =
            activeTargetAlert.type ==
            TradingPostTargetAlertType::Buy;

        ImGui::PushStyleVar(
            ImGuiStyleVar_ChildRounding,
            5.0f
        );

        ImGui::PushStyleVar(
            ImGuiStyleVar_WindowPadding,
            ImVec2(
                10.0f,
                8.0f
            )
        );

        ImGui::PushStyleColor(
            ImGuiCol_ChildBg,
            isBuyTargetAlert
            ? ImVec4(
                0.04f,
                0.07f,
                0.12f,
                0.92f
            )
            : ImVec4(
                0.07f,
                0.10f,
                0.04f,
                0.92f
            )
        );

        ImGui::PushStyleColor(
            ImGuiCol_Border,
            isBuyTargetAlert
            ? ImVec4(
                0.35f,
                0.68f,
                1.00f,
                1.00f
            )
            : ImVec4(
                0.68f,
                0.80f,
                0.14f,
                1.00f
            )
        );

        ImGui::BeginChild(
            "##TradingPostTargetCompactAlert",
            ImVec2(
                430.0f,
                64.0f
            ),
            true,
            ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoScrollWithMouse
        );

        ImGui::TextColored(
            attentionColor,
            "TARGET PRICE HIT!"
        );

        ImGui::SameLine(
            0.0f,
            8.0f
        );

        ImGui::TextColored(
            itemNameColor,
            "%s",
            activeTargetAlert.name.c_str()
        );

        const uint64_t activePriceCopper =
            isBuyTargetAlert
            ? activeTargetAlert.buyUnitPrice
            : activeTargetAlert.sellUnitPrice;

        const uint64_t activeTargetCopper =
            isBuyTargetAlert
            ? activeTargetAlert.targetBuyCopper
            : activeTargetAlert.targetSellCopper;

        const std::string activePriceText =
            FormatCoinValue(
                activePriceCopper
            );

        const std::string targetText =
            FormatCoinValue(
                activeTargetCopper
            );

        ImGui::TextColored(
            isBuyTargetAlert
            ? buyColor
            : sellColor,
            "%s: %s",
            isBuyTargetAlert
            ? "Buy"
            : "Sell",
            activePriceText.c_str()
        );

        ImGui::SameLine(
            0.0f,
            12.0f
        );

        ImGui::TextDisabled(
            "Target: %s",
            targetText.c_str()
        );

        ImGui::SameLine(
            0.0f,
            12.0f
        );

        if (
            ImGui::SmallButton(
                "Dismiss"
            )
            )
        {
            TradingPostWatchManager::
                DismissActiveTargetAlert();
        }

        ImGui::EndChild();

        ImGui::PopStyleColor(
            2
        );

        ImGui::PopStyleVar(
            2
        );

        ImGui::Spacing();
    }

    ImGui::Spacing();

    bool autoWatch =
        TradingPostWatchManager::
        IsAutoWatchEnabled();

    if (
        ImGui::Checkbox(
            "Auto watch prices",
            &autoWatch
        )
        )
    {
        TradingPostWatchManager::
            SetAutoWatchEnabled(
                autoWatch
            );
    }

    ImGui::SameLine();

    if (
        ImGui::Button(
            "Refresh All"
        )
        )
    {
        TradingPostWatchManager::
            RefreshAll();
    }

    ImGui::SameLine();

    const size_t indexItemCount =
        TradingPostItemIndexManager::
        GetItemCount();

    if (indexItemCount > 0)
    {
        ImGui::TextDisabled(
            "%llu items searchable",
            static_cast<
            unsigned long long
            >(
                indexItemCount
                )
        );
    }

    const int64_t topNextCheckSeconds =
        TradingPostWatchManager::
        GetSecondsUntilNextCheck();

    if (
        TradingPostWatchManager::
        IsAutoWatchEnabled()
        )
    {
        ImGui::Spacing();

        ImGui::TextColored(
            ImVec4(
                1.00f,
                0.72f,
                0.18f,
                1.00f
            ),
            "NEXT API REFRESH: %lld SEC",
            topNextCheckSeconds
        );
    }

    //
    // Collector status uses state Copper&Claw already maintains.
    // This first pass is display-only and does not change any price,
    // history, refresh, or Market Analysis behavior.
    //
    const bool collectorConnected =
        TradingPostPriceManager::
        IsCollectorAvailable();

    const std::vector<uint64_t>
        collectorCompletedSnapshotTimestamps =
        TradingPostHistoryManager::
        GetCollectorCompletedSnapshotTimestamps();

    const bool useCollectorAnalysisHistory =
        collectorConnected &&
        !collectorCompletedSnapshotTimestamps.empty();

    const uint64_t lastCollectorSuccessUnixSeconds =
        TradingPostPriceManager::
        GetLastCollectorSuccessUnixSeconds();

    uint64_t collectorContactAgeSeconds = 0;
    uint64_t collectorLastScanAgeSeconds = 0;

    const uint64_t nowUnixSeconds =
        static_cast<uint64_t>(
            std::chrono::duration_cast<
            std::chrono::seconds
            >(
                std::chrono::system_clock::
                now().time_since_epoch()
            ).count()
            );

    if (lastCollectorSuccessUnixSeconds > 0)
    {
        collectorContactAgeSeconds =
            nowUnixSeconds >=
            lastCollectorSuccessUnixSeconds
            ? nowUnixSeconds -
            lastCollectorSuccessUnixSeconds
            : 0;
    }

    uint64_t lastCollectorCompletedScanUnixSeconds = 0;

    if (!collectorCompletedSnapshotTimestamps.empty())
    {
        lastCollectorCompletedScanUnixSeconds =
            collectorCompletedSnapshotTimestamps.back();

        collectorLastScanAgeSeconds =
            nowUnixSeconds >=
            lastCollectorCompletedScanUnixSeconds
            ? nowUnixSeconds -
            lastCollectorCompletedScanUnixSeconds
            : 0;
    }

    ImGui::Spacing();

    ImGui::TextDisabled(
        "Collector:"
    );

    ImGui::SameLine(
        0.0f,
        4.0f
    );

    ImGui::TextColored(
        collectorConnected
        ? goodColor
        : trendDownColor,
        "%s",
        collectorConnected
        ? "Connected"
        : "Offline"
    );

    ImGui::SameLine(
        0.0f,
        14.0f
    );

    ImGui::TextDisabled(
        "| Price:"
    );

    ImGui::SameLine(
        0.0f,
        4.0f
    );

    ImGui::TextColored(
        collectorConnected
        ? goodColor
        : attentionColor,
        "%s",
        collectorConnected
        ? "Collector"
        : "GW2 API"
    );

    ImGui::SameLine(
        0.0f,
        14.0f
    );

    ImGui::TextDisabled(
        "| Analysis:"
    );

    ImGui::SameLine(
        0.0f,
        4.0f
    );

    ImGui::TextColored(
        useCollectorAnalysisHistory
        ? buyColor
        : mutedColor,
        "%s",
        useCollectorAnalysisHistory
        ? "Collector Snapshots"
        : "Local History"
    );

    ImGui::SameLine(
        0.0f,
        14.0f
    );

    ImGui::TextDisabled(
        "| Contact:"
    );

    ImGui::SameLine(
        0.0f,
        4.0f
    );

    if (lastCollectorSuccessUnixSeconds > 0)
    {
        ImGui::TextDisabled(
            "%llus ago",
            static_cast<unsigned long long>(
                collectorContactAgeSeconds
                )
        );
    }
    else
    {
        ImGui::TextDisabled(
            "--"
        );
    }

    ImGui::SameLine(
        0.0f,
        14.0f
    );

    ImGui::TextDisabled(
        "| Scan:"
    );

    ImGui::SameLine(
        0.0f,
        4.0f
    );

    if (lastCollectorCompletedScanUnixSeconds > 0)
    {
        ImVec4 scanAgeColor =
            mutedColor;

        if (collectorLastScanAgeSeconds >= 15ULL * 60ULL)
        {
            scanAgeColor =
                trendDownColor;
        }
        else if (collectorLastScanAgeSeconds >= 10ULL * 60ULL)
        {
            scanAgeColor =
                attentionColor;
        }

        if (collectorLastScanAgeSeconds < 60ULL)
        {
            ImGui::TextColored(
                scanAgeColor,
                "%llus ago",
                static_cast<unsigned long long>(
                    collectorLastScanAgeSeconds
                    )
            );
        }
        else
        {
            ImGui::TextColored(
                scanAgeColor,
                "%llum ago",
                static_cast<unsigned long long>(
                    collectorLastScanAgeSeconds / 60ULL
                    )
            );
        }
    }
    else
    {
        ImGui::TextDisabled(
            "--"
        );
    }

    ImGui::Spacing();

    ImGui::TextDisabled("Trend Window:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(90.0f);

    if (ImGui::Combo(
        "##TrendWindow",
        &g_Settings.tradingPostTrendWindowIndex,
        trendWindowLabels,
        static_cast<int>(sizeof(trendWindowLabels) / sizeof(trendWindowLabels[0]))))
    {
        Settings::Save(moduleHandle);
    }

    ImGui::SameLine(0.0f, 14.0f);
    ImGui::TextDisabled("Sort:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(145.0f);

    if (ImGui::Combo(
        "##SortWatchList",
        &g_Settings.tradingPostWatchSortMode,
        watchSortLabels,
        static_cast<int>(sizeof(watchSortLabels) / sizeof(watchSortLabels[0]))))
    {
        Settings::Save(moduleHandle);
    }

    ImGui::Spacing();

    ImGui::TextDisabled("View:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(100.0f);

    if (ImGui::Combo(
        "##ViewDensity",
        &g_Settings.tradingPostViewDensityIndex,
        viewDensityLabels,
        static_cast<int>(sizeof(viewDensityLabels) / sizeof(viewDensityLabels[0]))))
    {
        Settings::Save(moduleHandle);
    }

    ImGui::SameLine(0.0f, 14.0f);
    ImGui::TextDisabled("Show:");
    ImGui::SameLine();

    static bool showFavoritesOnly = false;

    if (ImGui::RadioButton("All Items", !showFavoritesOnly))
    {
        showFavoritesOnly = false;
    }

    ImGui::SameLine(0.0f, 10.0f);

    if (ImGui::RadioButton("Favorites", showFavoritesOnly))
    {
        showFavoritesOnly = true;
    }

    ImGui::Spacing();

    ImGui::TextDisabled(
        "Historical note: Copper&Claw uses Collector scan history when available and local history as fallback."
    );

    ImGui::TextDisabled(
        "Collector-backed history may include observations from before an item was added to this watch list."
    );

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    std::vector<
        TradingPostWatchItem
    > watchedItems =
        TradingPostWatchManager::
        GetItems();

    //
    // Sorting is intentionally display-only. GetItems() returns a copy,
    // so the user's persisted watch-list order is never rewritten.
    //
    struct WatchSortMetric
    {
        uint32_t itemID = 0;

        bool hasTargetDistance = false;
        double targetDistanceRatio = 0.0;

        bool hasSellTrend = false;
        double sellTrendPercent = 0.0;

        bool hasVolatility = false;
        double volatilityPercent = 0.0;
    };

    std::vector<WatchSortMetric>
        watchSortMetrics;

    if (
        g_Settings.tradingPostWatchSortMode >= 2 &&
        !watchedItems.empty()
        )
    {
        watchSortMetrics.reserve(
            watchedItems.size()
        );

        for (
            const TradingPostWatchItem& sortItem :
            watchedItems
            )
        {
            WatchSortMetric metric;
            metric.itemID =
                sortItem.itemID;

            TradingPostPrice sortPrice;

            const bool hasSortPrice =
                TradingPostPriceManager::
                TryGetPrice(
                    sortItem.itemID,
                    sortPrice
                );

            if (
                hasSortPrice &&
                sortPrice.available
                )
            {
                bool hasAnyTargetDistance =
                    false;

                double closestTargetDistanceRatio =
                    0.0;

                //
                // Sell Target:
                // reached when the current sell listing is at or below
                // the user's target. Otherwise measure how far above the
                // target the listing still is.
                //
                if (
                    sortItem.targetSellCopper > 0 &&
                    sortPrice.sellUnitPrice > 0
                    )
                {
                    const double targetSell =
                        static_cast<double>(
                            sortItem.targetSellCopper
                            );

                    const double currentSell =
                        static_cast<double>(
                            sortPrice.sellUnitPrice
                            );

                    const double sellDistanceRatio =
                        currentSell <= targetSell
                        ? 0.0
                        : (
                            currentSell -
                            targetSell
                            ) /
                        targetSell;

                    closestTargetDistanceRatio =
                        sellDistanceRatio;

                    hasAnyTargetDistance =
                        true;
                }

                //
                // Buy Target:
                // reached when the current buy order is at or above
                // the user's target. Otherwise measure how far below the
                // target the current buy order still is.
                //
                if (
                    sortItem.targetBuyCopper > 0 &&
                    sortPrice.buyUnitPrice > 0
                    )
                {
                    const double targetBuy =
                        static_cast<double>(
                            sortItem.targetBuyCopper
                            );

                    const double currentBuy =
                        static_cast<double>(
                            sortPrice.buyUnitPrice
                            );

                    const double buyDistanceRatio =
                        currentBuy >= targetBuy
                        ? 0.0
                        : (
                            targetBuy -
                            currentBuy
                            ) /
                        targetBuy;

                    if (
                        !hasAnyTargetDistance ||
                        buyDistanceRatio <
                        closestTargetDistanceRatio
                        )
                    {
                        closestTargetDistanceRatio =
                            buyDistanceRatio;
                    }

                    hasAnyTargetDistance =
                        true;
                }

                metric.hasTargetDistance =
                    hasAnyTargetDistance;

                metric.targetDistanceRatio =
                    closestTargetDistanceRatio;
            }

            if (
                g_Settings.tradingPostWatchSortMode ==
                3 ||
                g_Settings.tradingPostWatchSortMode ==
                4 ||
                g_Settings.tradingPostWatchSortMode ==
                5 ||
                g_Settings.tradingPostWatchSortMode ==
                6
                )
            {
                const std::vector<
                    TradingPostHistoryPoint
                > sortHistory =
                    TradingPostHistoryManager::
                    GetHistory(
                        sortItem.itemID
                    );

                const std::vector<
                    TradingPostHistoryPoint
                > sortAnalysisHistory =
                    useCollectorAnalysisHistory
                    ? BuildCollectorSampledHistory(
                        sortHistory,
                        collectorCompletedSnapshotTimestamps
                    )
                    : sortHistory;

                if (
                    g_Settings.tradingPostWatchSortMode ==
                    3 ||
                    g_Settings.tradingPostWatchSortMode ==
                    4
                    )
                {
                    const TrendInfo sortSellTrend =
                        CalculateTrend(
                            sortAnalysisHistory,
                            true,
                            trendWindowSeconds
                        );

                    metric.hasSellTrend =
                        sortSellTrend.available;

                    metric.sellTrendPercent =
                        sortSellTrend.percentChange;
                }
                else
                {
                    const WindowCoverageInfo sortWindowCoverage =
                        (
                            useCollectorAnalysisHistory
                            ? CalculateCollectorSnapshotCoverage(
                                sortAnalysisHistory,
                                trendWindowSeconds
                            )
                            : CalculateGapAwareCoverage(
                                sortAnalysisHistory,
                                trendWindowSeconds
                            )
                            );

                    size_t sortMinimumAnalysisSamples =
                        15;

                    if (useCollectorAnalysisHistory)
                    {
                        constexpr uint64_t
                            COLLECTOR_SCAN_INTERVAL_SECONDS =
                            5ULL * 60ULL;

                        const size_t expectedWindowSamples =
                            static_cast<size_t>(
                                trendWindowSeconds /
                                COLLECTOR_SCAN_INTERVAL_SECONDS
                                ) +
                            1;

                        const size_t seventyFivePercentSamples =
                            (
                                expectedWindowSamples *
                                3 +
                                3
                                ) /
                            4;

                        sortMinimumAnalysisSamples =
                            std::min<size_t>(
                                15,
                                std::max<size_t>(
                                    3,
                                    seventyFivePercentSamples
                                )
                            );
                    }

                    const bool sortVolatilityReady =
                        sortWindowCoverage.sampleCount >=
                        sortMinimumAnalysisSamples &&
                        sortWindowCoverage.coverage >=
                        0.75;

                    const VolatilityInfo sortVolatility =
                        CalculateVolatility(
                            sortAnalysisHistory,
                            trendWindowSeconds,
                            sortVolatilityReady
                        );

                    metric.hasVolatility =
                        sortVolatility.available;

                    metric.volatilityPercent =
                        sortVolatility.movementRmsPercent;
                }
            }

            watchSortMetrics.push_back(
                metric
            );
        }
    }

    auto FindWatchSortMetric =
        [&watchSortMetrics](
            uint32_t itemID
            ) ->
        const WatchSortMetric*
        {
            for (
                const WatchSortMetric& metric :
                watchSortMetrics
                )
            {
                if (metric.itemID == itemID)
                {
                    return &metric;
                }
            }

            return nullptr;
        };

    switch (
        g_Settings.tradingPostWatchSortMode
        )
    {
    case 1:
        std::stable_sort(
            watchedItems.begin(),
            watchedItems.end(),
            [](
                const TradingPostWatchItem& a,
                const TradingPostWatchItem& b
                )
            {
                return
                    a.name <
                    b.name;
            }
        );
        break;

    case 2:
        std::stable_sort(
            watchedItems.begin(),
            watchedItems.end(),
            [&FindWatchSortMetric](
                const TradingPostWatchItem& a,
                const TradingPostWatchItem& b
                )
            {
                const WatchSortMetric* metricA =
                    FindWatchSortMetric(
                        a.itemID
                    );

                const WatchSortMetric* metricB =
                    FindWatchSortMetric(
                        b.itemID
                    );

                const bool hasA =
                    metricA != nullptr &&
                    metricA->hasTargetDistance;

                const bool hasB =
                    metricB != nullptr &&
                    metricB->hasTargetDistance;

                if (hasA != hasB)
                {
                    return hasA;
                }

                if (!hasA)
                {
                    return false;
                }

                if (
                    metricA->targetDistanceRatio !=
                    metricB->targetDistanceRatio
                    )
                {
                    return
                        metricA->targetDistanceRatio <
                        metricB->targetDistanceRatio;
                }

                return
                    a.name <
                    b.name;
            }
        );
        break;

    case 3:
        std::stable_sort(
            watchedItems.begin(),
            watchedItems.end(),
            [&FindWatchSortMetric](
                const TradingPostWatchItem& a,
                const TradingPostWatchItem& b
                )
            {
                const WatchSortMetric* metricA =
                    FindWatchSortMetric(
                        a.itemID
                    );

                const WatchSortMetric* metricB =
                    FindWatchSortMetric(
                        b.itemID
                    );

                const bool hasA =
                    metricA != nullptr &&
                    metricA->hasSellTrend;

                const bool hasB =
                    metricB != nullptr &&
                    metricB->hasSellTrend;

                if (hasA != hasB)
                {
                    return hasA;
                }

                if (!hasA)
                {
                    return false;
                }

                if (
                    metricA->sellTrendPercent !=
                    metricB->sellTrendPercent
                    )
                {
                    return
                        metricA->sellTrendPercent <
                        metricB->sellTrendPercent;
                }

                return
                    a.name <
                    b.name;
            }
        );
        break;

    case 4:
        std::stable_sort(
            watchedItems.begin(),
            watchedItems.end(),
            [&FindWatchSortMetric](
                const TradingPostWatchItem& a,
                const TradingPostWatchItem& b
                )
            {
                const WatchSortMetric* metricA =
                    FindWatchSortMetric(
                        a.itemID
                    );

                const WatchSortMetric* metricB =
                    FindWatchSortMetric(
                        b.itemID
                    );

                const bool hasA =
                    metricA != nullptr &&
                    metricA->hasSellTrend;

                const bool hasB =
                    metricB != nullptr &&
                    metricB->hasSellTrend;

                if (hasA != hasB)
                {
                    return hasA;
                }

                if (!hasA)
                {
                    return false;
                }

                if (
                    metricA->sellTrendPercent !=
                    metricB->sellTrendPercent
                    )
                {
                    return
                        metricA->sellTrendPercent >
                        metricB->sellTrendPercent;
                }

                return
                    a.name <
                    b.name;
            }
        );
        break;

    case 5:
        std::stable_sort(
            watchedItems.begin(),
            watchedItems.end(),
            [&FindWatchSortMetric](
                const TradingPostWatchItem& a,
                const TradingPostWatchItem& b
                )
            {
                const WatchSortMetric* metricA =
                    FindWatchSortMetric(
                        a.itemID
                    );

                const WatchSortMetric* metricB =
                    FindWatchSortMetric(
                        b.itemID
                    );

                const bool hasA =
                    metricA != nullptr &&
                    metricA->hasVolatility;

                const bool hasB =
                    metricB != nullptr &&
                    metricB->hasVolatility;

                if (hasA != hasB)
                {
                    return hasA;
                }

                if (!hasA)
                {
                    return false;
                }

                if (
                    metricA->volatilityPercent !=
                    metricB->volatilityPercent
                    )
                {
                    return
                        metricA->volatilityPercent >
                        metricB->volatilityPercent;
                }

                return
                    a.name <
                    b.name;
            }
        );
        break;

    case 6:
        std::stable_sort(
            watchedItems.begin(),
            watchedItems.end(),
            [&FindWatchSortMetric](
                const TradingPostWatchItem& a,
                const TradingPostWatchItem& b
                )
            {
                const WatchSortMetric* metricA =
                    FindWatchSortMetric(
                        a.itemID
                    );

                const WatchSortMetric* metricB =
                    FindWatchSortMetric(
                        b.itemID
                    );

                const bool hasA =
                    metricA != nullptr &&
                    metricA->hasVolatility;

                const bool hasB =
                    metricB != nullptr &&
                    metricB->hasVolatility;

                if (hasA != hasB)
                {
                    return hasA;
                }

                if (!hasA)
                {
                    return false;
                }

                if (
                    metricA->volatilityPercent !=
                    metricB->volatilityPercent
                    )
                {
                    return
                        metricA->volatilityPercent <
                        metricB->volatilityPercent;
                }

                return
                    a.name <
                    b.name;
            }
        );
        break;

    default:
        // Added Order: leave the display copy exactly as returned.
        break;
    }

    if (watchedItems.empty())
    {
        ImGui::TextDisabled(
            "No watched items."
        );
    }
    else if (showFavoritesOnly)
    {
        const bool hasFavorite =
            std::any_of(
                watchedItems.begin(),
                watchedItems.end(),
                [](
                    const TradingPostWatchItem& item
                    )
                {
                    return
                        item.isFavorite;
                }
            );

        if (!hasFavorite)
        {
            ImGui::TextDisabled(
                "No favorites yet. Switch to All Items and click the star beside an item."
            );
        }
    }

    auto RenderWatchItem =
        [&](
            const TradingPostWatchItem& item
            )
        {
            ImGui::PushID(
                static_cast<int>(
                    item.itemID
                    )
            );

            auto RenderFavoriteButton =
                [&]()
                {
                    const float buttonSize =
                        18.0f;

                    const ImVec2 buttonPos =
                        ImGui::GetCursorScreenPos();

                    ImGui::InvisibleButton(
                        "##Favorite",
                        ImVec2(
                            buttonSize,
                            buttonSize
                        )
                    );

                    const bool hovered =
                        ImGui::IsItemHovered();

                    if (ImGui::IsItemClicked())
                    {
                        TradingPostWatchManager::
                            SetFavorite(
                                item.itemID,
                                !item.isFavorite
                            );
                    }

                    ImDrawList* drawList =
                        ImGui::GetWindowDrawList();

                    const ImVec2 center(
                        buttonPos.x +
                        buttonSize * 0.5f,
                        buttonPos.y +
                        buttonSize * 0.5f
                    );

                    const float outerRadius =
                        6.5f;

                    const float innerRadius =
                        3.0f;

                    ImVec2 starPoints[10];

                    constexpr float pi =
                        3.14159265358979323846f;

                    for (
                        int pointIndex = 0;
                        pointIndex < 10;
                        ++pointIndex
                        )
                    {
                        const float radius =
                            pointIndex % 2 == 0
                            ? outerRadius
                            : innerRadius;

                        const float angle =
                            -pi * 0.5f +
                            static_cast<float>(
                                pointIndex
                                ) *
                            pi /
                            5.0f;

                        starPoints[
                            pointIndex
                        ] =
                            ImVec2(
                                center.x +
                                std::cos(
                                    angle
                                ) *
                                radius,
                                center.y +
                                std::sin(
                                    angle
                                ) *
                                radius
                            );
                    }

                    if (item.isFavorite)
                    {
                        const ImU32 favoriteFillColor =
                            IM_COL32(
                                255,
                                190,
                                32,
                                255
                            );

                        for (
                            int pointIndex = 0;
                            pointIndex < 10;
                            ++pointIndex
                            )
                        {
                            const int nextPointIndex =
                                (
                                    pointIndex + 1
                                    ) %
                                10;

                            drawList->AddTriangleFilled(
                                center,
                                starPoints[
                                    pointIndex
                                ],
                                starPoints[
                                    nextPointIndex
                                ],
                                favoriteFillColor
                            );
                        }

                        drawList->AddPolyline(
                            starPoints,
                            10,
                            IM_COL32(
                                255,
                                222,
                                92,
                                255
                            ),
                            true,
                            1.0f
                        );
                    }
                    else
                    {
                        drawList->AddPolyline(
                            starPoints,
                            10,
                            hovered
                            ? IM_COL32(
                                220,
                                220,
                                220,
                                255
                            )
                            : IM_COL32(
                                145,
                                145,
                                150,
                                255
                            ),
                            true,
                            1.4f
                        );
                    }

                    if (hovered)
                    {
                        ImGui::SetTooltip(
                            item.isFavorite
                            ? "Remove from Favorites"
                            : "Add to Favorites"
                        );
                    }

                    ImGui::SameLine(
                        0.0f,
                        5.0f
                    );
                };

            TradingPostPrice price;

            const bool hasPrice =
                TradingPostPriceManager::
                TryGetPrice(
                    item.itemID,
                    price
                );

            const std::vector<
                TradingPostHistoryPoint
            > history =
                TradingPostHistoryManager::
                GetHistory(
                    item.itemID
                );

            const std::vector<
                TradingPostHistoryPoint
            > analysisHistory =
                useCollectorAnalysisHistory
                ? BuildCollectorSampledHistory(
                    history,
                    collectorCompletedSnapshotTimestamps
                )
                : history;

            const TrendInfo sellTrend =
                CalculateTrend(
                    analysisHistory,
                    true,
                    trendWindowSeconds
                );

            const TrendInfo buyTrend =
                CalculateTrend(
                    analysisHistory,
                    false,
                    trendWindowSeconds
                );

            std::string sellText =
                "Loading...";

            std::string buyText =
                "Loading...";

            uint64_t ageSeconds = 0;

            if (hasPrice)
            {
                sellText =
                    FormatCoinValue(
                        price.sellUnitPrice
                    );

                buyText =
                    FormatCoinValue(
                        price.buyUnitPrice
                    );

                if (
                    price.lastUpdatedUnixSeconds >
                    0
                    )
                {
                    const uint64_t nowUnixSeconds =
                        static_cast<uint64_t>(
                            std::chrono::
                            duration_cast<
                            std::chrono::seconds
                            >(
                                std::chrono::
                                system_clock::
                                now().
                                time_since_epoch()
                            ).count()
                            );

                    ageSeconds =
                        nowUnixSeconds >=
                        price.lastUpdatedUnixSeconds
                        ? nowUnixSeconds -
                        price.lastUpdatedUnixSeconds
                        : 0;
                }
            }
            else
            {
                TradingPostPriceManager::
                    RequestPrice(
                        item.itemID
                    );
            }

            uint64_t targetCopper =
                item.targetSellCopper;

            int targetGold =
                static_cast<int>(
                    targetCopper /
                    10000ULL
                    );

            int targetSilver =
                static_cast<int>(
                    (
                        targetCopper %
                        10000ULL
                        ) /
                    100ULL
                    );

            int targetCopperOnly =
                static_cast<int>(
                    targetCopper %
                    100ULL
                    );

            bool targetChanged =
                false;

            uint64_t buyTargetCopper =
                item.targetBuyCopper;

            int buyTargetGold =
                static_cast<int>(
                    buyTargetCopper /
                    10000ULL
                    );

            int buyTargetSilver =
                static_cast<int>(
                    (
                        buyTargetCopper %
                        10000ULL
                        ) /
                    100ULL
                    );

            int buyTargetCopperOnly =
                static_cast<int>(
                    buyTargetCopper %
                    100ULL
                    );

            bool buyTargetChanged =
                false;

            const bool targetReached =
                item.targetSellCopper > 0 &&
                hasPrice &&
                price.available &&
                price.sellUnitPrice > 0 &&
                static_cast<uint64_t>(
                    price.sellUnitPrice
                    ) <=
                item.targetSellCopper;

            const bool buyTargetReached =
                item.targetBuyCopper > 0 &&
                hasPrice &&
                price.available &&
                price.buyUnitPrice > 0 &&
                static_cast<uint64_t>(
                    price.buyUnitPrice
                    ) >=
                item.targetBuyCopper;

            std::string targetSummary =
                "TARGET: -";

            if (item.targetSellCopper > 0)
            {
                targetSummary =
                    "TARGET: " +
                    FormatCoinValue(
                        item.targetSellCopper
                    );
            }

            const std::string sellTargetValueText =
                item.targetSellCopper > 0
                ? FormatCoinValue(
                    item.targetSellCopper
                )
                : "-";

            const std::string buyTargetValueText =
                item.targetBuyCopper > 0
                ? FormatCoinValue(
                    item.targetBuyCopper
                )
                : "-";

            const bool activeSellTargetForItem =
                hasActiveTargetAlert &&
                activeTargetAlert.itemID ==
                item.itemID &&
                activeTargetAlert.type ==
                TradingPostTargetAlertType::Sell;

            const bool activeBuyTargetForItem =
                hasActiveTargetAlert &&
                activeTargetAlert.itemID ==
                item.itemID &&
                activeTargetAlert.type ==
                TradingPostTargetAlertType::Buy;

            const float targetPulse =
                0.5f +
                0.5f *
                std::sin(
                    static_cast<float>(
                        ImGui::GetTime()
                        ) *
                    5.0f
                );

            bool openEditTargetsPopup =
                false;

            const bool normalView =
                g_Settings.tradingPostViewDensityIndex == 0;

            const bool ultraSlimView =
                g_Settings.tradingPostViewDensityIndex == 2;

            const ImVec2 rowCellPadding =
                normalView
                ? ImVec2(
                    8.0f,
                    7.0f
                )
                : (
                    ultraSlimView
                    ? ImVec2(
                        4.0f,
                        2.0f
                    )
                    : ImVec2(
                        6.0f,
                        4.0f
                    )
                    );

            ImGui::PushStyleVar(
                ImGuiStyleVar_CellPadding,
                rowCellPadding
            );

            if (ultraSlimView)
            {
                if (
                    ImGui::BeginTable(
                        "##UltraSlimWatchItem",
                        4,
                        ImGuiTableFlags_SizingStretchProp |
                        ImGuiTableFlags_NoSavedSettings
                    )
                    )
                {
                    ImGui::TableSetupColumn(
                        "Item",
                        ImGuiTableColumnFlags_WidthStretch,
                        2.40f
                    );

                    ImGui::TableSetupColumn(
                        "Sell",
                        ImGuiTableColumnFlags_WidthFixed,
                        112.0f
                    );

                    ImGui::TableSetupColumn(
                        "Buy",
                        ImGuiTableColumnFlags_WidthFixed,
                        112.0f
                    );

                    ImGui::TableSetupColumn(
                        "Actions",
                        ImGuiTableColumnFlags_WidthFixed,
                        248.0f
                    );

                    ImGui::TableNextRow();

                    ImGui::TableSetColumnIndex(
                        0
                    );

                    RenderFavoriteButton();

                    Texture_t* itemIcon =
                        GetTradingPostItemIcon(
                            item.itemID
                        );

                    if (
                        itemIcon != nullptr &&
                        itemIcon->Resource != nullptr
                        )
                    {
                        ImGui::Image(
                            (ImTextureID)
                            itemIcon->Resource,
                            ImVec2(
                                20.0f,
                                20.0f
                            )
                        );

                        ImGui::SameLine(
                            0.0f,
                            5.0f
                        );
                    }

                    ImGui::TextColored(
                        itemNameColor,
                        "%s",
                        item.name.c_str()
                    );

                    ImGui::TableSetColumnIndex(
                        1
                    );

                    ImGui::TextColored(
                        sellColor,
                        "%s",
                        sellText.c_str()
                    );

                    ImGui::TableSetColumnIndex(
                        2
                    );

                    ImGui::TextColored(
                        buyColor,
                        "%s",
                        buyText.c_str()
                    );

                    ImGui::TableSetColumnIndex(
                        3
                    );

                    if (
                        ImGui::SmallButton(
                            "Targets"
                        )
                        )
                    {
                        openEditTargetsPopup =
                            true;
                    }

                    ImGui::SameLine();

                    if (
                        ImGui::SmallButton(
                            "Refresh"
                        )
                        )
                    {
                        TradingPostWatchManager::
                            RefreshItem(
                                item.itemID
                            );
                    }

                    if (!item.isDefault)
                    {
                        ImGui::SameLine();

                        if (
                            ImGui::SmallButton(
                                "Remove"
                            )
                            )
                        {
                            TradingPostWatchManager::
                                RemoveItem(
                                    item.itemID
                                );
                        }
                    }

                    ImGui::EndTable();
                }
            }
            else
            {
                if (
                    ImGui::BeginTable(
                        "##CompactWatchItem",
                        6,
                        ImGuiTableFlags_SizingStretchProp |
                        ImGuiTableFlags_NoSavedSettings
                    )
                    )
                {
                    ImGui::TableSetupColumn(
                        "Item",
                        ImGuiTableColumnFlags_WidthStretch,
                        2.20f
                    );

                    ImGui::TableSetupColumn(
                        "Sell",
                        ImGuiTableColumnFlags_WidthFixed,
                        118.0f
                    );

                    ImGui::TableSetupColumn(
                        "Buy",
                        ImGuiTableColumnFlags_WidthFixed,
                        118.0f
                    );

                    ImGui::TableSetupColumn(
                        "Sell Target",
                        ImGuiTableColumnFlags_WidthFixed,
                        180.0f
                    );

                    ImGui::TableSetupColumn(
                        "Buy Target",
                        ImGuiTableColumnFlags_WidthFixed,
                        180.0f
                    );

                    ImGui::TableSetupColumn(
                        "Actions",
                        ImGuiTableColumnFlags_WidthFixed,
                        215.0f
                    );

                    ImGui::TableNextRow();

                    ImGui::TableSetColumnIndex(
                        0
                    );

                    RenderFavoriteButton();

                    Texture_t* itemIcon =
                        GetTradingPostItemIcon(
                            item.itemID
                        );

                    if (
                        itemIcon != nullptr &&
                        itemIcon->Resource != nullptr
                        )
                    {
                        const float iconSize =
                            normalView
                            ? 28.0f
                            : 24.0f;

                        ImGui::Image(
                            (ImTextureID)
                            itemIcon->Resource,
                            ImVec2(
                                iconSize,
                                iconSize
                            )
                        );

                        ImGui::SameLine(
                            0.0f,
                            normalView
                            ? 9.0f
                            : 7.0f
                        );
                    }

                    ImGui::SetWindowFontScale(
                        normalView
                        ? 1.12f
                        : 1.08f
                    );

                    ImGui::TextColored(
                        itemNameColor,
                        "%s",
                        item.name.c_str()
                    );

                    ImGui::SetWindowFontScale(
                        1.00f
                    );

                    if (normalView)
                    {
                        if (hasPrice)
                        {
                            ImGui::TextDisabled(
                                "ID %u  |  Updated %llu sec ago",
                                item.itemID,
                                ageSeconds
                            );
                        }
                        else
                        {
                            ImGui::TextDisabled(
                                "ID %u  |  Price loading...",
                                item.itemID
                            );
                        }
                    }

                    ImGui::TableSetColumnIndex(
                        1
                    );

                    ImGui::TextDisabled(
                        "Sell"
                    );

                    ImGui::SameLine(
                        0.0f,
                        4.0f
                    );

                    ImGui::TextColored(
                        sellColor,
                        "%s",
                        sellText.c_str()
                    );

                    ImGui::TableSetColumnIndex(
                        2
                    );

                    ImGui::TextDisabled(
                        "Buy"
                    );

                    ImGui::SameLine(
                        0.0f,
                        4.0f
                    );

                    ImGui::TextColored(
                        buyColor,
                        "%s",
                        buyText.c_str()
                    );

                    ImGui::TableSetColumnIndex(
                        3
                    );

                    if (activeSellTargetForItem)
                    {
                        ImGui::TableSetBgColor(
                            ImGuiTableBgTarget_CellBg,
                            ImGui::GetColorU32(
                                ImVec4(
                                    0.10f,
                                    0.30f +
                                    0.08f * targetPulse,
                                    0.08f,
                                    0.58f +
                                    0.16f * targetPulse
                                )
                            )
                        );
                    }

                    ImGui::TextDisabled(
                        "Sell "
                    );

                    ImGui::SameLine(
                        0.0f,
                        2.0f
                    );

                    if (item.targetSellCopper > 0)
                    {
                        ImGui::TextColored(
                            sellColor,
                            "%s",
                            sellTargetValueText.c_str()
                        );
                    }
                    else
                    {
                        ImGui::TextDisabled(
                            "-"
                        );
                    }

                    ImGui::SameLine(
                        0.0f,
                        6.0f
                    );

                    if (item.targetSellCopper == 0)
                    {
                        ImGui::TextDisabled(
                            "NO TARGET"
                        );
                    }
                    else if (!hasPrice)
                    {
                        ImGui::TextDisabled(
                            "WAITING"
                        );
                    }
                    else if (targetReached)
                    {
                        ImGui::TextColored(
                            goodColor,
                            "HIT"
                        );
                    }
                    else
                    {
                        ImGui::TextColored(
                            attentionColor,
                            "WAIT"
                        );
                    }

                    ImGui::TableSetColumnIndex(
                        4
                    );

                    if (activeBuyTargetForItem)
                    {
                        ImGui::TableSetBgColor(
                            ImGuiTableBgTarget_CellBg,
                            ImGui::GetColorU32(
                                ImVec4(
                                    0.05f,
                                    0.16f +
                                    0.06f * targetPulse,
                                    0.34f +
                                    0.08f * targetPulse,
                                    0.54f +
                                    0.18f * targetPulse
                                )
                            )
                        );
                    }

                    ImGui::TextDisabled(
                        "Buy "
                    );

                    ImGui::SameLine(
                        0.0f,
                        2.0f
                    );

                    if (item.targetBuyCopper > 0)
                    {
                        ImGui::TextColored(
                            buyColor,
                            "%s",
                            buyTargetValueText.c_str()
                        );
                    }
                    else
                    {
                        ImGui::TextDisabled(
                            "-"
                        );
                    }

                    ImGui::SameLine(
                        0.0f,
                        6.0f
                    );

                    if (item.targetBuyCopper == 0)
                    {
                        ImGui::TextDisabled(
                            "NO TARGET"
                        );
                    }
                    else if (!hasPrice)
                    {
                        ImGui::TextDisabled(
                            "WAITING"
                        );
                    }
                    else if (buyTargetReached)
                    {
                        ImGui::TextColored(
                            goodColor,
                            "HIT"
                        );
                    }
                    else
                    {
                        ImGui::TextColored(
                            attentionColor,
                            "WAIT"
                        );
                    }

                    ImGui::TableSetColumnIndex(
                        5
                    );

                    if (
                        ImGui::SmallButton(
                            "Edit Targets"
                        )
                        )
                    {
                        openEditTargetsPopup =
                            true;
                    }

                    ImGui::SameLine();

                    if (
                        ImGui::SmallButton(
                            "Refresh"
                        )
                        )
                    {
                        TradingPostWatchManager::
                            RefreshItem(
                                item.itemID
                            );
                    }

                    if (!item.isDefault)
                    {
                        ImGui::SameLine();

                        if (
                            ImGui::SmallButton(
                                "Remove"
                            )
                            )
                        {
                            TradingPostWatchManager::
                                RemoveItem(
                                    item.itemID
                                );
                        }
                    }

                    ImGui::EndTable();
                }
            }

            ImGui::PopStyleVar();

            if (openEditTargetsPopup)
            {
                ImGui::OpenPopup(
                    "##EditTargetsPopup"
                );
            }

            //
            // Targets stay compact during normal viewing. The numeric g/s/c
            // controls only appear while this item is being edited.
            //
            ImGui::SetNextWindowSize(
                ImVec2(
                    430.0f,
                    0.0f
                ),
                ImGuiCond_Appearing
            );

            if (
                ImGui::BeginPopup(
                    "##EditTargetsPopup"
                )
                )
            {
                ImGui::PushStyleVar(
                    ImGuiStyleVar_FrameRounding,
                    4.0f
                );

                ImGui::SetWindowFontScale(
                    1.10f
                );

                ImGui::TextColored(
                    itemNameColor,
                    "%s",
                    item.name.c_str()
                );

                ImGui::SetWindowFontScale(
                    1.00f
                );

                ImGui::TextDisabled(
                    "Set alert prices for this watched item."
                );

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();

                if (
                    ImGui::BeginTable(
                        "##TargetEditorTable",
                        5,
                        ImGuiTableFlags_SizingFixedFit |
                        ImGuiTableFlags_NoSavedSettings
                    )
                    )
                {
                    ImGui::TableSetupColumn(
                        "Target",
                        ImGuiTableColumnFlags_WidthFixed,
                        82.0f
                    );

                    ImGui::TableSetupColumn(
                        "Gold",
                        ImGuiTableColumnFlags_WidthFixed,
                        92.0f
                    );

                    ImGui::TableSetupColumn(
                        "Silver",
                        ImGuiTableColumnFlags_WidthFixed,
                        72.0f
                    );

                    ImGui::TableSetupColumn(
                        "Copper",
                        ImGuiTableColumnFlags_WidthFixed,
                        72.0f
                    );

                    ImGui::TableSetupColumn(
                        "Clear",
                        ImGuiTableColumnFlags_WidthFixed,
                        58.0f
                    );

                    //
                    // Sell Target
                    //
                    ImGui::TableNextRow();

                    ImGui::TableSetColumnIndex(
                        0
                    );

                    ImGui::TextColored(
                        sellColor,
                        "Sell Target"
                    );

                    ImGui::TableSetColumnIndex(
                        1
                    );

                    ImGui::SetNextItemWidth(
                        62.0f
                    );

                    if (
                        ImGui::InputInt(
                            "##EditSellTargetGold",
                            &targetGold,
                            0,
                            0
                        )
                        )
                    {
                        targetChanged =
                            true;
                    }

                    ImGui::SameLine(
                        0.0f,
                        3.0f
                    );

                    ImGui::TextDisabled(
                        "g"
                    );

                    ImGui::TableSetColumnIndex(
                        2
                    );

                    ImGui::SetNextItemWidth(
                        43.0f
                    );

                    if (
                        ImGui::InputInt(
                            "##EditSellTargetSilver",
                            &targetSilver,
                            0,
                            0
                        )
                        )
                    {
                        targetChanged =
                            true;
                    }

                    ImGui::SameLine(
                        0.0f,
                        3.0f
                    );

                    ImGui::TextDisabled(
                        "s"
                    );

                    ImGui::TableSetColumnIndex(
                        3
                    );

                    ImGui::SetNextItemWidth(
                        43.0f
                    );

                    if (
                        ImGui::InputInt(
                            "##EditSellTargetCopper",
                            &targetCopperOnly,
                            0,
                            0
                        )
                        )
                    {
                        targetChanged =
                            true;
                    }

                    ImGui::SameLine(
                        0.0f,
                        3.0f
                    );

                    ImGui::TextDisabled(
                        "c"
                    );

                    ImGui::TableSetColumnIndex(
                        4
                    );

                    if (
                        ImGui::SmallButton(
                            "Clear##SellTarget"
                        )
                        )
                    {
                        targetGold = 0;
                        targetSilver = 0;
                        targetCopperOnly = 0;

                        targetChanged =
                            true;
                    }

                    //
                    // Buy Target
                    //
                    ImGui::TableNextRow();

                    ImGui::TableSetColumnIndex(
                        0
                    );

                    ImGui::TextColored(
                        buyColor,
                        "Buy Target"
                    );

                    ImGui::TableSetColumnIndex(
                        1
                    );

                    ImGui::SetNextItemWidth(
                        62.0f
                    );

                    if (
                        ImGui::InputInt(
                            "##EditBuyTargetGold",
                            &buyTargetGold,
                            0,
                            0
                        )
                        )
                    {
                        buyTargetChanged =
                            true;
                    }

                    ImGui::SameLine(
                        0.0f,
                        3.0f
                    );

                    ImGui::TextDisabled(
                        "g"
                    );

                    ImGui::TableSetColumnIndex(
                        2
                    );

                    ImGui::SetNextItemWidth(
                        43.0f
                    );

                    if (
                        ImGui::InputInt(
                            "##EditBuyTargetSilver",
                            &buyTargetSilver,
                            0,
                            0
                        )
                        )
                    {
                        buyTargetChanged =
                            true;
                    }

                    ImGui::SameLine(
                        0.0f,
                        3.0f
                    );

                    ImGui::TextDisabled(
                        "s"
                    );

                    ImGui::TableSetColumnIndex(
                        3
                    );

                    ImGui::SetNextItemWidth(
                        43.0f
                    );

                    if (
                        ImGui::InputInt(
                            "##EditBuyTargetCopper",
                            &buyTargetCopperOnly,
                            0,
                            0
                        )
                        )
                    {
                        buyTargetChanged =
                            true;
                    }

                    ImGui::SameLine(
                        0.0f,
                        3.0f
                    );

                    ImGui::TextDisabled(
                        "c"
                    );

                    ImGui::TableSetColumnIndex(
                        4
                    );

                    if (
                        ImGui::SmallButton(
                            "Clear##BuyTarget"
                        )
                        )
                    {
                        buyTargetGold = 0;
                        buyTargetSilver = 0;
                        buyTargetCopperOnly = 0;

                        buyTargetChanged =
                            true;
                    }

                    ImGui::EndTable();
                }

                if (targetGold < 0)
                {
                    targetGold = 0;
                }

                if (targetSilver < 0)
                {
                    targetSilver = 0;
                }

                if (targetSilver > 99)
                {
                    targetSilver = 99;
                }

                if (targetCopperOnly < 0)
                {
                    targetCopperOnly = 0;
                }

                if (targetCopperOnly > 99)
                {
                    targetCopperOnly = 99;
                }

                if (buyTargetGold < 0)
                {
                    buyTargetGold = 0;
                }

                if (buyTargetSilver < 0)
                {
                    buyTargetSilver = 0;
                }

                if (buyTargetSilver > 99)
                {
                    buyTargetSilver = 99;
                }

                if (buyTargetCopperOnly < 0)
                {
                    buyTargetCopperOnly = 0;
                }

                if (buyTargetCopperOnly > 99)
                {
                    buyTargetCopperOnly = 99;
                }

                if (targetChanged)
                {
                    const uint64_t newTargetCopper =
                        static_cast<uint64_t>(
                            targetGold
                            ) *
                        10000ULL +
                        static_cast<uint64_t>(
                            targetSilver
                            ) *
                        100ULL +
                        static_cast<uint64_t>(
                            targetCopperOnly
                            );

                    TradingPostWatchManager::
                        SetTargetSellPrice(
                            item.itemID,
                            newTargetCopper
                        );
                }

                if (buyTargetChanged)
                {
                    const uint64_t newBuyTargetCopper =
                        static_cast<uint64_t>(
                            buyTargetGold
                            ) *
                        10000ULL +
                        static_cast<uint64_t>(
                            buyTargetSilver
                            ) *
                        100ULL +
                        static_cast<uint64_t>(
                            buyTargetCopperOnly
                            );

                    TradingPostWatchManager::
                        SetTargetBuyPrice(
                            item.itemID,
                            newBuyTargetCopper
                        );
                }

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();

                const float doneButtonWidth =
                    96.0f;

                ImGui::SetCursorPosX(
                    (
                        ImGui::GetWindowSize().x -
                        doneButtonWidth
                        ) *
                    0.5f
                );

                if (
                    ImGui::Button(
                        "Done",
                        ImVec2(
                            doneButtonWidth,
                            26.0f
                        )
                    )
                    )
                {
                    ImGui::CloseCurrentPopup();
                }

                ImGui::PopStyleVar();

                ImGui::EndPopup();
            }

            uint64_t historySpanSeconds = 0;

            if (history.size() >= 2)
            {
                const uint64_t newestTimestamp =
                    history.back().
                    timestampUnixSeconds;

                const uint64_t oldestTimestamp =
                    history.front().
                    timestampUnixSeconds;

                historySpanSeconds =
                    newestTimestamp >= oldestTimestamp
                    ? newestTimestamp - oldestTimestamp
                    : 0;
            }

            size_t minimumAnalysisSamples =
                15;

            if (useCollectorAnalysisHistory)
            {
                constexpr uint64_t
                    COLLECTOR_SCAN_INTERVAL_SECONDS =
                    5ULL * 60ULL;

                const size_t expectedWindowSamples =
                    static_cast<size_t>(
                        trendWindowSeconds /
                        COLLECTOR_SCAN_INTERVAL_SECONDS
                        ) +
                    1;

                const size_t seventyFivePercentSamples =
                    (
                        expectedWindowSamples *
                        3 +
                        3
                        ) /
                    4;

                minimumAnalysisSamples =
                    std::min<size_t>(
                        15,
                        std::max<size_t>(
                            3,
                            seventyFivePercentSamples
                        )
                    );
            }

            const WindowCoverageInfo windowCoverage =
                useCollectorAnalysisHistory
                ? CalculateCollectorSnapshotCoverage(
                    analysisHistory,
                    trendWindowSeconds
                )
                : CalculateGapAwareCoverage(
                    analysisHistory,
                    trendWindowSeconds
                );

            const double selectedWindowCoverage =
                windowCoverage.coverage;

            const bool marketAnalysisReady =
                windowCoverage.sampleCount >=
                minimumAnalysisSamples &&
                selectedWindowCoverage >= 0.75 &&
                sellTrend.available &&
                buyTrend.available;

            const bool volatilityAnalysisReady =
                windowCoverage.sampleCount >=
                minimumAnalysisSamples &&
                selectedWindowCoverage >= 0.75;

            const VolatilityInfo volatility =
                CalculateVolatility(
                    analysisHistory,
                    trendWindowSeconds,
                    volatilityAnalysisReady
                );

            //
            // Keep the displayed direction strictly tied to the selected
            // Trend Window. If that window is not available yet, show
            // Developing instead of silently falling back to the entire
            // available history.
            //
            const TrendInfo observedSellTrend =
                sellTrend;

            const TrendInfo observedBuyTrend =
                buyTrend;

            if (
                !ultraSlimView &&
                hasPrice &&
                price.available &&
                price.sellUnitPrice > 0
                )
            {
                const std::string spanText =
                    FormatHistorySpan(
                        windowCoverage.coveredSeconds
                    );

                const std::string windowText =
                    FormatHistorySpan(
                        trendWindowSeconds
                    );

                const char* listingDirection =
                    "Stable";

                if (
                    observedSellTrend.available &&
                    observedSellTrend.trend ==
                    PriceTrend::Up
                    )
                {
                    listingDirection =
                        "More Expensive";
                }
                else if (
                    observedSellTrend.available &&
                    observedSellTrend.trend ==
                    PriceTrend::Down
                    )
                {
                    listingDirection =
                        "Cheaper";
                }
                else if (!observedSellTrend.available)
                {
                    listingDirection =
                        "Developing";
                }

                const char* buyOrderDirection =
                    "Stable";

                if (
                    observedBuyTrend.available &&
                    observedBuyTrend.trend ==
                    PriceTrend::Up
                    )
                {
                    buyOrderDirection =
                        "Higher";
                }
                else if (
                    observedBuyTrend.available &&
                    observedBuyTrend.trend ==
                    PriceTrend::Down
                    )
                {
                    buyOrderDirection =
                        "Lower";
                }
                else if (!observedBuyTrend.available)
                {
                    buyOrderDirection =
                        "Developing";
                }

                const char* confidenceLabel =
                    marketAnalysisReady &&
                    selectedWindowCoverage >= 0.90
                    ? "HIGH"
                    : (
                        marketAnalysisReady
                        ? "MED"
                        : "LOW"
                        );

                std::string listingSummary =
                    listingDirection;

                if (observedSellTrend.available)
                {
                    char percentBuffer[32] = {};

                    std::snprintf(
                        percentBuffer,
                        sizeof(percentBuffer),
                        " %+.2f%%",
                        observedSellTrend.percentChange
                    );

                    listingSummary +=
                        percentBuffer;
                }

                std::string buyOrderSummary =
                    buyOrderDirection;

                if (observedBuyTrend.available)
                {
                    char percentBuffer[32] = {};

                    std::snprintf(
                        percentBuffer,
                        sizeof(percentBuffer),
                        " %+.2f%%",
                        observedBuyTrend.percentChange
                    );

                    buyOrderSummary +=
                        percentBuffer;
                }

                ImGui::TextDisabled(
                    "Listings %s  |  Buy %s  |  %s  |  %s/%s",
                    listingSummary.c_str(),
                    buyOrderSummary.c_str(),
                    confidenceLabel,
                    spanText.c_str(),
                    windowText.c_str()
                );
            }

            const std::string detailsHeader =
                "Details (" +
                std::to_string(
                    history.size()
                ) +
                ")###DetailsHistory" +
                std::to_string(
                    item.itemID
                );

            const bool detailsOpen =
                ImGui::CollapsingHeader(
                    detailsHeader.c_str()
                );

            if (detailsOpen)
            {
                ImGui::Indent(
                    18.0f
                );

                ImGui::Dummy(
                    ImVec2(
                        0.0f,
                        3.0f
                    )
                );

                if (hasPrice)
                {
                    ImGui::TextDisabled(
                        "Item ID %u  |  Updated %llu sec ago",
                        item.itemID,
                        ageSeconds
                    );
                }
                else
                {
                    ImGui::TextDisabled(
                        "Item ID %u  |  Price data loading...",
                        item.itemID
                    );
                }

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();

                if (
                    hasPrice &&
                    price.available &&
                    price.sellUnitPrice > 0
                    )
                {
                    const DealWindowStats sellStats =
                        CalculateDealWindowStats(
                            analysisHistory,
                            trendWindowSeconds
                        );

                    const DealWindowStats buyStats =
                        CalculateBuyWindowStats(
                            analysisHistory,
                            trendWindowSeconds
                        );

                    const std::string spanText =
                        FormatHistorySpan(
                            windowCoverage.coveredSeconds
                        );

                    const std::string windowText =
                        FormatHistorySpan(
                            trendWindowSeconds
                        );

                    const int coveragePercent =
                        static_cast<int>(
                            selectedWindowCoverage *
                            100.0 +
                            0.5
                            );

                    std::vector<
                        SparklinePoint
                    > analysisBuyHistoryGold;

                    std::vector<
                        SparklinePoint
                    > analysisSellHistoryGold;

                    analysisBuyHistoryGold.reserve(
                        analysisHistory.size()
                    );

                    analysisSellHistoryGold.reserve(
                        analysisHistory.size()
                    );

                    uint64_t analysisSparklineStartTimestamp =
                        0;

                    uint64_t analysisSparklineEndTimestamp =
                        0;

                    if (!analysisHistory.empty())
                    {
                        analysisSparklineEndTimestamp =
                            analysisHistory.back().
                            timestampUnixSeconds;

                        if (
                            analysisSparklineEndTimestamp >
                            trendWindowSeconds
                            )
                        {
                            analysisSparklineStartTimestamp =
                                analysisSparklineEndTimestamp -
                                trendWindowSeconds;
                        }
                    }

                    for (
                        const TradingPostHistoryPoint&
                        point :
                        analysisHistory
                        )
                    {
                        if (
                            point.timestampUnixSeconds <
                            analysisSparklineStartTimestamp ||
                            point.timestampUnixSeconds >
                            analysisSparklineEndTimestamp
                            )
                        {
                            continue;
                        }

                        if (point.buyUnitPrice > 0)
                        {
                            SparklinePoint sparklinePoint;

                            sparklinePoint.timestampUnixSeconds =
                                point.timestampUnixSeconds;

                            sparklinePoint.value =
                                static_cast<float>(
                                    point.buyUnitPrice
                                    ) /
                                10000.0f;

                            analysisBuyHistoryGold.push_back(
                                sparklinePoint
                            );
                        }

                        if (point.sellUnitPrice > 0)
                        {
                            SparklinePoint sparklinePoint;

                            sparklinePoint.timestampUnixSeconds =
                                point.timestampUnixSeconds;

                            sparklinePoint.value =
                                static_cast<float>(
                                    point.sellUnitPrice
                                    ) /
                                10000.0f;

                            analysisSellHistoryGold.push_back(
                                sparklinePoint
                            );
                        }
                    }

                    ImGui::TextUnformatted(
                        "Market Analysis"
                    );

                    ImGui::Separator();

                    const float sellSectionTopY =
                        ImGui::GetCursorPosY();

                    const ImVec2 sellMarketCardTopLeft =
                        ImGui::GetCursorScreenPos();

                    ImGui::TextColored(
                        sellColor,
                        "SELL LISTINGS"
                    );

                    const char* listingDirection =
                        "Stable";

                    ImVec4 listingDirectionColor =
                        attentionColor;

                    if (
                        observedSellTrend.available &&
                        observedSellTrend.trend ==
                        PriceTrend::Up
                        )
                    {
                        listingDirection =
                            "Getting More Expensive";

                        listingDirectionColor =
                            trendDownColor;
                    }
                    else if (
                        observedSellTrend.available &&
                        observedSellTrend.trend ==
                        PriceTrend::Down
                        )
                    {
                        listingDirection =
                            "Getting Cheaper";

                        listingDirectionColor =
                            goodColor;
                    }
                    else if (!observedSellTrend.available)
                    {
                        listingDirection =
                            "Developing";

                        listingDirectionColor =
                            mutedColor;
                    }

                    ImGui::TextDisabled(
                        "Direction"
                    );

                    ImGui::SameLine(
                        125.0f
                    );

                    if (observedSellTrend.available)
                    {
                        ImGui::TextColored(
                            listingDirectionColor,
                            "%s  (%+.2f%%)",
                            listingDirection,
                            observedSellTrend.percentChange
                        );
                    }
                    else
                    {
                        ImGui::TextColored(
                            listingDirectionColor,
                            "%s",
                            listingDirection
                        );
                    }

                    if (
                        !marketAnalysisReady ||
                        !sellStats.available
                        )
                    {
                        ImGui::TextDisabled(
                            "Tracked Avg"
                        );

                        ImGui::SameLine(
                            125.0f
                        );

                        if (
                            sellStats.available &&
                            sellStats.averageSell > 0
                            )
                        {
                            const std::string trackedSellAverage =
                                FormatCoinValue(
                                    sellStats.averageSell
                                );

                            const double sellVsTrackedAverage =
                                (
                                    static_cast<double>(
                                        price.sellUnitPrice
                                        ) -
                                    static_cast<double>(
                                        sellStats.averageSell
                                        )
                                    ) /
                                static_cast<double>(
                                    sellStats.averageSell
                                    ) *
                                100.0;

                            ImGui::Text(
                                "%s  (%+.2f%%)",
                                trackedSellAverage.c_str(),
                                sellVsTrackedAverage
                            );
                        }
                        else
                        {
                            ImGui::TextDisabled(
                                "Developing"
                            );
                        }

                        ImGui::TextDisabled(
                            "For Buyers"
                        );

                        ImGui::SameLine(
                            125.0f
                        );

                        ImGui::TextDisabled(
                            "Developing"
                        );
                    }
                    else
                    {
                        const double sellVsAverage =
                            (
                                static_cast<double>(
                                    price.sellUnitPrice
                                    ) -
                                static_cast<double>(
                                    sellStats.averageSell
                                    )
                                ) /
                            static_cast<double>(
                                sellStats.averageSell
                                ) *
                            100.0;

                        const char* sellPosition =
                            "Near Recent Avg";

                        ImVec4 sellPositionColor =
                            attentionColor;

                        if (sellVsAverage <= -3.0)
                        {
                            sellPosition =
                                "Well Below Recent Avg";

                            sellPositionColor =
                                goodColor;
                        }
                        else if (sellVsAverage <= -1.0)
                        {
                            sellPosition =
                                "Below Recent Avg";

                            sellPositionColor =
                                goodColor;
                        }
                        else if (sellVsAverage >= 3.0)
                        {
                            sellPosition =
                                "Well Above Recent Avg";

                            sellPositionColor =
                                trendDownColor;
                        }
                        else if (sellVsAverage >= 1.0)
                        {
                            sellPosition =
                                "Above Recent Avg";

                            sellPositionColor =
                                trendDownColor;
                        }

                        //
                        // Keep the buyer-facing signal consistent with the
                        // displayed price position. This avoids conflicting
                        // combinations such as "Well Above Recent Avg" and
                        // "Typical" at the same time.
                        //
                        const char* buyerSignal =
                            "Typical";

                        ImVec4 buyerSignalColor =
                            attentionColor;

                        if (sellVsAverage <= -1.0)
                        {
                            buyerSignal =
                                "Favorable";

                            buyerSignalColor =
                                goodColor;
                        }
                        else if (sellVsAverage >= 1.0)
                        {
                            buyerSignal =
                                "Caution";

                            buyerSignalColor =
                                trendDownColor;
                        }

                        const std::string sellAverageText =
                            FormatCoinValue(
                                sellStats.averageSell
                            );

                        ImGui::TextDisabled(
                            "Recent Avg"
                        );

                        ImGui::SameLine(
                            125.0f
                        );

                        ImGui::Text(
                            "%s  (%+.2f%%)",
                            sellAverageText.c_str(),
                            sellVsAverage
                        );

                        ImGui::TextDisabled(
                            "Price Position"
                        );

                        ImGui::SameLine(
                            125.0f
                        );

                        ImGui::TextColored(
                            sellPositionColor,
                            "%s",
                            sellPosition
                        );

                        ImGui::TextDisabled(
                            "For Buyers"
                        );

                        ImGui::SameLine(
                            125.0f
                        );

                        ImGui::TextColored(
                            buyerSignalColor,
                            "%s",
                            buyerSignal
                        );
                    }

                    //
                    // Box the left-side sell market data into a subtle card.
                    // Keep the fill transparent so it does not overpower the
                    // analysis text, and use the sell color as the accent.
                    //
                    {
                        const ImVec2 sellMarketCardBottomCursor =
                            ImGui::GetCursorScreenPos();

                        ImDrawList* marketCardDrawList =
                            ImGui::GetWindowDrawList();

                        const float marketCardWidth =
                            400.0f;

                        const ImVec2 cardMin(
                            sellMarketCardTopLeft.x -
                            8.0f,
                            sellMarketCardTopLeft.y -
                            5.0f
                        );

                        const ImVec2 cardMax(
                            sellMarketCardTopLeft.x +
                            marketCardWidth,
                            sellMarketCardBottomCursor.y +
                            5.0f
                        );

                        marketCardDrawList->AddRect(
                            cardMin,
                            cardMax,
                            ImGui::
                            ColorConvertFloat4ToU32(
                                ImVec4(
                                    sellColor.x,
                                    sellColor.y,
                                    sellColor.z,
                                    0.34f
                                )
                            ),
                            4.0f,
                            0,
                            1.0f
                        );

                        marketCardDrawList->AddLine(
                            ImVec2(
                                cardMin.x,
                                cardMin.y +
                                4.0f
                            ),
                            ImVec2(
                                cardMin.x,
                                cardMax.y -
                                4.0f
                            ),
                            ImGui::
                            ColorConvertFloat4ToU32(
                                ImVec4(
                                    sellColor.x,
                                    sellColor.y,
                                    sellColor.z,
                                    0.82f
                                )
                            ),
                            2.0f
                        );
                    }

                    if (
                        analysisSellHistoryGold.size() >= 2
                        )
                    {
                        const float sellSectionBottomY =
                            ImGui::GetCursorPosY();

                        const float sellSparklineWidth =
                            std::max(
                                520.0f,
                                ImGui::GetWindowWidth() -
                                470.0f -
                                26.0f
                            );

                        const float sellSparklineX =
                            470.0f;

                        ImGui::SetCursorPos(
                            ImVec2(
                                sellSparklineX,
                                sellSectionTopY +
                                19.0f
                            )
                        );

                        DrawSparkline(
                            "##SellAnalysisSparkline",
                            analysisSellHistoryGold,
                            analysisSparklineStartTimestamp,
                            analysisSparklineEndTimestamp,
                            sellColor,
                            listingDirectionColor,
                            trendWindowLabel,
                            halfTrendWindowLabel.c_str(),
                            observedSellTrend,
                            static_cast<uint64_t>(
                                price.sellUnitPrice
                                ),
                            sellSparklineWidth,
                            82.0f
                        );

                        ImGui::SetCursorPosY(
                            std::max(
                                sellSectionBottomY,
                                sellSectionTopY +
                                108.0f
                            )
                        );
                    }

                    ImGui::Spacing();
                    ImGui::Separator();
                    ImGui::Spacing();

                    const float buySectionTopY =
                        ImGui::GetCursorPosY();

                    const ImVec2 buyMarketCardTopLeft =
                        ImGui::GetCursorScreenPos();

                    ImGui::TextColored(
                        buyColor,
                        "BUY ORDERS"
                    );

                    const char* orderDirection =
                        "Stable";

                    ImVec4 orderDirectionColor =
                        attentionColor;

                    if (
                        observedBuyTrend.available &&
                        observedBuyTrend.trend ==
                        PriceTrend::Up
                        )
                    {
                        orderDirection =
                            "Buyers Offering More";

                        orderDirectionColor =
                            goodColor;
                    }
                    else if (
                        observedBuyTrend.available &&
                        observedBuyTrend.trend ==
                        PriceTrend::Down
                        )
                    {
                        orderDirection =
                            "Buyers Offering Less";

                        orderDirectionColor =
                            trendDownColor;
                    }
                    else if (!observedBuyTrend.available)
                    {
                        orderDirection =
                            "Developing";

                        orderDirectionColor =
                            mutedColor;
                    }

                    ImGui::TextDisabled(
                        "Direction"
                    );

                    ImGui::SameLine(
                        125.0f
                    );

                    if (observedBuyTrend.available)
                    {
                        ImGui::TextColored(
                            orderDirectionColor,
                            "%s  (%+.2f%%)",
                            orderDirection,
                            observedBuyTrend.percentChange
                        );
                    }
                    else
                    {
                        ImGui::TextColored(
                            orderDirectionColor,
                            "%s",
                            orderDirection
                        );
                    }

                    if (
                        !marketAnalysisReady ||
                        !buyStats.available ||
                        price.buyUnitPrice == 0
                        )
                    {
                        ImGui::TextDisabled(
                            "Tracked Avg"
                        );

                        ImGui::SameLine(
                            125.0f
                        );

                        if (
                            buyStats.available &&
                            buyStats.averageSell > 0 &&
                            price.buyUnitPrice > 0
                            )
                        {
                            const std::string trackedBuyAverage =
                                FormatCoinValue(
                                    buyStats.averageSell
                                );

                            const double buyVsTrackedAverage =
                                (
                                    static_cast<double>(
                                        price.buyUnitPrice
                                        ) -
                                    static_cast<double>(
                                        buyStats.averageSell
                                        )
                                    ) /
                                static_cast<double>(
                                    buyStats.averageSell
                                    ) *
                                100.0;

                            ImGui::Text(
                                "%s  (%+.2f%%)",
                                trackedBuyAverage.c_str(),
                                buyVsTrackedAverage
                            );
                        }
                        else
                        {
                            ImGui::TextDisabled(
                                "Developing"
                            );
                        }

                        ImGui::TextDisabled(
                            "For Sellers"
                        );

                        ImGui::SameLine(
                            125.0f
                        );

                        ImGui::TextDisabled(
                            "Developing"
                        );

                        ImGui::Spacing();

                        ImGui::TextDisabled(
                            "Analysis is still developing for the selected Trend Window."
                        );
                    }
                    else
                    {
                        const double buyVsAverage =
                            (
                                static_cast<double>(
                                    price.buyUnitPrice
                                    ) -
                                static_cast<double>(
                                    buyStats.averageSell
                                    )
                                ) /
                            static_cast<double>(
                                buyStats.averageSell
                                ) *
                            100.0;

                        const char* buyPosition =
                            "Near Recent Avg";

                        ImVec4 buyPositionColor =
                            attentionColor;

                        if (buyVsAverage >= 3.0)
                        {
                            buyPosition =
                                "Well Above Recent Avg";

                            buyPositionColor =
                                goodColor;
                        }
                        else if (buyVsAverage >= 1.0)
                        {
                            buyPosition =
                                "Above Recent Avg";

                            buyPositionColor =
                                goodColor;
                        }
                        else if (buyVsAverage <= -3.0)
                        {
                            buyPosition =
                                "Well Below Recent Avg";

                            buyPositionColor =
                                trendDownColor;
                        }
                        else if (buyVsAverage <= -1.0)
                        {
                            buyPosition =
                                "Below Recent Avg";

                            buyPositionColor =
                                trendDownColor;
                        }

                        //
                        // Keep the seller-facing signal consistent with the
                        // displayed buy-order position.
                        //
                        const char* sellerSignal =
                            "Typical";

                        ImVec4 sellerSignalColor =
                            attentionColor;

                        if (buyVsAverage >= 1.0)
                        {
                            sellerSignal =
                                "Favorable";

                            sellerSignalColor =
                                goodColor;
                        }
                        else if (buyVsAverage <= -1.0)
                        {
                            sellerSignal =
                                "Weak";

                            sellerSignalColor =
                                trendDownColor;
                        }

                        const std::string buyAverageText =
                            FormatCoinValue(
                                buyStats.averageSell
                            );

                        ImGui::TextDisabled(
                            "Recent Avg"
                        );

                        ImGui::SameLine(
                            125.0f
                        );

                        ImGui::Text(
                            "%s  (%+.2f%%)",
                            buyAverageText.c_str(),
                            buyVsAverage
                        );

                        ImGui::TextDisabled(
                            "Price Position"
                        );

                        ImGui::SameLine(
                            125.0f
                        );

                        ImGui::TextColored(
                            buyPositionColor,
                            "%s",
                            buyPosition
                        );

                        ImGui::TextDisabled(
                            "For Sellers"
                        );

                        ImGui::SameLine(
                            125.0f
                        );

                        ImGui::TextColored(
                            sellerSignalColor,
                            "%s",
                            sellerSignal
                        );
                    }

                    //
                    // Matching card for buy-order market data.
                    //
                    {
                        const ImVec2 buyMarketCardBottomCursor =
                            ImGui::GetCursorScreenPos();

                        ImDrawList* marketCardDrawList =
                            ImGui::GetWindowDrawList();

                        const float marketCardWidth =
                            400.0f;

                        const ImVec2 cardMin(
                            buyMarketCardTopLeft.x -
                            8.0f,
                            buyMarketCardTopLeft.y -
                            5.0f
                        );

                        const ImVec2 cardMax(
                            buyMarketCardTopLeft.x +
                            marketCardWidth,
                            buyMarketCardBottomCursor.y +
                            5.0f
                        );

                        marketCardDrawList->AddRect(
                            cardMin,
                            cardMax,
                            ImGui::
                            ColorConvertFloat4ToU32(
                                ImVec4(
                                    buyColor.x,
                                    buyColor.y,
                                    buyColor.z,
                                    0.34f
                                )
                            ),
                            4.0f,
                            0,
                            1.0f
                        );

                        marketCardDrawList->AddLine(
                            ImVec2(
                                cardMin.x,
                                cardMin.y +
                                4.0f
                            ),
                            ImVec2(
                                cardMin.x,
                                cardMax.y -
                                4.0f
                            ),
                            ImGui::
                            ColorConvertFloat4ToU32(
                                ImVec4(
                                    buyColor.x,
                                    buyColor.y,
                                    buyColor.z,
                                    0.82f
                                )
                            ),
                            2.0f
                        );
                    }

                    if (
                        analysisBuyHistoryGold.size() >= 2
                        )
                    {
                        const float buySectionBottomY =
                            ImGui::GetCursorPosY();

                        const float buySparklineWidth =
                            std::max(
                                520.0f,
                                ImGui::GetWindowWidth() -
                                470.0f -
                                26.0f
                            );

                        const float buySparklineX =
                            470.0f;

                        ImGui::SetCursorPos(
                            ImVec2(
                                buySparklineX,
                                buySectionTopY +
                                19.0f
                            )
                        );

                        DrawSparkline(
                            "##BuyAnalysisSparkline",
                            analysisBuyHistoryGold,
                            analysisSparklineStartTimestamp,
                            analysisSparklineEndTimestamp,
                            buyColor,
                            orderDirectionColor,
                            trendWindowLabel,
                            halfTrendWindowLabel.c_str(),
                            observedBuyTrend,
                            static_cast<uint64_t>(
                                price.buyUnitPrice
                                ),
                            buySparklineWidth,
                            82.0f
                        );

                        ImGui::SetCursorPosY(
                            std::max(
                                buySectionBottomY,
                                buySectionTopY +
                                108.0f
                            )
                        );
                    }

                    ImGui::Spacing();

                    const char* confidenceLabel =
                        marketAnalysisReady &&
                        selectedWindowCoverage >= 0.90
                        ? "HIGH"
                        : (
                            marketAnalysisReady
                            ? "MEDIUM"
                            : "LOW"
                            );

                    ImVec4 confidenceColor =
                        marketAnalysisReady
                        ? (
                            selectedWindowCoverage >= 0.90
                            ? goodColor
                            : attentionColor
                            )
                        : mutedColor;

                    ImGui::TextDisabled(
                        "Confidence"
                    );

                    ImGui::SameLine(
                        125.0f
                    );

                    ImGui::TextColored(
                        confidenceColor,
                        "%s",
                        confidenceLabel
                    );

                    ImGui::TextDisabled(
                        "Coverage"
                    );

                    ImGui::SameLine(
                        125.0f
                    );

                    ImGui::TextDisabled(
                        "%s of %s (%d%%)",
                        spanText.c_str(),
                        windowText.c_str(),
                        coveragePercent
                    );

                    if (ImGui::IsItemHovered())
                    {
                        ImGui::SetTooltip(
                            "Coverage uses completed Collector scans while the Collector is connected; local gap-aware history is used as fallback."
                        );
                    }

                    ImGui::TextDisabled(
                        "Samples"
                    );

                    ImGui::SameLine(
                        125.0f
                    );

                    ImGui::TextDisabled(
                        "%llu",
                        static_cast<
                        unsigned long long
                        >(
                            windowCoverage.sampleCount
                            )
                    );

                    if (ImGui::IsItemHovered())
                    {
                        ImGui::SetTooltip(
                            "Observations inside the selected trend window."
                        );
                    }

                    const char* volatilityLabel =
                        "DEVELOPING";

                    ImVec4 volatilityColor =
                        mutedColor;

                    if (volatility.available)
                    {
                        switch (volatility.level)
                        {
                        case VolatilityLevel::Calm:
                            volatilityLabel =
                                "CALM";

                            volatilityColor =
                                ImVec4(
                                    0.40f,
                                    0.88f,
                                    0.62f,
                                    1.00f
                                );
                            break;

                        case VolatilityLevel::Normal:
                            volatilityLabel =
                                "NORMAL";

                            volatilityColor =
                                ImVec4(
                                    0.45f,
                                    0.76f,
                                    1.00f,
                                    1.00f
                                );
                            break;

                        case VolatilityLevel::Active:
                            volatilityLabel =
                                "ACTIVE";

                            volatilityColor =
                                attentionColor;
                            break;

                        case VolatilityLevel::Wild:
                            volatilityLabel =
                                "WILD";

                            volatilityColor =
                                ImVec4(
                                    1.00f,
                                    0.42f,
                                    0.24f,
                                    1.00f
                                );
                            break;

                        default:
                            break;
                        }
                    }

                    ImGui::TextDisabled(
                        "Market Volatility"
                    );

                    ImGui::SameLine(
                        125.0f
                    );

                    DrawStatusBadge(
                        volatilityLabel,
                        volatilityColor
                    );

                    if (ImGui::IsItemHovered())
                    {
                        ImGui::SetTooltip(
                            "Market Volatility uses the more active of sell-listing or buy-order scan-to-scan movement during the selected Trend Window. A one-time jump followed by stable prices is less volatile than repeated large swings."
                        );
                    }

                    if (
                        price.buyUnitPrice > 0 &&
                        price.sellUnitPrice >=
                        price.buyUnitPrice
                        )
                    {
                        const uint64_t spreadCopper =
                            static_cast<uint64_t>(
                                price.sellUnitPrice -
                                price.buyUnitPrice
                                );

                        const double spreadPercent =
                            static_cast<double>(
                                spreadCopper
                                ) /
                            static_cast<double>(
                                price.sellUnitPrice
                                ) *
                            100.0;

                        const std::string spreadText =
                            FormatCoinValue(
                                spreadCopper
                            );

                        ImGui::TextDisabled(
                            "Spread"
                        );

                        ImGui::SameLine(
                            125.0f
                        );

                        ImGui::TextDisabled(
                            "%s (%.2f%%)",
                            spreadText.c_str(),
                            spreadPercent
                        );
                    }
                }
                else
                {
                    ImGui::TextDisabled(
                        "Market analysis is waiting for current price data."
                    );
                }


                ImGui::Unindent(
                    18.0f
                );

                //
                // Minimal separation: keep the expanded content visually attached
                // to its watched item, then clearly end it before the next item.
                //
                ImGui::Dummy(
                    ImVec2(
                        0.0f,
                        8.0f
                    )
                );

                ImGui::Separator();

                ImGui::Dummy(
                    ImVec2(
                        0.0f,
                        10.0f
                    )
                );
            }
            else
            {
                ImGui::Separator();

                ImGui::Dummy(
                    ImVec2(
                        0.0f,
                        6.0f
                    )
                );
            }

            ImGui::PopID();
        };

    for (
        const TradingPostWatchItem& item :
        watchedItems
        )
    {
        if (
            showFavoritesOnly &&
            !item.isFavorite
            )
        {
            continue;
        }

        RenderWatchItem(
            item
        );
    }

    ImGui::TextUnformatted(
        "Add Watched Item"
    );

    static char itemSearchInput[160] = {};
    static std::string searchStatus;
    static bool searchStatusError = false;

    static std::string lastSearchText;
    static size_t lastSearchIndexCount = 0;

    static std::vector<
        TradingPostIndexedItem
    > searchResults;

    const bool indexReady =
        TradingPostItemIndexManager::
        IsReady();

    const bool indexBuilding =
        TradingPostItemIndexManager::
        IsBuilding();

    const size_t buildProcessed =
        TradingPostItemIndexManager::
        GetBuildProcessedCount();

    const size_t buildTotal =
        TradingPostItemIndexManager::
        GetBuildTotalCount();

    if (indexBuilding)
    {
        if (buildTotal > 0)
        {
            ImGui::TextDisabled(
                "Building item search index: %llu / %llu",
                static_cast<
                unsigned long long
                >(
                    buildProcessed
                    ),
                static_cast<
                unsigned long long
                >(
                    buildTotal
                    )
            );
        }
        else
        {
            ImGui::TextDisabled(
                "Loading Trading Post item list..."
            );
        }
    }
    else if (indexReady)
    {
        ImGui::TextDisabled(
            "%llu Trading Post items available to search.",
            static_cast<
            unsigned long long
            >(
                indexItemCount
                )
        );
    }
    else
    {
        ImGui::TextDisabled(
            "Item search index is not ready yet."
        );
    }

    const std::string indexError =
        TradingPostItemIndexManager::
        GetLastError();

    if (!indexError.empty())
    {
        ImGui::TextColored(
            trendDownColor,
            "%s",
            indexError.c_str()
        );
    }

    ImGui::SetNextItemWidth(
        420.0f
    );

    const bool enterPressed =
        ImGui::InputText(
            "Search item name##NewWatch",
            itemSearchInput,
            sizeof(
                itemSearchInput
                ),
            ImGuiInputTextFlags_EnterReturnsTrue
        );

    ImGui::TextDisabled(
        "Start typing an item name. Matching Trading Post items appear below."
    );

    const std::string currentSearchText =
        itemSearchInput;

    if (
        currentSearchText !=
        lastSearchText ||
        indexItemCount !=
        lastSearchIndexCount
        )
    {
        const bool searchTextChanged =
            currentSearchText !=
            lastSearchText;

        lastSearchText =
            currentSearchText;

        lastSearchIndexCount =
            indexItemCount;

        if (searchTextChanged)
        {
            searchStatus.clear();
            searchStatusError =
                false;
        }

        searchResults.clear();

        if (
            currentSearchText.size() >= 2 &&
            indexReady
            )
        {
            searchResults =
                TradingPostItemIndexManager::
                Search(
                    currentSearchText,
                    8
                );
        }
    }

    bool selectedSearchItem =
        false;

    TradingPostIndexedItem
        selectedItem;

    if (
        currentSearchText.size() >= 2 &&
        indexReady
        )
    {
        if (searchResults.empty())
        {
            ImGui::TextDisabled(
                "No matching Trading Post items."
            );
        }
        else
        {
            const float rowHeight =
                ImGui::
                GetTextLineHeightWithSpacing();

            const float suggestionHeight =
                rowHeight *
                static_cast<float>(
                    searchResults.size()
                    ) +
                8.0f;

            if (
                ImGui::BeginChild(
                    "##TradingPostItemSuggestions",
                    ImVec2(
                        520.0f,
                        suggestionHeight
                    ),
                    true
                )
                )
            {
                for (
                    const TradingPostIndexedItem&
                    result :
                    searchResults
                    )
                {
                    std::string displayName =
                        result.name;

                    if (!result.variantLabel.empty())
                    {
                        displayName +=
                            " - " +
                            result.variantLabel;
                    }

                    const std::string label =
                        displayName +
                        "  (Item ID: " +
                        std::to_string(
                            result.itemID
                        ) +
                        ")##TPResult" +
                        std::to_string(
                            result.itemID
                        );

                    if (
                        ImGui::Selectable(
                            label.c_str()
                        )
                        )
                    {
                        selectedItem =
                            result;

                        selectedSearchItem =
                            true;

                        break;
                    }
                }
            }

            ImGui::EndChild();
        }
    }

    if (
        enterPressed &&
        !searchResults.empty()
        )
    {
        selectedItem =
            searchResults.front();

        selectedSearchItem =
            true;
    }

    if (selectedSearchItem)
    {
        std::string selectedDisplayName =
            selectedItem.name;

        if (!selectedItem.variantLabel.empty())
        {
            selectedDisplayName +=
                " - " +
                selectedItem.variantLabel;
        }

        const bool added =
            TradingPostWatchManager::
            AddItem(
                selectedItem.itemID,
                selectedDisplayName
            );

        if (added)
        {
            searchStatus =
                "Added: " +
                selectedDisplayName +
                " (Item ID " +
                std::to_string(
                    selectedItem.itemID
                ) +
                ")";

            searchStatusError =
                false;

            itemSearchInput[0] =
                '\0';

            lastSearchText.clear();

            searchResults.clear();
        }
        else
        {
            searchStatus =
                selectedDisplayName +
                " is already on the watch list.";

            searchStatusError =
                true;
        }
    }

    if (!searchStatus.empty())
    {
        if (searchStatusError)
        {
            ImGui::TextColored(
                trendDownColor,
                "%s",
                searchStatus.c_str()
            );
        }
        else
        {
            ImGui::TextColored(
                goodColor,
                "%s",
                searchStatus.c_str()
            );
        }
    }

    ImGui::Spacing();

    ImGui::TextDisabled(
        "Watch list and target prices are saved automatically."
    );
}
