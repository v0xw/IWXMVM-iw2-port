#include "StdInclude.hpp"
#include "PlayerAnimationUI.hpp"

#include "Components/PlayerAnimation.hpp"
#include "IconsFontAwesome6.h"
#include "Mod.hpp"
#include "UI/Components/PrimaryTabs.hpp"
#include "UI/UIManager.hpp"

namespace IWXMVM::UI
{
    namespace
    {
        // Imported animation packs are registered under a game prefix (e.g. an anim converted from
        // CoD4 is named "iwx_iw3_death_..."); anims without a pack prefix belong to the running game
        struct AnimPack
        {
            std::string_view prefix;
            const char* label;
        };
        constexpr AnimPack ANIM_PACKS[] = {
            {"iwx_iw3_", "CoD4"},
            {"iwx_iw4_", "MW2"},
            {"iwx_t4_", "WaW"},
        };
        constexpr std::int32_t NATIVE_PACK = -1;

        std::int32_t GetAnimPack(std::string_view animName)
        {
            for (std::int32_t i = 0; i < std::ssize(ANIM_PACKS); ++i)
            {
                if (animName.starts_with(ANIM_PACKS[i].prefix))
                    return i;
            }
            return NATIVE_PACK;
        }

        const char* GetPackLabel(std::int32_t pack)
        {
            if (pack != NATIVE_PACK)
                return ANIM_PACKS[pack].label;

            switch (Mod::GetGameInterface()->GetGame())
            {
                case Types::Game::IW2:
                    return "CoD2";
                case Types::Game::IW3:
                    return "CoD4";
                case Types::Game::IW5:
                    return "MW3";
                default:
                    return "Native";
            }
        }

        std::string_view GetAnimDisplayName(std::string_view animName, std::int32_t pack)
        {
            // the remainder of the game-memory string stays null-terminated after stripping
            return pack == NATIVE_PACK ? animName : animName.substr(ANIM_PACKS[pack].prefix.size());
        }
    }  // namespace

    bool DrawHeaderAndResetButton(const char* label)
    {
        ImGui::AlignTextToFramePadding();
        ImGui::PushFont(UIManager::Get().GetBoldFont());
        ImGui::Text(label);
        ImGui::PopFont();
        ImGui::SameLine();

        const char* resetlabel = ICON_FA_REPEAT " Reset ";
        const bool ret = ImGui::Button(resetlabel);
        return ret;
    }

    void PlayerAnimation::Render()
    {
        if (!visible)
            return;

        ImGui::SetNextWindowFocus();

        if (Mod::GetGameInterface()->GetGameState() != Types::GameState::InDemo)
        {
            ImGui::Begin("Player Death Animations##1", &visible, ImGuiWindowFlags_NoCollapse);
            UI::DrawInaccessibleTabWarning();
            ImGui::Dummy(ImVec2(450, 0));
            ImGui::End();
            return;
        }

        if (ImGui::Begin("Player Death Animations##2", &visible, ImGuiWindowFlags_NoCollapse))
        {
            static std::int32_t selected = -1;
            if (DrawHeaderAndResetButton(ICON_FA_PERSON_FALLING "  Select a death animation   ")) 
            {
                Components::PlayerAnimation::SetSelectedAnimIndex(selected = -1);
                Components::PlayerAnimation::HideAllCorpses() = false;
                Components::PlayerAnimation::AttachWeaponToCorpse() = false;
            }
        
            const auto& anims = Components::PlayerAnimation::GetAnimations();

            // group by source game so imported packs don't turn this into one endless list
            static std::int32_t selectedPack = NATIVE_PACK;
            bool packPresent[1 + std::ssize(ANIM_PACKS)] = {};
            for (const auto& [animName, index] : anims)
                packPresent[1 + GetAnimPack(animName)] = true;

            if (!packPresent[1 + selectedPack])
                selectedPack = NATIVE_PACK;

            // only offer the game dropdown when there is actually more than one source game -
            // without imported packs (and on games that have none) the window stays as it was
            if (std::count(std::begin(packPresent), std::end(packPresent), true) > 1)
            {
                ImGui::SetNextItemWidth(ImGui::GetWindowWidth() * 0.4f);
                if (ImGui::BeginCombo("##animPackCombo", GetPackLabel(selectedPack)))
                {
                    for (std::int32_t pack = NATIVE_PACK; pack < std::ssize(ANIM_PACKS); ++pack)
                    {
                        if (packPresent[1 + pack] && ImGui::Selectable(GetPackLabel(pack), selectedPack == pack))
                            selectedPack = pack;
                    }
                    ImGui::EndCombo();
                }
                ImGui::Dummy(ImVec2(0.0f, 5.0f));
            }

            for (std::int32_t i = 0; i < std::ssize(anims); ++i)
            {
                assert(anims[i].first.length() > 0 && *(anims[i].first.data() + anims[i].first.length()) == '\0');

                const auto pack = GetAnimPack(anims[i].first);
                if (pack != selectedPack)
                    continue;

                if (ImGui::Selectable(GetAnimDisplayName(anims[i].first, pack).data(), selected == i))
                    Components::PlayerAnimation::SetSelectedAnimIndex(selected = i);
            }

            ImGui::NewLine();
            if (ImGui::Checkbox("Hide all corpses", &Components::PlayerAnimation::HideAllCorpses()))
            {
                Components::PlayerAnimation::SetSelectedAnimIndex(selected = -1);
            }
            ImGui::Checkbox("Attach weapon to player", &Components::PlayerAnimation::AttachWeaponToCorpse());
            ImGui::NewLine();

            if (const auto animName = Components::PlayerAnimation::GetLatestAnimationName(); !animName.empty())
                ImGui::Text("Latest death animation: \n%s", animName.data());
            else 
            {
                ImGui::NewLine();
                ImGui::NewLine();
            }
        
            ImGui::End();
        }
    }

    void PlayerAnimation::Release()
    {
    }

    void PlayerAnimation::Initialize()
    {
    }
} // namespace IWXMVM::UI