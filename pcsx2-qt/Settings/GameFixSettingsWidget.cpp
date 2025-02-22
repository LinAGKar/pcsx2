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

#include <QtWidgets/QMessageBox>
#include <algorithm>

#include "GameFixSettingsWidget.h"
#include "EmuThread.h"
#include "QtUtils.h"
#include "SettingWidgetBinder.h"
#include "SettingsDialog.h"

GameFixSettingsWidget::GameFixSettingsWidget(QWidget* parent, SettingsDialog* dialog)
	: QWidget(parent)
{
	m_ui.setupUi(this);

	SettingWidgetBinder::BindWidgetToBoolSetting(m_ui.FpuMulHack, "EmuCore/Gamefixes", "FpuMulHack", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(m_ui.FpuNegDivHack, "EmuCore/Gamefixes", "FpuNegDivHack", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(m_ui.GoemonTlbHack, "EmuCore/Gamefixes", "GoemonTlbHack", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(m_ui.SoftwareRendererFMVHack, "EmuCore/Gamefixes", "SoftwareRendererFMVHack", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(m_ui.SkipMPEGHack, "EmuCore/Gamefixes", "SkipMPEGHack", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(m_ui.OPHFlagHack, "EmuCore/Gamefixes", "OPHFlagHack", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(m_ui.EETimingHack, "EmuCore/Gamefixes", "EETimingHack", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(m_ui.DMABusyHack, "EmuCore/Gamefixes", "DMABusyHack", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(m_ui.GIFFIFOHack, "EmuCore/Gamefixes", "GIFFIFOHack", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(m_ui.VIFFIFOHack, "EmuCore/Gamefixes", "VIFFIFOHack", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(m_ui.VIF1StallHack, "EmuCore/Gamefixes", "VIF1StallHack", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(m_ui.VuAddSubHack, "EmuCore/Gamefixes", "VuAddSubHack", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(m_ui.IbitHack, "EmuCore/Gamefixes", "IbitHack", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(m_ui.VUKickstartHack, "EmuCore/Gamefixes", "VUKickstartHack", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(m_ui.VUOverflowHack, "EmuCore/Gamefixes", "VUOverflowHack", false);
	SettingWidgetBinder::BindWidgetToBoolSetting(m_ui.XgKickHack, "EmuCore/Gamefixes", "XgKickHack", false);
}

GameFixSettingsWidget::~GameFixSettingsWidget() = default;
