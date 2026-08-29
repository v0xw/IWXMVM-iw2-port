#pragma once
#include "UI/UIComponent.hpp"
#include "glm/vec3.hpp"
#include "Types/dof.hpp"
#include "Types/Filmtweaks.hpp"
#include "Components/VisualConfiguration.hpp"

namespace IWXMVM::UI
{
    class VisualsMenu : public UIComponent
    {
       public:
        void Render() final;
        void Release() final;

       private:
        struct Preset
        {
            std::string name;
            std::filesystem::path path;
        };
        std::vector<Preset> recentPresets;

        void Initialize() final;
        void RenderConfigSection();
        void RenderMiscSection();
        void RenderDOF();
        void RenderSun();
        void RenderFilmtweaks();

        void UpdateDof();
        void UpdateSun();
        void UpdateFilmtweaks();
        void UpdateHudInfo();

        void LoadPreset(Preset);
        void AddPresetToRecent(Preset);

        Components::VisualConfiguration::Settings visuals;
        Components::VisualConfiguration::Settings defaultVisuals;
        std::string selectedSky;  // empty = the map's own sky
        bool fogEnabled = true;
        std::string selectedFog;        // fog preset name; empty = the demo's own fog
        bool particlesEnabled = true;   // show the ambient-weather particles (real or replayed)
        std::string selectedParticles;  // particle style preset; empty = the current map's own
        bool visualsInitialized = false;
        Preset defaultPreset;
        Preset currentPreset;
    };
}  // namespace IWXMVM::UI