// iw2launcher - injects iw2.dll into CoD2MP_s.exe.
//
//   iw2launcher.exe [--wait <seconds>] [path\to\iw2.dll]
//
// Waits for the game to be started (however and from wherever the user runs it), then injects.
// Must run elevated when the game runs elevated (CoD2x starts the game as administrator); it re-runs
// itself elevated automatically when needed.

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

    std::filesystem::path FindDll(const std::wstring& explicitPath)
    {
        std::vector<std::filesystem::path> candidates;
        if (!explicitPath.empty())
            candidates.push_back(explicitPath);

        const auto launcherDir = GetLauncherDirectory();
        candidates.push_back(launcherDir / DLL_NAME);

        // running from the repository build tree: iw2launcher\bin\Win32\Release -> iw2\bin\Win32\Release
        candidates.push_back(launcherDir / L".." / L".." / L".." / L".." / L"iw2" / L"bin" / L"Win32" / L"Release" /
                             DLL_NAME);

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
    int extraWaitSeconds = 3;
    std::wstring dllArgument;

    for (int i = 1; i < argc; ++i)
    {
        std::wstring arg = argv[i];
        if (arg == L"--pause" || arg == L"--no-pause")
            continue;
        else if (arg == L"--wait" && i + 1 < argc)
            extraWaitSeconds = _wtoi(argv[++i]);
        else if (arg == L"--help" || arg == L"-h" || arg == L"/?")
        {
            std::printf("Usage: iw2launcher.exe [--wait <seconds>] [path\\to\\iw2.dll]\n"
                        "  Waits for CoD2MP_s.exe to be running, then injects iw2.dll into it.\n"
                        "  --wait <sec>  extra seconds to wait after the game window appears (default 3)\n");
            return 0;
        }
        else
            dllArgument = arg;
    }

    const auto dllPath = FindDll(dllArgument);
    if (dllPath.empty())
    {
        std::printf("Could not find %ls. Put it next to the launcher or pass its path.\n", DLL_NAME);
        return 1;
    }
    std::printf("Using %ls\n", dllPath.c_str());

    DWORD pid = FindProcessId(GAME_PROCESS_NAME);
    bool freshlyStarted = false;
    if (pid == 0)
    {
        std::printf("Waiting for the game - please start %ls ...\n", GAME_PROCESS_NAME);
        while ((pid = FindProcessId(GAME_PROCESS_NAME)) == 0)
            Sleep(500);
        std::printf("Game detected (process %lu).\n", pid);
        freshlyStarted = true;
    }

    if (!FindWindowA(GAME_WINDOW_CLASS, nullptr))
    {
        // wait for the game window to appear
        freshlyStarted = true;
        for (int i = 0; i < 600 && !FindWindowA(GAME_WINDOW_CLASS, nullptr); ++i)
            Sleep(100);
    }

    // give a freshly started game time to create its renderer / D3D device before injecting
    if (freshlyStarted)
        Sleep(extraWaitSeconds * 1000);

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
