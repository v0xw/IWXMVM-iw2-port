#pragma once
#include "Types/DemoMarker.hpp"

namespace IWXMVM::IW2::Hooks::Kills
{
    void Install();

    // Called from the "demo" command hook: forget the previous demo's kills / load the cached ones for the new demo
    void OnDemoUnloaded();
    void OnDemoLoaded();

    // True when the cache for the current demo came from a finished offline scan (no need to scan again)
    bool HasCompleteCache();

    // Record a kill from the offline demo scan (fromScan) or the live CG_Obituary hook; duplicates are ignored,
    // and live kills are ignored altogether once the scan has covered the whole demo
    void AddKill(int32_t serverTime, int32_t attacker, int32_t victim, bool fromScan);

    // The offline scan covered the whole demo: persist the cache as complete
    void OnScanFinished();

    // Markers in timeline (demo-relative) ticks; rebuilt lazily when new kills arrive or the start tick changes
    const std::vector<Types::DemoMarker>& GetMarkers();
}  // namespace IWXMVM::IW2::Hooks::Kills
