#pragma once

#include <Windows.h>
#include <WebView2.h>

EXTERN_C HRESULT __stdcall CreateCoreWebView2EnvironmentWithOptions(
	PCWSTR browserExecutableFolder,
	PCWSTR userDataFolder,
	ICoreWebView2EnvironmentOptions* environmentOptions,
	ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler* environmentCreatedHandler);
