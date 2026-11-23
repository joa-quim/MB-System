#pragma once

#include <cstddef>
#include <iomanip>
#include <sstream>
#include <string>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
// before windows.h, as mb_define.h has it, or windows.h brings in the old winsock.h first
#include <winsock2.h>
#include <windows.h>
#include <psapi.h>
// windows.h defines near and far as empty macros, and the mbmesh code uses them as names
#undef near
#undef far
#else
#include <sys/resource.h>
#endif

namespace mbmesh {

// The largest amount of physical memory this process has had in use at one time, in bytes, or 0 if the system does
// not say. getrusage() reports it in bytes on macOS and in kilobytes on Linux and the other systems; Windows has the
// peak working set (K32GetProcessMemoryInfo is in kernel32, so no psapi library is needed).
[[nodiscard]] inline std::size_t peak_memory_bytes() {
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS counters;
    if (!K32GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters))) {
        return 0;
    }
    return static_cast<std::size_t>(counters.PeakWorkingSetSize);
#else
    struct rusage usage;
    if (getrusage(RUSAGE_SELF, &usage) != 0 || usage.ru_maxrss < 0) {
        return 0;
    }
#if defined(__APPLE__)
    return static_cast<std::size_t>(usage.ru_maxrss);
#else
    return static_cast<std::size_t>(usage.ru_maxrss) * 1024;
#endif
#endif
}

// For example "512 MB" or "24.7 GB" (powers of 1000, as disk sizes are given).
[[nodiscard]] inline std::string format_bytes(double bytes) {
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(1);
    if (bytes >= 1.0e9) {
        stream << bytes / 1.0e9 << " GB";
    } else {
        stream << std::setprecision(0) << bytes / 1.0e6 << " MB";
    }
    return stream.str();
}

// "peak memory 24.7 GB", for the end of a log line.
[[nodiscard]] inline std::string peak_memory_text() {
    const std::size_t bytes = peak_memory_bytes();
    return bytes == 0 ? std::string("peak memory unknown") : "peak memory " + format_bytes(static_cast<double>(bytes));
}

} // namespace mbmesh
