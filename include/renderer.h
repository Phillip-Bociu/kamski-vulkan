#pragma once
#if defined(_WIN32)
#define VK_USE_PLATFORM_WIN32_KHR
#define NOMINMAX
#endif

#include <cstdint>
#include <vector>
#include <atomic>
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

		VkRenderPass renderPass;
		VkPipelineLayout layout;

		VkPipeline pipeline;
	};

	static constexpr std::uint32_t MAX_IN_FLIGHT_FRAMES = 2;
	struct RendererState {
		std::atomic<bool> isInitialized;
		VkInstance instance;
		VkDevice device;
		VkPhysicalDevice physicalDevice;
		std::uint32_t graphicsFamilyIndex;
		std::uint32_t presentFamilyIndex;
		std::uint32_t computeFamilyIndex;

		VkCommandPool commandPool;
		VkCommandBuffer commandBuffers[MAX_IN_FLIGHT_FRAMES];
		
		VkQueue graphicsQueue;
		VkQueue presentQueue;
		VkQueue computeQueue;

		VkSurfaceKHR surface;
		VkFence inFlightFences[MAX_IN_FLIGHT_FRAMES];
		VkSemaphore imageAvailableSemaphores[MAX_IN_FLIGHT_FRAMES];
		VkSemaphore renderFinishedSemaphores[MAX_IN_FLIGHT_FRAMES];

		//
		// Swapchain stuff
		//
		VkSwapchainKHR swapchain;
		std::uint32_t swapchainImageCount;
		std::vector<VkImage> swapchainImages;
		std::vector<VkImageView> swapchainImageViews;
		std::vector<VkFramebuffer> framebuffers;
		VkExtent2D swapchainExtent;
		VkSurfaceFormatKHR swapchainImageFormat;
		VkPresentModeKHR swapchainPresentMode;
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

	ReturnCode recordCommandBuffer(VkCommandBuffer commandBuffer,
								   Pipeline& pipeline,
								   VkFramebuffer framebuffer,
								   const VkExtent2D& extent);
	
	ReturnCode createSwapchain(RendererState& state,
							   VkExtent2D extent,
							   VkSurfaceFormatKHR format,
							   VkPresentModeKHR presentMode,
							   std::uint32_t imageCount);

	ReturnCode createFramebuffers(RendererState& state,
								 Pipeline& pipeline);

	ReturnCode recreateSwapchain(RendererState& state,
								 Pipeline& pipeline,
								 const std::uint32_t x,
								 const std::uint32_t y);
}
