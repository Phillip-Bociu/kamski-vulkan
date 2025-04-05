#pragma once
#include "glm/fwd.hpp"
#include "vulkan/vulkan_core.h"
#include <mutex>
#include <type_traits>

#if !defined(KVK_GLFW)
#if defined(_WIN32)

#define VK_USE_PLATFORM_WIN32_KHR

#endif // _WIN32
#endif // KVK_GLFW

#define NOMINMAX

#include <cstdint>
#include <optional>
#include <vector>
#include <span>
#include <deque>
#include <functional>

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include "common.h"

#if !defined(KVK_GLFW)

#if defined(_WIN32)
#include <Windows.h>
#endif

#else

#include <GLFW/glfw3.h>

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

#if !defined(KVK_GLFW)
#if defined(_WIN32)
        HWND window;
#endif
#else
        GLFWwindow* window;
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

    struct DescriptorSetLayoutBuilder {
        VkDescriptorSetLayoutBinding bindings[64];
        std::uint32_t bindingCount;
        
        DescriptorSetLayoutBuilder& addBinding(VkDescriptorType type);
        DescriptorSetLayoutBuilder& addBinding(const VkDescriptorType type, std::uint32_t binding);

        bool build(VkDescriptorSetLayout& layout,
                   VkDevice device,
                   VkShaderStageFlags stage);
    };

    struct PipelineBuilder {
        PipelineBuilder();

        std::vector<VkDynamicState> dynamicState;
        std::vector<VkPipelineShaderStageCreateInfo> shaderStages;
        std::vector<VkPushConstantRange> pushConstantRanges;
        std::vector<VkDescriptorSetLayout> descriptorSetLayouts;
        std::optional<VkPipelineLayout> prebuiltLayout;

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
        PipelineBuilder& setStencilAttachmentFormat(VkFormat format);
        PipelineBuilder& setPrebuiltLayout(VkPipelineLayout layout = VK_NULL_HANDLE);

        PipelineBuilder& enableDepthTest(bool depthWriteEnable, VkCompareOp op);
        PipelineBuilder& enableStencilTest(VkCompareOp compareOp, bool enableWriting);
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
        std::uint32_t swapchainImageIndex;

        VkFence inFlightFence;
        VkCommandBuffer commandBuffer;
        VkSemaphore imageAvailableSemaphore;
        VkSemaphore renderFinishedSemaphore;

        DescriptorAllocator descriptors;

        std::vector<std::function<void()>> deletionQueue;
    };

    struct Mesh {
        AllocatedBuffer indices;
        AllocatedBuffer vertices;
        VkDeviceAddress vertexBufferAddress;
        std::uint32_t indexCount;
    };

    enum MaterialPass {
        MAT_OPAQUE,
        MAT_SHADOW,
        MAT_TRANSPARENT,
        MAT_COUNT
    };

    struct MaterialInstance {
        Pipeline* pipeline;
        VkDescriptorSet materialSet;
        MaterialPass pass;
    };

    struct RenderObject {
        MaterialInstance* materialInstance;
        VkBuffer indexBuffer;
        VkDeviceAddress vertexBufferAddress;

        glm::mat4 transform;
        std::uint32_t indexCount;
        std::uint32_t firstIndex;
    };

    static constexpr std::uint32_t MAX_IN_FLIGHT_FRAMES = 1;
    struct RendererState {
        std::uint32_t currentFrame;

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
        std::mutex transferQueueMutex;

        AllocatedImage drawImage;
        AllocatedImage depthImage;
        AllocatedImage errorTexture;

        //
        // Swapchain stuff
        //
        VkSwapchainKHR swapchain;
        std::uint32_t swapchainImageCount;
        std::vector<VkImage> swapchainImages;
        std::vector<VkImageView> swapchainImageViews;
        VkExtent2D swapchainExtent;
        VkSurfaceFormatKHR swapchainImageFormat;
        VkPresentModeKHR swapchainPresentMode;
    };


    ReturnCode init(RendererState& state, const InitSettings* settings);

    ReturnCode createShaderModuleFromFile(VkShaderModule& shaderModule, RendererState& state, const char* shaderPath);
    ReturnCode createShaderModuleFromMemory(VkShaderModule& shaderModule, RendererState& state, const std::uint32_t* shaderContents, const std::uint64_t shaderSize);

    FrameData* startFrame(RendererState& state, std::uint32_t& frameIndex);

    ReturnCode endFrame(RendererState& state, FrameData& frame);


    ReturnCode createSwapchain(RendererState& state,
                               VkExtent2D extent,
                               VkSurfaceFormatKHR format,
                               VkPresentModeKHR presentMode,
                               std::uint32_t imageCount,
                               VkSwapchainKHR oldSwapchain = VK_NULL_HANDLE);

    ReturnCode recreateSwapchain(RendererState& state,
                                 const std::uint32_t x,
                                 const std::uint32_t y);

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
                           bool isCubemap = false);

    ReturnCode createImage(AllocatedImage& image,
                           RendererState& state,
                           const void* data,
                           const VkFormat format,
                           const VkExtent3D extent,
                           const VkImageUsageFlags usageFlags);

    ReturnCode createCubemap(AllocatedImage& image,
                             RendererState& state,
                             std::span<void*, 6> data,
                             const VkFormat format,
                             const VkExtent2D extent,
                             const VkImageUsageFlags usageFlags);

    void destroyImage(AllocatedImage& image,
                      VkDevice device,
                      VmaAllocator allocator);

    ReturnCode createMesh(kvk::Mesh& mesh,
                          RendererState& state,
                          std::span<std::uint32_t> indices,
                          std::span<std::uint8_t> vertices);
}
