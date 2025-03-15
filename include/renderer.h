#pragma once
#if defined(_WIN32)
#define VK_USE_PLATFORM_WIN32_KHR
#define NOMINMAX
#endif

#include <cstdint>
#include <vector>
#include <atomic>
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
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

	struct PushConstants {
		float time;
		int thingy;
		int thingy2;
	};

	struct Deletion {
		void(*deleteFunc)(void* handle);
		void* vkHandle;
	};

	struct Pipeline {
		VkPipelineLayout layout;
		VkPipeline pipeline;
	};

	struct PipelineBuilder {
		PipelineBuilder();

		std::vector<VkDynamicState> dynamicState;
		std::vector<VkPipelineShaderStageCreateInfo> shaderStages;
		std::vector<VkPushConstantRange> pushConstantRanges;
		std::vector<VkDescriptorSetLayout> descriptorSetLayouts;

		VkFormat colorAttachmentFormat;
		VkFormat depthAttachmentFormat;

		VkPipelineLayoutCreateInfo layoutCreateInfo;
		VkPipelineViewportStateCreateInfo viewportState;
		VkPipelineColorBlendAttachmentState colorBlendAttachment ;
		VkPipelineColorBlendStateCreateInfo blendState;
		VkPipelineVertexInputStateCreateInfo inputState;
		VkPipelineDynamicStateCreateInfo dynamicStateInfo;
		VkPipelineInputAssemblyStateCreateInfo inputAssembly;
		VkPipelineMultisampleStateCreateInfo multisample;
		VkPipelineDepthStencilStateCreateInfo depthStencil;
		VkPipelineRenderingCreateInfo renderInfo;
		VkPipelineRasterizationStateCreateInfo rasterizer;

		PipelineBuilder& setShaders(VkShaderModule vertexShader, VkShaderModule fragmentShader);
		PipelineBuilder& setInputTopology(VkPrimitiveTopology topology);
		PipelineBuilder& setPolygonMode(VkPolygonMode poly);
		PipelineBuilder& setCullMode(VkCullModeFlags cullMode, VkFrontFace face);
		PipelineBuilder& setColorAttachmentFormat(VkFormat format);
		PipelineBuilder& setDepthAttachmentFormat(VkFormat format);

		ReturnCode build(Pipeline& pipeline,
						 const VkDevice device);
	};

	struct AllocatedImage {
		VkImage image;
		VkImageView view;
		VmaAllocation allocation;
		VkExtent3D extent;
		VkFormat format;
	};

	struct FrameData {
		VkFence inFlightFence;
		VkCommandBuffer commandBuffer;
		VkSemaphore imageAvailableSemaphore;
		VkSemaphore renderFinishedSemaphore;

		std::vector<Deletion> deletionQueue;
	};

	static constexpr std::uint32_t MAX_IN_FLIGHT_FRAMES = 2;
	struct RendererState {
		std::atomic<bool> isInitialized;
		std::vector<Deletion> mainDeletionQueue;

		VmaAllocator allocator;

		VkInstance instance;
		VkDevice device;
		VkPhysicalDevice physicalDevice;
		std::uint32_t graphicsFamilyIndex;
		std::uint32_t presentFamilyIndex;
		std::uint32_t computeFamilyIndex;

		VkCommandPool commandPool;
		VkDescriptorPool descriptorPool;
		VkDescriptorSet drawImageDescriptors;
		VkDescriptorSetLayout drawImageDescriptorLayout;
		
		VkQueue graphicsQueue;
		VkQueue presentQueue;
		VkQueue computeQueue;

		VkSurfaceKHR surface;

		FrameData frames[MAX_IN_FLIGHT_FRAMES];
		AllocatedImage drawImage;

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

	ReturnCode createShaderModuleFromFile(VkShaderModule& shaderModule,
										  VkDevice device,
										  const char* shaderPath);

	ReturnCode createShaderModuleFromMemory(VkShaderModule& shaderModule,
											VkDevice device,
											const std::uint32_t* shaderContents,
											const std::uint64_t shaderSize);

	ReturnCode recordCommandBuffer(VkCommandBuffer commandBuffer,
								   VkImage drawImage,
								   VkImageView drawImageView,
								   const VkExtent2D& extent,
								   VkImage image,
								   VkDescriptorSet drawImageDescriptors,
								   const Pipeline& pipeline);

	
	ReturnCode createSwapchain(RendererState& state,
							   VkExtent2D extent,
							   VkSurfaceFormatKHR format,
							   VkPresentModeKHR presentMode,
							   std::uint32_t imageCount,
							   VkSwapchainKHR oldSwapchain = VK_NULL_HANDLE);

	ReturnCode recreateSwapchain(RendererState& state,
								 Pipeline& pipeline,
								 const std::uint32_t x,
								 const std::uint32_t y);

	ReturnCode createPipeline(Pipeline& pipeline,
							  const VkDevice device,
							  const VkDescriptorSetLayout setLayout);
}
