#pragma once
#include "CommonPCH.h"


struct CommonSharedConstants {
	static constexpr const wchar_t* XAML_HOST_CLASS_NAME = L"Magpie_XamlHost";

	static constexpr const COLORREF LIGHT_TINT_COLOR = RGB(243, 243, 243);
	static constexpr const COLORREF DARK_TINT_COLOR = RGB(32, 32, 32);

	static constexpr const char* LOG_PATH = "logs\\magpie.log";
	static constexpr const char* CONFIG_PATH = "config\\config.json";
	static constexpr const wchar_t* CONFIG_PATH_W = L"config\\config.json";
	static constexpr const wchar_t* SOURCES_DIR_W = L"sources\\";
	static constexpr const wchar_t* EFFECTS_DIR_W = L"effects\\";
	static constexpr const wchar_t* ASSETS_DIR_W = L"assets\\";
	static constexpr const wchar_t* CACHE_DIR_W = L"cache\\";

	static constexpr const wchar_t* OPTION_MINIMIZE_TO_TRAY_AT_STARTUP = L"-t";

	// 来自 Magpie\resource.h
	static constexpr UINT IDI_APP = 101;

	static constexpr UINT WM_NOTIFY_ICON = WM_USER;
	static constexpr UINT WM_RESTART_AS_ELEVATED = WM_USER + 1;
};