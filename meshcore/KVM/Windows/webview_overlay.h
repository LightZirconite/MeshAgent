#pragma once
#ifndef __KVM_WEBVIEW_OVERLAY_H__
#define __KVM_WEBVIEW_OVERLAY_H__

#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

int kvm_webview_overlay_start(HWND parent, const wchar_t *url);
void kvm_webview_overlay_resize(HWND parent);
void kvm_webview_overlay_make_input_transparent(HWND parent);
void kvm_webview_overlay_stop(void);

#ifdef __cplusplus
}
#endif

#endif
