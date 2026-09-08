#pragma once

#include <cstdint>

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
    // 5 = Most Volatile
    // 6 = Least Volatile
    //
    // This changes display order only. The saved watch list
    // remains in its original added order.
    int tradingPostWatchSortMode = 0;

    // Trading Post watch-list view density:
    // 0 = Normal
    // 1 = Slim
    // 2 = Ultra Slim
    //
    // Slim is the current/default layout.
    int tradingPostViewDensityIndex = 1;

    // Automatic backup settings:
    // interval:
    // 0 = 6h
    // 1 = 12h
    // 2 = 24h
    // 3 = 3d
    // 4 = 7d
    bool backupAutoEnabled = true;
    int backupIntervalIndex = 2;

    // Retained backup count:
    // 0 = 5
    // 1 = 10
    // 2 = 20
    int backupKeepCountIndex = 1;

    //
    // Packed addon version for which the one-time post-update
    // item-index notice has already been acknowledged.
    //
    uint64_t acknowledgedUpdateNoticeVersion = 0;
};

extern CopperAndClawSettings g_Settings;

namespace Settings
{
    bool Load(void* moduleHandle);
    bool Save(void* moduleHandle);
}
