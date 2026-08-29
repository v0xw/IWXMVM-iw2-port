#pragma once
#include "StdInclude.hpp"

#include "D3D9.hpp"
#include "Components/Camera.hpp"
#include "Types/GameState.hpp"
#include "Types/Game.hpp"
#include "Types/DemoInfo.hpp"
#include "Types/DemoMarker.hpp"
#include "Types/MouseMode.hpp"
#include "Types/Dvar.hpp"
#include "Types/Sun.hpp"
#include "Types/Dof.hpp"
#include "Types/Filmtweaks.hpp"
#include "Types/BoneData.hpp"
#include "Types/Entity.hpp"
#include "Types/PlaybackData.hpp"
#include "Types/HudInfo.hpp"
#include "Types/HudToggle.hpp"
#include "Types/RenderingFlags.hpp"
#include "Types/Features.hpp"

namespace IWXMVM
{
    const auto DEMO_TEMP_DIRECTORY = "IWXTMP";

    class GameInterface
    {
       public:
        GameInterface(const Types::Game game) : game(game)
        {
        }
        virtual ~GameInterface() = default;

        Types::Game GetGame() const
        {
            return game;
        }

        virtual void ExecuteNewServerCommands() = 0;
        virtual void InstallHooksAndPatches() = 0;
        virtual void SetupEventListeners() = 0;

        virtual IDirect3DDevice9* GetGameDevicePtr() const = 0;
        virtual uintptr_t GetWndProc() = 0;
        virtual void SetMouseMode(Types::MouseMode mode) = 0;
        virtual Types::GameState GetGameState() = 0;

        virtual std::optional<std::span<HMODULE>> GetModuleHandles(
            Types::ModuleType type = Types::ModuleType::BaseModule)
        {
            assert(type != Types::ModuleType::SecondaryModules);

            static std::vector<HMODULE> modules{::GetModuleHandle(nullptr)};
            return std::span{modules};
        }
        virtual void InitializeGameAddresses() = 0;

        virtual Types::Features GetSupportedFeatures()
        {
            return Types::Features_None;
        };

        virtual Types::DemoInfo GetDemoInfo() = 0;
        virtual std::string_view GetDemoExtension() = 0;

        // Optional: events worth marking on the timeline (kills, ...). Called every frame while drawing.
        virtual const std::vector<Types::DemoMarker>& GetDemoMarkers()
        {
            static const std::vector<Types::DemoMarker> none;
            return none;
        }

        virtual void PlayDemo(std::filesystem::path demoPath) = 0;
        virtual void Disconnect() = 0;
        virtual void Vid_Restart() = 0;

        virtual bool IsConsoleOpen() = 0;

        // TODO: Perhaps dvars shouldnt be exposed to core at all?
        virtual std::optional<Types::Dvar> GetDvar(const std::string_view name) = 0;

        // Required for setting the first person FOV
        // TODO: Perhaps this can be removed and consolidated into how we already set FOV for freecam?
        virtual void SetFov(float fov) = 0;

        // Required for all the settings of the "Visuals" tab
        virtual Types::Sun GetSun() = 0;
        virtual Types::DoF GetDof() = 0;
        virtual Types::Filmtweaks GetFilmtweaks() = 0;
        virtual Types::HudInfo GetHudInfo() = 0;
        virtual void SetSun(Types::Sun) = 0;
        virtual void SetDof(Types::DoF) = 0;
        virtual void SetFilmtweaks(Types::Filmtweaks) = 0;
        virtual void SetHudInfo(Types::HudInfo) = 0;

        // Sky override: names of alternative skies the current game can swap in on the fly.
        // An empty list hides the feature in the UI; an empty name restores the map's own sky.
        virtual std::vector<std::string> GetAvailableSkies()
        {
            return {};
        }
        virtual void SetSky(const std::string& materialName)
        {
        }

        // Fog override: names of alternative fog presets (typically other maps' fog) the current
        // game can apply on the fly. An empty list hides the fog controls in the UI.
        virtual std::vector<std::string> GetAvailableFogPresets()
        {
            return {};
        }
        // Whether the loaded demo itself renders any fog (used as the fog toggle's initial state;
        // some competitive mods suppress the map fog server-side, so their demos carry none).
        virtual bool HasDemoFog()
        {
            return true;
        }
        // enabled=false forces fog off. enabled=true with an empty preset shows the demo's own fog
        // (a game may substitute the current map's stock fog when the demo carries none); a
        // non-empty preset applies that preset's fog instead, and may carry the preset's
        // atmosphere (its ambient particle style) along. particles toggles the ambient-weather
        // particles (dust, snow, fog banks) independently of the fog, whether the demo carries
        // them itself or the game restores them.
        virtual void SetFog(bool enabled, const std::string& presetName, bool particles)
        {
        }

        // Game-specific fine-grained HUD toggles, rendered generically by the visuals tab. A
        // non-empty list replaces the combined "Show Icons and Text" switch there (the granular
        // toggles supersede it). Values reflect the game's current state; changes come back
        // through SetHudToggle, and each toggle is persisted in presets as "iwxmvm_ui_<id>".
        virtual std::vector<Types::HudToggle> GetHudToggles()
        {
            return {};
        }
        virtual void SetHudToggle(std::string_view id, bool value)
        {
        }

        // Required for BoneCamera
        virtual std::vector<Types::Entity> GetEntities() = 0;
        virtual Types::BoneData GetBoneData(int32_t entityId, const std::string& name) = 0;
        virtual constexpr std::vector<std::string> GetSupportedBoneNames() = 0;

        // Required for working rewinding
        virtual void CL_FirstSnapshot() = 0;
        virtual void ResetClientData(int serverTime) = 0;
        virtual Types::PlaybackData GetPlaybackDataAddresses() const = 0;

        // Some games have demo file footers/headers (see IW5)
        virtual uint32_t GetDemoFooterSize() { return 0; }
        virtual uint32_t GetDemoHeaderSize() { return 0; }

        // Size of the per-message header in the demo file
        // (CoD4: [type:1][seq:4][len:4] = 9, CoD2: [seq:4][len:4] = 8)
        virtual uint32_t GetDemoMessageHeaderSize() { return 9; }

        // Optional: name of the mod the loaded demo was recorded with (e.g. "zpam"), empty when unknown or
        // vanilla. Lets the UI disable mod-specific options.
        virtual std::string GetDemoModName() { return {}; }

       private:
        Types::Game game;
    };
}  // namespace IWXMVM
