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

        ::CreateThread(nullptr, 0, reinterpret_cast<LPTHREAD_START_ROUTINE>(Mod::Initialize), &gameInterface, 0,
                       nullptr);
    }
    return TRUE;
}
