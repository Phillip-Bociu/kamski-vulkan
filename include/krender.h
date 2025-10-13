#pragma once
#include "glm/fwd.hpp"
#include "vulkan/vulkan_core.h"
#include <mutex>

#if !defined(KVK_GLFW)
#if defined(_WIN32)

#define VK_USE_PLATFORM_WIN32_KHR

#endif // _WIN32
#endif // KVK_GLFW

#include <cstdint>
#include <vector>
#include <span>
#include <deque>
#include <functional>

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include "common.h"

#include <spirv_reflect.h>

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
    static constexpr std::uint32_t MAX_IN_FLIGHT_FRAMES = 3;

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
        VkPipeline handle;
        VkPipelineBindPoint bindPoint;

        void bind(VkCommandBuffer cmd);
        template<typename T>
        void pushConstants(VkCommandBuffer cmd, const T& constants, VkShaderStageFlags shaderStage = 0, u32 offset = 0) {
            if(shaderStage == 0) {
                if(bindPoint == VK_PIPELINE_BIND_POINT_COMPUTE) shaderStage = VK_SHADER_STAGE_COMPUTE_BIT;
                else if(bindPoint == VK_PIPELINE_BIND_POINT_GRAPHICS) shaderStage = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
            }
            vkCmdPushConstants(cmd,
                               layout,
                               shaderStage,
                               offset,
                               sizeof(T),
                               &constants);
        }
    };

    struct AllocatedImage {
        VkImage image = VK_NULL_HANDLE;
        VkImageView view;
        VmaAllocation allocation;
        VkExtent3D extent;
        VkFormat format;
        VkImageUsageFlags usage;
        u8 mipCount;
        u8 layerCount;
    };

    struct AllocatedBuffer {
        VkBuffer buffer = VK_NULL_HANDLE;
        VmaAllocation allocation;
        VkDeviceAddress address;
        VkBufferUsageFlags usage;
        VkDeviceSize size;
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


    struct Descriptor {
        union {
            struct {
                VkImageView image;
                VkSampler sampler;
            } imageSampler;
            struct {
                VkImageView image;
                VkDescriptorType imageType;
            };
            struct {
                VkBuffer buffer;
                VkDescriptorType bufferType;
            };
            VkSampler sampler;
            u32 lastUploadedImageIndex;
        };

        enum {
            IMAGE_SAMPLER,
            IMAGE,
            BUFFER,
            SAMPLER,
            IMAGES,
        } type;
    };

    struct DescriptorSet {
        VkDescriptorSet handle = VK_NULL_HANDLE;
        Descriptor descriptors[64];
        VkShaderStageFlags shaderStage = 0;
        std::uint32_t count = 0;

        bool operator==(const kvk::DescriptorSet& other) const noexcept {
            if(this->shaderStage != other.shaderStage) return false;
            if(this->count != other.count) return false;
            for(int i = 0; i != this->count; i++) {
                if(this->descriptors[i].type != other.descriptors[i].type) return false;
                switch(this->descriptors[i].type) {
                    case kvk::Descriptor::IMAGE: {
                        if(this->descriptors[i].imageType != other.descriptors[i].imageType) return false;
                    } break;

                    case kvk::Descriptor::BUFFER: {
                        if(this->descriptors[i].bufferType != other.descriptors[i].bufferType) return false;
                    } break;

                    default: {
                    } break;
                }
            }
            return true;
        }

    };


    struct PipelineLayoutInfo {
        vector<VkPushConstantRange> pushConstantRanges;
        vector<VkDescriptorSetLayout> layouts;

        bool operator==(const PipelineLayoutInfo& other) const noexcept {
            if(this->pushConstantRanges.size() != other.pushConstantRanges.size()) return false;
            if(this->layouts.size() != other.layouts.size()) return false;

            for(int i = 0; i != this->pushConstantRanges.size(); i++) {
                if(memcmp(&this->pushConstantRanges[i], &other.pushConstantRanges[i], sizeof(VkPushConstantRange)) != 0) return false;
            }

            for(int i = 0; i != this->layouts.size(); i++) {
                if(this->layouts[i] != other.layouts[i]) return false;
            }

            return true;
        }

    };

    inline bool operator==(const VkPipelineLayoutCreateInfo& a, const VkPipelineLayoutCreateInfo& b) {
        if(a.setLayoutCount != b.setLayoutCount) return false;
        if(a.pushConstantRangeCount != b.pushConstantRangeCount) return false;

        for(int i = 0; i != a.setLayoutCount; i++) {
            if(a.pSetLayouts[i] != b.pSetLayouts[i]) return false;
        }

        for(int i = 0; i != a.pushConstantRangeCount; i++) {
            if(memcmp(&a.pPushConstantRanges[i], &b.pPushConstantRanges[i], sizeof(VkPushConstantRange)) != 0) return false;
        }

        return true;
    }

    struct DescriptorSetLayoutHash {
        size_t operator()(const kvk::DescriptorSet& s) const noexcept {
            size_t retval = std::hash<std::uint32_t>()(s.shaderStage);
            for(std::uint32_t i = 0; i != s.count; i++) {
                switch(s.descriptors[i].type) {
                    case kvk::Descriptor::IMAGE_SAMPLER: {
                        retval = (retval << 1) ^ std::hash<std::uint32_t>()(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
                    } break;

                    case kvk::Descriptor::IMAGE: {
                        retval = (retval << 1) ^ std::hash<std::uint32_t>()(s.descriptors[i].imageType);
                    } break;

                    case kvk::Descriptor::SAMPLER: {
                        retval = (retval << 1) ^ std::hash<std::uint32_t>()(VK_DESCRIPTOR_TYPE_SAMPLER);
                    } break;

                    case kvk::Descriptor::BUFFER: {
                        retval = (retval << 1) ^ std::hash<std::uint32_t>()(s.descriptors[i].bufferType);
                    } break;

                    case kvk::Descriptor::IMAGES: {
                        retval = (retval << 1) ^ std::hash<std::uint32_t>()(VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE);
                        retval = (retval << 1) ^ std::hash<std::uint32_t>()(std::numeric_limits<u16>::max());
                    } break;
                }
            }
            return retval;
        }
    };

    struct PipelineLayoutHash {
        size_t operator()(const kvk::PipelineLayoutInfo& s) const noexcept {
            size_t retval = 0;
            for(int i = 0; i != s.layouts.size(); i++) {
                retval = (retval << 1) ^ std::hash<void*>()(s.layouts[i]);
            }

            for(int i = 0; i != s.pushConstantRanges.size(); i++) {
                retval = (retval << 1) ^ std::hash<std::uint32_t>()(s.pushConstantRanges[i].size);
                retval = (retval << 1) ^ std::hash<std::uint32_t>()(s.pushConstantRanges[i].offset);
                retval = (retval << 1) ^ std::hash<std::uint32_t>()(s.pushConstantRanges[i].stageFlags);
            }
            return retval;
        }
    };

    struct ShaderModule {
        VkShaderModule module;
        SpvReflectShaderModule reflection;
    };

    struct Cache {
        struct RendererState* state;

        std::mutex pipelineMutex;
        unordered_map<std::string, Pipeline> pipelines; 

        std::mutex descriptorMutex;
        unordered_map<std::string, DescriptorSet> descriptors; 

        std::mutex perFrameDescriptorMutex;
        unordered_map<std::string, DescriptorSet[MAX_IN_FLIGHT_FRAMES]> perFrameDescriptors; 

        std::mutex descriptorLayoutMutex;
        unordered_map<DescriptorSet, VkDescriptorSetLayout, DescriptorSetLayoutHash> descriptorLayouts;

        std::mutex pipelineLayoutMutex;
        unordered_map<kvk::PipelineLayoutInfo, VkPipelineLayout, PipelineLayoutHash> pipelineLayouts;

        std::mutex shaderModuleMutex;
        unordered_map<std::string, ShaderModule> shaderModules;
    };

    struct DescriptorSetBuilder {
        Cache& cache;
        Descriptor descriptors[64];
        DescriptorWriter writer;
        std::uint32_t count = 0;

        DescriptorSetBuilder(Cache& cache);

        DescriptorSetBuilder& image(VkImageView imageView, VkSampler sampler, VkImageLayout layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL); // assumed, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER
        DescriptorSetBuilder& image(VkImageView imageView, VkDescriptorType type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VkImageLayout layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        DescriptorSetBuilder& images(std::span<AllocatedImage> images, u32 offset, VkImageLayout layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL); // assumed, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE

        DescriptorSetBuilder& buffer(VkBuffer buffer, VkDescriptorType type, u64 size = VK_WHOLE_SIZE, u64 offset = 0);
        DescriptorSetBuilder& sampler(VkSampler sampler);
        
        DescriptorSet&         build(std::string_view name, VkShaderStageFlags shaderStage);
        DescriptorSet& buildPerFrame(std::string_view name, VkShaderStageFlags shaderStage);

        void buildInternal(DescriptorSet& set);
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
        RenderPassBuilder& addColorAttachment(VkImageView view,
                                              VkAttachmentLoadOp loadOp,
                                              glm::uvec4 clearValues,
                                              VkAttachmentStoreOp storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                                              VkImageLayout imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

        RenderPassBuilder& setDepthAttachment(VkImageView view,
                                              bool combinedDepthStencil = false,
                                              VkAttachmentLoadOp loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                                              float depthClear = 1.0f,
                                              std::uint32_t stencil = 0,
                                              VkAttachmentStoreOp storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                                              VkImageLayout imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);

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

    struct PipelineBuilder {
        PipelineBuilder();

        std::vector<VkDynamicState> dynamicState;
        std::vector<VkVertexInputAttributeDescription> vertexInputAttributes;
        std::uint32_t vertexInputAttributesSize;

        enum ShaderStage {
            SHADER_STAGE_VERTEX,
            SHADER_STAGE_FRAGMENT,
            SHADER_STAGE_COMPUTE,

            SHADER_STAGE_COUNT
        };

        std::string_view shaderNames[SHADER_STAGE_COUNT];
        std::vector<VkSpecializationMapEntry> specializationConstants[SHADER_STAGE_COUNT];
        std::vector<std::uint8_t> specializationConstantData[SHADER_STAGE_COUNT];

        std::vector<VkFormat> colorAttachmentFormats;
        VkPipeline basePipeline;
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

        PipelineBuilder& addShaders(std::string_view name, VkShaderStageFlags stageFlags);
        PipelineBuilder& clearShaders(VkShaderStageFlags stageFlags = VK_SHADER_STAGE_ALL);
        PipelineBuilder& setInputTopology(VkPrimitiveTopology topology);
        PipelineBuilder& setPolygonMode(VkPolygonMode poly);
        PipelineBuilder& setCullMode(VkCullModeFlags cullMode, VkFrontFace face);
        PipelineBuilder& addColorAttachmentFormat(VkFormat format, std::uint32_t count = 1);
        PipelineBuilder& setDepthAttachmentFormat(VkFormat format);
        PipelineBuilder& setStencilAttachmentFormat(VkFormat format);
        PipelineBuilder& setBasePipeline(VkPipeline pipeline);
        PipelineBuilder& setAllowDerivatives(bool allow);

        PipelineBuilder& enableDepthTest(bool depthWriteEnable, VkCompareOp op);
        PipelineBuilder& enableStencilTest(VkCompareOp compareOp, bool enableWriting);
        PipelineBuilder& enableBlendingAdditive();
        PipelineBuilder& enableBlendingAlpha();

        PipelineBuilder& disableBlending();

        PipelineBuilder& addVertexInputAttribute(VkFormat format,
                                                 std::uint32_t offset,
                                                 std::uint32_t size);

        PipelineBuilder& clearSpecializationConstants(const ShaderStage shaderStage = SHADER_STAGE_COUNT);
        PipelineBuilder& addSpecializationConstantData(const void* data, const std::uint64_t size, std::uint32_t constantId, const ShaderStage shaderStage);
        template<typename T>
        PipelineBuilder& addSpecializationConstant(const T& constant, const std::uint32_t constantId, const ShaderStage shaderStage) {
            addSpecializationConstantData(&constant, sizeof(T), constantId, shaderStage);
            return *this;
        }

        PipelineBuilder& addSpecializationConstantData(const void* data, const std::uint64_t size, const ShaderStage shaderStage);
        template<typename T>
        PipelineBuilder& addSpecializationConstant(const T& constant, const ShaderStage shaderStage) {
            addSpecializationConstantData(&constant, sizeof(T), shaderStage);
            return *this;
        }

        ReturnCode build(Pipeline& pipeline,
                         Cache& cache,
                         VkDevice device);
        ReturnCode buildCompute(Pipeline& pipeline,
                                Cache& cache,
                                VkDevice device);
    };

    struct Mesh {
        AllocatedBuffer indices;
        AllocatedBuffer vertices;
        VkDeviceAddress vertexBufferAddress;
        std::uint32_t indexCount;
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

        std::vector<std::function<void()>> deletionQueue;
    };


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
        VkPhysicalDeviceLimits limits;

        Queue* queues;
        std::uint32_t queueCount;

        VkSurfaceKHR surface;

        DescriptorAllocator descriptors;
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
        std::vector<VkSemaphore> renderFinishedSemaphores;
        VkExtent2D swapchainExtent;
        VkSurfaceFormatKHR swapchainImageFormat;
        VkPresentModeKHR swapchainPresentMode;
    };


    ReturnCode init(RendererState& state, const InitSettings* settings);

    ReturnCode createShaderModuleFromFile(VkShaderModule& shaderModule, VkDevice device, const char* shaderPath);
    ReturnCode createShaderModuleFromMemory(VkShaderModule& shaderModule, VkDevice device, const std::uint32_t* shaderContents, const std::uint64_t shaderSize);

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

    template<typename ... Sets>
        void bindDescriptorSets(VkCommandBuffer commandBuffer,
                                kvk::Pipeline& pipeline,
                                VkDescriptorSet* setArray,
                                const u32 setCount,
                                const DescriptorSet& set,
                                Sets&& ... sets) {
        if constexpr(sizeof...(sets) == 0) {
            setArray[setCount] = set.handle;
            vkCmdBindDescriptorSets(commandBuffer,
                                    pipeline.bindPoint,
                                    pipeline.layout,
                                    0,
                                    setCount + 1,
                                    setArray,
                                    0,
                                    nullptr);
        } else {
            setArray[setCount] = set.handle;
            bindDescriptorSets(commandBuffer, pipeline, setArray, setCount + 1, std::forward<Sets>(sets)...);
        }
    }

    template<typename ... Sets>
    void bindDescriptorSets(VkCommandBuffer commandBuffer, kvk::Pipeline& pipeline, Sets&& ... sets) {
        VkDescriptorSet setArray[sizeof...(sets)];
        bindDescriptorSets(commandBuffer,
                           pipeline,
                           setArray,
                           0,
                           std::forward<Sets>(sets)...);
    }

}
