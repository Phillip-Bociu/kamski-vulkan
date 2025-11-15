#include "spirv_reflect.h"
#include "vulkan/vulkan_core.h"
#include <cstdint>
#include <limits>
#include <mutex>
#include <thread>

#define VMA_IMPLEMENTATION
#define GLM_ENABLE_EXPERIMENTAL
#include "common.h"
#include "krender.h"
#include "utils.h"

#include <glm/gtx/transform.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <fstream>
#include <vector>
#include <set>
#include <algorithm>


#if defined(_WIN32)
#include "krender_win32.h"
#endif

#if defined(KVK_GLFW)
#include <GLFW/glfw3.h>
#endif

namespace kvk {
    static VkDescriptorSetLayout descriptorSetLayoutFromCache(Cache& cache,
                                                              const DescriptorSet& set,
                                                              const VkDevice device,
                                                              bool isPushDescriptor,
                                                              std::string_view name);

    static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
        VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
        VkDebugUtilsMessageTypeFlagsEXT messageType,
        const VkDebugUtilsMessengerCallbackDataEXT *pCallbackData,
        void *pUserData) {
        logError("Validation layer: %s", pCallbackData->pMessage);
        return VK_FALSE;
    }

    static VkResult CreateDebugUtilsMessengerEXT(
        VkInstance instance,
        const VkDebugUtilsMessengerCreateInfoEXT *pCreateInfo,
        const VkAllocationCallbacks *pAllocator,
        VkDebugUtilsMessengerEXT *pDebugMessenger) {
        auto func = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
            instance,
            "vkCreateDebugUtilsMessengerEXT");
        if (func != nullptr) {
            return func(instance, pCreateInfo, pAllocator, pDebugMessenger);
        } else {
            return VK_ERROR_EXTENSION_NOT_PRESENT;
        }
    }

    static void DestroyDebugUtilsMessengerEXT(
        VkInstance instance,
        VkDebugUtilsMessengerEXT debugMessenger,
        const VkAllocationCallbacks *pAllocator) {
        auto func = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
            instance,
            "vkDestroyDebugUtilsMessengerEXT");
        if (func != nullptr) {
            func(instance, debugMessenger, pAllocator);
        }
    }

    VkResult vkSetDebugUtilsObjectName(VkDevice device, const VkDebugUtilsObjectNameInfoEXT* nameInfo) {
        auto func = (PFN_vkSetDebugUtilsObjectNameEXT)vkGetDeviceProcAddr(device, "vkSetDebugUtilsObjectNameEXT");
        if(func != nullptr) {
            return func(device, nameInfo);
        }
        return VK_ERROR_EXTENSION_NOT_PRESENT;
    }

    ReturnCode createShaderModuleFromMemory(VkShaderModule& shaderModule,
                                            VkDevice device,
                                            const std::uint32_t* shaderContents,
                                            const std::uint64_t shaderSize) {
        KAMSKI_PROFILE();
        VkShaderModuleCreateInfo createInfo = {
            .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
            .codeSize = shaderSize,
            .pCode = shaderContents,
        };

        if(vkCreateShaderModule(device,
                                &createInfo,
                                nullptr,
                                &shaderModule) != VK_SUCCESS) {
            logError("Could not create shader module");
            return ReturnCode::UNKNOWN;
        }
        return ReturnCode::OK;
    }

    ReturnCode createShaderModuleFromFile(VkShaderModule& shaderModule,
                                          VkDevice device,
                                          const char* shaderPath) {
        KAMSKI_PROFILE();
        std::ifstream vs(shaderPath, std::ios::ate | std::ios::binary);
        if(!vs.is_open()) {
            logError("File %s not found", shaderPath);
            return ReturnCode::FILE_NOT_FOUND;
        }

        const std::uint64_t size = vs.tellg();
        std::vector<std::uint32_t> vsData(size / 4);
        vs.seekg(0);
        vs.read((char*)vsData.data(), size);

        ReturnCode rc = createShaderModuleFromMemory(shaderModule,
                                                     device,
                                                     vsData.data(),
                                                     size);
        return rc;
    }

    ReturnCode init(RendererState& state, const InitSettings* settings) {
        KAMSKI_PROFILE();
        /*===========================
                User settings
          ===========================*/
        if(!settings) return ReturnCode::WRONG_PARAMETERS;

        VkApplicationInfo appInfo = {
            .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
            .pApplicationName = settings->appName,
            .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
            .pEngineName = "Kamski",
            .engineVersion = VK_MAKE_VERSION(1, 0, 0),
            .apiVersion = VK_API_VERSION_1_4
        };

        state.currentFrame = 0;

        /*=====================================
                Validation layer handling
          =====================================*/
#ifdef KAMSKI_DEBUG
        logInfo("Adding validation layers");
        const char* desiredLayers[] = {
            "VK_LAYER_KHRONOS_validation",
            "VK_LAYER_KHRONOS_synchronization2",
        };

        std::uint32_t layerCount = 0;
        vkEnumerateInstanceLayerProperties(&layerCount, nullptr);

        std::vector<VkLayerProperties> layerProps(layerCount);
        vkEnumerateInstanceLayerProperties(&layerCount, layerProps.data());

        for(const char* d : desiredLayers) {
            bool found = false;
            for(const VkLayerProperties& prop : layerProps) {
                if(strcmp(prop.layerName, d) == 0) {
                    logDebug("found %s", d);
                    found = true;
                    break;
                }
            }

            if(!found) {
                return ReturnCode::LAYER_NOT_FOUND;
            }
        }
#else 
        printf("No validation layers\n");
#endif
        /*=====================================
                    Instance creation
          =====================================*/
        const char* desiredExtensions[] = {
            "VK_KHR_surface",
#ifndef KVK_GLFW
#ifdef _WIN32
            "VK_KHR_win32_surface",
#endif // _WIN32
#endif // KVK_GLKFW

#ifdef KAMSKI_DEBUG
            VK_EXT_DEBUG_UTILS_EXTENSION_NAME
#endif
        };

        std::vector<const char*> extensions(desiredExtensions, desiredExtensions + sizeof(desiredExtensions) / sizeof(desiredExtensions[0]));
#ifdef KVK_GLFW
        std::uint32_t glfwExtensionCount;
        const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);
        extensions.insert(extensions.end(), glfwExtensions, glfwExtensions + glfwExtensionCount);
#endif

        VkInstanceCreateInfo instanceCreateInfo = {
            .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
            .pApplicationInfo = &appInfo,
#if defined(KAMSKI_DEBUG)
            .enabledLayerCount = sizeof(desiredLayers) / sizeof(desiredLayers[0]),
            .ppEnabledLayerNames = desiredLayers,
#endif
            .enabledExtensionCount = std::uint32_t(extensions.size()),
            .ppEnabledExtensionNames = extensions.data(),
        };
        VkResult result = vkCreateInstance(&instanceCreateInfo,
                                           nullptr,
                                           &state.instance);
        if(result != VK_SUCCESS) {
            logError("Could not initialize vk instance: %d", result);
            return ReturnCode::UNKNOWN;
        }
        logDebug("Instance created");

#ifdef KAMSKI_DEBUG
        VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo = {
            .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
            .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
            .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
            .pfnUserCallback = debugCallback,
            .pUserData = nullptr,
        };

        VkDebugUtilsMessengerEXT debugMessenger;
        if(CreateDebugUtilsMessengerEXT(state.instance, &debugCreateInfo, nullptr, &debugMessenger) != VK_SUCCESS) {
            logError("Could not create debug messenger");
            return ReturnCode::UNKNOWN;
        }
#endif

        /*=====================================
                    Surface creation
          =====================================*/

        ReturnCode rc;
#if !defined(KVK_GLFW)
#if defined(_WIN32)
        rc = createWin32Surface(state, settings->window);
#endif // _WIN32
        if(rc != ReturnCode::OK) {
            return rc;
        }
#else // KVK_GLFW
        VK_CHECK(glfwCreateWindowSurface(state.instance,
                                         settings->window,
                                         nullptr,
                                         &state.surface));
#endif // KVK_GLFW
        logDebug("Surface created");

        /*=====================================
                Physical device selection
          =====================================*/
        const char* desiredDeviceExtensions[] = {
            VK_KHR_SWAPCHAIN_EXTENSION_NAME
        };

        state.physicalDevice = VK_NULL_HANDLE;
        std::vector<VkPresentModeKHR> surfacePresentModes;
        std::vector<VkSurfaceFormatKHR> surfaceFormats;
        VkSurfaceCapabilitiesKHR surfaceCapabilities;
        std::vector<VkQueueFamilyProperties> queueFamilies;


        std::uint32_t deviceCount = 0;
        vkEnumeratePhysicalDevices(state.instance, &deviceCount, nullptr);

        std::vector<VkPhysicalDevice> physicalDevices(deviceCount);
        vkEnumeratePhysicalDevices(state.instance, &deviceCount, physicalDevices.data());

        for(const VkPhysicalDevice pd : physicalDevices) {
            VkPhysicalDeviceProperties prop;
            vkGetPhysicalDeviceProperties(pd, &prop);
            logDebug("GPU: %s", prop.deviceName);

            if(prop.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
                bool graphicsFamilyFound = false;
                bool presentFamilyFound = false;
                bool computeFamilyFound = false;
                bool transferFamilyFound = false;
                bool dedicatedTransfer = false;

                std::uint32_t extensionCount = 0;
                vkEnumerateDeviceExtensionProperties(pd, nullptr, &extensionCount, nullptr);

                std::vector<VkExtensionProperties> deviceExtensions(extensionCount);
                vkEnumerateDeviceExtensionProperties(pd, nullptr, &extensionCount, deviceExtensions.data());

                extensionCount = 0;
                for(const char* de : desiredDeviceExtensions) {
                    for(const VkExtensionProperties& ext : deviceExtensions) {
                        if(strcmp(ext.extensionName, de) == 0) {
                            logInfo("Found device extension: %s", de);
                            extensionCount++;
                            break;
                        }
                    }
                }
                if(extensionCount != sizeof(desiredDeviceExtensions) / sizeof(desiredDeviceExtensions[0])) {
                    continue;
                }

                std::uint32_t queueFamilyCount = 0;
                vkGetPhysicalDeviceQueueFamilyProperties(pd, &queueFamilyCount, nullptr);
                queueFamilies.resize(queueFamilyCount);
                vkGetPhysicalDeviceQueueFamilyProperties(pd, &queueFamilyCount, queueFamilies.data());

                std::uint32_t i = 0;
                for(const VkQueueFamilyProperties& qf : queueFamilies) {
                    logInfo("Qfam[%u] queue flags: %u", i, qf.queueFlags);
                    if(qf.queueFlags & VK_QUEUE_TRANSFER_BIT) {
                        logInfo("Qfam[%u] supports TRANSFER", i);
                        if(!transferFamilyFound || !dedicatedTransfer) {
                            if(qf.queueFlags == VK_QUEUE_TRANSFER_BIT || qf.queueFlags == (VK_QUEUE_TRANSFER_BIT | VK_QUEUE_SPARSE_BINDING_BIT)) {
                                logInfo("Qfam[%u] DEDICATED TRANSFER", i);
                                dedicatedTransfer = true;
                            }
                            state.transferFamilyIndex = i;
                            transferFamilyFound = true;
                        } 
                    }

                    if(qf.queueFlags & VK_QUEUE_GRAPHICS_BIT) {
                        logInfo("Qfam[%u] supports GRAPHICS", i);
                        if(!graphicsFamilyFound) {
                            state.graphicsFamilyIndex = i;
                            graphicsFamilyFound = true;
                        }
                    }

                    if(!presentFamilyFound) {
                        VkBool32 presentSupport = false;
                        vkGetPhysicalDeviceSurfaceSupportKHR(pd, i, state.surface, &presentSupport);

                        if(presentSupport) {
                            state.presentFamilyIndex = i;
                            presentFamilyFound = true;
                        }
                    }

                    if(qf.queueFlags & VK_QUEUE_COMPUTE_BIT) {
                        logInfo("Qfam[%u] supports COMPUTE", i);
                        if(!computeFamilyFound) {
                            state.computeFamilyIndex = i;
                            computeFamilyFound = true;
                        }
                    }

                    i++;
                }
                if(!transferFamilyFound && !computeFamilyFound && !graphicsFamilyFound && !presentFamilyFound ) {
                    continue;
                }

                std::uint32_t formatCount = 0;
                vkGetPhysicalDeviceSurfaceFormatsKHR(pd, state.surface, &formatCount, nullptr);
                std::uint32_t presentModeCount = 0;
                vkGetPhysicalDeviceSurfacePresentModesKHR(pd, state.surface, &presentModeCount, nullptr);

                if(formatCount == 0 || presentModeCount == 0) {
                    continue;
                }

                surfaceFormats.resize(formatCount);
                vkGetPhysicalDeviceSurfaceFormatsKHR(pd, state.surface, &formatCount, surfaceFormats.data());
                surfacePresentModes.resize(presentModeCount);
                vkGetPhysicalDeviceSurfacePresentModesKHR(pd, state.surface, &presentModeCount, surfacePresentModes.data());

                vkGetPhysicalDeviceSurfaceCapabilitiesKHR(pd,
                                                          state.surface,
                                                          &surfaceCapabilities);

                if(!dedicatedTransfer) {
                    logWarning("No dedidcated transfer queue family present");
                }
                state.physicalDevice = pd;
                break;
            }
        }

        if(state.physicalDevice == VK_NULL_HANDLE) {
            logError("No supported GPUs found");
            return ReturnCode::DEVICE_NOT_FOUND;
        }

        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(state.physicalDevice, &props);
        state.limits = props.limits;

        /*=====================================
                Logical device creation
          =====================================*/
        state.device = VK_NULL_HANDLE;

            logInfo("GraphicsFamilyIndex: %u", state.graphicsFamilyIndex);
            logInfo("PresentFamilyIndex: %u", state.presentFamilyIndex);
            logInfo("ComputeFamilyIndex: %u", state.computeFamilyIndex);
            logInfo("TransferFamilyIndex: %u", state.transferFamilyIndex);
        const std::set<std::uint32_t> uniqueQueueFamilies = {
            state.graphicsFamilyIndex,
            state.presentFamilyIndex,
            state.computeFamilyIndex,
            state.transferFamilyIndex
        };
        std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
        queueCreateInfos.reserve(uniqueQueueFamilies.size());

        for(std::uint32_t qFam : uniqueQueueFamilies) {
            float queuePriorities[2] = {
                1.0f,
                0.0f
            };
            
            VkDeviceQueueCreateInfo queueCreateInfo = {
                .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
                .queueFamilyIndex = qFam,
                .queueCount = qFam == 0 ? 2u : 1u,
                .pQueuePriorities = queuePriorities,
            };
            queueCreateInfos.push_back(queueCreateInfo);
        }


        VkPhysicalDeviceVulkan14Features features14 = {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES,
            .pushDescriptor = VK_TRUE,
        };
        VkPhysicalDeviceVulkan11Features features11 = {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES,
            .pNext = &features14,
            .storageBuffer16BitAccess = VK_TRUE,
            .uniformAndStorageBuffer16BitAccess = VK_TRUE,
        };
        VkPhysicalDeviceVulkan13Features features13 = {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
            .pNext = &features11,
        };
        VkPhysicalDeviceVulkan12Features features12 = {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
            .pNext = &features13,
            .storageBuffer8BitAccess = VK_TRUE,
            .uniformAndStorageBuffer8BitAccess = VK_TRUE,
            .shaderFloat16 = VK_TRUE,
            .shaderInt8 = VK_TRUE,
            .samplerFilterMinmax = VK_TRUE,

        };

        VkPhysicalDeviceFeatures2 allDeviceFeatures = {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
            .pNext = &features12,
            .features = {
                .independentBlend = VK_TRUE,
                .fillModeNonSolid = VK_TRUE,
                .fragmentStoresAndAtomics = VK_TRUE,
                .shaderInt16 = VK_TRUE,
                .sparseBinding = VK_TRUE,
            },
        };

        vkGetPhysicalDeviceFeatures2(state.physicalDevice,
                                     &allDeviceFeatures);

#define CHECK_FEATURE(revision, feature)\
        if(!revision.feature) {\
            logInfo(#feature " is not available");\
            return ReturnCode::UNKNOWN; \
        }

        CHECK_FEATURE(features14, pushDescriptor);
        CHECK_FEATURE(features13, synchronization2);
        CHECK_FEATURE(features13, dynamicRendering);
        CHECK_FEATURE(features12, bufferDeviceAddress);
        CHECK_FEATURE(features12, samplerFilterMinmax);
        CHECK_FEATURE(features12, runtimeDescriptorArray);
        CHECK_FEATURE(features12, storageBuffer8BitAccess);
        CHECK_FEATURE(features12, uniformAndStorageBuffer8BitAccess);
        CHECK_FEATURE(features12, shaderInt8);
        CHECK_FEATURE(features12, descriptorBindingPartiallyBound);
        CHECK_FEATURE(features12, descriptorBindingVariableDescriptorCount);
        CHECK_FEATURE(features12, shaderSampledImageArrayNonUniformIndexing);
        CHECK_FEATURE(features12, shaderFloat16);
        CHECK_FEATURE(features12, drawIndirectCount);
        CHECK_FEATURE(features11, shaderDrawParameters);
        CHECK_FEATURE(features11, storageBuffer16BitAccess);
        CHECK_FEATURE(features11, uniformAndStorageBuffer16BitAccess);
        CHECK_FEATURE(allDeviceFeatures.features, independentBlend);
        CHECK_FEATURE(allDeviceFeatures.features, samplerAnisotropy);
        CHECK_FEATURE(allDeviceFeatures.features, multiDrawIndirect);
        CHECK_FEATURE(allDeviceFeatures.features, drawIndirectFirstInstance);
        CHECK_FEATURE(allDeviceFeatures.features, fragmentStoresAndAtomics);
        CHECK_FEATURE(allDeviceFeatures.features, shaderInt16);
        CHECK_FEATURE(allDeviceFeatures.features, fillModeNonSolid);
        CHECK_FEATURE(allDeviceFeatures.features, sparseBinding);

#undef CHECK_FEATURE

        features14 = VkPhysicalDeviceVulkan14Features {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES,
            .pushDescriptor = VK_TRUE,
        };

        features11 = VkPhysicalDeviceVulkan11Features {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES,
            .pNext = &features14,
            .storageBuffer16BitAccess = VK_TRUE,
            .uniformAndStorageBuffer16BitAccess = VK_TRUE,
            .shaderDrawParameters = VK_TRUE,
        };

        features13 = VkPhysicalDeviceVulkan13Features {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
            .pNext = &features11,
            .synchronization2 = VK_TRUE,
            .dynamicRendering = VK_TRUE
        };

        features12 = VkPhysicalDeviceVulkan12Features {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
            .pNext = &features13,
            .drawIndirectCount = VK_TRUE,
            .storageBuffer8BitAccess = VK_TRUE,
            .uniformAndStorageBuffer8BitAccess = VK_TRUE,
            .shaderFloat16 = VK_TRUE,
            .shaderInt8 = VK_TRUE,
            .shaderSampledImageArrayNonUniformIndexing = VK_TRUE,
            .descriptorBindingPartiallyBound = VK_TRUE,
            .descriptorBindingVariableDescriptorCount = VK_TRUE,
            .runtimeDescriptorArray = VK_TRUE,
            .samplerFilterMinmax = VK_TRUE,
            .bufferDeviceAddress = VK_TRUE,
        };

        allDeviceFeatures = VkPhysicalDeviceFeatures2 {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
            .pNext = &features12,
            .features = {
                .independentBlend = VK_TRUE,
                .multiDrawIndirect = VK_TRUE,
                .drawIndirectFirstInstance = VK_TRUE,
                .fillModeNonSolid = VK_TRUE,
                .samplerAnisotropy = VK_TRUE,
                .fragmentStoresAndAtomics = VK_TRUE,
                .shaderInt16 = VK_TRUE,
                .sparseBinding = VK_TRUE,
            },
        };

        VkDeviceCreateInfo deviceCreateInfo = {
            .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
            .pNext = &allDeviceFeatures,
            .queueCreateInfoCount = static_cast<std::uint32_t>(queueCreateInfos.size()),
            .pQueueCreateInfos = queueCreateInfos.data(),
            .enabledExtensionCount = sizeof(desiredDeviceExtensions) / sizeof(desiredDeviceExtensions[0]),
            .ppEnabledExtensionNames = desiredDeviceExtensions,
        };

        if(vkCreateDevice(state.physicalDevice, &deviceCreateInfo, nullptr, &state.device) != VK_SUCCESS) {
            logError("Could not access GPU driver");
            return ReturnCode::UNKNOWN;
        }
        logDebug("Logical device created");

        state.queues = new Queue[uniqueQueueFamilies.size()];
        state.queueCount = uniqueQueueFamilies.size();
        std::uint32_t i = 0;
        for(std::uint32_t qFam : uniqueQueueFamilies) {
            if(createQueue(state.queues[i],
                           state,
                           queueFamilies[qFam].queueFlags,
                           qFam,
                           qFam == 0) != ReturnCode::OK) {
                return ReturnCode::UNKNOWN;
            }
            i++;
        }

        /*=====================================
                    Vma initialization
          =====================================*/

        VmaAllocatorCreateInfo vmaCreateInfo = {
            .flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT,
            .physicalDevice = state.physicalDevice,
            .device = state.device,
            .instance = state.instance,
        };

        vmaCreateAllocator(&vmaCreateInfo,
                           &state.allocator);



        /*=====================================
                    Swapchain creation
          =====================================*/

        VkSurfaceFormatKHR chosenFormat = surfaceFormats[0];
        for (const auto& availableFormat : surfaceFormats) {
            if (availableFormat.format == VK_FORMAT_B8G8R8A8_SRGB && availableFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
                chosenFormat = availableFormat;
                break;
            }
        }
        state.swapchainImageFormat = chosenFormat;

        VkPresentModeKHR chosenPresentMode = surfacePresentModes[0];
        for (const VkPresentModeKHR pm : surfacePresentModes) {
            if (pm == VK_PRESENT_MODE_IMMEDIATE_KHR) {
                chosenPresentMode = pm;
                break;
            }
        }
        state.swapchainPresentMode = chosenPresentMode;

        logInfo("Presentmode: %d", chosenPresentMode);

        VkExtent2D chosenExtent;

        if(surfaceCapabilities.currentExtent.width != std::numeric_limits<std::uint32_t>::max()) {
            chosenExtent = surfaceCapabilities.currentExtent;
        } else {
            chosenExtent = {
                settings->width,
                settings->height
            };

            chosenExtent.width = std::clamp(chosenExtent.width, surfaceCapabilities.minImageExtent.width, surfaceCapabilities.maxImageExtent.width);
            chosenExtent.height = std::clamp(chosenExtent.height, surfaceCapabilities.minImageExtent.height, surfaceCapabilities.maxImageExtent.height);
        }
        state.swapchainExtent = chosenExtent;

        std::uint32_t imageCount = surfaceCapabilities.minImageCount + 1;
        if (surfaceCapabilities.maxImageCount > 0 && imageCount > surfaceCapabilities.maxImageCount) {
            imageCount = surfaceCapabilities.maxImageCount;
        }

        DescriptorAllocator::PoolSizeRatio ratios[] = {
            DescriptorAllocator::PoolSizeRatio{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 30 },
            DescriptorAllocator::PoolSizeRatio{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 30 },
            DescriptorAllocator::PoolSizeRatio{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 30 },
            DescriptorAllocator::PoolSizeRatio{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 40 },
            DescriptorAllocator::PoolSizeRatio{ VK_DESCRIPTOR_TYPE_SAMPLER, 100 },
            DescriptorAllocator::PoolSizeRatio{ VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 100 },
        };

        state.descriptors.init(state.device, 100000, ratios);

        if(createSwapchain(state,
                           chosenExtent,
                           chosenFormat,
                           chosenPresentMode,
                           imageCount) != ReturnCode::OK) {
            logError("Could not create swapchain");
            return ReturnCode::UNKNOWN;
        }

        VkSemaphoreCreateInfo semaphoreCreateInfo = {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
        };

        VkFenceCreateInfo fenceCreateInfo = {
            .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
            .flags = VK_FENCE_CREATE_SIGNALED_BIT,
        };

        state.renderFinishedSemaphores.resize(imageCount);
        for(int i = 0; i != imageCount; i++) {
            if(vkCreateSemaphore(state.device, &semaphoreCreateInfo, nullptr, &state.renderFinishedSemaphores[i]) != VK_SUCCESS) {
                logError("Could not create sync objects");
                return ReturnCode::UNKNOWN;
            }
        }

        for(int i = 0; i != MAX_IN_FLIGHT_FRAMES; i++) {
            if(vkCreateSemaphore(state.device, &semaphoreCreateInfo, nullptr, &state.frames[i].imageAvailableSemaphore) != VK_SUCCESS ||
               vkCreateFence    (state.device, &fenceCreateInfo    , nullptr, &state.frames[i].inFlightFence)           != VK_SUCCESS) {
                logError("Could not create sync objects");
                return ReturnCode::UNKNOWN;
            }
        }
        return ReturnCode::OK;
    }

    ReturnCode createSwapchain(RendererState& state,
                               VkExtent2D extent,
                               VkSurfaceFormatKHR format,
                               VkPresentModeKHR presentMode,
                               std::uint32_t imageCount,
                               VkSwapchainKHR oldSwapchain) {
        KAMSKI_PROFILE();
        state.swapchainExtent = extent;
        state.swapchainImageFormat = format;
        state.swapchainPresentMode = presentMode;
        state.swapchainImageCount = imageCount;

        std::uint32_t queueFamilyIndices[] = {
            state.graphicsFamilyIndex,
            state.presentFamilyIndex,
            state.computeFamilyIndex
        };
        std::uint32_t familyCount = sizeof(queueFamilyIndices) / sizeof(queueFamilyIndices[0]);
        std::sort(queueFamilyIndices, queueFamilyIndices + familyCount);
        familyCount = std::unique(queueFamilyIndices, queueFamilyIndices + familyCount) - queueFamilyIndices;

        VkSwapchainCreateInfoKHR swapchainCreateInfo = {
            .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
            .surface = state.surface,
            .minImageCount = imageCount,
            .imageFormat = format.format,
            .imageColorSpace = format.colorSpace,
            .imageExtent = extent,
            .imageArrayLayers = 1,
            .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
            .imageSharingMode = (familyCount == 1 ? VK_SHARING_MODE_EXCLUSIVE : VK_SHARING_MODE_CONCURRENT),
            .queueFamilyIndexCount = familyCount,
            .pQueueFamilyIndices = queueFamilyIndices,
            .preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
            .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
            .presentMode = presentMode,
            .clipped = VK_TRUE,
            .oldSwapchain = oldSwapchain
        };

        if(vkCreateSwapchainKHR(state.device,
                             &swapchainCreateInfo,
                             nullptr,
                             &state.swapchain) != VK_SUCCESS) {
            logError("Could not create swapchain");
            return ReturnCode::UNKNOWN;
        }

        vkGetSwapchainImagesKHR(state.device,
                                state.swapchain,
                                &imageCount,
                                nullptr);
        state.swapchainImages.resize(imageCount);
        vkGetSwapchainImagesKHR(state.device,
                                state.swapchain,
                                &imageCount,
                                state.swapchainImages.data());

        state.swapchainImageViews.clear();
        state.swapchainImageViews.reserve(imageCount);
        for(VkImage img : state.swapchainImages) {
            VkImageViewCreateInfo createInfo = {
                .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                .image = img,
                .viewType = VK_IMAGE_VIEW_TYPE_2D,
                .format = format.format,
            };
            createInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
            createInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
            createInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
            createInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
            createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            createInfo.subresourceRange.baseMipLevel = 0;
            createInfo.subresourceRange.levelCount = 1;
            createInfo.subresourceRange.baseArrayLayer = 0;
            createInfo.subresourceRange.layerCount = 1;

            VkImageView imageView;
            if(vkCreateImageView(state.device,
                                 &createInfo,
                                 nullptr,
                                 &imageView) != VK_SUCCESS) {
                logError("Could not create image view");
                return ReturnCode::UNKNOWN;
            }
            state.swapchainImageViews.push_back(imageView);
        }
        return ReturnCode::OK;
    }

    ReturnCode recreateSwapchain(RendererState& state,
                                 const std::uint32_t x,
                                 const std::uint32_t y) {
        KAMSKI_PROFILE();
        vkDeviceWaitIdle(state.device);
        if(x == 0 || y == 0) {
            state.swapchainExtent.width = x;
            state.swapchainExtent.height = y;
            return ReturnCode::OK;
        }

        for(VkImageView imageView : state.swapchainImageViews) {
            vkDestroyImageView(state.device,
                               imageView,
                               nullptr);
        }

        VkSwapchainKHR oldSwapchain = state.swapchain;

        VkExtent2D chosenExtent;
        VkSurfaceCapabilitiesKHR surfaceCapabilities;
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(state.physicalDevice,
                                                  state.surface,
                                                  &surfaceCapabilities);
        if(surfaceCapabilities.currentExtent.width != std::numeric_limits<std::uint32_t>::max()) {
            chosenExtent = surfaceCapabilities.currentExtent;
        } else {
            chosenExtent.width = std::clamp(x, surfaceCapabilities.minImageExtent.width, surfaceCapabilities.maxImageExtent.width);
            chosenExtent.height = std::clamp(y, surfaceCapabilities.minImageExtent.height, surfaceCapabilities.maxImageExtent.height);
        }

        ReturnCode rc = createSwapchain(state,
                                        chosenExtent,
                                        state.swapchainImageFormat,
                                        state.swapchainPresentMode,
                                        state.swapchainImageCount,
                                        oldSwapchain);
        vkDestroySwapchainKHR(state.device,
                              oldSwapchain,
                              nullptr);
        return rc;
    }

    void Pipeline::bind(VkCommandBuffer cmd) { 
        vkCmdBindPipeline(cmd, bindPoint, handle);
    }
    
    PipelineBuilder::PipelineBuilder() {
        vertexInputAttributesSize = 0;
        multisample = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
            .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
            .sampleShadingEnable = VK_FALSE,
            .minSampleShading = 1.0f,
            .pSampleMask = nullptr,
            .alphaToCoverageEnable = VK_FALSE,
            .alphaToOneEnable = VK_FALSE,
        };

        viewportState = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
            .viewportCount = 1,
            .scissorCount = 1,
        };

        inputAssembly = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
            .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
            .primitiveRestartEnable = VK_FALSE,
        };

        rasterizer = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
            .polygonMode = VK_POLYGON_MODE_FILL,
            .cullMode = VK_CULL_MODE_BACK_BIT,
            .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
            .lineWidth = 1.0f,
        };

        colorBlendAttachment = {
            .blendEnable = VK_FALSE,
            .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
        };

        dynamicState = {
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR
        };

        renderInfo = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        };
        inputState = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        };
        depthStencil = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
            .depthTestEnable = VK_FALSE,
            .depthWriteEnable = VK_FALSE,
            .depthCompareOp = VK_COMPARE_OP_NEVER,
            .depthBoundsTestEnable = VK_FALSE,
            .stencilTestEnable = VK_FALSE,
            .front = {},
            .back = {},
            .minDepthBounds = 0.f,
            .maxDepthBounds = 1.f,
        };

        blendState = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
            .logicOpEnable = VK_FALSE,
            .logicOp = VK_LOGIC_OP_COPY,
            .attachmentCount = 1,
            .pAttachments = &colorBlendAttachment
        };
        basePipeline = VK_NULL_HANDLE;
        allowDerivatives = false;
    }

    PipelineBuilder& PipelineBuilder::addSpecializationConstantData(const void* data, const std::uint64_t size, const ShaderStage shaderStage) {
        addSpecializationConstantData(data, size, specializationConstants[shaderStage].size(), shaderStage);
        return *this;
    }

    PipelineBuilder& PipelineBuilder::setPushDescriptor(u32 setIndex) {
        pushDescriptorIndex = setIndex;
        return *this;
    }

    PipelineBuilder& PipelineBuilder::addVertexInputAttribute(VkFormat format,
                                                              std::uint32_t offset,
                                                              std::uint32_t size) {
        VkVertexInputAttributeDescription& attr = vertexInputAttributes.emplace_back();
        attr.location = vertexInputAttributes.size() - 1;
        attr.binding = 0;
        attr.format = format;
        attr.offset = offset;
        vertexInputAttributesSize += size;
        return *this;
    }

    PipelineBuilder& PipelineBuilder::addSpecializationConstantData(const void* data, const std::uint64_t size, const std::uint32_t constantId, const ShaderStage shaderStage) {
        VkSpecializationMapEntry entry;
        entry.size = size;
        entry.offset = specializationConstantData[shaderStage].size();
        entry.constantID = constantId;

        specializationConstantData[shaderStage].resize(specializationConstantData[shaderStage].size() + size);
        memcpy(specializationConstantData[shaderStage].data(), data, size);

        auto toFind = std::find_if(specializationConstants[shaderStage].begin(),
                                   specializationConstants[shaderStage].end(),
                                   [constantId](const VkSpecializationMapEntry& entry) {
                                       return entry.constantID == constantId;
                                   });
        if(toFind == specializationConstants[shaderStage].end()) {
            specializationConstants[shaderStage].emplace_back(entry);
        } else {
            *toFind = entry;
        }
        return *this;
    }

    PipelineBuilder& PipelineBuilder::clearSpecializationConstants(const ShaderStage shaderStage) {
        if(shaderStage == SHADER_STAGE_COUNT) {
            for(auto& v : specializationConstants) {
                v.clear();
            }

            for(auto& v : specializationConstantData) {
                v.clear();
            }
        } else {
            specializationConstants[shaderStage].clear();
            specializationConstantData[shaderStage].clear();
        }

        return *this;
    }

    PipelineBuilder& PipelineBuilder::setAllowDerivatives(bool allow) {
        allowDerivatives = allow;
        return *this;
    }

    PipelineBuilder& PipelineBuilder::disableBlending() {
        colorBlendAttachment = {};
        return *this;
    }

    PipelineBuilder& PipelineBuilder::enableBlendingAdditive() {
        colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        colorBlendAttachment.blendEnable = VK_TRUE;
        colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
        colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
        colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
        return *this;
    }

    PipelineBuilder& PipelineBuilder::enableBlendingAlpha() {
        colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        colorBlendAttachment.blendEnable = VK_TRUE;
        colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
        colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

        return *this;
    }

    PipelineBuilder& PipelineBuilder::enableDepthTest(bool depthWriteEnable, VkCompareOp op) {
        depthStencil.depthTestEnable = VK_TRUE;
        depthStencil.depthWriteEnable = depthWriteEnable;
        depthStencil.depthCompareOp = op;
        depthStencil.depthBoundsTestEnable = VK_FALSE;
        depthStencil.minDepthBounds = 0.f;
        depthStencil.maxDepthBounds = 1.f;
        return *this;
    }

    PipelineBuilder& PipelineBuilder::enableStencilTest(VkCompareOp compareOp, bool enableWriting) {
        depthStencil.stencilTestEnable = VK_TRUE;
        depthStencil.back = {
            .failOp = VK_STENCIL_OP_REPLACE,
            .passOp = VK_STENCIL_OP_REPLACE,
            .depthFailOp = VK_STENCIL_OP_REPLACE,
            .compareOp = compareOp,
            .compareMask = 0xff,
            .writeMask = static_cast<uint32_t>(enableWriting ? 0xff : 0),
            .reference = 1
        };
        depthStencil.front = depthStencil.back;
        return *this;
    }

    PipelineBuilder& PipelineBuilder::addShaders(std::string_view name, VkShaderStageFlags stageFlags) {
        if(stageFlags & VK_SHADER_STAGE_VERTEX_BIT) {
            shaderNames[SHADER_STAGE_VERTEX] = name;
        }

        if(stageFlags & VK_SHADER_STAGE_FRAGMENT_BIT) {
            shaderNames[SHADER_STAGE_FRAGMENT] = name;
        }

        if(stageFlags & VK_SHADER_STAGE_COMPUTE_BIT) {
            shaderNames[SHADER_STAGE_COMPUTE] = name;
        }

        return *this;
    }

    PipelineBuilder& PipelineBuilder::clearShaders(VkShaderStageFlags stageFlags) {
        if(stageFlags & VK_SHADER_STAGE_VERTEX_BIT) {
            shaderNames[SHADER_STAGE_VERTEX] = {};
        }

        if(stageFlags & VK_SHADER_STAGE_FRAGMENT_BIT) {
            shaderNames[SHADER_STAGE_FRAGMENT] = {};
        }

        if(stageFlags & VK_SHADER_STAGE_COMPUTE_BIT) {
            shaderNames[SHADER_STAGE_COMPUTE] = {};
        }

        return *this;
    }

    PipelineBuilder& PipelineBuilder::setInputTopology(VkPrimitiveTopology topology) {
        inputAssembly.topology = topology;
        return *this;
    }

    PipelineBuilder& PipelineBuilder::setPolygonMode(VkPolygonMode poly) {
        rasterizer.polygonMode = poly;
        return *this;
    }

    PipelineBuilder& PipelineBuilder::setCullMode(VkCullModeFlags cullMode, VkFrontFace face) {
        rasterizer.cullMode = cullMode;
        rasterizer.frontFace = face;
        return *this;
    }

    PipelineBuilder& PipelineBuilder::addColorAttachmentFormat(VkFormat format, std::uint32_t count) {
        std::uint64_t oldSize = colorAttachmentFormats.size();
        colorAttachmentFormats.resize(oldSize + count);
        for(std::uint32_t i = 0; i != count; i++) {
            colorAttachmentFormats[i + oldSize] = format;
        }
        return *this;
    }

    PipelineBuilder& PipelineBuilder::setDepthAttachmentFormat(VkFormat format) {
        renderInfo.depthAttachmentFormat = format;
        return *this;
    }

    PipelineBuilder& PipelineBuilder::setStencilAttachmentFormat(VkFormat format) {
        renderInfo.stencilAttachmentFormat = format;
        return *this;
    }

    PipelineBuilder& PipelineBuilder::setBasePipeline(VkPipeline pipeline) {
        basePipeline = pipeline;
        return *this;
    }

    static ReturnCode shaderModuleFromCache(ShaderModule& outputModule, const std::string& path, Cache& cache, const VkDevice device) {
        std::unique_lock lock(cache.shaderModuleMutex);

        auto moduleIter = cache.shaderModules.find(path);
        if(moduleIter == cache.shaderModules.end()) {
            lock.unlock();

            std::ifstream fileHandle(path, std::ios::ate | std::ios::binary);
            if(!fileHandle.is_open()) {
                logError("File %s not found", path.c_str());
                return ReturnCode::FILE_NOT_FOUND;
            }

            const std::uint64_t size = fileHandle.tellg();
            std::vector<std::uint32_t> fileData(size / 4);
            fileHandle.seekg(0);
            fileHandle.read((char*)fileData.data(), size);

            ShaderModule module;
            ReturnCode rc = createShaderModuleFromMemory(module.module, device, fileData.data(), size);
            if(rc != ReturnCode::OK) {
                logError("Could not create vertex shader module %s", path.c_str());
                return rc;
            }
            spvReflectCreateShaderModule(size, fileData.data(), &module.reflection);

            lock.lock();
            moduleIter = cache.shaderModules.find(path);
            if(moduleIter == cache.shaderModules.end()) {
                // if the module is NOT already cached, store it in the map, as it will be cleaned up later
                cache.shaderModules[path] = module;
                outputModule = module;
            } else {
                outputModule = moduleIter->second;
                lock.unlock();
                // if the module is already cached, cleanup the local one
                spvReflectDestroyShaderModule(&module.reflection);
                vkDestroyShaderModule(device, module.module, nullptr);
            }
        } else {
            outputModule = moduleIter->second;
        }

        return ReturnCode::OK;
    }

    static void gatherDescriptorSetsFromShaderModule(vector<DescriptorSet>& descriptorSets, const ShaderModule& module) {
        for(u32 setIter = 0; setIter != module.reflection.descriptor_set_count; setIter++) {
            const SpvReflectDescriptorSet& reflectSet = module.reflection.descriptor_sets[setIter];
            const u32 setIndex = reflectSet.set;
            descriptorSets.resize(std::max<std::size_t>(setIndex + 1, descriptorSets.size()));
            descriptorSets[setIndex].shaderStage |= module.reflection.shader_stage;

            DescriptorSet& descriptorSet = descriptorSets[setIndex];

            for(u32 bindingIndex = 0; bindingIndex != reflectSet.binding_count; bindingIndex++) {
                SpvReflectDescriptorBinding& reflectBinding = *reflectSet.bindings[bindingIndex];

                VkDescriptorSetLayoutBinding bindingInfo = {};
                bindingInfo.binding = reflectBinding.binding;
                bindingInfo.descriptorType = (VkDescriptorType)reflectBinding.descriptor_type;
                bindingInfo.descriptorCount = 1;

                for(u32 dim = 0; dim != reflectBinding.array.dims_count; dim++) {
                    bindingInfo.descriptorCount *= reflectBinding.array.dims[dim];
                }

                descriptorSet.count = std::max(descriptorSet.count, bindingInfo.binding + 1);
                switch(bindingInfo.descriptorType) {
                    case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER: {
                        descriptorSet.descriptors[bindingInfo.binding].type = Descriptor::IMAGE_SAMPLER;
                    } break;

                    case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
                    case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE: {
                        if(bindingInfo.descriptorCount == 0) {
                            descriptorSet.descriptors[bindingInfo.binding].type = Descriptor::IMAGES;
                        } else {
                            descriptorSet.descriptors[bindingInfo.binding].type = Descriptor::IMAGE;
                            descriptorSet.descriptors[bindingInfo.binding].imageType = bindingInfo.descriptorType;
                        }
                    } break;

                    case VK_DESCRIPTOR_TYPE_SAMPLER: {
                        descriptorSet.descriptors[bindingInfo.binding].type = Descriptor::SAMPLER;
                    } break;

                    case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
                    case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
                    case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
                    case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
                    case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
                    case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC: {
                        descriptorSet.descriptors[bindingInfo.binding].type = Descriptor::BUFFER;
                        descriptorSet.descriptors[bindingInfo.binding].bufferType = bindingInfo.descriptorType;
                    } break;

                    default: {
                        assert(false);
                    } break;
                }
            }
        }
    }

    ReturnCode PipelineBuilder::build(Pipeline& pipeline,
                                      Cache& cache,
                                      VkDevice device,
                                      std::string_view name) {
        KAMSKI_PROFILE();

        if(pipeline.handle != VK_NULL_HANDLE) {
            vkDestroyPipeline(device, pipeline.handle, nullptr);
        }

        const std::string vertexPath = std::string(shaderNames[SHADER_STAGE_VERTEX].begin(), shaderNames[SHADER_STAGE_VERTEX].end()) + std::string(".vert.slang.spv");
        const std::string fragmentPath = std::string(shaderNames[SHADER_STAGE_FRAGMENT].begin(), shaderNames[SHADER_STAGE_FRAGMENT].end()) + std::string(".frag.slang.spv");

        ShaderModule vertexModule;
        ShaderModule fragmentModule;

        if(ReturnCode rc = shaderModuleFromCache(vertexModule, vertexPath, cache, device); rc != ReturnCode::OK) {
            return rc;
        }

        if(!shaderNames[SHADER_STAGE_FRAGMENT].empty()) {
            if(ReturnCode rc = shaderModuleFromCache(fragmentModule, fragmentPath, cache, device); rc != ReturnCode::OK) {
                return rc;
            }
        }

        VkPushConstantRange pushConstantRange = {};

        if(vertexModule.reflection.push_constant_block_count != 0) {
            pushConstantRange.size = vertexModule.reflection.push_constant_blocks[0].size;
            pushConstantRange.stageFlags |= VK_SHADER_STAGE_VERTEX_BIT;
        }

        if(!shaderNames[SHADER_STAGE_FRAGMENT].empty()) {
            if(fragmentModule.reflection.push_constant_block_count != 0) {
                assert(pushConstantRange.size == 0 || pushConstantRange.size == fragmentModule.reflection.push_constant_blocks[0].size);
                pushConstantRange.size = fragmentModule.reflection.push_constant_blocks[0].size;
                pushConstantRange.stageFlags |= VK_SHADER_STAGE_FRAGMENT_BIT;
            }
        }

        vector<VkDescriptorSetLayout> descriptorSetLayouts;
        vector<DescriptorSet> descriptorSets;

        gatherDescriptorSetsFromShaderModule(descriptorSets, vertexModule);
        if(!shaderNames[SHADER_STAGE_FRAGMENT].empty()) {
            gatherDescriptorSetsFromShaderModule(descriptorSets, fragmentModule);
        }

        descriptorSetLayouts.resize(descriptorSets.size());
        for(u32 i = 0; i != descriptorSets.size(); i++) {
            descriptorSetLayouts[i] = descriptorSetLayoutFromCache(cache,
                                                                   descriptorSets[i],
                                                                   device,
                                                                   pushDescriptorIndex == i,
                                                                   {});
        }

        VkPipelineLayoutCreateInfo layoutCreateInfo = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            .setLayoutCount = static_cast<std::uint32_t>(descriptorSetLayouts.size()),
            .pSetLayouts = descriptorSetLayouts.data(),
            .pushConstantRangeCount = pushConstantRange.size ? 1u : 0u,
            .pPushConstantRanges = &pushConstantRange,
        };

        {
            PipelineLayoutInfo info;
            if(pushConstantRange.size != 0) {
                info.pushConstantRanges = {pushConstantRange};
            }
            info.layouts = descriptorSetLayouts;

            std::lock_guard lck(cache.pipelineLayoutMutex);
            VkPipelineLayout& layout = cache.pipelineLayouts[info];
            if(layout == VK_NULL_HANDLE) {
                if(vkCreatePipelineLayout(device,
                                          &layoutCreateInfo,
                                          nullptr,
                                          &layout) != VK_SUCCESS) {
                    logError("Could not create pipeline layout");
                    return ReturnCode::UNKNOWN;
                }

#ifdef KAMSKI_DEBUG
                string layoutName(name.begin(), name.end());
                layoutName += "_pipelineLayout";

                VkDebugUtilsObjectNameInfoEXT nameInfo = {
                    .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT,
                    .objectType = VK_OBJECT_TYPE_PIPELINE_LAYOUT,
                    .objectHandle = (u64)layout,
                    .pObjectName = layoutName.c_str()
                };
                VkResult res = kvk::vkSetDebugUtilsObjectName(device, &nameInfo);
                kassert(res == VK_SUCCESS);
#endif
            }
            pipeline.layout = layout;
        }


        VkPipelineShaderStageCreateInfo shaderStages[] = {
            {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                .stage = VK_SHADER_STAGE_VERTEX_BIT,
                .module = vertexModule.module,
                .pName = "main",
            },

            {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
                .module = shaderNames[SHADER_STAGE_FRAGMENT].empty() ? VK_NULL_HANDLE : fragmentModule.module,
                .pName = "main",
            }
        };

        VkVertexInputBindingDescription bindingDesc = {
            .stride = vertexInputAttributesSize,
            .inputRate = VK_VERTEX_INPUT_RATE_VERTEX
        };
        if(!vertexInputAttributes.empty()) {
            inputState.vertexBindingDescriptionCount = 1;
            inputState.pVertexBindingDescriptions = &bindingDesc;
            inputState.vertexAttributeDescriptionCount = vertexInputAttributes.size();
            inputState.pVertexAttributeDescriptions = vertexInputAttributes.data();
        }

        VkPipelineDynamicStateCreateInfo dynamicStateInfo = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
            .dynamicStateCount = static_cast<std::uint32_t>(dynamicState.size()),
            .pDynamicStates = dynamicState.data(),
        };

        renderInfo.colorAttachmentCount = colorAttachmentFormats.size();
        renderInfo.pColorAttachmentFormats = colorAttachmentFormats.data();

        VkPipelineColorBlendAttachmentState defaultBlendAttachment = {
            .blendEnable = VK_FALSE,
            .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
        };
        std::vector<VkPipelineColorBlendAttachmentState> attachments(colorAttachmentFormats.size(), defaultBlendAttachment);
        if(colorAttachmentFormats.size() != 0) {
            attachments[0] = colorBlendAttachment;
        }

        blendState.attachmentCount = attachments.size();
        blendState.pAttachments = attachments.data();

        VkSpecializationInfo specializationInfos[2];
        for(int i = 0; i < SHADER_STAGE_COMPUTE; i++) {
            if(specializationConstants[i].empty()) {
                continue;
            }

            specializationInfos[i] = {
                .mapEntryCount = std::uint32_t(specializationConstants[i].size()),
                .pMapEntries = specializationConstants[i].data(),
                .dataSize = specializationConstantData[i].size(),
                .pData = specializationConstantData[i].data(),
            };
    
            shaderStages[i].pSpecializationInfo = &specializationInfos[i];
        }

        VkGraphicsPipelineCreateInfo createInfo = {
            .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
            .pNext = &renderInfo,
            .flags = (allowDerivatives ? VK_PIPELINE_CREATE_ALLOW_DERIVATIVES_BIT : 0u) | (basePipeline != VK_NULL_HANDLE ? VK_PIPELINE_CREATE_DERIVATIVE_BIT : 0u),
            .stageCount = shaderNames[SHADER_STAGE_FRAGMENT].empty() ? 1u : 2u,
            .pStages = shaderStages,
            .pVertexInputState = &inputState,
            .pInputAssemblyState = &inputAssembly,
            .pViewportState = &viewportState,
            .pRasterizationState = &rasterizer,
            .pMultisampleState = &multisample,
            .pDepthStencilState = &depthStencil,
            .pColorBlendState = &blendState,
            .pDynamicState = &dynamicStateInfo,
            .layout = pipeline.layout,
            .basePipelineHandle = basePipeline,
            .basePipelineIndex = -1,
        };

        if(vkCreateGraphicsPipelines(device,
                                     VK_NULL_HANDLE,
                                     1,
                                     &createInfo,
                                     nullptr,
                                     &pipeline.handle) != VK_SUCCESS) {
            logError("Could not create graphics pipeline");
            return ReturnCode::UNKNOWN;
        }

#ifdef KAMSKI_DEBUG
        VkDebugUtilsObjectNameInfoEXT nameInfo = {
            .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT,
            .objectType = VK_OBJECT_TYPE_PIPELINE,
            .objectHandle = (u64)pipeline.handle,
            .pObjectName = name.data()
        };
        VkResult res = kvk::vkSetDebugUtilsObjectName(device, &nameInfo);
        kassert(res == VK_SUCCESS);
#endif

        pipeline.bindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        return ReturnCode::OK;
    }

    ReturnCode PipelineBuilder::buildCompute(Pipeline& pipeline, Cache& cache, const VkDevice device, std::string_view name) {
        KAMSKI_PROFILE();
        if(pipeline.handle != VK_NULL_HANDLE) {
            vkDestroyPipeline(device, pipeline.handle, nullptr);
        }

        const std::string computePath = std::string(shaderNames[SHADER_STAGE_COMPUTE].begin(), shaderNames[SHADER_STAGE_COMPUTE].end()) + std::string(".comp.slang.spv");

        ShaderModule computeModule;

        if(ReturnCode rc = shaderModuleFromCache(computeModule, computePath, cache, device); rc != ReturnCode::OK) {
            return rc;
        }
        
        VkPushConstantRange pushConstantRange = {};

        if(computeModule.reflection.push_constant_block_count != 0) {
            pushConstantRange.size = computeModule.reflection.push_constant_blocks[0].size;
            pushConstantRange.stageFlags |= VK_SHADER_STAGE_COMPUTE_BIT;
        }

        vector<VkDescriptorSetLayout> descriptorSetLayouts;
        vector<DescriptorSet> descriptorSets;

        gatherDescriptorSetsFromShaderModule(descriptorSets, computeModule);

        descriptorSetLayouts.resize(descriptorSets.size());
        for(u32 i = 0; i != descriptorSets.size(); i++) {
            descriptorSetLayouts[i] = descriptorSetLayoutFromCache(cache,
                                                                   descriptorSets[i],
                                                                   device,
                                                                   pushDescriptorIndex == i,
                                                                   {});
        }

        VkPipelineLayoutCreateInfo layoutCreateInfo = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            .setLayoutCount = static_cast<std::uint32_t>(descriptorSetLayouts.size()),
            .pSetLayouts = descriptorSetLayouts.data(),
            .pushConstantRangeCount = pushConstantRange.size ? 1u : 0u,
            .pPushConstantRanges = &pushConstantRange,
        };

        {
            PipelineLayoutInfo info;
            if(pushConstantRange.size != 0) {
                info.pushConstantRanges = {pushConstantRange};
            }
            info.layouts = descriptorSetLayouts;

            std::lock_guard lck(cache.pipelineLayoutMutex);
            VkPipelineLayout& layout = cache.pipelineLayouts[info];
            if(layout == VK_NULL_HANDLE) {
                if(vkCreatePipelineLayout(device,
                                          &layoutCreateInfo,
                                          nullptr,
                                          &layout) != VK_SUCCESS) {
                    logError("Could not create pipeline layout");
                    return ReturnCode::UNKNOWN;
                }
#ifdef KAMSKI_DEBUG
                string layoutName(name.begin(), name.end());
                layoutName += "_pipelineLayout";

                VkDebugUtilsObjectNameInfoEXT nameInfo = {
                    .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT,
                    .objectType = VK_OBJECT_TYPE_PIPELINE_LAYOUT,
                    .objectHandle = (u64)layout,
                    .pObjectName = layoutName.c_str()
                };
                VkResult res = kvk::vkSetDebugUtilsObjectName(device, &nameInfo);
                kassert(res == VK_SUCCESS);
#endif
            }
            pipeline.layout = layout;
        }


        VkPipelineShaderStageCreateInfo shaderStages[] = {
            {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                .stage = VK_SHADER_STAGE_COMPUTE_BIT,
                .module = computeModule.module,
                .pName = "main",
            },
        };

        VkSpecializationInfo specializationInfo;
        if(!specializationConstants[SHADER_STAGE_COMPUTE].empty()) {
            specializationInfo = {
                .mapEntryCount = std::uint32_t(specializationConstants[SHADER_STAGE_COMPUTE].size()),
                .pMapEntries = specializationConstants[SHADER_STAGE_COMPUTE].data(),
                .dataSize = specializationConstantData[SHADER_STAGE_COMPUTE].size(),
                .pData = specializationConstantData[SHADER_STAGE_COMPUTE].data(),
            };
            shaderStages[0].pSpecializationInfo = &specializationInfo;
        } else {
            shaderStages[0].pSpecializationInfo = nullptr;
        }

        VkComputePipelineCreateInfo createInfo = {
            .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
            .flags = (allowDerivatives ? VK_PIPELINE_CREATE_ALLOW_DERIVATIVES_BIT : 0u) | (basePipeline != VK_NULL_HANDLE ? VK_PIPELINE_CREATE_DERIVATIVE_BIT : 0u),
            .stage = shaderStages[0],
            .layout = pipeline.layout,
            .basePipelineHandle = basePipeline,
            .basePipelineIndex = -1
        };

        if(vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &createInfo, nullptr, &pipeline.handle) != VK_SUCCESS) {
            logError("Could not create compute pipeline");
            return ReturnCode::UNKNOWN;
        }

#ifdef KAMSKI_DEBUG
        VkDebugUtilsObjectNameInfoEXT nameInfo = {
            .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT,
            .objectType = VK_OBJECT_TYPE_PIPELINE,
            .objectHandle = (u64)pipeline.handle,
            .pObjectName = name.data()
        };
        VkResult res = kvk::vkSetDebugUtilsObjectName(device, &nameInfo);
        kassert(res == VK_SUCCESS);
#endif

        pipeline.bindPoint = VK_PIPELINE_BIND_POINT_COMPUTE;
        return ReturnCode::OK;
    }

    ReturnCode createBuffer(AllocatedBuffer& buffer,
                            VkDevice device,
                            VmaAllocator allocator,
                            std::uint64_t size,
                            VkBufferUsageFlags bufferUsage,
                            VmaMemoryUsage memoryUsage) {
        KAMSKI_PROFILE();
        if(buffer.buffer != VK_NULL_HANDLE) {
            destroyBuffer(buffer, allocator);
        }

        VkBufferCreateInfo bufferCreateInfo = {
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size = size,
            .usage = bufferUsage,
        };

        VmaAllocationCreateInfo allocInfo = {
            .flags = VMA_ALLOCATION_CREATE_MAPPED_BIT,
            .usage = memoryUsage,
        };

        if(vmaCreateBuffer(allocator,
                           &bufferCreateInfo,
                           &allocInfo,
                           &buffer.buffer,
                           &buffer.allocation,
                           nullptr) != VK_SUCCESS) {
            logError("Could not allocate buffer");
            return ReturnCode::UNKNOWN;
        }
        if(bufferUsage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) {
            VkBufferDeviceAddressInfo deviceAddressInfo = {
                .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
                .buffer = buffer.buffer
            };
            buffer.address = vkGetBufferDeviceAddress(device, &deviceAddressInfo);
        } else {
            buffer.address = 0;
        }

        buffer.usage = bufferUsage;
        buffer.size = size;

        return ReturnCode::OK;
    }

    void destroyBuffer(AllocatedBuffer& buffer,
                       VmaAllocator allocator) {
        KAMSKI_PROFILE();
        vmaDestroyBuffer(allocator,
                         buffer.buffer,
                         buffer.allocation);
    }

    ReturnCode createImage(AllocatedImage& image,
                           RendererState& state,
                           const VkFormat format,
                           const VkExtent3D extent,
                           const VkImageUsageFlags usageFlags,
                           bool isCubemap,
                           std::uint32_t mipLevels) {
        KAMSKI_PROFILE();
        if(image.image != VK_NULL_HANDLE) {
            destroyImage(image, state.device, state.allocator);
        }

        if(mipLevels > 1) {
            VkFormatProperties formatProps;
            vkGetPhysicalDeviceFormatProperties(state.physicalDevice, format, &formatProps);
            assert(formatProps.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_SRC_BIT);
            assert(formatProps.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_DST_BIT);
        }

        VkImageCreateInfo imageInfo = imageCreateInfo(state.physicalDevice,
                                                      format,
                                                      usageFlags,
                                                      extent,
                                                      isCubemap ? 6 : 1,
                                                      mipLevels);

        VmaAllocationCreateInfo imageAllocInfo = {
            .usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
            .requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        };

        if(vmaCreateImage(state.allocator,
                          &imageInfo,
                          &imageAllocInfo,
                          &image.image,
                          &image.allocation,
                          nullptr) != VK_SUCCESS) {
            logError("Could not allocate image memory");
            return ReturnCode::UNKNOWN;
        }

        VkImageAspectFlags aspect;
        switch(format) {
            case VK_FORMAT_D24_UNORM_S8_UINT: {
                aspect = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
            } break;

            case VK_FORMAT_D32_SFLOAT: {
                aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
            } break;

            default: {
                aspect = VK_IMAGE_ASPECT_COLOR_BIT;
            } break;
        }

        VkImageViewCreateInfo imageViewInfo = imageViewCreateInfo(format, image.image, aspect, isCubemap, 0, mipLevels);
        if(vkCreateImageView(state.device, &imageViewInfo, nullptr, &image.view) != VK_SUCCESS) {
            logError("Could not create draw image");
            return ReturnCode::UNKNOWN;
        }
        image.format = format;
        image.extent = extent;
        image.usage = usageFlags;
        return ReturnCode::OK;
    }

    ReturnCode createImage(AllocatedImage& image,
                           RendererState& state,
                           const void* data,
                           const VkFormat format,
                           const VkExtent3D extent,
                           const VkImageUsageFlags usage,
                           const std::uint32_t mipLevels) {
        KAMSKI_PROFILE();
        const VkImageUsageFlags usageFlags = usage | VK_IMAGE_USAGE_TRANSFER_DST_BIT | (mipLevels > 1 ? VK_IMAGE_USAGE_TRANSFER_SRC_BIT : 0);
        const std::uint64_t size = extent.width * extent.height * extent.depth * 4;
        AllocatedBuffer stagingBuffer = {};
        ReturnCode rc = createBuffer(stagingBuffer,
                                     state.device,
                                     state.allocator,
                                     size,
                                     VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                     VMA_MEMORY_USAGE_CPU_ONLY);

        if(rc != ReturnCode::OK) {
            logError("Could not create staging buffer");
            return rc;
        }
        memcpy(stagingBuffer.allocation->GetMappedData(), data, size);

        rc = createImage(image,
                         state,
                         format,
                         extent,
                         usageFlags,
                         false,
                         mipLevels);
        if(rc != ReturnCode::OK) {
            logError("Could not create image");
            return rc;
        }

        auto transferFunc = [&](VkCommandBuffer cmd) {
            KAMSKI_PROFILE();
            transitionImageMip(cmd,
                               image.image,
                               0, 1,
                               VK_IMAGE_LAYOUT_UNDEFINED,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 
                               VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                               0,
                               VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                               VK_ACCESS_2_MEMORY_WRITE_BIT);

            VkBufferImageCopy copyRegion = {
                .bufferOffset = 0,
                .bufferRowLength = 0,
                .bufferImageHeight = 0,

                .imageSubresource = {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .mipLevel = 0,
                    .baseArrayLayer = 0,
                    .layerCount = 1,
                },
                .imageExtent = extent,
            };

            vkCmdCopyBufferToImage(cmd,
                                   stagingBuffer.buffer,
                                   image.image,
                                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                   1,
                                   &copyRegion);
            if(mipLevels > 1) {
                std::uint32_t mipWidth = extent.width;
                std::uint32_t mipHeight = extent.height;

                transitionImageMip(cmd,
                                   image.image,
                                   0, 1,
                                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                   VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, 
                                   VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                                   VK_ACCESS_2_MEMORY_WRITE_BIT,
                                   VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                                   VK_ACCESS_2_MEMORY_READ_BIT);
                for(int i = 1; i != mipLevels; i++) {
                    transitionImageMip(cmd,
                                    image.image,
                                    i, 1,
                                    VK_IMAGE_LAYOUT_UNDEFINED,
                                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                    0,
                                    0,
                                    VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                                    VK_ACCESS_2_MEMORY_WRITE_BIT);
                    blitImageToImage(cmd,
                                     image.image,
                                     image.image,
                                     {mipWidth, mipHeight},
                                     {mipWidth > 1 ? mipWidth / 2 : 1, mipHeight > 1 ? mipHeight / 2 : 1},
                                     VK_IMAGE_ASPECT_COLOR_BIT,
                                     i - 1,
                                     i);
                    mipWidth  = std::max(1u, mipWidth  / 2);
                    mipHeight = std::max(1u, mipHeight / 2);
                    transitionImageMip(cmd,
                                    image.image,
                                    i, 1,
                                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                    VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                                    VK_ACCESS_2_MEMORY_WRITE_BIT,
                                    VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                                    VK_ACCESS_2_MEMORY_READ_BIT);
                }
                transitionImageMip(cmd,
                                   image.image,
                                   0, mipLevels,
                                   VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            } else {
                transitionImage(cmd,
                                image.image,
                                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                                VK_ACCESS_2_MEMORY_WRITE_BIT,
                                VK_PIPELINE_STAGE_2_NONE,
                                0);
            }
        };

        PoolInfo poolInfo = lockCommandPool(state, VK_QUEUE_GRAPHICS_BIT);
        defer {
            unlockCommandPool(state, poolInfo);
        };
        VkResult res = kvk::immediateSubmit(poolInfo.queue->commandBuffers[poolInfo.poolIndex],
                                            state.device,
                                            poolInfo.queue->handle,
                                            poolInfo.queue->submitMutex,
                                            transferFunc);
        if(res != VK_SUCCESS) {
            logError("transfer failed: %d", res);
            return ReturnCode::UNKNOWN;
        }

        destroyBuffer(stagingBuffer,
                      state.allocator);
        return ReturnCode::OK;
    }

    ReturnCode createCubemap(AllocatedImage& image,
                             RendererState& state,
                             const CubemapContents& data,
                             const VkFormat format,
                             const VkExtent2D extent,
                             VkImageUsageFlags usage) {
        KAMSKI_PROFILE();
        const VkImageUsageFlags usageFlags = usage | VK_IMAGE_USAGE_TRANSFER_DST_BIT;

        const std::uint64_t size = extent.width * extent.height * 6 * 4;
        const std::uint64_t imageSize = extent.width * extent.height * 4;

        AllocatedBuffer stagingBuffer = {};
        ReturnCode rc = createBuffer(stagingBuffer,
                                     state.device,
                                     state.allocator,
                                     size,
                                     VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                     VMA_MEMORY_USAGE_CPU_ONLY);

        if(rc != ReturnCode::OK) {
            logError("Could not create staging buffer");
            return rc;
        }
        defer {
            destroyBuffer(stagingBuffer, state.allocator);
        };

        std::uint8_t* dst = (std::uint8_t*) stagingBuffer.allocation->GetMappedData();
        for(std::uint64_t i = 0; i != 6; i++) {
            memcpy(dst + imageSize * i,
                   data.imageContents[i],
                   imageSize);
        }

        rc = createImage(image,
                         state,
                         format,
                         VkExtent3D{extent.width, extent.height, 1},
                         usageFlags,
                         true);
        if(rc != ReturnCode::OK) {
            logError("Could not create image");
            return rc;
        }

        auto transferFunc = [&](VkCommandBuffer cmd) {
            KAMSKI_PROFILE();
            transitionImage(cmd,
                            image.image,
                            VK_IMAGE_LAYOUT_UNDEFINED,
                            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 
                            VK_PIPELINE_STAGE_2_NONE,
                            0,
                            VK_PIPELINE_STAGE_2_TRANSFER_BIT);

            VkBufferImageCopy copyRegion = {
                .bufferOffset = 0,
                .bufferRowLength = 0,
                .bufferImageHeight = 0,

                .imageSubresource = {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .mipLevel = 0,
                    .baseArrayLayer = 0,
                    .layerCount = 6,
                },
                .imageExtent = {
                    .width = extent.width,
                    .height = extent.height,
                    .depth = 1,
                },
            };

            vkCmdCopyBufferToImage(cmd,
                                   stagingBuffer.buffer,
                                   image.image,
                                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                   1,
                                   &copyRegion);
            transitionImage(cmd,
                            image.image,
                            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                            VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                            VK_ACCESS_2_MEMORY_WRITE_BIT,
                            VK_PIPELINE_STAGE_2_NONE,
                            0);
        };

        PoolInfo poolInfo = lockCommandPool(state, VK_QUEUE_TRANSFER_BIT);
        defer {
            unlockCommandPool(state, poolInfo);
        };
        VkResult res = kvk::immediateSubmit(poolInfo.queue->commandBuffers[poolInfo.poolIndex],
                                            state.device,
                                            poolInfo.queue->handle,
                                            poolInfo.queue->submitMutex,
                                            transferFunc);
        if(res != VK_SUCCESS) {
            logError("transfer failed: %d", res);
            return ReturnCode::UNKNOWN;
        }

        return ReturnCode::OK;
    }

    void destroyImage(AllocatedImage& image,
                      VkDevice device,
                      VmaAllocator allocator) {
        KAMSKI_PROFILE();
        vkDestroyImageView(device,
                           image.view,
                           nullptr);
        vmaDestroyImage(allocator,
                        image.image,
                        image.allocation);
    }


    void DescriptorAllocator::init(VkDevice device,
                                   std::uint32_t initialSets,
                                   std::span<PoolSizeRatio> poolRatios) {
        KAMSKI_PROFILE();
        ratios.clear();
        VkDescriptorPool newPool = createPool(device, initialSets, poolRatios);
        setsPerPool = initialSets * 1.5f;
        readyPools.push_back(newPool);
    }

    VkDescriptorPool DescriptorAllocator::getPool(VkDevice device) {
        KAMSKI_PROFILE();
        VkDescriptorPool retval;

        if(!readyPools.empty()) {
            retval = readyPools.back();
            readyPools.pop_back();
        } else {
            retval = createPool(device,
                                setsPerPool,
                                ratios);

            setsPerPool = std::min(setsPerPool + setsPerPool / 2, MAX_SETS_PER_POOL);
        }
        return retval;
    }

    VkDescriptorPool DescriptorAllocator::createPool(VkDevice device,
                                                        const std::uint32_t setCount,
                                                        const std::span<PoolSizeRatio> poolRatios) {
        KAMSKI_PROFILE();
        std::vector<VkDescriptorPoolSize> poolSizes;
        for(const PoolSizeRatio& ratio : poolRatios) {
            poolSizes.push_back(VkDescriptorPoolSize {
                .type = ratio.type,
                .descriptorCount = std::uint32_t(ratio.ratio * setCount)
            });
        }

        VkDescriptorPoolCreateInfo createInfo = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
            .maxSets = setCount,
            .poolSizeCount = std::uint32_t(poolSizes.size()),
            .pPoolSizes = poolSizes.data()
        };

        VkDescriptorPool retval;
        VkResult result = vkCreateDescriptorPool(device,
                                &createInfo,
                                nullptr,
                                &retval);

        return retval;
    }

    void DescriptorAllocator::clearPools(VkDevice device) {
        KAMSKI_PROFILE();
        for(auto p : readyPools) {
            vkResetDescriptorPool(device,
                                    p,
                                    0);
        }

        for(auto p : fullPools) {
            vkResetDescriptorPool(device,
                                    p,
                                    0);
        }
        readyPools.insert(readyPools.end(), fullPools.begin(), fullPools.end());
        fullPools.clear();
    }

    void DescriptorAllocator::destroyPools(VkDevice device) {
        KAMSKI_PROFILE();
        for(auto p : readyPools) {
            vkDestroyDescriptorPool(device,
                                    p,
                                    nullptr);
        }

        for(auto p : fullPools) {
            vkDestroyDescriptorPool(device,
                                    p,
                                    nullptr);
        }
        readyPools.clear();
        fullPools.clear();
    }

    ReturnCode DescriptorAllocator::alloc(VkDescriptorSet& set,
                                            VkDevice device,
                                            VkDescriptorSetLayout layout,
                                            void* pNext) {
        KAMSKI_PROFILE();
        VkDescriptorPool poolToUse = getPool(device);

        VkDescriptorSetAllocateInfo allocInfo = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            .pNext = pNext,
            .descriptorPool = poolToUse,
            .descriptorSetCount = 1,
            .pSetLayouts = &layout
        };

        VkResult result = vkAllocateDescriptorSets(device,
                                                    &allocInfo,
                                                    &set);
        while(result == VK_ERROR_OUT_OF_POOL_MEMORY || result == VK_ERROR_FRAGMENTED_POOL) {
            fullPools.push_back(poolToUse);
            poolToUse = getPool(device);
            allocInfo.descriptorPool = poolToUse;

            result = vkAllocateDescriptorSets(device,
                                              &allocInfo,
                                              &set);
        }
        readyPools.push_back(poolToUse);
        return ReturnCode::OK;
    }


    DescriptorWriter::DescriptorWriter() {
        bindingCount = 0;
    }

    void DescriptorWriter::writeBuffers(std::span<VkDescriptorBufferInfo> bufferInfos,
                                        VkDescriptorType type,
                                        std::uint32_t dstArrayElement) {
        writeBuffers(bindingCount, bufferInfos, type, dstArrayElement);
    }

    void DescriptorWriter::writeBuffers(int binding,
                                        std::span<VkDescriptorBufferInfo> bufferInfos,
                                       VkDescriptorType type,
                                       std::uint32_t dstArrayElement) {
        KAMSKI_PROFILE();

        VkWriteDescriptorSet write = {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstBinding = std::uint32_t(binding),
            .dstArrayElement = dstArrayElement,
            .descriptorCount = std::uint32_t(bufferInfos.size()),
            .descriptorType = type,
            .pBufferInfo = bufferInfos.data(),
        };
        writes.push_back(write);
        bindingCount++;
    }
    void DescriptorWriter::writeBuffer(VkBuffer buffer,
                                       const std::uint64_t size,
                                       const std::uint64_t offset,
                                       VkDescriptorType type,
                                       std::uint32_t dstArrayElement) {
        writeBuffer(bindingCount, buffer, size, offset, type, dstArrayElement);
    }
    void DescriptorWriter::writeBuffer(int binding,
                                       VkBuffer buffer,
                                       const std::uint64_t size,
                                       const std::uint64_t offset,
                                       VkDescriptorType type,
                                       std::uint32_t dstArrayElement) {
        KAMSKI_PROFILE();
        auto& info = bufferInfos.emplace_back(VkDescriptorBufferInfo{
            .buffer = buffer,
            .offset = offset,
            .range = size
        });

        VkWriteDescriptorSet write = {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstBinding = std::uint32_t(binding),
            .dstArrayElement = dstArrayElement,
            .descriptorCount = 1,
            .descriptorType = type,
            .pBufferInfo = &info,
        };
        writes.push_back(write);
        bindingCount++;
    }

    void DescriptorWriter::writeImages(std::span<VkDescriptorImageInfo> imageInfos,
                                       VkDescriptorType type, std::uint32_t dstArrayElement) {
        writeImages(bindingCount, imageInfos, type, dstArrayElement);
    }

    void DescriptorWriter::writeImages(int binding, std::span<VkDescriptorImageInfo> imageInfos,
                                       VkDescriptorType type, std::uint32_t dstArrayElement) {
        KAMSKI_PROFILE();

        VkWriteDescriptorSet write = {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstBinding = std::uint32_t(binding),
            .dstArrayElement = dstArrayElement,
            .descriptorCount = std::uint32_t(imageInfos.size()),
            .descriptorType = type,
            .pImageInfo = imageInfos.data(),
        };
        writes.push_back(write);
        bindingCount++;
    }

    void DescriptorWriter::writeImage(int binding,
                                      VkImageView view,
                                      VkSampler sampler,
                                      VkImageLayout layout,
                                      VkDescriptorType type,
                                      std::uint32_t dstArrayElement) {
        KAMSKI_PROFILE();
        auto& info = imageInfos.emplace_back(VkDescriptorImageInfo{
            .sampler = sampler,
            .imageView = view,
            .imageLayout = layout,
        });

        VkWriteDescriptorSet write = {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstBinding = std::uint32_t(binding),
            .dstArrayElement = dstArrayElement,
            .descriptorCount = 1,
            .descriptorType = type,
            .pImageInfo = &info,
        };
        writes.push_back(write);
        bindingCount++;
    }

    void DescriptorWriter::writeImage(VkImageView view,
                                      VkSampler sampler,
                                      VkImageLayout layout,
                                      VkDescriptorType type,
                                      std::uint32_t dstArrayElement) {
        writeImage(bindingCount, view, sampler, layout, type, dstArrayElement);
    }

    void DescriptorWriter::clear() {
        imageInfos.clear();
        bufferInfos.clear();
        writes.clear();
        bindingCount = 0;
    }

    void DescriptorWriter::updateSet(VkDevice device, VkDescriptorSet set) {
        KAMSKI_PROFILE();
        for(auto& write : writes) {
            write.dstSet = set;
        }

        vkUpdateDescriptorSets(device,
                               std::uint32_t(writes.size()),
                               writes.data(),
                               0,
                               nullptr);
    }

    void DescriptorWriter::push(VkCommandBuffer commandBuffer, u32 setIndex, const kvk::Pipeline& pipeline) {
        KAMSKI_PROFILE();
        vkCmdPushDescriptorSet(commandBuffer,
                               pipeline.bindPoint,
                               pipeline.layout,
                               setIndex,
                               writes.size(),
                               writes.data());
    }

    FrameData* startFrame(RendererState& state, std::uint32_t& frameIndex) {
        KAMSKI_PROFILE();
        frameIndex = state.currentFrame;
        FrameData& frame = state.frames[state.currentFrame];
        {
            KAMSKI_PROFILE_NAMED("WaitForFences");
            VkResult res = vkWaitForFences(state.device,
                                           1,
                                           &frame.inFlightFence,
                                           VK_TRUE,
                                           1000ull * 1000ull * 1000ull);
            if(res != VK_SUCCESS) {
                logInfo("Wait for fence failed %d",res);
                assert(false);
            }
        }
        std::uint32_t imageIndex;

        VkResult result = vkAcquireNextImageKHR(state.device,
                                                state.swapchain,
                                                std::numeric_limits<std::uint64_t>::max(),
                                                frame.imageAvailableSemaphore,
                                                VK_NULL_HANDLE,
                                                &imageIndex);
        frame.swapchainImageIndex = imageIndex;

        vkResetFences(state.device,
                        1,
                        &frame.inFlightFence);

        if (result == VK_ERROR_OUT_OF_DATE_KHR) {
            return nullptr;
        } else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
            logError("Something gone wrong: %d", result);
            return nullptr;
        }

        //
        // Flush the per-frame deletionQueue
        //
        for(auto iter = frame.deletionQueue.rbegin(); iter != frame.deletionQueue.rend(); ++iter) {
            (*iter)();
        }
        {
            KAMSKI_PROFILE_NAMED("Clear deletion queue");
            frame.deletionQueue.clear();
        }

        PoolInfo poolInfo = lockCommandPool(state, VK_QUEUE_GRAPHICS_BIT);
        frame.commandBuffer = poolInfo.queue->commandBuffers[poolInfo.poolIndex];
        frame.queue = poolInfo.queue;
        frame.inFlightFence = poolInfo.queue->fences[poolInfo.poolIndex];
        frame.deletionQueue.emplace_back([&state, poolInfo]() mutable {
            unlockCommandPool(state, poolInfo);
        });

        return &frame;
    }

    ReturnCode endFrame(RendererState& state, FrameData& frame) {
        KAMSKI_PROFILE();
        state.currentFrame = (state.currentFrame + 1) % MAX_IN_FLIGHT_FRAMES;
            

        VkPipelineStageFlags waitStages[] = {
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
        };

        VkSubmitInfo submitInfo = {
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .waitSemaphoreCount = 1,
            .pWaitSemaphores = &frame.imageAvailableSemaphore,
            .pWaitDstStageMask = waitStages,
            .commandBufferCount = 1,
            .pCommandBuffers = &frame.commandBuffer,
            .signalSemaphoreCount = 1,
            .pSignalSemaphores = &state.renderFinishedSemaphores[frame.swapchainImageIndex],
        };

        std::lock_guard lck (frame.queue->submitMutex);
        if(VkResult res = vkQueueSubmit(frame.queue->handle,
                         1,
                         &submitInfo,
                         frame.inFlightFence)) {
            logError("Queue submit failed: %d", res);
            return ReturnCode::UNKNOWN;
        }

        VkPresentInfoKHR presentInfo = {
            .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
            .waitSemaphoreCount = 1,
            .pWaitSemaphores = &state.renderFinishedSemaphores[frame.swapchainImageIndex],
            .swapchainCount = 1,
            .pSwapchains = &state.swapchain,
            .pImageIndices = &frame.swapchainImageIndex,
            .pResults = nullptr
        };

        VkResult result = vkQueuePresentKHR(frame.queue->handle, &presentInfo);
        if (result == VK_ERROR_OUT_OF_DATE_KHR) {
            return ReturnCode::UNKNOWN;
        } else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
            logError("Very wrong");
            return ReturnCode::UNKNOWN;
        }
        return ReturnCode::OK;
    }

    ReturnCode createMesh(kvk::Mesh& mesh,
                          RendererState& state,
                          std::span<std::uint32_t> indices,
                          std::span<std::uint8_t> vertices) {
        KAMSKI_PROFILE();
        const std::uint64_t vertexBufferSize = vertices.size();
        const std::uint64_t indexBufferSize = indices.size_bytes();
        mesh.indexCount = indices.size();

        kvk::ReturnCode rc = createBuffer(mesh.vertices,
                                          state.device,
                                          state.allocator,
                                          vertexBufferSize,
                                          VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                          VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                                          VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                                          VMA_MEMORY_USAGE_GPU_ONLY);

        if(rc != kvk::ReturnCode::OK) {
            return rc;
        }

        rc = createBuffer(mesh.indices,
                          state.device,
                          state.allocator,
                          indexBufferSize,
                          VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
                          VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                          VMA_MEMORY_USAGE_GPU_ONLY);

        if(rc != kvk::ReturnCode::OK) {
            return rc;
        }

        kvk::AllocatedBuffer stagingBuffer = {};
        rc = createBuffer(stagingBuffer,
                          state.device,
                          state.allocator,
                          vertexBufferSize + indexBufferSize,
                          VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                          VMA_MEMORY_USAGE_CPU_ONLY);
        if(rc != kvk::ReturnCode::OK) {
            return rc;
        }
        defer {
            destroyBuffer(stagingBuffer, state.allocator);
        };


        void* data;
        VkResult vkResult = vmaMapMemory(state.allocator, stagingBuffer.allocation, &data);
        if(vkResult != VK_SUCCESS) {
            logError("Could not map memory :( %d", vkResult);
            return rc;
        }
        memcpy(data, vertices.data(), vertexBufferSize);
        memcpy((char*)data + vertexBufferSize, indices.data(), indexBufferSize);
        vmaUnmapMemory(state.allocator, stagingBuffer.allocation);

        auto transferFunc = [&](VkCommandBuffer cmd) {
            KAMSKI_PROFILE();
            VkBufferCopy vertexCopy{ 0 };
            vertexCopy.dstOffset = 0;
            vertexCopy.srcOffset = 0;
            vertexCopy.size = vertexBufferSize;

            vkCmdCopyBuffer(cmd,
                stagingBuffer.buffer,
                mesh.vertices.buffer,
                1,
                &vertexCopy);

            VkBufferCopy indexCopy{ 0 };
            indexCopy.dstOffset = 0;
            indexCopy.srcOffset = vertexBufferSize;
            indexCopy.size = indexBufferSize;

            vkCmdCopyBuffer(cmd,
                stagingBuffer.buffer,
                mesh.indices.buffer,
                1,
                &indexCopy);
        };

        PoolInfo poolInfo = lockCommandPool(state, VK_QUEUE_TRANSFER_BIT);
        defer {
            unlockCommandPool(state, poolInfo);
        };
        vkResult = kvk::immediateSubmit(poolInfo.queue->commandBuffers[poolInfo.poolIndex],
                                        state.device,
                                        poolInfo.queue->handle,
                                        poolInfo.queue->submitMutex,
                                        transferFunc);

        if(vkResult != VK_SUCCESS) {
            logError("Immediate submit failed: %d", vkResult);
            return ReturnCode::UNKNOWN;
        }

        return ReturnCode::OK;
    }

    DescriptorSetLayoutBuilder::DescriptorSetLayoutBuilder() {
        bindingCount = 0;
    }

    DescriptorSetLayoutBuilder& DescriptorSetLayoutBuilder::addBinding(VkDescriptorType type, std::uint32_t descriptorCount, VkDescriptorBindingFlags flags) {
        bindings[bindingCount] = VkDescriptorSetLayoutBinding {
            .binding = bindingCount,
            .descriptorType = type,
            .descriptorCount = descriptorCount
        };
        flagArray[bindingCount] = flags;
        bindingCount++;
        return *this;
    }

    DescriptorSetLayoutBuilder& DescriptorSetLayoutBuilder::addBinding(u32 binding, VkDescriptorType type, std::uint32_t descriptorCount, VkDescriptorBindingFlags flags) {
        bindings[bindingCount] = VkDescriptorSetLayoutBinding {
            .binding = binding,
            .descriptorType = type,
            .descriptorCount = descriptorCount
        };
        flagArray[bindingCount] = flags;
        bindingCount++;
        return *this;
    }

    bool DescriptorSetLayoutBuilder::build(VkDescriptorSetLayout& layout,
                                           VkDevice device,
                                           VkShaderStageFlags stage) {
        KAMSKI_PROFILE();
        VkDescriptorSetLayoutBindingFlagsCreateInfo flags = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO,
            .bindingCount = bindingCount,
            .pBindingFlags = flagArray
        };
        if(kvk::createDescriptorSetLayout(layout,
                                          device,
                                          stage,
                                          std::span(bindings, bindingCount),
                                          &flags,
                                          false) != kvk::ReturnCode::OK) {
            logError("Could not create descriptor layout");
            return false;
        }
        return true;
    }

    bool DescriptorSetLayoutBuilder::buildPush(VkDescriptorSetLayout& layout,
                                               VkDevice device,
                                               VkShaderStageFlags stage) {
        KAMSKI_PROFILE();
        VkDescriptorSetLayoutBindingFlagsCreateInfo flags = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO,
            .bindingCount = bindingCount,
            .pBindingFlags = flagArray
        };
        if(kvk::createDescriptorSetLayout(layout,
                                          device,
                                          stage,
                                          std::span(bindings, bindingCount),
                                          &flags,
                                          true) != kvk::ReturnCode::OK) {
            logError("Could not create descriptor layout");
            return false;
        }
        return true;
    }

    DescriptorSetBuilder::DescriptorSetBuilder(Cache& cache):cache(cache) {}

    DescriptorSetBuilder& DescriptorSetBuilder::image(VkImageView view, VkSampler sampler, VkImageLayout layout) {
        assert(sampler != VK_NULL_HANDLE);

        descriptors[count].imageSampler = {view, sampler};
        descriptors[count].type = Descriptor::IMAGE_SAMPLER;
        count++;
        writer.writeImage(view, sampler, layout, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);

        return *this;
    }

    DescriptorSetBuilder& DescriptorSetBuilder::image(VkImageView view, VkDescriptorType type, VkImageLayout layout) {
        descriptors[count].image = view;
        descriptors[count].imageType = type;
        descriptors[count].type = Descriptor::IMAGE;

        count++;
        if(layout == 0) {
            if(type == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE) {
                layout = VK_IMAGE_LAYOUT_GENERAL;
            } else {
                layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            }
        }
        writer.writeImage(view, VK_NULL_HANDLE, layout, type);
        return *this;
    }

    DescriptorSetBuilder& DescriptorSetBuilder::images(std::span<kvk::AllocatedImage> imagesToUpload, u32 offset, VkImageLayout layout) {
        descriptors[count].lastUploadedImageIndex = offset + imagesToUpload.size();
        descriptors[count].type = Descriptor::IMAGES;

        imageInfoVector.resize(imagesToUpload.size());
        for(u32 i = 0; i != imagesToUpload.size(); i++) {
            imageInfoVector[i].imageView = imagesToUpload[i].view;
            imageInfoVector[i].imageLayout = layout;
        }
        writer.writeImages(imageInfoVector, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, offset);
        count++;
        return *this;
    }

    DescriptorSetBuilder& DescriptorSetBuilder::buffer(VkBuffer buffer, VkDescriptorType type, u64 size, u64 offset) {
        descriptors[count].buffer = buffer;
        descriptors[count].bufferType = type;
        descriptors[count].type = Descriptor::BUFFER;
        count++;

        writer.writeBuffer(buffer, size, offset, type);

        return *this;
    }

    DescriptorSetBuilder& DescriptorSetBuilder::sampler(VkSampler sampler) {
        descriptors[count].sampler = sampler;
        descriptors[count].type = Descriptor::SAMPLER;
        count++;
        writer.writeImage(VK_NULL_HANDLE, sampler, VK_IMAGE_LAYOUT_UNDEFINED, VK_DESCRIPTOR_TYPE_SAMPLER);
        
        return *this;
    }

    static VkDescriptorSetLayout descriptorSetLayoutFromCache(Cache& cache,
                                                              const DescriptorSet& set,
                                                              const VkDevice device,
                                                              bool isPushDescriptor,
                                                              std::string_view name) {
        std::lock_guard lck(cache.descriptorLayoutMutex);
        VkDescriptorSetLayout& layout = cache.descriptorLayouts[set];
        if(layout == VK_NULL_HANDLE) {
            DescriptorSetLayoutBuilder builder;
            for(u32 i = 0; i != set.count; i++) {
                switch(set.descriptors[i].type) {
                    case kvk::Descriptor::IMAGE_SAMPLER: {
                        builder.addBinding(i, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
                    } break;

                    case kvk::Descriptor::IMAGE: {
                        builder.addBinding(i, set.descriptors[i].imageType);
                    } break;

                    case kvk::Descriptor::SAMPLER: {
                        builder.addBinding(i, VK_DESCRIPTOR_TYPE_SAMPLER);
                    } break;

                    case kvk::Descriptor::BUFFER: {
                        builder.addBinding(i, set.descriptors[i].bufferType);
                    } break;

                    case kvk::Descriptor::IMAGES: {
                        builder.addBinding(i, 
                                           VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                                           std::numeric_limits<u16>::max(),
                                           VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT);
                    } break;

                    case kvk::Descriptor::NONE: {
                        // add nothing
                    } break;

                    default: {
                        crash();
                    } break;
                }
            }
            if(isPushDescriptor) {
                builder.buildPush(layout, device, set.shaderStage);
            } else {
                builder.build(layout, device, set.shaderStage);
            }
        }

        if(!name.empty()) {
#ifdef KAMSKI_DEBUG
            string layoutName(name.begin(), name.end());
            layoutName += "_layout";

            VkDebugUtilsObjectNameInfoEXT nameInfo = {
                .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT,
                .objectType = VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT,
                .objectHandle = (u64)layout,
                .pObjectName = layoutName.data()
            };
            VkResult res = vkSetDebugUtilsObjectName(device, &nameInfo);
            kassert(res == VK_SUCCESS);
#endif
        }
        return layout;
    }

    void DescriptorSetBuilder::buildInternal(std::string_view name, DescriptorSet& set) {
        const VkDevice device = cache.state->device;
        DescriptorAllocator& allocator = cache.state->descriptors;

        if(set.handle == VK_NULL_HANDLE) {
            memcpy(set.descriptors, descriptors, sizeof(descriptors[0]) * count);
            set.count = count;

            VkDescriptorSetLayout layout = descriptorSetLayoutFromCache(cache,
                                                                        set,
                                                                        device,
                                                                        false,
                                                                        name);
            ReturnCode rc;
            if(descriptors[count - 1].type == Descriptor::IMAGES) {
                const u32 descriptorCount = std::numeric_limits<u16>::max();
                VkDescriptorSetVariableDescriptorCountAllocateInfo setAllocateCountInfo = {
                    .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO,
                    .descriptorSetCount = 1,
                    .pDescriptorCounts = &descriptorCount,
                };
                rc = allocator.alloc(set.handle, device, layout, &setAllocateCountInfo);
            } else {
                rc = allocator.alloc(set.handle, device, layout);
            }

#ifdef KAMSKI_DEBUG
            VkDebugUtilsObjectNameInfoEXT nameInfo = {
                .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT,
                .objectType = VK_OBJECT_TYPE_DESCRIPTOR_SET,
                .objectHandle = (u64)set.handle,
                .pObjectName = name.data()
            };
            VkResult res = vkSetDebugUtilsObjectName(device, &nameInfo);
            kassert(res == VK_SUCCESS);
#endif
            assert(rc == ReturnCode::OK);
        } else {
            assert(count == set.count);

            for(u32 i = 0; i != count; i++) {
                assert(descriptors[i].type == set.descriptors[i].type);
                bool shouldRemove = false;
                switch(descriptors[i].type) {
                    case kvk::Descriptor::IMAGE_SAMPLER: {
                        shouldRemove = descriptors[i].imageSampler.image == set.descriptors[i].imageSampler.image && 
                           descriptors[i].imageSampler.sampler == set.descriptors[i].imageSampler.sampler;
                    } break;

                    case kvk::Descriptor::IMAGE: {
                        shouldRemove = descriptors[i].image == set.descriptors[i].image;
                    } break;

                    case kvk::Descriptor::SAMPLER: {
                        shouldRemove = descriptors[i].sampler == set.descriptors[i].sampler;
                    } break;

                    case kvk::Descriptor::BUFFER: {
                        shouldRemove = descriptors[i].buffer == set.descriptors[i].buffer;
                    } break;

                    case kvk::Descriptor::IMAGES: {
                        shouldRemove = writer.writes.back().descriptorCount == 0;
                    } break;

                    case kvk::Descriptor::NONE: {
                    } break;

                    default: {
                    } break;
                }

                if(shouldRemove) {
                    writer.writes.erase(std::find_if(writer.writes.begin(), writer.writes.end(),
                                                     [&](const VkWriteDescriptorSet& write){
                                                         return write.dstBinding == i;
                                                     }));
                    writer.bindingCount--;
                }
            }
            
            memcpy(set.descriptors, descriptors, sizeof(descriptors[0]) * count);
            set.count = count;
        }
        if(writer.bindingCount != 0) {
            writer.updateSet(device, set.handle);
        }
    }

    DescriptorSet DescriptorSetBuilder::build(const std::string& name, VkShaderStageFlags shaderStage) {
        std::lock_guard lck(cache.descriptorMutex);
        DescriptorSet& retval = cache.descriptors[name];
        retval.shaderStage = shaderStage;
        buildInternal(name, retval);
        return retval;
    }

    DescriptorSet DescriptorSetBuilder::buildPerFrame(const std::string& name, VkShaderStageFlags shaderStage) {
        std::lock_guard lck(cache.perFrameDescriptorMutex);
        const u32 frameIndex = cache.state->currentFrame;

        DescriptorSet& retval = cache.perFrameDescriptors[name][frameIndex];
        retval.shaderStage = shaderStage;
        buildInternal(name, retval);
        return retval;
    }

    void DescriptorSetBuilder::push(VkCommandBuffer commandBuffer, u32 setIndex, const kvk::Pipeline& pipeline) {
        if(writer.bindingCount != 0) {
            writer.push(commandBuffer, setIndex, pipeline);
            writer.clear();
        }
    }


    RenderPassBuilder::RenderPassBuilder():
        combinedDepthStencil(false),
        hasDepth(false),
        hasStencil(false) {
    }

    RenderPassBuilder& RenderPassBuilder::addColorAttachment(VkImageView view,
                                                             VkAttachmentLoadOp loadOp,
                                                             glm::vec4 clearColor,
                                                             VkAttachmentStoreOp storeOp,
                                                             VkImageLayout imageLayout) {
        VkRenderingAttachmentInfo& colorAttachment = colorAttachments.emplace_back();
        colorAttachment = VkRenderingAttachmentInfo {
            .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .imageView = view,
            .imageLayout = imageLayout,
            .loadOp = loadOp,
            .storeOp = storeOp,
            .clearValue = {
                .color = {
                    clearColor[0],
                    clearColor[1],
                    clearColor[2],
                    clearColor[3],
                },
            },
        };
        return *this;
    }

    RenderPassBuilder& RenderPassBuilder::addColorAttachment(VkImageView view,
                                                             VkAttachmentLoadOp loadOp,
                                                             glm::uvec4 clearValues,
                                                             VkAttachmentStoreOp storeOp,
                                                             VkImageLayout imageLayout) {
        VkRenderingAttachmentInfo& colorAttachment = colorAttachments.emplace_back();
        colorAttachment = VkRenderingAttachmentInfo {
            .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
                .imageView = view,
                .imageLayout = imageLayout,
                .loadOp = loadOp,
                .storeOp = storeOp,
                .clearValue = {
                    .color = {
                        .uint32 = {
                            clearValues[0],
                            clearValues[1],
                            clearValues[2],
                            clearValues[3],
                        },
                    },
                },
        };
        return *this;
    }

    RenderPassBuilder& RenderPassBuilder::setDepthAttachment(VkImageView view,
                                                             bool combinedDepthStencil,
                                                             VkAttachmentLoadOp loadOp,
                                                             float depthClear,
                                                             std::uint32_t stencil,
                                                             VkAttachmentStoreOp storeOp,
                                                             VkImageLayout imageLayout) {
        depthAttachment = VkRenderingAttachmentInfo {
            .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .imageView = view,
            .imageLayout = imageLayout,
            .loadOp = loadOp,
            .storeOp = storeOp,
            .clearValue = {
                .depthStencil = {
                    .depth = depthClear,
                    .stencil = stencil,
                },
            },
        };
        if(combinedDepthStencil) {
            this->combinedDepthStencil = true;
            this->hasStencil = true;
        }
        this->hasDepth = true;
        return *this;
    }

    RenderPassBuilder& RenderPassBuilder::setStencilAttachment(VkImageView view,
                                                               VkAttachmentLoadOp loadOp,
                                                               std::uint32_t stencil,
                                                               VkAttachmentStoreOp storeOp,
                                                               VkImageLayout imageLayout) {
        depthAttachment = VkRenderingAttachmentInfo {
            .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .imageView = view,
            .imageLayout = imageLayout,
            .loadOp = loadOp,
            .storeOp = storeOp,
            .clearValue = {
                .depthStencil = {
                    .stencil = stencil,
                },
            },
        };
        this->hasStencil = true;
        this->combinedDepthStencil = false;
        return *this;
    }

    void RenderPassBuilder::cmdBeginRendering(VkCommandBuffer cmd,
                                              VkExtent2D extent,
                                              VkOffset2D offset,
                                              std::uint32_t layerCount) {
        KAMSKI_PROFILE();
        VkRenderingInfo info = {
            .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
            .renderArea = {
                offset,
                extent,
            },
            .layerCount = layerCount,
            .colorAttachmentCount = std::uint32_t(colorAttachments.size()),
            .pColorAttachments = colorAttachments.data(),
            .pDepthAttachment = hasDepth ? &depthAttachment : nullptr,
        };
        if(hasStencil) {
            if(combinedDepthStencil) {
                info.pStencilAttachment = &depthAttachment;
            } else {
                info.pStencilAttachment = &stencilAttachment;
            }
        }
        vkCmdBeginRendering(cmd, &info);
    }

    ReturnCode createQueue(Queue& queue,
                           RendererState& state,
                           const VkQueueFlags flags,
                           const std::uint32_t queueFamilyIndex,
                           bool hasSecondaryQueue) {
        KAMSKI_PROFILE();
        vkGetDeviceQueue(state.device, queueFamilyIndex, 0, &queue.handle);
        logInfo("Queue 0x%llx, flags: %u", (std::uint64_t)queue.handle, flags);
        if(hasSecondaryQueue) {
            vkGetDeviceQueue(state.device, queueFamilyIndex, 1, &queue.secondaryHandle);
            logInfo("Queue 0x%llx, flags: %u", (std::uint64_t)queue.secondaryHandle, flags);
        }

        queue.familyIndex = queueFamilyIndex;

        VkCommandPoolCreateInfo commandPoolCreateInfo = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
            .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
            .queueFamilyIndex = queueFamilyIndex,
        };
        const std::uint32_t coreCount = std::thread::hardware_concurrency();
        queue.freePoolCount = coreCount;
        queue.isSlotOccupied.resize(coreCount, false);
        queue.pools.resize(coreCount);
        queue.commandBuffers.resize(coreCount);
        queue.fences.resize(coreCount);

        for(VkCommandPool& pool : queue.pools) {
            if(vkCreateCommandPool(state.device,
                                   &commandPoolCreateInfo,
                                   nullptr,
                                   &pool) != VK_SUCCESS) {
                logError("Could not create command pool");
                return ReturnCode::UNKNOWN;
            }
        }

        VkCommandBufferAllocateInfo allocInfo = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        };
        for(std::uint32_t i = 0; i != coreCount; i++) {
            allocInfo.commandPool = queue.pools[i];
            allocInfo.commandBufferCount = 1;

            if(vkAllocateCommandBuffers(state.device,
                                        &allocInfo,
                                        &queue.commandBuffers[i]) != VK_SUCCESS) {
                logError("Could not allocate cbuffers");
                return ReturnCode::UNKNOWN;
            }
        }

        VkFenceCreateInfo fenceCreateInfo = {
            .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        };
        for(VkFence& fence : queue.fences) {
            if(vkCreateFence(state.device, &fenceCreateInfo, nullptr, &fence) != VK_SUCCESS) {
                return ReturnCode::UNKNOWN;
            }
        }

        return ReturnCode::OK;
    }

    PoolInfo lockCommandPool(RendererState& state, VkQueueFlags desiredQueueFlags) {
        KAMSKI_PROFILE();
        //std::uint32_t familyIndex = 0;
        //std::uint32_t bestScore = std::numeric_limits<std::uint32_t>::max();
        //const std::uint32_t maxScore = std::popcount(desiredQueueFlags);

        //for(std::uint32_t qIndex = 0; qIndex != state.queueCount; qIndex++) {
        //    if((state.queues[qIndex].flags & desiredQueueFlags) != desiredQueueFlags) {
        //        continue;
        //    }
        //    // popcount counts the '1' bits
        //    const std::uint32_t score = maxScore - std::popcount(state.queues[qIndex].flags ^ desiredQueueFlags);
        //    if(score < bestScore) {
        //        bestScore = score;
        //        familyIndex = qIndex;
        //    }
        //}
        //assert(bestScore != std::numeric_limits<std::uint32_t>::max());
        std::uint32_t familyIndex;
        if(desiredQueueFlags == VK_QUEUE_TRANSFER_BIT) {
            familyIndex = 1;
        } else {
            familyIndex = 0;
        }
        Queue& queue = state.queues[familyIndex];

        std::unique_lock lck(queue.poolMutex);
        if(queue.freePoolCount == 0) {
            queue.poolCvar.wait(lck, [&]() { return queue.freePoolCount != 0; });
        }
        for(std::uint32_t slotIndex = 0; slotIndex != queue.isSlotOccupied.size(); slotIndex++) {
            if(!queue.isSlotOccupied[slotIndex]) {
                queue.isSlotOccupied[slotIndex] = true;
                queue.freePoolCount--;
                //logInfo("Qfam: %u, index: %u", queue.familyIndex, slotIndex);
                return {&queue, slotIndex};
            }
        }
        assert(false);
        return {};
    }

    void unlockCommandPool(RendererState& state, PoolInfo& poolInfo) {
        KAMSKI_PROFILE();
        std::lock_guard lck(poolInfo.queue->poolMutex);
        assert(poolInfo.queue->isSlotOccupied.size() > poolInfo.poolIndex);
        assert(poolInfo.queue->isSlotOccupied[poolInfo.poolIndex]);
        assert(poolInfo.queue->freePoolCount != std::thread::hardware_concurrency());
        poolInfo.queue->isSlotOccupied[poolInfo.poolIndex] = false;
        poolInfo.queue->freePoolCount++;
        poolInfo.queue->poolCvar.notify_one();
    }
}
