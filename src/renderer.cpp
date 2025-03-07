#include "renderer.h"
#include "common.h"
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

	ReturnCode createPipeline(const RendererState& state,
							  Pipeline& pipeline,
							  const std::uint32_t* vertexShaderData,   const size_t vertexShaderSize,
							  const std::uint32_t* fragmentShaderData, const size_t fragmentShaderSize) {
		
		VkShaderModuleCreateInfo shaderModuleCreateInfo = {
			.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
			.codeSize = vertexShaderSize,
			.pCode = vertexShaderData,
		};
		if(vkCreateShaderModule(state.device,
								&shaderModuleCreateInfo,
								nullptr,
								&pipeline.vertexShader) != VK_SUCCESS) {
			logError("Could not create vertex shader");
			return ReturnCode::SHADER_CREATION_ERROR;
		}

		shaderModuleCreateInfo.codeSize = fragmentShaderSize;
		shaderModuleCreateInfo.pCode = fragmentShaderData;
		if(vkCreateShaderModule(state.device,
								&shaderModuleCreateInfo,
								nullptr,
								&pipeline.fragmentShader) != VK_SUCCESS) {
			logError("Could not create fragment shader");
			return ReturnCode::SHADER_CREATION_ERROR;
		}

		VkPipelineShaderStageCreateInfo shaderStageCreateInfo[2] = {
			{
				.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
				.stage = VK_SHADER_STAGE_VERTEX_BIT,
				.module = pipeline.vertexShader,
				.pName = "main"
			}, 
			{
				.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
				.stage = VK_SHADER_STAGE_FRAGMENT_BIT,
				.module = pipeline.fragmentShader,
				.pName = "main"
			}
		};

		VkDynamicState dynamicStates[] = {
			VK_DYNAMIC_STATE_VIEWPORT,
			VK_DYNAMIC_STATE_SCISSOR,
		};
		
		VkPipelineDynamicStateCreateInfo dynamicStateCreateInfo = {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
			.dynamicStateCount = sizeof(dynamicStates) / sizeof(dynamicStates[0]),
			.pDynamicStates = dynamicStates,
		};

		VkPipelineVertexInputStateCreateInfo vertexStateCreateInfo = {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
			.vertexBindingDescriptionCount = 0,
			.pVertexBindingDescriptions = nullptr,
			.vertexAttributeDescriptionCount = 0,
			.pVertexAttributeDescriptions = nullptr
		};

		VkPipelineInputAssemblyStateCreateInfo inputAssemblyCreateInfo = {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
			.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
			.primitiveRestartEnable = VK_FALSE
		};

		VkViewport viewport =  {
			.x = 0.0f,
			.y = 0.0f,
			.width = static_cast<float>(state.swapchainExtent.width),
			.height = static_cast<float>(state.swapchainExtent.width),
			.minDepth = 0.0f,
			.maxDepth = 1.0f,
		};
		
		VkRect2D scissor = {
			.offset = {0, 0},
			.extent = state.swapchainExtent
		};
		
		VkPipelineViewportStateCreateInfo viewportStateCreateInfo = {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
			.viewportCount = 1,
			.pViewports = &viewport,
			.scissorCount = 1,
			.pScissors = &scissor,
		};

		VkPipelineRasterizationStateCreateInfo rasterCreateInfo = {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
			.depthClampEnable = VK_FALSE,
			.rasterizerDiscardEnable = VK_FALSE,
			.polygonMode = VK_POLYGON_MODE_FILL,
			.cullMode = VK_CULL_MODE_BACK_BIT,
			.frontFace = VK_FRONT_FACE_CLOCKWISE,
			.depthBiasEnable = VK_FALSE,
			.depthBiasConstantFactor = 0.0f,
			.depthBiasClamp = 0.0f,
			.depthBiasSlopeFactor = 0.0f,
			.lineWidth = 1.0f,
		};

		VkPipelineMultisampleStateCreateInfo multisamplingCreateInfo = {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
			.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
			.sampleShadingEnable = VK_FALSE,
			.minSampleShading = 1.0f,
			.pSampleMask = nullptr,
			.alphaToCoverageEnable = VK_FALSE,
			.alphaToOneEnable = VK_FALSE,
		};

		VkPipelineColorBlendAttachmentState colorBlendAttachment = {
			.blendEnable = VK_TRUE,
			.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
			.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
			.colorBlendOp = VK_BLEND_OP_ADD,
			.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
			.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
			.alphaBlendOp = VK_BLEND_OP_ADD,
			.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
		};

		VkPipelineColorBlendStateCreateInfo blendCreateInfo = {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
			.logicOpEnable = VK_FALSE,
			.attachmentCount = 1,
			.pAttachments = &colorBlendAttachment,
			.blendConstants = {
				0.0f,
				0.0f,
				0.0f,
				0.0f,
			}
		};

		VkPipelineLayoutCreateInfo layoutCreateInfo = {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
			.setLayoutCount = 0,
			.pSetLayouts = nullptr,
			.pushConstantRangeCount = 0,
			.pPushConstantRanges = nullptr
		};

		if(vkCreatePipelineLayout(state.device,
								  &layoutCreateInfo,
								  nullptr,
								  &pipeline.layout) != VK_SUCCESS) {
			logError("Could not create pipeline layout");
			return ReturnCode::UNKNOWN;
		}

		VkAttachmentDescription colorAttachment = {
			.format = state.swapchainImageFormat.format,
			.samples = VK_SAMPLE_COUNT_1_BIT,
			.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
			.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
			.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
			.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
			.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
			.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
		};

		VkAttachmentReference colorRef = {
			.attachment = 0,
			.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		};

		VkSubpassDescription subpassDesc = {
			.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
			.colorAttachmentCount = 1,
			.pColorAttachments = &colorRef
		};

		VkSubpassDependency subpassDep = {
			.srcSubpass = VK_SUBPASS_EXTERNAL,
			.dstSubpass = 0,
			.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			.srcAccessMask = 0,
			.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
		};

		VkRenderPassCreateInfo renderPassCreateInfo = {
			.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
			.attachmentCount = 1,
			.pAttachments = &colorAttachment,
			.subpassCount = 1,
			.pSubpasses = &subpassDesc,
			.dependencyCount = 1,
			.pDependencies = &subpassDep
		};

		if(vkCreateRenderPass(state.device,
							  &renderPassCreateInfo,
							  nullptr,
							  &pipeline.renderPass) != VK_SUCCESS) {
			logError("Could not create render pass");
			return ReturnCode::UNKNOWN;
		}

		VkGraphicsPipelineCreateInfo createInfo = {
			.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
			.stageCount = 2,
			.pStages = shaderStageCreateInfo,
			.pVertexInputState = &vertexStateCreateInfo,
			.pInputAssemblyState = &inputAssemblyCreateInfo,
			.pViewportState = &viewportStateCreateInfo,
			.pRasterizationState = &rasterCreateInfo,
			.pMultisampleState = &multisamplingCreateInfo,
			.pDepthStencilState = nullptr,
			.pColorBlendState = &blendCreateInfo,
			.pDynamicState = &dynamicStateCreateInfo,
			.layout = pipeline.layout,
			.renderPass = pipeline.renderPass,
			.subpass = 0,
			.basePipelineHandle = VK_NULL_HANDLE,
			.basePipelineIndex = -1,
		};

		if(vkCreateGraphicsPipelines(state.device,
									 VK_NULL_HANDLE,
									 1,
									 &createInfo,
									 nullptr,
									 &pipeline.pipeline) != VK_SUCCESS) {
			logError("Could not create pipeline");
			return ReturnCode::UNKNOWN;
		}
		logDebug("Created pipeline");

		return ReturnCode::OK;
	}

	ReturnCode createPipeline(const RendererState& state,
							  Pipeline& pipeline,
							  const char* vertexShaderPath,
							  const char* fragmentShaderPath) {
		std::ifstream vs(vertexShaderPath, std::ios::ate | std::ios::binary);
		if(!vs.is_open()) {
			logError("File %s not found", vertexShaderPath);
			return ReturnCode::FILE_NOT_FOUND;
		}
		std::vector<char> vsData(vs.tellg());
		vs.seekg(0);
		vs.read(vsData.data(), vsData.size());

		std::ifstream fs(fragmentShaderPath, std::ios::ate | std::ios::binary);
		if(!fs.is_open()) {
			logError("File %s not found", fragmentShaderPath);
			return ReturnCode::FILE_NOT_FOUND;
		}
		std::vector<char> fsData(fs.tellg());
		fs.seekg(0);
		fs.read(fsData.data(), fsData.size());

		return createPipeline(state, pipeline,
							  reinterpret_cast<std::uint32_t*>(vsData.data()), vsData.size(),
							  reinterpret_cast<std::uint32_t*>(fsData.data()), fsData.size());
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
		logDebug("what %llu", sizeof(desiredLayers));

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


		VkPhysicalDeviceFeatures deviceFeatures = {
		};

		VkDeviceCreateInfo deviceCreateInfo = {
			.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
			.queueCreateInfoCount = static_cast<std::uint32_t>(queueCreateInfos.size()),
			.pQueueCreateInfos = queueCreateInfos.data(),
			.enabledExtensionCount = sizeof(desiredDeviceExtensions) / sizeof(desiredDeviceExtensions[0]),
			.ppEnabledExtensionNames = desiredDeviceExtensions,
			.pEnabledFeatures = &deviceFeatures,
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
			if (pm == VK_PRESENT_MODE_MAILBOX_KHR) {
				chosenPresentMode = pm;
				break;
			}
		}
		state.swapchainPresentMode = chosenPresentMode;

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

		if(createSwapchain(state,
						   chosenExtent,
						   chosenFormat,
						   chosenPresentMode,
						   imageCount) != ReturnCode::OK) {
			logError("Could not create swapchain");
			return ReturnCode::UNKNOWN;
		}
		
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

		VmaAllocatorCreateInfo vmaCreateInfo = {
			.physicalDevice = state.physicalDevice,
			.device = state.device,
			.instance = state.instance,
			.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT,
		};
		
		vmaCreateAllocator(&vmaCreateInfo,
						   &state.allocator);
		return ReturnCode::OK;
	}

	ReturnCode recordCommandBuffer(VkCommandBuffer commandBuffer,
								   Pipeline& pipeline,
								   VkFramebuffer framebuffer,
								   const VkExtent2D& extent) {

		vkResetCommandBuffer(commandBuffer, 0);
		VkCommandBufferBeginInfo beginInfo = {
			.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		};

		if(vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS) {
			logError("Could not start command buffer recording");
			return ReturnCode::UNKNOWN;
		}

		VkClearValue clearColor = {1.0f, 0.0f, 0.0f, 1.0};
		VkRenderPassBeginInfo renderPassInfo = {
			.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
			.renderPass = pipeline.renderPass,
			.framebuffer = framebuffer,
			.renderArea = {
				.offset = {0, 0},
				.extent = extent
			},
			.clearValueCount = 1,
			.pClearValues = &clearColor,
		};

		vkCmdBeginRenderPass(commandBuffer,
							 &renderPassInfo,
							 VK_SUBPASS_CONTENTS_INLINE);

		vkCmdBindPipeline(commandBuffer,
						  VK_PIPELINE_BIND_POINT_GRAPHICS,
						  pipeline.pipeline);

		VkViewport viewport = {
			.width = static_cast<float>(extent.width),
			.height = static_cast<float>(extent.height),
			.minDepth = 0.0f,
			.maxDepth = 1.0f,
		};

		vkCmdSetViewport(commandBuffer,
						 0,
						 1,
						 &viewport);

		VkRect2D scissor = {
			.offset = {0, 0},
			.extent = extent,
		};

		vkCmdSetScissor(commandBuffer, 
						0,
						1,
						&scissor);

		vkCmdDraw(commandBuffer,
				  3,
				  1,
				  0,
				  0);

		vkCmdEndRenderPass(commandBuffer);

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
							   std::uint32_t imageCount) {
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
			.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
			.imageSharingMode = (queueFamilyIndices.size() == 1 ? VK_SHARING_MODE_EXCLUSIVE : VK_SHARING_MODE_CONCURRENT),
			.queueFamilyIndexCount = static_cast<std::uint32_t>(queueFamilyIndices.size()),
			.pQueueFamilyIndices = queueFamilyIndices.data(),
			.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
			.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
			.presentMode = presentMode,
			.clipped = VK_TRUE,
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

	ReturnCode createFramebuffers(RendererState& state,
								  Pipeline& pipeline) {
		state.framebuffers.resize(state.swapchainImages.size());

		for(int i = 0; i != state.swapchainImages.size(); i++) {
			VkImageView attachments[] = {
				state.swapchainImageViews[i]
			};
			VkFramebufferCreateInfo createInfo = {
				.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
				.renderPass = pipeline.renderPass,
				.attachmentCount = 1,
				.pAttachments = attachments,
				.width = state.swapchainExtent.width,
				.height = state.swapchainExtent.height,
				.layers = 1
			};

			if(vkCreateFramebuffer(state.device,
								   &createInfo,
								   nullptr,
								   &state.framebuffers[i]) != VK_SUCCESS) {
				logError("Could not create framebuffers");
				return ReturnCode::UNKNOWN;
			}
		}
		//logInfo("Created framebuffers");

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

		for(VkFramebuffer framebuffer : state.framebuffers) {
			vkDestroyFramebuffer(state.device,
								 framebuffer,
								 nullptr);
		}

		for(VkImageView imageView : state.swapchainImageViews) {
			vkDestroyImageView(state.device,
							   imageView,
							   nullptr);
		}
		vkDestroySwapchainKHR(state.device, 
							  state.swapchain,
							  nullptr);
		
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
										state.swapchainImageCount);
		if(rc != ReturnCode::OK) {
			return rc;
		}

		return createFramebuffers(state, pipeline);
	}
}
