#include "common.h"
#include "utils.h"
#include <cstring>
#define VMA_VULKAN_VERSION 1003000
#include "glm/ext/matrix_transform.hpp"
#include "renderer.h"
#include "vulkan/vulkan_core.h"
#include "vk_mem_alloc.h"
#include <kvk.h>
#include <cstdint>
#include <Windows.h>
#include <random>
#include <thread>
#include <mutex>

kvk::RendererState state;
std::mutex renderThreadMutex;
std::atomic<bool> allow;

static LRESULT CALLBACK winProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
	switch (message) {
		case WM_USER: {
			return 0;
		} break;
		case WM_CLOSE: {
			PostQuitMessage(0);
			return 0;
		} break;
		case WM_SIZE: {
			if (state.isInitialized.load()) {
				allow.store(false);
				RECT res = {};
				GetClientRect(window, &res);
				std::lock_guard<std::mutex> lck(renderThreadMutex);
				if(kvk::recreateSwapchain(state,
										  res.right,
										  res.bottom) != kvk::ReturnCode::OK) {
					ShowWindow(window, SW_HIDE);
					ExitProcess(1);
				}
				allow.store(true);
			} else {
				logInfo("Renderer not yet initialized");
			}
		} break;
	}

	return DefWindowProc(window, message, wParam, lParam);
}



int main() {
	const HINSTANCE instance = GetModuleHandle(NULL);
	WNDCLASSEX winClass = {0};
	winClass.cbSize = sizeof(winClass);
	winClass.style = CS_HREDRAW | CS_VREDRAW;
	winClass.lpfnWndProc = winProc;
	winClass.hInstance = instance;
	winClass.hIcon = LoadIcon(instance, IDI_APPLICATION);
	winClass.hIconSm = LoadIcon(instance, IDI_APPLICATION);
	winClass.hCursor = LoadCursor(NULL, IDC_ARROW);
	winClass.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
	winClass.lpszClassName = "KamskiWindowClass";

	if (!RegisterClassEx(&winClass)) {
		printf("Could not register window class\n");
		return EXIT_FAILURE;
	}

	// Creating actual window
	HWND window = CreateWindowExA(0,
								  winClass.lpszClassName,
								  "Vulkan triangle",
								  WS_OVERLAPPEDWINDOW,
								  CW_USEDEFAULT,
								  CW_USEDEFAULT,
								  1000,
								  1000,
								  NULL,
								  NULL,
								  instance,
								  NULL);
	logInfo("Window created");
	ShowWindow(window, SW_SHOW);

	std::thread([&]() {
		renderThreadMutex.lock();
		const kvk::InitSettings initSettings = {
			.appName = "kamki tet",
			.width = 1600,
			.height = 900,
			.window = window
		};

		std::uint32_t currentFrame = 0;
		kvk::ReturnCode rc = kvk::init(state, &initSettings);
		if(rc != kvk::ReturnCode::OK) {
			ShowWindow(window, SW_HIDE);
			logError("Init returned: %d", static_cast<int>(rc));
			ExitProcess(1);
		}

		VkShaderModule solidColorFragmentShader;
		rc = kvk::createShaderModuleFromFile(solidColorFragmentShader,
											 state.device,
											 "../shaders/solid_color.frag.glsl.spv");
		if(rc != kvk::ReturnCode::OK) {
			ExitProcess(1);
		}

		VkShaderModule meshVertexShader;
		rc = kvk::createShaderModuleFromFile(meshVertexShader,
											 state.device,
											 "../shaders/mesh.vert.glsl.spv");
		if(rc != kvk::ReturnCode::OK) {
			ExitProcess(1);
		}

		VkShaderModule fragmentShader;
		rc = kvk::createShaderModuleFromFile(fragmentShader,
											 state.device,
											 "../shaders/simple_shader.frag.glsl.spv");
		if(rc != kvk::ReturnCode::OK) {
			ExitProcess(1);
		}

		kvk::Pipeline meshPipeline;
		rc = kvk::PipelineBuilder()
			.setShaders(meshVertexShader, fragmentShader)
			.setColorAttachmentFormat(state.drawImage.format)
			.setDepthAttachmentFormat(state.depthImage.format)
			.setStencilAttachmentFormat(state.depthImage.format)
			.enableDepthTest(true, VK_COMPARE_OP_GREATER_OR_EQUAL)
			.enableStencilTest(VK_COMPARE_OP_ALWAYS, true)
			.enableBlendingAlpha()
			.addPushConstantRange(VK_SHADER_STAGE_VERTEX_BIT, sizeof(kvk::PushConstants))
			.addDescriptorSetLayout(state.sceneDescriptorLayout)
			.addDescriptorSetLayout(state.samplerDescriptorLayout)
			.build(meshPipeline, state.device);

		kvk::Pipeline outlinePipeline;
		rc = kvk::PipelineBuilder()
			.setShaders(meshVertexShader, solidColorFragmentShader)
			.setColorAttachmentFormat(state.drawImage.format)
			.setDepthAttachmentFormat(state.depthImage.format)
			.setStencilAttachmentFormat(state.depthImage.format)
			.enableStencilTest(VK_COMPARE_OP_NOT_EQUAL, false)
			.enableBlendingAlpha()
			.addPushConstantRange(VK_SHADER_STAGE_VERTEX_BIT, sizeof(kvk::PushConstants))
			.addDescriptorSetLayout(state.sceneDescriptorLayout)
			.build(outlinePipeline, state.device);

		state.isInitialized.store(true);

		allow.store(true);
		renderThreadMutex.unlock();

		vkDestroyShaderModule(state.device,
							  fragmentShader,
							  nullptr);

		vkDestroyShaderModule(state.device,
							  meshVertexShader,
							  nullptr);
        vkDestroyShaderModule(state.device,
                              solidColorFragmentShader,
                              nullptr);

		std::vector<kvk::MeshAsset> meshes;
		rc = kvk::loadGltf(meshes,
						   state,
						   "../assets/monkey.glb");

		if(rc != kvk::ReturnCode::OK) {
			logError("could not load basicmesh.glb");
			ShowWindow(window, SW_HIDE);
			ExitProcess(1);
		}

		std::uint32_t monkeCount = 10;

		kvk::AllocatedBuffer instanceBuffer;
		rc = kvk::createBuffer(instanceBuffer,
	                           state.allocator,
	                           monkeCount * sizeof(glm::mat4),
                               VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
	                           VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
	                           VK_BUFFER_USAGE_TRANSFER_DST_BIT,
	                           VMA_MEMORY_USAGE_GPU_ONLY);

		if(rc != kvk::ReturnCode::OK) {
			logError("could not create instance buffer");
			ShowWindow(window, SW_HIDE);
			ExitProcess(1);
		}

		kvk::AllocatedBuffer stagingBuffer;
		rc = kvk::createBuffer(stagingBuffer,
	                           state.allocator,
	                           monkeCount * sizeof(glm::mat4),
	                           VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
	                           VMA_MEMORY_USAGE_CPU_TO_GPU);
		if(rc != kvk::ReturnCode::OK) {
			logError("could not create staging buffer");
			ShowWindow(window, SW_HIDE);
			ExitProcess(1);
		}

		{
		    logInfo("Generating %u monkeys...", monkeCount);
		    std::random_device device;
			std::mt19937 generator = std::mt19937(device());
			std::uniform_real_distribution<> rotationDis(-90.0f, 90.0f);
			std::uniform_real_distribution<> axisDis(0.0f, 1.0f);
			std::uniform_real_distribution<> positionDis(-5.0f, 5.0f);
            std::vector<glm::mat4> monkeyPositions(monkeCount, glm::mat4(1.0f));
			for(glm::mat4& model : monkeyPositions) {
			    const glm::vec3 axis = {
					axisDis(generator),
					axisDis(generator),
					axisDis(generator),
				};

				const glm::vec3 position = {
    				positionDis(generator),
    				positionDis(generator),
    				positionDis(generator),
				};

                model = glm::rotate<float>(model, glm::radians(rotationDis(generator)), axis);
                model = glm::translate<float>(model, position);
			}
			logInfo("Generated %u monkeys!", monkeCount);
			void* data;
			if(vmaMapMemory(state.allocator, stagingBuffer.allocation, &data) != VK_SUCCESS) {
			    logError("Failed to map memory");
				ShowWindow(window, SW_HIDE);
				ExitProcess(1);
			}
			memcpy(data, monkeyPositions.data(), monkeyPositions.size() * sizeof(glm::mat4));
			vmaUnmapMemory(state.allocator, stagingBuffer.allocation);
		}

		auto transferInstanceBuffer = [&](VkCommandBuffer cmd) {
		    VkBufferCopy copyRegion {
				.size = monkeCount * sizeof(glm::mat4)
			};
            vkCmdCopyBuffer(cmd, stagingBuffer.buffer, instanceBuffer.buffer, 1, &copyRegion);
		};
		if(kvk::immediateSubmit(state.transferCommandBuffers[1], state.device, state.transferQueue, transferInstanceBuffer) != VK_SUCCESS) {
		    logError("instance data upload failed");
			ShowWindow(window, SW_HIDE);
			ExitProcess(1);
		}
		destroyBuffer(stagingBuffer, state.allocator);

		VkBufferDeviceAddressInfo addrInfo = {
		    .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
			.buffer = instanceBuffer.buffer
		};
		const VkDeviceAddress instanceBufferAddress = vkGetBufferDeviceAddress(state.device, &addrInfo);

		while(true) {
			if(!allow.load() || !state.swapchainExtent.width || !state.swapchainExtent.height) {
				continue;
			}
			std::lock_guard<std::mutex> lck(renderThreadMutex);
			if(!state.swapchainExtent.width || !state.swapchainExtent.height) {
				continue;
			}
			kvk::FrameData& frame = state.frames[currentFrame];

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

			if (result == VK_ERROR_OUT_OF_DATE_KHR) {
				continue;
			} else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
				ShowWindow(window, SW_HIDE);
				ExitProcess(1);
			}

			vkResetFences(state.device,
						  1,
						  &frame.inFlightFence);

			if(kvk::drawScene(frame,
			                  state,
							  state.swapchainImages[imageIndex],
							  state.swapchainExtent,
							  meshPipeline,
							  outlinePipeline,
							  meshes,
							  instanceBufferAddress,
							  monkeCount) != kvk::ReturnCode::OK) {
				ShowWindow(window, SW_HIDE);
				logError("Could not record commandBuffer");
				ExitProcess(1);
			}

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
				ShowWindow(window, SW_HIDE);
				logError("Queue submit failed");
				ExitProcess(1);
			}

			VkPresentInfoKHR presentInfo = {
				.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
				.waitSemaphoreCount = 1,
				.pWaitSemaphores = signalSemaphores,
				.swapchainCount = 1,
				.pSwapchains = &state.swapchain,
				.pImageIndices = &imageIndex,
				.pResults = nullptr
			};

			result = vkQueuePresentKHR(state.presentQueue,
									   &presentInfo);

			if (result == VK_ERROR_OUT_OF_DATE_KHR) {
				continue;
			} else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
				ShowWindow(window, SW_HIDE);
				ExitProcess(1);
			}

			currentFrame = (currentFrame + 1) % kvk::MAX_IN_FLIGHT_FRAMES;
		}
	}).detach();

	MSG msg;
	while(true) {
		while (PeekMessage(&msg, 0, 0, 0, PM_REMOVE)) {
			switch (msg.message) {
				case WM_QUIT: {
					logInfo("YEPPERS");
					ShowWindow(window, SW_HIDE);
					ExitProcess(0);
				} break;
			}
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}

	}

	return 0;
}
