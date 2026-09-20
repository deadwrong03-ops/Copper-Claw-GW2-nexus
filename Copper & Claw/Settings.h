#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>

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
    // 8 = 3mo
    // 9 = 6mo
    // 10 = 1y
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

    //
    // Watch-list group assignments.
    //
    // Key   = GW2 item ID
    // Value = 1 Investments, 2 Consumables, 3 Materials
    //
    // Ungrouped items are intentionally omitted.
    //
    std::unordered_map<uint32_t, int>
        tradingPostWatchGroupAssignments;

    //
    // Watch-list presentation:
    // 0 = List
    // 1 = Heatmap
    //
    // This only changes presentation. The same watched-item set,
    // groups, favorites, targets, and Item Analysis remain intact.
    //
    int tradingPostWatchDisplayMode = 0;

    //
    // Prowl Follow List.
    //
    // Key   = GW2 item ID
    // Value = expiration Unix timestamp
    //
    // A value of 0 means follow until manually removed.
    // Expired timed follows are cleaned up automatically by the UI.
    //
    std::unordered_map<uint32_t, uint64_t>
        tradingPostProwlFollowExpirations;

    // Trading Post watch-list view density:
    // 0 = Normal
    // 1 = Slim
    // 2 = Ultra Slim
    //
    // Slim is the current/default layout.
    int tradingPostViewDensityIndex = 1;

    // Trading Post visual theme:
    // 0 = Janthir Wilds
    // 1 = Cyberpunk 2077
    // 2 = Heart of Thorns
    // 3 = Path of Fire
    // 4 = End of Dragons
    // 5 = Secrets of the Obscure
    // 6 = Visions of Eternity
    // 7 = Living World Season 1
    // 8 = Living World Season 2
    // 9 = Living World Season 3
    // 10 = Living World Season 4
    // 11 = The Icebrood Saga
    //
    // Existing theme indices are intentionally preserved.
    // Janthir Wilds remains the default/release-candidate theme.
    int tradingPostThemeIndex = 0;

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
    // Collector connection settings.
    //
    // Default to the hosted Copper&Claw Collector.
    //
    std::string collectorHost = "copperclaw-api.duckdns.org";
    uint16_t collectorPort = 443;
    bool collectorUseHttps = true;

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
