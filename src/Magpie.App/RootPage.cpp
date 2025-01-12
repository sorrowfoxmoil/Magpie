#include "pch.h"
#include "RootPage.h"
#if __has_include("RootPage.g.cpp")
#include "RootPage.g.cpp"
#endif

#include "XamlUtils.h"
#include "Logger.h"
#include "Win32Utils.h"
#include "ProfileService.h"
#include "AppXReader.h"
#include "IconHelper.h"
#include "ComboBoxHelper.h"
#include "CommonSharedConstants.h"
#include "ContentDialogHelper.h"
#include "LocalizationService.h"

using namespace winrt;
using namespace Windows::Graphics::Display;
using namespace Windows::Graphics::Imaging;
using namespace Windows::UI::ViewManagement;
using namespace Windows::UI;
using namespace Windows::UI::Xaml::Controls;
using namespace Windows::UI::Xaml::Controls::Primitives;
using namespace Windows::UI::Xaml::Input;
using namespace Windows::UI::Xaml::Media::Imaging;

namespace winrt::Magpie::App::implementation {

static constexpr uint32_t FIRST_PROFILE_ITEM_IDX = 4;

RootPage::RootPage() {
	_themeChangedRevoker = AppSettings::Get().ThemeChanged(
		auto_revoke, { this, &RootPage::_AppSettings_ThemeChanged });
	_UpdateColorValuesChangedRevoker();

	_displayInformation = DisplayInformation::GetForCurrentView();
	_dpiChangedRevoker = _displayInformation.DpiChanged(
		auto_revoke, [this](DisplayInformation const&, IInspectable const&) { _UpdateIcons(false); });

	ProfileService& profileService = ProfileService::Get();
	_profileAddedRevoker = profileService.ProfileAdded(
		auto_revoke, { this, &RootPage::_ProfileService_ProfileAdded });
	_profileRenamedRevoker = profileService.ProfileRenamed(
		auto_revoke, { this, &RootPage::_ProfileService_ProfileRenamed });
	_profileRemovedRevoker = profileService.ProfileRemoved(
		auto_revoke, { this, &RootPage::_ProfileService_ProfileRemoved });
	_profileMovedRevoker = profileService.ProfileMoved(
		auto_revoke, { this, &RootPage::_ProfileService_ProfileReordered });

	// 设置 Language 属性帮助 XAML 选择合适的字体，比如繁体中文使用 Microsoft JhengHei UI，日语使用 Yu Gothic UI
	Language(LocalizationService::Get().Language());
}

RootPage::~RootPage() {
	ContentDialogHelper::CloseActiveDialog();

	// 不手动置空会内存泄露
	// 似乎是 XAML Islands 的 bug？
	ContentFrame().Content(nullptr);

	// 每次主窗口关闭都清理 AppXReader 的缓存
	AppXReader::ClearCache();
}

void RootPage::InitializeComponent() {
	RootPageT::InitializeComponent();

	_UpdateTheme(false);

	const Win32Utils::OSVersion& osVersion = Win32Utils::GetOSVersion();
	if (osVersion.Is22H2OrNewer()) {
		// Win11 22H2+ 使用系统的 Mica 背景
		MUXC::BackdropMaterial::SetApplyToRootOrPageBackground(*this, true);
	}

	IVector<IInspectable> navMenuItems = RootNavigationView().MenuItems();
	for (const Profile& profile : AppSettings::Get().Profiles()) {
		MUXC::NavigationViewItem item;
		item.Content(box_value(profile.name));
		// 用于占位
		item.Icon(FontIcon());
		_LoadIcon(item, profile);

		navMenuItems.InsertAt(navMenuItems.Size() - 1, item);
	}
}

void RootPage::Loaded(IInspectable const&, RoutedEventArgs const&) {
	// 消除焦点框
	IsTabStop(true);
	Focus(FocusState::Programmatic);
	IsTabStop(false);

	// 设置 NavigationView 内的 Tooltip 的主题
	XamlUtils::UpdateThemeOfTooltips(RootNavigationView(), ActualTheme());
}

void RootPage::NavigationView_SelectionChanged(
	MUXC::NavigationView const&,
	MUXC::NavigationViewSelectionChangedEventArgs const& args
) {
	auto contentFrame = ContentFrame();

	if (args.IsSettingsSelected()) {
		contentFrame.Navigate(xaml_typename<SettingsPage>());
	} else {
		IInspectable selectedItem = args.SelectedItem();
		if (!selectedItem) {
			contentFrame.Content(nullptr);
			return;
		}

		IInspectable tag = selectedItem.as<MUXC::NavigationViewItem>().Tag();
		if (tag) {
			hstring tagStr = unbox_value<hstring>(tag);
			Interop::TypeName typeName;
			if (tagStr == L"Home") {
				typeName = xaml_typename<HomePage>();
			} else if (tagStr == L"ScalingModes") {
				typeName = xaml_typename<ScalingModesPage>();
			} else if (tagStr == L"About") {
				typeName = xaml_typename<AboutPage>();
			} else {
				typeName = xaml_typename<HomePage>();
			}

			contentFrame.Navigate(typeName);
		} else {
			// 缩放配置页面
			MUXC::NavigationView nv = RootNavigationView();
			uint32_t index;
			if (nv.MenuItems().IndexOf(nv.SelectedItem(), index)) {
				contentFrame.Navigate(xaml_typename<ProfilePage>(), box_value((int)index - 4));
			}
		}
	}
}

void RootPage::NavigationView_PaneOpening(MUXC::NavigationView const&, IInspectable const&) {
	if (Win32Utils::GetOSVersion().IsWin11()) {
		// Win11 中 Tooltip 自动适应主题
		return;
	}

	XamlUtils::UpdateThemeOfTooltips(*this, ActualTheme());

	// UpdateThemeOfTooltips 中使用的 hack 会使 NavigationViewItem 在展开时不会自动删除 Tooltip
	// 因此这里手动删除
	const MUXC::NavigationView& nv = RootNavigationView();
	for (const IInspectable& item : nv.MenuItems()) {
		ToolTipService::SetToolTip(item.as<DependencyObject>(), nullptr);
	}
	for (const IInspectable& item : nv.FooterMenuItems()) {
		ToolTipService::SetToolTip(item.as<DependencyObject>(), nullptr);
	}
}

void RootPage::NavigationView_PaneClosing(MUXC::NavigationView const&, MUXC::NavigationViewPaneClosingEventArgs const&) {
	XamlUtils::UpdateThemeOfTooltips(*this, ActualTheme());
}

void RootPage::NavigationView_DisplayModeChanged(MUXC::NavigationView const& nv, MUXC::NavigationViewDisplayModeChangedEventArgs const&) {
	bool isExpanded = nv.DisplayMode() == MUXC::NavigationViewDisplayMode::Expanded;
	nv.IsPaneToggleButtonVisible(!isExpanded);
	if (isExpanded) {
		nv.IsPaneOpen(true);
	}

	// !!! HACK !!!
	// 使导航栏的可滚动区域不会覆盖标题栏
	FrameworkElement menuItemsScrollViewer = nv.as<IControlProtected>()
		.GetTemplateChild(L"MenuItemsScrollViewer").as<FrameworkElement>();
	menuItemsScrollViewer.Margin({ 0,isExpanded ? TitleBar().ActualHeight() : 0.0,0,0});

	XamlUtils::UpdateThemeOfTooltips(*this, ActualTheme());
}

fire_and_forget RootPage::NavigationView_ItemInvoked(MUXC::NavigationView const&, MUXC::NavigationViewItemInvokedEventArgs const& args) {
	if (args.InvokedItemContainer() == NewProfileNavigationViewItem()) {
		const UINT dpi = (UINT)std::lroundf(_displayInformation.LogicalDpi());
		const bool isLightTheme = ActualTheme() == ElementTheme::Light;
		_newProfileViewModel.PrepareForOpen(dpi, isLightTheme, Dispatcher());

		// 同步调用 ShowAt 有时会失败
		co_await Dispatcher();

		NewProfileFlyout().ShowAt(NewProfileNavigationViewItem());
	}
}

void RootPage::ComboBox_DropDownOpened(IInspectable const&, IInspectable const&) const {
	XamlUtils::UpdateThemeOfXamlPopups(XamlRoot(), ActualTheme());
}

void RootPage::NewProfileConfirmButton_Click(IInspectable const&, RoutedEventArgs const&) {
	_newProfileViewModel.Confirm();
	NewProfileFlyout().Hide();
}

void RootPage::NewProfileNameTextBox_KeyDown(IInspectable const&, Input::KeyRoutedEventArgs const& args) {
	if (args.Key() == VirtualKey::Enter && _newProfileViewModel.IsConfirmButtonEnabled()) {
		NewProfileConfirmButton_Click(nullptr, nullptr);
	}
}

void RootPage::NavigateToAboutPage() {
	MUXC::NavigationView nv = RootNavigationView();
	nv.SelectedItem(nv.FooterMenuItems().GetAt(0));
}

static Color Win32ColorToWinRTColor(COLORREF color) {
	return { 255, GetRValue(color), GetGValue(color), GetBValue(color) };
}

void RootPage::_UpdateTheme(bool updateIcons) {
	Theme theme = AppSettings::Get().Theme();

	bool isDarkTheme = FALSE;
	if (theme == Theme::System) {
		// 前景色是亮色表示当前是深色主题
		isDarkTheme = XamlUtils::IsColorLight(_uiSettings.GetColorValue(UIColorType::Foreground));
	} else {
		isDarkTheme = theme == Theme::Dark;
	}

	if (IsLoaded() && (ActualTheme() == ElementTheme::Dark) == isDarkTheme) {
		// 无需切换
		return;
	}

	if (!Win32Utils::GetOSVersion().Is22H2OrNewer()) {
		const Windows::UI::Color bkgColor = Win32ColorToWinRTColor(
			isDarkTheme ? CommonSharedConstants::DARK_TINT_COLOR : CommonSharedConstants::LIGHT_TINT_COLOR);
		Background(SolidColorBrush(bkgColor));
	}

	ElementTheme newTheme = isDarkTheme ? ElementTheme::Dark : ElementTheme::Light;
	RequestedTheme(newTheme);

	XamlUtils::UpdateThemeOfXamlPopups(XamlRoot(), newTheme);
	XamlUtils::UpdateThemeOfTooltips(*this, newTheme);

	if (updateIcons && IsLoaded()) {
		_UpdateIcons(true);
	}
}

fire_and_forget RootPage::_LoadIcon(MUXC::NavigationViewItem const& item, const Profile& profile) {
	weak_ref<MUXC::NavigationViewItem> weakRef(item);

	bool preferLightTheme = ActualTheme() == ElementTheme::Light;
	bool isPackaged = profile.isPackaged;
	std::wstring path = profile.pathRule;
	CoreDispatcher dispatcher = Dispatcher();
	const uint32_t iconSize = (uint32_t)std::lroundf(16 * _displayInformation.LogicalDpi() / USER_DEFAULT_SCREEN_DPI);

	co_await resume_background();

	std::wstring iconPath;
	SoftwareBitmap iconBitmap{ nullptr };

	if (isPackaged) {
		AppXReader reader;
		if (reader.Initialize(path)) {
			std::variant<std::wstring, SoftwareBitmap> uwpIcon =
				reader.GetIcon(iconSize, preferLightTheme);
			if (uwpIcon.index() == 0) {
				iconPath = std::get<0>(uwpIcon);
			} else {
				iconBitmap = std::get<1>(uwpIcon);
			}
		}
	} else {
		iconBitmap = IconHelper::ExtractIconFromExe(path.c_str(), iconSize);
	}

	co_await dispatcher;

	auto strongRef = weakRef.get();
	if (!strongRef) {
		co_return;
	}

	if (!iconPath.empty()) {
		BitmapIcon icon;
		icon.ShowAsMonochrome(false);
		icon.UriSource(Uri(iconPath));
		icon.Width(16);
		icon.Height(16);

		strongRef.Icon(icon);
	} else if (iconBitmap) {
		SoftwareBitmapSource imageSource;
		co_await imageSource.SetBitmapAsync(iconBitmap);

		MUXC::ImageIcon imageIcon;
		imageIcon.Width(16);
		imageIcon.Height(16);
		imageIcon.Source(imageSource);

		strongRef.Icon(imageIcon);
	} else {
		FontIcon icon;
		icon.Glyph(L"\uECAA");
		strongRef.Icon(icon);
	}
}

void RootPage::_UpdateColorValuesChangedRevoker() {
	if (AppSettings::Get().Theme() == Theme::System) {
		_colorValuesChangedRevoker = _uiSettings.ColorValuesChanged(
			auto_revoke, { this, &RootPage::_UISettings_ColorValuesChanged });
	} else {
		_colorValuesChangedRevoker.revoke();
	}
}

void RootPage::_UISettings_ColorValuesChanged(Windows::UI::ViewManagement::UISettings const&, IInspectable const&) {
	Dispatcher().RunAsync(CoreDispatcherPriority::Normal, [weakThis(get_weak())]() {
		if (auto strongThis = weakThis.get()) {
			strongThis->_UpdateTheme(true);
		}
	});
}

void RootPage::_AppSettings_ThemeChanged(Theme) {
	_UpdateColorValuesChangedRevoker();
	_UpdateTheme(true);
}

void RootPage::_UpdateIcons(bool skipDesktop) {
	IVector<IInspectable> navMenuItems = RootNavigationView().MenuItems();
	const std::vector<Profile>& profiles = AppSettings::Get().Profiles();

	for (uint32_t i = 0; i < profiles.size(); ++i) {
		if (skipDesktop && !profiles[i].isPackaged) {
			continue;
		}

		MUXC::NavigationViewItem item = navMenuItems.GetAt(FIRST_PROFILE_ITEM_IDX + i).as<MUXC::NavigationViewItem>();
		_LoadIcon(item, profiles[i]);
	}
}

void RootPage::_ProfileService_ProfileAdded(Profile& profile) {
	MUXC::NavigationViewItem item;
	item.Content(box_value(profile.name));
	// 用于占位
	item.Icon(FontIcon());
	_LoadIcon(item, profile);

	IVector<IInspectable> navMenuItems = RootNavigationView().MenuItems();
	navMenuItems.InsertAt(navMenuItems.Size() - 1, item);
	RootNavigationView().SelectedItem(item);
}

void RootPage::_ProfileService_ProfileRenamed(uint32_t idx) {
	RootNavigationView().MenuItems()
		.GetAt(FIRST_PROFILE_ITEM_IDX + idx)
		.as<MUXC::NavigationViewItem>()
		.Content(box_value(AppSettings::Get().Profiles()[idx].name));
}

void RootPage::_ProfileService_ProfileRemoved(uint32_t idx) {
	MUXC::NavigationView nv = RootNavigationView();
	IVector<IInspectable> menuItems = nv.MenuItems();
	nv.SelectedItem(menuItems.GetAt(FIRST_PROFILE_ITEM_IDX - 1));
	menuItems.RemoveAt(FIRST_PROFILE_ITEM_IDX + idx);
}

void RootPage::_ProfileService_ProfileReordered(uint32_t profileIdx, bool isMoveUp) {
	IVector<IInspectable> menuItems = RootNavigationView().MenuItems();

	uint32_t curIdx = FIRST_PROFILE_ITEM_IDX + profileIdx;
	uint32_t otherIdx = isMoveUp ? curIdx - 1 : curIdx + 1;
	
	IInspectable otherItem = menuItems.GetAt(otherIdx);
	menuItems.RemoveAt(otherIdx);
	menuItems.InsertAt(curIdx, otherItem);
}

}
