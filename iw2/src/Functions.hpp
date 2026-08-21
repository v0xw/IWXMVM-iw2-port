#pragma once
#include "StdInclude.hpp"
#include "Structures.hpp"

namespace IWXMVM::IW2::Functions
{
    // dvars
    Structures::dvar_t* FindDvar(std::string_view name);
    void Dvar_SetBool(Structures::dvar_t* dvar, bool value);
    void Dvar_SetInt(Structures::dvar_t* dvar, int value);
    void Dvar_SetFloat(Structures::dvar_t* dvar, float value);
    void Dvar_SetString(Structures::dvar_t* dvar, const char* value);

    // console / commands
    void Cbuf_AddText(std::string_view command);
    void Com_Printf(std::string_view text);

    // client
    void CL_FirstSnapshot();
    void CG_ExecuteNewServerCommands(int latestSequence);

    // huffman-compressed message support
    void EnsureHuffmanInitialized();
    int MSG_ReadBitsCompress(const uint8_t* from, uint8_t* to, int compressedSize);

    // bit-message readers (the game's own, so bit positions stay consistent with delta decoding)
    int MSG_ReadBits(Structures::msg_t* msg, int bits);
    int MSG_ReadByte(Structures::msg_t* msg);
    int MSG_ReadLong(Structures::msg_t* msg);
    const char* MSG_ReadString(Structures::msg_t* msg);
    const char* MSG_ReadBigString(Structures::msg_t* msg);
    // returns true if the entity was removed (then 'to' is not valid)
    bool MSG_ReadDeltaEntity(Structures::msg_t* msg, const Structures::entityState_t* from, Structures::entityState_t* to, int number);
    void MSG_ReadDeltaPlayerstate(Structures::msg_t* msg, const void* from, void* to);

    // math
    void AnglesToAxis(const float* angles, float axis[3][3]);

    // filesystem helpers
    std::filesystem::path GetGameDirectory();   // fs_homepath (falls back to the exe directory)
    std::string GetGameDirName();               // fs_game or "main"
    std::filesystem::path GetDemoDirectory();   // <game dir>/<fs_game|main>/demos
}  // namespace IWXMVM::IW2::Functions
