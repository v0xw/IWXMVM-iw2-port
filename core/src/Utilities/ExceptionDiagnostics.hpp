#pragma once

namespace IWXMVM::ExceptionDiagnostics
{
    // "0x12345678 (module.dll+0x1234)", or just "0x12345678" when the address lies outside every loaded module
    std::string DescribeAddress(uintptr_t address);

    // "access violation reading 0x0 at 0x12345678 (module.dll+0x1234)", "C++ exception of type 'class
    // std::runtime_error'", "exception 0x406d1388 at ..., parameters 0x1000, ..."
    std::string DescribeException(const EXCEPTION_RECORD& record);

    // Installs a vectored exception handler that sees every exception the moment it is raised, before any catch
    // block can swallow it (with /EHa, catch(...) catches CPU faults too). CPU faults are logged as errors, C++
    // throws and custom codes at debug level, each with a rough backtrace, and the latest one is remembered per
    // thread. It only observes and never handles anything itself.
    void InstallFirstChanceLogger();
    void UninstallFirstChanceLogger();

    // Description of the most recent exception raised on the calling thread, as recorded by the first-chance
    // logger, for catch(...) blocks that would otherwise have nothing to report. Empty when none was raised;
    // the record is cleared once taken.
    std::string TakeLastExceptionOnThisThread();
}  // namespace IWXMVM::ExceptionDiagnostics
