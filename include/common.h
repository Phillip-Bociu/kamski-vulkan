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

namespace kvk {

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

#ifndef defer

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

#endif
