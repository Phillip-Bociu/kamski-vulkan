#pragma once

#include <vulkan/vulkan.h>
#include "common.h"
#include <Windows.h>


namespace kvk {
	struct RendererState;
	ReturnCode createWin32Surface(RendererState& state, HWND window);
}
