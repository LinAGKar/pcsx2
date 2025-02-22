/*  PCSX2 - PS2 Emulator for PCs
 *  Copyright (C) 2002-2022  PCSX2 Dev Team
 *
 *  PCSX2 is free software: you can redistribute it and/or modify it under the terms
 *  of the GNU Lesser General Public License as published by the Free Software Found-
 *  ation, either version 3 of the License, or (at your option) any later version.
 *
 *  PCSX2 is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 *  without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 *  PURPOSE.  See the GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along with PCSX2.
 *  If not, see <http://www.gnu.org/licenses/>.
 */

#include "PrecompiledHeader.h"

#include "InterfaceSettingsWidget.h"
#include "MainWindow.h"
#include "SettingWidgetBinder.h"
#include "SettingsDialog.h"

static const char* THEME_NAMES[] = {QT_TRANSLATE_NOOP("InterfaceSettingsWidget", "Native"),
	QT_TRANSLATE_NOOP("InterfaceSettingsWidget", "Fusion"),
	QT_TRANSLATE_NOOP("InterfaceSettingsWidget", "Dark Fusion (Gray)"),
	QT_TRANSLATE_NOOP("InterfaceSettingsWidget", "Dark Fusion (Blue)"), nullptr};

static const char* THEME_VALUES[] = {"", "fusion", "darkfusion", "darkfusionblue", nullptr};

InterfaceSettingsWidget::InterfaceSettingsWidget(QWidget* parent, SettingsDialog* dialog)
	: QWidget(parent)
{
	m_ui.setupUi(this);

	SettingWidgetBinder::BindWidgetToBoolSetting(m_ui.inhibitScreensaver, "UI", "InhibitScreensaver", true);
	SettingWidgetBinder::BindWidgetToBoolSetting(m_ui.discordPresence, "UI", "DiscordPresence", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(m_ui.confirmPowerOff, "UI", "ConfirmPowerOff", true);
	SettingWidgetBinder::BindWidgetToBoolSetting(m_ui.saveStateOnExit, "EmuCore", "AutoStateLoadSave", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(m_ui.pauseOnStart, "UI", "StartPaused", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(m_ui.pauseOnFocusLoss, "UI", "PauseOnFocusLoss", false);

	SettingWidgetBinder::BindWidgetToBoolSetting(m_ui.startFullscreen, "UI", "StartFullscreen", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(m_ui.doubleClickTogglesFullscreen, "UI", "DoubleClickTogglesFullscreen",
		true);
	SettingWidgetBinder::BindWidgetToBoolSetting(m_ui.hideMouseCursor, "UI", "HideMouseCursor", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(m_ui.renderToMainWindow, "UI", "RenderToMainWindow", true);

	SettingWidgetBinder::BindWidgetToEnumSetting(m_ui.theme, "UI", "Theme", THEME_NAMES, THEME_VALUES,
		MainWindow::DEFAULT_THEME_NAME);
	connect(m_ui.theme, QOverload<int>::of(&QComboBox::currentIndexChanged), [this]() { emit themeChanged(); });

	dialog->registerWidgetHelp(
		m_ui.inhibitScreensaver, tr("Inhibit Screensaver"), tr("Checked"),
		tr("Prevents the screen saver from activating and the host from sleeping while emulation is running."));

	dialog->registerWidgetHelp(m_ui.discordPresence, tr("Enable Discord Presence"), tr("Unchecked"),
		tr("Shows the game you are currently playing as part of your profile in Discord."));
	if (true)
	{
		SettingWidgetBinder::BindWidgetToBoolSetting(m_ui.autoUpdateEnabled, "AutoUpdater", "CheckAtStartup", true);
		dialog->registerWidgetHelp(m_ui.autoUpdateEnabled, tr("Enable Automatic Update Check"), tr("Checked"),
			tr("Automatically checks for updates to the program on startup. Updates can be deferred "
			   "until later or skipped entirely."));

		// m_ui.autoUpdateTag->addItems(AutoUpdaterDialog::getTagList());
		// SettingWidgetBinder::BindWidgetToStringSetting(m_ui.autoUpdateTag, "AutoUpdater", "UpdateTag",
		// AutoUpdaterDialog::getDefaultTag());

		// m_ui.autoUpdateCurrentVersion->setText(tr("%1 (%2)").arg(g_scm_tag_str).arg(g_scm_date_str));
		// connect(m_ui.checkForUpdates, &QPushButton::clicked, [this]() {
		// m_host_interface->getMainWindow()->checkForUpdates(true); });
	}
	else
	{
		m_ui.verticalLayout->removeWidget(m_ui.automaticUpdaterGroup);
		m_ui.automaticUpdaterGroup->hide();
	}

	dialog->registerWidgetHelp(
		m_ui.confirmPowerOff, tr("Confirm Power Off"), tr("Checked"),
		tr("Determines whether a prompt will be displayed to confirm shutting down the emulator/game "
		   "when the hotkey is pressed."));
	dialog->registerWidgetHelp(m_ui.saveStateOnExit, tr("Save State On Exit"), tr("Checked"),
		tr("Automatically saves the emulator state when powering down or exiting. You can then "
		   "resume directly from where you left off next time."));
	dialog->registerWidgetHelp(m_ui.pauseOnStart, tr("Pause On Start"), tr("Unchecked"),
		tr("Pauses the emulator when a game is started."));
	dialog->registerWidgetHelp(m_ui.pauseOnFocusLoss, tr("Pause On Focus Loss"), tr("Unchecked"),
		tr("Pauses the emulator when you minimize the window or switch to another application, "
		   "and unpauses when you switch back."));
	dialog->registerWidgetHelp(m_ui.startFullscreen, tr("Start Fullscreen"), tr("Unchecked"),
		tr("Automatically switches to fullscreen mode when a game is started."));
	dialog->registerWidgetHelp(m_ui.hideMouseCursor, tr("Hide Cursor In Fullscreen"), tr("Checked"),
		tr("Hides the mouse pointer/cursor when the emulator is in fullscreen mode."));
	dialog->registerWidgetHelp(
		m_ui.renderToMainWindow, tr("Render To Main Window"), tr("Checked"),
		tr("Renders the display of the simulated console to the main window of the application, over "
		   "the game list. If unchecked, the display will render in a separate window."));
}

InterfaceSettingsWidget::~InterfaceSettingsWidget() = default;
