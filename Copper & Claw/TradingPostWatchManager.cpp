#include "TradingPostWatchManager.h"

#include "TradingPostPriceManager.h"
#include "TradingPostHistoryManager.h"

#include <Windows.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace
{
    constexpr uint32_t
        AURENES_BITE_ITEM_ID =
        96356;

    constexpr int64_t
        WATCH_INTERVAL_SECONDS =
        60;

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

    std::filesystem::path
        g_SettingsPath;

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

    g_SettingsPath =
        BuildSettingsPath(
            moduleHandle
        );

    g_HasActiveTargetAlert =
        false;

    g_ActiveTargetAlert =
        TradingPostTargetAlert{};

    g_PendingTargetAlerts.clear();

    LoadLocked();

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
}

void TradingPostWatchManager::Shutdown()
{
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

    g_HasLastCheck =
        false;
}

void TradingPostWatchManager::Update()
{
    std::lock_guard<std::mutex> lock(
        g_WatchMutex
    );

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

    if (
        elapsedSeconds <
        WATCH_INTERVAL_SECONDS
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
}

void TradingPostWatchManager::RefreshItem(
    uint32_t itemID
)
{
    if (itemID == 0)
    {
        return;
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

    if (
        elapsedSeconds >=
        WATCH_INTERVAL_SECONDS
        )
    {
        return 0;
    }

    return
        WATCH_INTERVAL_SECONDS -
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
