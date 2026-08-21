#include "StdInclude.hpp"
#include "Functions.hpp"

#include "Addresses.hpp"
#include "Utilities/PathUtils.hpp"

namespace IWXMVM::IW2::Functions
{
    using namespace Structures;

    Structures::dvar_t* FindDvar(std::string_view name)
    {
        typedef dvar_t*(__cdecl * Dvar_GetDvarByName_t)(const char* name);
        static const auto Dvar_GetDvarByName = reinterpret_cast<Dvar_GetDvarByName_t>(Addresses::Dvar_GetDvarByName);

        char buffer[256];
        const auto length = std::min(name.size(), sizeof(buffer) - 1);
        std::memcpy(buffer, name.data(), length);
        buffer[length] = '\0';

        return Dvar_GetDvarByName(buffer);
    }

    void Dvar_SetBool(dvar_t* dvar, bool value)
    {
        typedef void(__cdecl * Dvar_SetBool_t)(dvar_t*, int);
        reinterpret_cast<Dvar_SetBool_t>(Addresses::Dvar_SetBool)(dvar, value ? 1 : 0);
    }

    void Dvar_SetInt(dvar_t* dvar, int value)
    {
        typedef void(__cdecl * Dvar_SetInt_t)(dvar_t*, int);
        reinterpret_cast<Dvar_SetInt_t>(Addresses::Dvar_SetInt)(dvar, value);
    }

    void Dvar_SetFloat(dvar_t* dvar, float value)
    {
        typedef void(__cdecl * Dvar_SetFloat_t)(dvar_t*, float);
        reinterpret_cast<Dvar_SetFloat_t>(Addresses::Dvar_SetFloat)(dvar, value);
    }

    void Dvar_SetString(dvar_t* dvar, const char* value)
    {
        typedef void(__cdecl * Dvar_SetString_t)(dvar_t*, const char*);
        reinterpret_cast<Dvar_SetString_t>(Addresses::Dvar_SetString)(dvar, value);
    }

    // Kept free of C++ objects so the compiler does not use EBX as a frame pointer here (C4731)
    static void __declspec(noinline) Cbuf_AddTextRaw(const char* text)
    {
        // Cbuf_AddText takes the text pointer in EAX
        const auto address = Addresses::Cbuf_AddText;

        __asm
        {
            push ebx
            push esi
            push edi
            mov eax, text
            call address
            pop edi
            pop esi
            pop ebx
        }
    }

    void Cbuf_AddText(std::string_view command)
    {
        std::string text(command);
        Cbuf_AddTextRaw(text.c_str());
    }

    void Com_Printf(std::string_view text)
    {
        typedef void(__cdecl * Com_Printf_t)(const char* fmt, ...);
        std::string str(text);
        reinterpret_cast<Com_Printf_t>(Addresses::Com_Printf)("%s", str.c_str());
    }

    void CL_FirstSnapshot()
    {
        typedef void(__cdecl * CL_FirstSnapshot_t)();
        reinterpret_cast<CL_FirstSnapshot_t>(Addresses::CL_FirstSnapshot)();
    }

    void CG_ExecuteNewServerCommands(int latestSequence)
    {
        // __usercall: latest sequence in ESI, EDI (low byte, "localClientNum"-ish) = 0
        const auto address = Addresses::CG_ExecuteNewServerCommands;

        __asm
        {
            pushad
            mov esi, latestSequence
            xor edi, edi
            call address
            popad
        }
    }

    void EnsureHuffmanInitialized()
    {
        auto& initialized = *reinterpret_cast<int*>(Addresses::Huff_Initialized);
        if (!initialized)
        {
            initialized = 1;
            typedef void(__cdecl * Huff_Init_t)();
            reinterpret_cast<Huff_Init_t>(Addresses::Huff_Init)();
        }
    }

    int MSG_ReadBitsCompress(const uint8_t* from, uint8_t* to, int compressedSize)
    {
        // __usercall: from in EAX; to and size pushed (caller cleans up); returns the decompressed size in EAX
        const auto address = Addresses::MSG_ReadBitsCompress;
        int result = 0;

        __asm
        {
            pushad
            mov eax, from
            push compressedSize
            push to
            call address
            add esp, 8
            mov result, eax
            popad
        }

        return result;
    }

    int MSG_ReadBits(msg_t* msg, int bits)
    {
        // __usercall: msg in EDX, bits on the stack - and the CALLER pops it (plain ret), so this must not be
        // declared __fastcall (callee-cleanup) or every call shifts ESP by 4
        const auto address = Addresses::MSG_ReadBits;
        int result = 0;

        __asm
        {
            push ebx
            push esi
            push edi
            push bits
            mov edx, msg
            call address
            add esp, 4
            mov result, eax
            pop edi
            pop esi
            pop ebx
        }

        return result;
    }

    int MSG_ReadByte(msg_t* msg)
    {
        typedef int(__fastcall * MSG_ReadByte_t)(msg_t* msg, int unused);
        return reinterpret_cast<MSG_ReadByte_t>(Addresses::MSG_ReadByte)(msg, 0);
    }

    int MSG_ReadLong(msg_t* msg)
    {
        typedef int(__fastcall * MSG_ReadLong_t)(msg_t* msg, int unused);
        return reinterpret_cast<MSG_ReadLong_t>(Addresses::MSG_ReadLong)(msg, 0);
    }

    const char* MSG_ReadString(msg_t* msg)
    {
        typedef const char*(__fastcall * MSG_ReadString_t)(int unused, msg_t* msg);
        return reinterpret_cast<MSG_ReadString_t>(Addresses::MSG_ReadString)(0, msg);
    }

    const char* MSG_ReadBigString(msg_t* msg)
    {
        typedef const char*(__fastcall * MSG_ReadBigString_t)(int unused, msg_t* msg);
        return reinterpret_cast<MSG_ReadBigString_t>(Addresses::MSG_ReadBigString)(0, msg);
    }

    bool MSG_ReadDeltaEntity(msg_t* msg, const entityState_t* from, entityState_t* to, int number)
    {
        // __usercall: to in EAX, msg in EBX; stack: from, number, numFields, fieldTable (caller cleans up)
        const auto address = Addresses::MSG_ReadDeltaEntity;
        const auto fieldTable = Addresses::entityStateFields;
        const int fieldCount = Addresses::entityStateFieldCount;
        int removed = 0;

        __asm
        {
            pushad
            push fieldTable
            push fieldCount
            push number
            push from
            mov eax, to
            mov ebx, msg
            call address
            add esp, 16
            mov removed, eax
            popad
        }

        return removed != 0;
    }

    void MSG_ReadDeltaPlayerstate(msg_t* msg, const void* from, void* to)
    {
        // __usercall: msg in EBX; stack: from, to (caller cleans up)
        const auto address = Addresses::MSG_ReadDeltaPlayerstate;

        __asm
        {
            pushad
            push to
            push from
            mov ebx, msg
            call address
            add esp, 8
            popad
        }
    }

    void AnglesToAxis(const float* angles, float axis[3][3])
    {
        // __usercall: axis (out) in EAX, angles (in) in EDX
        const auto address = Addresses::AnglesToAxis;
        float* axisPtr = &axis[0][0];

        __asm
        {
            pushad
            mov eax, axisPtr
            mov edx, angles
            call address
            popad
        }
    }

    std::filesystem::path GetGameDirectory()
    {
        const auto fs_homepath = FindDvar("fs_homepath");
        if (fs_homepath && fs_homepath->value.string && fs_homepath->value.string[0])
        {
            return std::filesystem::path(fs_homepath->value.string);
        }

        const auto fs_basepath = FindDvar("fs_basepath");
        if (fs_basepath && fs_basepath->value.string && fs_basepath->value.string[0])
        {
            return std::filesystem::path(fs_basepath->value.string);
        }

        return std::filesystem::path(PathUtils::GetCurrentGameDirectory());
    }

    std::string GetGameDirName()
    {
        const auto fs_game = FindDvar("fs_game");
        if (fs_game && fs_game->value.string && fs_game->value.string[0])
        {
            return fs_game->value.string;
        }

        return "main";
    }

    std::filesystem::path GetDemoDirectory()
    {
        return GetGameDirectory() / GetGameDirName() / "demos";
    }
}  // namespace IWXMVM::IW2::Functions
