#pragma once

namespace IWXMVM::Components
{
    namespace Rewinding
    {
        // How the game module classifies a read on the demo file
        enum class DemoReadKind
        {
            MessageStart,  // first read of a new demo message (header)
            Payload,       // the message body
            Other          // remaining header fields / anything else
        };

        bool CheckSkipForward();
        bool IsRewinding();
        void RewindBy(std::int32_t ticks);

        int FS_Seek(int offset, int origin);
        int FS_Read(void* buffer, int len, DemoReadKind kind);

        // Compatibility overload for game modules that don't classify their demo reads: infers the
        // read kind from the length, exactly like this function did before DemoReadKind existed
        // (a 1-byte read starts a new message; anything larger than 12 bytes is the payload)
        inline int FS_Read(void* buffer, int len)
        {
            return FS_Read(buffer, len,
                           len == 1 ? DemoReadKind::MessageStart
                                    : (len > 12 ? DemoReadKind::Payload : DemoReadKind::Other));
        }

        void Initialize();
    } // namespace Rewinding
}  // namespace IWXMVM::Components