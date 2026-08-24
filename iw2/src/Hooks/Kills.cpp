#include "StdInclude.hpp"
#include "Kills.hpp"

#include "Utilities/HookManager.hpp"
#include "Utilities/PathUtils.hpp"
#include "../Addresses.hpp"
#include "../Structures.hpp"
#include "../DemoParser.hpp"
#include "HUD.hpp"
#include "Playback.hpp"

#include "nlohmann/json.hpp"

// CoD2 delivers the killfeed as EV_OBITUARY entity events inside the (delta compressed) snapshots, which the demo
// parser does not decode. Instead we watch the game's own CG_Obituary while the demo plays, remember every kill it
// reports and cache them per demo, so the timeline fills up as the demo is played / skipped through and is complete
// from the second load on.
namespace IWXMVM::IW2::Hooks::Kills
{
    using namespace Structures;

    struct Kill
    {
        int32_t serverTime;  // absolute cl.serverTime when the obituary was processed
        int32_t attacker;
        int32_t victim;

        bool operator==(const Kill&) const = default;
    };

    std::vector<Kill> kills;
    std::vector<Types::DemoMarker> markers;
    int32_t markersStartTick = -1;
    bool markersDirty = true;
    bool cacheDirty = false;
    bool cacheComplete = false;  // the offline scan covered the whole demo
    std::string cacheDemoName;

    std::filesystem::path CacheDirectory()
    {
        return PathUtils::GetIWXMVMPath() / "markers";
    }

    std::filesystem::path CachePath(const std::string& demoName)
    {
        std::string safeName = demoName;
        for (auto& c : safeName)
        {
            if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|')
                c = '_';
        }
        return CacheDirectory() / (safeName + ".json");
    }

    int32_t GetPovClientNum()
    {
        return *At<int32_t>(Addresses::clc_clientNum);
    }

    void Save()
    {
        if (!cacheDirty || cacheDemoName.empty())
            return;

        try
        {
            std::filesystem::create_directories(CacheDirectory());

            nlohmann::json root;
            root["demo"] = cacheDemoName;
            root["povClientNum"] = GetPovClientNum();
            root["complete"] = cacheComplete;
            auto& list = root["kills"] = nlohmann::json::array();
            for (const auto& kill : kills)
            {
                list.push_back({{"t", kill.serverTime}, {"a", kill.attacker}, {"v", kill.victim}});
            }

            std::ofstream file(CachePath(cacheDemoName));
            file << root.dump();
            cacheDirty = false;
        }
        catch (std::exception& ex)
        {
            LOG_ERROR("Failed to write kill marker cache: {}", ex.what());
        }
    }

    void Load(const std::string& demoName)
    {
        kills.clear();
        markersDirty = true;
        cacheComplete = false;

        const auto path = CachePath(demoName);
        if (!std::filesystem::exists(path))
            return;

        try
        {
            std::ifstream file(path);
            const auto root = nlohmann::json::parse(file);
            for (const auto& entry : root.at("kills"))
            {
                kills.push_back(Kill{entry.at("t").get<int32_t>(), entry.at("a").get<int32_t>(), entry.at("v").get<int32_t>()});
            }
            cacheComplete = root.value("complete", false);
            LOG_DEBUG("Loaded {} cached kill markers for {} ({})", kills.size(), demoName,
                      cacheComplete ? "complete" : "partial");
        }
        catch (std::exception& ex)
        {
            LOG_WARN("Ignoring unreadable kill marker cache {}: {}", path.string(), ex.what());
            kills.clear();
            cacheComplete = false;
        }
    }

    bool HasCompleteCache()
    {
        return cacheComplete;
    }

    void OnDemoUnloaded()
    {
        Save();
        kills.clear();
        markers.clear();
        markersDirty = true;
        cacheComplete = false;
        cacheDemoName.clear();
    }

    void OnDemoLoaded()
    {
        cacheDemoName = Playback::GetCurrentDemoName();
        cacheDirty = false;
        Load(cacheDemoName);
    }

    void AddKill(int32_t serverTime, int32_t attacker, int32_t victim)
    {
        const Kill kill{serverTime, attacker, victim};

        // the live hook sees obituaries again after a rewind, and overlaps with the offline scan;
        // ignore duplicates (with a little slack)
        for (const auto& known : kills)
        {
            if (known.attacker == kill.attacker && known.victim == kill.victim &&
                std::abs(known.serverTime - kill.serverTime) <= 100)
                return;
        }

        kills.push_back(kill);
        markersDirty = true;
        cacheDirty = true;
    }

    void OnScanFinished()
    {
        cacheComplete = true;
        cacheDirty = true;
        Save();
    }

    void OnObituary(const entityState_t* es)
    {
        if (!es || !IsDemoPlaying())
            return;

        AddKill(*At<int32_t>(Addresses::cl_serverTime), es->attackerEntityNum, es->otherEntityNum);

        // the file is tiny; writing it right away means nothing is lost on a crash
        Save();
    }

    const std::vector<Types::DemoMarker>& GetMarkers()
    {
        const auto [startTick, endTick] = DemoParser::GetDemoTickRange();
        if (!markersDirty && startTick == markersStartTick)
            return markers;

        markers.clear();
        if (startTick > 0)
        {
            const auto pov = GetPovClientNum();
            markers.reserve(kills.size());
            for (const auto& kill : kills)
            {
                if (kill.serverTime < startTick)
                    continue;
                markers.push_back(Types::DemoMarker{static_cast<uint32_t>(kill.serverTime - startTick), kill.attacker == pov});
            }
        }

        markersStartTick = startTick;
        markersDirty = false;
        return markers;
    }

    // clears the "You killed X" / "Killed by X" centerprint the original just queued when it is toggled off;
    // other centerprints (round messages etc.) are untouched since this only runs right after an obituary
    static void __cdecl SuppressKilledByMessage()
    {
        if (!HUD::showKilledByMessages)
            *At<int>(Addresses::cg_centerPrintTime) = 0;
    }

    // CG_Obituary(entityState_t* es <eax>, char localClientNum <dil>), void return, no stack arguments
    uintptr_t CG_Obituary_Trampoline = 0;

    void __declspec(naked) CG_Obituary_Hook()
    {
        static const entityState_t* obituaryEntity;

        __asm
        {
            mov obituaryEntity, eax
            pushad
        }

        OnObituary(obituaryEntity);

        __asm
        {
            popad
            call CG_Obituary_Trampoline
            pushad
        }

        SuppressKilledByMessage();

        __asm
        {
            popad
            ret
        }
    }

    void Install()
    {
        HookManager::CreateHook(Addresses::CG_Obituary, reinterpret_cast<uintptr_t>(CG_Obituary_Hook), &CG_Obituary_Trampoline);
    }
}  // namespace IWXMVM::IW2::Hooks::Kills
