#include "StdInclude.hpp"
#include "Mod.hpp"

#include "IW2Interface.hpp"

using namespace IWXMVM;
using namespace IW2;

IW2Interface gameInterface = IW2Interface();

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
    if (fdwReason == DLL_PROCESS_ATTACH)
    {
        // The renderer DLL (and with it d3d9.dll) is unloaded/reloaded on vid_restart. Core's D3D9 hooks live
        // inside d3d9.dll, so keep it pinned for the lifetime of this module.
        ::LoadLibraryA("d3d9.dll");

        // D3DX loads D3DCompiler_43.dll lazily when core compiles its shaders. That DLL is not present on every
        // system (CoD2 predates it), so preload it from next to iw2.dll; later loads by name then resolve to the
        // already-mapped module and the game install needs no extra files.
        wchar_t modulePath[MAX_PATH]{};
        if (::GetModuleFileNameW(hinstDLL, modulePath, MAX_PATH) != 0)
        {
            if (auto lastSlash = wcsrchr(modulePath, L'\\'))
            {
                *(lastSlash + 1) = L'\0';
                std::wstring compilerPath = std::wstring(modulePath) + L"D3DCompiler_43.dll";
                ::LoadLibraryW(compilerPath.c_str());
            }
        }

        ::CreateThread(nullptr, 0, reinterpret_cast<LPTHREAD_START_ROUTINE>(Mod::Initialize), &gameInterface, 0,
                       nullptr);
    }
    return TRUE;
}
