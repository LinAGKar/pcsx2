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

#pragma once

#include <QtWidgets/QWidget>

#include "ui_GraphicsSettingsWidget.h"

class SettingsDialog;

class GraphicsSettingsWidget : public QWidget
{
	Q_OBJECT

public:
	GraphicsSettingsWidget(QWidget* parent, SettingsDialog* dialog);
	~GraphicsSettingsWidget();

	void updateRendererDependentOptions();

Q_SIGNALS:
	void fullscreenModesChanged(const QStringList& modes);

private Q_SLOTS:
	void onRendererChanged(int index);
	void onAdapterChanged(int index);
	void onEnableHardwareFixesChanged();
	void onIntegerScalingChanged();
	void onFullscreenModeChanged(int index);

private:
	Ui::GraphicsSettingsWidget m_ui;

	bool m_hardware_renderer_visible = true;
	bool m_software_renderer_visible = true;
};
