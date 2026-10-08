#pragma once

// Build-system selection wins; direct Linux/Make builds retain their default.
#if !defined(FADEROS_PLATFORM_POSIX) && !defined(FADEROS_PLATFORM_PICO) && !defined(FADEROS_PLATFORM_CORE)
# if defined(__linux__)
#  define FADEROS_PLATFORM_POSIX 1
# else
#  error "Select FADEROS_PLATFORM_POSIX, FADEROS_PLATFORM_PICO or FADEROS_PLATFORM_CORE"
# endif
#endif
#if (defined(FADEROS_PLATFORM_POSIX) + defined(FADEROS_PLATFORM_PICO) + defined(FADEROS_PLATFORM_CORE)) != 1
# error "Select exactly one faderOS platform"
#endif

namespace bkds::link {
#if defined(FADEROS_PLATFORM_PICO)
inline constexpr const char* PlatformName="pico";
#elif defined(FADEROS_PLATFORM_POSIX)
inline constexpr const char* PlatformName="posix";
#else
inline constexpr const char* PlatformName="core";
#endif
}
