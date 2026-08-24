#pragma once
#include "StdInclude.hpp"

// Hardcoded addresses for Call of Duty 2 Multiplayer v1.3 (CoD2MP_s.exe, "pc_1.3_1_1").
// The executable has a fixed image base (0x400000), no ASLR and stripped relocations, so absolute addresses are
// stable. gfx_d3d_mp_x86_s.dll is relocatable (and reloaded on every vid_restart) so renderer addresses are
// expressed as RVAs and resolved through the module handle the game stores at 0xD53E80.
//
// Sources: CoD2x symbol tables (symbols_cod2mp_s.h / symbols_gfx_d3d_mp_x86_s.h), the Hex-Rays decompile of the
// Windows exe and the named Mac 1.3 decompile (identical struct layouts).
namespace IWXMVM::IW2::Addresses
{
    // --- version check -------------------------------------------------------------------------------------------
    constexpr uintptr_t VersionString = 0x0059B6C0;  // "pc_1.3_1_1"
    constexpr const char* ExpectedVersion = "pc_1.3_1_1";

    // --- functions (exe) -----------------------------------------------------------------------------------------
    constexpr uintptr_t CL_ReadDemoMessage = 0x0040C5B0;
    constexpr uintptr_t CL_DemoCompleted = 0x0040C500;
    constexpr uintptr_t CL_PlayDemo_f = 0x0040C710;  // "demo" / "timedemo" console command, void __cdecl()
    constexpr uintptr_t CL_SetCGameTime = 0x00403080;
    constexpr uintptr_t CL_FirstSnapshot = 0x00402EC0;  // void __cdecl()
    constexpr uintptr_t CL_ParseServerMessage = 0x004143E0;
    constexpr uintptr_t CL_Disconnect = 0x0040CD10;
    constexpr uintptr_t CL_Vid_Restart_f = 0x0040D780;  // "vid_restart" command

    constexpr uintptr_t Com_Frame = 0x00435240;
    constexpr uintptr_t Com_ModifyMsec = 0x00434CB0;  // int __cdecl(int msec)
    constexpr uintptr_t SV_Frame = 0x0045C550;
    constexpr uintptr_t CL_Frame = 0x0040F850;
    constexpr uintptr_t SCR_UpdateFrame = 0x00414B20;  // no args (returns GetCurrentThreadId); renders one frame

    constexpr uintptr_t FS_Read = 0x00422EC0;  // int __cdecl(void* buffer, int len, int handle)
    constexpr uintptr_t FS_PureServerSetLoadedIwds = 0x0043C550;  // __thiscall(const char* checksums <ecx>, const char* names)
    constexpr uintptr_t FS_FOpenFileRead = 0x00422700;
    constexpr uintptr_t FS_FCloseFile = 0x00421FD0;
    constexpr uintptr_t FS_FileExists = 0x00421EA0;

    constexpr uintptr_t Cbuf_AddText = 0x00420AD0;  // text in EAX
    constexpr uintptr_t Cbuf_ExecuteText = 0x00420BB0;
    constexpr uintptr_t Cmd_AddCommand = 0x004212F0;
    constexpr uintptr_t Cmd_Argc = 0x00420EE0;
    constexpr uintptr_t Cmd_Argv = 0x00420F00;
    constexpr uintptr_t Cmd_ExecuteString = 0x004214C0;

    constexpr uintptr_t Dvar_GetDvarByName = 0x004373A0;  // dvar_t* __cdecl(const char* name)
    constexpr uintptr_t Dvar_SetBool = 0x00438B90;        // void __cdecl(dvar_t*, int)
    constexpr uintptr_t Dvar_SetInt = 0x00438BF0;         // void __cdecl(dvar_t*, int)
    constexpr uintptr_t Dvar_SetFloat = 0x00438C10;       // void __cdecl(dvar_t*, float)
    constexpr uintptr_t Dvar_SetString = 0x00438CA0;      // void __cdecl(dvar_t*, const char*)

    constexpr uintptr_t Com_Printf = 0x00431EE0;
    constexpr uintptr_t Com_Error = 0x004324C0;

    constexpr uintptr_t MSG_ReadBitsCompress = 0x00444050;  // from in EAX; to, size on the stack; returns bytes written
    constexpr uintptr_t MSG_ReadBits = 0x00443F40;          // msg in EDX, bits on the stack (caller cleans up)
    constexpr uintptr_t MSG_ReadByte = 0x004443C0;          // __thiscall(msg_t* msg)
    constexpr uintptr_t MSG_ReadLong = 0x00444410;          // __thiscall(msg_t* msg)
    constexpr uintptr_t MSG_ReadString = 0x00444480;        // __fastcall(unused, msg_t* msg) -> char*
    constexpr uintptr_t MSG_ReadBigString = 0x004444E0;     // __fastcall(unused, msg_t* msg) -> char*
    constexpr uintptr_t MSG_ReadDeltaEntity = 0x00445A40;   // to in EAX, msg in EBX; stack: from, number, numFields, fieldTable; != 0 if removed
    constexpr uintptr_t MSG_ReadDeltaPlayerstate = 0x00446CA0;  // msg in EBX; stack: from, to
    constexpr uintptr_t entityStateFields = 0x0059BDA0;     // netField_t table used for entityState_t
    constexpr int entityStateFieldCount = 59;
    constexpr uintptr_t Huff_Init = 0x00447670;             // void __cdecl()
    constexpr uintptr_t Huff_Initialized = 0x019A1B9C;      // int

    constexpr uintptr_t CG_CalcViewValues = 0x004CF900;  // void __cdecl()
    constexpr uintptr_t CG_CalcFov = 0x004CF2E0;
    constexpr uintptr_t AnglesToAxis = 0x0042A6F0;  // axis in EAX, angles in EDX
    constexpr uintptr_t CG_OffsetThirdPersonView = 0x004CE890;
    constexpr uintptr_t CG_DrawActiveFrame2 = 0x004CFE10;
    constexpr uintptr_t CG_DrawActive = 0x004CBE50;
    constexpr uintptr_t CG_Draw2D = 0x004CBCA0;
    constexpr uintptr_t CG_Draw2dHudElems = 0x004DDFF0;  // void __cdecl()
    constexpr uintptr_t CG_DrawGameMessages = 0x004CAD60;
    constexpr uintptr_t CG_DrawCrosshairNames = 0x004C97C0;
    constexpr uintptr_t CG_DrawPlayerSprites = 0x004CBC40;
    constexpr uintptr_t CG_AddViewWeapon = 0x004D6390;
    constexpr uintptr_t CL_FX_AdjustCamera = 0x00402650;
    constexpr uintptr_t CG_DrawWeapReticle = 0x004C8CB0;  // double __cdecl(); draws the sniper scope overlay when zoomed (else no-op), returns the crosshair fade factor in st0
    constexpr uintptr_t CG_DrawDamageBlend = 0x004C99F0;  // int __cdecl(); full-screen red blend while cg.damageTime > cg.time (the on-hit blood overlay)

    constexpr uintptr_t CG_Obituary = 0x004E03F0;  // entityState_t* in EAX (otherEntityNum = victim, attackerEntityNum), localClientNum in DIL

    constexpr uintptr_t CG_ExecuteNewServerCommands = 0x004D2150;  // latestSequence in ESI, EDI (low byte) = 0
    constexpr uintptr_t CG_ServerCommand = 0x004D1B80;
    constexpr uintptr_t CL_GetServerCommand = 0x00401710;
    constexpr uintptr_t CL_GetConfigString = 0x004020D0;  // index in EAX, returns const char* in EAX

    // bone camera (__usercall; the caller cleans up all stack arguments)
    constexpr uintptr_t CG_DObjGetWorldTagMatrix = 0x004CCD40;  // tag in EAX, DObj* in ECX; stack: centity*, float axis[3][3] out; returns bool
    constexpr uintptr_t CG_DObjGetWorldTagPos = 0x004CCE70;     // tag in EAX, DObj* in ECX; stack: centity*, float pos[3] out; returns bool
    constexpr uintptr_t Com_GetClientDObj = 0x004356B0;         // localClientNum (0) in EAX, entity number in ECX; returns DObj* in EAX or null
    constexpr uintptr_t DObjGetBoneIndex = 0x00486D00;          // DObj* in EAX; stack: tag; returns the bone index or < 0
    constexpr uintptr_t SL_FindStringOfLen = 0x004772D0;        // int __cdecl(const char* str, unsigned int lengthIncludingNull); scriptstring handle, 0 if never interned (byte-exact comparison, so case matters)

    constexpr uintptr_t Mouse_GetMovement = 0x004649C0;
    constexpr uintptr_t Mouse_Loop = 0x00464B30;
    constexpr uintptr_t IN_MouseEvent = 0x00464940;
    constexpr uintptr_t CL_KeyEvent = 0x0040B720;
    // inside CL_KeyEvent: 'jnz' taken when clc.demoplaying != 0, which turns any key into ESC (opens the menu)
    constexpr uintptr_t CL_KeyEvent_DemoPlayingJump = 0x0040B85E;  // 75 0A
    constexpr uintptr_t WIN_WndProc = 0x00468DB0;

    // --- globals: client active (cl) -----------------------------------------------------------------------------
    constexpr uintptr_t cl = 0x0096B650;
    constexpr uintptr_t cl_snap_valid = 0x0096B668;
    constexpr uintptr_t cl_snap_serverTime = 0x0096B670;
    constexpr uintptr_t cl_snap_messageNum = 0x0096B674;
    constexpr uintptr_t cl_serverTime = 0x0096DD40;
    constexpr uintptr_t cl_oldServerTime = 0x0096DD44;
    constexpr uintptr_t cl_oldFrameServerTime = 0x0096DD48;
    constexpr uintptr_t cl_serverTimeDelta = 0x0096DD4C;
    constexpr uintptr_t cl_extrapolatedSnapshot = 0x0096DD54;
    constexpr uintptr_t cl_newSnapshots = 0x0096DD58;
    constexpr uintptr_t cl_gameState = 0x0096DD5C;  // {int stringOffsets[2048]; char stringData[0x20000]; int dataCount;}
    constexpr uint32_t cl_gameState_size = 0x22004;
    constexpr uintptr_t cl_parseEntitiesNum = 0x0098FDA0;
    constexpr uintptr_t cl_parseClientsNum = 0x0098FDA4;
    constexpr uintptr_t cl_snapshots = 0x009D0DB0;  // clSnapshot_t[32], stride 9944, first int = valid
    constexpr uint32_t cl_snapshots_count = 32;
    constexpr uint32_t cl_snapshot_size = 9944;
    constexpr uintptr_t cl_entityBaselines = 0x00A1E8B0;  // entityState_t[1024], stride 240

    // --- globals: client connection (clc) -------------------------------------------------------------------------
    constexpr uintptr_t clc_state = 0x00609FE0;  // connstate_t
    constexpr uintptr_t clc_clientNum = 0x00609FE8;
    constexpr uintptr_t clc_lastPacketTime = 0x00609FF0;  // = cls.realtime whenever a demo message is read
    constexpr uintptr_t clc_serverMessageSequence = 0x0062A120;
    constexpr uintptr_t clc_serverCommandSequence = 0x0062A124;
    constexpr uintptr_t clc_lastExecutedServerCommand = 0x0062A128;
    constexpr uintptr_t clc_serverCommands = 0x0062A12C;  // char[128][1024]
    constexpr uint32_t clc_serverCommands_size = 128 * 1024;
    constexpr uintptr_t clc_demoName = 0x0064A12C;  // char[64]
    constexpr uintptr_t clc_demorecording = 0x0064A16C;
    constexpr uintptr_t clc_demoplaying = 0x0064A170;
    constexpr uintptr_t clc_timedemo = 0x0064A174;
    constexpr uintptr_t clc_demowaiting = 0x0064A178;
    constexpr uintptr_t clc_firstDemoFrameSkipped = 0x0064A17C;
    constexpr uintptr_t clc_demofile = 0x0064A180;  // int (FS handle)

    // --- globals: client static (cls) ----------------------------------------------------------------------------
    constexpr uintptr_t cls_cgameStarted = 0x0068A518;
    constexpr uintptr_t cls_frametime = 0x0068A51C;
    constexpr uintptr_t cls_realtime = 0x0068A520;

    // --- globals: cgame ------------------------------------------------------------------------------------------
    constexpr uintptr_t cgs_viewport = 0x014E5704;  // int x, y, width, height
    constexpr uintptr_t cgs_serverCommandSequence = 0x014E5718;
    constexpr uintptr_t cgs_processedSnapshotNum = 0x014E571C;

    constexpr uintptr_t cg = 0x014EE080;  // sizeof(cg_t) == 0xF49A0
    constexpr uintptr_t cg_clientNum = 0x014EE084;
    constexpr uintptr_t cg_isDemoPlaying = 0x014EE088;
    constexpr uintptr_t cg_cubemapShot = 0x014EE08C;  // int; nonzero while rendering a cubemap shot (game skips all 2D)
    constexpr uintptr_t cg_latestSnapshotNum = 0x014EE098;
    constexpr uintptr_t cg_latestSnapshotTime = 0x014EE09C;
    constexpr uintptr_t cg_snap = 0x014EE0A0;      // snapshot_t*
    constexpr uintptr_t cg_nextSnap = 0x014EE0A4;  // snapshot_t*
    // inside snapshot_t: the playerstate's shellshock state (CG_UpdateShellShock @ 0x4D3260 deactivates
    // cleanly - blur, sound, turn cap - when the start time reads 0)
    constexpr uint32_t snap_shellshockIndex = 1508;     // int (cgs shellshock parms index)
    constexpr uint32_t snap_shellshockTime = 1512;      // int (server time the shock started)
    constexpr uint32_t snap_shellshockDuration = 1516;  // int
    // inside snapshot_t: the playerstate's scripted hudelems, two arrays of 31 elements (128 bytes each);
    // collection stops at the first element with type 0 (CG_GetSortedHudElems @ 0x4DDF90)
    constexpr uint32_t snap_hudElemsCurrent = 5940;
    constexpr uint32_t snap_hudElemsArchival = 1972;
    constexpr uint32_t hudElem_count = 31;
    constexpr uint32_t hudElem_size = 128;
    constexpr uint32_t hudElem_type = 0;        // int: 1 text, 2..5 timers/clocks, 6 value, 0xB/0xC material, 13 fade
    constexpr uint32_t hudElem_materialIdx = 60;  // int: material configstring index (types 0xB/0xC)
    constexpr uint32_t hudElem_foreground = 124;  // int: pass selector compared by CG_Draw2dHudElems @ 0x4DDFF0
    // material configstring lookup used by the hudelem drawer (CG_DrawHudElemMaterial @ 0x4DD9F0):
    // name = (char*)(materialCSData + materialCSOffsets[index]), valid for 0 < index < 128
    constexpr uintptr_t materialCSOffsets = 0x0096F5D4;  // int[128]
    constexpr uintptr_t materialCSData = 0x0096FD5C;     // char blob
    constexpr uintptr_t cg_predictedPlayerState = 0x014EE080 + 0x25BC4;
    constexpr uintptr_t cg_refdef = 0x015165F0;           // refdef_t
    constexpr uintptr_t cg_refdefViewAngles = 0x01516648;  // vec3

    constexpr uintptr_t cg_entities = 0x015E2A80;  // centity_t[1024], stride 548
    constexpr uint32_t cg_entities_count = 1024;
    constexpr uint32_t cg_entity_size = 548;
    constexpr uintptr_t clientInfo = 0x015CF994;  // clientInfo_t[64], stride 0x4B8 (inside cg.bgs)
    constexpr uint32_t clientInfo_count = 64;
    constexpr uint32_t clientInfo_size = 0x4B8;

    // --- globals: misc -------------------------------------------------------------------------------------------
    constexpr uintptr_t keyCatchers = 0x0096B654;  // 1 = console, 8 = ui, 0x10 = chat
    constexpr uintptr_t com_frameTime = 0x00C28B14;
    constexpr uintptr_t cmd_argc = 0x00B1A480;
    constexpr uintptr_t cmd_argv = 0x00B17A80;  // char*[]
    constexpr uintptr_t win_hwnd = 0x00D7713C;  // HWND
    constexpr uintptr_t gfx_module = 0x00D53E80;  // HMODULE of gfx_d3d_mp_x86_s.dll
    constexpr uintptr_t mouse_windowIsActive = 0x00D52A60;  // int
    constexpr uintptr_t mouse_ingameCursorActive = 0x00D52A68;  // byte
    constexpr uintptr_t mouse_enabled = 0x00D52A69;  // byte, gates both the vanilla and the CoD2x mouse loop
    constexpr uintptr_t fs_gamedir = 0x00B1A4A8;  // char[]

    // --- dvar pointers (dvar_t*) ---------------------------------------------------------------------------------
    constexpr uintptr_t dvar_timescale = 0x00C260FC;
    constexpr uintptr_t dvar_fixedtime = 0x00C260F0;
    constexpr uintptr_t dvar_cl_ingame = 0x0096B61C;
    constexpr uintptr_t dvar_cl_freezeDemo = 0x0096B604;
    constexpr uintptr_t dvar_cg_draw2D = 0x014C3660;
    constexpr uintptr_t dvar_cg_thirdPerson = 0x014B5BDC;
    constexpr uintptr_t dvar_cg_thirdPersonRange = 0x0166BAA0;
    constexpr uintptr_t dvar_cg_thirdPersonAngle = 0x0166E024;
    constexpr uintptr_t dvar_cg_fov = 0x0166BB78;
    constexpr uintptr_t dvar_cg_drawGun = 0x014C3674;
    constexpr uintptr_t dvar_cg_drawCrosshair = 0x014B5BF4;
    constexpr uintptr_t dvar_cl_bypassMouseInput = 0x006067D0;
    constexpr uintptr_t dvar_sv_cheats = 0x00C5C5CC;
    constexpr uintptr_t dvar_fs_homepath = 0x00B1E7E0;

    // --- renderer (gfx_d3d_mp_x86_s.dll) RVAs --------------------------------------------------------------------
    namespace GfxRVA
    {
        constexpr uint32_t d3d9 = 0x001D1BF4;        // IDirect3D9*
        constexpr uint32_t d3d9Device = 0x001D1BF8;  // IDirect3DDevice9*
        constexpr uint32_t R_RenderScene = 0x00025B90;
        constexpr uint32_t R_EndFrame = 0x00022390;
        constexpr uint32_t R_SetSunLightOverride = 0x00001430;
        constexpr uint32_t R_ResetSunLightOverride = 0x00001460;
    }  // namespace GfxRVA

    inline HMODULE GetGfxModule()
    {
        return *reinterpret_cast<HMODULE*>(gfx_module);
    }

    // Resolves a renderer RVA against the *current* module base. Never cache the result: the DLL is unloaded and
    // reloaded by vid_restart.
    inline uintptr_t Gfx(uint32_t rva)
    {
        const auto base = reinterpret_cast<uintptr_t>(GetGfxModule());
        return base ? base + rva : 0;
    }

    inline bool VerifyGameVersion()
    {
        return std::strncmp(reinterpret_cast<const char*>(VersionString), ExpectedVersion, 16) == 0;
    }
}  // namespace IWXMVM::IW2::Addresses
