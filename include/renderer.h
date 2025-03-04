#pragma once
#if defined(_WIN32)
#define VK_USE_PLATFORM_WIN32_KHR
#define NOMINMAX
#endif

#include <cstdint>
#include <vector>
#include <vulkan/vulkan.h>
#include "common.h"

#if defined(_WIN32)
#include <Windows.h>
#endif



namespace kvk {

	struct InitSettings {
		const char* appName;
		std::uint32_t width;
		std::uint32_t height;

#if defined(_WIN32)
		HWND window;
#endif
	};

	struct Pipeline {
		VkShaderModule vertexShader;
		VkShaderModule fragmentShader;
		VkPipelineLayout layout;
		VkPipeline pipeline;
	};

	struct RendererState {
		VkInstance instance;
		VkDevice device;
		VkPhysicalDevice physicalDevice;
		std::uint32_t graphicsFamilyIndex;
		std::uint32_t presentFamilyIndex;
		std::uint32_t computeFamilyIndex;

		VkQueue graphicsQueue;
		VkQueue presentQueue;
		VkQueue computeQueue;

		VkSurfaceKHR surface;

		//
		// Swapchain stuff
		//
		VkSwapchainKHR swapchain;
		std::vector<VkImage> swapchainImages;
		std::vector<VkImageView> swapchainImageViews;
		VkExtent2D swapchainExtent;
	};


	ReturnCode init(RendererState& state, const InitSettings* settings);
	ReturnCode createPipeline(const RendererState& state, 
							  Pipeline& pipeline,
							  const char* vertexShaderPath,
							  const char* fragmentShaderPath);

	ReturnCode createPipeline(const RendererState& state,
							  Pipeline& pipeline,
							  const std::uint32_t* vertexShaderData,   const size_t vertexShaderSize,
							  const std::uint32_t* fragmentShaderData, const size_t fragmentShaderSize);
}
