#pragma once

#include <cstdint>

namespace TradingPostUI
{
    void RenderTab(
        void* moduleHandle
    );

    void RenderTargetOverlay();

    //
    // Shows a one-time standalone notice after the addon version changes.
    // The notice explains that some updates can trigger an automatic
    // Trading Post item-index rebuild.
    //
    void RenderUpdateNotice(
        void* moduleHandle,
        uint64_t currentVersionKey
    );

    bool WasTabVisibleRecently();
}
