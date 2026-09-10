#pragma once

#include <cstdint>
#include <string>
#include <vector>

enum class TradingPostTargetAlertType
{
    Sell,
    Buy
};

struct TradingPostWatchItem
{
    uint32_t itemID = 0;
    std::string name;

    uint64_t targetSellCopper = 0;
    uint64_t targetBuyCopper = 0;

    //
    // Optional user favorite. Favorites do not change watch behavior;
    // they only allow the UI to show a clean Favorites-only view.
    //
    bool isFavorite = false;

    //
    // Retained for UI/source compatibility.
    // Copper&Claw no longer has a permanently protected watched item;
    // normal loaded/added items, including the first-run Aurene's Bite
    // starter item, use false.
    //
    bool isDefault = false;
};

struct TradingPostTargetAlert
{
    TradingPostTargetAlertType type =
        TradingPostTargetAlertType::Sell;

    uint32_t itemID = 0;

    std::string name;

    uint64_t sellUnitPrice = 0;
    uint64_t targetSellCopper = 0;

    uint64_t buyUnitPrice = 0;
    uint64_t targetBuyCopper = 0;

    uint64_t triggeredUnixSeconds = 0;
};

namespace TradingPostWatchManager
{
    void Start(
        void* moduleHandle
    );

    void Shutdown();

    void Update();

    std::vector<
        TradingPostWatchItem
    > GetItems();

    void SetFavorite(
        uint32_t itemID,
        bool favorite
    );

    bool AddItem(
        uint32_t itemID,
        const std::string& name
    );

    void RemoveItem(
        uint32_t itemID
    );

    void SetTargetSellPrice(
        uint32_t itemID,
        uint64_t targetCopper
    );

    void SetTargetBuyPrice(
        uint32_t itemID,
        uint64_t targetCopper
    );

    bool IsAutoWatchEnabled();

    void SetAutoWatchEnabled(
        bool enabled
    );

    //
    // Manual refreshes are globally rate-limited internally so rapid
    // repeated button presses cannot generate request bursts.
    //
    void RefreshItem(
        uint32_t itemID
    );

    void RefreshAll();

    int64_t GetSecondsUntilNextCheck();

    //
    // Returns the currently visible target alert, if one is waiting
    // for the user to dismiss it. Additional simultaneous hits are
    // queued and shown one at a time.
    //
    bool TryGetActiveTargetAlert(
        TradingPostTargetAlert& outAlert
    );

    //
    // Dismisses the currently visible alert and immediately promotes
    // the next queued target hit, if one exists. This does NOT re-arm
    // a target. Sell targets re-arm only after a later sell price rises
    // above the target; Buy targets re-arm only after a later buy price
    // falls below the target.
    //
    void DismissActiveTargetAlert();
}
