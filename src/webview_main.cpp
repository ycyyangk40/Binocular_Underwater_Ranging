#include "webview_app.hpp"

#include <Windows.h>

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCmd) {
	HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
	if (FAILED(hr)) {
		return 1;
	}

	WebViewApp app;
	int result = app.Run(instance, showCmd);

	CoUninitialize();
	return result;
}
