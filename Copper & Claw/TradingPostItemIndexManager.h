#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

struct TradingPostIndexedItem
{
    uint32_t itemID = 0;
    std::string name;

    //
    // Optional distinguishing label taken from the item's stat metadata.
    // This is used for duplicate-name Trading Post items such as infusions
    // that have separate item IDs / listings for different attributes.
    //
    std::string variantLabel;

    //
    // Guild Wars 2 render URL returned by /v2/items.
    // Used by the UI to request the item's icon through Nexus.
    //
    std::string iconUrl;

    //
    // Small metadata set retained from /v2/items for hover tooltips.
    // itemSubtype is the details.type value when the item exposes one
    // (Food, Utility, LongBow, Rune, etc.).
    //
    std::string itemType;
    std::string itemSubtype;

    //
    // Consumable unlock subtype from details.unlock_type when present.
    // Used by the Watchlist automatic category filter to distinguish dyes
    // from other consumables without any account/API-key access.
    //
    std::string unlockType;

    std::string rarity;
    uint32_t level = 0;

    //
    // Dye metadata from details.color_id plus the Guild Wars 2 /v2/colors
    // cloth RGB preview. These stay in the local item-index cache so the UI
    // never performs a network request while rendering.
    //
    uint32_t colorId = 0;
    uint8_t dyeColorRed = 0;
    uint8_t dyeColorGreen = 0;
    uint8_t dyeColorBlue = 0;
    bool dyeColorAvailable = false;

    //
    // Preformatted player-facing detail summary built from the public
    // /v2/items metadata. This includes whichever useful fields exist for the
    // item type: descriptions, stats, weapon strength, defense, durations,
    // upgrade information, restrictions, vendor value, and similar details.
    //
    std::string tooltipText;
};

namespace TradingPostItemIndexManager
{
    //
    // Loads the cached searchable Trading Post item index immediately,
    // then refreshes it in the background when the cache is missing or stale.
    //
    void Start(
        void* moduleHandle
    );

    void Shutdown();

    //
    // Clears the in-memory index only.
    // The on-disk cache is left intact.
    //
    void Reset();

    //
    // Requests a fresh Trading Post item-index rebuild.
    // The current cached index remains searchable while the rebuild runs.
    //
    void RequestRefresh();

    //
    // Searches the local in-memory index.
    //
    // Matching is case-insensitive and favors:
    // exact match -> starts-with -> word-prefix -> contains.
    //
    // Variant labels are searchable too, so searches such as
    // "festive confetti concentration" can identify a specific stat version.
    //
    std::vector<TradingPostIndexedItem> Search(
        const std::string& query,
        size_t maxResults = 8
    );

    //
    // Looks up one indexed Trading Post item by item ID.
    // Used by the UI to retrieve icon, hover-tooltip, and automatic-category metadata.
    //
    bool TryGetItem(
        uint32_t itemID,
        TradingPostIndexedItem& outItem
    );

    bool IsReady();

    bool IsBuilding();

    size_t GetItemCount();

    size_t GetBuildProcessedCount();

    size_t GetBuildTotalCount();

    std::string GetLastError();
}
