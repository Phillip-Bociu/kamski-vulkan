#pragma once
#include "glm/fwd.hpp"
#include "vulkan/vulkan_core.h"
#if defined(_WIN32)
#define VK_USE_PLATFORM_WIN32_KHR
#define NOMINMAX
#endif

#include <cstdint>
#include <vector>
#include <span>
#include <atomic>
#include <deque>
#include <functional>

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include "common.h"

#if defined(_WIN32)
#include <Windows.h>
#endif

#define VK_CHECK(call) \
	do { \
		VkResult result = call;\
		if(result != VK_SUCCESS) {\
			logError(#call "failed: %d", int(result));\
			return ReturnCode::UNKNOWN;\
		}\
	}while(0)

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

	struct DescriptorAllocator {
		struct PoolSizeRatio {
			VkDescriptorType type;
			float ratio;
		};

		void init(VkDevice device,
				  std::uint32_t initialSets,
				  std::span<PoolSizeRatio> poolRatios);

		void clearPools(VkDevice device);
		void destroyPools(VkDevice device);

		ReturnCode alloc(VkDescriptorSet& set,
						 VkDevice device,
						 VkDescriptorSetLayout layout,
						 void* pNext = nullptr);

		VkDescriptorPool getPool(VkDevice device);
		VkDescriptorPool createPool(VkDevice device,
									std::uint32_t setCount,
									std::span<PoolSizeRatio> poolRatios);

		std::vector<PoolSizeRatio> ratios;
		std::vector<VkDescriptorPool> fullPools;
		std::vector<VkDescriptorPool> readyPools;

		static constexpr std::uint32_t MAX_SETS_PER_POOL = 4096;

		std::uint32_t setsPerPool;
	};

	struct SceneData {
    	glm::mat4 view;
    	glm::mat4 proj;
    	glm::mat4 viewproj;
    	glm::mat4 model;
    	glm::vec4 ambientColor;
    	glm::vec4 sunlightDirection;
    	glm::vec4 sunlightColor;
	};

	struct DescriptorWriter {
		std::deque<VkDescriptorImageInfo> imageInfos;
		std::deque<VkDescriptorBufferInfo> bufferInfos;
		std::vector<VkWriteDescriptorSet> writes;

		void writeImage(int binding,
						VkImageView view,
						VkSampler sampler,
						VkImageLayout layout,
						VkDescriptorType type);
		void writeBuffer(int binding,
						 VkBuffer buffer,
						 const std::uint64_t size,
						 const std::uint64_t offset,
						 VkDescriptorType type);

		void clear();
		void updateSet(VkDevice device,
					   VkDescriptorSet set);
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
		PipelineBuilder& enableStencilTest(VkCompareOp compareOp);
		PipelineBuilder& enableBlendingAdditive();
		PipelineBuilder& enableBlendingAlpha();

		PipelineBuilder& addPushConstantRange(VkShaderStageFlags stage,
											  std::uint32_t size,
											  std::uint32_t offset = 0);
		PipelineBuilder& addDescriptorSetLayout(VkDescriptorSetLayout layout);

		ReturnCode build(Pipeline& pipeline,
						 const VkDevice device);
	};

	struct FrameData {
		VkFence inFlightFence;
		VkCommandBuffer commandBuffer;
		VkSemaphore imageAvailableSemaphore;
		VkSemaphore renderFinishedSemaphore;

		DescriptorAllocator descriptors;

		std::vector<std::function<void()>> deletionQueue;
	};

	struct Vertex {
		glm::vec3 position;
		float uvX;
		glm::vec3 normal;
		float uvY;
		glm::vec4 color;
	};

	struct PushConstants {
        glm::mat4 model;
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
		std::vector<std::function<void()>> mainDeletionQueue;

		VmaAllocator allocator;

		VkInstance instance;
		VkDevice device;
		VkPhysicalDevice physicalDevice;
		std::uint32_t transferFamilyIndex;
		std::uint32_t graphicsFamilyIndex;
		std::uint32_t presentFamilyIndex;
		std::uint32_t computeFamilyIndex;

		VkSampler sampler;
		VkCommandPool commandPool;

		VkQueue transferQueue;
		VkQueue graphicsQueue;
		VkQueue presentQueue;
		VkQueue computeQueue;

		VkSurfaceKHR surface;

		FrameData frames[MAX_IN_FLIGHT_FRAMES];
		VkCommandBuffer transferCommandBuffers[4];

		AllocatedImage drawImage;
		AllocatedImage depthImage;

		AllocatedImage errorTexture;

		SceneData sceneData;
		VkDescriptorSetLayout sceneDescriptorLayout;
		VkDescriptorSetLayout samplerDescriptorLayout;
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

	ReturnCode drawScene(FrameData& frame,
					     RendererState& state,
						 VkImage image,
					     const VkExtent2D& extent,
					     const Pipeline& meshPipeline,
					     const Pipeline& outlinePipeline,
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
						   const VkImageUsageFlags usageFlags);

	ReturnCode createImage(AllocatedImage& image,
						   RendererState& state,
						   const void* data,
						   const VkFormat format,
						   const VkExtent3D extent,
						   const VkImageUsageFlags usageFlags);

	void destroyImage(AllocatedImage& image,
					  VkDevice device,
					  VmaAllocator allocator);

	ReturnCode loadGltf(std::vector<MeshAsset>& retval,
						RendererState& state,
						const char* filePath);
}
