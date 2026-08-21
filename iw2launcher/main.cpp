// iw2launcher - injects iw2.dll into a running (or freshly started) CoD2MP_s.exe.
//
//   iw2launcher.exe [--launch] [--wait <seconds>] [path\to\iw2.dll]
//
// Without --launch the launcher attaches to an already running game (start it and wait for the main menu first).
// With --launch it starts CoD2MP_s.exe from the install directory and injects once the game window exists.
// Must run elevated when the game runs elevated (CoD2x starts the game as administrator).

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <TlHelp32.h>

#include <shellapi.h>

#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#pragma comment(lib, "shell32.lib")

namespace
{
    constexpr const wchar_t* GAME_PROCESS_NAME = L"CoD2MP_s.exe";
    constexpr const char* GAME_WINDOW_CLASS = "CoD2";
    constexpr const wchar_t* DEFAULT_GAME_DIR = L"C:\\Games\\Call of Duty 2";
    constexpr const wchar_t* DLL_NAME = L"iw2.dll";

    DWORD FindProcessId(const wchar_t* processName)
    {
        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snapshot == INVALID_HANDLE_VALUE)
            return 0;

        PROCESSENTRY32W entry{};
        entry.dwSize = sizeof(entry);
        DWORD pid = 0;

        if (Process32FirstW(snapshot, &entry))
        {
            do
            {
                if (_wcsicmp(entry.szExeFile, processName) == 0)
                {
                    pid = entry.th32ProcessID;
                    break;
                }
            } while (Process32NextW(snapshot, &entry));
        }

        CloseHandle(snapshot);
        return pid;
    }

    std::filesystem::path GetLauncherDirectory()
    {
        wchar_t buffer[MAX_PATH];
        GetModuleFileNameW(nullptr, buffer, MAX_PATH);
        return std::filesystem::path(buffer).parent_path();
    }

    std::filesystem::path ReadInstallPathFromRegistry()
    {
        for (auto key : {L"SOFTWARE\\WOW6432Node\\Activision\\Call of Duty 2", L"SOFTWARE\\Activision\\Call of Duty 2"})
        {
            HKEY hKey;
            if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, key, 0, KEY_READ, &hKey) == ERROR_SUCCESS)
            {
                wchar_t value[MAX_PATH];
                DWORD size = sizeof(value);
                DWORD type = 0;
                const auto result = RegQueryValueExW(hKey, L"InstallPath", nullptr, &type, reinterpret_cast<LPBYTE>(value), &size);
                RegCloseKey(hKey);
                if (result == ERROR_SUCCESS && type == REG_SZ)
                    return std::filesystem::path(value);
            }
        }
        return {};
    }

    std::filesystem::path FindGameDirectory()
    {
        auto fromRegistry = ReadInstallPathFromRegistry();
        if (!fromRegistry.empty() && std::filesystem::exists(fromRegistry / GAME_PROCESS_NAME))
            return fromRegistry;

        if (std::filesystem::exists(std::filesystem::path(DEFAULT_GAME_DIR) / GAME_PROCESS_NAME))
            return DEFAULT_GAME_DIR;

        const auto local = GetLauncherDirectory();
        if (std::filesystem::exists(local / GAME_PROCESS_NAME))
            return local;

        return {};
    }

    std::filesystem::path FindDll(const std::wstring& explicitPath)
    {
        std::vector<std::filesystem::path> candidates;
        if (!explicitPath.empty())
            candidates.push_back(explicitPath);

        candidates.push_back(GetLauncherDirectory() / DLL_NAME);

        const auto gameDir = FindGameDirectory();
        if (!gameDir.empty())
            candidates.push_back(gameDir / L"IW2MVM" / DLL_NAME);

        for (const auto& candidate : candidates)
        {
            if (std::filesystem::is_regular_file(candidate))
                return std::filesystem::absolute(candidate);
        }

        return {};
    }

    bool IsModuleLoaded(DWORD pid, const wchar_t* moduleName)
    {
        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
        if (snapshot == INVALID_HANDLE_VALUE)
            return false;

        MODULEENTRY32W entry{};
        entry.dwSize = sizeof(entry);
        bool found = false;

        if (Module32FirstW(snapshot, &entry))
        {
            do
            {
                if (_wcsicmp(entry.szModule, moduleName) == 0)
                {
                    found = true;
                    break;
                }
            } while (Module32NextW(snapshot, &entry));
        }

        CloseHandle(snapshot);
        return found;
    }

    // True when this process created its own console (started by double-click / "Run as administrator"),
    // in which case the window would vanish together with our output.
    bool OwnsConsole()
    {
        DWORD processes[2];
        return GetConsoleProcessList(processes, 2) == 1;
    }

    bool IsElevated()
    {
        HANDLE token = nullptr;
        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
            return false;
        TOKEN_ELEVATION elevation{};
        DWORD size = sizeof(elevation);
        const bool ok = GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &size);
        CloseHandle(token);
        return ok && elevation.TokenIsElevated;
    }

    // Re-run ourselves elevated with the same arguments (+ --pause so the new console stays readable)
    int RelaunchElevated(int argc, wchar_t* argv[])
    {
        std::wstring args = L"--pause";
        for (int i = 1; i < argc; ++i)
        {
            args += L" \"";
            args += argv[i];
            args += L"\"";
        }

        wchar_t self[MAX_PATH];
        GetModuleFileNameW(nullptr, self, MAX_PATH);

        SHELLEXECUTEINFOW sei{};
        sei.cbSize = sizeof(sei);
        sei.fMask = SEE_MASK_NOCLOSEPROCESS;
        sei.lpVerb = L"runas";
        sei.lpFile = self;
        sei.lpParameters = args.c_str();
        sei.nShow = SW_SHOWNORMAL;

        if (!ShellExecuteExW(&sei) || !sei.hProcess)
        {
            std::printf("Elevation was declined or failed (error %lu).\n", GetLastError());
            return 1;
        }

        WaitForSingleObject(sei.hProcess, INFINITE);
        DWORD code = 1;
        GetExitCodeProcess(sei.hProcess, &code);
        CloseHandle(sei.hProcess);
        return static_cast<int>(code);
    }

    enum class InjectResult
    {
        Ok,
        Failed,
        AccessDenied
    };

    InjectResult Inject(DWORD pid, const std::filesystem::path& dllPath)
    {
        HANDLE process = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION |
                                         PROCESS_VM_WRITE | PROCESS_VM_READ,
                                     FALSE, pid);
        if (!process)
        {
            const auto error = GetLastError();
            std::printf("OpenProcess failed (error %lu).\n", error);
            return error == ERROR_ACCESS_DENIED ? InjectResult::AccessDenied : InjectResult::Failed;
        }

        const auto pathString = dllPath.wstring();
        const auto size = (pathString.size() + 1) * sizeof(wchar_t);

        void* remoteMemory = VirtualAllocEx(process, nullptr, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (!remoteMemory)
        {
            std::printf("VirtualAllocEx failed (error %lu)\n", GetLastError());
            CloseHandle(process);
            return InjectResult::Failed;
        }

        if (!WriteProcessMemory(process, remoteMemory, pathString.c_str(), size, nullptr))
        {
            std::printf("WriteProcessMemory failed (error %lu)\n", GetLastError());
            VirtualFreeEx(process, remoteMemory, 0, MEM_RELEASE);
            CloseHandle(process);
            return InjectResult::Failed;
        }

        // kernel32 is mapped at the same address in every 32-bit process, and this launcher is 32-bit too
        const auto loadLibrary = reinterpret_cast<LPTHREAD_START_ROUTINE>(
            GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW"));

        HANDLE thread = CreateRemoteThread(process, nullptr, 0, loadLibrary, remoteMemory, 0, nullptr);
        if (!thread)
        {
            std::printf("CreateRemoteThread failed (error %lu)\n", GetLastError());
            VirtualFreeEx(process, remoteMemory, 0, MEM_RELEASE);
            CloseHandle(process);
            return InjectResult::Failed;
        }

        WaitForSingleObject(thread, 15000);
        DWORD exitCode = 0;
        GetExitCodeThread(thread, &exitCode);
        CloseHandle(thread);

        VirtualFreeEx(process, remoteMemory, 0, MEM_RELEASE);
        CloseHandle(process);

        if (exitCode == 0)
        {
            std::printf("LoadLibrary returned NULL inside the game (blocked by Windows security, or a missing "
                        "dependency such as d3dx9_43.dll / D3DCompiler_43.dll).\n");
            return InjectResult::Failed;
        }

        std::printf("Injected %ls (remote module handle 0x%08lX)\n", dllPath.filename().c_str(), exitCode);
        return InjectResult::Ok;
    }

    DWORD LaunchGame(const std::filesystem::path& gameDir, const std::wstring& extraArgs)
    {
        const auto exe = (gameDir / GAME_PROCESS_NAME).wstring();
        std::wstring commandLine = L"\"" + exe + L"\"";
        if (!extraArgs.empty())
            commandLine += L" " + extraArgs;

        STARTUPINFOW si{};
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi{};

        if (!CreateProcessW(exe.c_str(), commandLine.data(), nullptr, nullptr, FALSE, 0, nullptr,
                            gameDir.wstring().c_str(), &si, &pi))
        {
            const auto error = GetLastError();
            std::printf("Failed to start %ls (error %lu)%s\n", exe.c_str(), error,
                        error == ERROR_ELEVATION_REQUIRED ? " - the game is configured to run as administrator" : "");
            return error == ERROR_ELEVATION_REQUIRED ? static_cast<DWORD>(-1) : 0;
        }

        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        return pi.dwProcessId;
    }
}  // namespace

int Run(int argc, wchar_t* argv[]);

int wmain(int argc, wchar_t* argv[])
{
    bool pause = OwnsConsole();
    for (int i = 1; i < argc; ++i)
    {
        if (std::wstring(argv[i]) == L"--pause")
            pause = true;
        else if (std::wstring(argv[i]) == L"--no-pause")
            pause = false;
    }

    const int result = Run(argc, argv);

    if (pause)
    {
        std::printf("\nPress Enter to close.");
        std::getchar();
    }
    return result;
}

int Run(int argc, wchar_t* argv[])
{
    bool launch = false;
    int extraWaitSeconds = 3;
    std::wstring dllArgument;
    std::wstring gameArgs;  // everything after "--" is passed to CoD2MP_s.exe (e.g. +set r_fullscreen 0)

    for (int i = 1; i < argc; ++i)
    {
        std::wstring arg = argv[i];
        if (arg == L"--pause" || arg == L"--no-pause")
            continue;
        else if (arg == L"--")
        {
            for (int j = i + 1; j < argc; ++j)
            {
                if (!gameArgs.empty())
                    gameArgs += L" ";
                gameArgs += argv[j];
            }
            break;
        }
        else if (arg == L"--launch")
            launch = true;
        else if (arg == L"--windowed")
        {
            // r_fullscreen / r_mode are archived dvars, so the game will remember this until changed again
            launch = true;
            std::wstring mode = L"1600x900";
            if (i + 1 < argc && iswdigit(argv[i + 1][0]))
                mode = argv[++i];
            if (!gameArgs.empty())
                gameArgs += L" ";
            gameArgs += L"+set r_fullscreen 0 +set r_mode " + mode;
        }
        else if (arg == L"--wait" && i + 1 < argc)
            extraWaitSeconds = _wtoi(argv[++i]);
        else if (arg == L"--help" || arg == L"-h" || arg == L"/?")
        {
            std::printf("Usage: iw2launcher.exe [--launch] [--windowed [WxH]] [--wait <seconds>] [path\\to\\iw2.dll] [-- <game args>]\n"
                        "  --launch        start CoD2MP_s.exe first (otherwise attach to the running game)\n"
                        "  --windowed WxH  like --launch, but windowed at the given size (default 1600x900)\n"
                        "  --wait <sec>    extra seconds to wait after the game window appears (default 3)\n"
                        "  -- <game args>  passed to the game when using --launch, e.g. -- +set r_fullscreen 0\n");
            return 0;
        }
        else
            dllArgument = arg;
    }

    const auto dllPath = FindDll(dllArgument);
    if (dllPath.empty())
    {
        std::printf("Could not find %ls. Put it next to the launcher, into <game>\\IW2MVM\\, or pass its path.\n", DLL_NAME);
        return 1;
    }
    std::printf("Using %ls\n", dllPath.c_str());

    DWORD pid = FindProcessId(GAME_PROCESS_NAME);

    if (pid == 0 && launch)
    {
        const auto gameDir = FindGameDirectory();
        if (gameDir.empty())
        {
            std::printf("Could not locate the game directory (registry / %ls).\n", DEFAULT_GAME_DIR);
            return 1;
        }

        std::printf("Starting %ls ...\n", (gameDir / GAME_PROCESS_NAME).c_str());
        pid = LaunchGame(gameDir, gameArgs);
        if (pid == static_cast<DWORD>(-1) && !IsElevated())
        {
            std::printf("Retrying elevated ...\n");
            return RelaunchElevated(argc, argv);
        }
        if (pid == 0 || pid == static_cast<DWORD>(-1))
            return 1;

        // wait for the game window and give the renderer time to create its device
        for (int i = 0; i < 600 && !FindWindowA(GAME_WINDOW_CLASS, nullptr); ++i)
            Sleep(100);
        Sleep(extraWaitSeconds * 1000);
    }

    if (pid == 0)
    {
        std::printf("%ls is not running. Start the game (and wait for the main menu) or use --launch.\n", GAME_PROCESS_NAME);
        return 1;
    }

    if (!FindWindowA(GAME_WINDOW_CLASS, nullptr))
    {
        std::printf("Game window not found yet; waiting for it ...\n");
        for (int i = 0; i < 600 && !FindWindowA(GAME_WINDOW_CLASS, nullptr); ++i)
            Sleep(100);
        Sleep(extraWaitSeconds * 1000);
    }

    if (IsModuleLoaded(pid, DLL_NAME))
    {
        std::printf("%ls is already loaded in process %lu.\n", DLL_NAME, pid);
        return 0;
    }

    std::printf("Injecting into process %lu ...\n", pid);
    const auto result = Inject(pid, dllPath);
    if (result == InjectResult::AccessDenied && !IsElevated())
    {
        // the game runs elevated (e.g. "run as administrator" compatibility flag): we need to as well
        std::printf("The game is running with higher privileges; retrying elevated ...\n");
        return RelaunchElevated(argc, argv);
    }
    return result == InjectResult::Ok ? 0 : 1;
}
