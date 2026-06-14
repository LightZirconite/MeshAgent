#include "webview_overlay.h"

#include <stdio.h>
#include <wrl.h>
#include <WebView2.h>

using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;

static ComPtr<ICoreWebView2Environment> g_environment;
static ComPtr<ICoreWebView2Controller> g_controller;
static ComPtr<ICoreWebView2> g_webview;
static HWND g_parent = NULL;
static LONG g_generation = 0;
static int g_comInitialized = 0;
static int g_state = 0;
static DWORD g_lastStartAttempt = 0;

static BOOL CALLBACK make_child_transparent(HWND hwnd, LPARAM)
{
	LONG_PTR style = GetWindowLongPtr(hwnd, GWL_EXSTYLE);
	SetWindowLongPtr(hwnd, GWL_EXSTYLE, style | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE);
	return TRUE;
}

static void kvm_webview_overlay_make_input_transparent(HWND parent)
{
	if (parent != NULL) { EnumChildWindows(parent, make_child_transparent, 0); }
}

extern "C" void kvm_webview_overlay_resize(HWND parent)
{
	RECT bounds;
	if (parent == NULL || parent != g_parent || g_controller == NULL) { return; }
	if (GetClientRect(parent, &bounds) != 0) { g_controller->put_Bounds(bounds); }
}

extern "C" void kvm_webview_overlay_stop(void)
{
	InterlockedIncrement(&g_generation);
	if (g_controller != NULL)
	{
		g_controller->put_IsVisible(FALSE);
		g_controller->Close();
	}
	g_webview.Reset();
	g_controller.Reset();
	g_environment.Reset();
	g_parent = NULL;
	g_state = 0;
	if (g_comInitialized != 0)
	{
		CoUninitialize();
		g_comInitialized = 0;
	}
}

extern "C" int kvm_webview_overlay_start(HWND parent, const wchar_t *url)
{
	LONG generation;
	HRESULT hr;
	wchar_t userDataFolder[MAX_PATH];
	wchar_t tempPath[MAX_PATH];

	if (parent == NULL || url == NULL || url[0] == 0) { return 0; }
	kvm_webview_overlay_stop();
	g_lastStartAttempt = GetTickCount();

	hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
	if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) { return 0; }
	g_comInitialized = SUCCEEDED(hr) ? 1 : 0;
	g_parent = parent;
	g_state = 1;
	generation = InterlockedIncrement(&g_generation);

	userDataFolder[0] = 0;
	if (GetTempPathW(MAX_PATH, tempPath) > 0)
	{
		swprintf_s(userDataFolder, MAX_PATH, L"%sMeshAgentWebView2", tempPath);
	}

	hr = CreateCoreWebView2EnvironmentWithOptions(
		NULL,
		userDataFolder[0] != 0 ? userDataFolder : NULL,
		NULL,
		Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
			[parent, url, generation](HRESULT result, ICoreWebView2Environment *environment) -> HRESULT
			{
				if (FAILED(result) || environment == NULL || generation != g_generation ||
					parent != g_parent || IsWindow(parent) == 0)
				{
					if (generation == g_generation) { g_state = 0; }
					return S_OK;
				}

				g_environment = environment;
				return environment->CreateCoreWebView2Controller(
					parent,
					Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
						[parent, url, generation](HRESULT controllerResult, ICoreWebView2Controller *controller) -> HRESULT
						{
							ComPtr<ICoreWebView2Settings> settings;
							EventRegistrationToken navigationToken;
							EventRegistrationToken processFailedToken;
							if (FAILED(controllerResult) || controller == NULL ||
								generation != g_generation || parent != g_parent ||
								IsWindow(parent) == 0)
							{
								if (generation == g_generation) { g_state = 0; }
								return S_OK;
							}

							g_controller = controller;
							if (FAILED(controller->get_CoreWebView2(g_webview.ReleaseAndGetAddressOf())) ||
								g_webview == NULL)
							{
								g_state = 0;
								return S_OK;
							}

							if (SUCCEEDED(g_webview->get_Settings(settings.ReleaseAndGetAddressOf())) &&
								settings != NULL)
							{
								settings->put_AreDefaultContextMenusEnabled(FALSE);
								settings->put_AreDevToolsEnabled(FALSE);
								settings->put_IsStatusBarEnabled(FALSE);
								settings->put_IsZoomControlEnabled(FALSE);
							}

							g_webview->add_NavigationCompleted(
								Callback<ICoreWebView2NavigationCompletedEventHandler>(
									[generation](ICoreWebView2*, ICoreWebView2NavigationCompletedEventArgs *args) -> HRESULT
									{
										BOOL succeeded = FALSE;
										if (generation != g_generation) { return S_OK; }
										if (args != NULL) { args->get_IsSuccess(&succeeded); }
										g_state = succeeded != FALSE ? 2 : 0;
										if (succeeded != FALSE && g_controller != NULL) { g_controller->put_IsVisible(TRUE); }
										return S_OK;
									}).Get(),
								&navigationToken);
							g_webview->add_ProcessFailed(
								Callback<ICoreWebView2ProcessFailedEventHandler>(
									[generation](ICoreWebView2*, ICoreWebView2ProcessFailedEventArgs*) -> HRESULT
									{
										if (generation == g_generation) { g_state = 0; }
										return S_OK;
									}).Get(),
								&processFailedToken);

							kvm_webview_overlay_resize(parent);
							controller->put_IsVisible(FALSE);
							kvm_webview_overlay_make_input_transparent(parent);
							g_webview->Navigate(url);
							return S_OK;
						}).Get());
			}).Get());

	if (FAILED(hr))
	{
		kvm_webview_overlay_stop();
		return 0;
	}
	return 1;
}

extern "C" void kvm_webview_overlay_watchdog(HWND parent, const wchar_t *url)
{
	DWORD now;
	if (parent == NULL || url == NULL || url[0] == 0 || IsWindow(parent) == 0) { return; }

	if (parent == g_parent && g_controller != NULL && g_state != 0)
	{
		g_controller->put_IsVisible(TRUE);
		kvm_webview_overlay_resize(parent);
		kvm_webview_overlay_make_input_transparent(parent);
		return;
	}

	now = GetTickCount();
	if ((now - g_lastStartAttempt) >= 3000)
	{
		kvm_webview_overlay_start(parent, url);
	}
}
