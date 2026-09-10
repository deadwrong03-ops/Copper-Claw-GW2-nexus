#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

struct TradingPostHistoryPoint
{
    uint64_t timestampUnixSeconds = 0;
    uint32_t buyUnitPrice = 0;
    uint32_t sellUnitPrice = 0;
};

namespace TradingPostHistoryManager
{
    //
    // Loads persistent Trading Post history from disk.
    //
    void Start(
        void* moduleHandle
    );

    //
    // Stops history recording and clears in-memory state.
    // Persistent history on disk is preserved.
    //
    void Shutdown();

    //
    // Marks an item as currently watched.
    //
    // The first time an item is registered during the current
    // Copper&Claw session, the manager also makes one best-effort
    // request to the local Copper&Claw Collector and imports any
    // historical points that are not already stored locally.
    //
    // If the Collector is unavailable, registration still succeeds
    // and local history recording continues normally.
    //
    void RegisterWatchedItem(
        uint32_t itemID
    );

    //
    // Retries Collector history import for a watched item.
    // Once an import succeeds during the current addon session,
    // later calls become no-ops for price-history import.
    //
    // This call also keeps the cached list of successfully completed
    // Collector scans refreshed for market-analysis coverage.
    //
    bool TryImportCollectorHistory(
        uint32_t itemID
    );

    //
    // Stops future history recording for an item.
    // Existing historical observations are preserved.
    //
    void UnregisterWatchedItem(
        uint32_t itemID
    );

    //
    // Records one successful Trading Post price observation.
    // The call is ignored when the item is not currently watched.
    //
    void RecordObservation(
        uint32_t itemID,
        uint32_t buyUnitPrice,
        uint32_t sellUnitPrice,
        uint64_t timestampUnixSeconds
    );

    //
    // Returns all locally stored observations for one item,
    // oldest to newest.
    //
    std::vector<TradingPostHistoryPoint>
        GetHistory(
            uint32_t itemID
        );

    //
    // Returns the cached timestamps of successfully completed Collector
    // scans, oldest to newest. The cache is refreshed from the Collector
    // while watched-item Collector requests are being made.
    //
    std::vector<uint64_t>
        GetCollectorCompletedSnapshotTimestamps();

    size_t GetObservationCount(
        uint32_t itemID
    );
}
