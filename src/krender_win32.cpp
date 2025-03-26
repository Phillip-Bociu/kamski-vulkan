#include "krender_win32.h"

namespace kvk {

ReturnCode createWin32Surface(RendererState& state, HWND window) {
	VkWin32SurfaceCreateInfoKHR createInfo = {
		.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR,
		.hinstance = GetModuleHandle(nullptr),
		.hwnd = window,
	};

	if(vkCreateWin32SurfaceKHR(state.instance,
							   &createInfo,
							   nullptr,
							   &state.surface) != VK_SUCCESS) {
		logError("Could not create surface");
		return ReturnCode::UNKNOWN;
	}
	logDebug("Created surface");
	return ReturnCode::OK;
}

}
