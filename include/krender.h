#pragma once
#include "glm/fwd.hpp"
#include "vulkan/vulkan_core.h"
#include <mutex>

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
        VkDeviceAddress address;
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
        int bindingCount;

        DescriptorWriter();

        void writeImage(int binding,
                        VkImageView view,
                        VkSampler sampler,
                        VkImageLayout layout,
                        VkDescriptorType type,
                        std::uint32_t arrayOffset = 0);
        void writeImage(VkImageView view,
                        VkSampler sampler,
                        VkImageLayout layout,
                        VkDescriptorType type,
                        std::uint32_t arrayOffset = 0);
        void writeImages(std::span<VkDescriptorImageInfo> imageInfos,
                         VkDescriptorType type,
                         std::uint32_t arrayOffset = 0);
        void writeImages(int binding,
                         std::span<VkDescriptorImageInfo> imageInfos,
                         VkDescriptorType type,
                         std::uint32_t arrayOffset = 0);

        void writeBuffer(int binding,
                         VkBuffer buffer,
                         const std::uint64_t size,
                         const std::uint64_t offset,
                         VkDescriptorType type,
                         std::uint32_t arrayOffset = 0);
        void writeBuffer(VkBuffer buffer,
                         const std::uint64_t size,
                         const std::uint64_t offset,
                         VkDescriptorType type,
                         std::uint32_t arrayOffset = 0);
        void writeBuffers(std::span<VkDescriptorBufferInfo> bufferInfos,
                          VkDescriptorType type,
                          std::uint32_t arrayOffset = 0);
        void writeBuffers(int binding,
                          std::span<VkDescriptorBufferInfo> bufferInfos,
                          VkDescriptorType type,
                          std::uint32_t arrayOffset = 0);

        void clear();
        void updateSet(VkDevice device,
                       VkDescriptorSet set);
    };
    
    struct RenderPass {
        VkCommandBuffer cmd;
        ~RenderPass();
    };

    struct RenderPassBuilder {
        private:
        std::vector<VkRenderingAttachmentInfo> colorAttachments;
        VkRenderingAttachmentInfo              depthAttachment;
        VkRenderingAttachmentInfo              stencilAttachment;
        bool                                   combinedDepthStencil : 1;
        bool                                   hasDepth             : 1;
        bool                                   hasStencil           : 1;

        public:
        RenderPassBuilder();
        RenderPassBuilder& addColorAttachment(VkImageView view,
                                              VkAttachmentLoadOp loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                                              glm::vec4 clearColor = {0.0f, 0.0f, 0.0f, 1.0f},
                                              VkAttachmentStoreOp storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                                              VkImageLayout imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

        RenderPassBuilder& setDepthAttachment(VkImageView view,
                                              bool combinedDepthStencil = false,
                                              VkAttachmentLoadOp loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                                              float depthClear = 1.0f,
                                              std::uint32_t stencil = 0,
                                              VkAttachmentStoreOp storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                                              VkImageLayout imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);

        RenderPassBuilder& setStencilAttachment(VkImageView view,
                                                VkAttachmentLoadOp loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                                                std::uint32_t stencil = 0,
                                                VkAttachmentStoreOp storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                                                VkImageLayout imageLayout = VK_IMAGE_LAYOUT_STENCIL_ATTACHMENT_OPTIMAL);

        [[nodiscard]] RenderPass cmdBeginRendering(VkCommandBuffer cmd,
                                                   VkExtent2D extent,
                                                   VkOffset2D offset = {0, 0},
                                                   std::uint32_t layerCount = 1);
    };

    struct DescriptorSetLayoutBuilder {
        DescriptorSetLayoutBuilder();
        VkDescriptorBindingFlags flagArray[64];
        VkDescriptorSetLayoutBinding bindings[64];
        std::uint32_t bindingCount;
        
        DescriptorSetLayoutBuilder& addBinding(VkDescriptorType type, std::uint32_t descriptorCount = 1, VkDescriptorBindingFlags flags = 0);

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

        enum ShaderStage {
            SHADER_STAGE_VERTEX,
            SHADER_STAGE_FRAGMENT,

            SHADER_STAGE_COUNT
        };

        std::vector<VkSpecializationMapEntry> specializationConstants[SHADER_STAGE_COUNT];
        std::vector<std::uint8_t> specializationConstantData[SHADER_STAGE_COUNT];

        std::vector<VkFormat> colorAttachmentFormats;
        VkPipeline basePipeline;
        VkPipelineCache cache;
        bool allowDerivatives;

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

        PipelineBuilder& setShader(VkShaderModule computeShader);
        PipelineBuilder& setShaders(VkShaderModule vertexShader, VkShaderModule fragmentShader);
        PipelineBuilder& setInputTopology(VkPrimitiveTopology topology);
        PipelineBuilder& setPolygonMode(VkPolygonMode poly);
        PipelineBuilder& setCullMode(VkCullModeFlags cullMode, VkFrontFace face);
        PipelineBuilder& addColorAttachmentFormat(VkFormat format, std::uint32_t count = 1);
        PipelineBuilder& setDepthAttachmentFormat(VkFormat format);
        PipelineBuilder& setStencilAttachmentFormat(VkFormat format);
        PipelineBuilder& setPrebuiltLayout(VkPipelineLayout layout = VK_NULL_HANDLE);
        PipelineBuilder& setBasePipeline(VkPipeline pipeline);
        PipelineBuilder& setAllowDerivatives(bool allow);
        PipelineBuilder& setPipelineCache(VkPipelineCache cache);

        PipelineBuilder& enableDepthTest(bool depthWriteEnable, VkCompareOp op);
        PipelineBuilder& enableStencilTest(VkCompareOp compareOp, bool enableWriting);
        PipelineBuilder& enableBlendingAdditive();
        PipelineBuilder& enableBlendingAlpha();

        PipelineBuilder& disableBlending();

        PipelineBuilder& addPushConstantRange(VkShaderStageFlags stage,
                                              std::uint32_t size,
                                              std::uint32_t offset = 0);
        PipelineBuilder& addDescriptorSetLayout(VkDescriptorSetLayout layout);

        PipelineBuilder& addSpecializationConstantData(const void* data, const std::uint64_t size, std::uint32_t constantId, const ShaderStage shaderStage);
        template<typename T>
        PipelineBuilder& addSpecializationConstant(const T& constant, const std::uint32_t constantId, const ShaderStage shaderStage) {
            addSpecializationConstantData(&constant, sizeof(T), constantId, shaderStage);
        }

        PipelineBuilder& addSpecializationConstantData(const void* data, const std::uint64_t size, const ShaderStage shaderStage);
        template<typename T>
        PipelineBuilder& addSpecializationConstant(const T& constant, const ShaderStage shaderStage) {
            addSpecializationConstantData(&constant, sizeof(T), shaderStage);
        }

        ReturnCode build(Pipeline& pipeline,
                         const VkDevice device);
        ReturnCode buildCompute(Pipeline& pipeline,
                                const VkDevice device);
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

    union CubemapContents {
        struct {
            const void* left;
            const void* right;
            const void* top;
            const void* bottom;
            const void* back;
            const void* front;
        };
        const char* imageContents[6];
    };

    struct RenderObject {
        MaterialInstance* materialInstance;
        VkBuffer indexBuffer;
        VkDeviceAddress vertexBufferAddress;

        glm::mat4 transform;
        std::uint32_t indexCount;
        std::uint32_t firstIndex;
    };

    struct Queue {
        VkQueue handle;
        VkQueue secondaryHandle;
        std::mutex submitMutex;
        std::mutex poolMutex;
        std::condition_variable poolCvar;
        std::vector<bool> isSlotOccupied;
        std::vector<VkCommandPool> pools;
        std::vector<VkCommandBuffer> commandBuffers;
        std::vector<VkFence> fences;

        std::uint32_t familyIndex;
        std::uint32_t freePoolCount;
        VkQueueFlags flags;
    };

    struct PoolInfo {
        Queue* queue;
        std::uint32_t poolIndex;
    };

    struct FrameData {
        std::uint32_t swapchainImageIndex;

        Queue* queue;
        VkFence inFlightFence;
        VkCommandBuffer commandBuffer;

        VkSemaphore imageAvailableSemaphore;
        VkSemaphore renderFinishedSemaphore;

        DescriptorAllocator descriptors;

        std::vector<std::function<void()>> deletionQueue;
    };


    static constexpr std::uint32_t MAX_IN_FLIGHT_FRAMES = 3;
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

        Queue* queues;
        std::uint32_t queueCount;

        VkSurfaceKHR surface;

        FrameData frames[MAX_IN_FLIGHT_FRAMES];

        AllocatedImage drawImage;
        AllocatedImage depthImage;

        DescriptorAllocator gpDescriptorAllocator;

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
                            VkDevice device,
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
                           bool isCubemap = false,
                           std::uint32_t mipLevels = 1);

    ReturnCode createImage(AllocatedImage& image,
                           RendererState& state,
                           const void* data,
                           const VkFormat format,
                           const VkExtent3D extent,
                           const VkImageUsageFlags usageFlags,
                           std::uint32_t mipLevels = 1);

    ReturnCode createCubemap(AllocatedImage& image,
                             RendererState& state,
                             const CubemapContents& data,
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

    ReturnCode createQueue(Queue& queue,
                           RendererState& state,
                           VkQueueFlags flags,
                           std::uint32_t queueFamilyIndex,
                           bool hasSecondaryQueue = false);

    PoolInfo lockCommandPool(RendererState& state, VkQueueFlags desiredQueueFlags = VK_QUEUE_GRAPHICS_BIT);
    void unlockCommandPool(RendererState& state, PoolInfo& poolInfo);
}
