#include "StdInclude.hpp"
#include "WindowsConsole.hpp"

namespace IWXMVM
{
    FILE* stream = nullptr;

    void WindowsConsole::Open()
    {
        // Try to allocate the windows console, if it already exists we set the std handle to ours.
        if (GetConsoleWindow() == NULL)
        {
            AllocConsole();
            freopen_s(&stream, "CONOUT$", "w", stdout);
        }
        else
        {
            HANDLE hConsole = NULL;

            if (stream != NULL)
                hConsole = (HANDLE)_get_osfhandle(_fileno(stream));

            if (hConsole != NULL)
                SetStdHandle(STD_OUTPUT_HANDLE, hConsole);
        }

        // Disable quick-edit mode: with it enabled, a stray click into the console window starts a text
        // selection and every subsequent console write blocks until the selection is cleared - which freezes
        // the game's main thread as soon as anything is logged.
        HANDLE hInput = CreateFileA("CONIN$", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                    nullptr, OPEN_EXISTING, 0, nullptr);
        if (hInput != INVALID_HANDLE_VALUE)
        {
            DWORD mode = 0;
            if (GetConsoleMode(hInput, &mode))
            {
                SetConsoleMode(hInput, (mode & ~ENABLE_QUICK_EDIT_MODE) | ENABLE_EXTENDED_FLAGS);
            }
            CloseHandle(hInput);
        }
    }

    void WindowsConsole::Close()
    {
        fclose(stream);
        FreeConsole();
    }
}  // namespace IWXMVM