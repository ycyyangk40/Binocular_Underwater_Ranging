#pragma once

#include <Windows.h>
#include <wrl.h>

#include <WebView2.h>

#include <atomic>
#include <mutex>
#include <thread>
#include <string>
#include <vector>

class WebViewApp {
public:
	int Run(HINSTANCE instance, int showCmd);

private:
	static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
	LRESULT HandleMessage(UINT msg, WPARAM wparam, LPARAM lparam);
	void InitWebView();
	void ResizeWebView();
	std::wstring GetWebRoot() const;
	std::wstring ToFileUrl(const std::wstring& path) const;
	void StartCliProcess(const std::wstring& mode, const std::wstring& view, bool grayEnabled, bool enhanceEnabled, bool showLeft, bool showRight, bool showDisp, bool showDepth);
	void StopCliProcess();
	void StartLogTail(const std::wstring& logPath);
	void StopLogTail();
	void EnqueueWebMessage(const std::wstring& message);

	HWND hwnd_ = nullptr;
	Microsoft::WRL::ComPtr<ICoreWebView2Controller> controller_;
	Microsoft::WRL::ComPtr<ICoreWebView2> webview_;
	PROCESS_INFORMATION cliProcess_ = {};
	std::thread logThread_;
	std::atomic<bool> logRunning_{false};
	std::mutex logMutex_;
	std::wstring logPath_;
	std::wstring currentMode_;
	std::mutex messageMutex_;
	std::vector<std::wstring> messageQueue_;
};
