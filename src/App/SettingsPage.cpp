#include "pch.h"
#include "SettingsPage.h"
#if __has_include("SettingsPage.g.cpp")
#include "SettingsPage.g.cpp"
#endif
#include "MainPage.h"
#include "Utils.h"


using namespace winrt;
using namespace Windows::UI::Xaml;
using namespace Windows::UI::Xaml::Controls;
using namespace Windows::UI::Xaml::Input;


namespace winrt::Magpie::implementation {

SettingsPage::SettingsPage() {
	InitializeComponent();

	_settings = Application::Current().as<Magpie::App>().Settings();

	PortableModeToggleSwitch().IsOn(_settings.IsPortableMode());
	ThemeComboBox().SelectedIndex(_settings.Theme());
}

void SettingsPage::ThemeComboBox_SelectionChanged(IInspectable const&, SelectionChangedEventArgs const&) {
	_settings.Theme(ThemeComboBox().SelectedIndex());
}

void SettingsPage::PortableModeToggleSwitch_Toggled(IInspectable const&, RoutedEventArgs const&) {
	_settings.IsPortableMode(PortableModeToggleSwitch().IsOn());
}

void SettingsPage::ComboBox_DropDownOpened(IInspectable const&, IInspectable const&) {
	Utils::UpdateThemeOfXamlPopups(XamlRoot(), ActualTheme());
}

void SettingsPage::ScrollViewer_PointerPressed(IInspectable const&, PointerRoutedEventArgs const&) {
	ThemeComboBox().IsDropDownOpen(false);
}

}
