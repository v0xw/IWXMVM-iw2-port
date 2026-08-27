#include "StdInclude.hpp"
#include "Models.hpp"

#include "Utilities/HookManager.hpp"
#include "../Addresses.hpp"
#include "../Structures.hpp"

// The engine picks model LODs by distance to the recorded player's eyes (CL_SetLodOrigin), not the free
// camera, so distant players render low-poly no matter how close the camera gets. Hooking the LOD pick
// itself is the only safe way to force full detail: the r_forceLod / r_*LodDist overrides poison the
// world load instead, because R_FilterStaticModelIntoCells bakes XModelGetLodOutDist - which reads the
// same per-LOD override table, where r_forceLod puts 0.001 epsilons on the non-selected LODs - into the
// static model cells, distance-culling every multi-LOD static model (trees, foliage) instantly.
namespace IWXMVM::IW2::Hooks::Models
{
    typedef int(__cdecl* XModelGetLodForDist_t)(uintptr_t model, float dist);
    XModelGetLodForDist_t XModelGetLodForDist_Trampoline = nullptr;

    int __cdecl XModelGetLodForDist_Hook(uintptr_t model, float dist)
    {
        if (Structures::IsDemoPlaying())
        {
            // lod 0 is the highest detail; -1 (do not draw) only for lod-less models, like the game
            return *reinterpret_cast<int16_t*>(model + 124) > 0 ? 0 : -1;
        }

        return XModelGetLodForDist_Trampoline(model, dist);
    }

    void Install()
    {
        HookManager::CreateHook(Addresses::XModelGetLodForDist,
                                reinterpret_cast<uintptr_t>(XModelGetLodForDist_Hook),
                                reinterpret_cast<uintptr_t*>(&XModelGetLodForDist_Trampoline));
    }
}  // namespace IWXMVM::IW2::Hooks::Models
