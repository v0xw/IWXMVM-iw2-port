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

        // CoD2MP_s.exe reserves only 64 KB of stack for threads created with the default size (SizeOfStackReserve
        // in its PE header), which the D3D9 setup alone can exhaust on some systems. Reserve a proper one instead.
        ::CreateThread(nullptr, 1024 * 1024, reinterpret_cast<LPTHREAD_START_ROUTINE>(Mod::Initialize),
                       &gameInterface, STACK_SIZE_PARAM_IS_A_RESERVATION, nullptr);
    }
    return TRUE;
}
