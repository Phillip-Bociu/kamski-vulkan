#pragma once
#if defined(_WIN32)
#define VK_USE_PLATFORM_WIN32_KHR
#define NOMINMAX
#endif

#include <cstdint>
#include <vector>
#include <span>
#include <atomic>
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
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

	struct Deletion {
		void(*deleteFunc)(void* handle);
		void* vkHandle;
	};

	struct Pipeline {
		VkPipelineLayout layout;
		VkPipeline pipeline;
	};

	struct AllocatedImage {
		VkImage image;
		VkImageView view;
		VmaAllocation allocation;
		VkExtent3D extent;
		VkFormat format;
	};

	struct AllocatedBuffer {
		VkBuffer buffer;
		VmaAllocation allocation;
		VmaAllocationInfo info;
	};

	struct PipelineBuilder {
		PipelineBuilder();

		std::vector<VkDynamicState> dynamicState;
		std::vector<VkPipelineShaderStageCreateInfo> shaderStages;
		std::vector<VkPushConstantRange> pushConstantRanges;
		std::vector<VkDescriptorSetLayout> descriptorSetLayouts;

		VkFormat colorAttachmentFormat;

		VkPipelineLayoutCreateInfo layoutCreateInfo;
		VkPipelineViewportStateCreateInfo viewportState;
		VkPipelineColorBlendAttachmentState colorBlendAttachment;
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
		PipelineBuilder& enableDepthTest(bool depthWriteEnable, VkCompareOp op);

		PipelineBuilder& addPushConstantRange(VkShaderStageFlags stage,
											  std::uint32_t size,
											  std::uint32_t offset = 0);

		ReturnCode build(Pipeline& pipeline,
						 const VkDevice device);
	};

	struct FrameData {
		VkFence inFlightFence;
		VkCommandBuffer commandBuffer;
		VkSemaphore imageAvailableSemaphore;
		VkSemaphore renderFinishedSemaphore;

		std::vector<Deletion> deletionQueue;
	};

	struct Vertex {
		glm::vec3 position;
		float uvX;
		glm::vec3 normal;
		float uvY;
		glm::vec4 color;
	};

	struct PushConstants {
		glm::mat4 worldMatrix;
		VkDeviceAddress vertexBuffer;
	};

	struct Mesh {
		AllocatedBuffer indices;
		AllocatedBuffer vertices;
		VkDeviceAddress vertexBufferAddress;
	};

	struct GeoSurface {
		std::uint32_t startIndex;
		std::uint32_t count;
	};

	struct MeshAsset {
		Mesh mesh;
		std::vector<GeoSurface> surfaces;
	};

	static constexpr std::uint32_t MAX_IN_FLIGHT_FRAMES = 2;
	struct RendererState {
		std::atomic<bool> isInitialized;
		std::vector<Deletion> mainDeletionQueue;

		VmaAllocator allocator;

		VkInstance instance;
		VkDevice device;
		VkPhysicalDevice physicalDevice;
		std::uint32_t transferFamilyIndex;
		std::uint32_t graphicsFamilyIndex;
		std::uint32_t presentFamilyIndex;
		std::uint32_t computeFamilyIndex;

		VkCommandPool commandPool;
		VkDescriptorPool descriptorPool;
		VkDescriptorSet drawImageDescriptors;
		VkDescriptorSetLayout drawImageDescriptorLayout;
		
		VkQueue transferQueue;
		VkQueue graphicsQueue;
		VkQueue presentQueue;
		VkQueue computeQueue;

		VkSurfaceKHR surface;

		FrameData frames[MAX_IN_FLIGHT_FRAMES];
		VkCommandBuffer transferCommandBuffers[4];

		AllocatedImage drawImage;
		AllocatedImage depthImage;

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
								   AllocatedImage& drawImage,
								   AllocatedImage& depthImage,
								   const VkExtent2D& extent,
								   VkImage image,
								   VkDescriptorSet drawImageDescriptors,
								   const Pipeline& pipeline,
								   const Pipeline& meshPipeline,
								   const std::vector<MeshAsset>& meshes);

	
	ReturnCode createSwapchain(RendererState& state,
							   VkExtent2D extent,
							   VkSurfaceFormatKHR format,
							   VkPresentModeKHR presentMode,
							   std::uint32_t imageCount,
							   VkSwapchainKHR oldSwapchain = VK_NULL_HANDLE);

	ReturnCode recreateSwapchain(RendererState& state,
								 const std::uint32_t x,
								 const std::uint32_t y);

	ReturnCode createMesh(Mesh& mesh,
						  RendererState& state,
						  std::span<std::uint32_t> indices, 
						  std::span<Vertex> vertices);

	ReturnCode createBuffer(AllocatedBuffer& buffer,
							VmaAllocator allocator,
							std::uint64_t size,
							VkBufferUsageFlags bufferUsage,
							VmaMemoryUsage memoryUsage);

	void destroyBuffer(AllocatedBuffer& buffer,
					   VmaAllocator allocator);

	ReturnCode createImage(AllocatedImage& image,
						   RendererState& state,
						   const VkFormat format,
						   const VkExtent3D extent,
						   const VkImageUsageFlags usageFlags,
						   const VkImageAspectFlags aspectFlags);

	ReturnCode loadGltf(std::vector<MeshAsset>& retval,
						RendererState& state,
						const char* filePath);
}
