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

#include <iostream>
#include "SettingsDlg.h"

// globals
extern Glib::RefPtr<Gtk::Application> theApp;

CSettingsDlg::CSettingsDlg() :
	pDlg(nullptr)
{
}

CSettingsDlg::~CSettingsDlg()
{
	if (pDlg)
		delete pDlg;
}

bool CSettingsDlg::Init(const Glib::RefPtr<Gtk::Builder> builder, const Glib::ustring &name, Gtk::Window *pWin)
{
	builder->get_widget(name, pDlg);
	if (nullptr == pDlg) {
		std::cerr << "Failed to initialize SettingsDialog!" << std::endl;
		return true;
	}
	pDlg->set_transient_for(*pWin);
	pDlg->add_button("Cancel", Gtk::RESPONSE_CANCEL);
	pDlg->add_button("Okay", Gtk::RESPONSE_OK);

	builder->get_widget("UseMyCallsignCheckButton", pUseMyCall);
	if (nullptr == pUseMyCall) {
		std::cerr << "Failed to get UserMyCallsignCheckButton!" << std::endl;
		return true;
	}
	pUseMyCall->signal_clicked().connect(sigc::mem_fun(*this, &CSettingsDlg::on_UseMyCallsignCheckButton_clicked));

	builder->get_widget("StationCallsignEntry", pStationCallsign);
	if (nullptr == pStationCallsign)  {
		std::cerr << "Failed to get StationCallsignEntry" << std::endl;
		return true;
	}

	builder->get_widget("MyCallsignEntry", pMyCallsign);
	if (nullptr == pMyCallsign)  {
		std::cerr << "Failed to get MyCallsignEntry" << std::endl;
		return true;
	}
	pMyCallsign->signal_changed().connect(sigc::mem_fun(*this, &CSettingsDlg::on_MyCallsignEntry_changed));

	builder->get_widget("MyNameEntry", pMyName);
	if (nullptr == pMyName)  {
		std::cerr << "Failed to get MyNameEntry" << std::endl;
		return true;
	}
	pMyName->signal_changed().connect(sigc::mem_fun(*this, &CSettingsDlg::on_MyNameEntry_changed));

	builder->get_widget("StationCallsignEntry", pStationCallsign);
	if (nullptr == pStationCallsign)  {
		std::cerr << "Failed to get StationCallsignEntry" << std::endl;
		return true;
	}
	pStationCallsign->signal_changed().connect(sigc::mem_fun(*this, &CSettingsDlg::on_StationCallsignEntry_changed));

	builder->get_widget("MessageEntry", pMessage);
	if (nullptr == pMessage)  {
		std::cerr << "Failed to get MessageEntry" << std::endl;
		return true;
	}
	pMessage->signal_changed().connect(sigc::mem_fun(*this, &CSettingsDlg::on_MessageEntry_changed));

	return false;
}

void CSettingsDlg::Show()
{
	switch(pDlg->run()) {
		case Gtk::RESPONSE_CANCEL:
			std::cout << "Clicked Cancel!" << std::endl;
			break;
		case Gtk::RESPONSE_OK:
			std::cout << "Clicked OK!" << std::endl;
			break;
		default:
			std::cout << "Unexpected return from dlg->run()!" << std::endl;
			break;
	}
	pDlg->hide();
}

void CSettingsDlg::on_UseMyCallsignCheckButton_clicked()
{
	if (pUseMyCall->get_active()) {
		pStationCallsign->hide();
		pStationCallsign->set_text(pMyCallsign->get_text());
	} else {
		pStationCallsign->show();
	}
}

void CSettingsDlg::on_MyCallsignEntry_changed()
 {
	 int pos = pMyCallsign->get_position();
	 Glib::ustring s = pMyCallsign->get_text();
	 pMyCallsign->set_text(s.uppercase());
	 pMyCallsign->set_position(pos);
	 if (pUseMyCall->get_active())
	 	pStationCallsign->set_text(s);
 }

void CSettingsDlg::on_MyNameEntry_changed()
 {
	 int pos = pMyName->get_position();
	 Glib::ustring s = pMyName->get_text();
	 pMyName->set_text(s.uppercase());
	 pMyName->set_position(pos);
 }

void CSettingsDlg::on_StationCallsignEntry_changed()
 {
	 int pos = pStationCallsign->get_position();
	 Glib::ustring s = pStationCallsign->get_text();
	 pStationCallsign->set_text(s.uppercase());
	 pStationCallsign->set_position(pos);
 }

void CSettingsDlg::on_MessageEntry_changed()
{
	int pos = pMessage->get_position();
	const Glib::ustring good(" ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-/.,:_");
	Glib::ustring s = pMessage->get_text();
	Glib::ustring n;
	for (auto it=s.begin(); it!=s.end(); it++) {
		if (Glib::ustring::npos != good.find(*it)) {
			n.append(1, *it);
		}
	}
	pMessage->set_text(n);
	pMessage->set_position(pos);
}
