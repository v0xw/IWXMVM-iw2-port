#pragma once

namespace IWXMVM::Types
{
    enum Features : uint32_t
    {
        Features_None = 0,

        Features_ChangeAnimations = 1 << 0,

        // core smooths the bone camera's source bone over demo time (games where the raw bone
        // jitters too much to be usable as a camera mount)
        Features_TemporalBoneSmoothing = 1 << 1,

        // the engine creates its scene depth stencil as the first large depth stencil surface and
        // re-binds it while offscreen render targets are active, so depth interception must detect
        // it that way and hand the game a decoy for offscreen passes
        Features_SharedDepthStencil = 1 << 2,

        // DOF and filmtweaks are rendered by core as a post-process because the game has no engine
        // implementation of them
        Features_CorePostProcess = 1 << 3,

        // the game has no flashbang effect, so the shellshock toggle is labeled accordingly
        Features_NoFlashbangs = 1 << 4,
    };
}  // namespace IWXMVM::Types