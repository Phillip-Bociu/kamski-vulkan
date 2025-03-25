#include "glm/geometric.hpp"
#include "glm/packing.hpp"
#include "glm/trigonometric.hpp"
#include "vulkan/vulkan_core.h"
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

#include <fastgltf/glm_element_traits.hpp>
#include <fastgltf/core.hpp>
#include <fastgltf/tools.hpp>


#if defined(_WIN32)
#include "krender_win32.h"
#endif

#include <GLFW/glfw3.h>

namespace kvk {
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

	ReturnCode createShaderModuleFromMemory(VkShaderModule& shaderModule,
                                            RendererState& state,
											const std::uint32_t* shaderContents,
											const std::uint64_t shaderSize) {
		VkShaderModuleCreateInfo createInfo = {
			.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
			.codeSize = shaderSize,
			.pCode = shaderContents,
		};

		if(vkCreateShaderModule(state.device,
								&createInfo,
								nullptr,
								&shaderModule) != VK_SUCCESS) {
			logError("Could not create shader module");
			return ReturnCode::UNKNOWN;
		}
		return ReturnCode::OK;
	}

	ReturnCode createShaderModuleFromFile(VkShaderModule& shaderModule,
										  RendererState& state,
										  const char* shaderPath) {
		std::ifstream vs(shaderPath, std::ios::ate | std::ios::binary);
		if(!vs.is_open()) {
			logError("File %s not found", shaderPath);
			return ReturnCode::FILE_NOT_FOUND;
		}

		const std::uint64_t size = vs.tellg();
		std::vector<std::uint32_t> vsData(size / 4);
		vs.seekg(0);
		vs.read((char*)vsData.data(), size);

		return createShaderModuleFromMemory(shaderModule,
											state,
											vsData.data(),
											size);
	}

	ReturnCode init(RendererState& state, const InitSettings* settings) {
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
			.apiVersion = VK_API_VERSION_1_3
		};

		state.currentFrame = 0;

		/*=====================================
				Validation layer handling
		  =====================================*/
#ifdef KVK_DEBUG
		const char* desiredLayers[] = {
		"VK_LAYER_KHRONOS_validation",
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

#ifdef KVK_DEBUG
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
#if defined(KVK_DEBUG)
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

#ifdef KVK_DEBUG
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

				std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
				vkGetPhysicalDeviceQueueFamilyProperties(pd, &queueFamilyCount, queueFamilies.data());

				std::uint32_t i = 0;
				for(const VkQueueFamilyProperties& qf : queueFamilies) {
					if(!graphicsFamilyFound && qf.queueFlags & VK_QUEUE_GRAPHICS_BIT) {
						state.graphicsFamilyIndex = i;
						graphicsFamilyFound = true;
					}

					if(!transferFamilyFound && qf.queueFlags & VK_QUEUE_TRANSFER_BIT) {
						state.transferFamilyIndex = i;
						transferFamilyFound = true;
					}

					if(!presentFamilyFound) {
						VkBool32 presentSupport = false;
						vkGetPhysicalDeviceSurfaceSupportKHR(pd, i, state.surface, &presentSupport);

						if(presentSupport) {
							state.presentFamilyIndex = i;
							presentFamilyFound = true;
						}
					}

					if(!computeFamilyFound && qf.queueFlags & VK_QUEUE_COMPUTE_BIT) {
						state.computeFamilyIndex = i;
						computeFamilyFound = true;
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

				state.physicalDevice = pd;
				break;
			}
		}

		if(state.physicalDevice == VK_NULL_HANDLE) {
			logError("No supported GPUs found");
			return ReturnCode::DEVICE_NOT_FOUND;
		}


		/*=====================================
				Logical device creation
		  =====================================*/
		state.device = VK_NULL_HANDLE;

		const std::set<std::uint32_t> uniqueQueueFamilies = {
			state.graphicsFamilyIndex,
			state.presentFamilyIndex,
			state.computeFamilyIndex
		};
		std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
		queueCreateInfos.reserve(uniqueQueueFamilies.size());

		float queuePriority = 1.0f;
		for(std::uint32_t qFam : uniqueQueueFamilies) {
			VkDeviceQueueCreateInfo queueCreateInfo = {
				.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
				.queueFamilyIndex = qFam,
				.queueCount = 1,
				.pQueuePriorities = &queuePriority,
			};

			queueCreateInfos.push_back(queueCreateInfo);
		}

		VkPhysicalDeviceVulkan13Features features13 = {
			.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
		};
		VkPhysicalDeviceVulkan12Features features12 = {
			.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
			.pNext = &features13,
		};

		VkPhysicalDeviceFeatures2 allDeviceFeatures = {
			.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
			.pNext = &features12,
		};

		vkGetPhysicalDeviceFeatures2(state.physicalDevice,
									 &allDeviceFeatures);

#define CHECK_FEATURE(revision, feature)\
		if(!revision.feature) {\
			logInfo(#feature " is not available");\
			return ReturnCode::UNKNOWN; \
		}
		CHECK_FEATURE(features13, synchronization2);
		CHECK_FEATURE(features13, dynamicRendering);
		CHECK_FEATURE(features12, bufferDeviceAddress);
#undef CHECK_FEATURE

		features13 = VkPhysicalDeviceVulkan13Features {
			.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
			.synchronization2 = VK_TRUE,
			.dynamicRendering = VK_TRUE
		};

		features12 = VkPhysicalDeviceVulkan12Features {
			.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
			.pNext = &features13,
			.bufferDeviceAddress = VK_TRUE,
		};

		allDeviceFeatures = VkPhysicalDeviceFeatures2 {
			.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
			.pNext = &features12,
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

		vkGetDeviceQueue(state.device,
						 state.transferFamilyIndex,
						 0,
						 &state.transferQueue);

		vkGetDeviceQueue(state.device,
						 state.graphicsFamilyIndex,
						 0,
						 &state.graphicsQueue);

		vkGetDeviceQueue(state.device,
						 state.presentFamilyIndex,
						 0,
						 &state.presentQueue);

		vkGetDeviceQueue(state.device,
						 state.computeFamilyIndex,
						 0,
						 &state.computeQueue);

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
			if (pm == VK_PRESENT_MODE_FIFO_KHR) {
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
			DescriptorAllocator::PoolSizeRatio{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 3 },
			DescriptorAllocator::PoolSizeRatio{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 3 },
			DescriptorAllocator::PoolSizeRatio{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 3 },
			DescriptorAllocator::PoolSizeRatio{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4 },
		};

		for(int frameIndex = 0; frameIndex != MAX_IN_FLIGHT_FRAMES; frameIndex++) {
			state.frames[frameIndex].descriptors.init(state.device,
													  1000,
													  ratios);
		}

		VkDescriptorSetLayoutBinding bindings[] = {
            {
				.binding = 0,
				.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
				.descriptorCount = 1,
            },
		};

		if(createDescriptorSetLayout(state.sceneDescriptorLayout,
		                             state.device,
                                     VK_SHADER_STAGE_VERTEX_BIT,
                                     bindings) != ReturnCode::OK) {
            logError("Could not create descriptor layout");
            return ReturnCode::UNKNOWN;
        }

        bindings[0] =  {
			.binding = 0,
			.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			.descriptorCount = 1,
        };

        if(createDescriptorSetLayout(state.samplerDescriptorLayout,
		                             state.device,
                                     VK_SHADER_STAGE_FRAGMENT_BIT,
                                     bindings) != ReturnCode::OK) {
            logError("Could not create sampler descriptor layout");
            return ReturnCode::UNKNOWN;
        }

		if(createSwapchain(state,
						   chosenExtent,
						   chosenFormat,
						   chosenPresentMode,
						   imageCount) != ReturnCode::OK) {
			logError("Could not create swapchain");
			return ReturnCode::UNKNOWN;
		}
		logInfo("Created swapchain");

		VkCommandPoolCreateInfo commandPoolCreateInfo = {
			.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
			.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
			.queueFamilyIndex = state.graphicsFamilyIndex,
		};
		if(vkCreateCommandPool(state.device,
							   &commandPoolCreateInfo,
							   nullptr,
							   &state.commandPool) != VK_SUCCESS) {
			logError("Could not create command pool");
			return ReturnCode::UNKNOWN;
		}
		logInfo("Created command pool");

		VkCommandBufferAllocateInfo cbufferAllocInfo = {
			.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
			.commandPool = state.commandPool,
			.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
			.commandBufferCount = MAX_IN_FLIGHT_FRAMES + 4
		};
		VkCommandBuffer cBuffers[MAX_IN_FLIGHT_FRAMES + 4];

		if(vkAllocateCommandBuffers(state.device,
									&cbufferAllocInfo,
									cBuffers) != VK_SUCCESS) {
			logError("Could not allocate cbuffers");
			return ReturnCode::UNKNOWN;
		}

		for(int i = 0; i != MAX_IN_FLIGHT_FRAMES; i++) {
			state.frames[i].commandBuffer = cBuffers[i];
		}

		for(int i = MAX_IN_FLIGHT_FRAMES; i != MAX_IN_FLIGHT_FRAMES + 4; i++) {
			state.transferCommandBuffers[i - MAX_IN_FLIGHT_FRAMES] = cBuffers[i];
		}

		VkSemaphoreCreateInfo semaphoreCreateInfo = {
			.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
		};

		VkFenceCreateInfo fenceCreateInfo = {
			.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
			.flags = VK_FENCE_CREATE_SIGNALED_BIT,
		};

		for(int i = 0; i != MAX_IN_FLIGHT_FRAMES; i++) {
			if(vkCreateSemaphore(state.device, &semaphoreCreateInfo, nullptr, &state.frames[i].imageAvailableSemaphore) != VK_SUCCESS ||
			   vkCreateSemaphore(state.device, &semaphoreCreateInfo, nullptr, &state.frames[i].renderFinishedSemaphore) != VK_SUCCESS ||
			   vkCreateFence	(state.device, &fenceCreateInfo    , nullptr, &state.frames[i].inFlightFence)			  != VK_SUCCESS) {
				logError("Could not create sync objects");
				return ReturnCode::UNKNOWN;
			}
		}

        const uint32_t magenta = glm::packUnorm4x8(glm::vec4(1, 0, 1, 1));
        const uint32_t black = glm::packUnorm4x8(glm::vec4(0, 0, 0, 1));
        const uint32_t white = glm::packUnorm4x8(glm::vec4(1, 1, 1, 1));
        std::uint32_t pixels[16 * 16];
        for(std::uint32_t y = 0; y != 16; y++) {
            for(std::uint32_t x = 0; x != 16; x++) {
                pixels[y * 16 + x] = ((x & 1) ^ (y & 1)) ? magenta : black;
                //pixels[y * 16 + x] = white;
            }
        }

        rc = createImage(state.errorTexture,
                         state,
                         pixels,
                         VK_FORMAT_R8G8B8A8_UNORM,
                         VkExtent3D{16, 16, 1},
                         VK_IMAGE_USAGE_SAMPLED_BIT |
                         VK_IMAGE_USAGE_TRANSFER_DST_BIT);
        if(rc != ReturnCode::OK) {
            logError("Could not create the error texture");
            return rc;
        }

        VkSamplerCreateInfo samplerCreateInfo {
            .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
            .magFilter = VK_FILTER_NEAREST,
            .minFilter = VK_FILTER_NEAREST,
        };
        VK_CHECK(vkCreateSampler(state.device, &samplerCreateInfo, nullptr, &state.sampler));

        VK_CHECK(immediateSubmit(state.transferCommandBuffers[0],
	                    state.device,
						state.transferQueue,
						[&](VkCommandBuffer cmd) {
			transitionImage(cmd,
							state.depthImage.image,
							VK_IMAGE_LAYOUT_UNDEFINED,
							VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
		}));

		return ReturnCode::OK;
	}

	ReturnCode drawScene(FrameData& frame,
					     RendererState& state,
					     const VkExtent2D& extent,
					     const Pipeline& meshPipeline,
					     const std::vector<MeshAsset>& meshes) {
		vkResetCommandBuffer(frame.commandBuffer, 0);
		VkCommandBufferBeginInfo beginInfo = {
			.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		};

		if(vkBeginCommandBuffer(frame.commandBuffer, &beginInfo) != VK_SUCCESS) {
			logError("Could not start command buffer recording");
			return ReturnCode::UNKNOWN;
		}

		transitionImage(frame.commandBuffer,
						state.drawImage.image,
						VK_IMAGE_LAYOUT_UNDEFINED,
						VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

		VkRenderingAttachmentInfo colorAttachment = {
			.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
			.imageView = state.drawImage.view,
			.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
			.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
			.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
			.clearValue =  {
				.color = { 0.0f, 1.0f, 0.0f, 1.0f },
			},
		};

		VkRenderingAttachmentInfo depthAttachment = {
			.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
			.imageView = state.depthImage.view,
			.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
			.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
			.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
			.clearValue =  {
				.depthStencil =  {
					.depth = 0.0f,
					.stencil = 0
				},
			},
		};

		VkRenderingInfo renderInfo = {
			.sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
			.renderArea = {
				VkOffset2D {0, 0},
				extent
			},
			.layerCount = 1,
			.colorAttachmentCount = 1,
			.pColorAttachments = &colorAttachment,
			.pDepthAttachment = &depthAttachment,
			.pStencilAttachment = &depthAttachment,
		};

		AllocatedBuffer sceneDataBuffer;
		ReturnCode rc = createBuffer(sceneDataBuffer,
	                                 state.allocator,
	                                 sizeof(SceneData),
	                                 VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
	                                 VMA_MEMORY_USAGE_CPU_TO_GPU);
		if(rc != ReturnCode::OK) {
            logError("Could not create sceneDataBuffer");
            return rc;
		}
		frame.deletionQueue.push_back([sceneDataBuffer, allocator=state.allocator]() mutable {
		    destroyBuffer(sceneDataBuffer, allocator);
		});

		auto makeInfReversedZProjRH = [](float fovY_radians, float aspectWbyH, float zNear) -> glm::mat4 {
			float f = 1.0f / tan(fovY_radians / 2.0f);
			return glm::mat4(
				f / aspectWbyH, 0.0f,  0.0f,  0.0f,
				0.0f,    -f,  0.0f,  0.0f,
				0.0f, 0.0f,  0.0f, -1.0f,
				0.0f, 0.0f, zNear,  0.0f
				);
		};

		static glm::mat4 view = glm::translate(glm::mat4(1.0f), glm::vec3{ 0,0, -5});
		static float radians = 0.0f;
		radians += 1.0f;
		glm::mat4 model = glm::rotate(glm::mat4(1.0f), glm::radians(radians), glm::vec3(0.0f, 1.0f, 0.0f));
		SceneData sData = {
    		.view = view,
    		.proj = makeInfReversedZProjRH(glm::radians(45.0f), float(extent.width) / float(extent.height), 0.1f),
    		.viewproj = sData.proj * sData.view,
    		.ambientColor = {1.0f, 1.0f, 1.0f, 0.2f},
    		.sunlightDirection = glm::normalize(glm::vec4{1.0f, -1.0f, 0.0f, 0.0f}),
    		.sunlightColor = {0.0f, 1.0f, 0.0f, 1.0f},
		};
		sData.sunlightDirection.w = 1.0f;
		auto sceneData = (SceneData*)sceneDataBuffer.allocation->GetMappedData();
		*sceneData = sData;

		VkDescriptorSet globalDescriptor;
		rc = frame.descriptors.alloc(globalDescriptor, state.device, state.sceneDescriptorLayout);
		if(rc != ReturnCode::OK) {
		    logError("could not allocate globalDescriptor");
			return rc;
		}

		DescriptorWriter writer;
		writer.writeBuffer(0, sceneDataBuffer.buffer, sizeof(SceneData), 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
		writer.updateSet(state.device, globalDescriptor);
		writer.clear();

		VkDescriptorSet imageDescriptor;
		rc = frame.descriptors.alloc(imageDescriptor, state.device, state.samplerDescriptorLayout);
		if(rc != ReturnCode::OK) {
		    logError("could not allocate globalDescriptor");
			return rc;
		}
		writer.writeImage(0, state.errorTexture.view, state.sampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
		writer.updateSet(state.device, imageDescriptor);
		writer.clear();

		vkCmdBeginRendering(frame.commandBuffer,
							&renderInfo);

		vkCmdBindPipeline(frame.commandBuffer,
						  VK_PIPELINE_BIND_POINT_GRAPHICS,
						  meshPipeline.pipeline);

		VkDescriptorSet sets[] = {
			globalDescriptor,
            imageDescriptor
		};
		vkCmdBindDescriptorSets(frame.commandBuffer,
	                            VK_PIPELINE_BIND_POINT_GRAPHICS,
	                            meshPipeline.layout,
	                            0,
	                            2,
	                            sets,
	                            0,
	                            nullptr);

		VkViewport viewport = {
			.x = 0,
			.y = 0,
			.width = static_cast<float>(extent.width),
			.height = static_cast<float>(extent.height),
			.minDepth = 0.0f,
			.maxDepth = 1.0f,
		};

		vkCmdSetViewport(frame.commandBuffer, 0, 1, &viewport);
		VkRect2D scissor = {
			.offset = {
				.x = 0,
				.y = 0
			},
			.extent = extent,
		};

		vkCmdSetScissor(frame.commandBuffer, 0, 1, &scissor);
		// camera projection
		PushConstants pc;
		pc.scaling = model;
		for(const MeshAsset& asset : meshes) {
		    pc.vertexBuffer =  asset.mesh.vertexBufferAddress;
			vkCmdPushConstants(frame.commandBuffer,
							   meshPipeline.layout,
							   VK_SHADER_STAGE_VERTEX_BIT,
							   0,
							   sizeof(pc),
							   &pc);
			vkCmdBindIndexBuffer(frame.commandBuffer,
								 asset.mesh.indices.buffer,
								 0,
								 VK_INDEX_TYPE_UINT32);

			for(const GeoSurface& surface : asset.surfaces) {
				vkCmdDrawIndexed(frame.commandBuffer,
								 surface.count,
								 1,
								 surface.startIndex,
								 0,
								 0);
			}
		}
		vkCmdEndRendering(frame.commandBuffer);

		transitionImage(frame.commandBuffer,
						state.swapchainImages[frame.swapchainImageIndex],
						VK_IMAGE_LAYOUT_UNDEFINED,
						VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

		transitionImage(frame.commandBuffer,
						state.drawImage.image,
						VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
						VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

		blitImageToImage(frame.commandBuffer,
 						 state.drawImage.image,
                         state.swapchainImages[frame.swapchainImageIndex],
 						 extent,
 						 extent);

		transitionImage(frame.commandBuffer,
		                state.swapchainImages[frame.swapchainImageIndex],
						VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
						VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);


		if(vkEndCommandBuffer(frame.commandBuffer) != VK_SUCCESS) {
			logError("Could not end command buffer");
			return ReturnCode::UNKNOWN;
		}
		return ReturnCode::OK;
	}

	ReturnCode createSwapchain(RendererState& state,
							   VkExtent2D extent,
							   VkSurfaceFormatKHR format,
							   VkPresentModeKHR presentMode,
							   std::uint32_t imageCount,
							   VkSwapchainKHR oldSwapchain) {
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

		VkExtent3D drawImageExtent = {
			extent.width,
			extent.height,
			1
		};

		ReturnCode rc;

		rc = createImage(state.depthImage,
						 state,
						 VK_FORMAT_D24_UNORM_S8_UINT,
						 drawImageExtent,
						 VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT);

		if(rc != ReturnCode::OK) {
			logError("Could not create depth image");
			return rc;
		}

		rc = createImage(state.drawImage,
						 state,
						 VK_FORMAT_R16G16B16A16_SFLOAT,
						 drawImageExtent,
						 VK_IMAGE_USAGE_TRANSFER_DST_BIT |
						 VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
						 VK_IMAGE_USAGE_STORAGE_BIT |
						 VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);
		if(rc != ReturnCode::OK) {
			logError("Could not create draw image");
			return rc;
		}
		return ReturnCode::OK;
	}

	ReturnCode recreateSwapchain(RendererState& state,
								 const std::uint32_t x,
								 const std::uint32_t y) {
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

		vkDestroyImageView(state.device,
						   state.drawImage.view,
						   nullptr);
		vmaDestroyImage(state.allocator,
						state.drawImage.image,
						state.drawImage.allocation);

		vkDestroyImageView(state.device,
						   state.depthImage.view,
						   nullptr);
		vmaDestroyImage(state.allocator,
						state.depthImage.image,
						state.depthImage.allocation);

		ReturnCode rc = createSwapchain(state,
										chosenExtent,
										state.swapchainImageFormat,
										state.swapchainPresentMode,
										state.swapchainImageCount,
										oldSwapchain);
		vkDestroySwapchainKHR(state.device,
							  oldSwapchain,
							  nullptr);

		VK_CHECK(immediateSubmit(state.transferCommandBuffers[0],
		                         state.device,
		                         state.transferQueue,
		                         [&](VkCommandBuffer cmd) {
    		                         transitionImage(cmd,
        		                                     state.depthImage.image,
        		                                     VK_IMAGE_LAYOUT_UNDEFINED,
        		                                     VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
		                         }));
		return rc;
	}

	PipelineBuilder::PipelineBuilder() {
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
			.cullMode = VK_CULL_MODE_FRONT_BIT,
			.frontFace = VK_FRONT_FACE_CLOCKWISE,
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
			.colorAttachmentCount = 1,
			.pColorAttachmentFormats = &colorAttachmentFormat,
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


	PipelineBuilder& PipelineBuilder::addPushConstantRange(VkShaderStageFlags stage, std::uint32_t size, std::uint32_t offset) {
		pushConstantRanges.emplace_back(stage, offset, size);
		return *this;
	}


	PipelineBuilder& PipelineBuilder::setShaders(VkShaderModule vertexShader, VkShaderModule fragmentShader) {
		shaderStages.clear();
		VkPipelineShaderStageCreateInfo vs = {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
			.stage = VK_SHADER_STAGE_VERTEX_BIT,
			.module = vertexShader,
			.pName = "main",
		};

		VkPipelineShaderStageCreateInfo fs = {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
			.stage = VK_SHADER_STAGE_FRAGMENT_BIT,
			.module = fragmentShader,
			.pName = "main",
		};

		shaderStages.push_back(vs);
		shaderStages.push_back(fs);

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

	PipelineBuilder& PipelineBuilder::setColorAttachmentFormat(VkFormat format) {
		colorAttachmentFormat = format;
		return *this;
	}

	PipelineBuilder& PipelineBuilder::setDepthAttachmentFormat(VkFormat format) {
		renderInfo.depthAttachmentFormat = format;
		renderInfo.stencilAttachmentFormat = format;
		return *this;
	}

	PipelineBuilder& PipelineBuilder::setStencilAttachmentFormat(VkFormat format) {
		renderInfo.stencilAttachmentFormat = format;
		return *this;
	}

	PipelineBuilder& PipelineBuilder::addDescriptorSetLayout(VkDescriptorSetLayout layout) {
	    descriptorSetLayouts.push_back(layout);
	    return *this;
	}

	ReturnCode PipelineBuilder::build(Pipeline& pipeline,
									  const VkDevice device) {
		VkPipelineLayoutCreateInfo layoutCreateInfo = {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
			.setLayoutCount = static_cast<std::uint32_t>(descriptorSetLayouts.size()),
			.pSetLayouts = descriptorSetLayouts.data(),
			.pushConstantRangeCount = static_cast<std::uint32_t>(pushConstantRanges.size()),
			.pPushConstantRanges = pushConstantRanges.data(),
		};

		if(vkCreatePipelineLayout(device,
								  &layoutCreateInfo,
								  nullptr,
								  &pipeline.layout) != VK_SUCCESS) {
			logError("Could not create pipeline layout");
			return ReturnCode::UNKNOWN;
		}
		VkPipelineDynamicStateCreateInfo dynamicStateInfo = {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
			.dynamicStateCount = static_cast<std::uint32_t>(dynamicState.size()),
			.pDynamicStates = dynamicState.data(),
		};

		VkGraphicsPipelineCreateInfo createInfo = {
			.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
			.pNext = &renderInfo,
			.stageCount = static_cast<std::uint32_t>(shaderStages.size()),
			.pStages = shaderStages.data(),
			.pVertexInputState = &inputState,
			.pInputAssemblyState = &inputAssembly,
			.pViewportState = &viewportState,
			.pRasterizationState = &rasterizer,
			.pMultisampleState = &multisample,
			.pDepthStencilState = &depthStencil,
			.pColorBlendState = &blendState,
			.pDynamicState = &dynamicStateInfo,
			.layout = pipeline.layout
		};

		if(vkCreateGraphicsPipelines(device,
									 VK_NULL_HANDLE,
									 1,
									 &createInfo,
									 nullptr,
									 &pipeline.pipeline) != VK_SUCCESS) {
			logError("Could not create graphics pipeline");
			return ReturnCode::UNKNOWN;
		}

		logDebug("Created pipeline");
		return ReturnCode::OK;
	}

	ReturnCode createBuffer(AllocatedBuffer& buffer,
							VmaAllocator allocator,
							std::uint64_t size,
							VkBufferUsageFlags bufferUsage,
							VmaMemoryUsage memoryUsage) {
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
						   &buffer.info) != VK_SUCCESS) {
			logError("Could not allocate buffer");
			return ReturnCode::UNKNOWN;
		}
		return ReturnCode::OK;
	}

	void destroyBuffer(AllocatedBuffer& buffer,
					   VmaAllocator allocator) {
		vmaDestroyBuffer(allocator,
						 buffer.buffer,
						 buffer.allocation);
	}

	ReturnCode createMesh(Mesh& mesh,
						  RendererState& state,
						  std::span<std::uint32_t> indices,
						  std::span<Vertex> vertices) {
		const std::uint64_t vertexBufferSize = vertices.size() * sizeof(Vertex);
		const std::uint64_t indexBufferSize = indices.size() * sizeof(std::uint32_t);
		ReturnCode rc;

		rc = createBuffer(mesh.vertices,
						  state.allocator,
						  vertexBufferSize,
						  VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
						  VK_BUFFER_USAGE_TRANSFER_DST_BIT |
						  VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
						  VMA_MEMORY_USAGE_GPU_ONLY);

		if(rc != ReturnCode::OK) {
			return rc;
		}

		VkBufferDeviceAddressInfo deviceAddressInfo = {
			.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
			.buffer = mesh.vertices.buffer
		};

		mesh.vertexBufferAddress = vkGetBufferDeviceAddress(state.device,
															&deviceAddressInfo);

		rc = createBuffer(mesh.indices,
						  state.allocator,
						  indexBufferSize,
						  VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
						  VK_BUFFER_USAGE_TRANSFER_DST_BIT,
						  VMA_MEMORY_USAGE_GPU_ONLY);

		if(rc != ReturnCode::OK) {
			return rc;
		}

		AllocatedBuffer stagingBuffer;
		rc = createBuffer(stagingBuffer,
						  state.allocator,
						  vertexBufferSize + indexBufferSize,
						  VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
						  VMA_MEMORY_USAGE_CPU_ONLY);

		if(rc != ReturnCode::OK) {
			return rc;
		}

		void* data = stagingBuffer.allocation->GetMappedData();
		memcpy(data, vertices.data(), vertexBufferSize);
		memcpy((char*)data + vertexBufferSize, indices.data(), indexBufferSize);

		auto transferFunc = [&](VkCommandBuffer cmd) {
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

		VkResult res = immediateSubmit(state.transferCommandBuffers[0],
									   state.device,
									   state.transferQueue,
									   transferFunc);

		if(res != VK_SUCCESS) {
			logError("Immediate submit failed: %d", res);
			return ReturnCode::UNKNOWN;
		}

		destroyBuffer(stagingBuffer,
					  state.allocator);

		return ReturnCode::OK;
	}

	ReturnCode loadGltf(std::vector<MeshAsset>& retval,
						RendererState& state,
						const char* pth) {
		std::filesystem::path filePath = pth;
		auto data = fastgltf::GltfDataBuffer::FromPath(filePath);

		fastgltf::Asset gltf;
		fastgltf::Parser parser;

		auto load = parser.loadGltfBinary(data.get(),
										  filePath.parent_path(),
										  fastgltf::Options::LoadExternalBuffers);

		if(load) {
			gltf = std::move(load.get());
		} else {
			logError("Could not parse gltf file");
			return ReturnCode::UNKNOWN;
		}

		retval.reserve(gltf.meshes.size());

		std::vector<std::uint32_t> indices;
		std::vector<Vertex> vertices;

		for(fastgltf::Mesh& mesh : gltf.meshes) {
			retval.emplace_back();
			MeshAsset& newMesh = retval.back();

			indices.clear();
			vertices.clear();

			newMesh.surfaces.reserve(mesh.primitives.size());
			for(auto& p : mesh.primitives) {
				GeoSurface newSurface;
				newSurface.startIndex = indices.size();
				newSurface.count = gltf.accessors[p.indicesAccessor.value()].count;
				newMesh.surfaces.emplace_back(newSurface);

				std::uint32_t initialVertex = vertices.size();
				// load indices
				{
					fastgltf::Accessor& indexAccessor = gltf.accessors[p.indicesAccessor.value()];
					fastgltf::iterateAccessor<std::uint32_t>(gltf,
															 indexAccessor,
															 [&](size_t index) {
																 indices.push_back(static_cast<std::uint32_t>(index + initialVertex));
															 });
				}

				// load vertices
				{
					fastgltf::Accessor& vertexAccessor = gltf.accessors[p.findAttribute("POSITION")->accessorIndex];
					vertices.resize(vertexAccessor.count + vertices.size());
					fastgltf::iterateAccessorWithIndex<glm::vec3>(gltf,
																  vertexAccessor,
																  [&](glm::vec3 v, size_t index) {
																	  Vertex& vertex = vertices[index + initialVertex];
																	  vertex.position = v;
																	  vertex.normal = {1, 0, 0};
																	  vertex.color = glm::vec4{1.0f, 1.0f, 1.0f, 1.0f};
																	  vertex.uvX = 0.0f;
																	  vertex.uvY = 0.0f;
																  });
				}

				// load normals
				{
					fastgltf::Accessor& normalAccessor = gltf.accessors[p.findAttribute("NORMAL")->accessorIndex];
					fastgltf::iterateAccessorWithIndex<glm::vec3>(gltf,
																  normalAccessor,
																  [&](glm::vec3 v, size_t index) {
																	  vertices[index + initialVertex].normal = v;
																  });
				}

				// load colors
				{
					auto ind = p.findAttribute("COLOR_0");
					if(ind != p.attributes.end()) {
						auto normalAccessor = gltf.accessors[ind->accessorIndex];
						fastgltf::iterateAccessorWithIndex<glm::vec4>(gltf,
																	  normalAccessor,
																	  [&](glm::vec4 v, size_t index) {
																		  vertices[index + initialVertex].color = v;
																	  });
					}
				}

				// load UV
				{
					auto ind = p.findAttribute("TEXCOORD_0");
					if(ind != p.attributes.end()) {
						fastgltf::Accessor& normalAccessor = gltf.accessors[ind->accessorIndex];
						fastgltf::iterateAccessorWithIndex<glm::vec2>(gltf,
																	  normalAccessor,
																	  [&](glm::vec2 v, size_t index) {
																		  vertices[index + initialVertex].uvX = v.x;
																		  vertices[index + initialVertex].uvY = v.y;
																	  });
					}
				}
			}
			if constexpr(false) {
				for(Vertex & v : vertices) {
					v.color = glm::vec4(v.normal, 1.0f);
				}
			}
			ReturnCode rc = createMesh(newMesh.mesh,
									   state,
									   indices,
									   vertices);
			if(rc != ReturnCode::OK) {
				logError("could not create mesh");
				return rc;
			}
		}
		return ReturnCode::OK;
	}

	ReturnCode createImage(AllocatedImage& image,
						   RendererState& state,
						   const VkFormat format,
						   const VkExtent3D extent,
						   const VkImageUsageFlags usageFlags) {
		VkImageCreateInfo imageInfo = imageCreateInfo(state.physicalDevice,
															format,
															usageFlags,
															extent);

		VmaAllocationCreateInfo imageAllocInfo = {
			.usage = VMA_MEMORY_USAGE_GPU_ONLY,
			.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
		};

		vmaCreateImage(state.allocator,
					   &imageInfo,
					   &imageAllocInfo,
					   &image.image,
					   &image.allocation,
					   nullptr);
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
		VkImageViewCreateInfo imageViewInfo = imageViewCreateInfo(format,
																  image.image,
																  aspect);
		if(vkCreateImageView(state.device,
							 &imageViewInfo,
							 nullptr,
							 &image.view) != VK_SUCCESS) {
			logError("Could not create draw image");
			return ReturnCode::UNKNOWN;
		}
		image.format = format;
		image.extent = extent;

		return ReturnCode::OK;
	}

	ReturnCode createImage(AllocatedImage& image,
						   RendererState& state,
						   const void* data,
						   const VkFormat format,
						   const VkExtent3D extent,
						   const VkImageUsageFlags usageFlags) {
		const std::uint64_t size = extent.width * extent.height * extent.depth * 4;

		AllocatedBuffer stagingBuffer;
		ReturnCode rc = createBuffer(stagingBuffer,
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
						 usageFlags);
		if(rc != ReturnCode::OK) {
			logError("Could not create image");
			return rc;
		}

		auto transferFunc = [&](VkCommandBuffer cmd) {
			transitionImage(cmd,
							image.image,
							VK_IMAGE_LAYOUT_UNDEFINED,
							VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

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
			transitionImage(cmd,
							image.image,
							VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
							VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
		};

		VkResult res = immediateSubmit(state.transferCommandBuffers[0],
									   state.device,
									   state.transferQueue,
									   transferFunc);
		if(res != VK_SUCCESS) {
			logError("transfer failed: %d", res);
			return ReturnCode::UNKNOWN;
		}

		destroyBuffer(stagingBuffer,
					  state.allocator);
		return ReturnCode::OK;
	}

	void destroyImage(AllocatedImage& image,
					  VkDevice device,
					  VmaAllocator allocator) {
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
		ratios.clear();
		VkDescriptorPool newPool = createPool(device, initialSets, poolRatios);
		setsPerPool = initialSets * 1.5f;
		readyPools.push_back(newPool);
	}

	VkDescriptorPool DescriptorAllocator::getPool(VkDevice device) {
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
			vkCreateDescriptorPool(device,
								   &createInfo,
								   nullptr,
								   &retval);
			return retval;
		}

		void DescriptorAllocator::clearPools(VkDevice device) {
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
			if(result == VK_ERROR_OUT_OF_POOL_MEMORY || result == VK_ERROR_FRAGMENTED_POOL) {
				fullPools.push_back(poolToUse);
				poolToUse = getPool(device);
				allocInfo.descriptorPool = poolToUse;

				VK_CHECK(vkAllocateDescriptorSets(device,
												  &allocInfo,
												  &set));
			}
			readyPools.push_back(poolToUse);
			return ReturnCode::OK;
		}


		void DescriptorWriter::writeBuffer(int binding,
										   VkBuffer buffer,
										   const std::uint64_t size,
										   const std::uint64_t offset,
										   VkDescriptorType type) {
			auto& info = bufferInfos.emplace_back(VkDescriptorBufferInfo{
				.buffer = buffer,
				.offset = offset,
				.range = size
			});

			VkWriteDescriptorSet write = {
				.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
				.dstSet = VK_NULL_HANDLE,
				.descriptorCount = 1,
				.descriptorType = type,
				.pBufferInfo = &info,
			};
			writes.push_back(write);
		}

		void DescriptorWriter::writeImage(int binding,
										  VkImageView view,
										  VkSampler sampler,
										  VkImageLayout layout,
										  VkDescriptorType type) {
			auto& info = imageInfos.emplace_back(VkDescriptorImageInfo{
				.sampler = sampler,
				.imageView = view,
				.imageLayout = layout,
			});

			VkWriteDescriptorSet write = {
				.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
				.dstSet = VK_NULL_HANDLE,
				.descriptorCount = 1,
				.descriptorType = type,
				.pImageInfo = &info,
			};
			writes.push_back(write);
		}

		void DescriptorWriter::clear() {
			imageInfos.clear();
			bufferInfos.clear();
			writes.clear();
		}

		void DescriptorWriter::updateSet(VkDevice device,
										 VkDescriptorSet set) {
			for(auto& write : writes) {
				write.dstSet = set;
			}

			vkUpdateDescriptorSets(device,
								   std::uint32_t(writes.size()),
								   writes.data(),
								   0,
								   nullptr);
		}

		FrameData* startFrame(RendererState& state) {
		    FrameData& frame = state.frames[state.currentFrame];
			vkWaitForFences(state.device,
							1,
							&frame.inFlightFence,
							VK_TRUE,
							std::numeric_limits<std::uint64_t>::max());
			std::uint32_t imageIndex;

			//
			// Flush the per-frame deletionQueue
			//
			for(auto iter = frame.deletionQueue.rbegin(); iter != frame.deletionQueue.rend(); ++iter) {
				(*iter)();
			}
			frame.deletionQueue.clear();
			frame.descriptors.clearPools(state.device);

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
			return &frame;
		}

		ReturnCode endFrame(RendererState& state, FrameData& frame) {
		    state.currentFrame = (state.currentFrame + 1) % MAX_IN_FLIGHT_FRAMES;
			VkSemaphore waitSemaphores[] = {
				frame.imageAvailableSemaphore
			};

			VkSemaphore signalSemaphores[] = {
				frame.renderFinishedSemaphore
			};
			VkPipelineStageFlags waitStages[] = {
				VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
			};

			VkSubmitInfo submitInfo = {
				.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
				.waitSemaphoreCount = 1,
				.pWaitSemaphores = waitSemaphores,
				.pWaitDstStageMask = waitStages,
				.commandBufferCount = 1,
				.pCommandBuffers = &frame.commandBuffer,
				.signalSemaphoreCount = 1,
				.pSignalSemaphores = signalSemaphores
			};

			if(vkQueueSubmit(state.graphicsQueue,
							 1,
							 &submitInfo,
							 frame.inFlightFence) != VK_SUCCESS) {
				logError("Queue submit failed");
				return ReturnCode::UNKNOWN;
			}

			VkPresentInfoKHR presentInfo = {
				.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
				.waitSemaphoreCount = 1,
				.pWaitSemaphores = signalSemaphores,
				.swapchainCount = 1,
				.pSwapchains = &state.swapchain,
				.pImageIndices = &frame.swapchainImageIndex,
				.pResults = nullptr
			};

			VkResult result = vkQueuePresentKHR(state.presentQueue, &presentInfo);
			if (result == VK_ERROR_OUT_OF_DATE_KHR) {
			    return ReturnCode::UNKNOWN;
			} else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
			    logError("Very wrong");
				return ReturnCode::UNKNOWN;
			}
			return ReturnCode::OK;
		}
}
