#pragma once

/*
 *   Copyright (C) 2016,2020 by Thomas A. Early N7TAE
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

#include <string>
#include <future>
#include <atomic>

#include "TypeDefs.h"
#include "TCPReaderWriterClient.h"
#include "Configure.h"
#include "Base.h"

class CAPRS : CBase
{
public:
	// functions
	CAPRS();
	~CAPRS();
	void UpdateUser();
	void Init();
	void Close();

private:
	// data
	CFGDATA cfgdata;
 	std::future<void> aprs_future;
	time_t last_time;
	std::string station;
	std::atomic<bool> keep_running;

	// classes
	CConfigure cfg;
	CTCPReaderWriterClient aprs_sock;

	// functions
	int compute_aprs_hash();
	void APRSBeaconThread();
	void Open();
	void CloseSock();
	void FinishThread();
};
