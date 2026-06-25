#include "webview_app.hpp"

#include "webview2_loader_shim.hpp"

#include <shlwapi.h>

#include <algorithm>
#include <string>
#include <vector>

namespace {
const wchar_t kWindowClassName[] = L"OpenCVWebView2Window";
const UINT kMsgWebMessage = WM_APP + 1;

std::wstring TrimJsonWhitespace(const std::wstring& value) {
	size_t start = value.find_first_not_of(L" \t\r\n");
	if (start == std::wstring::npos) {
		return L"";
	}
	size_t end = value.find_last_not_of(L" \t\r\n");
	return value.substr(start, end - start + 1);
}

std::wstring UnquoteJsonString(const std::wstring& json) {
	if (json.size() < 2 || json.front() != L'"' || json.back() != L'"') {
		return json;
	}
	std::wstring out;
	out.reserve(json.size());
	for (size_t i = 1; i + 1 < json.size(); ++i) {
		wchar_t ch = json[i];
		if (ch == L'\\' && i + 1 < json.size() - 1) {
			wchar_t next = json[i + 1];
			if (next == L'"' || next == L'\\' || next == L'/') {
				out.push_back(next);
				++i;
				continue;
			}
		}
		out.push_back(ch);
	}
	return out;
}

bool ExtractJsonBool(const std::wstring& json, const std::wstring& key, bool& out) {
	std::wstring pattern = L"\"" + key + L"\"";
	size_t pos = json.find(pattern);
	if (pos == std::wstring::npos) {
		return false;
	}
	pos = json.find(L':', pos + pattern.size());
	if (pos == std::wstring::npos) {
		return false;
	}
	std::wstring tail = TrimJsonWhitespace(json.substr(pos + 1));
	if (tail.rfind(L"true", 0) == 0) {
		out = true;
		return true;
	}
	if (tail.rfind(L"false", 0) == 0) {
		out = false;
		return true;
	}
	return false;
}

bool ExtractJsonString(const std::wstring& json, const std::wstring& key, std::wstring& out) {
	std::wstring pattern = L"\"" + key + L"\"";
	size_t pos = json.find(pattern);
	if (pos == std::wstring::npos) {
		return false;
	}
	pos = json.find(L':', pos + pattern.size());
	if (pos == std::wstring::npos) {
		return false;
	}
	pos = json.find(L'\"', pos + 1);
	if (pos == std::wstring::npos) {
		return false;
	}
	size_t end = json.find(L'\"', pos + 1);
	if (end == std::wstring::npos) {
		return false;
	}
	out = json.substr(pos + 1, end - pos - 1);
	return true;
}

std::wstring ToWide(const std::string& input, UINT codepage) {
	if (input.empty()) {
		return L"";
	}
	int size = MultiByteToWideChar(codepage, 0, input.c_str(), static_cast<int>(input.size()), nullptr, 0);
	if (size <= 0) {
		return L"";
	}
	std::wstring output(size, L'\0');
	MultiByteToWideChar(codepage, 0, input.c_str(), static_cast<int>(input.size()), output.data(), size);
	return output;
}
}

using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;

int WebViewApp::Run(HINSTANCE instance, int showCmd) {
	WNDCLASSEXW wc = {};
	wc.cbSize = sizeof(wc);
	wc.hInstance = instance;
	wc.lpfnWndProc = WebViewApp::WndProc;
	wc.lpszClassName = kWindowClassName;
	wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
	wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
	RegisterClassExW(&wc);

	hwnd_ = CreateWindowExW(
		0,
		kWindowClassName,
		L"OpenCV Web UI",
		WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT, CW_USEDEFAULT,
		1280, 800,
		nullptr,
		nullptr,
		instance,
		this);

	if (!hwnd_) {
		return 1;
	}

	ShowWindow(hwnd_, showCmd);
	UpdateWindow(hwnd_);

	MSG msg;
	while (GetMessageW(&msg, nullptr, 0, 0)) {
		TranslateMessage(&msg);
		DispatchMessageW(&msg);
	}

	return static_cast<int>(msg.wParam);
}

LRESULT CALLBACK WebViewApp::WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
	WebViewApp* app = nullptr;
	if (msg == WM_NCCREATE) {
		auto createStruct = reinterpret_cast<CREATESTRUCTW*>(lparam);
		app = static_cast<WebViewApp*>(createStruct->lpCreateParams);
		SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
		app->hwnd_ = hwnd;
	} else {
		app = reinterpret_cast<WebViewApp*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
	}

	if (app) {
		return app->HandleMessage(msg, wparam, lparam);
	}
	return DefWindowProcW(hwnd, msg, wparam, lparam);
}

LRESULT WebViewApp::HandleMessage(UINT msg, WPARAM wparam, LPARAM lparam) {
	switch (msg) {
	case WM_CREATE:
		InitWebView();
		return 0;
	case WM_SIZE:
		ResizeWebView();
		return 0;
	case kMsgWebMessage: {
		std::vector<std::wstring> pending;
		{
			std::lock_guard<std::mutex> lock(messageMutex_);
			pending.swap(messageQueue_);
		}
		if (webview_) {
			for (const auto& msgText : pending) {
				webview_->PostWebMessageAsString(msgText.c_str());
			}
		}
		return 0;
	}
	case WM_DESTROY:
		StopCliProcess();
		StopLogTail();
		PostQuitMessage(0);
		return 0;
	default:
		return DefWindowProcW(hwnd_, msg, wparam, lparam);
	}
}

void WebViewApp::InitWebView() {
	CreateCoreWebView2EnvironmentWithOptions(
		nullptr,
		nullptr,
		nullptr,
		Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
			[this](HRESULT result, ICoreWebView2Environment* env) -> HRESULT {
				if (FAILED(result)) {
					MessageBoxW(hwnd_, L"WebView2 environment creation failed.", L"Error", MB_ICONERROR);
					return result;
				}
				return env->CreateCoreWebView2Controller(
					hwnd_,
					Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
						[this](HRESULT result, ICoreWebView2Controller* controller) -> HRESULT {
							if (FAILED(result)) {
								MessageBoxW(hwnd_, L"WebView2 controller creation failed.", L"Error", MB_ICONERROR);
								return result;
							}

							controller_ = controller;
							controller_->get_CoreWebView2(&webview_);
							ResizeWebView();

							EventRegistrationToken token;
							webview_->add_WebMessageReceived(
								Callback<ICoreWebView2WebMessageReceivedEventHandler>(
									[this](ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs* args) -> HRESULT {
										LPWSTR message = nullptr;
										args->get_WebMessageAsJson(&message);
										if (message) {
											std::wstring msg = UnquoteJsonString(message);
											std::wstring type;
											ExtractJsonString(msg, L"type", type);
											if (type == L"stop") {
												StopCliProcess();
												StopLogTail();
												webview_->PostWebMessageAsString(L"CLI stopped.");
												webview_->PostWebMessageAsString(L"status:stopped");
												CoTaskMemFree(message);
												return S_OK;
											}

											std::wstring mode;
											std::wstring view;
											bool grayEnabled = false;
											bool enhanceEnabled = true;
											bool showLeft = true;
											bool showRight = true;
											bool showDisp = true;
											bool showDepth = true;
											ExtractJsonString(msg, L"mode", mode);
											ExtractJsonString(msg, L"view", view);
											ExtractJsonBool(msg, L"gray", grayEnabled);
											if (ExtractJsonBool(msg, L"enhance", enhanceEnabled) == false) {
												enhanceEnabled = true;
											}
											ExtractJsonBool(msg, L"showLeft", showLeft);
											ExtractJsonBool(msg, L"showRight", showRight);
											ExtractJsonBool(msg, L"showDisp", showDisp);
											ExtractJsonBool(msg, L"showDepth", showDepth);
											if (!mode.empty()) {
												webview_->PostWebMessageAsString(L"status:starting");
												StartCliProcess(mode, view, grayEnabled, enhanceEnabled, showLeft, showRight, showDisp, showDepth);
												std::wstring reply = L"CLI launched: mode=" + mode;
												reply += grayEnabled ? L" gray=on" : L" gray=off";
												reply += enhanceEnabled ? L" enhance=on" : L" enhance=off";
												webview_->PostWebMessageAsString(reply.c_str());
											} else {
												webview_->PostWebMessageAsString(L"Invalid message payload.");
												webview_->PostWebMessageAsString(L"status:failed");
											}
											CoTaskMemFree(message);
										}
										return S_OK;
									}).Get(),
								&token);

							std::wstring indexPath = GetWebRoot() + L"\\index.html";
							std::wstring url = ToFileUrl(indexPath);
							webview_->Navigate(url.c_str());
							return S_OK;
						}).Get());
			}).Get());
}

void WebViewApp::ResizeWebView() {
	if (!controller_) {
		return;
	}
	RECT bounds = {};
	GetClientRect(hwnd_, &bounds);
	controller_->put_Bounds(bounds);
}

std::wstring WebViewApp::GetWebRoot() const {
	wchar_t modulePath[MAX_PATH];
	GetModuleFileNameW(nullptr, modulePath, MAX_PATH);
	PathRemoveFileSpecW(modulePath);
	std::wstring path(modulePath);
	path += L"\\web";
	return path;
}

std::wstring WebViewApp::ToFileUrl(const std::wstring& path) const {
	std::wstring url = L"file:///";
	url.reserve(url.size() + path.size());
	for (wchar_t ch : path) {
		url.push_back(ch == L'\\' ? L'/' : ch);
	}
	return url;
}

void WebViewApp::StartCliProcess(const std::wstring& mode, const std::wstring& view, bool grayEnabled, bool enhanceEnabled, bool showLeft, bool showRight, bool showDisp, bool showDepth) {
	StopCliProcess();
	StopLogTail();
	currentMode_ = mode;

	wchar_t modulePath[MAX_PATH];
	GetModuleFileNameW(nullptr, modulePath, MAX_PATH);
	PathRemoveFileSpecW(modulePath);
	std::wstring exePath(modulePath);
	exePath += L"\\opencv_cli.exe";
	std::wstring logPath(modulePath);
	logPath += L"\\opencv_cli.log";
{
		std::lock_guard<std::mutex> lock(logMutex_);
		logPath_ = logPath;
}

	wchar_t workDir[MAX_PATH];
	wcscpy_s(workDir, modulePath);
	PathRemoveFileSpecW(workDir);
	PathRemoveFileSpecW(workDir);

	std::wstring cmd = L"\"" + exePath + L"\" 1 " + mode;
	if (!view.empty() && view != L"both") {
		cmd += L" --view " + view;
	}
	if (grayEnabled) {
		cmd += L" --gray";
	}
	if (!enhanceEnabled) {
		cmd += L" --no-enhance";
	}
	cmd += showLeft ? L" --show-left" : L" --hide-left";
	cmd += showRight ? L" --show-right" : L" --hide-right";
	cmd += showDisp ? L" --show-disp" : L" --hide-disp";
	cmd += showDepth ? L" --show-depth" : L" --hide-depth";

	STARTUPINFOW si = {};
	si.cb = sizeof(si);
	SECURITY_ATTRIBUTES sa = {};
	sa.nLength = sizeof(sa);
	sa.bInheritHandle = TRUE;
	HANDLE logHandle = CreateFileW(
		logPath.c_str(),
		GENERIC_WRITE,
		FILE_SHARE_READ,
		&sa,
		CREATE_ALWAYS,
		FILE_ATTRIBUTE_NORMAL,
		nullptr);
	if (logHandle != INVALID_HANDLE_VALUE) {
		si.dwFlags |= STARTF_USESTDHANDLES;
		si.hStdOutput = logHandle;
		si.hStdError = logHandle;
		si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

		std::wstring dllRelease = std::wstring(modulePath) + L"\\opencv_world4120.dll";
		std::wstring dllDebug = std::wstring(modulePath) + L"\\opencv_world4120d.dll";
		bool hasRelease = GetFileAttributesW(dllRelease.c_str()) != INVALID_FILE_ATTRIBUTES;
		bool hasDebug = GetFileAttributesW(dllDebug.c_str()) != INVALID_FILE_ATTRIBUTES;
		if (webview_) {
			if (hasRelease && hasDebug) {
				webview_->PostWebMessageAsString(L"Warning: both opencv_world4120.dll and opencv_world4120d.dll exist in output folder.");
			} else if (!hasRelease && !hasDebug) {
				webview_->PostWebMessageAsString(L"Warning: no OpenCV runtime DLL found in output folder.");
			}
		}
	}
	PROCESS_INFORMATION pi = {};
	std::vector<wchar_t> cmdBuffer(cmd.begin(), cmd.end());
	cmdBuffer.push_back(L'\0');

	BOOL ok = CreateProcessW(
		exePath.c_str(),
		cmdBuffer.data(),
		nullptr,
		nullptr,
		TRUE,
		CREATE_NO_WINDOW,
		nullptr,
		workDir,
		&si,
		&pi);
	if (logHandle != INVALID_HANDLE_VALUE) {
		CloseHandle(logHandle);
	}
	if (ok) {
		cliProcess_ = pi;
		if (webview_) {
			webview_->PostWebMessageAsString(L"status:running");
		}
		if (mode == L"calib" || mode == L"depth" || mode == L"yolo") {
			StartLogTail(logPath);
		}
	} else if (webview_) {
		webview_->PostWebMessageAsString(L"CLI failed to launch.");
		webview_->PostWebMessageAsString(L"status:failed");
	}
}

void WebViewApp::StopCliProcess() {
	if (cliProcess_.hProcess) {
		TerminateProcess(cliProcess_.hProcess, 0);
		CloseHandle(cliProcess_.hProcess);
		CloseHandle(cliProcess_.hThread);
		cliProcess_.hProcess = nullptr;
		cliProcess_.hThread = nullptr;
	}
}

void WebViewApp::StartLogTail(const std::wstring& logPath) {
	StopLogTail();
	logRunning_.store(true);
	logThread_ = std::thread([this, logPath]() {
		LARGE_INTEGER offset = {};
		std::string buffer;
		while (logRunning_.load()) {
			HANDLE file = CreateFileW(
				logPath.c_str(),
				GENERIC_READ,
				FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
				nullptr,
				OPEN_EXISTING,
				FILE_ATTRIBUTE_NORMAL,
				nullptr);
			if (file == INVALID_HANDLE_VALUE) {
				Sleep(200);
				continue;
			}

			LARGE_INTEGER size = {};
			if (!GetFileSizeEx(file, &size)) {
				CloseHandle(file);
				Sleep(200);
				continue;
			}
			if (size.QuadPart < offset.QuadPart) {
				offset.QuadPart = 0;
			}
			if (size.QuadPart > offset.QuadPart) {
				SetFilePointerEx(file, offset, nullptr, FILE_BEGIN);
				DWORD toRead = static_cast<DWORD>(std::min<LONGLONG>(size.QuadPart - offset.QuadPart, 4096));
				std::vector<char> tmp(toRead);
				DWORD read = 0;
				if (ReadFile(file, tmp.data(), toRead, &read, nullptr) && read > 0) {
					offset.QuadPart += read;
					buffer.append(tmp.data(), tmp.data() + read);
					size_t pos = 0;
					while ((pos = buffer.find('\n')) != std::string::npos) {
						std::string line = buffer.substr(0, pos);
						buffer.erase(0, pos + 1);
						if (!line.empty() && line.back() == '\r') {
							line.pop_back();
						}
						std::wstring wide = ToWide(line, CP_UTF8);
						if (wide.empty()) {
							wide = ToWide(line, CP_ACP);
						}
						if (webview_ && !wide.empty()) {
							if (!wide.empty() && wide[0] == L'[') {
								continue; // skip OpenCV info lines
							}
							std::wstring prefix = (currentMode_ == L"depth") ? L"depth:" : L"calib:";
							std::wstring msg = prefix + wide;
							EnqueueWebMessage(msg);
						}
					}
				}
			}
			CloseHandle(file);
			Sleep(200);
		}
	});
}

void WebViewApp::StopLogTail() {
	logRunning_.store(false);
	if (logThread_.joinable()) {
		logThread_.join();
	}
}

void WebViewApp::EnqueueWebMessage(const std::wstring& message) {
	{
		std::lock_guard<std::mutex> lock(messageMutex_);
		messageQueue_.push_back(message);
	}
	if (hwnd_) {
		PostMessageW(hwnd_, kMsgWebMessage, 0, 0);
	}
}
