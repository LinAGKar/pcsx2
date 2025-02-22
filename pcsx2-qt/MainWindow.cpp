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

#include <QtCore/QDateTime>
#include <QtWidgets/QFileDialog>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QProgressBar>
#include <QtWidgets/QStyle>
#include <QtWidgets/QStyleFactory>

#include "common/FileSystem.h"

#include "pcsx2/CDVD/CDVDaccess.h"
#include "pcsx2/Frontend/GameList.h"
#include "pcsx2/HostDisplay.h"

#include "AboutDialog.h"
#include "DisplayWidget.h"
#include "EmuThread.h"
#include "GameList/GameListRefreshThread.h"
#include "GameList/GameListWidget.h"
#include "MainWindow.h"
#include "QtHost.h"
#include "QtUtils.h"
#include "Settings/ControllerSettingsDialog.h"
#include "Settings/GameListSettingsWidget.h"
#include "Settings/InterfaceSettingsWidget.h"
#include "svnrev.h"

static constexpr char DISC_IMAGE_FILTER[] = QT_TRANSLATE_NOOP(
	"MainWindow", "All File Types (*.bin *.iso *.cue *.chd *.cso *.elf *.irx *.m3u);;Single-Track Raw Images (*.bin "
				  "*.iso);;Cue Sheets (*.cue);;MAME CHD Images (*.chd);;CSO Images (*.cso);;"
				  "ELF Executables (*.elf);;IRX Executables (*.irx);;Playlists (*.m3u)");

const char* MainWindow::DEFAULT_THEME_NAME = "darkfusion";

MainWindow* g_main_window = nullptr;

MainWindow::MainWindow(const QString& unthemed_style_name)
	: m_unthemed_style_name(unthemed_style_name)
{
	pxAssert(!g_main_window);
	g_main_window = this;
}

MainWindow::~MainWindow()
{
	// we compare here, since recreate destroys the window later
	if (g_main_window == this)
		g_main_window = nullptr;
}

void MainWindow::initialize()
{
	setIconThemeFromSettings();
	m_ui.setupUi(this);
	setupAdditionalUi();
	setStyleFromSettings();
	connectSignals();

	restoreStateFromConfig();
	switchToGameListView();
	updateWindowTitle();
	updateSaveStateMenus(QString(), QString(), 0);
}

void MainWindow::setupAdditionalUi()
{
	const bool toolbar_visible = QtHost::GetBaseBoolSettingValue("UI", "ShowToolbar", false);
	m_ui.actionViewToolbar->setChecked(toolbar_visible);
	m_ui.toolBar->setVisible(toolbar_visible);

	const bool toolbars_locked = QtHost::GetBaseBoolSettingValue("UI", "LockToolbar", false);
	m_ui.actionViewLockToolbar->setChecked(toolbars_locked);
	m_ui.toolBar->setMovable(!toolbars_locked);
	m_ui.toolBar->setContextMenuPolicy(Qt::PreventContextMenu);

	const bool status_bar_visible = QtHost::GetBaseBoolSettingValue("UI", "ShowStatusBar", true);
	m_ui.actionViewStatusBar->setChecked(status_bar_visible);
	m_ui.statusBar->setVisible(status_bar_visible);

	m_game_list_widget = new GameListWidget(m_ui.mainContainer);
	m_game_list_widget->initialize();
	m_ui.mainContainer->insertWidget(0, m_game_list_widget);
	m_ui.mainContainer->setCurrentIndex(0);
	m_ui.actionGridViewShowTitles->setChecked(m_game_list_widget->getShowGridCoverTitles());

	m_status_progress_widget = new QProgressBar(m_ui.statusBar);
	m_status_progress_widget->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
	m_status_progress_widget->setFixedSize(140, 16);
	m_status_progress_widget->hide();

	for (u32 scale = 0; scale <= 10; scale++)
	{
		QAction* action =
			m_ui.menuWindowSize->addAction((scale == 0) ? tr("Internal Resolution") : tr("%1x Scale").arg(scale));
		connect(action, &QAction::triggered, [scale]() { g_emu_thread->requestDisplaySize(static_cast<float>(scale)); });
	}

	updateEmulationActions(false, false);
}

void MainWindow::connectSignals()
{
	connect(m_ui.actionStartFile, &QAction::triggered, this, &MainWindow::onStartFileActionTriggered);
	connect(m_ui.actionStartBios, &QAction::triggered, this, &MainWindow::onStartBIOSActionTriggered);
	connect(m_ui.actionChangeDisc, &QAction::triggered, [this] { m_ui.menuChangeDisc->exec(QCursor::pos()); });
	connect(m_ui.actionChangeDiscFromFile, &QAction::triggered, this, &MainWindow::onChangeDiscFromFileActionTriggered);
	connect(m_ui.actionChangeDiscFromDevice, &QAction::triggered, this,
		&MainWindow::onChangeDiscFromDeviceActionTriggered);
	connect(m_ui.actionChangeDiscFromGameList, &QAction::triggered, this,
		&MainWindow::onChangeDiscFromGameListActionTriggered);
	connect(m_ui.menuChangeDisc, &QMenu::aboutToShow, this, &MainWindow::onChangeDiscMenuAboutToShow);
	connect(m_ui.menuChangeDisc, &QMenu::aboutToHide, this, &MainWindow::onChangeDiscMenuAboutToHide);
	connect(m_ui.actionPowerOff, &QAction::triggered, []() { g_emu_thread->shutdownVM(); });
	connect(m_ui.actionLoadState, &QAction::triggered, this, [this]() { m_ui.menuLoadState->exec(QCursor::pos()); });
	connect(m_ui.actionSaveState, &QAction::triggered, this, [this]() { m_ui.menuSaveState->exec(QCursor::pos()); });
	connect(m_ui.actionExit, &QAction::triggered, this, &MainWindow::close);
	connect(m_ui.menuLoadState, &QMenu::aboutToShow, this, &MainWindow::onLoadStateMenuAboutToShow);
	connect(m_ui.menuSaveState, &QMenu::aboutToShow, this, &MainWindow::onSaveStateMenuAboutToShow);
	connect(m_ui.actionSettings, &QAction::triggered, [this]() { doSettings(SettingsDialog::Category::Count); });
	connect(m_ui.actionInterfaceSettings, &QAction::triggered,
		[this]() { doSettings(SettingsDialog::Category::InterfaceSettings); });
	connect(m_ui.actionGameListSettings, &QAction::triggered,
		[this]() { doSettings(SettingsDialog::Category::GameListSettings); });
	connect(m_ui.actionEmulationSettings, &QAction::triggered,
		[this]() { doSettings(SettingsDialog::Category::EmulationSettings); });
	connect(m_ui.actionBIOSSettings, &QAction::triggered,
		[this]() { doSettings(SettingsDialog::Category::BIOSSettings); });
	connect(m_ui.actionSystemSettings, &QAction::triggered,
		[this]() { doSettings(SettingsDialog::Category::SystemSettings); });
	connect(m_ui.actionGraphicsSettings, &QAction::triggered,
		[this]() { doSettings(SettingsDialog::Category::GraphicsSettings); });
	connect(m_ui.actionAudioSettings, &QAction::triggered,
		[this]() { doSettings(SettingsDialog::Category::AudioSettings); });
	connect(m_ui.actionMemoryCardSettings, &QAction::triggered,
		[this]() { doSettings(SettingsDialog::Category::MemoryCardSettings); });
	connect(m_ui.actionControllerSettings, &QAction::triggered,
		[this]() { doControllerSettings(ControllerSettingsDialog::Category::GlobalSettings); });
	connect(m_ui.actionHotkeySettings, &QAction::triggered,
		[this]() { doControllerSettings(ControllerSettingsDialog::Category::HotkeySettings); });
	connect(m_ui.actionAddGameDirectory, &QAction::triggered,
		[this]() { getSettingsDialog()->getGameListSettingsWidget()->addSearchDirectory(this); });
	connect(m_ui.actionScanForNewGames, &QAction::triggered, [this]() { refreshGameList(false); });
	connect(m_ui.actionRescanAllGames, &QAction::triggered, [this]() { refreshGameList(true); });
	connect(m_ui.actionViewToolbar, &QAction::toggled, this, &MainWindow::onViewToolbarActionToggled);
	connect(m_ui.actionViewLockToolbar, &QAction::toggled, this, &MainWindow::onViewLockToolbarActionToggled);
	connect(m_ui.actionViewStatusBar, &QAction::toggled, this, &MainWindow::onViewStatusBarActionToggled);
	connect(m_ui.actionViewGameList, &QAction::triggered, this, &MainWindow::onViewGameListActionTriggered);
	connect(m_ui.actionViewGameGrid, &QAction::triggered, this, &MainWindow::onViewGameGridActionTriggered);
	connect(m_ui.actionViewSystemDisplay, &QAction::triggered, this, &MainWindow::onViewSystemDisplayTriggered);
	connect(m_ui.actionViewGameProperties, &QAction::triggered, this, &MainWindow::onViewGamePropertiesActionTriggered);
	connect(m_ui.actionGitHubRepository, &QAction::triggered, this, &MainWindow::onGitHubRepositoryActionTriggered);
	connect(m_ui.actionSupportForums, &QAction::triggered, this, &MainWindow::onSupportForumsActionTriggered);
	connect(m_ui.actionDiscordServer, &QAction::triggered, this, &MainWindow::onDiscordServerActionTriggered);
	connect(m_ui.actionAboutQt, &QAction::triggered, qApp, &QApplication::aboutQt);
	connect(m_ui.actionAbout, &QAction::triggered, this, &MainWindow::onAboutActionTriggered);
	connect(m_ui.actionCheckForUpdates, &QAction::triggered, this, &MainWindow::onCheckForUpdatesActionTriggered);
	connect(m_ui.actionOpenDataDirectory, &QAction::triggered, this, &MainWindow::onToolsOpenDataDirectoryTriggered);
	connect(m_ui.actionGridViewShowTitles, &QAction::triggered, m_game_list_widget, &GameListWidget::setShowCoverTitles);
	connect(m_ui.actionGridViewZoomIn, &QAction::triggered, m_game_list_widget, [this]() {
		if (isShowingGameList())
			m_game_list_widget->gridZoomIn();
	});
	connect(m_ui.actionGridViewZoomOut, &QAction::triggered, m_game_list_widget, [this]() {
		if (isShowingGameList())
			m_game_list_widget->gridZoomOut();
	});
	connect(m_ui.actionGridViewRefreshCovers, &QAction::triggered, m_game_list_widget,
		&GameListWidget::refreshGridCovers);

	// These need to be queued connections to stop crashing due to menus opening/closing and switching focus.
	connect(m_game_list_widget, &GameListWidget::refreshProgress, this, &MainWindow::onGameListRefreshProgress);
	connect(m_game_list_widget, &GameListWidget::refreshComplete, this, &MainWindow::onGameListRefreshComplete);
	connect(m_game_list_widget, &GameListWidget::selectionChanged, this, &MainWindow::onGameListSelectionChanged,
		Qt::QueuedConnection);
	connect(m_game_list_widget, &GameListWidget::entryActivated, this, &MainWindow::onGameListEntryActivated,
		Qt::QueuedConnection);
	connect(m_game_list_widget, &GameListWidget::entryContextMenuRequested, this,
		&MainWindow::onGameListEntryContextMenuRequested, Qt::QueuedConnection);
}

void MainWindow::connectVMThreadSignals(EmuThread* thread)
{
	connect(thread, &EmuThread::onCreateDisplayRequested, this, &MainWindow::createDisplay, Qt::BlockingQueuedConnection);
	connect(thread, &EmuThread::onUpdateDisplayRequested, this, &MainWindow::updateDisplay, Qt::BlockingQueuedConnection);
	connect(thread, &EmuThread::onDestroyDisplayRequested, this, &MainWindow::destroyDisplay,
		Qt::BlockingQueuedConnection);
	connect(thread, &EmuThread::onResizeDisplayRequested, this, &MainWindow::displayResizeRequested);
	connect(thread, &EmuThread::onVMStarting, this, &MainWindow::onVMStarting);
	connect(thread, &EmuThread::onVMStarted, this, &MainWindow::onVMStarted);
	connect(thread, &EmuThread::onVMPaused, this, &MainWindow::onVMPaused);
	connect(thread, &EmuThread::onVMResumed, this, &MainWindow::onVMResumed);
	connect(thread, &EmuThread::onVMStopped, this, &MainWindow::onVMStopped);
	connect(thread, &EmuThread::onGameChanged, this, &MainWindow::onGameChanged);

	connect(m_ui.actionReset, &QAction::triggered, thread, &EmuThread::resetVM);
	connect(m_ui.actionPause, &QAction::toggled, thread, &EmuThread::setVMPaused);
	connect(m_ui.actionFullscreen, &QAction::triggered, thread, &EmuThread::toggleFullscreen);
	connect(m_ui.actionToggleSoftwareRendering, &QAction::triggered, thread, &EmuThread::toggleSoftwareRendering);
	connect(m_ui.actionReloadPatches, &QAction::triggered, thread, &EmuThread::reloadPatches);

	static constexpr GSRendererType renderers[] = {
#ifdef _WIN32
		GSRendererType::DX11,
#endif
		GSRendererType::OGL, GSRendererType::VK, GSRendererType::SW, GSRendererType::Null};
	for (GSRendererType renderer : renderers)
	{
		connect(
			m_ui.menuDebugSwitchRenderer->addAction(QString::fromUtf8(Pcsx2Config::GSOptions::GetRendererName(renderer))),
			&QAction::triggered, [renderer] { g_emu_thread->switchRenderer(renderer); });
	}
}

void MainWindow::recreate()
{
	if (m_vm_valid)
		g_emu_thread->shutdownVM(true, true);

	close();
	g_main_window = nullptr;

	MainWindow* new_main_window = new MainWindow(m_unthemed_style_name);
	new_main_window->initialize();
	new_main_window->refreshGameList(false);
	new_main_window->show();
	deleteLater();
}

void MainWindow::setStyleFromSettings()
{
	const std::string theme(QtHost::GetBaseStringSettingValue("UI", "Theme", DEFAULT_THEME_NAME));

	if (theme == "fusion")
	{
		qApp->setPalette(QApplication::style()->standardPalette());
		qApp->setStyleSheet(QString());
		qApp->setStyle(QStyleFactory::create("Fusion"));
	}
	else if (theme == "darkfusion")
	{
		// adapted from https://gist.github.com/QuantumCD/6245215
		qApp->setStyle(QStyleFactory::create("Fusion"));

		const QColor lighterGray(75, 75, 75);
		const QColor darkGray(53, 53, 53);
		const QColor gray(128, 128, 128);
		const QColor black(25, 25, 25);
		const QColor blue(198, 238, 255);

		QPalette darkPalette;
		darkPalette.setColor(QPalette::Window, darkGray);
		darkPalette.setColor(QPalette::WindowText, Qt::white);
		darkPalette.setColor(QPalette::Base, black);
		darkPalette.setColor(QPalette::AlternateBase, darkGray);
		darkPalette.setColor(QPalette::ToolTipBase, darkGray);
		darkPalette.setColor(QPalette::ToolTipText, Qt::white);
		darkPalette.setColor(QPalette::Text, Qt::white);
		darkPalette.setColor(QPalette::Button, darkGray);
		darkPalette.setColor(QPalette::ButtonText, Qt::white);
		darkPalette.setColor(QPalette::Link, blue);
		darkPalette.setColor(QPalette::Highlight, lighterGray);
		darkPalette.setColor(QPalette::HighlightedText, Qt::white);

		darkPalette.setColor(QPalette::Active, QPalette::Button, gray.darker());
		darkPalette.setColor(QPalette::Disabled, QPalette::ButtonText, gray);
		darkPalette.setColor(QPalette::Disabled, QPalette::WindowText, gray);
		darkPalette.setColor(QPalette::Disabled, QPalette::Text, gray);
		darkPalette.setColor(QPalette::Disabled, QPalette::Light, darkGray);

		qApp->setPalette(darkPalette);

		qApp->setStyleSheet("QToolTip { color: #ffffff; background-color: #2a82da; border: 1px solid white; }");
	}
	else if (theme == "darkfusionblue")
	{
		// adapted from https://gist.github.com/QuantumCD/6245215
		qApp->setStyle(QStyleFactory::create("Fusion"));

		const QColor lighterGray(75, 75, 75);
		const QColor darkGray(53, 53, 53);
		const QColor gray(128, 128, 128);
		const QColor black(25, 25, 25);
		const QColor blue(198, 238, 255);
		const QColor blue2(0, 88, 208);

		QPalette darkPalette;
		darkPalette.setColor(QPalette::Window, darkGray);
		darkPalette.setColor(QPalette::WindowText, Qt::white);
		darkPalette.setColor(QPalette::Base, black);
		darkPalette.setColor(QPalette::AlternateBase, darkGray);
		darkPalette.setColor(QPalette::ToolTipBase, blue2);
		darkPalette.setColor(QPalette::ToolTipText, Qt::white);
		darkPalette.setColor(QPalette::Text, Qt::white);
		darkPalette.setColor(QPalette::Button, darkGray);
		darkPalette.setColor(QPalette::ButtonText, Qt::white);
		darkPalette.setColor(QPalette::Link, blue);
		darkPalette.setColor(QPalette::Highlight, blue2);
		darkPalette.setColor(QPalette::HighlightedText, Qt::white);

		darkPalette.setColor(QPalette::Active, QPalette::Button, gray.darker());
		darkPalette.setColor(QPalette::Disabled, QPalette::ButtonText, gray);
		darkPalette.setColor(QPalette::Disabled, QPalette::WindowText, gray);
		darkPalette.setColor(QPalette::Disabled, QPalette::Text, gray);
		darkPalette.setColor(QPalette::Disabled, QPalette::Light, darkGray);

		qApp->setPalette(darkPalette);

		qApp->setStyleSheet("QToolTip { color: #ffffff; background-color: #2a82da; border: 1px solid white; }");
	}
	else
	{
		qApp->setPalette(QApplication::style()->standardPalette());
		qApp->setStyleSheet(QString());
		qApp->setStyle(m_unthemed_style_name);
	}
}

void MainWindow::setIconThemeFromSettings()
{
	const std::string theme(QtHost::GetBaseStringSettingValue("UI", "Theme", DEFAULT_THEME_NAME));
	QString icon_theme;

	if (theme == "darkfusion" || theme == "darkfusionblue")
		icon_theme = QStringLiteral("white");
	else
		icon_theme = QStringLiteral("black");

	QIcon::setThemeName(icon_theme);
}

void MainWindow::saveStateToConfig()
{
	{
		const QByteArray geometry = saveGeometry();
		const QByteArray geometry_b64 = geometry.toBase64();
		const std::string old_geometry_b64 = QtHost::GetBaseStringSettingValue("UI", "MainWindowGeometry");
		if (old_geometry_b64 != geometry_b64.constData())
			QtHost::SetBaseStringSettingValue("UI", "MainWindowGeometry", geometry_b64.constData());
	}

	{
		const QByteArray state = saveState();
		const QByteArray state_b64 = state.toBase64();
		const std::string old_state_b64 = QtHost::GetBaseStringSettingValue("UI", "MainWindowState");
		if (old_state_b64 != state_b64.constData())
			QtHost::SetBaseStringSettingValue("UI", "MainWindowState", state_b64.constData());
	}
}

void MainWindow::restoreStateFromConfig()
{
	{
		const std::string geometry_b64 = QtHost::GetBaseStringSettingValue("UI", "MainWindowGeometry");
		const QByteArray geometry = QByteArray::fromBase64(QByteArray::fromStdString(geometry_b64));
		if (!geometry.isEmpty())
			restoreGeometry(geometry);
	}

	{
		const std::string state_b64 = QtHost::GetBaseStringSettingValue("UI", "MainWindowState");
		const QByteArray state = QByteArray::fromBase64(QByteArray::fromStdString(state_b64));
		if (!state.isEmpty())
			restoreState(state);

		{
			QSignalBlocker sb(m_ui.actionViewToolbar);
			m_ui.actionViewToolbar->setChecked(!m_ui.toolBar->isHidden());
		}
		{
			QSignalBlocker sb(m_ui.actionViewStatusBar);
			m_ui.actionViewStatusBar->setChecked(!m_ui.statusBar->isHidden());
		}
	}
}

void MainWindow::updateEmulationActions(bool starting, bool running)
{
	const bool starting_or_running = starting || running;

	m_ui.actionStartFile->setDisabled(starting_or_running);
	m_ui.actionStartDisc->setDisabled(starting_or_running);
	m_ui.actionStartBios->setDisabled(starting_or_running);

	m_ui.actionPowerOff->setEnabled(running);
	m_ui.actionReset->setEnabled(running);
	m_ui.actionPause->setEnabled(running);
	m_ui.actionChangeDisc->setEnabled(running);
	m_ui.actionCheats->setEnabled(running);
	m_ui.actionScreenshot->setEnabled(running);
	m_ui.actionViewSystemDisplay->setEnabled(starting_or_running);
	m_ui.menuChangeDisc->setEnabled(running);
	m_ui.menuCheats->setEnabled(running);

	m_ui.actionSaveState->setEnabled(running);
	m_ui.menuSaveState->setEnabled(running);
	m_ui.menuWindowSize->setEnabled(starting_or_running);

	m_ui.actionFullscreen->setEnabled(starting_or_running);
	m_ui.actionViewGameProperties->setEnabled(running);

	m_game_list_widget->setDisabled(starting && !running);
}

void MainWindow::updateWindowTitle()
{
	QString title;
	if (!m_vm_valid || m_current_game_name.isEmpty())
	{
#if defined(_DEBUG)
		title = QStringLiteral("PCSX2 [Debug] %1").arg(GIT_REV);
#else
		title = QStringLiteral("PCSX2 %1").arg(GIT_REV);
#endif
	}
	else
	{
#if defined(_DEBUG)
		title = QStringLiteral("%1 [Debug]").arg(m_current_game_name);
#else
		title = m_current_game_name;
#endif
	}

	if (windowTitle() != title)
		setWindowTitle(title);
}

void MainWindow::setProgressBar(int current, int total)
{
	m_status_progress_widget->setValue(current);
	m_status_progress_widget->setMaximum(total);

	if (m_status_progress_widget->isVisible())
		return;

	m_status_progress_widget->show();
	m_ui.statusBar->addPermanentWidget(m_status_progress_widget);
}

void MainWindow::clearProgressBar()
{
	if (!m_status_progress_widget->isVisible())
		return;

	m_status_progress_widget->hide();
	m_ui.statusBar->removeWidget(m_status_progress_widget);
}

bool MainWindow::isShowingGameList() const
{
	return m_ui.mainContainer->currentIndex() == 0;
}

void MainWindow::switchToGameListView()
{
	if ((m_display_widget && !m_display_widget->parent()) || m_ui.mainContainer->currentIndex() == 0)
		return;

	if (m_vm_valid)
	{
		m_was_focused_on_container_switch = m_vm_paused;
		if (!m_vm_paused)
			g_emu_thread->setVMPaused(true);
	}

	m_ui.mainContainer->setCurrentIndex(0);
	m_game_list_widget->setFocus();
}

void MainWindow::switchToEmulationView()
{
	if (!m_display_widget || !m_display_widget->parent() || m_ui.mainContainer->currentIndex() == 1)
		return;

	if (m_vm_valid)
	{
		m_ui.mainContainer->setCurrentIndex(1);
		if (m_vm_paused && !m_was_focused_on_container_switch)
			g_emu_thread->setVMPaused(false);
	}

	m_display_widget->setFocus();
}

void MainWindow::refreshGameList(bool invalidate_cache)
{
	m_game_list_widget->refresh(invalidate_cache);
}

void MainWindow::invalidateSaveStateCache()
{
	m_save_states_invalidated = true;
}

void MainWindow::reportError(const QString& title, const QString& message)
{
	QMessageBox::critical(this, title, message);
}

void Host::InvalidateSaveStateCache()
{
	QMetaObject::invokeMethod(g_main_window, &MainWindow::invalidateSaveStateCache, Qt::QueuedConnection);
}

void MainWindow::onGameListRefreshProgress(const QString& status, int current, int total)
{
	m_ui.statusBar->showMessage(status);
	setProgressBar(current, total);
}

void MainWindow::onGameListRefreshComplete()
{
	clearProgressBar();
}

void MainWindow::onGameListSelectionChanged()
{
	auto lock = GameList::GetLock();
	const GameList::Entry* entry = m_game_list_widget->getSelectedEntry();
	if (!entry)
		return;

	m_ui.statusBar->showMessage(QString::fromStdString(entry->path));
}

void MainWindow::onGameListEntryActivated()
{
	auto lock = GameList::GetLock();
	const GameList::Entry* entry = m_game_list_widget->getSelectedEntry();
	if (!entry)
		return;

	if (m_vm_valid)
	{
		// change disc on double click
		g_emu_thread->changeDisc(QString::fromStdString(entry->path));
		switchToEmulationView();
		return;
	}

	// only resume if the option is enabled, and we have one for this game
	const bool resume =
		(VMManager::ShouldSaveResumeState() && VMManager::HasSaveStateInSlot(entry->serial.c_str(), entry->crc, -1));
	startGameListEntry(entry, resume ? std::optional<s32>(-1) : std::optional<s32>(), std::nullopt);
}

void MainWindow::onGameListEntryContextMenuRequested(const QPoint& point)
{
	auto lock = GameList::GetLock();
	const GameList::Entry* entry = m_game_list_widget->getSelectedEntry();

	QMenu menu;

	if (entry)
	{
		QAction* action = menu.addAction(tr("Properties..."));
		// connect(action, &QAction::triggered, [this, entry]() { GamePropertiesDialog::showForEntry(entry, this); });

		action = menu.addAction(tr("Open Containing Directory..."));
		connect(action, &QAction::triggered, [this, entry]() {
			const QFileInfo fi(QString::fromStdString(entry->path));
			QtUtils::OpenURL(this, QUrl::fromLocalFile(fi.absolutePath()));
		});

		action = menu.addAction(tr("Set Cover Image..."));
		connect(action, &QAction::triggered, [this, entry]() { setGameListEntryCoverImage(entry); });

		connect(menu.addAction(tr("Exclude From List")), &QAction::triggered,
			[this, entry]() { getSettingsDialog()->getGameListSettingsWidget()->addExcludedPath(entry->path); });

		menu.addSeparator();

		if (!m_vm_valid)
		{
			action = menu.addAction(tr("Default Boot"));
			connect(action, &QAction::triggered, [this, entry]() { startGameListEntry(entry); });

			// Make bold to indicate it's the default choice when double-clicking
			if (!VMManager::ShouldSaveResumeState() || !VMManager::HasSaveStateInSlot(entry->serial.c_str(), entry->crc, -1))
				QtUtils::MarkActionAsDefault(action);

			action = menu.addAction(tr("Fast Boot"));
			connect(action, &QAction::triggered, [this, entry]() { startGameListEntry(entry, std::nullopt, true); });

			action = menu.addAction(tr("Full Boot"));
			connect(action, &QAction::triggered, [this, entry]() { startGameListEntry(entry, std::nullopt, false); });

			if (m_ui.menuDebug->menuAction()->isVisible())
			{
				// TODO: Hook this up once it's implemented.
				action = menu.addAction(tr("Boot and Debug"));
			}

			menu.addSeparator();
			populateLoadStateMenu(&menu, QString::fromStdString(entry->path), QString::fromStdString(entry->serial),
				entry->crc);
		}
		else
		{
			action = menu.addAction(tr("Change Disc"));
			connect(action, &QAction::triggered, [this, entry]() {
				g_emu_thread->changeDisc(QString::fromStdString(entry->path));
				switchToEmulationView();
			});
			QtUtils::MarkActionAsDefault(action);
		}

		menu.addSeparator();
	}

	connect(menu.addAction(tr("Add Search Directory...")), &QAction::triggered,
		[this]() { getSettingsDialog()->getGameListSettingsWidget()->addSearchDirectory(this); });

	menu.exec(point);
}

void MainWindow::onStartFileActionTriggered()
{
	QString filename = QDir::toNativeSeparators(
		QFileDialog::getOpenFileName(this, tr("Select Disc Image"), QString(), tr(DISC_IMAGE_FILTER), nullptr));
	if (filename.isEmpty())
		return;

	std::shared_ptr<VMBootParameters> params = std::make_shared<VMBootParameters>();
	VMManager::SetBootParametersForPath(filename.toStdString(), params.get());
	g_emu_thread->startVM(std::move(params));
}

void MainWindow::onStartBIOSActionTriggered()
{
	std::shared_ptr<VMBootParameters> params = std::make_shared<VMBootParameters>();
	params->source_type = CDVD_SourceType::NoDisc;
	g_emu_thread->startVM(std::move(params));
}

void MainWindow::onChangeDiscFromFileActionTriggered()
{
	ScopedVMPause pauser(m_vm_paused);

	QString filename =
		QFileDialog::getOpenFileName(this, tr("Select Disc Image"), QString(), tr(DISC_IMAGE_FILTER), nullptr);
	if (filename.isEmpty())
		return;

	g_emu_thread->changeDisc(filename);
}

void MainWindow::onChangeDiscFromGameListActionTriggered()
{
	switchToGameListView();
}

void MainWindow::onChangeDiscFromDeviceActionTriggered()
{
	// TODO
}

void MainWindow::onChangeDiscMenuAboutToShow()
{
	// TODO: This is where we would populate the playlist if there is one.
}

void MainWindow::onChangeDiscMenuAboutToHide()
{
}

void MainWindow::onLoadStateMenuAboutToShow()
{
	if (m_save_states_invalidated)
		updateSaveStateMenus(m_current_disc_path, m_current_game_serial, m_current_game_crc);
}

void MainWindow::onSaveStateMenuAboutToShow()
{
	if (m_save_states_invalidated)
		updateSaveStateMenus(m_current_disc_path, m_current_game_serial, m_current_game_crc);
}

void MainWindow::onViewToolbarActionToggled(bool checked)
{
	QtHost::SetBaseBoolSettingValue("UI", "ShowToolbar", checked);
	m_ui.toolBar->setVisible(checked);
}

void MainWindow::onViewLockToolbarActionToggled(bool checked)
{
	QtHost::SetBaseBoolSettingValue("UI", "LockToolbar", checked);
	m_ui.toolBar->setMovable(!checked);
}

void MainWindow::onViewStatusBarActionToggled(bool checked)
{
	QtHost::SetBaseBoolSettingValue("UI", "ShowStatusBar", checked);
	m_ui.statusBar->setVisible(checked);
}

void MainWindow::onViewGameListActionTriggered()
{
	switchToGameListView();
	m_game_list_widget->showGameList();
}

void MainWindow::onViewGameGridActionTriggered()
{
	switchToGameListView();
	m_game_list_widget->showGameGrid();
}

void MainWindow::onViewSystemDisplayTriggered()
{
	if (m_vm_valid)
		switchToEmulationView();
}

void MainWindow::onViewGamePropertiesActionTriggered()
{
	if (!m_vm_valid)
		return;
}

void MainWindow::onGitHubRepositoryActionTriggered()
{
	QtUtils::OpenURL(this, AboutDialog::getGitHubRepositoryUrl());
}

void MainWindow::onSupportForumsActionTriggered()
{
	QtUtils::OpenURL(this, AboutDialog::getSupportForumsUrl());
}

void MainWindow::onDiscordServerActionTriggered()
{
	QtUtils::OpenURL(this, AboutDialog::getDiscordServerUrl());
}

void MainWindow::onAboutActionTriggered()
{
	AboutDialog about(this);
	about.exec();
}

void MainWindow::onCheckForUpdatesActionTriggered()
{
}

void MainWindow::onToolsOpenDataDirectoryTriggered()
{
	const QString path(QtUtils::WxStringToQString(EmuFolders::DataRoot.ToString()));
	QtUtils::OpenURL(this, QUrl::fromLocalFile(path));
}

void MainWindow::onThemeChanged()
{
	setStyleFromSettings();
	setIconThemeFromSettings();
	recreate();
}

void MainWindow::onThemeChangedFromSettings()
{
	// reopen the settings dialog after recreating
	onThemeChanged();
	g_main_window->doSettings(SettingsDialog::Category::InterfaceSettings);
}

void MainWindow::onVMStarting()
{
	m_vm_valid = true;
	updateEmulationActions(true, false);
	updateWindowTitle();

	// prevent loading state until we're fully initialized
	updateSaveStateMenus(QString(), QString(), 0);
}

void MainWindow::onVMStarted()
{
	m_vm_valid = true;
	updateEmulationActions(true, true);
	updateWindowTitle();
}

void MainWindow::onVMPaused()
{
	// update UI
	{
		QSignalBlocker sb(m_ui.actionPause);
		m_ui.actionPause->setChecked(true);
	}

	m_vm_paused = true;
	updateWindowTitle();
}

void MainWindow::onVMResumed()
{
	// update UI
	{
		QSignalBlocker sb(m_ui.actionPause);
		m_ui.actionPause->setChecked(false);
	}

	m_vm_paused = false;
	updateWindowTitle();
}

void MainWindow::onVMStopped()
{
	m_vm_valid = false;
	m_vm_paused = false;
	updateEmulationActions(false, false);
	updateWindowTitle();
	switchToGameListView();
}

void MainWindow::onGameChanged(const QString& path, const QString& serial, const QString& name, quint32 crc)
{
	m_current_disc_path = path;
	m_current_game_serial = serial;
	m_current_game_name = name;
	m_current_game_crc = crc;
	updateWindowTitle();
	updateSaveStateMenus(path, serial, crc);
}

void MainWindow::closeEvent(QCloseEvent* event)
{
	g_emu_thread->shutdownVM(true, true);
	saveStateToConfig();
	QMainWindow::closeEvent(event);
}

DisplayWidget* MainWindow::createDisplay(bool fullscreen, bool render_to_main)
{
	pxAssertRel(!fullscreen || !render_to_main, "Not rendering to main and fullscreen");

	HostDisplay* host_display = Host::GetHostDisplay();
	if (!host_display)
		return nullptr;

	const std::string fullscreen_mode(QtHost::GetBaseStringSettingValue("EmuCore/GS", "FullscreenMode", ""));
	const bool is_exclusive_fullscreen = (fullscreen && !fullscreen_mode.empty() && host_display->SupportsFullscreen());

	QWidget* container;
	if (DisplayContainer::IsNeeded(fullscreen, render_to_main))
	{
		m_display_container = new DisplayContainer();
		m_display_widget = new DisplayWidget(m_display_container);
		m_display_container->setDisplayWidget(m_display_widget);
		container = m_display_container;
	}
	else
	{
		m_display_widget = new DisplayWidget((!fullscreen && render_to_main) ? m_ui.mainContainer : nullptr);
		container = m_display_widget;
	}

	container->setWindowTitle(windowTitle());
	container->setWindowIcon(windowIcon());

	if (fullscreen)
	{
		if (!is_exclusive_fullscreen)
			container->showFullScreen();
		else
			container->showNormal();

		// updateMouseMode(System::IsPaused());
	}
	else if (!render_to_main)
	{
		restoreDisplayWindowGeometryFromConfig();
		container->showNormal();
	}
	else
	{
		m_ui.mainContainer->insertWidget(1, m_display_widget);
		switchToEmulationView();
	}

	// we need the surface visible.. this might be able to be replaced with something else
	QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);

	std::optional<WindowInfo> wi = m_display_widget->getWindowInfo();
	if (!wi.has_value())
	{
		QMessageBox::critical(this, tr("Error"), tr("Failed to get window info from widget"));
		destroyDisplayWidget();
		return nullptr;
	}

	if (!host_display->CreateRenderDevice(wi.value(), Host::GetStringSettingValue("EmuCore/GS", "Adapter", ""),
			Host::GetBoolSettingValue("EmuCore/GS", "ThreadedPresentation", false),
			Host::GetBoolSettingValue("EmuCore/GS", "UseDebugDevice", false)))
	{
		QMessageBox::critical(this, tr("Error"), tr("Failed to create host display device context."));
		destroyDisplayWidget();
		return nullptr;
	}

	if (is_exclusive_fullscreen)
		setDisplayFullscreen(fullscreen_mode);

	host_display->DoneRenderContextCurrent();
	return m_display_widget;
}

DisplayWidget* MainWindow::updateDisplay(bool fullscreen, bool render_to_main)
{
	HostDisplay* host_display = Host::GetHostDisplay();
	const bool is_fullscreen = m_display_widget->isFullScreen();
	const bool is_rendering_to_main = (!is_fullscreen && m_display_widget->parent());
	const std::string fullscreen_mode(QtHost::GetBaseStringSettingValue("EmuCore/GS", "FullscreenMode", ""));
	const bool is_exclusive_fullscreen = (fullscreen && !fullscreen_mode.empty() && host_display->SupportsFullscreen());
	if (fullscreen == is_fullscreen && is_rendering_to_main == render_to_main)
		return m_display_widget;

	// Skip recreating the surface if we're just transitioning between fullscreen and windowed with render-to-main off.
	const bool has_container = (m_display_container != nullptr);
	const bool needs_container = DisplayContainer::IsNeeded(fullscreen, render_to_main);
	if (!is_rendering_to_main && !render_to_main && !is_exclusive_fullscreen && has_container == needs_container)
	{
		qDebug() << "Toggling to" << (fullscreen ? "fullscreen" : "windowed") << "without recreating surface";
		if (host_display->IsFullscreen())
			host_display->SetFullscreen(false, 0, 0, 0.0f);

		if (fullscreen)
		{
			m_display_widget->showFullScreen();
		}
		else
		{
			restoreDisplayWindowGeometryFromConfig();
			m_display_widget->showNormal();
		}

		QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
		// updateMouseMode(System::IsPaused());
		return m_display_widget;
	}

	host_display->DestroyRenderSurface();

	destroyDisplayWidget();

	QWidget* container;
	if (DisplayContainer::IsNeeded(fullscreen, render_to_main))
	{
		m_display_container = new DisplayContainer();
		m_display_widget = new DisplayWidget(m_display_container);
		m_display_container->setDisplayWidget(m_display_widget);
		container = m_display_container;
	}
	else
	{
		m_display_widget = new DisplayWidget((!fullscreen && render_to_main) ? m_ui.mainContainer : nullptr);
		container = m_display_widget;
	}

	container->setWindowTitle(windowTitle());
	container->setWindowIcon(windowIcon());

	if (fullscreen)
	{
		if (!is_exclusive_fullscreen)
			container->showFullScreen();
		else
			container->showNormal();

		// updateMouseMode(System::IsPaused());
	}
	else if (!render_to_main)
	{
		restoreDisplayWindowGeometryFromConfig();
		container->showNormal();
	}
	else
	{
		m_ui.mainContainer->insertWidget(1, m_display_widget);
		switchToEmulationView();
	}

	// we need the surface visible.. this might be able to be replaced with something else
	QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);

	std::optional<WindowInfo> wi = m_display_widget->getWindowInfo();
	if (!wi.has_value())
	{
		QMessageBox::critical(this, tr("Error"), tr("Failed to get new window info from widget"));
		destroyDisplayWidget();
		return nullptr;
	}

	if (!host_display->ChangeRenderWindow(wi.value()))
		pxFailRel("Failed to recreate surface on new widget.");

	if (is_exclusive_fullscreen)
		setDisplayFullscreen(fullscreen_mode);

	m_display_widget->setFocus();

	QSignalBlocker blocker(m_ui.actionFullscreen);
	m_ui.actionFullscreen->setChecked(fullscreen);
	return m_display_widget;
}

void MainWindow::displayResizeRequested(qint32 width, qint32 height)
{
	if (!m_display_widget)
		return;

	// unapply the pixel scaling factor for hidpi
	const float dpr = devicePixelRatioF();
	width = static_cast<qint32>(std::max(static_cast<int>(std::lroundf(static_cast<float>(width) / dpr)), 1));
	height = static_cast<qint32>(std::max(static_cast<int>(std::lroundf(static_cast<float>(height) / dpr)), 1));

	if (m_display_container || !m_display_widget->parent())
	{
		// no parent - rendering to separate window. easy.
		getDisplayContainer()->resize(QSize(std::max<qint32>(width, 1), std::max<qint32>(height, 1)));
		return;
	}

	// we are rendering to the main window. we have to add in the extra height from the toolbar/status bar.
	const s32 extra_height = this->height() - m_display_widget->height();
	resize(QSize(std::max<qint32>(width, 1), std::max<qint32>(height + extra_height, 1)));
}

void MainWindow::destroyDisplay()
{
	destroyDisplayWidget();
}

void MainWindow::focusDisplayWidget()
{
	if (m_ui.mainContainer->currentIndex() != 1)
		return;

	m_display_widget->setFocus();
}

QWidget* MainWindow::getDisplayContainer() const
{
	return (m_display_container ? static_cast<QWidget*>(m_display_container) : static_cast<QWidget*>(m_display_widget));
}

void MainWindow::saveDisplayWindowGeometryToConfig()
{
	const QByteArray geometry = getDisplayContainer()->saveGeometry();
	const QByteArray geometry_b64 = geometry.toBase64();
	const std::string old_geometry_b64 = QtHost::GetBaseStringSettingValue("UI", "DisplayWindowGeometry");
	if (old_geometry_b64 != geometry_b64.constData())
		QtHost::SetBaseStringSettingValue("UI", "DisplayWindowGeometry", geometry_b64.constData());
}

void MainWindow::restoreDisplayWindowGeometryFromConfig()
{
	const std::string geometry_b64 = QtHost::GetBaseStringSettingValue("UI", "DisplayWindowGeometry");
	const QByteArray geometry = QByteArray::fromBase64(QByteArray::fromStdString(geometry_b64));
	QWidget* container = getDisplayContainer();
	if (!geometry.isEmpty())
		container->restoreGeometry(geometry);
	else
		container->resize(640, 480);
}

void MainWindow::destroyDisplayWidget()
{
	if (!m_display_widget)
		return;

	if (m_display_container || (!m_display_widget->parent() && !m_display_widget->isFullScreen()))
		saveDisplayWindowGeometryToConfig();

	if (m_display_container)
		m_display_container->removeDisplayWidget();

	if (m_display_widget->parent())
	{
		m_ui.mainContainer->removeWidget(m_display_widget);
		m_ui.mainContainer->setCurrentIndex(0);
		m_game_list_widget->setFocus();
	}

	delete m_display_widget;
	m_display_widget = nullptr;

	delete m_display_container;
	m_display_container = nullptr;
}

void MainWindow::setDisplayFullscreen(const std::string& fullscreen_mode)
{
	u32 width, height;
	float refresh_rate;
	if (HostDisplay::ParseFullscreenMode(fullscreen_mode, &width, &height, &refresh_rate))
	{
		if (Host::GetHostDisplay()->SetFullscreen(true, width, height, refresh_rate))
		{
			Host::AddOSDMessage("Acquired exclusive fullscreen.", 10.0f);
		}
		else
		{
			Host::AddOSDMessage("Failed to acquire exclusive fullscreen.", 10.0f);
		}
	}
}

SettingsDialog* MainWindow::getSettingsDialog()
{
	if (!m_settings_dialog)
	{
		m_settings_dialog = new SettingsDialog(this);
		connect(m_settings_dialog->getInterfaceSettingsWidget(), &InterfaceSettingsWidget::themeChanged, this,
			&MainWindow::onThemeChangedFromSettings);
	}

	return m_settings_dialog;
}

void MainWindow::doSettings(SettingsDialog::Category category)
{
	SettingsDialog* dlg = getSettingsDialog();
	if (!dlg->isVisible())
	{
		dlg->setModal(false);
		dlg->show();
	}

	if (category != SettingsDialog::Category::Count)
		dlg->setCategory(category);
}

ControllerSettingsDialog* MainWindow::getControllerSettingsDialog()
{
	if (!m_controller_settings_dialog)
		m_controller_settings_dialog = new ControllerSettingsDialog(this);

	return m_controller_settings_dialog;
}

void MainWindow::doControllerSettings(ControllerSettingsDialog::Category category)
{
	ControllerSettingsDialog* dlg = getControllerSettingsDialog();
	if (!dlg->isVisible())
	{
		dlg->setModal(false);
		dlg->show();
	}

	if (category != ControllerSettingsDialog::Category::Count)
		dlg->setCategory(category);
}

void MainWindow::startGameListEntry(const GameList::Entry* entry, std::optional<s32> save_slot,
	std::optional<bool> fast_boot)
{
	std::shared_ptr<VMBootParameters> params = std::make_shared<VMBootParameters>();
	params->fast_boot = fast_boot;

	GameList::FillBootParametersForEntry(params.get(), entry);

	if (save_slot.has_value() && !entry->serial.empty())
	{
		std::string state_filename = VMManager::GetSaveStateFileName(entry->serial.c_str(), entry->crc, save_slot.value());
		if (!FileSystem::FileExists(state_filename.c_str()))
		{
			QMessageBox::critical(this, tr("Error"), tr("This save state does not exist."));
			return;
		}

		params->save_state = std::move(state_filename);
	}

	g_emu_thread->startVM(std::move(params));
}

void MainWindow::setGameListEntryCoverImage(const GameList::Entry* entry)
{
	const QString filename(QFileDialog::getOpenFileName(this, tr("Select Cover Image"), QString(),
		tr("All Cover Image Types (*.jpg *.jpeg *.png)")));
	if (filename.isEmpty())
		return;

	if (!GameList::GetCoverImagePathForEntry(entry).empty())
	{
		if (QMessageBox::question(this, tr("Cover Already Exists"),
				tr("A cover image for this game already exists, do you wish to replace it?"),
				QMessageBox::Yes, QMessageBox::No) != QMessageBox::Yes)
		{
			return;
		}
	}

	const QString new_filename(QString::fromStdString(GameList::GetNewCoverImagePathForEntry(entry, filename.toUtf8().constData())));
	if (new_filename.isEmpty())
		return;

	if (QFile::exists(new_filename) && !QFile::remove(new_filename))
	{
		QMessageBox::critical(this, tr("Copy Error"), tr("Failed to remove existing cover '%1'").arg(new_filename));
		return;
	}

	if (!QFile::copy(filename, new_filename))
	{
		QMessageBox::critical(this, tr("Copy Error"), tr("Failed to copy '%1' to '%2'").arg(filename).arg(new_filename));
		return;
	}

	m_game_list_widget->refreshGridCovers();
}

void MainWindow::loadSaveStateSlot(s32 slot)
{
	if (m_vm_valid)
	{
		// easy when we're running
		g_emu_thread->loadStateFromSlot(slot);
		return;
	}
	else
	{
		// we're not currently running, therefore we must've right clicked in the game list
		const GameList::Entry* entry = m_game_list_widget->getSelectedEntry();
		if (!entry)
			return;

		startGameListEntry(entry, slot, std::nullopt);
	}
}

void MainWindow::loadSaveStateFile(const QString& filename, const QString& state_filename)
{
	if (m_vm_valid)
	{
		g_emu_thread->loadState(filename);
	}
	else
	{
		std::shared_ptr<VMBootParameters> params = std::make_shared<VMBootParameters>();
		VMManager::SetBootParametersForPath(filename.toStdString(), params.get());
		params->save_state = state_filename.toStdString();
		g_emu_thread->startVM(std::move(params));
	}
}

static QString formatTimestampForSaveStateMenu(time_t timestamp)
{
	const QDateTime qtime(QDateTime::fromSecsSinceEpoch(static_cast<qint64>(timestamp)));
	return qtime.toString(QLocale::system().dateTimeFormat(QLocale::ShortFormat));
}

void MainWindow::populateLoadStateMenu(QMenu* menu, const QString& filename, const QString& serial, quint32 crc)
{
	if (serial.isEmpty())
		return;

	const bool is_right_click_menu = (menu != m_ui.menuLoadState);

	QAction* action = menu->addAction(is_right_click_menu ? tr("Load State File...") : tr("Load From File..."));
	connect(action, &QAction::triggered, [this, filename]() {
		const QString path(
			QFileDialog::getOpenFileName(this, tr("Select Save State File"), QString(), tr("Save States (*.p2s)")));
		if (path.isEmpty())
			return;

		loadSaveStateFile(filename, path);
	});

	// don't include undo in the right click menu
	if (!is_right_click_menu)
	{
		QAction* load_undo_state = menu->addAction(tr("Undo Load State"));
		load_undo_state->setEnabled(false); // CanUndoLoadState()
		// connect(load_undo_state, &QAction::triggered, this, &QtHostInterface::undoLoadState);
		menu->addSeparator();
	}

	const QByteArray game_serial_utf8(serial.toUtf8());
	std::string state_filename;
	FILESYSTEM_STAT_DATA sd;
	if (is_right_click_menu)
	{
		state_filename = VMManager::GetSaveStateFileName(game_serial_utf8.constData(), crc, -1);
		if (FileSystem::StatFile(state_filename.c_str(), &sd))
		{
			action = menu->addAction(tr("Resume (%2)").arg(formatTimestampForSaveStateMenu(sd.ModificationTime)));
			connect(action, &QAction::triggered, [this]() { loadSaveStateSlot(-1); });

			// Make bold to indicate it's the default choice when double-clicking
			if (VMManager::ShouldSaveResumeState())
				QtUtils::MarkActionAsDefault(action);
		}
	}

	for (s32 i = 1; i <= NUM_SAVE_STATE_SLOTS; i++)
	{
		FILESYSTEM_STAT_DATA sd;
		state_filename = VMManager::GetSaveStateFileName(game_serial_utf8.constData(), crc, i);
		if (!FileSystem::StatFile(state_filename.c_str(), &sd))
			continue;

		action = menu->addAction(tr("Save Slot %1 (%2)").arg(i).arg(formatTimestampForSaveStateMenu(sd.ModificationTime)));
		connect(action, &QAction::triggered, [this, i]() { loadSaveStateSlot(i); });
	}
}

void MainWindow::populateSaveStateMenu(QMenu* menu, const QString& serial, quint32 crc)
{
	if (serial.isEmpty())
		return;

	connect(menu->addAction(tr("Save To File...")), &QAction::triggered, [this]() {
		const QString path(
			QFileDialog::getSaveFileName(this, tr("Select Save State File"), QString(), tr("Save States (*.p2s)")));
		if (path.isEmpty())
			return;

		g_emu_thread->saveState(path);
	});

	menu->addSeparator();

	const QByteArray game_serial_utf8(serial.toUtf8());
	for (s32 i = 1; i <= NUM_SAVE_STATE_SLOTS; i++)
	{
		std::string filename(VMManager::GetSaveStateFileName(game_serial_utf8.constData(), crc, i));
		FILESYSTEM_STAT_DATA sd;
		QString timestamp;
		if (FileSystem::StatFile(filename.c_str(), &sd))
			timestamp = formatTimestampForSaveStateMenu(sd.ModificationTime);
		else
			timestamp = tr("Empty");

		QString title(tr("Save Slot %1 (%2)").arg(i).arg(timestamp));
		connect(menu->addAction(title), &QAction::triggered, [this, i]() { g_emu_thread->saveStateToSlot(i); });
	}
}

void MainWindow::updateSaveStateMenus(const QString& filename, const QString& serial, quint32 crc)
{
	const bool load_enabled = !serial.isEmpty();
	const bool save_enabled = !serial.isEmpty() && m_vm_valid;
	m_ui.menuLoadState->clear();
	m_ui.menuLoadState->setEnabled(load_enabled);
	m_ui.actionLoadState->setEnabled(load_enabled);
	m_ui.menuSaveState->clear();
	m_ui.menuSaveState->setEnabled(save_enabled);
	m_ui.actionSaveState->setEnabled(save_enabled);
	m_save_states_invalidated = false;
	if (load_enabled)
		populateLoadStateMenu(m_ui.menuLoadState, filename, serial, crc);
	if (save_enabled)
		populateSaveStateMenu(m_ui.menuSaveState, serial, crc);
}
