/*
 *   Copyright (c) 2019 by Thomas A. Early N7TAE
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#pragma once

#include <set>
#include <gtkmm.h>

class CMainWindow
{
public:
	CMainWindow();
	~CMainWindow();

	bool Init(const Glib::RefPtr<Gtk::Builder>, const Glib::ustring &);
	void Run();
protected:
	// state data
	std::set<Glib::ustring> routeset;
	// helpers
	void ReadRoutes();
	// events
	void on_QuitButton_clicked();
	void on_SettingsButton_clicked();
	// widgets
	Gtk::Window *pWin;
	Gtk::Button *pQuitButton, *pSettingsButton, *pQuickKeyButton, *pLinkButton, *pUnlinkButton, *pRouteActionButton;
	Gtk::ComboBoxText *pRouteComboBox;
	Gtk::RadioButton *pRouteRadioButton, *pLinkRadioButton;
	Gtk::Entry *pLinkEntry, *pRouteEntry;
	Gtk::ToggleButton *pEchoTestButton, *pPTTButton;
	Gtk::TextBuffer *pLogTextBuffer;
	Gtk::TextView *pLogTextView;
	// events
	void on_RouteActionButton_clicked();
	void on_RouteComboBox_changed();
	void on_RouteEntry_changed();
	void on_EchoTestButton_toggled();
};
