#pragma once
#include "StdInclude.hpp"
#include "GameInterface.hpp"

#include "Addresses.hpp"
#include "Structures.hpp"
#include "Functions.hpp"
#include "Patches.hpp"
#include "Hooks.hpp"
#include "Hooks/Camera.hpp"
#include "Hooks/Diagnostics.hpp"
#include "Hooks/HUD.hpp"
#include "Hooks/Kills.hpp"
#include "Hooks/Playback.hpp"
#include "DemoParser.hpp"
#include "Events.hpp"
#include "Components/CameraManager.hpp"
#include "Components/Rewinding.hpp"
#include "Graphics/PostProcessSettings.hpp"

#include "glm/vec3.hpp"
#include "glm/gtc/type_ptr.hpp"

namespace IWXMVM::IW2
{
    class IW2Interface : public GameInterface
    {
       public:
        IW2Interface() : GameInterface(Types::Game::IW2)
        {
        }

        // ----------------------------------------------------------------------------------------------------
        // lifecycle
        // ----------------------------------------------------------------------------------------------------

        void InitializeGameAddresses() final
        {
            if (!Addresses::VerifyGameVersion())
            {
                const std::string found(reinterpret_cast<const char*>(Addresses::VersionString), 16);
                LOG_ERROR("Unsupported game version: '{}' (expected '{}')", found.c_str(), Addresses::ExpectedVersion);
                MessageBoxA(NULL,
                            "IWXMVM for CoD2 only supports Call of Duty 2 Multiplayer v1.3 (pc_1.3_1_1).\n"
                            "The running game reports a different version.",
                            "Unsupported game version", MB_OK | MB_ICONERROR);
                throw std::runtime_error("unsupported game version");
            }

            LOG_INFO("CoD2MP_s.exe 1.3 detected; gfx module at {}", reinterpret_cast<void*>(Addresses::GetGfxModule()));
        }

        void InstallHooksAndPatches() final
        {
            Hooks::Install();
            Patches::GetGamePatches();
        }

        void SetupEventListeners() final
        {
            Events::RegisterListener(EventType::OnCameraChanged, Hooks::Camera::OnCameraChanged);
            Events::RegisterListener(EventType::PreDemoLoad, DemoParser::Reset);
        }

        Types::Features GetSupportedFeatures() final
        {
            return Types::Features_None;
        }

        // ----------------------------------------------------------------------------------------------------
        // rendering / window / input
        // ----------------------------------------------------------------------------------------------------

        IDirect3DDevice9* GetGameDevicePtr() const final
        {
            const auto address = Addresses::Gfx(Addresses::GfxRVA::d3d9Device);
            if (!address)
                return nullptr;
            return *reinterpret_cast<IDirect3DDevice9**>(address);
        }

        uintptr_t GetWndProc() final
        {
            HWND hwnd = Structures::GetWindowHandle();
            if (!hwnd)
                hwnd = FindWindowA("CoD2", nullptr);

            // With CoD2x installed the class window procedure lives inside mss32.dll and forwards to the game's
            // WIN_WndProc; hooking whatever is registered right now makes sure we see every message first.
            if (hwnd)
            {
                const auto wndProc = static_cast<uintptr_t>(GetWindowLongPtrA(hwnd, GWLP_WNDPROC));
                if (wndProc)
                    return wndProc;
            }

            return Addresses::WIN_WndProc;
        }

        void SetMouseMode(Types::MouseMode mode) final
        {
            Hooks::Playback::SetMouseCaptured(mode == Types::MouseMode::Capture);
        }

        Types::GameState GetGameState() final
        {
            if (Structures::IsDemoPlaying())
                return Types::GameState::InDemo;

            const auto cl_ingame = reinterpret_cast<Structures::dvar_t*>(
                *reinterpret_cast<uintptr_t*>(Addresses::dvar_cl_ingame));
            const auto inGame = cl_ingame && cl_ingame->value.boolean;

            if (Structures::GetConnectionState() != Structures::CA_ACTIVE || !inGame)
                return Types::GameState::MainMenu;

            return Types::GameState::InGame;
        }

        bool IsConsoleOpen() final
        {
            return (Structures::GetKeyCatchers() & Structures::KEYCATCH_CONSOLE) != 0;
        }

        // ----------------------------------------------------------------------------------------------------
        // demos
        // ----------------------------------------------------------------------------------------------------

        std::string_view GetDemoExtension() final
        {
            return {".dm_1"};
        }

        const std::vector<Types::DemoMarker>& GetDemoMarkers() final
        {
            return Hooks::Kills::GetMarkers();
        }

        uint32_t GetDemoMessageHeaderSize() final
        {
            return 8;  // int32 serverMessageSequence + int32 length
        }

        uint32_t GetDemoFooterSize() final
        {
            return DemoParser::GetTrailingBytes();
        }

        Types::DemoInfo GetDemoInfo() final
        {
            const auto [startTick, endTick] = DemoParser::GetDemoTickRange();

            Types::DemoInfo demoInfo;
            if (!Structures::IsDemoPlaying())
            {
                demoInfo.gameTick = 0;
                demoInfo.endTick = 0;
                return demoInfo;
            }

            demoInfo.name = Hooks::Playback::GetCurrentDemoName();
            if (demoInfo.name.starts_with(DEMO_TEMP_DIRECTORY))
                demoInfo.name = demoInfo.name.substr(strlen(DEMO_TEMP_DIRECTORY) + 1);
            demoInfo.path = Hooks::Playback::GetCurrentDemoPath().string();

            const auto serverTime = *reinterpret_cast<int32_t*>(Addresses::cl_serverTime);
            demoInfo.endTick = endTick > startTick ? static_cast<uint32_t>(endTick - startTick) : 0;
            demoInfo.gameTick = serverTime > startTick ? static_cast<uint32_t>(serverTime - startTick) : 0;

            return demoInfo;
        }

        void PlayDemo(std::filesystem::path demoPath) final
        {
            // closes core's read stream on the previous temp copy before we try to replace it
            // (the "demo" command hook fires it again, which is harmless)
            Events::Invoke(EventType::PreDemoLoad);

            try
            {
                LOG_INFO("Playing demo {0}", demoPath.string());

                if (!std::filesystem::exists(demoPath) || !std::filesystem::is_regular_file(demoPath))
                    return;

                const auto demoDirectory = Functions::GetDemoDirectory();
                const auto tempDemoDirectory = demoDirectory / DEMO_TEMP_DIRECTORY;
                if (!std::filesystem::exists(tempDemoDirectory))
                    std::filesystem::create_directories(tempDemoDirectory);

                const auto targetPath = tempDemoDirectory / demoPath.filename();
                if (std::filesystem::exists(targetPath) && std::filesystem::is_regular_file(targetPath))
                    std::filesystem::remove(targetPath);

                std::filesystem::copy(demoPath, targetPath);

                // loading a demo on top of a live session (another demo playing, or the leftovers of a
                // failed load) makes the engine trip over half-torn-down assets ("unknown anim tree",
                // hangs on the load screen), so give it its full teardown first. Cbuf executes the
                // commands in order.
                if (*Structures::At<int>(Addresses::clc_state) != 0)
                    Functions::Cbuf_AddText("disconnect\n");

                // the "demo" command appends ".dm_1" itself; quote the name (zPAM demo names contain '#')
                const auto stem = targetPath.filename().replace_extension().string();
                Functions::Cbuf_AddText(std::format("demo \"{0}/{1}\"\n", DEMO_TEMP_DIRECTORY, stem));
            }
            catch (std::filesystem::filesystem_error& e)
            {
                LOG_ERROR("Failed to play demo file {0}: {1}", demoPath.string(), e.what());
            }
        }

        void Disconnect() final
        {
            Functions::Cbuf_AddText("disconnect\n");
        }

        void Vid_Restart() final
        {
            // Core only needs this once, right after injection, to recreate the device through its hooks.
            // Anything else is a bug; log who asked and refuse while a demo is playing.
            void* frames[12] = {};
            const auto count = CaptureStackBackTrace(1, 12, frames, nullptr);
            std::string trace;
            for (USHORT i = 0; i < count; ++i)
                trace += Hooks::Diagnostics::DescribeAddress(reinterpret_cast<uintptr_t>(frames[i])) + " <- ";
            LOG_WARN("Vid_Restart requested; stack: {}", trace);

            if (Structures::IsDemoPlaying())
            {
                LOG_ERROR("Ignoring vid_restart request during demo playback");
                return;
            }

            // the render thread (r_smp_backend) and the device re-creation we rely on don't mix well
            Functions::Cbuf_AddText("r_smp_backend 0\nvid_restart\n");
        }

        // ----------------------------------------------------------------------------------------------------
        // dvars
        // ----------------------------------------------------------------------------------------------------

        std::optional<Types::Dvar> GetDvar(const std::string_view name) final
        {
            // Core treats every dvar value as a 4-byte number. In CoD2 a dvar that was created implicitly (config,
            // server info, "set" before registration) is a STRING whose value is a char*; handing core a pointer to
            // that would let it overwrite the pointer (that is exactly how the first demo load crashed in
            // Dvar_MakeExplicitType). Such dvars, and dvars that don't exist at all, get a private scratch value.
            const auto gameDvar = Functions::FindDvar(name);
            if (gameDvar && gameDvar->type != Structures::DVAR_TYPE_STRING)
            {
                Types::Dvar dvar;
                dvar.name = gameDvar->name;
                dvar.value = reinterpret_cast<Types::Dvar::Value*>(&gameDvar->value);
                return dvar;
            }

            static std::map<std::string, Types::Dvar::Value, std::less<>> fallbacks;
            static const std::map<std::string, float, std::less<>> fallbackDefaults = {
                {"r_clear", 0.0f},     {"r_clearcolor", 0.0f}, {"r_znear", 4.0f},   {"r_smp_backend", 0.0f},
                {"cg_drawgun", 1.0f},  {"timescale", 1.0f},    {"com_maxfps", 0.0f},
            };

            std::string key(name);
            std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) { return std::tolower(c); });

            auto it = fallbacks.find(key);
            if (it == fallbacks.end())
            {
                const auto defaultIt = fallbackDefaults.find(key);
                if (defaultIt == fallbackDefaults.end() && !gameDvar)
                    return std::nullopt;

                LOG_WARN("Dvar '{}' is {}; using a private fallback value", name,
                         gameDvar ? "an implicit string dvar" : "not registered");

                Types::Dvar::Value value{};
                if (key == "r_znear")
                    value.floating_point = defaultIt != fallbackDefaults.end() ? defaultIt->second : 0.0f;
                else if (gameDvar && gameDvar->value.string)
                    value.int32 = std::atoi(gameDvar->value.string);
                else
                    value.int32 = defaultIt != fallbackDefaults.end() ? static_cast<int32_t>(defaultIt->second) : 0;
                it = fallbacks.emplace(key, value).first;
            }

            return Types::Dvar{it->first, &it->second};
        }

        void SetFov(float fov) final
        {
            Hooks::Camera::firstPersonFOV = fov;
            if (auto cg_fov = Functions::FindDvar("cg_fov"); cg_fov && cg_fov->type == Structures::DVAR_TYPE_FLOAT)
                cg_fov->value.decimal = fov;
        }

        // ----------------------------------------------------------------------------------------------------
        // visuals
        // ----------------------------------------------------------------------------------------------------

        glm::vec3 ReadVec3Dvar(const char* name, glm::vec3 fallback)
        {
            const auto dvar = Functions::FindDvar(name);
            if (!dvar)
                return fallback;

            if (dvar->type == Structures::DVAR_TYPE_VEC3 || dvar->type == Structures::DVAR_TYPE_VEC4)
                return dvar->value.vec3 ? glm::make_vec3(dvar->value.vec3) : fallback;

            if (dvar->type == Structures::DVAR_TYPE_COLOR)
                return glm::vec3(dvar->value.color[0], dvar->value.color[1], dvar->value.color[2]) / 255.0f;

            return fallback;
        }

        void WriteVec3Dvar(const char* name, glm::vec3 value)
        {
            const auto dvar = Functions::FindDvar(name);
            if (!dvar)
                return;

            if ((dvar->type == Structures::DVAR_TYPE_VEC3 || dvar->type == Structures::DVAR_TYPE_VEC4) && dvar->value.vec3)
            {
                dvar->value.vec3[0] = value.x;
                dvar->value.vec3[1] = value.y;
                dvar->value.vec3[2] = value.z;
            }
            else if (dvar->type == Structures::DVAR_TYPE_COLOR)
            {
                for (int i = 0; i < 3; ++i)
                    dvar->value.color[i] = static_cast<uint8_t>(std::clamp(glm::value_ptr(value)[i], 0.0f, 1.0f) * 255.0f);
            }
            dvar->modified = true;
        }

        Types::Sun GetSun() final
        {
            Types::Sun sun{};
            sun.color = ReadVec3Dvar("r_lightTweakSunColor", glm::vec3(1.0f));
            sun.direction = ReadVec3Dvar("r_lightTweakSunDirection", glm::vec3(0.0f));
            if (auto light = Functions::FindDvar("r_lightTweakSunLight"))
                sun.brightness = light->type == Structures::DVAR_TYPE_FLOAT ? light->value.decimal : 1.0f;
            else
                sun.brightness = 1.0f;
            return sun;
        }

        void SetSun(Types::Sun sun) final
        {
            WriteVec3Dvar("r_lightTweakSunColor", sun.color);
            WriteVec3Dvar("r_lightTweakSunDirection", sun.direction);
            if (auto light = Functions::FindDvar("r_lightTweakSunLight"); light && light->type == Structures::DVAR_TYPE_FLOAT)
            {
                light->value.decimal = sun.brightness;
                light->modified = true;
            }
            if (auto fromDvars = Functions::FindDvar("r_sun_from_dvars"); fromDvars && fromDvars->type == Structures::DVAR_TYPE_BOOL)
            {
                fromDvars->value.boolean = true;
                fromDvars->modified = true;
            }
        }

        Types::DoF GetDof() final
        {
            // CoD2 has no engine depth of field; ours is a post process (see GraphicsManager::ApplyDof)
            return GFX::GetDofSettings();
        }

        void SetDof(Types::DoF dof) final
        {
            GFX::SetDofSettings(dof);
        }

        Types::Filmtweaks GetFilmtweaks() final
        {
            // CoD2 has no engine filmtweaks; ours is a post process (see GraphicsManager::ApplyFilmtweaks)
            return GFX::GetFilmtweaksSettings();
        }

        void SetFilmtweaks(Types::Filmtweaks filmtweaks) final
        {
            GFX::SetFilmtweaksSettings(filmtweaks);
        }

        bool GetDvarBool(const char* name, bool fallback = true)
        {
            const auto dvar = Functions::FindDvar(name);
            if (!dvar)
                return fallback;
            if (dvar->type == Structures::DVAR_TYPE_BOOL)
                return dvar->value.boolean;
            if (dvar->type == Structures::DVAR_TYPE_INT)
                return dvar->value.integer != 0;
            if (dvar->type == Structures::DVAR_TYPE_STRING && dvar->value.string)
                return std::atoi(dvar->value.string) != 0;
            return fallback;
        }

        void SetDvarBool(const char* name, bool value)
        {
            const auto dvar = Functions::FindDvar(name);
            if (!dvar)
                return;
            if (dvar->type == Structures::DVAR_TYPE_BOOL)
                dvar->value.boolean = value;
            else if (dvar->type == Structures::DVAR_TYPE_INT)
                dvar->value.integer = value ? 1 : 0;
            else
                return;  // implicit string dvar (not registered by cgame yet): leave it alone
            dvar->modified = true;
        }

        std::string GetDemoModName() final
        {
            if (!Structures::IsDemoPlaying())
                return {};

            // the serverinfo configstring carries mod markers; zPAM sets "\_zpam\<version>"
            const auto stringOffsets = reinterpret_cast<const int*>(Addresses::cl_gameState);
            const auto offset = stringOffsets[0];
            if (offset <= 0 || offset >= 0x20000)
                return {};

            const auto serverInfo = reinterpret_cast<const char*>(Addresses::cl_gameState + 2048 * sizeof(int)) + offset;
            if (std::strstr(serverInfo, "\\_zpam\\"))
                return "zpam";

            return {};
        }

        Types::HudInfo GetHudInfo() final
        {
            Types::HudInfo hudInfo{};
            // reported from our own flags (not the archived game dvars, which carry over from previous
            // sessions) so the moviemaking defaults apply on first load
            hudInfo.show2DElements = Hooks::HUD::show2DElements;
            hudInfo.showPlayerHUD = Hooks::HUD::showPlayerHUD;
            hudInfo.showShellshock = Hooks::HUD::showShellshock;
            hudInfo.showCrosshair = Hooks::HUD::showCrosshair;
            hudInfo.showScore = Hooks::HUD::showScore;
            hudInfo.showIconsAndText = true;  // no IW2 toggle: unclassified hudelems always draw
            hudInfo.showHitmarkers = Hooks::HUD::showHitmarkers;
            hudInfo.showKilledByMessages = Hooks::HUD::showKilledByMessages;
            hudInfo.showModText = Hooks::HUD::showModText;
            hudInfo.showTimer = Hooks::HUD::showTimer;
            hudInfo.showPlayersLeftAlive = Hooks::HUD::showPlayersLeftAlive;
            hudInfo.showHints = Hooks::HUD::showHints;
            hudInfo.showTeammateIcons = Hooks::HUD::showTeammateIcons;
            hudInfo.showChat = Hooks::HUD::showChat;
            hudInfo.showBombTimer = Hooks::HUD::showBombTimer;
            hudInfo.showKillfeedKills = Hooks::HUD::showKillfeedKills;
            hudInfo.showKillfeedBombEvents = Hooks::HUD::showKillfeedBombEvents;
            hudInfo.showKillfeedOtherInfo = Hooks::HUD::showKillfeedOtherInfo;
            hudInfo.showKillfeedModMessages = Hooks::HUD::showKillfeedModMessages;
            hudInfo.showBloodOverlay = !Patches::GetGamePatches().CG_DrawDamageBlend.IsApplied();
            hudInfo.showKillfeed = Hooks::HUD::showKillfeed;
            hudInfo.killfeedTeam1Color = ReadVec3Dvar("g_TeamColor_Allies", glm::vec3(0.5f, 0.5f, 1.0f));
            hudInfo.killfeedTeam2Color = ReadVec3Dvar("g_TeamColor_Axis", glm::vec3(1.0f, 0.5f, 0.5f));
            return hudInfo;
        }

        void SetHudInfo(Types::HudInfo hudInfo) final
        {
            Hooks::HUD::show2DElements = hudInfo.show2DElements;
            Hooks::HUD::showKillfeed = hudInfo.showKillfeed;
            SetDvarBool("cg_draw2D", hudInfo.show2DElements);
            if (!hudInfo.show2DElements)
            {
                hudInfo.showPlayerHUD = false;
                hudInfo.showCrosshair = false;
                hudInfo.showKillfeed = false;
                // drawn outside the cg_draw2D-gated pass, so it must be forced off explicitly
                hudInfo.showTeammateIcons = false;
            }

            Hooks::HUD::showPlayerHUD = hudInfo.showPlayerHUD;
            Hooks::HUD::showCrosshair = hudInfo.showCrosshair;

            // the CoD2 player HUD (compass, health bar, ammo, stance, offhand) is menu-driven and has its
            // own master switch
            SetDvarBool("hud_enable", hudInfo.showPlayerHUD);
            SetDvarBool("cg_drawCrosshairNames", hudInfo.showPlayerHUD);
            if (auto damageIconTime = Functions::FindDvar("cg_hudDamageIconTime");
                damageIconTime && damageIconTime->type == Structures::DVAR_TYPE_INT)
                damageIconTime->value.integer = hudInfo.showPlayerHUD ? 2000 : 0;

            SetDvarBool("cg_drawCrosshair", hudInfo.showCrosshair);
            // cg_blood only controls the 3D blood puffs; the visible screen "blood overlay" is the
            // full-screen damage blend, which has no dvar - patch its draw function out instead
            SetDvarBool("cg_blood", hudInfo.showBloodOverlay);
            if (hudInfo.showBloodOverlay)
                Patches::GetGamePatches().CG_DrawDamageBlend.Revert();
            else
                Patches::GetGamePatches().CG_DrawDamageBlend.Apply();
            SetDvarBool("cg_drawGameMessages", hudInfo.showKillfeed);

            Hooks::HUD::showHitmarkers = hudInfo.showHitmarkers;
            Hooks::HUD::showScore = hudInfo.showScore;
            Hooks::HUD::showShellshock = hudInfo.showShellshock;
            Hooks::HUD::showKilledByMessages = hudInfo.showKilledByMessages;
            Hooks::HUD::showModText = hudInfo.showModText;
            Hooks::HUD::showTimer = hudInfo.showTimer;
            Hooks::HUD::showPlayersLeftAlive = hudInfo.showPlayersLeftAlive;
            Hooks::HUD::showHints = hudInfo.showHints;
            Hooks::HUD::showTeammateIcons = hudInfo.showTeammateIcons;
            if (hudInfo.showTeammateIcons)
                Patches::GetGamePatches().CG_DrawPlayerSprites.Revert();
            else
                Patches::GetGamePatches().CG_DrawPlayerSprites.Apply();

            Hooks::HUD::showChat = hudInfo.showChat;
            if (hudInfo.showChat)
                Patches::GetGamePatches().CG_DrawChatMessages.Revert();
            else
                Patches::GetGamePatches().CG_DrawChatMessages.Apply();

            Hooks::HUD::showBombTimer = hudInfo.showBombTimer;

            Hooks::HUD::showKillfeedKills = hudInfo.showKillfeedKills;
            Hooks::HUD::showKillfeedBombEvents = hudInfo.showKillfeedBombEvents;
            Hooks::HUD::showKillfeedOtherInfo = hudInfo.showKillfeedOtherInfo;
            Hooks::HUD::showKillfeedModMessages = hudInfo.showKillfeedModMessages;
            if (hudInfo.showKillfeedKills)
                Patches::GetGamePatches().CG_AddObituaryMessage.Revert();
            else
                Patches::GetGamePatches().CG_AddObituaryMessage.Apply();

            WriteVec3Dvar("g_TeamColor_Allies", hudInfo.killfeedTeam1Color);
            WriteVec3Dvar("g_TeamColor_Axis", hudInfo.killfeedTeam2Color);
        }

        // ----------------------------------------------------------------------------------------------------
        // entities / bones
        // ----------------------------------------------------------------------------------------------------

        std::vector<Types::Entity> GetEntities() final
        {
            std::vector<Types::Entity> entities;
            entities.reserve(Addresses::cg_entities_count);

            auto ToEntityType = [](int eType) -> Types::EntityType {
                switch (eType)
                {
                    case Structures::ET_PLAYER:
                        return Types::EntityType::Player;
                    case Structures::ET_PLAYER_CORPSE:
                        return Types::EntityType::Corpse;
                    case Structures::ET_ITEM:
                        return Types::EntityType::Item;
                    case Structures::ET_MISSILE:
                        return Types::EntityType::Missile;
                    default:
                        return Types::EntityType::Unsupported;
                }
            };

            const auto cg_entities = Structures::GetEntities();
            for (uint32_t i = 0; i < Addresses::cg_entities_count; i++)
            {
                const auto& entity = cg_entities[i];
                entities.push_back(Types::Entity{
                    .id = static_cast<int32_t>(i),
                    .type = ToEntityType(entity.nextState.eType),
                    .clientNum = entity.nextState.clientNum,
                    .isValid = entity.currentValid != 0,
                });
            }

            return entities;
        }

        Types::BoneData GetBoneData(int32_t entityId, const std::string& name) final
        {
            if (entityId < 0 || entityId >= static_cast<int32_t>(Addresses::cg_entities_count) ||
                *reinterpret_cast<int*>(Addresses::cls_cgameStarted) == 0)
            {
                return Types::BoneData{.id = -1};
            }


            // While rewinding, the demo replays without the render path running, so entity states
            // advance while the DObj animation state goes stale; evaluating bones on that mismatch
            // faults in the player animation code (e.g. when scrubbing across a round start)
            if (Components::Rewinding::IsRewinding())
            {
                return Types::BoneData{.id = -1};
            }

            // A timeline jump bumps cls.realtime far ahead and the game then chews through the
            // backlog over several frames with the rewinding flag already cleared; entity and
            // DObj state are just as inconsistent during that catch-up, so skip it too
            const auto realtime = *reinterpret_cast<const int32_t*>(Addresses::cls_realtime);
            const auto serverTime = *reinterpret_cast<const int32_t*>(Addresses::cl_serverTime);
            constexpr int32_t MAX_UNCONSUMED_DEMO_TIME = 2000;  // ms
            if (realtime - serverTime > MAX_UNCONSUMED_DEMO_TIME)
            {
                return Types::BoneData{.id = -1};
            }

            // Only evaluate entities the game itself is currently processing; during round
            // transitions the followed entity briefly drops out while being respawned
            if (!Structures::GetEntities()[entityId].currentValid)
            {
                return Types::BoneData{.id = -1};
            }

            const auto tagName = Functions::SL_FindString(name);
            if (tagName == 0)
            {
                return Types::BoneData{.id = -1};
            }

            const auto dobj = Functions::Com_GetClientDObj(entityId);
            if (dobj == nullptr)
            {
                return Types::BoneData{.id = -1};
            }

            const auto boneIndex = Functions::DObjGetBoneIndex(dobj, tagName);
            if (boneIndex < 0)
            {
                return Types::BoneData{.id = -1};
            }

            const auto entity = &Structures::GetEntities()[entityId];
            float axis[3][3];
            float origin[3];

            // The game only sets level_bgs around its own animation processing and nulls it
            // afterwards. Our tag query triggers a lazy skeleton evaluation (bone controllers)
            // whenever the entity wasn't rendered this frame - off-screen players, round
            // transitions - and the controller code would then read the animation tables through
            // the null pointer and crash. Provide the client bgs for the evaluation, like the
            // game does around its own calls, and restore the previous value afterwards.
            auto& levelBgs = *reinterpret_cast<uintptr_t*>(Addresses::level_bgs);
            const auto previousBgs = levelBgs;
            if (levelBgs == 0)
            {
                levelBgs = Addresses::cg_bgs;
            }

            const bool gotTag = Functions::CG_DObjGetWorldTagMatrix(tagName, dobj, entity, axis) &&
                                Functions::CG_DObjGetWorldTagPos(tagName, dobj, entity, origin);

            levelBgs = previousBgs;

            if (!gotTag)
            {
                return Types::BoneData{.id = -1};
            }

            Types::BoneData boneData;
            boneData.id = boneIndex;
            boneData.position = glm::make_vec3(origin);
            boneData.rotation = glm::make_mat3(&axis[0][0]);
            return boneData;
        }

        constexpr std::vector<std::string> GetSupportedBoneNames() final
        {
            // scriptstring lookups are case-sensitive; these names appear in the game executable itself,
            // matching the casing used by the stock CoD2 skeletons. Bones a model doesn't have simply
            // resolve to id -1.
            return {
                "tag_weapon",
                "tag_weapon_left",
                "tag_weapon_right",
                "tag_flash",
                "tag_brass",
                "j_head",
                "J_Spine4",
                "pelvis",
                "torso_upper",
                "torso_lower",
                "tag_camera",
                "tag_origin",
            };
        }

        // ----------------------------------------------------------------------------------------------------
        // rewinding
        // ----------------------------------------------------------------------------------------------------

        void CL_FirstSnapshot() final
        {
            Functions::CL_FirstSnapshot();
        }

        void ResetClientData(int serverTime) final
        {
            for (uint32_t i = 0; i < Addresses::cl_snapshots_count; i++)
                Structures::GetSnapshot(i)->valid = 0;

            // Mimic what CL_FirstSnapshot does on CoD4 (CoD2's version only recomputes the delta): the client
            // clock restarts at the first snapshot's server time, with cls.realtime back at 0. Core measures the
            // subsequent skip-forward relative to cl.serverTime, so it must already hold the initial time here.
            *reinterpret_cast<int*>(Addresses::cls_realtime) = 0;
            *reinterpret_cast<int*>(Addresses::cl_snap_serverTime) = serverTime;
            *reinterpret_cast<int*>(Addresses::cl_serverTime) = serverTime;
            *reinterpret_cast<int*>(Addresses::cl_oldServerTime) = serverTime;
            *reinterpret_cast<int*>(Addresses::cl_oldFrameServerTime) = serverTime;
            *reinterpret_cast<int*>(Addresses::cl_serverTimeDelta) = serverTime;  // serverTime - realtime(0)

            *reinterpret_cast<int*>(Addresses::cgs_processedSnapshotNum) = 0;
            *reinterpret_cast<int*>(Addresses::cg_latestSnapshotNum) = 0;
            *reinterpret_cast<int*>(Addresses::cg_latestSnapshotTime) = 0;
            *reinterpret_cast<uintptr_t*>(Addresses::cg_snap) = 0;
            *reinterpret_cast<uintptr_t*>(Addresses::cg_nextSnap) = 0;
        }

        Types::PlaybackData GetPlaybackDataAddresses() const final
        {
            return Types::PlaybackData{
                .cl =
                    {
                        .snap_serverTime = Addresses::cl_snap_serverTime,
                        .serverTime = Addresses::cl_serverTime,
                        .parseEntitiesNum = Addresses::cl_parseEntitiesNum,
                        .parseClientsNum = Addresses::cl_parseClientsNum,
                    },
                .clc =
                    {
                        .serverCommandSequence = Addresses::clc_serverCommandSequence,
                        .lastExecutedServerCommand = Addresses::clc_lastExecutedServerCommand,
                        .serverCommands = {.address = Addresses::clc_serverCommands,
                                           .size = Addresses::clc_serverCommands_size},
                        .serverConfigDataSequence = 0,
                    },
                .cgs =
                    {
                        .serverCommandSequence = Addresses::cgs_serverCommandSequence,
                    },
                .cls =
                    {
                        .realtime = Addresses::cls_realtime,
                    },
                .s_compassActors = {.address = 0, .size = 0},
                .teamChatMsgs = {.address = 0, .size = 0},
                .cg_entities = {.address = Addresses::cg_entities,
                                .size = Addresses::cg_entities_count * Addresses::cg_entity_size},
                .clientInfo = {.address = Addresses::clientInfo,
                               .size = Addresses::clientInfo_count * Addresses::clientInfo_size},
                .gameState = {.address = Addresses::cl_gameState, .size = Addresses::cl_gameState_size},
                .killfeed = 0,
            };
        }

        void ExecuteNewServerCommands() final
        {
            auto& cgsSequence = *reinterpret_cast<int*>(Addresses::cgs_serverCommandSequence);
            const auto clcSequence = *reinterpret_cast<int*>(Addresses::clc_serverCommandSequence);
            auto serverCommands = reinterpret_cast<char(*)[1024]>(Addresses::clc_serverCommands);

            const auto oldServerCommandSequence = cgsSequence;
            const auto newServerCommandSequence = clcSequence;

            // Once the backlog reaches half the ring buffer the engine would error out with
            // "a reliable command was cycled out"; process (only) the configstring updates eagerly.
            if (oldServerCommandSequence > 0 && oldServerCommandSequence + 64 <= newServerCommandSequence)
            {
                for (auto i = oldServerCommandSequence + 1; i <= newServerCommandSequence; ++i)
                {
                    if (serverCommands[i & 127][0] != 'd')
                    {
                        // erase server commands that do not modify the gamestate strings
                        serverCommands[i & 127][0] = '\0';
                    }
                }

                Functions::CG_ExecuteNewServerCommands(newServerCommandSequence);

                for (auto i = oldServerCommandSequence + 1; i <= newServerCommandSequence; ++i)
                {
                    serverCommands[i & 127][0] = '\0';
                }
            }
        }
    };
}  // namespace IWXMVM::IW2
