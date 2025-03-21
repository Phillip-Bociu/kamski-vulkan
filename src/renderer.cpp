#define VMA_IMPLEMENTATION
#define GLM_ENABLE_EXPERIMENTAL
#include "common.h"
#include "renderer.h"
#include "utils.h"

#include <glm/gtx/transform.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <fstream>
#include <vector>
#include <set>
#include <numeric>
#include <algorithm>

#include <fastgltf/glm_element_traits.hpp>
#include <fastgltf/core.hpp>
#include <fastgltf/tools.hpp>


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
		return ReturnCode::OK;
	}

	static void drawGeometry(VkCommandBuffer cmd,
							 VkImageView drawImageView,
							 VkImageView depthImageView,
							 VkExtent2D drawExtent,
							 VkPipeline pipeline,
							 VkPipelineLayout pipelineLayout,
							 VkPipeline meshPipeline,
							 VkPipelineLayout meshPipelineLayout,
							 const std::vector<MeshAsset>& meshes) {
		VkRenderingAttachmentInfo colorAttachment = {
			.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
			.imageView = drawImageView,
			.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
			.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
			.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
			.clearValue =  {
				.color = { 0.0f, 0.0f, 0.0f, 1.0f },
			},
		};

		VkRenderingAttachmentInfo depthAttachment = {
			.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
			.imageView = depthImageView,
			.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
			.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
			.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
			.clearValue =  {
				.depthStencil =  {
					.depth = 0.0f
				},
			}, 
		};

		VkRenderingInfo renderInfo = {
			.sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
			.renderArea = { 
				VkOffset2D {0, 0},
				drawExtent 
			},
			.layerCount = 1,
			.colorAttachmentCount = 1,
			.pColorAttachments = &colorAttachment,
			.pDepthAttachment = &depthAttachment,
			.pStencilAttachment = nullptr
		};

		vkCmdBindPipeline(cmd,
						  VK_PIPELINE_BIND_POINT_GRAPHICS,
						  meshPipeline);

		vkCmdBeginRendering(cmd,
							&renderInfo);

		VkViewport viewport = {
			.x = 0,
			.y = 0,
			.width = static_cast<float>(drawExtent.width),
			.height = static_cast<float>(drawExtent.height),
			.minDepth = 0.0f,
			.maxDepth = 1.0f,
		};

		vkCmdSetViewport(cmd, 0, 1, &viewport);
		VkRect2D scissor = {
			.offset = {
				.x = 0,
				.y = 0
			}, 
			.extent = drawExtent,
		};

		vkCmdSetScissor(cmd, 0, 1, &scissor);

		auto makeInfReversedZProjRH = [](float fovY_radians, float aspectWbyH, float zNear) -> glm::mat4 {
			float f = 1.0f / tan(fovY_radians / 2.0f);
			return glm::mat4(
				f / aspectWbyH, 0.0f,  0.0f,  0.0f,
				0.0f,    f,  0.0f,  0.0f,
				0.0f, 0.0f,  0.0f, -1.0f,
				0.0f, 0.0f, zNear,  0.0f);
		};

		PushConstants pc = {
			.proj = makeInfReversedZProjRH(glm::radians(45.f), (float)drawExtent.width / (float)drawExtent.height, 0.1f),
			.view = glm::translate(glm::mat4(1.0f), glm::vec3{ 0,0, -5}),
		};
		pc.proj[1][1] *= -1;

		static glm::mat4 model = glm::mat4(1.0f);
		model = glm::rotate(model, glm::radians(1.0f), glm::vec3(0.0f, 1.0f, 0.0f));
		pc.model = model;
		
		// camera projection
		const MeshAsset& asset = meshes[2];
		pc.vertexBuffer = asset.mesh.vertexBufferAddress;
		vkCmdPushConstants(cmd,
						   meshPipelineLayout,
						   VK_SHADER_STAGE_VERTEX_BIT,
						   0,
						   sizeof(pc),
						   &pc);
		vkCmdBindIndexBuffer(cmd,
							 asset.mesh.indices.buffer,
							 0,
							 VK_INDEX_TYPE_UINT32);

		vkCmdDrawIndexed(cmd,
						 asset.surfaces[0].count,
						 1,
						 asset.surfaces[0].startIndex,
						 0,
						 0);

		vkCmdEndRendering(cmd);
	}

	ReturnCode recordCommandBuffer(VkCommandBuffer commandBuffer,
								   AllocatedImage& drawImage,
								   AllocatedImage& depthImage,
								   const VkExtent2D& extent,
								   VkImage image,
								   VkDescriptorSet drawImageDescriptors,
								   const Pipeline& pipeline,
								   const Pipeline& meshPipeline,
								   const std::vector<MeshAsset>& meshes) {

		vkResetCommandBuffer(commandBuffer, 0);
		VkCommandBufferBeginInfo beginInfo = {
			.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		};

		if(vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS) {
			logError("Could not start command buffer recording");
			return ReturnCode::UNKNOWN;
		}

		transitionImage(commandBuffer,
						drawImage.image,
						VK_IMAGE_LAYOUT_UNDEFINED,
						VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

		transitionImage(commandBuffer,
						depthImage.image,
						VK_IMAGE_LAYOUT_UNDEFINED,
						VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);

		drawGeometry(commandBuffer,
					 drawImage.view,
					 depthImage.view,
					 extent,
					 pipeline.pipeline,
					 pipeline.layout,
					 meshPipeline.pipeline,
					 meshPipeline.layout,
					 meshes);

		transitionImage(commandBuffer,
						image,
						VK_IMAGE_LAYOUT_UNDEFINED,
						VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
		transitionImage(commandBuffer,
						drawImage.image,
						VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
						VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

		blitImageToImage(commandBuffer,
						 drawImage.image,
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

		ReturnCode rc;

		rc = createImage(state.depthImage,
						 state,
						 VK_FORMAT_D32_SFLOAT,
						 drawImageExtent,
						 VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
						 VK_IMAGE_ASPECT_DEPTH_BIT);

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
						 VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
						 VK_IMAGE_ASPECT_COLOR_BIT);
		if(rc != ReturnCode::OK) {
			logError("Could not create draw image");
			return rc;
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
			.primitiveRestartEnable = VK_FALSE,
		};
		
		rasterizer = {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
			.lineWidth = 1.0f
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
		depthStencil.stencilTestEnable = VK_FALSE;
		depthStencil.front = {};
		depthStencil.back = {};
		depthStencil.minDepthBounds = 0.f;
		depthStencil.maxDepthBounds = 1.f;
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
										  fastgltf::Options::LoadGLBBuffers |
										  fastgltf::Options::LoadExternalBuffers);

		if(load) {
			gltf = std::move(load.get());
		} else {
			logError("fuck");
			return ReturnCode::UNKNOWN;
		}

		retval.reserve(gltf.meshes.size());

		std::vector<std::uint32_t> indices;
		std::vector<Vertex> vertices;

		for(fastgltf::Mesh& mesh : gltf.meshes) {
			retval.emplace_back();
			MeshAsset& newMesh = retval.back();
			logInfo("loading %s", mesh.name.c_str());
			
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
			for(Vertex & v : vertices) {
				v.color = glm::vec4(v.normal, 1.0f);
			}
			ReturnCode rc = createMesh(newMesh.mesh,
									   state,
									   indices,
									   vertices);
			if(rc != ReturnCode::OK) {
				logError("fml");
				return rc;
			}
		}
		return ReturnCode::OK;
	}

	ReturnCode createImage(AllocatedImage& image,
						   RendererState& state,
						   const VkFormat format,
						   const VkExtent3D extent,
						   const VkImageUsageFlags usageFlags,
						   const VkImageAspectFlags aspectFlags) {

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

		VkImageViewCreateInfo imageViewInfo = imageViewCreateInfo(format,
																  image.image,
																  aspectFlags);
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


}
