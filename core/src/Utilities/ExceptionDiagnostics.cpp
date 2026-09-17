#include "StdInclude.hpp"
#include "ExceptionDiagnostics.hpp"

#include <typeinfo>

namespace IWXMVM::ExceptionDiagnostics
{
    constexpr DWORD CPP_EXCEPTION_CODE = 0xE06D7363;       // what MSVC's throw raises ('msc' | 0xE0000000)
    constexpr ULONG_PTR CPP_EXCEPTION_MAGIC = 0x19930520;  // ExceptionInformation[0] of such a record

    std::string DescribeAddress(uintptr_t address)
    {
        HMODULE module = nullptr;
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               reinterpret_cast<LPCSTR>(address), &module) &&
            module)
        {
            char name[MAX_PATH]{};
            GetModuleFileNameA(module, name, MAX_PATH);
            return std::format("{:#x} ({}+{:#x})", address, std::filesystem::path(name).filename().string(),
                               address - reinterpret_cast<uintptr_t>(module));
        }
        return std::format("{:#x}", address);
    }

    // True when [pointer, pointer + size) lies within committed, readable memory
    bool IsReadable(const void* pointer, size_t size)
    {
        MEMORY_BASIC_INFORMATION info{};
        if (!pointer || VirtualQuery(pointer, &info, sizeof(info)) == 0 || info.State != MEM_COMMIT ||
            (info.Protect & (PAGE_NOACCESS | PAGE_GUARD)) != 0)
        {
            return false;
        }
        const auto regionEnd = reinterpret_cast<uintptr_t>(info.BaseAddress) + info.RegionSize;
        return reinterpret_cast<uintptr_t>(pointer) + size <= regionEnd;
    }

    // Name of a CPU fault, nullptr for every other code (C++ throws, RaiseException with a custom code, ...)
    const char* GetFaultName(DWORD code)
    {
        switch (code)
        {
            case EXCEPTION_ACCESS_VIOLATION:
                return "access violation";
            case EXCEPTION_IN_PAGE_ERROR:
                return "in-page error";
            case EXCEPTION_ILLEGAL_INSTRUCTION:
                return "illegal instruction";
            case EXCEPTION_PRIV_INSTRUCTION:
                return "privileged instruction";
            case EXCEPTION_INT_DIVIDE_BY_ZERO:
                return "integer division by zero";
            case EXCEPTION_INT_OVERFLOW:
                return "integer overflow";
            case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
                return "array bounds exceeded";
            case EXCEPTION_DATATYPE_MISALIGNMENT:
                return "datatype misalignment";
            case EXCEPTION_BREAKPOINT:
                return "breakpoint";
            default:
                return nullptr;
        }
    }

#if defined(_M_IX86)
    // Just enough of MSVC's exception-handling metadata (see ehdata.h) to read a thrown object's type name.
    // On x86 these hold plain pointers; other targets store image-relative offsets instead.
    struct TypeDescriptor_
    {
        const void* vftable;
        void* spare;
        char name[1];
    };
    struct CatchableType_
    {
        unsigned int properties;
        const TypeDescriptor_* type;
    };
    struct CatchableTypeArray_
    {
        int count;
        const CatchableType_* types[1];
    };
    struct ThrowInfo_
    {
        unsigned int attributes;
        void* unwind;
        void* forwardCompat;
        const CatchableTypeArray_* catchableTypes;
    };
#endif

    // "class std::runtime_error" / "char const *" for an MSVC C++ exception, empty when it cannot be read
    std::string GetThrownTypeName(const EXCEPTION_RECORD& record)
    {
#if defined(_M_IX86)
        if (record.NumberParameters < 3 || record.ExceptionInformation[0] != CPP_EXCEPTION_MAGIC)
        {
            return {};
        }

        const auto throwInfo = reinterpret_cast<const ThrowInfo_*>(record.ExceptionInformation[2]);
        if (!IsReadable(throwInfo, sizeof(*throwInfo)) ||
            !IsReadable(throwInfo->catchableTypes, sizeof(CatchableTypeArray_)) || throwInfo->catchableTypes->count < 1)
        {
            return {};
        }

        const auto catchable = throwInfo->catchableTypes->types[0];
        if (!IsReadable(catchable, sizeof(*catchable)) || !IsReadable(catchable->type, sizeof(TypeDescriptor_)))
        {
            return {};
        }

        // the descriptor has the layout of std::type_info, so its name undecorates the usual way
        return reinterpret_cast<const std::type_info*>(catchable->type)->name();
#else
        return {};
#endif
    }

    std::string DescribeException(const EXCEPTION_RECORD& record)
    {
        const auto code = record.ExceptionCode;
        const auto location = DescribeAddress(reinterpret_cast<uintptr_t>(record.ExceptionAddress));

        if (code == CPP_EXCEPTION_CODE)
        {
            const auto typeName = GetThrownTypeName(record);
            return std::format("C++ exception of type '{}'", typeName.empty() ? "<unreadable>" : typeName);
        }

        if (const auto name = GetFaultName(code))
        {
            if ((code == EXCEPTION_ACCESS_VIOLATION || code == EXCEPTION_IN_PAGE_ERROR) && record.NumberParameters >= 2)
            {
                const auto kind = record.ExceptionInformation[0];
                const auto operation = kind == 0 ? "reading" : kind == 1 ? "writing" : "executing";
                return std::format("{} {} {:#x} at {}", name, operation,
                                   static_cast<uintptr_t>(record.ExceptionInformation[1]), location);
            }
            return std::format("{} at {}", name, location);
        }

        auto description = std::format("exception {:#010x} at {}", code, location);
        for (DWORD i = 0; i < record.NumberParameters && i < 4; i++)
        {
            description += std::format("{}{:#x}", i == 0 ? ", parameters " : ", ",
                                       static_cast<uintptr_t>(record.ExceptionInformation[i]));
        }
        return description;
    }

    // --- first-chance logging ---------------------------------------------------------------------------------

    constexpr int MAX_LOGGED_PER_KIND = 20;

    PVOID handlerHandle = nullptr;
    std::atomic<int> loggedFaults = 0;
    std::atomic<int> loggedOthers = 0;
    thread_local char lastException[256] = {};

    // Raised by Windows and the CRT in normal operation - remembered cheaply, never logged
    bool IsRoutine(DWORD code)
    {
        switch (code)
        {
            case 0x406D1388:  // MS_VC_EXCEPTION: thread naming for debuggers
            case 0x40010006:  // DBG_PRINTEXCEPTION_C: OutputDebugStringA
            case 0x4001000A:  // DBG_PRINTEXCEPTION_WIDE_C: OutputDebugStringW
            case EXCEPTION_GUARD_PAGE:  // stack growth
            case EXCEPTION_SINGLE_STEP:
                return true;
            default:
                return false;
        }
    }

    bool IsCodeAddress(uintptr_t address)
    {
        HMODULE module = nullptr;
        return address >= 0x10000 &&
               GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                  reinterpret_cast<LPCSTR>(address), &module) &&
               module != nullptr;
    }

    LONG CALLBACK FirstChanceLogger(EXCEPTION_POINTERS* info)
    {
        const auto& record = *info->ExceptionRecord;

        // A stack overflow leaves no room for formatting or logging; remember it in the cheapest possible way so
        // that a catch(...) further up can still report it once the stack has been unwound.
        if (record.ExceptionCode == EXCEPTION_STACK_OVERFLOW)
        {
            strcpy_s(lastException, "stack overflow");
            return EXCEPTION_CONTINUE_SEARCH;
        }

        static thread_local bool inHandler = false;
        if (inHandler)
        {
            return EXCEPTION_CONTINUE_SEARCH;
        }
        inHandler = true;

        if (IsRoutine(record.ExceptionCode))
        {
            snprintf(lastException, sizeof(lastException), "exception %#010lx", record.ExceptionCode);
            inHandler = false;
            return EXCEPTION_CONTINUE_SEARCH;
        }

        const auto description = DescribeException(record);
        strncpy_s(lastException, description.c_str(), _TRUNCATE);

        // CPU faults are always news. C++ exceptions and custom codes are usually caught by whoever raised them,
        // so they only get a debug line; when one escapes into a catch(...) the record above names it. Each kind
        // is capped so that something raised on every frame cannot flood the log.
        const bool fault = GetFaultName(record.ExceptionCode) != nullptr;
        auto& logged = fault ? loggedFaults : loggedOthers;
        if (logged.fetch_add(1) < MAX_LOGGED_PER_KIND)
        {
            const auto level = fault ? spdlog::level::err : spdlog::level::debug;
            const auto logger = Logger::GetInternalLogger();
            logger->log(level, "First-chance {} (thread {})", description, GetCurrentThreadId());

            // Without symbols for the game a proper stack walk is unreliable; scanning the top of the stack for
            // values that point into loaded modules gives a good enough picture of who called into the fault.
            // For a C++ throw the context is that of RaiseException, so the thrower shows up a few entries down.
#if defined(_M_IX86)
            const auto stackPointer = static_cast<uintptr_t>(info->ContextRecord->Esp);
#else
            const auto stackPointer = static_cast<uintptr_t>(info->ContextRecord->Rsp);
#endif
            int entries = 0;
            for (uintptr_t slot = stackPointer; slot < stackPointer + 0x800 && entries < 12; slot += sizeof(uintptr_t))
            {
                if (!IsReadable(reinterpret_cast<const void*>(slot), sizeof(uintptr_t)))
                {
                    break;
                }
                const auto value = *reinterpret_cast<const uintptr_t*>(slot);
                if (IsCodeAddress(value))
                {
                    logger->log(level, "  stack[{:#x}]: {}", slot - stackPointer, DescribeAddress(value));
                    entries++;
                }
            }
        }

        inHandler = false;
        return EXCEPTION_CONTINUE_SEARCH;
    }

    void InstallFirstChanceLogger()
    {
        if (!handlerHandle)
        {
            handlerHandle = AddVectoredExceptionHandler(1, FirstChanceLogger);
        }
    }

    void UninstallFirstChanceLogger()
    {
        if (handlerHandle)
        {
            RemoveVectoredExceptionHandler(handlerHandle);
            handlerHandle = nullptr;
        }
    }

    std::string TakeLastExceptionOnThisThread()
    {
        std::string description = lastException;
        lastException[0] = '\0';
        return description;
    }
}  // namespace IWXMVM::ExceptionDiagnostics
