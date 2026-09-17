#include "StdInclude.hpp"
#include "Mod.hpp"

#include "GameInterface.hpp"
#include "Version.hpp"
#include "WindowsConsole.hpp"
#include "Input.hpp"
#include "Utilities/HookManager.hpp"
#include "Utilities/ExceptionDiagnostics.hpp"
#include "Utilities/PathUtils.hpp"
#include "Utilities/MemoryUtils.hpp"
#include "UI/UIManager.hpp"
#include "Configuration/Configuration.hpp"
#include "Graphics/Graphics.hpp"

namespace IWXMVM
{
    GameInterface* Mod::internalGameInterface = nullptr;
    std::atomic<bool> Mod::ejectRequested = false;

    HMODULE GetCurrentModule()
    {
        static bool dummy;

        MEMORY_BASIC_INFORMATION mbi;
        ::VirtualQuery(&dummy, &mbi, sizeof(mbi));

        return static_cast<HMODULE>(mbi.AllocationBase);
    }

    // Path and modification time of this DLL, so that a log always tells which build produced it
    void LogModuleInfo()
    {
        char path[MAX_PATH]{};
        GetModuleFileNameA(GetCurrentModule(), path, MAX_PATH);

        std::string built = "unknown time";
        std::error_code error;
        const auto writeTime = std::filesystem::last_write_time(path, error);
        if (!error)
        {
            const auto systemTime = std::chrono::clock_cast<std::chrono::system_clock>(writeTime);
            const time_t timestamp = std::chrono::system_clock::to_time_t(systemTime);
            tm utc{};
            gmtime_s(&utc, &timestamp);
            built = std::format("{:04}-{:02}-{:02} {:02}:{:02}:{:02} UTC", utc.tm_year + 1900, utc.tm_mon + 1,
                                utc.tm_mday, utc.tm_hour, utc.tm_min, utc.tm_sec);
        }

        LOG_INFO("Module: {} (built {})", path, built);
    }

    void Mod::RequestEject()
    {
        ejectRequested.store(true);
    }

    void Mod::Initialize(GameInterface* gameInterface)
    {
        try
        {
            internalGameInterface = gameInterface;

            WindowsConsole::Open();
            Logger::Initialize();

            LOG_INFO("Loading IWXMVM {}", IWXMVM_VERSION);
            LogModuleInfo();
            LOG_INFO("Game: {}", magic_enum::enum_name(gameInterface->GetGame()));
            LOG_INFO("Game Path: {}", PathUtils::GetCurrentExecutablePath());

            LOG_DEBUG("Scanning signatures...");
            gameInterface->InitializeGameAddresses();

            // From here on the mod touches the game; log hardware exceptions with their location as soon as they
            // happen, before anything can swallow them.
            ExceptionDiagnostics::InstallFirstChanceLogger();

            LOG_DEBUG("Initializing components...");
            Configuration::Get().Initialize();
            Components::CameraManager::Get().Initialize();
            Components::CampathManager::Get().Initialize();
            Components::KeyframeManager::Get().Initialize();
            Components::Rewinding::Initialize();
            Components::Rendering::Initialize();

            LOG_DEBUG("Installing game hooks and patches...");
            D3D9::Initialize();
            gameInterface->InstallHooksAndPatches();
            gameInterface->SetupEventListeners();

            LOG_INFO("Initialized IWXMVM!");

            while (!ejectRequested.load())
                std::this_thread::sleep_for(std::chrono::milliseconds(1));

            HookManager::UnhookAll();
            LOG_DEBUG("Unhooked");

            // TODO: extract the entire resource release operation to a single function?
            // this is done for d3d9 reset, d3d9 create device, and here below
            GFX::GraphicsManager::Get().Uninitialize();
            LOG_DEBUG("Released UI and graphic resources");
            UI::UIManager::Get().ShutdownImGui();
            LOG_DEBUG("ImGui successfully shutdown");

            ExceptionDiagnostics::UninstallFirstChanceLogger();
            WindowsConsole::Close();
            ::FreeLibraryAndExitThread(GetCurrentModule(), 0);
        }
        catch (std::exception& ex)
        {
            LOG_ERROR("An error occurred during initialization: {}", ex.what());
            // TODO: What do we do here?
        }
        catch (...)
        {
            // with /EHa this is where hardware exceptions (access violations, ...) end up
            const auto lastException = ExceptionDiagnostics::TakeLastExceptionOnThisThread();
            LOG_ERROR("An error occurred during initialization: {}",
                      lastException.empty() ? "unknown exception type" : lastException);
        }
    }
}  // namespace IWXMVM