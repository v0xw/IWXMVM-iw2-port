#pragma once
#include "StdInclude.hpp"
#include "Addresses.hpp"

// Only the parts of the CoD2 1.3 client structures that the mod actually touches.
// Layouts taken from CoD2x (src/shared/cod2_*.h) and the decompiles.
namespace IWXMVM::IW2::Structures
{
    typedef float vec3_t[3];

    enum connstate_t : int
    {
        CA_DISCONNECTED = 0,
        CA_CINEMATIC = 1,
        CA_AUTHORIZING = 2,
        CA_CONNECTING = 3,
        CA_CHALLENGING = 4,
        CA_CONNECTED = 5,
        CA_LOADING = 6,
        CA_PRIMED = 7,
        CA_ACTIVE = 8,
    };

    enum keyCatcher_t : int
    {
        KEYCATCH_CONSOLE = 1,
        KEYCATCH_UI = 8,
        KEYCATCH_MESSAGE = 0x10,
    };

    // server -> client command bytes inside a (decompressed) server message
    enum svc_ops_e : uint8_t
    {
        svc_nop = 0,
        svc_gamestate = 1,
        svc_serverCommand = 4,
        svc_download = 5,
        svc_snapshot = 6,
        svc_EOF = 7,
    };

    enum dvarFlags_e : uint16_t
    {
        DVAR_ARCHIVE = 0x1,
        DVAR_USERINFO = 0x2,
        DVAR_SERVERINFO = 0x4,
        DVAR_SYSTEMINFO = 0x8,
        DVAR_NOWRITE = 0x10,
        DVAR_LATCH = 0x20,
        DVAR_ROM = 0x40,
        DVAR_CHEAT = 0x80,
    };

    enum dvarType_e : uint8_t
    {
        DVAR_TYPE_BOOL = 0,
        DVAR_TYPE_FLOAT = 1,
        DVAR_TYPE_VEC2 = 2,
        DVAR_TYPE_VEC3 = 3,
        DVAR_TYPE_VEC4 = 4,
        DVAR_TYPE_INT = 5,
        DVAR_TYPE_ENUM = 6,
        DVAR_TYPE_STRING = 7,
        DVAR_TYPE_COLOR = 8,
    };

    union DvarLimits
    {
        struct
        {
            int stringCount;
            const char** strings;
        } enumeration;
        struct
        {
            int min;
            int max;
        } integer;
        struct
        {
            float min;
            float max;
        } decimal;
    };

    // 4 bytes: vector types are stored as pointers, colors inline
    union dvarValue_t
    {
        bool boolean;
        int integer;
        float decimal;
        float* vec2;
        float* vec3;
        float* vec4;
        const char* string;
        unsigned char color[4];
    };
    static_assert(sizeof(dvarValue_t) == 4);

    struct dvar_t
    {
        const char* name;          // 0x00
        uint16_t flags;            // 0x04
        dvarType_e type;           // 0x06
        bool modified;             // 0x07
        dvarValue_t value;         // 0x08
        dvarValue_t latchedValue;  // 0x0C
        dvarValue_t defaultValue;  // 0x10
        DvarLimits limits;         // 0x14
        dvar_t* next;              // 0x1C
        dvar_t* hashNext;          // 0x20
    };
    static_assert(sizeof(dvar_t) == 0x24);

    struct refdef_t
    {
        int x;
        int y;
        int width;
        int height;
        float fov_x;  // degrees
        float fov_y;  // degrees
        vec3_t vieworg;
        vec3_t viewaxis[3];
        int time;
        int rdflags;
        uint8_t areamask[8];
    };
    static_assert(offsetof(refdef_t, fov_x) == 0x10);
    static_assert(offsetof(refdef_t, vieworg) == 0x18);
    static_assert(offsetof(refdef_t, viewaxis) == 0x24);
    static_assert(offsetof(refdef_t, time) == 0x48);

    enum entityType_t : int
    {
        ET_GENERAL = 0,
        ET_PLAYER = 1,
        ET_PLAYER_CORPSE = 2,
        ET_ITEM = 3,
        ET_MISSILE = 4,
        ET_INVISIBLE = 5,
        ET_SCRIPTMOVER = 6,
        ET_FX = 7,
        ET_LOOP_FX = 8,
        ET_TURRET = 9,
    };

    struct trajectory_t
    {
        int trType;
        int trTime;
        int trDuration;
        vec3_t trBase;
        vec3_t trDelta;
    };

    // 240 bytes. Only the leading fields are named; see CoD2x cod2_entity.h for the rest.
    struct entityState_t
    {
        int number;        // 0x00
        int eType;         // 0x04
        int eFlags;        // 0x08
        trajectory_t pos;  // 0x0C
        trajectory_t apos;  // 0x30
        int time;          // 0x54
        int time2;         // 0x58
        vec3_t origin2;    // 0x5C
        vec3_t angles2;    // 0x68
        int otherEntityNum;  // 0x74
        int attackerEntityNum;  // 0x78
        int groundEntityNum;  // 0x7C
        int constantLight;  // 0x80
        int loopSound;     // 0x84
        int surfType;      // 0x88
        int index;         // 0x8C
        int clientNum;     // 0x90
        int iHeadIcon;     // 0x94
        int iHeadIconTeam;  // 0x98
        int solid;         // 0x9C
        int eventParm;     // 0xA0
        int eventSequence;  // 0xA4
        int events[4];     // 0xA8
        int eventParms[4];  // 0xB8
        int weapon;        // 0xC8
        int weaponModel;   // 0xCC
        int legsAnim;      // 0xD0
        int torsoAnim;     // 0xD4
        uint8_t pad[240 - 0xD8];
    };
    static_assert(sizeof(entityState_t) == 240);
    static_assert(offsetof(entityState_t, clientNum) == 0x90);

    struct centity_t
    {
        entityState_t currentState;  // 0x000
        entityState_t nextState;     // 0x0F0
        int currentValid;            // 0x1E0
        int pad[2];
        vec3_t lerpOrigin;
        vec3_t lerpAngles;
        int pad2[8];
    };
    static_assert(sizeof(centity_t) == 548);
    static_assert(offsetof(centity_t, currentValid) == 0x1E0);

    struct clientInfo_t
    {
        int infoValid;
        int nextValid;
        int clientNum;
        char name[32];
        int team;
        int oldteam;
        int score;
        int location;
        int health;
        char model[64];
        uint8_t pad[0x4B8 - 0x80];  // named fields end at 0x80
    };
    static_assert(sizeof(clientInfo_t) == 0x4B8);

    // Only the prefix: the full clSnapshot_t is 9944 bytes.
    struct clSnapshot_t
    {
        int valid;
        int snapFlags;
        int serverTime;
        int messageNum;
        int deltaNum;
        int ping;
    };

    struct gameState_t
    {
        int stringOffsets[2048];
        char stringData[0x20000];
        int dataCount;
    };
    static_assert(sizeof(gameState_t) == 0x22004);

    // The engine's bit-message (24 bytes)
    struct msg_t
    {
        int overflowed;
        uint8_t* data;
        int maxsize;
        int cursize;
        int readcount;
        int bit;
    };

    // --- accessors -------------------------------------------------------------------------------------------------

    template <typename T>
    inline T* At(uintptr_t address)
    {
        return reinterpret_cast<T*>(address);
    }

    inline connstate_t GetConnectionState()
    {
        return *At<connstate_t>(Addresses::clc_state);
    }

    inline bool IsDemoPlaying()
    {
        return *At<int>(Addresses::clc_demoplaying) != 0;
    }

    inline int GetDemoFileHandle()
    {
        return *At<int>(Addresses::clc_demofile);
    }

    inline int& GetKeyCatchers()
    {
        return *At<int>(Addresses::keyCatchers);
    }

    inline refdef_t& GetRefdef()
    {
        return *At<refdef_t>(Addresses::cg_refdef);
    }

    inline float* GetRefdefViewAngles()
    {
        return At<float>(Addresses::cg_refdefViewAngles);
    }

    inline centity_t* GetEntities()
    {
        return At<centity_t>(Addresses::cg_entities);
    }

    inline clientInfo_t* GetClientInfo()
    {
        return At<clientInfo_t>(Addresses::clientInfo);
    }

    inline clSnapshot_t* GetSnapshot(uint32_t index)
    {
        return At<clSnapshot_t>(Addresses::cl_snapshots + index * Addresses::cl_snapshot_size);
    }

    inline gameState_t& GetGameState()
    {
        return *At<gameState_t>(Addresses::cl_gameState);
    }

    inline HWND GetWindowHandle()
    {
        return *At<HWND>(Addresses::win_hwnd);
    }

    inline const char* GetCmdArgv(int index)
    {
        if (index < 0 || index >= *At<int>(Addresses::cmd_argc))
            return "";
        const char* arg = At<const char*>(Addresses::cmd_argv)[index];
        return arg ? arg : "";
    }
}  // namespace IWXMVM::IW2::Structures
