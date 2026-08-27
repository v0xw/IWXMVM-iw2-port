#include "StdInclude.hpp"
#include "PlayerAnimation.hpp"

#include "../Addresses.hpp"
#include "../Structures.hpp"
#include "Components/PlayerAnimation.hpp"
#include "Utilities/HookManager.hpp"

namespace IWXMVM::IW2::Hooks::PlayerAnimation
{
    namespace
    {
        // The anim table is rebuilt for every demo (mods bring their own animation sets), so the
        // death-anim list is re-enumerated whenever the table's size changes rather than once
        int knownAnimationCount = -1;

        void RefreshAnimationNames()
        {
            const auto animScriptData = Structures::GetAnimScriptData();
            const auto count = std::clamp(animScriptData->numAnimations, 0, Structures::MAX_ANIMATIONS);
            if (count == knownAnimationCount)
            {
                return;
            }
            knownAnimationCount = count;

            std::vector<std::pair<std::string_view, std::uint32_t>> anims;
            for (int i = 1; i < count; i++)
            {
                const std::string_view animName{animScriptData->animations[i].name};
                if (animName.find("death") != std::string_view::npos)
                {
                    anims.emplace_back(animName, static_cast<std::uint32_t>(i));
                }
            }
            Components::PlayerAnimation::PopulateAnimationData(anims);
        }

        void __cdecl CG_ProcessEntity_Hook_Internal(Structures::centity_t* centity)
        {
            RefreshAnimationNames();

            // last seen weapon per client, so corpses (whose clientNum still identifies the
            // player) can get their owner's weapon attached
            static std::array<std::uint8_t, 64> weaponIndices{};

            auto& nextState = centity->nextState;
            if (nextState.eType == Structures::ET_PLAYER)
            {
                if (static_cast<std::size_t>(nextState.clientNum) < weaponIndices.size() && nextState.weapon != 0)
                {
                    weaponIndices[nextState.clientNum] = static_cast<std::uint8_t>(nextState.weapon);
                }
            }
            else if (nextState.eType == Structures::ET_PLAYER_CORPSE)
            {
                auto& legsAnim = reinterpret_cast<std::uint32_t&>(nextState.legsAnim);
                const auto animIndex = legsAnim & ~Structures::ANIM_TOGGLEBIT;
                if (animIndex != 0 && animIndex < static_cast<std::uint32_t>(knownAnimationCount))
                {
                    const std::string_view animName{Structures::GetAnimScriptData()->animations[animIndex].name};
                    if (animName.find("death") != std::string_view::npos)
                    {
                        Components::PlayerAnimation::SetPlayerAnimation(animName, legsAnim);
                    }
                }

                if (Components::PlayerAnimation::AttachWeaponToCorpse())
                {
                    if (static_cast<std::size_t>(nextState.clientNum) < weaponIndices.size())
                    {
                        nextState.weapon = weaponIndices[nextState.clientNum];
                        // the corpse draw path (BG code called from CG_Corpse) zeroes the weapon
                        // while either of these two flags is set, so clear them to actually show it
                        nextState.eFlags &= ~0x300;
                    }
                }
                else
                {
                    nextState.weapon = 0;
                }

                if (Components::PlayerAnimation::HideAllCorpses())
                {
                    nextState.eFlags |= 0x20;
                }
                else
                {
                    nextState.eFlags &= ~0x20;
                }
            }
            else if (nextState.eType == Structures::ET_ITEM)
            {
                // with weapons attached to their corpses, the dropped duplicates only clutter shots
                if (Components::PlayerAnimation::AttachWeaponToCorpse())
                {
                    nextState.eFlags |= 0x20;
                }
                else
                {
                    nextState.eFlags &= ~0x20;
                }
            }
        }

        std::uintptr_t CG_ProcessEntity_Trampoline = 0;
        void __declspec(naked) CG_ProcessEntity_Hook()
        {
            __asm
            {
                pushad
                push eax
                call CG_ProcessEntity_Hook_Internal
                add esp, 0x4
                popad
                jmp CG_ProcessEntity_Trampoline
            }
        }
    }  // namespace

    void Install()
    {
        HookManager::CreateHook(Addresses::CG_ProcessEntity, reinterpret_cast<std::uintptr_t>(CG_ProcessEntity_Hook),
                                &CG_ProcessEntity_Trampoline);
    }
}  // namespace IWXMVM::IW2::Hooks::PlayerAnimation
