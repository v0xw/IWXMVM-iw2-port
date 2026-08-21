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

        void Initialize();
    } // namespace Rewinding
}  // namespace IWXMVM::Components