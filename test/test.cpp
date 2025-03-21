#include <kvk.h>
#include <cstdint>
#include <Windows.h>
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
			ExitProcess(0);
		}

		VkShaderModule vertexShader;
		rc = kvk::createShaderModuleFromFile(vertexShader,
											 state.device,
											 "shaders/simple_shader.vert.glsl.spv");
		if(rc != kvk::ReturnCode::OK) {
			ExitProcess(1);
		}

		VkShaderModule meshVertexShader;
		rc = kvk::createShaderModuleFromFile(meshVertexShader,
											 state.device,
											 "shaders/mesh.vert.glsl.spv");
		if(rc != kvk::ReturnCode::OK) {
			ExitProcess(1);
		}

		VkShaderModule fragmentShader;
		rc = kvk::createShaderModuleFromFile(fragmentShader,
											 state.device,
											 "shaders/simple_shader.frag.glsl.spv");
		if(rc != kvk::ReturnCode::OK) {
			ExitProcess(1);
		}

		kvk::Pipeline pipeline;
		rc = kvk::PipelineBuilder()
			.setColorAttachmentFormat(state.swapchainImageFormat.format)
			.setShaders(vertexShader, fragmentShader)
			.setInputTopology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST)
			.setPolygonMode(VK_POLYGON_MODE_FILL)
			.setCullMode(VK_CULL_MODE_NONE, VK_FRONT_FACE_CLOCKWISE)
			.setColorAttachmentFormat(state.drawImage.format)
			.setDepthAttachmentFormat(VK_FORMAT_UNDEFINED)
			.build(pipeline, state.device);

		if(rc != kvk::ReturnCode::OK) {
			ShowWindow(window, SW_HIDE);
			logError("create pipeline did not work: %d", static_cast<int>(rc));
			ExitProcess(0);
		}

		kvk::Pipeline meshPipeline;
		rc = kvk::PipelineBuilder()
			.setColorAttachmentFormat(state.swapchainImageFormat.format)
			.addPushConstantRange(VK_SHADER_STAGE_VERTEX_BIT, sizeof(kvk::PushConstants))
			.setShaders(meshVertexShader, fragmentShader)
			.setInputTopology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST)
			.setPolygonMode(VK_POLYGON_MODE_FILL)
			.setCullMode(VK_CULL_MODE_NONE, VK_FRONT_FACE_CLOCKWISE)
			.setColorAttachmentFormat(state.drawImage.format)
			.setDepthAttachmentFormat(state.depthImage.format)
			.enableDepthTest(true, VK_COMPARE_OP_GREATER_OR_EQUAL)
			.enableBlendingAlpha()
			.build(meshPipeline, state.device);

		state.isInitialized.store(true);

		allow.store(true);
		renderThreadMutex.unlock();

		vkDestroyShaderModule(state.device,
							  vertexShader,
							  nullptr);

		vkDestroyShaderModule(state.device,
							  fragmentShader,
							  nullptr);

		vkDestroyShaderModule(state.device,
							  meshVertexShader,
							  nullptr);

		kvk::Vertex vertices[] = {
			{
				.position = {0.5f, -0.5f, 0.0f},
				.color = {0.0f, 0.0f, 0.0f, 0.1f},
			},
			{
				.position = {0.5f, 0.5f, 0.0f},
				.color = {0.5f, 0.5f, 0.5f, 0.1f},
			},
			{
				.position = {-0.5f, -0.5f, 0.0f},
				.color = {1.0f, 0.0f, 0.0f, 0.1f},
			},
			{
				.position = {-0.5f, 0.5f, 0.0f},
				.color = {0.0f, 1.0f, 0.0f, 0.1f},
			},
		};

		std::uint32_t indices[] = { 0, 1, 2, 2, 1, 3 };

		kvk::Mesh testMesh;
		rc = kvk::createMesh(testMesh,
							 state,
							 std::span(indices, 6),
							 std::span(vertices, 4));

		if(rc != kvk::ReturnCode::OK) {
			logError("could not load basicmesh.glb");
			ShowWindow(window, SW_HIDE);
			ExitProcess(1);
		}

		std::vector<kvk::MeshAsset> meshes;
		rc = kvk::loadGltf(meshes,
						   state,
						   "assets/basicmesh.glb");

		if(rc != kvk::ReturnCode::OK) {
			logError("could not load basicmesh.glb");
			ShowWindow(window, SW_HIDE);
			ExitProcess(1);
		}

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

			for(auto iter = frame.deletionQueue.rbegin(); iter != frame.deletionQueue.rend(); ++iter) {
				iter->deleteFunc(iter->vkHandle);
			}
			frame.deletionQueue.clear(); 


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
				ExitProcess(0);
			}

			vkResetFences(state.device,
						  1,
						  &frame.inFlightFence);

			kvk::MeshAsset ass = {
				.mesh = testMesh,
				.surfaces = {
					{
						.startIndex = 0,
						.count = 6
					}
				},
			};

			std::vector<kvk::MeshAsset> test {
				ass
			};
			if(kvk::recordCommandBuffer(frame.commandBuffer,
										state.drawImage,
										state.depthImage,
										state.swapchainExtent,
										state.swapchainImages[imageIndex],
										state.drawImageDescriptors,
										pipeline,
										meshPipeline,
										meshes) != kvk::ReturnCode::OK) {
				ShowWindow(window, SW_HIDE);
				logError("Could not record commandBuffer");
				ExitProcess(0);
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
				ExitProcess(0);
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
				ExitProcess(0);
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
