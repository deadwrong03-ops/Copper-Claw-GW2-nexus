#include "TradingPostWatchManager.h"

#include "TradingPostPriceManager.h"
#include "TradingPostHistoryManager.h"
#include "Settings.h"

#include <Windows.h>
#include <winhttp.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#pragma comment(lib, "winhttp.lib")

namespace
{
    constexpr uint32_t
        AURENES_BITE_ITEM_ID =
        96356;

    constexpr int64_t
        FALLBACK_WATCH_INTERVAL_SECONDS =
        60;

    constexpr int64_t
        COLLECTOR_WATCH_INTERVAL_SECONDS =
        5 * 60;

    constexpr int64_t
        WATCHLIST_SYNC_PULL_INTERVAL_SECONDS =
        30;

    //
    // Protect manual Refresh / Refresh All from rapid repeated clicking.
    // One manual refresh action is allowed every 10 seconds across the addon.
    //
    constexpr int64_t
        MANUAL_REFRESH_COOLDOWN_SECONDS =
        10;

    struct TargetAlertState
    {
        //
        // Sell and Buy targets arm independently.
        //
        // Sell re-arms after a later observed sell listing rises
        // back ABOVE its target.
        //
        // Buy re-arms after a later observed buy order falls back
        // BELOW its target.
        //
        bool sellReachedLatch = false;
        bool buyReachedLatch = false;

        //
        // Keep independent processed timestamps so editing one target
        // cannot suppress evaluation of the other side of the market.
        //
        uint64_t lastProcessedSellPriceTimestamp = 0;
        uint64_t lastProcessedBuyPriceTimestamp = 0;
    };

    std::mutex g_WatchMutex;

    std::vector<
        TradingPostWatchItem
    > g_WatchedItems;

    std::unordered_map<
        uint32_t,
        TargetAlertState
    > g_TargetAlertStates;

    TradingPostTargetAlert
        g_ActiveTargetAlert;

    bool g_HasActiveTargetAlert = false;

    std::vector<
        TradingPostTargetAlert
    > g_PendingTargetAlerts;

    bool g_AutoWatchEnabled = true;

    bool g_HasLastCheck = false;

    std::chrono::steady_clock::
        time_point g_LastCheckTime;

    bool g_HasLastManualRefresh = false;

    std::chrono::steady_clock::
        time_point g_LastManualRefreshTime;

    std::filesystem::path
        g_SettingsPath;

    void*
        g_ModuleHandle = nullptr;

    std::thread
        g_SyncPullThread;

    std::atomic<bool>
        g_SyncPullStopRequested{
            false
    };

    std::mutex
        g_SyncPullWakeMutex;

    std::condition_variable
        g_SyncPullWakeCondition;

    std::mutex
        g_SyncPullResultMutex;

    std::string
        g_PendingSyncPullResponse;

    bool
        g_HasPendingSyncPullResponse =
        false;

    uint64_t GetCurrentUnixSeconds()
    {
        return
            static_cast<uint64_t>(
                std::chrono::duration_cast<
                std::chrono::seconds
                >(
                    std::chrono::
                    system_clock::now().
                    time_since_epoch()
                ).count()
                );
    }

    bool TryBeginManualRefreshLocked()
    {
        const auto now =
            std::chrono::steady_clock::
            now();

        if (g_HasLastManualRefresh)
        {
            const int64_t elapsedSeconds =
                std::chrono::
                duration_cast<
                std::chrono::seconds
                >(
                    now -
                    g_LastManualRefreshTime
                ).count();

            if (
                elapsedSeconds <
                MANUAL_REFRESH_COOLDOWN_SECONDS
                )
            {
                return false;
            }
        }

        g_LastManualRefreshTime =
            now;

        g_HasLastManualRefresh =
            true;

        return true;
    }

    std::filesystem::path
        BuildSettingsPath(
            void* moduleHandle
        )
    {
        wchar_t modulePath[
            MAX_PATH
        ] = {};

            const DWORD length =
                GetModuleFileNameW(
                    static_cast<HMODULE>(
                        moduleHandle
                        ),
                    modulePath,
                    MAX_PATH
                );

            if (length == 0)
            {
                return {};
            }

            std::filesystem::path path(
                modulePath
            );

            return
                path.parent_path() /
                L"CopperAndClaw_TradingPostWatch.ini";
    }

    void AddFirstRunStarterItemLocked()
    {
        TradingPostWatchItem item;

        item.itemID =
            AURENES_BITE_ITEM_ID;

        item.name =
            "Aurene's Bite";

        //
        // Aurene's Bite is only a first-run starter/example item.
        // It is not permanent and behaves exactly like any other
        // watched item after it has been created.
        //
        item.isDefault =
            false;

        g_WatchedItems.push_back(
            item
        );

        g_TargetAlertStates[
            AURENES_BITE_ITEM_ID
        ];
    }

    void PromoteNextTargetAlertLocked()
    {
        if (g_HasActiveTargetAlert)
        {
            return;
        }

        if (g_PendingTargetAlerts.empty())
        {
            g_ActiveTargetAlert =
                TradingPostTargetAlert{};

            return;
        }

        g_ActiveTargetAlert =
            g_PendingTargetAlerts.front();

        g_PendingTargetAlerts.erase(
            g_PendingTargetAlerts.begin()
        );

        g_HasActiveTargetAlert =
            true;
    }

    void QueueTargetAlertLocked(
        const TradingPostTargetAlert& alert
    )
    {
        if (alert.itemID == 0)
        {
            return;
        }

        if (
            g_HasActiveTargetAlert &&
            g_ActiveTargetAlert.itemID ==
            alert.itemID &&
            g_ActiveTargetAlert.type ==
            alert.type
            )
        {
            return;
        }

        const auto queuedIt =
            std::find_if(
                g_PendingTargetAlerts.begin(),
                g_PendingTargetAlerts.end(),
                [&alert](
                    const TradingPostTargetAlert& queuedAlert
                    )
                {
                    return
                        queuedAlert.itemID ==
                        alert.itemID &&
                        queuedAlert.type ==
                        alert.type;
                }
            );

        if (
            queuedIt !=
            g_PendingTargetAlerts.end()
            )
        {
            return;
        }

        if (!g_HasActiveTargetAlert)
        {
            g_ActiveTargetAlert =
                alert;

            g_HasActiveTargetAlert =
                true;

            return;
        }

        g_PendingTargetAlerts.push_back(
            alert
        );
    }

    void RemoveQueuedTargetAlertLocked(
        uint32_t itemID
    )
    {
        g_PendingTargetAlerts.erase(
            std::remove_if(
                g_PendingTargetAlerts.begin(),
                g_PendingTargetAlerts.end(),
                [itemID](
                    const TradingPostTargetAlert& alert
                    )
                {
                    return
                        alert.itemID ==
                        itemID;
                }
            ),
            g_PendingTargetAlerts.end()
        );
    }

    void RemoveQueuedTargetAlertLocked(
        uint32_t itemID,
        TradingPostTargetAlertType type
    )
    {
        g_PendingTargetAlerts.erase(
            std::remove_if(
                g_PendingTargetAlerts.begin(),
                g_PendingTargetAlerts.end(),
                [itemID, type](
                    const TradingPostTargetAlert& alert
                    )
                {
                    return
                        alert.itemID == itemID &&
                        alert.type == type;
                }
            ),
            g_PendingTargetAlerts.end()
        );
    }

    std::string JsonEscape(
        const std::string& value
    )
    {
        static constexpr char
            HEX[] =
            "0123456789ABCDEF";

        std::string escaped;

        escaped.reserve(
            value.size() + 8
        );

        for (
            const unsigned char ch :
        value
            )
        {
            switch (ch)
            {
            case '"':
                escaped += "\\\"";
                break;

            case '\\':
                escaped += "\\\\";
                break;

            case '\b':
                escaped += "\\b";
                break;

            case '\f':
                escaped += "\\f";
                break;

            case '\n':
                escaped += "\\n";
                break;

            case '\r':
                escaped += "\\r";
                break;

            case '\t':
                escaped += "\\t";
                break;

            default:
                if (ch < 0x20)
                {
                    escaped += "\\u00";
                    escaped +=
                        HEX[
                            (ch >> 4) &
                                0x0F
                        ];
                    escaped +=
                        HEX[
                            ch &
                                0x0F
                        ];
                }
                else
                {
                    escaped +=
                        static_cast<char>(
                            ch
                            );
                }

                break;
            }
        }

        return escaped;
    }

    std::string
        BuildWatchlistSyncJsonLocked()
    {
        std::string json;

        json.reserve(
            256 +
            g_WatchedItems.size() *
            160
        );

        json +=
            "{\"autoWatch\":";

        json +=
            g_AutoWatchEnabled
            ? "true"
            : "false";

        json +=
            ",\"items\":[";

        for (
            size_t index = 0;
            index <
            g_WatchedItems.size();
            ++index
            )
        {
            if (index != 0)
            {
                json += ',';
            }

            const TradingPostWatchItem&
                item =
                g_WatchedItems[index];

            int groupIndex = 0;

            const auto groupIt =
                g_Settings.
                tradingPostWatchGroupAssignments.
                find(
                    item.itemID
                );

            if (
                groupIt !=
                g_Settings.
                tradingPostWatchGroupAssignments.
                end() &&
                groupIt->second >= 1 &&
                groupIt->second <= 3
                )
            {
                groupIndex =
                    groupIt->second;
            }

            json +=
                "{\"itemId\":";

            json +=
                std::to_string(
                    item.itemID
                );

            json +=
                ",\"name\":\"";

            json +=
                JsonEscape(
                    item.name
                );

            json +=
                "\",\"targetSellCopper\":";

            json +=
                std::to_string(
                    item.targetSellCopper
                );

            json +=
                ",\"targetBuyCopper\":";

            json +=
                std::to_string(
                    item.targetBuyCopper
                );

            json +=
                ",\"isFavorite\":";

            json +=
                item.isFavorite
                ? "true"
                : "false";

            json +=
                ",\"groupIndex\":";

            json +=
                std::to_string(
                    groupIndex
                );

            json += '}';
        }

        json += "]}";

        return json;
    }

    void SkipJsonWhitespace(
        const std::string& text,
        size_t& position
    )
    {
        while (
            position < text.size() &&
            std::isspace(
                static_cast<unsigned char>(
                    text[position]
                    )
            ) != 0
            )
        {
            ++position;
        }
    }

    bool TryParseHex4(
        const std::string& text,
        size_t position,
        uint32_t& value
    )
    {
        if (
            position + 4 >
            text.size()
            )
        {
            return false;
        }

        value = 0;

        for (
            size_t i = 0;
            i < 4;
            ++i
            )
        {
            const char ch =
                text[
                    position + i
                ];

            uint32_t digit = 0;

            if (
                ch >= '0' &&
                ch <= '9'
                )
            {
                digit =
                    static_cast<uint32_t>(
                        ch - '0'
                        );
            }
            else if (
                ch >= 'a' &&
                ch <= 'f'
                )
            {
                digit =
                    10u +
                    static_cast<uint32_t>(
                        ch - 'a'
                        );
            }
            else if (
                ch >= 'A' &&
                ch <= 'F'
                )
            {
                digit =
                    10u +
                    static_cast<uint32_t>(
                        ch - 'A'
                        );
            }
            else
            {
                return false;
            }

            value =
                (value << 4) |
                digit;
        }

        return true;
    }

    void AppendUtf8CodePoint(
        uint32_t codePoint,
        std::string& output
    )
    {
        if (codePoint <= 0x7Fu)
        {
            output +=
                static_cast<char>(
                    codePoint
                    );
        }
        else if (
            codePoint <=
            0x7FFu
            )
        {
            output +=
                static_cast<char>(
                    0xC0u |
                    (
                        codePoint >>
                        6
                        )
                    );

            output +=
                static_cast<char>(
                    0x80u |
                    (
                        codePoint &
                        0x3Fu
                        )
                    );
        }
        else if (
            codePoint <=
            0xFFFFu
            )
        {
            output +=
                static_cast<char>(
                    0xE0u |
                    (
                        codePoint >>
                        12
                        )
                    );

            output +=
                static_cast<char>(
                    0x80u |
                    (
                        (
                            codePoint >>
                            6
                            ) &
                        0x3Fu
                        )
                    );

            output +=
                static_cast<char>(
                    0x80u |
                    (
                        codePoint &
                        0x3Fu
                        )
                    );
        }
        else
        {
            output +=
                static_cast<char>(
                    0xF0u |
                    (
                        codePoint >>
                        18
                        )
                    );

            output +=
                static_cast<char>(
                    0x80u |
                    (
                        (
                            codePoint >>
                            12
                            ) &
                        0x3Fu
                        )
                    );

            output +=
                static_cast<char>(
                    0x80u |
                    (
                        (
                            codePoint >>
                            6
                            ) &
                        0x3Fu
                        )
                    );

            output +=
                static_cast<char>(
                    0x80u |
                    (
                        codePoint &
                        0x3Fu
                        )
                    );
        }
    }

    bool TryParseJsonStringAt(
        const std::string& text,
        size_t& position,
        std::string& value
    )
    {
        SkipJsonWhitespace(
            text,
            position
        );

        if (
            position >=
            text.size() ||
            text[position] != '"'
            )
        {
            return false;
        }

        ++position;

        value.clear();

        while (
            position <
            text.size()
            )
        {
            const char ch =
                text[position++];

            if (ch == '"')
            {
                return true;
            }

            if (ch != '\\')
            {
                value += ch;
                continue;
            }

            if (
                position >=
                text.size()
                )
            {
                return false;
            }

            const char escaped =
                text[position++];

            switch (escaped)
            {
            case '"':
                value += '"';
                break;

            case '\\':
                value += '\\';
                break;

            case '/':
                value += '/';
                break;

            case 'b':
                value += '\b';
                break;

            case 'f':
                value += '\f';
                break;

            case 'n':
                value += '\n';
                break;

            case 'r':
                value += '\r';
                break;

            case 't':
                value += '\t';
                break;

            case 'u':
            {
                uint32_t firstCodeUnit = 0;

                if (
                    !TryParseHex4(
                        text,
                        position,
                        firstCodeUnit
                    )
                    )
                {
                    return false;
                }

                position += 4;

                uint32_t codePoint =
                    firstCodeUnit;

                if (
                    firstCodeUnit >=
                    0xD800u &&
                    firstCodeUnit <=
                    0xDBFFu
                    )
                {
                    if (
                        position + 6 >
                        text.size() ||
                        text[position] != '\\' ||
                        text[position + 1] != 'u'
                        )
                    {
                        return false;
                    }

                    uint32_t secondCodeUnit = 0;

                    if (
                        !TryParseHex4(
                            text,
                            position + 2,
                            secondCodeUnit
                        ) ||
                        secondCodeUnit <
                        0xDC00u ||
                        secondCodeUnit >
                        0xDFFFu
                        )
                    {
                        return false;
                    }

                    position += 6;

                    codePoint =
                        0x10000u +
                        (
                            (
                                firstCodeUnit -
                                0xD800u
                                ) <<
                            10
                            ) +
                        (
                            secondCodeUnit -
                            0xDC00u
                            );
                }

                AppendUtf8CodePoint(
                    codePoint,
                    value
                );

                break;
            }

            default:
                return false;
            }
        }

        return false;
    }

    bool TryFindJsonFieldValueStart(
        const std::string& objectText,
        const std::string& key,
        size_t& valuePosition
    )
    {
        const std::string needle =
            "\"" +
            key +
            "\"";

        size_t keyPosition =
            objectText.find(
                needle
            );

        while (
            keyPosition !=
            std::string::npos
            )
        {
            size_t position =
                keyPosition +
                needle.size();

            SkipJsonWhitespace(
                objectText,
                position
            );

            if (
                position <
                objectText.size() &&
                objectText[position] == ':'
                )
            {
                ++position;

                SkipJsonWhitespace(
                    objectText,
                    position
                );

                valuePosition =
                    position;

                return true;
            }

            keyPosition =
                objectText.find(
                    needle,
                    keyPosition + 1
                );
        }

        return false;
    }

    bool TryGetJsonStringField(
        const std::string& objectText,
        const std::string& key,
        std::string& value
    )
    {
        size_t position = 0;

        if (
            !TryFindJsonFieldValueStart(
                objectText,
                key,
                position
            )
            )
        {
            return false;
        }

        return
            TryParseJsonStringAt(
                objectText,
                position,
                value
            );
    }

    bool TryGetJsonBoolField(
        const std::string& objectText,
        const std::string& key,
        bool& value
    )
    {
        size_t position = 0;

        if (
            !TryFindJsonFieldValueStart(
                objectText,
                key,
                position
            )
            )
        {
            return false;
        }

        if (
            objectText.compare(
                position,
                4,
                "true"
            ) == 0
            )
        {
            value = true;
            return true;
        }

        if (
            objectText.compare(
                position,
                5,
                "false"
            ) == 0
            )
        {
            value = false;
            return true;
        }

        return false;
    }

    bool TryGetJsonUnsignedField(
        const std::string& objectText,
        const std::string& key,
        uint64_t& value
    )
    {
        size_t position = 0;

        if (
            !TryFindJsonFieldValueStart(
                objectText,
                key,
                position
            )
            )
        {
            return false;
        }

        const size_t start =
            position;

        while (
            position <
            objectText.size() &&
            objectText[position] >= '0' &&
            objectText[position] <= '9'
            )
        {
            ++position;
        }

        if (position == start)
        {
            return false;
        }

        try
        {
            value =
                std::stoull(
                    objectText.substr(
                        start,
                        position - start
                    )
                );

            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    bool TryExtractJsonObject(
        const std::string& text,
        size_t& position,
        std::string& objectText
    )
    {
        SkipJsonWhitespace(
            text,
            position
        );

        if (
            position >=
            text.size() ||
            text[position] != '{'
            )
        {
            return false;
        }

        const size_t start =
            position;

        int depth = 0;
        bool inString = false;
        bool escaped = false;

        for (
            ;
            position <
            text.size();
            ++position
            )
        {
            const char ch =
                text[position];

            if (inString)
            {
                if (escaped)
                {
                    escaped = false;
                    continue;
                }

                if (ch == '\\')
                {
                    escaped = true;
                    continue;
                }

                if (ch == '"')
                {
                    inString = false;
                }

                continue;
            }

            if (ch == '"')
            {
                inString = true;
                continue;
            }

            if (ch == '{')
            {
                ++depth;
                continue;
            }

            if (ch == '}')
            {
                --depth;

                if (depth == 0)
                {
                    ++position;

                    objectText =
                        text.substr(
                            start,
                            position - start
                        );

                    return true;
                }

                if (depth < 0)
                {
                    return false;
                }
            }
        }

        return false;
    }

    bool TryParseWatchlistSyncResponseLocked(
        const std::string& response,
        bool& changed
    )
    {
        changed = false;

        bool remoteAutoWatch = true;

        if (
            !TryGetJsonBoolField(
                response,
                "autoWatch",
                remoteAutoWatch
            )
            )
        {
            return false;
        }

        size_t itemsPosition = 0;

        if (
            !TryFindJsonFieldValueStart(
                response,
                "items",
                itemsPosition
            )
            )
        {
            return false;
        }

        if (
            itemsPosition >=
            response.size() ||
            response[itemsPosition] != '['
            )
        {
            return false;
        }

        ++itemsPosition;

        std::vector<
            TradingPostWatchItem
        > remoteItems;

        std::unordered_map<
            uint32_t,
            int
        > remoteGroups;

        std::unordered_map<
            uint32_t,
            TargetAlertState
        > remoteAlertStates;

        while (true)
        {
            SkipJsonWhitespace(
                response,
                itemsPosition
            );

            if (
                itemsPosition >=
                response.size()
                )
            {
                return false;
            }

            if (
                response[itemsPosition] == ']'
                )
            {
                ++itemsPosition;
                break;
            }

            if (
                response[itemsPosition] == ','
                )
            {
                ++itemsPosition;
                continue;
            }

            std::string itemObject;

            if (
                !TryExtractJsonObject(
                    response,
                    itemsPosition,
                    itemObject
                )
                )
            {
                return false;
            }

            uint64_t itemID64 = 0;

            if (
                !TryGetJsonUnsignedField(
                    itemObject,
                    "itemId",
                    itemID64
                ) ||
                itemID64 == 0 ||
                itemID64 >
                0xFFFFFFFFull
                )
            {
                return false;
            }

            TradingPostWatchItem item;

            item.itemID =
                static_cast<uint32_t>(
                    itemID64
                    );

            std::string savedName;

            if (
                !TryGetJsonStringField(
                    itemObject,
                    "savedName",
                    savedName
                ) ||
                savedName.empty()
                )
            {
                if (
                    !TryGetJsonStringField(
                        itemObject,
                        "name",
                        savedName
                    )
                    )
                {
                    return false;
                }
            }

            item.name =
                savedName;

            uint64_t targetSell = 0;

            if (
                TryGetJsonUnsignedField(
                    itemObject,
                    "targetSellCopper",
                    targetSell
                )
                )
            {
                item.targetSellCopper =
                    targetSell;
            }

            uint64_t targetBuy = 0;

            if (
                TryGetJsonUnsignedField(
                    itemObject,
                    "targetBuyCopper",
                    targetBuy
                )
                )
            {
                item.targetBuyCopper =
                    targetBuy;
            }

            bool isFavorite = false;

            if (
                TryGetJsonBoolField(
                    itemObject,
                    "isFavorite",
                    isFavorite
                )
                )
            {
                item.isFavorite =
                    isFavorite;
            }

            item.isDefault =
                false;

            uint64_t groupIndex64 = 0;

            if (
                TryGetJsonUnsignedField(
                    itemObject,
                    "groupIndex",
                    groupIndex64
                ) &&
                groupIndex64 >= 1 &&
                groupIndex64 <= 3
                )
            {
                remoteGroups[
                    item.itemID
                ] =
                    static_cast<int>(
                        groupIndex64
                        );
            }

            TargetAlertState newAlertState;

            const auto oldItemIt =
                std::find_if(
                    g_WatchedItems.begin(),
                    g_WatchedItems.end(),
                    [
                        &item
                    ](
                        const TradingPostWatchItem&
                        oldItem
                        )
                    {
                        return
                            oldItem.itemID ==
                            item.itemID;
                    }
                            );

            const auto oldStateIt =
                g_TargetAlertStates.find(
                    item.itemID
                );

            if (
                oldItemIt !=
                g_WatchedItems.end() &&
                oldStateIt !=
                g_TargetAlertStates.end()
                )
            {
                if (
                    oldItemIt->
                    targetSellCopper ==
                    item.targetSellCopper
                    )
                {
                    newAlertState.
                        sellReachedLatch =
                        oldStateIt->
                        second.
                        sellReachedLatch;
                }

                if (
                    oldItemIt->
                    targetBuyCopper ==
                    item.targetBuyCopper
                    )
                {
                    newAlertState.
                        buyReachedLatch =
                        oldStateIt->
                        second.
                        buyReachedLatch;
                }
            }

            remoteAlertStates[
                item.itemID
            ] =
                newAlertState;

                remoteItems.push_back(
                    item
                );
        }

        bool sameItems =
            remoteItems.size() ==
            g_WatchedItems.size();

        if (sameItems)
        {
            for (
                size_t index = 0;
                index <
                remoteItems.size();
                ++index
                )
            {
                const TradingPostWatchItem&
                    remoteItem =
                    remoteItems[index];

                const TradingPostWatchItem&
                    localItem =
                    g_WatchedItems[index];

                if (
                    remoteItem.itemID !=
                    localItem.itemID ||
                    remoteItem.name !=
                    localItem.name ||
                    remoteItem.targetSellCopper !=
                    localItem.targetSellCopper ||
                    remoteItem.targetBuyCopper !=
                    localItem.targetBuyCopper ||
                    remoteItem.isFavorite !=
                    localItem.isFavorite
                    )
                {
                    sameItems = false;
                    break;
                }
            }
        }

        const bool sameGroups =
            remoteGroups ==
            g_Settings.
            tradingPostWatchGroupAssignments;

        changed =
            remoteAutoWatch !=
            g_AutoWatchEnabled ||
            !sameItems ||
            !sameGroups;

        if (!changed)
        {
            return true;
        }

        g_AutoWatchEnabled =
            remoteAutoWatch;

        g_WatchedItems =
            std::move(
                remoteItems
            );

        g_TargetAlertStates =
            std::move(
                remoteAlertStates
            );

        g_Settings.
            tradingPostWatchGroupAssignments =
            std::move(
                remoteGroups
            );

        return true;
    }

    bool FetchWatchlistSyncResponse(
        std::string& response
    )
    {
        response.clear();

        if (
            g_Settings.
            collectorSyncProfileId.
            empty() ||
            g_Settings.
            collectorSyncKey.
            empty()
            )
        {
            return false;
        }

        HINTERNET session =
            WinHttpOpen(
                L"CopperAndClaw/1.0",
                WINHTTP_ACCESS_TYPE_NO_PROXY,
                WINHTTP_NO_PROXY_NAME,
                WINHTTP_NO_PROXY_BYPASS,
                0
            );

        if (session == nullptr)
        {
            return false;
        }

        WinHttpSetTimeouts(
            session,
            1500,
            1500,
            1500,
            1500
        );

        std::wstring collectorHost(
            g_Settings.
            collectorHost.begin(),
            g_Settings.
            collectorHost.end()
        );

        if (collectorHost.empty())
        {
            collectorHost =
                L"127.0.0.1";
        }

        const INTERNET_PORT
            collectorPort =
            static_cast<INTERNET_PORT>(
                g_Settings.
                collectorPort
                );

        HINTERNET connection =
            WinHttpConnect(
                session,
                collectorHost.c_str(),
                collectorPort,
                0
            );

        if (connection == nullptr)
        {
            WinHttpCloseHandle(
                session
            );

            return false;
        }

        const DWORD requestFlags =
            g_Settings.
            collectorUseHttps
            ? WINHTTP_FLAG_SECURE
            : 0;

        HINTERNET request =
            WinHttpOpenRequest(
                connection,
                L"GET",
                L"/watchlist",
                nullptr,
                WINHTTP_NO_REFERER,
                WINHTTP_DEFAULT_ACCEPT_TYPES,
                requestFlags
            );

        if (request == nullptr)
        {
            WinHttpCloseHandle(
                connection
            );

            WinHttpCloseHandle(
                session
            );

            return false;
        }

        const std::wstring
            profileId(
                g_Settings.
                collectorSyncProfileId.
                begin(),
                g_Settings.
                collectorSyncProfileId.
                end()
            );

        const std::wstring
            syncKey(
                g_Settings.
                collectorSyncKey.
                begin(),
                g_Settings.
                collectorSyncKey.
                end()
            );

        std::wstring headers =
            L"X-CopperClaw-Profile-Id: ";

        headers +=
            profileId;

        headers +=
            L"\r\n"
            L"X-CopperClaw-Sync-Key: ";

        headers +=
            syncKey;

        headers +=
            L"\r\n";

        const BOOL sent =
            WinHttpSendRequest(
                request,
                headers.c_str(),
                static_cast<DWORD>(
                    -1L
                    ),
                WINHTTP_NO_REQUEST_DATA,
                0,
                0,
                0
            );

        if (
            !sent ||
            !WinHttpReceiveResponse(
                request,
                nullptr
            )
            )
        {
            WinHttpCloseHandle(
                request
            );

            WinHttpCloseHandle(
                connection
            );

            WinHttpCloseHandle(
                session
            );

            return false;
        }

        DWORD statusCode = 0;

        DWORD statusCodeSize =
            sizeof(statusCode);

        const BOOL statusRead =
            WinHttpQueryHeaders(
                request,
                WINHTTP_QUERY_STATUS_CODE |
                WINHTTP_QUERY_FLAG_NUMBER,
                WINHTTP_HEADER_NAME_BY_INDEX,
                &statusCode,
                &statusCodeSize,
                WINHTTP_NO_HEADER_INDEX
            );

        if (
            !statusRead ||
            statusCode != 200
            )
        {
            WinHttpCloseHandle(
                request
            );

            WinHttpCloseHandle(
                connection
            );

            WinHttpCloseHandle(
                session
            );

            return false;
        }

        while (true)
        {
            DWORD available = 0;

            if (
                !WinHttpQueryDataAvailable(
                    request,
                    &available
                )
                )
            {
                WinHttpCloseHandle(
                    request
                );

                WinHttpCloseHandle(
                    connection
                );

                WinHttpCloseHandle(
                    session
                );

                return false;
            }

            if (available == 0)
            {
                break;
            }

            std::vector<char> buffer(
                available
            );

            DWORD bytesRead = 0;

            if (
                !WinHttpReadData(
                    request,
                    buffer.data(),
                    available,
                    &bytesRead
                )
                )
            {
                WinHttpCloseHandle(
                    request
                );

                WinHttpCloseHandle(
                    connection
                );

                WinHttpCloseHandle(
                    session
                );

                return false;
            }

            response.append(
                buffer.data(),
                bytesRead
            );
        }

        WinHttpCloseHandle(
            request
        );

        WinHttpCloseHandle(
            connection
        );

        WinHttpCloseHandle(
            session
        );

        return true;
    }

    bool PullWatchlistSyncLocked(
        bool& changed
    )
    {
        std::string response;

        if (
            !FetchWatchlistSyncResponse(
                response
            )
            )
        {
            changed = false;
            return false;
        }

        return
            TryParseWatchlistSyncResponseLocked(
                response,
                changed
            );
    }

    void SyncPullWorker()
    {
        while (
            !g_SyncPullStopRequested.
            load()
            )
        {
            std::unique_lock<std::mutex>
                wakeLock(
                    g_SyncPullWakeMutex
                );

            const bool stopRequested =
                g_SyncPullWakeCondition.
                wait_for(
                    wakeLock,
                    std::chrono::seconds(
                        WATCHLIST_SYNC_PULL_INTERVAL_SECONDS
                    ),
                    []()
                    {
                        return
                            g_SyncPullStopRequested.
                            load();
                    }
                );

            wakeLock.unlock();

            if (
                stopRequested ||
                g_SyncPullStopRequested.
                load()
                )
            {
                break;
            }

            std::string response;

            if (
                !FetchWatchlistSyncResponse(
                    response
                )
                )
            {
                continue;
            }

            {
                std::lock_guard<std::mutex>
                    resultLock(
                        g_SyncPullResultMutex
                    );

                g_PendingSyncPullResponse =
                    std::move(
                        response
                    );

                g_HasPendingSyncPullResponse =
                    true;
            }
        }
    }

    bool PushWatchlistSyncLocked()
    {
        if (
            g_Settings.
            collectorSyncProfileId.
            empty() ||
            g_Settings.
            collectorSyncKey.
            empty()
            )
        {
            return false;
        }

        const std::string requestBody =
            BuildWatchlistSyncJsonLocked();

        HINTERNET session =
            WinHttpOpen(
                L"CopperAndClaw/1.0",
                WINHTTP_ACCESS_TYPE_NO_PROXY,
                WINHTTP_NO_PROXY_NAME,
                WINHTTP_NO_PROXY_BYPASS,
                0
            );

        if (session == nullptr)
        {
            return false;
        }

        //
        // This first sync step is intentionally conservative and
        // synchronous. Watch-list edits are rare, and short timeouts keep
        // a failed sync from hanging the game for long. Once push/pull
        // behavior is validated end-to-end, this can move onto a worker
        // without changing the wire format.
        //
        WinHttpSetTimeouts(
            session,
            1500,
            1500,
            1500,
            1500
        );

        std::wstring collectorHost(
            g_Settings.
            collectorHost.begin(),
            g_Settings.
            collectorHost.end()
        );

        if (collectorHost.empty())
        {
            collectorHost =
                L"127.0.0.1";
        }

        const INTERNET_PORT
            collectorPort =
            static_cast<INTERNET_PORT>(
                g_Settings.
                collectorPort
                );

        HINTERNET connection =
            WinHttpConnect(
                session,
                collectorHost.c_str(),
                collectorPort,
                0
            );

        if (connection == nullptr)
        {
            WinHttpCloseHandle(
                session
            );

            return false;
        }

        const DWORD requestFlags =
            g_Settings.
            collectorUseHttps
            ? WINHTTP_FLAG_SECURE
            : 0;

        HINTERNET request =
            WinHttpOpenRequest(
                connection,
                L"PUT",
                L"/watchlist",
                nullptr,
                WINHTTP_NO_REFERER,
                WINHTTP_DEFAULT_ACCEPT_TYPES,
                requestFlags
            );

        if (request == nullptr)
        {
            WinHttpCloseHandle(
                connection
            );

            WinHttpCloseHandle(
                session
            );

            return false;
        }

        const std::wstring
            profileId(
                g_Settings.
                collectorSyncProfileId.
                begin(),
                g_Settings.
                collectorSyncProfileId.
                end()
            );

        const std::wstring
            syncKey(
                g_Settings.
                collectorSyncKey.
                begin(),
                g_Settings.
                collectorSyncKey.
                end()
            );

        std::wstring headers =
            L"Content-Type: application/json\r\n"
            L"X-CopperClaw-Profile-Id: ";

        headers +=
            profileId;

        headers +=
            L"\r\n"
            L"X-CopperClaw-Sync-Key: ";

        headers +=
            syncKey;

        headers +=
            L"\r\n";

        const DWORD bodyLength =
            static_cast<DWORD>(
                requestBody.size()
                );

        const BOOL sent =
            WinHttpSendRequest(
                request,
                headers.c_str(),
                static_cast<DWORD>(
                    -1L
                    ),
                bodyLength == 0
                ? WINHTTP_NO_REQUEST_DATA
                : const_cast<char*>(
                    requestBody.data()
                    ),
                bodyLength,
                bodyLength,
                0
            );

        if (
            !sent ||
            !WinHttpReceiveResponse(
                request,
                nullptr
            )
            )
        {
            WinHttpCloseHandle(
                request
            );

            WinHttpCloseHandle(
                connection
            );

            WinHttpCloseHandle(
                session
            );

            return false;
        }

        DWORD statusCode = 0;

        DWORD statusCodeSize =
            sizeof(statusCode);

        const BOOL statusRead =
            WinHttpQueryHeaders(
                request,
                WINHTTP_QUERY_STATUS_CODE |
                WINHTTP_QUERY_FLAG_NUMBER,
                WINHTTP_HEADER_NAME_BY_INDEX,
                &statusCode,
                &statusCodeSize,
                WINHTTP_NO_HEADER_INDEX
            );

        WinHttpCloseHandle(
            request
        );

        WinHttpCloseHandle(
            connection
        );

        WinHttpCloseHandle(
            session
        );

        return
            statusRead &&
            statusCode == 200;
    }

    void SaveLocked()
    {
        if (g_SettingsPath.empty())
        {
            return;
        }

        std::ofstream file(
            g_SettingsPath,
            std::ios::trunc
        );

        if (!file.is_open())
        {
            return;
        }

        file
            << "autoWatch="
            << (
                g_AutoWatchEnabled
                ? 1
                : 0
                )
            << '\n';

        for (
            const TradingPostWatchItem&
            item :
            g_WatchedItems
            )
        {
            file
                << "item="
                << item.itemID
                << '|'
                << item.targetSellCopper
                << '|'
                << item.targetBuyCopper
                << '|'
                << item.name
                << '\n';

            if (item.isFavorite)
            {
                file
                    << "favorite="
                    << item.itemID
                    << '\n';
            }

            const auto alertStateIt =
                g_TargetAlertStates.find(
                    item.itemID
                );

            const bool sellReachedLatch =
                alertStateIt !=
                g_TargetAlertStates.end()
                ? alertStateIt->
                second.sellReachedLatch
                : false;

            const bool buyReachedLatch =
                alertStateIt !=
                g_TargetAlertStates.end()
                ? alertStateIt->
                second.buyReachedLatch
                : false;

            //
            // Backward-compatible extension of the existing alertLatch row:
            //
            // old: alertLatch=itemID|sellLatch
            // new: alertLatch=itemID|sellLatch|buyLatch
            //
            file
                << "alertLatch="
                << item.itemID
                << '|'
                << (
                    sellReachedLatch
                    ? 1
                    : 0
                    )
                << '|'
                << (
                    buyReachedLatch
                    ? 1
                    : 0
                    )
                << '\n';
        }
    }

    void LoadLocked()
    {
        g_WatchedItems.clear();
        g_TargetAlertStates.clear();

        std::unordered_map<
            uint32_t,
            bool
        > pendingFavoriteStates;

        const bool settingsFileExists =
            !g_SettingsPath.empty() &&
            std::filesystem::exists(
                g_SettingsPath
            );

        if (settingsFileExists)
        {
            std::ifstream file(
                g_SettingsPath
            );

            std::string line;

            while (
                file.is_open() &&
                std::getline(
                    file,
                    line
                )
                )
            {
                if (
                    line.rfind(
                        "autoWatch=",
                        0
                    ) == 0
                    )
                {
                    const std::string value =
                        line.substr(
                            10
                        );

                    g_AutoWatchEnabled =
                        value == "1" ||
                        value == "true";

                    continue;
                }

                if (
                    line.rfind(
                        "favorite=",
                        0
                    ) == 0
                    )
                {
                    try
                    {
                        const uint32_t itemID =
                            static_cast<uint32_t>(
                                std::stoul(
                                    line.substr(
                                        9
                                    )
                                )
                                );

                        if (itemID != 0)
                        {
                            pendingFavoriteStates[
                                itemID
                            ] = true;
                        }
                    }
                    catch (...)
                    {
                        // Ignore malformed saved favorite rows.
                    }

                    continue;
                }

                if (
                    line.rfind(
                        "alertLatch=",
                        0
                    ) == 0
                    )
                {
                    const std::string payload =
                        line.substr(
                            11
                        );

                    const size_t firstSeparator =
                        payload.find(
                            '|'
                        );

                    if (
                        firstSeparator ==
                        std::string::npos
                        )
                    {
                        continue;
                    }

                    const size_t secondSeparator =
                        payload.find(
                            '|',
                            firstSeparator + 1
                        );

                    try
                    {
                        const uint32_t itemID =
                            static_cast<uint32_t>(
                                std::stoul(
                                    payload.substr(
                                        0,
                                        firstSeparator
                                    )
                                )
                                );

                        const std::string sellValue =
                            secondSeparator ==
                            std::string::npos
                            ? payload.substr(
                                firstSeparator + 1
                            )
                            : payload.substr(
                                firstSeparator + 1,
                                secondSeparator -
                                firstSeparator - 1
                            );

                        const std::string buyValue =
                            secondSeparator ==
                            std::string::npos
                            ? std::string()
                            : payload.substr(
                                secondSeparator + 1
                            );

                        if (itemID != 0)
                        {
                            TargetAlertState& state =
                                g_TargetAlertStates[
                                    itemID
                                ];

                            state.sellReachedLatch =
                                sellValue == "1" ||
                                sellValue == "true";

                            state.buyReachedLatch =
                                buyValue == "1" ||
                                buyValue == "true";
                        }
                    }
                    catch (...)
                    {
                        // Ignore malformed saved rows.
                    }

                    continue;
                }

                if (
                    line.rfind(
                        "item=",
                        0
                    ) != 0
                    )
                {
                    continue;
                }

                const std::string payload =
                    line.substr(
                        5
                    );

                const size_t firstSeparator =
                    payload.find(
                        '|'
                    );

                if (
                    firstSeparator ==
                    std::string::npos
                    )
                {
                    continue;
                }

                const size_t secondSeparator =
                    payload.find(
                        '|',
                        firstSeparator + 1
                    );

                if (
                    secondSeparator ==
                    std::string::npos
                    )
                {
                    continue;
                }

                const size_t thirdSeparator =
                    payload.find(
                        '|',
                        secondSeparator + 1
                    );

                try
                {
                    TradingPostWatchItem item;

                    item.itemID =
                        static_cast<uint32_t>(
                            std::stoul(
                                payload.substr(
                                    0,
                                    firstSeparator
                                )
                            )
                            );

                    item.targetSellCopper =
                        std::stoull(
                            payload.substr(
                                firstSeparator + 1,
                                secondSeparator -
                                firstSeparator - 1
                            )
                        );

                    //
                    // Backward compatibility:
                    //
                    // old: item=itemID|sellTarget|name
                    // new: item=itemID|sellTarget|buyTarget|name
                    //
                    if (
                        thirdSeparator ==
                        std::string::npos
                        )
                    {
                        item.targetBuyCopper =
                            0;

                        item.name =
                            payload.substr(
                                secondSeparator + 1
                            );
                    }
                    else
                    {
                        item.targetBuyCopper =
                            std::stoull(
                                payload.substr(
                                    secondSeparator + 1,
                                    thirdSeparator -
                                    secondSeparator - 1
                                )
                            );

                        item.name =
                            payload.substr(
                                thirdSeparator + 1
                            );
                    }

                    //
                    // No watched item is permanently protected anymore.
                    // Existing saved Aurene's Bite entries therefore become
                    // removable automatically on the next load.
                    //
                    item.isDefault =
                        false;

                    if (item.itemID != 0)
                    {
                        g_WatchedItems.push_back(
                            item
                        );

                        g_TargetAlertStates[
                            item.itemID
                        ];
                    }
                }
                catch (...)
                {
                    // Ignore malformed saved rows.
                }

            }

            for (
                TradingPostWatchItem& item :
                g_WatchedItems
                )
            {
                const auto favoriteIt =
                    pendingFavoriteStates.find(
                        item.itemID
                    );

                item.isFavorite =
                    favoriteIt !=
                    pendingFavoriteStates.end() &&
                    favoriteIt->second;
            }
        }
        else
        {
            //
            // Genuine first run only: seed one useful example so the
            // addon immediately demonstrates live pricing/history.
            //
            AddFirstRunStarterItemLocked();

            //
            // Persist the starter immediately. If the user later removes it,
            // the existing settings file proves initialization already
            // happened, so it will not be recreated on future restarts.
            //
            SaveLocked();
        }
    }

    void QueueAllLocked(
        bool forceRefresh
    )
    {
        for (
            const TradingPostWatchItem&
            item :
            g_WatchedItems
            )
        {
            TradingPostPriceManager::
                RequestPrice(
                    item.itemID,
                    forceRefresh
                );
        }
    }

    void EvaluateTargetAlertsLocked()
    {
        bool settingsChanged = false;

        for (
            const TradingPostWatchItem&
            item :
            g_WatchedItems
            )
        {
            TradingPostPrice price;

            if (
                !TradingPostPriceManager::
                TryGetPrice(
                    item.itemID,
                    price
                ) ||
                !price.available ||
                price.lastUpdatedUnixSeconds == 0
                )
            {
                continue;
            }

            TargetAlertState& alertState =
                g_TargetAlertStates[
                    item.itemID
                ];

            //
            // SELL TARGET
            //
            if (
                price.lastUpdatedUnixSeconds >
                alertState.
                lastProcessedSellPriceTimestamp
                )
            {
                alertState.
                    lastProcessedSellPriceTimestamp =
                    price.lastUpdatedUnixSeconds;

                if (
                    item.targetSellCopper == 0 ||
                    price.sellUnitPrice == 0
                    )
                {
                    if (alertState.sellReachedLatch)
                    {
                        alertState.sellReachedLatch =
                            false;

                        settingsChanged = true;
                    }
                }
                else
                {
                    const bool targetReached =
                        static_cast<uint64_t>(
                            price.sellUnitPrice
                            ) <=
                        item.targetSellCopper;

                    if (targetReached)
                    {
                        if (!alertState.sellReachedLatch)
                        {
                            alertState.sellReachedLatch =
                                true;

                            settingsChanged = true;

                            TradingPostTargetAlert
                                targetAlert;

                            targetAlert.type =
                                TradingPostTargetAlertType::
                                Sell;

                            targetAlert.itemID =
                                item.itemID;

                            targetAlert.name =
                                item.name;

                            targetAlert.sellUnitPrice =
                                price.sellUnitPrice;

                            targetAlert.targetSellCopper =
                                item.targetSellCopper;

                            targetAlert.triggeredUnixSeconds =
                                GetCurrentUnixSeconds();

                            QueueTargetAlertLocked(
                                targetAlert
                            );
                        }
                    }
                    else
                    {
                        //
                        // Sell price moved back above target. Re-arm so a
                        // later drop back into range can create a new alert.
                        //
                        if (alertState.sellReachedLatch)
                        {
                            alertState.sellReachedLatch =
                                false;

                            settingsChanged = true;
                        }
                    }
                }
            }

            //
            // BUY TARGET
            //
            if (
                price.lastUpdatedUnixSeconds >
                alertState.
                lastProcessedBuyPriceTimestamp
                )
            {
                alertState.
                    lastProcessedBuyPriceTimestamp =
                    price.lastUpdatedUnixSeconds;

                if (
                    item.targetBuyCopper == 0 ||
                    price.buyUnitPrice == 0
                    )
                {
                    if (alertState.buyReachedLatch)
                    {
                        alertState.buyReachedLatch =
                            false;

                        settingsChanged = true;
                    }
                }
                else
                {
                    const bool targetReached =
                        static_cast<uint64_t>(
                            price.buyUnitPrice
                            ) >=
                        item.targetBuyCopper;

                    if (targetReached)
                    {
                        if (!alertState.buyReachedLatch)
                        {
                            alertState.buyReachedLatch =
                                true;

                            settingsChanged = true;

                            TradingPostTargetAlert
                                targetAlert;

                            targetAlert.type =
                                TradingPostTargetAlertType::
                                Buy;

                            targetAlert.itemID =
                                item.itemID;

                            targetAlert.name =
                                item.name;

                            targetAlert.buyUnitPrice =
                                price.buyUnitPrice;

                            targetAlert.targetBuyCopper =
                                item.targetBuyCopper;

                            targetAlert.triggeredUnixSeconds =
                                GetCurrentUnixSeconds();

                            QueueTargetAlertLocked(
                                targetAlert
                            );
                        }
                    }
                    else
                    {
                        //
                        // Buy price moved back below target. Re-arm so a
                        // later rise back into range can create a new alert.
                        //
                        if (alertState.buyReachedLatch)
                        {
                            alertState.buyReachedLatch =
                                false;

                            settingsChanged = true;
                        }
                    }
                }
            }
        }

        if (settingsChanged)
        {
            SaveLocked();
        }
    }

}

void TradingPostWatchManager::Start(
    void* moduleHandle
)
{
    std::lock_guard<std::mutex> lock(
        g_WatchMutex
    );

    g_ModuleHandle =
        moduleHandle;

    g_SettingsPath =
        BuildSettingsPath(
            moduleHandle
        );

    g_HasActiveTargetAlert =
        false;

    g_ActiveTargetAlert =
        TradingPostTargetAlert{};

    g_PendingTargetAlerts.clear();

    g_HasLastManualRefresh =
        false;

    LoadLocked();

    //
    // Startup-only sync pull for the first bidirectional test.
    // A failed request or malformed response leaves the local watchlist
    // untouched. A successful pull becomes the local source for this session.
    //
    bool startupSyncChanged = false;

    if (
        PullWatchlistSyncLocked(
            startupSyncChanged
        ) &&
        startupSyncChanged
        )
    {
        SaveLocked();

        //
        // Group assignments live in CopperAndClaw.ini rather than the
        // watch-list INI, so persist them through the main settings writer.
        //
        Settings::Save(
            moduleHandle
        );
    }

    for (
        const TradingPostWatchItem& item :
        g_WatchedItems
        )
    {
        TradingPostHistoryManager::
            RegisterWatchedItem(
                item.itemID
            );
    }

    QueueAllLocked(
        false
    );

    g_LastCheckTime =
        std::chrono::steady_clock::
        now();

    g_HasLastCheck =
        true;

    g_SyncPullStopRequested.
        store(
            false
        );

    {
        std::lock_guard<std::mutex>
            resultLock(
                g_SyncPullResultMutex
            );

        g_PendingSyncPullResponse.clear();

        g_HasPendingSyncPullResponse =
            false;
    }

    if (
        !g_SyncPullThread.
        joinable()
        )
    {
        g_SyncPullThread =
            std::thread(
                SyncPullWorker
            );
    }
}

void TradingPostWatchManager::Shutdown()
{
    g_SyncPullStopRequested.
        store(
            true
        );

    g_SyncPullWakeCondition.
        notify_all();

    if (
        g_SyncPullThread.
        joinable()
        )
    {
        g_SyncPullThread.
            join();
    }

    {
        std::lock_guard<std::mutex>
            resultLock(
                g_SyncPullResultMutex
            );

        g_PendingSyncPullResponse.clear();

        g_HasPendingSyncPullResponse =
            false;
    }

    std::lock_guard<std::mutex> lock(
        g_WatchMutex
    );

    SaveLocked();

    g_HasActiveTargetAlert =
        false;

    g_ActiveTargetAlert =
        TradingPostTargetAlert{};

    g_PendingTargetAlerts.clear();

    g_TargetAlertStates.clear();

    g_WatchedItems.clear();

    g_SettingsPath.clear();

    g_ModuleHandle =
        nullptr;

    g_HasLastCheck =
        false;

    g_HasLastManualRefresh =
        false;
}

void TradingPostWatchManager::Update()
{
    std::string pendingSyncResponse;

    {
        std::lock_guard<std::mutex>
            resultLock(
                g_SyncPullResultMutex
            );

        if (g_HasPendingSyncPullResponse)
        {
            pendingSyncResponse =
                std::move(
                    g_PendingSyncPullResponse
                );

            g_PendingSyncPullResponse.clear();

            g_HasPendingSyncPullResponse =
                false;
        }
    }

    std::lock_guard<std::mutex> lock(
        g_WatchMutex
    );

    if (!pendingSyncResponse.empty())
    {
        std::vector<uint32_t>
            previousItemIDs;

        previousItemIDs.reserve(
            g_WatchedItems.size()
        );

        for (
            const TradingPostWatchItem& item :
            g_WatchedItems
            )
        {
            previousItemIDs.push_back(
                item.itemID
            );
        }

        bool syncChanged = false;

        if (
            TryParseWatchlistSyncResponseLocked(
                pendingSyncResponse,
                syncChanged
            ) &&
            syncChanged
            )
        {
            //
            // Reconcile local history registrations when another client
            // adds or removes watched items.
            //
            for (
                const uint32_t oldItemID :
            previousItemIDs
                )
            {
                const auto stillWatchedIt =
                    std::find_if(
                        g_WatchedItems.begin(),
                        g_WatchedItems.end(),
                        [
                            oldItemID
                        ](
                            const TradingPostWatchItem&
                            item
                            )
                        {
                            return
                                item.itemID ==
                                oldItemID;
                        }
                                );

                if (
                    stillWatchedIt ==
                    g_WatchedItems.end()
                    )
                {
                    TradingPostHistoryManager::
                        UnregisterWatchedItem(
                            oldItemID
                        );

                    RemoveQueuedTargetAlertLocked(
                        oldItemID
                    );

                    if (
                        g_HasActiveTargetAlert &&
                        g_ActiveTargetAlert.
                        itemID ==
                        oldItemID
                        )
                    {
                        g_HasActiveTargetAlert =
                            false;

                        g_ActiveTargetAlert =
                            TradingPostTargetAlert{};

                        PromoteNextTargetAlertLocked();
                    }
                }
            }

            for (
                const TradingPostWatchItem& item :
                g_WatchedItems
                )
            {
                const bool wasPreviouslyWatched =
                    std::find(
                        previousItemIDs.begin(),
                        previousItemIDs.end(),
                        item.itemID
                    ) !=
                    previousItemIDs.end();

                if (!wasPreviouslyWatched)
                {
                    TradingPostHistoryManager::
                        RegisterWatchedItem(
                            item.itemID
                        );

                    TradingPostPriceManager::
                        RequestPrice(
                            item.itemID,
                            true
                        );
                }
            }

            SaveLocked();

            if (g_ModuleHandle != nullptr)
            {
                Settings::Save(
                    g_ModuleHandle
                );
            }
        }
    }

    //
    // This runs every render update so completed asynchronous price
    // requests are evaluated as soon as their new timestamp appears.
    //
    EvaluateTargetAlertsLocked();

    if (!g_AutoWatchEnabled)
    {
        return;
    }

    const auto now =
        std::chrono::steady_clock::
        now();

    if (!g_HasLastCheck)
    {
        g_LastCheckTime =
            now;

        g_HasLastCheck =
            true;

        QueueAllLocked(
            true
        );

        return;
    }

    const int64_t elapsedSeconds =
        std::chrono::
        duration_cast<
        std::chrono::seconds
        >(
            now -
            g_LastCheckTime
        ).count();

    const int64_t watchIntervalSeconds =
        TradingPostPriceManager::
        IsCollectorAvailable()
        ? COLLECTOR_WATCH_INTERVAL_SECONDS
        : FALLBACK_WATCH_INTERVAL_SECONDS;

    if (
        elapsedSeconds <
        watchIntervalSeconds
        )
    {
        return;
    }

    g_LastCheckTime =
        now;

    QueueAllLocked(
        true
    );
}

std::vector<
    TradingPostWatchItem
> TradingPostWatchManager::GetItems()
{
    std::lock_guard<std::mutex> lock(
        g_WatchMutex
    );

    return
        g_WatchedItems;
}

void TradingPostWatchManager::SetFavorite(
    uint32_t itemID,
    bool favorite
)
{
    if (itemID == 0)
    {
        return;
    }

    std::lock_guard<std::mutex> lock(
        g_WatchMutex
    );

    for (
        TradingPostWatchItem& item :
        g_WatchedItems
        )
    {
        if (item.itemID == itemID)
        {
            if (item.isFavorite != favorite)
            {
                item.isFavorite =
                    favorite;

                SaveLocked();
                PushWatchlistSyncLocked();
            }

            return;
        }
    }
}

bool TradingPostWatchManager::AddItem(
    uint32_t itemID,
    const std::string& name
)
{
    if (itemID == 0)
    {
        return false;
    }

    std::lock_guard<std::mutex> lock(
        g_WatchMutex
    );

    const auto it =
        std::find_if(
            g_WatchedItems.begin(),
            g_WatchedItems.end(),
            [
                itemID
            ](
                const TradingPostWatchItem&
                item
                )
            {
                return
                    item.itemID ==
                    itemID;
            }
                    );

    if (
        it !=
        g_WatchedItems.end()
        )
    {
        return false;
    }

    TradingPostWatchItem item;

    item.itemID =
        itemID;

    item.name =
        name;

    g_WatchedItems.push_back(
        item
    );

    g_TargetAlertStates[
        itemID
    ] = TargetAlertState{};

        SaveLocked();
        PushWatchlistSyncLocked();

        TradingPostHistoryManager::
            RegisterWatchedItem(
                itemID
            );

        TradingPostPriceManager::
            RequestPrice(
                itemID,
                true
            );

        return true;
}

void TradingPostWatchManager::RemoveItem(
    uint32_t itemID
)
{
    std::lock_guard<std::mutex> lock(
        g_WatchMutex
    );

    g_WatchedItems.erase(
        std::remove_if(
            g_WatchedItems.begin(),
            g_WatchedItems.end(),
            [
                itemID
            ](
                const TradingPostWatchItem&
                item
                )
            {
                return
                    item.itemID ==
                    itemID;
            }
                    ),
        g_WatchedItems.end()
    );

    g_TargetAlertStates.erase(
        itemID
    );

    RemoveQueuedTargetAlertLocked(
        itemID
    );

    if (
        g_HasActiveTargetAlert &&
        g_ActiveTargetAlert.itemID ==
        itemID
        )
    {
        g_HasActiveTargetAlert =
            false;

        g_ActiveTargetAlert =
            TradingPostTargetAlert{};

        PromoteNextTargetAlertLocked();
    }

    TradingPostHistoryManager::
        UnregisterWatchedItem(
            itemID
        );

    SaveLocked();
    PushWatchlistSyncLocked();
}

void TradingPostWatchManager::
SetTargetSellPrice(
    uint32_t itemID,
    uint64_t targetCopper
)
{
    std::lock_guard<std::mutex> lock(
        g_WatchMutex
    );

    for (
        TradingPostWatchItem& item :
        g_WatchedItems
        )
    {
        if (item.itemID == itemID)
        {
            if (
                item.targetSellCopper !=
                targetCopper
                )
            {
                item.targetSellCopper =
                    targetCopper;

                //
                // A changed Sell Target is a new alert condition, but the
                // edit itself must NOT fire from an already-cached price.
                //
                TargetAlertState& alertState =
                    g_TargetAlertStates[
                        itemID
                    ];

                alertState.sellReachedLatch =
                    false;

                TradingPostPrice currentPrice;

                if (
                    TradingPostPriceManager::
                    TryGetPrice(
                        itemID,
                        currentPrice
                    ) &&
                    currentPrice.available
                    )
                {
                    alertState.
                        lastProcessedSellPriceTimestamp =
                        currentPrice.
                        lastUpdatedUnixSeconds;
                }
                else
                {
                    alertState.
                        lastProcessedSellPriceTimestamp =
                        0;
                }

                RemoveQueuedTargetAlertLocked(
                    itemID,
                    TradingPostTargetAlertType::
                    Sell
                );

                if (
                    g_HasActiveTargetAlert &&
                    g_ActiveTargetAlert.itemID ==
                    itemID &&
                    g_ActiveTargetAlert.type ==
                    TradingPostTargetAlertType::
                    Sell
                    )
                {
                    g_HasActiveTargetAlert =
                        false;

                    g_ActiveTargetAlert =
                        TradingPostTargetAlert{};

                    PromoteNextTargetAlertLocked();
                }

                SaveLocked();
                PushWatchlistSyncLocked();
            }

            return;
        }
    }
}

void TradingPostWatchManager::
SetTargetBuyPrice(
    uint32_t itemID,
    uint64_t targetCopper
)
{
    std::lock_guard<std::mutex> lock(
        g_WatchMutex
    );

    for (
        TradingPostWatchItem& item :
        g_WatchedItems
        )
    {
        if (item.itemID == itemID)
        {
            if (
                item.targetBuyCopper !=
                targetCopper
                )
            {
                item.targetBuyCopper =
                    targetCopper;

                //
                // A changed Buy Target is a new alert condition, but the
                // edit itself must NOT fire from an already-cached price.
                //
                TargetAlertState& alertState =
                    g_TargetAlertStates[
                        itemID
                    ];

                alertState.buyReachedLatch =
                    false;

                TradingPostPrice currentPrice;

                if (
                    TradingPostPriceManager::
                    TryGetPrice(
                        itemID,
                        currentPrice
                    ) &&
                    currentPrice.available
                    )
                {
                    alertState.
                        lastProcessedBuyPriceTimestamp =
                        currentPrice.
                        lastUpdatedUnixSeconds;
                }
                else
                {
                    alertState.
                        lastProcessedBuyPriceTimestamp =
                        0;
                }

                RemoveQueuedTargetAlertLocked(
                    itemID,
                    TradingPostTargetAlertType::
                    Buy
                );

                if (
                    g_HasActiveTargetAlert &&
                    g_ActiveTargetAlert.itemID ==
                    itemID &&
                    g_ActiveTargetAlert.type ==
                    TradingPostTargetAlertType::
                    Buy
                    )
                {
                    g_HasActiveTargetAlert =
                        false;

                    g_ActiveTargetAlert =
                        TradingPostTargetAlert{};

                    PromoteNextTargetAlertLocked();
                }

                SaveLocked();
                PushWatchlistSyncLocked();
            }

            return;
        }
    }
}

bool TradingPostWatchManager::
IsAutoWatchEnabled()
{
    std::lock_guard<std::mutex> lock(
        g_WatchMutex
    );

    return
        g_AutoWatchEnabled;
}

void TradingPostWatchManager::
SetAutoWatchEnabled(
    bool enabled
)
{
    std::lock_guard<std::mutex> lock(
        g_WatchMutex
    );

    g_AutoWatchEnabled =
        enabled;

    g_LastCheckTime =
        std::chrono::steady_clock::
        now();

    g_HasLastCheck =
        true;

    SaveLocked();
    PushWatchlistSyncLocked();
}

void TradingPostWatchManager::RefreshItem(
    uint32_t itemID
)
{
    if (itemID == 0)
    {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(
            g_WatchMutex
        );

        if (!TryBeginManualRefreshLocked())
        {
            return;
        }
    }

    TradingPostPriceManager::
        RequestPrice(
            itemID,
            true
        );
}

void TradingPostWatchManager::RefreshAll()
{
    std::lock_guard<std::mutex> lock(
        g_WatchMutex
    );

    if (!TryBeginManualRefreshLocked())
    {
        return;
    }

    QueueAllLocked(
        true
    );

    g_LastCheckTime =
        std::chrono::steady_clock::
        now();

    g_HasLastCheck =
        true;
}

int64_t TradingPostWatchManager::
GetSecondsUntilNextCheck()
{
    std::lock_guard<std::mutex> lock(
        g_WatchMutex
    );

    if (
        !g_AutoWatchEnabled ||
        !g_HasLastCheck
        )
    {
        return 0;
    }

    const int64_t elapsedSeconds =
        std::chrono::
        duration_cast<
        std::chrono::seconds
        >(
            std::chrono::steady_clock::
            now() -
            g_LastCheckTime
        ).count();

    const int64_t watchIntervalSeconds =
        TradingPostPriceManager::
        IsCollectorAvailable()
        ? COLLECTOR_WATCH_INTERVAL_SECONDS
        : FALLBACK_WATCH_INTERVAL_SECONDS;

    if (
        elapsedSeconds >=
        watchIntervalSeconds
        )
    {
        return 0;
    }

    return
        watchIntervalSeconds -
        elapsedSeconds;
}

bool TradingPostWatchManager::
TryGetActiveTargetAlert(
    TradingPostTargetAlert& outAlert
)
{
    std::lock_guard<std::mutex> lock(
        g_WatchMutex
    );

    if (!g_HasActiveTargetAlert)
    {
        return false;
    }

    outAlert =
        g_ActiveTargetAlert;

    return true;
}

void TradingPostWatchManager::
DismissActiveTargetAlert()
{
    std::lock_guard<std::mutex> lock(
        g_WatchMutex
    );

    g_HasActiveTargetAlert =
        false;

    g_ActiveTargetAlert =
        TradingPostTargetAlert{};

    PromoteNextTargetAlertLocked();
}
