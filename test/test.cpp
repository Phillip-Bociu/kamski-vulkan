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
	const kvk::InitSettings initSettings = {
		.appName = "kamki tet",
		.width = 1600,
		.height = 900,
		.window = window
	};

	kvk::RendererState rData;

	kvk::ReturnCode rc = kvk::init(rData, &initSettings);
	if(rc != kvk::ReturnCode::OK) {
		ShowWindow(window, SW_HIDE);
		logError("Init returned: %d", static_cast<int>(rc));
		return 0;
	}
	
	kvk::Pipeline pipeline;
	rc = kvk::createPipeline(rData,
							 pipeline,
							 "shaders/simple_shader.vert.spv",
							 "shaders/simple_shader.frag.spv");
	if(rc != kvk::ReturnCode::OK) {
		ShowWindow(window, SW_HIDE);
		logError("create pipeline did not work: %d", static_cast<int>(rc));
		return 0;
	}

	MSG msg;
	while(true) {
		while (PeekMessage(&msg, 0, 0, 0, PM_REMOVE)) {
			switch (msg.message) {
				case WM_QUIT: {
					ShowWindow(window, SW_HIDE);
					return 0;
				} break;
			}
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
	}

	return 0;
}
