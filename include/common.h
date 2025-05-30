#pragma once
#include <stdio.h>
#include <cstdint>

#ifdef PROFILER_ENABLED

#include <vulkan/vulkan_core.h>
#include <tracy/Tracy.hpp>
#include <tracy/TracyVulkan.hpp>
#define KVK_PROFILE() ZoneScoped
#define KVK_PROFILE_NAMED(name) ZoneScopedN(name)
#define KVK_SET_THREAD_NAME(name) tracy::SetThreadName(name)
#define KVK_FRAME_MARK() FrameMark
#define KVK_GPU_ZONE(name) TracyGpuZone(name)

#else

#define KVK_GPU_ZONE(name)
#define KVK_FRAME_MARK()
#define KVK_PROFILE()
#define KVK_PROFILE_NAMED(name)
#define KVK_SET_THREAD_NAME(name)

#endif

#define KB(x) (1024ull * std::uint64_t(x))
#define MB(x) (1024ull * KB(x))
#define GB(x) (1024ull * MB(x))
#define TB(x) (1024ull * GB(x))

namespace kvk {

#if defined(KVK_DEBUG) && !defined(logDebug)

#if defined (_WIN32) 
#define logDebug(format, ...)   printf("[DEBUG][%5d]: " __FILE__ ": " format "\n", __LINE__, __VA_ARGS__)
#define logInfo(format, ...)    printf("[INFO] [%5d]: " __FILE__ ": " format "\n", __LINE__, __VA_ARGS__)
#define logWarning(format, ...) printf("[WARN] [%5d]: " __FILE__ ": " format "\n", __LINE__, __VA_ARGS__)
#define logError(format, ...)   printf("[ERROR][%5d]: " __FILE__ ": " format "\n", __LINE__, __VA_ARGS__)

#elif defined (__linux__)

#define logDebug(format, ...)   printf("[DEBUG][%5d]: " __FILE__ ": " format "\n", __LINE__ __VA_OPT__(,) __VA_ARGS__)
#define logInfo(format, ...)    printf("[INFO] [%5d]: " __FILE__ ": " format "\n", __LINE__ __VA_OPT__(,) __VA_ARGS__)
#define logWarning(format, ...) printf("[WARN] [%5d]: " __FILE__ ": " format "\n", __LINE__ __VA_OPT__(,) __VA_ARGS__)
#define logError(format, ...)   printf("[ERROR][%5d]: " __FILE__ ": " format "\n", __LINE__ __VA_OPT__(,) __VA_ARGS__)

#endif
#else

#define logDebug(format, ...)
#define logInfo(format, ...)
#define logWarning(format, ...)
#define logError(format, ...)

#endif

enum class [[nodiscard]] ReturnCode {
	OK,
	WRONG_PARAMETERS,
	LAYER_NOT_FOUND,
	DEVICE_NOT_FOUND,
	QFAM_NOT_FOUND,
	SHADER_CREATION_ERROR,
	FILE_NOT_FOUND,
	UNKNOWN,
	COUNT,
};

}

template<typename Fn>
class _deferClass {
    public:
        _deferClass(Fn&& fn): fn(fn){}
        ~_deferClass() { fn(); }
    private:
        Fn fn;
};

#define CONCAT(a, b) a##b
#define CONCAT2(a, b) CONCAT(a, b)
#define MAKE_DEFER_NAME() CONCAT2(anon_deferVar_, __COUNTER__)
#define defer const _deferClass MAKE_DEFER_NAME() = [&]()
