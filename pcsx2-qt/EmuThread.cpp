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

#include <QtWidgets/QApplication>

#include "common/Assertions.h"
#include "common/Console.h"
#include "common/Exceptions.h"
#include "common/SettingsWrapper.h"
#include "common/StringUtil.h"

#include "pcsx2/CDVD/CDVD.h"
#include "pcsx2/Frontend/InputManager.h"
#include "pcsx2/Frontend/ImGuiManager.h"
#include "pcsx2/GS.h"
#include "pcsx2/HostDisplay.h"
#include "pcsx2/PAD/Host/PAD.h"
#include "pcsx2/PerformanceMetrics.h"
#include "pcsx2/VMManager.h"

#include "pcsx2/Frontend/OpenGLHostDisplay.h"

#ifdef _WIN32
#include "pcsx2/Frontend/D3D11HostDisplay.h"
#endif

#include "DisplayWidget.h"
#include "EmuThread.h"
#include "MainWindow.h"
#include "QtHost.h"
#include "QtUtils.h"

EmuThread* g_emu_thread = nullptr;
WindowInfo g_gs_window_info;

static std::unique_ptr<HostDisplay> s_host_display;

EmuThread::EmuThread(QThread* ui_thread)
	: QThread()
	, m_ui_thread(ui_thread)
{
}

EmuThread::~EmuThread() = default;

bool EmuThread::isOnEmuThread() const
{
	return QThread::currentThread() == this;
}

void EmuThread::start()
{
	pxAssertRel(!g_emu_thread, "Emu thread does not exist");

	g_emu_thread = new EmuThread(QThread::currentThread());
	g_emu_thread->QThread::start();
	g_emu_thread->m_started_semaphore.acquire();
	g_emu_thread->moveToThread(g_emu_thread);
	g_main_window->connectVMThreadSignals(g_emu_thread);
}

void EmuThread::stop()
{
	pxAssertRel(g_emu_thread, "Emu thread exists");
	pxAssertRel(!g_emu_thread->isOnEmuThread(), "Not called on the emu thread");

	QMetaObject::invokeMethod(g_emu_thread, &EmuThread::stopInThread, Qt::QueuedConnection);
	while (g_emu_thread->isRunning())
		QApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 1);
}

void EmuThread::stopInThread()
{
	if (VMManager::HasValidVM())
		destroyVM();

	m_event_loop->quit();
	m_shutdown_flag.store(true);
}

void EmuThread::startVM(std::shared_ptr<VMBootParameters> boot_params)
{
	if (!isOnEmuThread())
	{
		QMetaObject::invokeMethod(this, "startVM", Qt::QueuedConnection,
			Q_ARG(std::shared_ptr<VMBootParameters>, boot_params));
		return;
	}

	pxAssertRel(!VMManager::HasValidVM(), "VM is shut down");

	emit onVMStarting();

	// create the display, this may take a while...
	m_is_fullscreen = boot_params->fullscreen.value_or(QtHost::GetBaseBoolSettingValue("UI", "StartFullscreen", false));
	m_is_rendering_to_main = !m_is_fullscreen && QtHost::GetBaseBoolSettingValue("UI", "RenderToMainWindow", true);
	if (!VMManager::Initialize(*boot_params))
		return;

	VMManager::SetState(VMState::Running);
	m_event_loop->quit();
}

void EmuThread::resetVM()
{
	if (!isOnEmuThread())
	{
		QMetaObject::invokeMethod(this, &EmuThread::resetVM, Qt::QueuedConnection);
		return;
	}

	VMManager::Reset();
}

void EmuThread::setVMPaused(bool paused)
{
	if (!isOnEmuThread())
	{
		QMetaObject::invokeMethod(this, "setVMPaused", Qt::QueuedConnection, Q_ARG(bool, paused));
		return;
	}

	VMManager::SetPaused(paused);
}

void EmuThread::shutdownVM(bool allow_save_to_state /* = true */, bool blocking /* = false */)
{
	if (!isOnEmuThread())
	{
		QMetaObject::invokeMethod(this, "shutdownVM", Qt::QueuedConnection, Q_ARG(bool, allow_save_to_state),
			Q_ARG(bool, blocking));

		if (blocking)
		{
			// we need to yield here, since the display gets destroyed
			while (VMManager::HasValidVM())
				QApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 1);
		}

		return;
	}

	const VMState state = VMManager::GetState();
	if (state == VMState::Paused)
		m_event_loop->quit();
	else if (state != VMState::Running)
		return;

	VMManager::SetState(VMState::Stopping);
}

void EmuThread::loadState(const QString& filename)
{
	if (!isOnEmuThread())
	{
		QMetaObject::invokeMethod(this, "loadState", Qt::QueuedConnection, Q_ARG(const QString&, filename));
		return;
	}

	if (!VMManager::HasValidVM())
		return;

	VMManager::LoadState(filename.toUtf8().constData());
}

void EmuThread::loadStateFromSlot(qint32 slot)
{
	if (!isOnEmuThread())
	{
		QMetaObject::invokeMethod(this, "loadStateFromSlot", Qt::QueuedConnection, Q_ARG(qint32, slot));
		return;
	}

	if (!VMManager::HasValidVM())
		return;

	VMManager::LoadStateFromSlot(slot);
}

void EmuThread::saveState(const QString& filename)
{
	if (!isOnEmuThread())
	{
		QMetaObject::invokeMethod(this, "saveState", Qt::QueuedConnection, Q_ARG(const QString&, filename));
		return;
	}

	if (!VMManager::HasValidVM())
		return;

	if (!VMManager::SaveState(filename.toUtf8().constData()))
	{
		// this one is usually the result of a user-chosen path, so we can display a message box safely here
		Console.Error("Failed to save state");
	}
}

void EmuThread::saveStateToSlot(qint32 slot)
{
	if (!isOnEmuThread())
	{
		QMetaObject::invokeMethod(this, "saveStateToSlot", Qt::QueuedConnection, Q_ARG(qint32, slot));
		return;
	}

	if (!VMManager::HasValidVM())
		return;

	VMManager::SaveStateToSlot(slot);
}

void EmuThread::run()
{
	PerformanceMetrics::SetCPUThreadTimer(Common::ThreadCPUTimer::GetForCallingThread());
	m_event_loop = new QEventLoop();
	m_started_semaphore.release();

	if (!VMManager::InitializeMemory())
		pxFailRel("Failed to allocate memory map");

	// we need input sources ready for binding
	reloadInputSources();
	createBackgroundControllerPollTimer();
	startBackgroundControllerPollTimer();

	while (!m_shutdown_flag.load())
	{
		if (!VMManager::HasValidVM())
		{
			m_event_loop->exec();
			continue;
		}

		executeVM();
	}

	stopBackgroundControllerPollTimer();
	destroyBackgroundControllerPollTimer();
	InputManager::CloseSources();
	VMManager::ReleaseMemory();
	PerformanceMetrics::SetCPUThreadTimer(Common::ThreadCPUTimer());
	moveToThread(m_ui_thread);
	deleteLater();
}

void EmuThread::destroyVM()
{
	VMManager::Shutdown();
}

void EmuThread::executeVM()
{
	for (;;)
	{
		switch (VMManager::GetState())
		{
			case VMState::Initializing:
				pxFailRel("Shouldn't be in the starting state state");
				continue;

			case VMState::Paused:
				m_event_loop->exec();
				continue;

			case VMState::Running:
				m_event_loop->processEvents(QEventLoop::AllEvents);
				VMManager::Execute();
				continue;

			case VMState::Stopping:
				destroyVM();
				m_event_loop->processEvents(QEventLoop::AllEvents);
				return;
		}
	}
}

void EmuThread::createBackgroundControllerPollTimer()
{
	pxAssert(!m_background_controller_polling_timer);
	m_background_controller_polling_timer = new QTimer(this);
	m_background_controller_polling_timer->setSingleShot(false);
	m_background_controller_polling_timer->setTimerType(Qt::CoarseTimer);
	connect(m_background_controller_polling_timer, &QTimer::timeout, this, &EmuThread::doBackgroundControllerPoll);
}

void EmuThread::destroyBackgroundControllerPollTimer()
{
	delete m_background_controller_polling_timer;
	m_background_controller_polling_timer = nullptr;
}

void EmuThread::startBackgroundControllerPollTimer()
{
	if (m_background_controller_polling_timer->isActive())
		return;

	m_background_controller_polling_timer->start(BACKGROUND_CONTROLLER_POLLING_INTERVAL);
}

void EmuThread::stopBackgroundControllerPollTimer()
{
	if (!m_background_controller_polling_timer->isActive())
		return;

	m_background_controller_polling_timer->stop();
}

void EmuThread::doBackgroundControllerPoll()
{
	InputManager::PollSources();
}

void EmuThread::toggleFullscreen()
{
	if (!isOnEmuThread())
	{
		QMetaObject::invokeMethod(this, &EmuThread::toggleFullscreen, Qt::QueuedConnection);
		return;
	}

	setFullscreen(!m_is_fullscreen);
}

void EmuThread::setFullscreen(bool fullscreen)
{
	if (!isOnEmuThread())
	{
		QMetaObject::invokeMethod(this, "setFullscreen", Qt::QueuedConnection, Q_ARG(bool, fullscreen));
		return;
	}

	if (!VMManager::HasValidVM() || m_is_fullscreen == fullscreen)
		return;

	// This will call back to us on the MTGS thread.
	m_is_fullscreen = fullscreen;
	GetMTGS().UpdateDisplayWindow();
	GetMTGS().WaitGS();
}

void EmuThread::applySettings()
{
	if (!isOnEmuThread())
	{
		QMetaObject::invokeMethod(this, &EmuThread::applySettings, Qt::QueuedConnection);
		return;
	}

	checkForSettingChanges();
	VMManager::ApplySettings();
}

void EmuThread::checkForSettingChanges()
{
	if (VMManager::HasValidVM())
	{
		const bool render_to_main = QtHost::GetBaseBoolSettingValue("UI", "RenderToMainWindow", true);
		if (!m_is_fullscreen && m_is_rendering_to_main != render_to_main)
		{
			m_is_rendering_to_main = render_to_main;
			GetMTGS().UpdateDisplayWindow();
			GetMTGS().WaitGS();
		}
	}
}

void EmuThread::toggleSoftwareRendering()
{
	if (!isOnEmuThread())
	{
		QMetaObject::invokeMethod(this, &EmuThread::toggleSoftwareRendering, Qt::QueuedConnection);
		return;
	}

	if (!VMManager::HasValidVM())
		return;

	GetMTGS().ToggleSoftwareRendering();
}

void EmuThread::switchRenderer(GSRendererType renderer)
{
	if (!isOnEmuThread())
	{
		QMetaObject::invokeMethod(this, "switchRenderer", Qt::QueuedConnection, Q_ARG(GSRendererType, renderer));
		return;
	}

	if (!VMManager::HasValidVM())
		return;

	GetMTGS().SwitchRenderer(renderer);
}

void EmuThread::changeDisc(const QString& path)
{
	if (!isOnEmuThread())
	{
		QMetaObject::invokeMethod(this, "changeDisc", Qt::QueuedConnection, Q_ARG(const QString&, path));
		return;
	}

	if (!VMManager::HasValidVM())
		return;

	VMManager::ChangeDisc(path.toStdString());
}

void EmuThread::reloadPatches()
{
	if (!isOnEmuThread())
	{
		QMetaObject::invokeMethod(this, &EmuThread::reloadPatches, Qt::QueuedConnection);
		return;
	}

	if (!VMManager::HasValidVM())
		return;

	VMManager::ReloadPatches(true);
}

void EmuThread::reloadInputSources()
{
	if (!isOnEmuThread())
	{
		QMetaObject::invokeMethod(this, &EmuThread::reloadInputSources, Qt::QueuedConnection);
		return;
	}

	auto lock = Host::GetSettingsLock();
	SettingsInterface* si = Host::GetSettingsInterface();
	InputManager::ReloadSources(*si);

	// skip loading bindings if we're not running, since it'll get done on startup anyway
	if (VMManager::HasValidVM())
		InputManager::ReloadBindings(*si);
}

void EmuThread::reloadInputBindings()
{
	if (!isOnEmuThread())
	{
		QMetaObject::invokeMethod(this, &EmuThread::reloadInputBindings, Qt::QueuedConnection);
		return;
	}

	// skip loading bindings if we're not running, since it'll get done on startup anyway
	if (!VMManager::HasValidVM())
		return;

	auto lock = Host::GetSettingsLock();
	SettingsInterface* si = Host::GetSettingsInterface();
	InputManager::ReloadBindings(*si);
}

void EmuThread::requestDisplaySize(float scale)
{
	if (!isOnEmuThread())
	{
		QMetaObject::invokeMethod(this, "requestDisplaySize", Qt::QueuedConnection, Q_ARG(float, scale));
		return;
	}

	if (!VMManager::HasValidVM())
		return;

	VMManager::RequestDisplaySize(scale);
}

void EmuThread::enumerateInputDevices()
{
	if (!isOnEmuThread())
	{
		QMetaObject::invokeMethod(this, &EmuThread::enumerateInputDevices, Qt::QueuedConnection);
		return;
	}

	const std::vector<std::pair<std::string, std::string>> devs(InputManager::EnumerateDevices());
	QList<QPair<QString, QString>> qdevs;
	qdevs.reserve(devs.size());
	for (const std::pair<std::string, std::string>& dev : devs)
		qdevs.emplace_back(QString::fromStdString(dev.first), QString::fromStdString(dev.second));

	onInputDevicesEnumerated(qdevs);
}

void EmuThread::enumerateVibrationMotors()
{
	if (!isOnEmuThread())
	{
		QMetaObject::invokeMethod(this, &EmuThread::enumerateVibrationMotors, Qt::QueuedConnection);
		return;
	}

	const std::vector<InputBindingKey> motors(InputManager::EnumerateMotors());
	QList<InputBindingKey> qmotors;
	qmotors.reserve(motors.size());
	for (InputBindingKey key : motors)
		qmotors.push_back(key);

	onVibrationMotorsEnumerated(qmotors);
}

void EmuThread::connectDisplaySignals(DisplayWidget* widget)
{
	widget->disconnect(this);

	connect(widget, &DisplayWidget::windowFocusEvent, this, &EmuThread::onDisplayWindowFocused);
	connect(widget, &DisplayWidget::windowResizedEvent, this, &EmuThread::onDisplayWindowResized);
	// connect(widget, &DisplayWidget::windowRestoredEvent, this, &EmuThread::redrawDisplayWindow);
	connect(widget, &DisplayWidget::windowClosedEvent, []() { g_emu_thread->shutdownVM(true, true); });
	connect(widget, &DisplayWidget::windowKeyEvent, this, &EmuThread::onDisplayWindowKeyEvent);
	connect(widget, &DisplayWidget::windowMouseMoveEvent, this, &EmuThread::onDisplayWindowMouseMoveEvent);
	connect(widget, &DisplayWidget::windowMouseButtonEvent, this, &EmuThread::onDisplayWindowMouseButtonEvent);
	connect(widget, &DisplayWidget::windowMouseWheelEvent, this, &EmuThread::onDisplayWindowMouseWheelEvent);
}

void EmuThread::onDisplayWindowMouseMoveEvent(int x, int y) {}

void EmuThread::onDisplayWindowMouseButtonEvent(int button, bool pressed)
{
	InputManager::InvokeEvents(InputManager::MakeHostMouseButtonKey(button), pressed ? 1.0f : 0.0f);
}

void EmuThread::onDisplayWindowMouseWheelEvent(const QPoint& delta_angle) {}

void EmuThread::onDisplayWindowKeyEvent(int key, int mods, bool pressed)
{
	InputManager::InvokeEvents(InputManager::MakeHostKeyboardKey(key), pressed ? 1.0f : 0.0f);
}

void EmuThread::onDisplayWindowResized(int width, int height, float scale)
{
	if (!VMManager::HasValidVM())
		return;

	GetMTGS().ResizeDisplayWindow(width, height, scale);
}

void EmuThread::onDisplayWindowFocused() {}

void EmuThread::updateDisplay()
{
	pxAssertRel(!isOnEmuThread(), "Not on emu thread");

	// finished with the display for now
	HostDisplay* display = Host::GetHostDisplay();
	display->DoneRenderContextCurrent();

	// but we should get it back after this call
	DisplayWidget* widget = onUpdateDisplayRequested(m_is_fullscreen, !m_is_fullscreen && m_is_rendering_to_main);
	if (!widget || !display->MakeRenderContextCurrent())
	{
		pxFailRel("Failed to recreate context after updating");
		return;
	}

	// this is always a new widget, so reconnect it
	connectDisplaySignals(widget);
}

HostDisplay* EmuThread::acquireHostDisplay(HostDisplay::RenderAPI api)
{
	s_host_display = HostDisplay::CreateDisplayForAPI(api);
	if (!s_host_display)
		return nullptr;

	DisplayWidget* widget = emit onCreateDisplayRequested(m_is_fullscreen, m_is_rendering_to_main);
	if (!widget)
	{
		s_host_display.reset();
		return nullptr;
	}

	connectDisplaySignals(widget);

	if (!s_host_display->MakeRenderContextCurrent())
	{
		Console.Error("Failed to make render context current");
		releaseHostDisplay();
		return nullptr;
	}

	if (!s_host_display->InitializeRenderDevice(StringUtil::wxStringToUTF8String(EmuFolders::Cache.ToString()), false) ||
		!ImGuiManager::Initialize())
	{
		Console.Error("Failed to initialize device/imgui");
		releaseHostDisplay();
		return nullptr;
	}

	g_gs_window_info = s_host_display->GetWindowInfo();

	return s_host_display.get();
}

void EmuThread::releaseHostDisplay()
{
	ImGuiManager::Shutdown();

	if (s_host_display)
	{
		s_host_display->DestroyRenderSurface();
		s_host_display->DestroyRenderDevice();
	}

	g_gs_window_info = WindowInfo();

	emit onDestroyDisplayRequested();

	s_host_display.reset();
}

HostDisplay* Host::GetHostDisplay()
{
	return s_host_display.get();
}

HostDisplay* Host::AcquireHostDisplay(HostDisplay::RenderAPI api)
{
	return g_emu_thread->acquireHostDisplay(api);
}

void Host::ReleaseHostDisplay()
{
	g_emu_thread->releaseHostDisplay();
}

bool Host::BeginPresentFrame(bool frame_skip)
{
	return s_host_display->BeginPresent(frame_skip);
}

void Host::EndPresentFrame()
{
	ImGuiManager::RenderOSD();
	s_host_display->EndPresent();
	ImGuiManager::NewFrame();
}

void Host::ResizeHostDisplay(u32 new_window_width, u32 new_window_height, float new_window_scale)
{
	s_host_display->ResizeRenderWindow(new_window_width, new_window_height, new_window_scale);
	ImGuiManager::WindowResized();
}

void Host::RequestResizeHostDisplay(s32 width, s32 height)
{
	g_emu_thread->onResizeDisplayRequested(width, height);
}

void Host::UpdateHostDisplay()
{
	g_emu_thread->updateDisplay();
	ImGuiManager::WindowResized();
}

void Host::OnVMStarting()
{
	g_emu_thread->stopBackgroundControllerPollTimer();
	emit g_emu_thread->onVMStarting();
}

void Host::OnVMStarted()
{
	emit g_emu_thread->onVMStarted();
}

void Host::OnVMDestroyed()
{
	emit g_emu_thread->onVMStopped();
	g_emu_thread->startBackgroundControllerPollTimer();
}

void Host::OnVMPaused()
{
	g_emu_thread->startBackgroundControllerPollTimer();
	emit g_emu_thread->onVMPaused();
}

void Host::OnVMResumed()
{
	// exit the event loop when we eventually return
	g_emu_thread->getEventLoop()->quit();
	g_emu_thread->stopBackgroundControllerPollTimer();
	emit g_emu_thread->onVMResumed();
}

void Host::OnGameChanged(const std::string& disc_path, const std::string& game_serial, const std::string& game_name,
	u32 game_crc)
{
	emit g_emu_thread->onGameChanged(QString::fromStdString(disc_path), QString::fromStdString(game_serial),
		QString::fromStdString(game_name), game_crc);
}

void Host::OnSaveStateLoading(const std::string_view& filename)
{
	emit g_emu_thread->onSaveStateLoading(QtUtils::StringViewToQString(filename));
}

void Host::OnSaveStateLoaded(const std::string_view& filename, bool was_successful)
{
	emit g_emu_thread->onSaveStateLoaded(QtUtils::StringViewToQString(filename), was_successful);
}

void Host::OnSaveStateSaved(const std::string_view& filename)
{
	emit g_emu_thread->onSaveStateSaved(QtUtils::StringViewToQString(filename));
}

void Host::PumpMessagesOnCPUThread()
{
	g_emu_thread->getEventLoop()->processEvents(QEventLoop::AllEvents);
}

ScopedVMPause::ScopedVMPause(bool was_paused)
	: m_was_paused(was_paused)
{
	if (!m_was_paused)
		g_emu_thread->setVMPaused(true);
}

ScopedVMPause::~ScopedVMPause()
{
	if (m_was_paused)
		g_emu_thread->setVMPaused(false);
}

alignas(16) static SysMtgsThread s_mtgs_thread;

SysMtgsThread& GetMTGS()
{
	return s_mtgs_thread;
}

// ------------------------------------------------------------------------
// Hotkeys
// ------------------------------------------------------------------------

BEGIN_HOTKEY_LIST(g_host_hotkeys)
DEFINE_HOTKEY("Screenshot", "General", "Save Screenshot", [](bool pressed) {
	if (!pressed)
	{
		// TODO
	}
})
DEFINE_HOTKEY("TogglePause", "System", "Toggle Pause", [](bool pressed) {
	if (!pressed)
		g_emu_thread->setVMPaused(VMManager::GetState() != VMState::Paused);
})
DEFINE_HOTKEY("ToggleFullscreen", "General", "Toggle Fullscreen", [](bool pressed) {
	if (!pressed)
		g_emu_thread->toggleFullscreen();
})
END_HOTKEY_LIST()
