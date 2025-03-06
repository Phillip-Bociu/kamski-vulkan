#include <kvk.h>
#include <cstdint>
#include <Windows.h>
#include <thread>

static LRESULT CALLBACK winProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {

	switch (message) {
		case WM_USER: {
			return 0;
		} break;
		case WM_SIZE: {
			RECT res = {};
			GetClientRect(window, &res);
			std::uint32_t x = res.right;
			std::uint32_t y = res.bottom;
		} break;
		case WM_CLOSE: {
			PostQuitMessage(0);
			return 0;
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
								  "Hopaaaaaa",
								  WS_OVERLAPPEDWINDOW,
								  CW_USEDEFAULT,
								  CW_USEDEFAULT,
								  1600,
								  900,
								  NULL,
								  NULL,
								  instance,
								  NULL);
	ShowWindow(window, SW_SHOW);
	logInfo("Window created");

	std::thread([&]() {
		kvk::RendererState state;
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

		kvk::Pipeline pipeline;
		rc = kvk::createPipeline(state,
								 pipeline,
								 "shaders/simple_shader.vert.spv",
								 "shaders/simple_shader.frag.spv");
		if(rc != kvk::ReturnCode::OK) {
			ShowWindow(window, SW_HIDE);
			logError("create pipeline did not work: %d", static_cast<int>(rc));
			ExitProcess(0);
		}

		std::vector<VkFramebuffer> framebuffers(state.swapchainImages.size());

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
								   &framebuffers[i]) != VK_SUCCESS) {
				logError("Could not create framebuffers");
				ShowWindow(window, SW_HIDE);
				ExitProcess(1);
			}
		}
		logInfo("Created framebuffers");

		while(true) {
			vkWaitForFences(state.device,
							1,
							&state.inFlightFences[currentFrame],
							VK_TRUE,
							std::numeric_limits<std::uint64_t>::max());
			vkResetFences(state.device,
						  1,
						  &state.inFlightFences[currentFrame]);

			std::uint32_t imageIndex;

			vkAcquireNextImageKHR(state.device,
								  state.swapchain,
								  std::numeric_limits<std::uint64_t>::max(),
								  state.imageAvailableSemaphores[currentFrame],
								  VK_NULL_HANDLE,
								  &imageIndex);
			if(recordCommandBuffer(state.commandBuffers[currentFrame],
								   pipeline,
								   framebuffers[imageIndex],
								   state.swapchainExtent) != kvk::ReturnCode::OK) {
				ShowWindow(window, SW_HIDE);
				logError("Could not record commandBuffer");
				ExitProcess(0);
			}

			VkSemaphore waitSemaphores[] = {
				state.imageAvailableSemaphores[currentFrame]
			};

			VkSemaphore signalSemaphores[] = {
				state.renderFinishedSemaphores[currentFrame]
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
				.pCommandBuffers = &state.commandBuffers[currentFrame],
				.signalSemaphoreCount = 1,
				.pSignalSemaphores = signalSemaphores
			};

			if(vkQueueSubmit(state.graphicsQueue,
							 1,
							 &submitInfo,
							 state.inFlightFences[currentFrame]) != VK_SUCCESS) {
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

			vkQueuePresentKHR(state.presentQueue,
							  &presentInfo);
			currentFrame = (currentFrame + 1) % kvk::MAX_IN_FLIGHT_FRAMES;
		}
	}).detach();

	MSG msg;
	while(true) {
		while (PeekMessage(&msg, 0, 0, 0, PM_REMOVE)) {
			switch (msg.message) {
				case WM_QUIT: {
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
