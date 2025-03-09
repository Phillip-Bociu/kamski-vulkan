#include "common.h"
#include "renderer.h"
#include "utils.h"

#include <fstream>
#include <vector>
#include <set>
#include <numeric>
#include <algorithm>
#include <bitset>
#include <iostream>

#if defined(_WIN32)
#include "renderer_win32.h"
#endif

namespace kvk {

	static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
		VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
		VkDebugUtilsMessageTypeFlagsEXT messageType,
		const VkDebugUtilsMessengerCallbackDataEXT *pCallbackData,
		void *pUserData) {
		logError("Validation layer: %s", pCallbackData->pMessage);

		return VK_FALSE;
	}

	VkResult CreateDebugUtilsMessengerEXT(
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

	void DestroyDebugUtilsMessengerEXT(
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
											VkDevice device,
											const std::uint32_t* shaderContents,
											const std::uint64_t shaderSize) {
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
											device,
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
			"VK_KHR_win32_surface",
#ifdef KVK_DEBUG
			VK_EXT_DEBUG_UTILS_EXTENSION_NAME
#endif
		};

		VkInstanceCreateInfo instanceCreateInfo = {
			.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
			.pApplicationInfo = &appInfo,
#if defined(KVK_DEBUG)
			.enabledLayerCount = sizeof(desiredLayers) / sizeof(desiredLayers[0]),
			.ppEnabledLayerNames = desiredLayers,
#endif
			.enabledExtensionCount = sizeof(desiredExtensions) / sizeof(desiredExtensions[0]),
			.ppEnabledExtensionNames = desiredExtensions
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
#if defined(_WIN32)
		rc = createWin32Surface(state, settings->window);
#endif
		if(rc != ReturnCode::OK) {
			return rc;
		}
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
				if(!computeFamilyFound && !graphicsFamilyFound && !presentFamilyFound ) {
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

		if(!features13.synchronization2) {
			logInfo("Sync2 is not available");
			return ReturnCode::UNKNOWN;
		}

		if(!features12.bufferDeviceAddress) {
			logInfo("bufferDeviceAddress is not available");
			return ReturnCode::UNKNOWN;
		}

		features13 = VkPhysicalDeviceVulkan13Features {
			.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
			.synchronization2 = VK_TRUE
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

		VkDescriptorPoolSize poolSizes[] {
			{
				.type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
				.descriptorCount = 10,
			},
		};


		if(createDescriptorPool(state.descriptorPool,
								state.device,
								poolSizes,
								sizeof(poolSizes) / sizeof(poolSizes[0])) != ReturnCode::OK) {
			return ReturnCode::UNKNOWN;
		}
		logInfo("Created descriptor pool");

		VkDescriptorSetLayoutBinding bindings[] {
			{
				.binding = 0,
				.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
				.descriptorCount = 1,
			}
		};

		if(createDescriptorSetLayout(state.drawImageDescriptorLayout,
									 state.device,
									 VK_SHADER_STAGE_COMPUTE_BIT,
									 bindings,
									 sizeof(bindings) / sizeof(bindings[0])) != ReturnCode::OK) {
			return ReturnCode::UNKNOWN;
		}
		logInfo("Created descriptor set layout");

		if(allocateDescriptorSet(state.drawImageDescriptors,
								 state.descriptorPool,
								 state.device,
								 state.drawImageDescriptorLayout) != ReturnCode::OK) {
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
			.commandBufferCount = MAX_IN_FLIGHT_FRAMES
		};

		VkCommandBuffer cBuffers[MAX_IN_FLIGHT_FRAMES];

		if(vkAllocateCommandBuffers(state.device,
									&cbufferAllocInfo,
									cBuffers) != VK_SUCCESS) {
			logError("Could not allocate cbuffers");
			return ReturnCode::UNKNOWN;
		}

		for(int i = 0; i != MAX_IN_FLIGHT_FRAMES; i++) {
			state.frames[i].commandBuffer = cBuffers[i];
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
		return ReturnCode::OK;
	}

	static void drawBackground(VkCommandBuffer commandBuffer,
							   VkPipeline pipeline,
							   VkPipelineLayout pipelineLayout,
							   VkDescriptorSet drawImageDescriptors,
							   VkExtent2D drawExtent) {
		vkCmdBindPipeline(commandBuffer,
						  VK_PIPELINE_BIND_POINT_COMPUTE,
						  pipeline);

		vkCmdBindDescriptorSets(commandBuffer,
								VK_PIPELINE_BIND_POINT_COMPUTE,
								pipelineLayout,
								0,
								1,
								&drawImageDescriptors,
								0,
								nullptr);

		static float lmao = 0.0f;
		lmao += 0.01f;
		PushConstants pc {
			lmao,
			0,
			-lmao * 511.0f,
		};
		vkCmdPushConstants(commandBuffer,
						   pipelineLayout,
						   VK_SHADER_STAGE_COMPUTE_BIT,
						   0,
						   sizeof(PushConstants),
						   &pc);

		vkCmdDispatch(commandBuffer,
					  std::ceil(drawExtent.width  / 16.0),
					  std::ceil(drawExtent.height / 16.0),
					  1);
	}

	ReturnCode recordCommandBuffer(VkCommandBuffer commandBuffer,
								   VkImage drawImage,
								   const VkExtent2D& extent,
								   VkImage image,
								   VkDescriptorSet drawImageDescriptors,
								   const Pipeline& pipeline) {

		vkResetCommandBuffer(commandBuffer, 0);
		VkCommandBufferBeginInfo beginInfo = {
			.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		};

		if(vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS) {
			logError("Could not start command buffer recording");
			return ReturnCode::UNKNOWN;
		}
		transitionImage(commandBuffer,
						drawImage,
						VK_IMAGE_LAYOUT_UNDEFINED,
						VK_IMAGE_LAYOUT_GENERAL);

		drawBackground(commandBuffer,
					   pipeline.pipeline,
					   pipeline.layout,
					   drawImageDescriptors,
					   extent);

		transitionImage(commandBuffer,
						drawImage,
						VK_IMAGE_LAYOUT_GENERAL,
						VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

		transitionImage(commandBuffer,
						image,
						VK_IMAGE_LAYOUT_UNDEFINED,
						VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

		blitImageToImage(commandBuffer,
						 drawImage,
						 image,
						 extent,
						 extent);

		transitionImage(commandBuffer,
						image,
						VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
						VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);


		if(vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
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

		std::vector<std::uint32_t> queueFamilyIndices = {
			state.graphicsFamilyIndex,
			state.presentFamilyIndex,
			state.computeFamilyIndex
		};

		std::sort(queueFamilyIndices.begin(), queueFamilyIndices.end());
		queueFamilyIndices.erase(std::unique(queueFamilyIndices.begin(), queueFamilyIndices.end()),
								 queueFamilyIndices.end());

		if(queueFamilyIndices.size() == 1) {
			//logInfo("Swapchain running in exclusive mode");
		} else {
			//logInfo("Swapchain running in concurrent mode");
		}

		VkSwapchainCreateInfoKHR swapchainCreateInfo = {
			.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
			.surface = state.surface,
			.minImageCount = imageCount,
			.imageFormat = format.format,
			.imageColorSpace = format.colorSpace,
			.imageExtent = extent,
			.imageArrayLayers = 1,
			.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
			.imageSharingMode = (queueFamilyIndices.size() == 1 ? VK_SHARING_MODE_EXCLUSIVE : VK_SHARING_MODE_CONCURRENT),
			.queueFamilyIndexCount = static_cast<std::uint32_t>(queueFamilyIndices.size()),
			.pQueueFamilyIndices = queueFamilyIndices.data(),
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

		state.drawImage.format = format.format;
		state.drawImage.extent = drawImageExtent;
		VkImageUsageFlags drawImageUsage = 
			VK_IMAGE_USAGE_TRANSFER_DST_BIT | 
			VK_IMAGE_USAGE_TRANSFER_SRC_BIT | 
			VK_IMAGE_USAGE_STORAGE_BIT | 
			VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

		VkImageCreateInfo drawImageCreateInfo = imageCreateInfo(state.physicalDevice,
																VK_FORMAT_R16G16B16A16_SFLOAT,
																drawImageUsage,
																drawImageExtent);

		VmaAllocationCreateInfo drawImageAllocInfo = {
			.usage = VMA_MEMORY_USAGE_GPU_ONLY,
			.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
		};

		vmaCreateImage(state.allocator,
					   &drawImageCreateInfo,
					   &drawImageAllocInfo,
					   &state.drawImage.image,
					   &state.drawImage.allocation,
					   nullptr);

		VkImageViewCreateInfo drawImageViewInfo = imageViewCreateInfo(VK_FORMAT_R16G16B16A16_SFLOAT,
																	  state.drawImage.image,
																	  VK_IMAGE_ASPECT_COLOR_BIT);
		if(vkCreateImageView(state.device,
							 &drawImageViewInfo,
							 nullptr,
							 &state.drawImage.view) != VK_SUCCESS) {
			logError("Could not create draw image");
			return ReturnCode::UNKNOWN;
		}
		VkDescriptorImageInfo imageInfo = {
			.imageView = state.drawImage.view,
			.imageLayout = VK_IMAGE_LAYOUT_GENERAL,
		};

		VkWriteDescriptorSet writeDescriptor = {
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstSet = state.drawImageDescriptors,
			.dstBinding = 0,
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
			.pImageInfo = &imageInfo,
		};

		vkUpdateDescriptorSets(state.device,
							   1,
							   &writeDescriptor,
							   0,
							   nullptr);
		return ReturnCode::OK;
	}

	ReturnCode recreateSwapchain(RendererState& state,
								 Pipeline& pipeline,
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

	ReturnCode createPipeline(Pipeline& pipeline,
							  const RendererState& state) {
		VkPushConstantRange pcRange = {
			.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
			.offset = 0,
			.size = sizeof(PushConstants),
		};
		
		VkPipelineLayoutCreateInfo layoutCreateInfo = {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
			.setLayoutCount = 1,
			.pSetLayouts = &state.drawImageDescriptorLayout,
			.pushConstantRangeCount = 1,
			.pPushConstantRanges = &pcRange
		};

		if(vkCreatePipelineLayout(state.device,
								  &layoutCreateInfo,
								  nullptr,
								  &pipeline.layout) != VK_SUCCESS) {
			logError("Could not create pipeline layout");
			return ReturnCode::UNKNOWN;
		}

		VkShaderModule shaderModule;
		ReturnCode rc = createShaderModuleFromFile(shaderModule,
												   state.device,
												   "shaders/gradient.comp.glsl.spv");
		if(rc != ReturnCode::OK) {
			return rc;
		}
												   
		VkComputePipelineCreateInfo createInfo = {
			.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
			.stage = {
				.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
				.stage = VK_SHADER_STAGE_COMPUTE_BIT,
				.module = shaderModule,
				.pName = "main"
			},
			.layout = pipeline.layout,
		};

		if(vkCreateComputePipelines(state.device,
									VK_NULL_HANDLE,
									1,
									&createInfo,
									nullptr,
									&pipeline.pipeline) != VK_SUCCESS) {
			logError("Could not create pipeline");
			return ReturnCode::UNKNOWN;
		}

		vkDestroyShaderModule(state.device, 
							  shaderModule,
							  nullptr);
		return ReturnCode::OK;
	}

}
