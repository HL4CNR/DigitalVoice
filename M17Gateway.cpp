/*
 *   Copyright (c) 2020 by Thomas A. Early N7TAE
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

#include <sys/select.h>

#include <string>
#include <sstream>
#include <iostream>
#include <fstream>
#include <cstring>

#include "M17Gateway.h"

#ifndef CFG_DIR
#define CFG_DIR "/tmp/"
#endif

CM17Gateway::CM17Gateway()
{

}

CM17Gateway::~CM17Gateway()
{
	AM2M17.Close();
	ipv4.Close();
	ipv6.Close();
}

bool CM17Gateway::Init(const CFGDATA &cfgdata, CM17RouteMap *map)
{
	std::string path(CFG_DIR);
	path.append("qn.db");
	if (qnDB.Open(path.c_str()))
		return true;
	if (AM2M17.Open("am2m17"))
		return true;
	M172AM.SetUp("m172am");
	LogInput.SetUp("log_input");
	if (cfgdata.eNetType != EInternetType::ipv6only) {
		if (ipv4.Open(CSockAddress(AF_INET, 17000, "any")))
			return true;
	}
	if (cfgdata.eNetType != EInternetType::ipv4only) {
		if (ipv6.Open(CSockAddress(AF_INET6, 17000, "any")))
			return true;
	}
	keep_running = true;
	CConfigure config;
	config.CopyFrom(cfgdata);
	config.CopyTo(cfg);
	routeMap = map;
	return false;
}

void CM17Gateway::LinkCheck()
{
	if (mlink.receivePingTimer.time() > 30) { // is the reflector okay?
		// looks like we lost contact
		std::stringstream ss("Unlinked from ");
		ss << mlink.cs.GetCS() << ", TIMEOUT...";
		LogInput.Write(ss.str());
		qnDB.DeleteLS(mlink.addr.GetAddress());
		mlink.state = ELinkState::unlinked;
		mlink.addr.Clear();
	}
}

void CM17Gateway::StreamTimeout()
{
	// set the frame number
	uint16_t fn = (currentStream.header.framenumber + 1) % 0x8000u;
	currentStream.header.framenumber = fn | 0x8000u;
	// fill in a silent codec2
	switch (currentStream.header.lich.frametype & 0x6u) {
	case 0x4u: { //3200
			uint8_t silent[] = { 0x01u, 0x00u, 0x09u, 0x43u, 0x9cu, 0xe4u, 0x21u, 0x08u };
			memcpy(currentStream.header.payload,   silent, 8);
			memcpy(currentStream.header.payload+8, silent, 8);
		}
		break;
	case 0x6u: { // 1600
			uint8_t silent[] = { 0x01u, 0x00u, 0x04u, 0x00u, 0x25u, 0x75u, 0xddu, 0xf2u };
			memcpy(currentStream.header.payload, silent, 8);
		}
		break;
	default:
		break;
	}
	// calculate the crc
	currentStream.header.crc = crc.CalcCRC(currentStream.header.magic, sizeof(SM17Frame) - 2);
	// send the packet
	M172AM.Write(currentStream.header.magic, sizeof(SM17Frame));
	// close the stream;
	currentStream.header.streamid = 0;
}

void CM17Gateway::PlayVoiceFile()
{
		// play a qnvoice file if it is specified
		// this could be coming from qnvoice or qngateway (connected2network or notincache)
		std::ifstream voicefile(qnvoice_file.c_str(), std::ifstream::in);
		if (voicefile) {
			if (keep_running) {
				char line[FILENAME_MAX];
				voicefile.getline(line, FILENAME_MAX);
				// trim whitespace
				char *start = line;
				while (isspace(*start))
					start++;
				char *end = start + strlen(start) - 1;
				while (isspace(*end))
					*end-- = (char)0;
				// anthing reasonable left?
				if (strlen(start) > 2)
					PlayAudioNotifyMessage(start);
			}
			//clean-up
			voicefile.close();
			remove(qnvoice_file.c_str());
		}

}

void CM17Gateway::PlayAudioNotifyMessage(const char *msg)
{
	if (strlen(msg) > sizeof(SM17Frame) - 5) {
		fprintf(stderr, "Audio Message string too long: %s", msg);
		return;
	}
	SM17Frame frame;
	memcpy(frame.magic, "PLAY", 4);
	memcpy(frame.magic+4, msg, strlen(msg)+1);	// copy the terminating NULL
	M172AM.Write(frame.magic, sizeof(SM17Frame));
}

void CM17Gateway::Process()
{
	fd_set fdset;
	timeval tv;
	int max_nfds = 0;
	const auto ip4fd = ipv4.GetSocket();
	const auto ip6fd = ipv6.GetSocket();
	const auto amfd = AM2M17.GetFD();
	if ((EInternetType::ipv6only != cfg.eNetType) && (ip4fd > max_nfds))
		max_nfds = ip4fd;
	if ((EInternetType::ipv4only != cfg.eNetType) && (ip6fd > max_nfds))
		max_nfds = ip6fd;
	if (amfd > max_nfds)
		max_nfds = amfd;
	while (keep_running) {
		if (ELinkState::linked == mlink.state)
			LinkCheck();
		if (currentStream.header.streamid && currentStream.lastPacketTime.time() >= 2.0) {
			StreamTimeout(); // current stream has timed out
		}
		PlayVoiceFile(); // play if there is any msg to play

		FD_ZERO(&fdset);
		if (EInternetType::ipv6only != cfg.eNetType)
			FD_SET(ip4fd, &fdset);
		if (EInternetType::ipv4only != cfg.eNetType)
			FD_SET(ip6fd, &fdset);
		FD_SET(amfd, &fdset);
		tv.tv_sec = 0;
		tv.tv_usec = 40000;	// wait up to 40 ms for something to happen
		auto rval = select(max_nfds + 1, &fdset, 0, 0, &tv);
		if (0 > rval) {
			std::cerr << "select() error: " << strerror(errno) << std::endl;
			return;
		}

		bool is_packet = false;
		uint8_t buf[100];
		CSockAddress from17k;
		socklen_t fromlen = sizeof(struct sockaddr_storage);
		int length;

		if (keep_running && (ip4fd >= 0) && FD_ISSET(ip4fd, &fdset)) {
			length = recvfrom(ip4fd, buf, 100, 0, from17k.GetPointer(), &fromlen);
			is_packet = true;
			FD_CLR(ip4fd, &fdset);
		}

		if (keep_running && (ip6fd >= 0) && FD_ISSET(ip6fd, &fdset)) {
			length = recvfrom(ip6fd, buf, 100, 0, from17k.GetPointer(), &fromlen);
			is_packet = true;
			FD_CLR(ip6fd, &fdset);
		}

		if (keep_running && is_packet) {
			switch (length) {
			case 4:  				// DISC, ACKN or NACK
				if ((ELinkState::linking == mlink.state) && (from17k == mlink.addr)) {
					if (0 == memcmp(buf, "ACKN", 4)) {
						mlink.state = ELinkState::linked;
						qnDB.UpdateLS(mlink.addr.GetAddress(), mlink.from_mod, mlink.cs.GetCS(8).c_str(), mlink.cs.GetModule(), time(NULL));
						mlink.receivePingTimer.start();
					} else if (0 == memcmp(buf, "NACK", 4)) {
						mlink.state = ELinkState::unlinked;
						std::string msg("Link request refused from ");
						msg += mlink.cs.GetCS();
						LogInput.Write(msg.c_str());
					} else if (0 == memcmp(buf, "DISC", 4)) {
						;
					} else {
						is_packet = false;
					}
				} else {
					is_packet = false;
				}
				break;
			case 10: 				// PING or DISC
				if ((ELinkState::linked == mlink.state) && (from17k == mlink.addr)) {
					if (0 == memcmp(buf, "PING", 4)) {
						Send(mlink.pongPacket.magic, 10, mlink.addr);
						mlink.receivePingTimer.start();
					} else if (0 == memcmp(buf, "DISC", 4)) {
						mlink.state = ELinkState::unlinked;
						qnDB.DeleteLS(mlink.addr.GetAddress());
					} else {
						is_packet = false;
					}
				}
				break;
			case sizeof(SM17Frame):	// An M17 frame
				is_packet = ProcessFrame(buf);
				break;
			default:
				is_packet = false;
				break;
			}
			if (! is_packet)
				std::cout << "Unknown packet size=" << length << " from " << from17k.GetAddress() << std::endl;
		}

		if (keep_running && FD_ISSET(amfd, &fdset)) {
			SM17Frame frame;
			length = AM2M17.Read(frame.magic, sizeof(SM17Frame));
			const CCallsign dest(frame.lich.addr_dst);
			if (0 == dest.GetCS(3).compare("M17")) { // Linking a reflector
				switch (mlink.state) {
				case ELinkState::linked:
					if (mlink.cs == dest) { // this is heading in to the correct desination
						Write(frame.magic, sizeof(SM17Frame), mlink.addr);
					}
					break;
				case ELinkState::unlinked:
					if ('L' == dest.GetCS().at(8)) {
						std::string ref(dest.GetCS(7));
						ref.resize(8, ' ');
						ref.resize(9, dest.GetModule());
						SendLinkRequest(ref);
					}
					break;
				default:
					break;
				}
			} else if (0 == dest.GetCS().compare("U        ")) {
				SM17RefPacket disc;
				memcpy(disc.magic, "DISC", 4);
				mlink.cs.GetCode(disc.cs);
				Write(disc.magic, 10, mlink.addr);
			} else {
				const auto addr = routeMap->Find(dest.GetCS());
				if (addr)
					Write(frame.magic, sizeof(SM17Frame), *addr);
			}
			FD_CLR(amfd, &fdset);
		}
	}
}

void CM17Gateway::SendLinkRequest(const CCallsign &ref)
{
	mlink.addr.Initialize((std::string::npos == cfg.sM17DestIp.find(':')) ? AF_INET : AF_INET6, 17000, cfg.sM17DestIp.c_str());
	mlink.cs = ref;
	mlink.from_mod = cfg.cModule;

	// make a CONN packet
	SM17RefPacket conn;
	memcpy(conn.magic, "CONN", 4);
	std::string source(cfg.sM17SourceCallsign);
	source.resize(8, ' ');
	source.append(1, cfg.cModule);
	const CCallsign from(source);
	from.GetCode(conn.cs);
	conn.mod = ref.GetModule();
	Write(conn.magic, 11, mlink.addr);	// send the link request

	// go ahead and make the pong packet
	memcpy(mlink.pongPacket.magic, "PONG", 4);
	from.GetCode(mlink.pongPacket.cs);

	// finish up
	mlink.state = ELinkState::linking;
	linkingTime.start();
}

bool CM17Gateway::ProcessFrame(const uint8_t *buf)
{
	SM17Frame frame;
	memcpy(frame.magic, buf, sizeof(SM17Frame));
	if (currentStream.header.streamid) {
		if (currentStream.header.streamid == frame.streamid) {
			M172AM.Write(frame.magic, sizeof(SM17Frame));
			currentStream.header.framenumber = frame.framenumber;
			uint16_t fn = ntohs(frame.framenumber);
			if (fn & 0x8000u) {
				currentStream.header.framenumber = 0; // close the stream
			} else {
				currentStream.lastPacketTime.start();
			}
		} else {
			return false;
		}
	} else {
		// here comes a first packet, so init the currentStream
		auto check = crc.CalcCRC(frame.magic, sizeof(SM17Frame) - 2);
		std::cout << "Header Packet crc=0x" << std::hex << frame.crc << " calculate=0x" << std::hex << check;
		memcpy(currentStream.header.magic, frame.magic, sizeof(SM17Frame));
		M172AM.Write(frame.magic, sizeof(SM17Frame));
		currentStream.lastPacketTime.start();
	}
	return true;
}

void CM17Gateway::Write(const void *buf, const size_t size, const CSockAddress &addr) const
{
	if (AF_INET6 == addr.GetFamily())
		ipv6.Write(buf, size, addr);
	else
		ipv4.Write(buf, size, addr);
}

void CM17Gateway::PlayAudioMessage(const char *msg)
{
	auto len = strlen(msg);
	if (len > sizeof(SM17Frame)-5) {
		fprintf(stderr, "Audio Message string too long: %s", msg);
		return;
	}
	SM17Frame m17;
	memcpy(m17.magic, "PLAY", 4);
	memcpy(m17.magic+4, msg, len+1);	// copy the terminating NULL
	M172AM.Write(m17.magic, sizeof(SM17Frame));
}

void CM17Gateway::Send(const void *buf, size_t size, const CSockAddress &addr) const
{
	if (AF_INET ==  addr.GetFamily())
		ipv4.Write(buf, size, addr);
	else
		ipv6.Write(buf, size, addr);
}
