#pragma once

namespace IWXMVM::Types
{
    // A point of interest in the demo that the timeline draws as a marker (e.g. a kill).
    struct DemoMarker
    {
        uint32_t tick;     // relative to the demo start, like DemoInfo::gameTick
        bool highlighted;  // e.g. a kill by the recording player (drawn brighter)
    };
}  // namespace IWXMVM::Types
