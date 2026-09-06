#pragma once

struct CopperAndClawSettings
{
    // Trading Post trend window:
    // 0 = 15m
    // 1 = 30m
    // 2 = 1h
    // 3 = 6h
    // 4 = 24h
    // 5 = 3d
    // 6 = 7d
    // 7 = 30d
    // 8 = 90d
    int tradingPostTrendWindowIndex = 1;

    // Watch-list display sorting:
    // 0 = Added Order
    // 1 = Name A-Z
    // 2 = Closest to Target
    // 3 = Biggest Price Drop
    // 4 = Biggest Price Rise
    //
    // This changes display order only. The saved watch list
    // remains in its original added order.
    int tradingPostWatchSortMode = 0;
};

extern CopperAndClawSettings g_Settings;

namespace Settings
{
    bool Load(void* moduleHandle);
    bool Save(void* moduleHandle);
}
