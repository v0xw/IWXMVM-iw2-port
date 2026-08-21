#pragma once

namespace IWXMVM::IW2::DemoParser
{
    // Indexes the demo that is currently being played and determines its first / last snapshot server times.
    // Must run on the game thread after the huffman tables have been initialised (i.e. from the "demo" command
    // hook). Also arms the incremental event scan (see Step).
    void Run();
    void Reset();

    // Decodes the demo's snapshots with the game's own delta routines to find events (kills) without having to
    // play the demo. Runs on the game thread in slices of at most 'budgetMs' so the game never stalls; call it
    // once per frame. Returns true while there is still work to do.
    bool Step(double budgetMs);
    bool IsScanning();
    void CancelScan();
    float GetScanProgress();  // 0..1

    std::pair<int32_t, int32_t> GetDemoTickRange();  // {start, end}

    // Bytes after the last complete [seq][len][data] record (8 for a proper end marker, more if truncated).
    // Reported to core as the demo "footer" so the engine never reads past the end of the file.
    uint32_t GetTrailingBytes();
}  // namespace IWXMVM::IW2::DemoParser
