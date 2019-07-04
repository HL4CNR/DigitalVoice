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

#define ALSA_PCM_NEW_HW_PARAMS_API
#include <alsa/asoundlib.h>
#include <netinet/in.h>

#include <iostream>

#include "Configure.h"
#include "AudioManager.h"

// globals
extern CConfigure cfg;

void CAudioManager::RecordMicThread(E_PTT_Type for_who, const std::string &urcall)
{
	hot_mic = true;
	audio_queue.Clear();
	ambe_queue.Clear();
	a2d_queue.Clear();
	gateway_queue.Clear();
	link_queue.Clear();

	r1 = std::async(std::launch::async, &CAudioManager::microphone2audioqueue, this);

	r2 = std::async(std::launch::async, &CAudioManager::audioqueue2ambedevice, this);

	switch (for_who) {
		case E_PTT_Type::echo:
			r3 = std::async(std::launch::async, &CAudioManager::ambedevice2ambequeue, this);
			break;
		case E_PTT_Type::gateway:
			r3 = std::async(std::launch::async, &CAudioManager::ambedevice2packetqueue, this, std::ref(gateway_queue), std::ref(gateway_mutex), urcall);
			break;
		case E_PTT_Type::link:
			r3 = std::async(std::launch::async, &CAudioManager::ambedevice2packetqueue, this, std::ref(link_queue), std::ref(link_mutex), urcall);
	}
}

void CAudioManager::makeheader(CDSVT &c, const std::string &urcall)
{
	CFGDATA data;
	cfg.CopyTo(data);
	memset(c.title, 0, sizeof(CDSVT));
	memcpy(c.title, "DSVT", 4);
	c.config = 0x10U;
	c.id = 0x20U;
	c.flagb[2] = 1U;
	c.streamid = htons(random.NewStreamID());
	c.ctrl = 0x80U;
	memset(c.hdr.flag+3, ' ', 36);
	memcpy(c.hdr.rpt1, data.sStation.c_str(), data.sStation.size());
	memcpy(c.hdr.rpt2, c.hdr.rpt1, 8);
	c.hdr.rpt1[7] = data.cModule;
	c.hdr.rpt2[7] = 'G';
	memcpy(c.hdr.urcall, urcall.c_str(), urcall.size());
	memcpy(c.hdr.mycall, data.sCallsign.c_str(), data.sCallsign.size());
	calcPFCS(c.hdr.flag, c.hdr.pfcs);
}

void CAudioManager::GetPacket4Gateway(CDSVT &packet)
{
	gateway_mutex.lock();
	packet = gateway_queue.Pop();
	gateway_mutex.unlock();
}

void CAudioManager::GetPacket4Link(CDSVT &packet)
{
	link_mutex.lock();
	packet = link_queue.Pop();
	link_mutex.unlock();
}

void CAudioManager::microphone2audioqueue()
{
	// Open PCM device for recording (capture).
	snd_pcm_t *handle;
	int rc = snd_pcm_open(&handle, "default", SND_PCM_STREAM_CAPTURE, 0);
	if (rc < 0) {
		std::cerr << "unable to open pcm device: " << snd_strerror(rc) << std::endl;
		return;
	}
	// Allocate a hardware parameters object.
	snd_pcm_hw_params_t *params;
	snd_pcm_hw_params_alloca(&params);

	// Fill it in with default values.
	snd_pcm_hw_params_any(handle, params);

	// Set the desired hardware parameters.

	// Interleaved mode
	snd_pcm_hw_params_set_access(handle, params, SND_PCM_ACCESS_RW_INTERLEAVED);

	// Signed 16-bit little-endian format
	snd_pcm_hw_params_set_format(handle, params, SND_PCM_FORMAT_S16_LE);

	// One channels (mono)
	snd_pcm_hw_params_set_channels(handle, params, 1);

	// 8000 samples/second
	snd_pcm_hw_params_set_rate(handle, params, 8000, 0);

	// Set period size to 160 frames.
	snd_pcm_uframes_t frames = 160;
	snd_pcm_hw_params_set_period_size(handle, params, frames, 0);

	// Write the parameters to the driver
	rc = snd_pcm_hw_params(handle, params);
	if (rc < 0) {
		std::cerr << "unable to set hw parameters: " << snd_strerror(rc) << std::endl;
		return;
	}

	unsigned count = 0U;
	bool keep_running;
	do {
		short int audio_buffer[frames];
		rc = snd_pcm_readi(handle, audio_buffer, frames);
		//std::cout << "audio:" << count << " hot_mic:" << hot_mic << std::endl;
		if (rc == -EPIPE) {
			// EPIPE means overrun
			std::cerr << "overrun occurred" << std::endl;
			snd_pcm_prepare(handle);
		} else if (rc < 0) {
			std::cerr << "error from readi: " << snd_strerror(rc) << std::endl;
		} else if (rc != int(frames)) {
			std::cerr << "short readi, read " << rc << " frames" << std::endl;
		}
		keep_running = hot_mic;
		unsigned char seq = count % 21;
		if (! keep_running)
			seq |= 0x40U;
		CAudioFrame frame(audio_buffer);
		frame.SetSequence(seq);
		audio_mutex.lock();
		audio_queue.Push(frame);
		audio_mutex.unlock();
		count++;
	} while (keep_running);
//	std::cout << count << " frames by microphone2audioqueue\n";
	snd_pcm_drop(handle);
	snd_pcm_close(handle);
}

void CAudioManager::audioqueue2ambedevice()
{
	unsigned char seq = 0U;
	do {
		while (audio_is_empty())
			std::this_thread::sleep_for(std::chrono::milliseconds(3));
		audio_mutex.lock();
		CAudioFrame frame(audio_queue.Pop());
		audio_mutex.unlock();
		if (! AMBEDevice.IsOpen())
			return;
		if(AMBEDevice.SendAudio(frame.GetData()))
			break;
		seq = frame.GetSequence();
		a2d_mutex.lock();
		a2d_queue.Push(frame.GetSequence());
		a2d_mutex.unlock();
		//std::cout << "audio2ambedev seq:" << std::hex << unsigned(seq) << std::dec << std::endl;
	} while (0U == (seq & 0x40U));
//	std::cout << "audioqueue2ambedevice is finished\n";
}

void CAudioManager::ambedevice2ambequeue()
{
	unsigned char seq = 0U;
	do {
		unsigned char ambe[9];
		if (! AMBEDevice.IsOpen())
			return;
		if (AMBEDevice.GetData(ambe))
			break;
		CAMBEFrame frame(ambe);
		a2d_mutex.lock();
		seq = a2d_queue.Pop();
		a2d_mutex.unlock();
		frame.SetSequence(seq);
		ambe_mutex.lock();
		ambe_queue.Push(frame);
		ambe_mutex.unlock();
		//std::cout << "ambedev2ambeque seq:" << std::hex << unsigned(seq) << std::dec << std::endl;
	} while (0U == (seq & 0x40U));
	//std::cout << "amebedevice2ambequeue is finished\n";
	return;
}

void CAudioManager::ambedevice2packetqueue(PacketQueue &queue, std::mutex &mtx, const std::string &urcall)
{
	// add a header;
	CDSVT h;
	makeheader(h, urcall);
	CDSVT v(h);
	v.config = 0x20U;
	bool header_not_sent = true;
	do {
		if (! AMBEDevice.IsOpen())
			return;
		if (AMBEDevice.GetData(v.vasd.voice))
			break;
		a2d_mutex.lock();
		v.ctrl = a2d_queue.Pop();
		a2d_mutex.unlock();
		// CHANGE THIS!!!!!!!!!!!!!!!!!!!!!!
		v.vasd.text[0] = 0x70U;
		v.vasd.text[1] = 0x4FU;
		v.vasd.text[2] = 0x93U;
		////////////////////////////////////
		mtx.lock();
		if (header_not_sent) {
			queue.Push(h);
			header_not_sent = false;
		}
		queue.Push(v);
		mtx.unlock();
	} while (0U == (v.ctrl & 0x40U));
}

void CAudioManager::PlayAMBEDataThread()
{
	hot_mic = false;
	r3.get();

	std::this_thread::sleep_for(std::chrono::milliseconds(200));

	p1 = std::async(std::launch::async, &CAudioManager::ambequeue2ambedevice, this);

	p2 = std::async(std::launch::async, &CAudioManager::ambedevice2audioqueue, this);

	p3 = std::async(std::launch::async, &CAudioManager::play_audio_queue, this);
}

void CAudioManager::Link2AudioMgr(const CDSVT &dvst)
{
	if (AMBEDevice.IsOpen()) {
		if (dvst.config == 0x10U) {
			if (link_sid_in == 0U) {
				link_sid_in = dvst.streamid;

				p1 = std::async(std::launch::async, &CAudioManager::ambequeue2ambedevice, this);
				p2 = std::async(std::launch::async, &CAudioManager::ambedevice2audioqueue, this);
				p3 = std::async(std::launch::async, &CAudioManager::play_audio_queue, this);
			} else
				link_sid_in = dvst.streamid;
			std::cout << "header streamid=" << std::hex << ntohs(dvst.streamid) << std::dec << std::endl;
			return;
		}
		if (dvst.streamid != link_sid_in)
			return;
		CAMBEFrame frame(dvst.vasd.voice);
		frame.SetSequence(dvst.ctrl);
		ambe_mutex.lock();
		ambe_queue.Push(frame);
		ambe_mutex.unlock();
		if (dvst.ctrl & 0x40U) {
			p1.get();
			p2.get();
			p3.get();
			link_sid_in = 0U;
		}
	}
}

void CAudioManager::Gateway2AudioMgr(const CDSVT &dvst)
{
	if (dvst.config == 0x10U) {
		if (gwy_sid_in == 0U) {
			gwy_sid_in = dvst.streamid;

			p1 = std::async(std::launch::async, &CAudioManager::ambequeue2ambedevice, this);
			p2 = std::async(std::launch::async, &CAudioManager::ambedevice2audioqueue, this);
			p3 = std::async(std::launch::async, &CAudioManager::play_audio_queue, this);
		} else
			gwy_sid_in = dvst.streamid;
		return;
	}
	if (AMBEDevice.IsOpen()) {
		if (dvst.streamid != gwy_sid_in)
			return;
		CAMBEFrame frame(dvst.vasd.voice);
		frame.SetSequence(dvst.ctrl);
		ambe_mutex.lock();
		ambe_queue.Push(frame);
		ambe_mutex.unlock();
		if (dvst.ctrl & 0x40U) {
			p1.get();
			p2.get();
			p3.get();
			gwy_sid_in = 0U;
		}
	}
}

void CAudioManager::ambequeue2ambedevice()
{
	unsigned char seq = 0U;
	do {
		while (ambe_is_empty())
			std::this_thread::sleep_for(std::chrono::milliseconds(3));
		ambe_mutex.lock();
		CAMBEFrame frame(ambe_queue.Pop());
		ambe_mutex.unlock();
		seq = frame.GetSequence();
		d2a_mutex.lock();
		d2a_queue.Push(seq);
		d2a_mutex.unlock();
		if (! AMBEDevice.IsOpen())
			return;
		if (AMBEDevice.SendData(frame.GetData()))
			return;
	} while (0U == (seq & 0x40U));
//	std::cout << "ambequeue2ambedevice is complete\n";
}

void CAudioManager::ambedevice2audioqueue()
{
	unsigned char seq = 0U;
	do {
		if (! AMBEDevice.IsOpen())
			return;
		short audio[160];
		if (AMBEDevice.GetAudio(audio))
			return;
		CAudioFrame frame(audio);
		d2a_mutex.lock();
		seq = d2a_queue.Pop();
		d2a_mutex.unlock();
		frame.SetSequence(seq);
		audio_mutex.lock();
		audio_queue.Push(frame);
		audio_mutex.unlock();
	} while ( 0U == (seq & 0x40U));
//	std::cout << "ambedevice2audioqueue is complete\n";
}

void CAudioManager::play_audio_queue()
{
	std::this_thread::sleep_for(std::chrono::milliseconds(300));
	// Open PCM device for playback.
	snd_pcm_t *handle;
	int rc = snd_pcm_open(&handle, "default", SND_PCM_STREAM_PLAYBACK, 0);
	if (rc < 0) {
		std::cerr << "unable to open pcm device: " << snd_strerror(rc) << std::endl;
		return;
	}

	// Allocate a hardware parameters object.
	snd_pcm_hw_params_t *params;
	snd_pcm_hw_params_alloca(&params);

	/* Fill it in with default values. */
	snd_pcm_hw_params_any(handle, params);

	// Set the desired hardware parameters.

	// Interleaved mode
	snd_pcm_hw_params_set_access(handle, params, SND_PCM_ACCESS_RW_INTERLEAVED);

	// Signed 16-bit little-endian format
	snd_pcm_hw_params_set_format(handle, params, SND_PCM_FORMAT_S16_LE);

	// One channels (mono)
	snd_pcm_hw_params_set_channels(handle, params, 1);

	// 8000 samples/second sampling rate
	snd_pcm_hw_params_set_rate(handle, params, 8000, 0);

	// Set period size to 32 frames.
	snd_pcm_uframes_t frames = 160;
	snd_pcm_hw_params_set_period_size(handle, params, frames, 0);
	//snd_pcm_hw_params_set_period_size_near(handle, params, &frames, &dir);

	// Write the parameters to the driver
	rc = snd_pcm_hw_params(handle, params);
	if (rc < 0) {
		std::cerr << "unable to set hw parameters: " << snd_strerror(rc) << std::endl;
		return;
	}

	// Use a buffer large enough to hold one period
	snd_pcm_hw_params_get_period_size(params, &frames, 0);

	unsigned char seq = 0U;
	do {
		while (audio_is_empty())
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		audio_mutex.lock();
		CAudioFrame frame(audio_queue.Pop());
		audio_mutex.unlock();
		seq = frame.GetSequence();
		rc = snd_pcm_writei(handle, frame.GetData(), frames);
		if (rc == -EPIPE) {
			// EPIPE means underrun
			std::cerr << "underrun occurred" << std::endl;
			snd_pcm_prepare(handle);
		} else if (rc < 0) {
			std::cerr <<  "error from writei: " << snd_strerror(rc) << std::endl;
		}  else if (rc != int(frames)) {
			std::cerr << "short write, write " << rc << " frames" << std::endl;
		}
	} while (0U == (seq & 0x40U));

	snd_pcm_drain(handle);
	snd_pcm_close(handle);
}

bool CAudioManager::audio_is_empty()
{
	audio_mutex.lock();
	bool ret = audio_queue.Empty();
	audio_mutex.unlock();
	return ret;
}

bool CAudioManager::ambe_is_empty()
{
	ambe_mutex.lock();
	bool ret = ambe_queue.Empty();
	ambe_mutex.unlock();
	return ret;
}

bool CAudioManager::GatewayQueueIsReady()
{
	gateway_mutex.lock();
	bool ret = gateway_queue.Empty();
	gateway_mutex.unlock();
	return !ret;
}

bool CAudioManager::LinkQueueIsReady()
{
	link_mutex.lock();
	bool ret = link_queue.Empty();
	link_mutex.unlock();
	return !ret;
}


void CAudioManager::calcPFCS(const unsigned char *packet, unsigned char *pfcs)
{
	unsigned short crc_dstar_ffff = 0xffff;
	unsigned short tmp, short_c;
	unsigned short crc_tabccitt[256] = {
		0x0000,0x1189,0x2312,0x329b,0x4624,0x57ad,0x6536,0x74bf,0x8c48,0x9dc1,0xaf5a,0xbed3,0xca6c,0xdbe5,0xe97e,0xf8f7,
		0x1081,0x0108,0x3393,0x221a,0x56a5,0x472c,0x75b7,0x643e,0x9cc9,0x8d40,0xbfdb,0xae52,0xdaed,0xcb64,0xf9ff,0xe876,
		0x2102,0x308b,0x0210,0x1399,0x6726,0x76af,0x4434,0x55bd,0xad4a,0xbcc3,0x8e58,0x9fd1,0xeb6e,0xfae7,0xc87c,0xd9f5,
		0x3183,0x200a,0x1291,0x0318,0x77a7,0x662e,0x54b5,0x453c,0xbdcb,0xac42,0x9ed9,0x8f50,0xfbef,0xea66,0xd8fd,0xc974,
		0x4204,0x538d,0x6116,0x709f,0x0420,0x15a9,0x2732,0x36bb,0xce4c,0xdfc5,0xed5e,0xfcd7,0x8868,0x99e1,0xab7a,0xbaf3,
		0x5285,0x430c,0x7197,0x601e,0x14a1,0x0528,0x37b3,0x263a,0xdecd,0xcf44,0xfddf,0xec56,0x98e9,0x8960,0xbbfb,0xaa72,
		0x6306,0x728f,0x4014,0x519d,0x2522,0x34ab,0x0630,0x17b9,0xef4e,0xfec7,0xcc5c,0xddd5,0xa96a,0xb8e3,0x8a78,0x9bf1,
		0x7387,0x620e,0x5095,0x411c,0x35a3,0x242a,0x16b1,0x0738,0xffcf,0xee46,0xdcdd,0xcd54,0xb9eb,0xa862,0x9af9,0x8b70,
		0x8408,0x9581,0xa71a,0xb693,0xc22c,0xd3a5,0xe13e,0xf0b7,0x0840,0x19c9,0x2b52,0x3adb,0x4e64,0x5fed,0x6d76,0x7cff,
		0x9489,0x8500,0xb79b,0xa612,0xd2ad,0xc324,0xf1bf,0xe036,0x18c1,0x0948,0x3bd3,0x2a5a,0x5ee5,0x4f6c,0x7df7,0x6c7e,
		0xa50a,0xb483,0x8618,0x9791,0xe32e,0xf2a7,0xc03c,0xd1b5,0x2942,0x38cb,0x0a50,0x1bd9,0x6f66,0x7eef,0x4c74,0x5dfd,
		0xb58b,0xa402,0x9699,0x8710,0xf3af,0xe226,0xd0bd,0xc134,0x39c3,0x284a,0x1ad1,0x0b58,0x7fe7,0x6e6e,0x5cf5,0x4d7c,
		0xc60c,0xd785,0xe51e,0xf497,0x8028,0x91a1,0xa33a,0xb2b3,0x4a44,0x5bcd,0x6956,0x78df,0x0c60,0x1de9,0x2f72,0x3efb,
		0xd68d,0xc704,0xf59f,0xe416,0x90a9,0x8120,0xb3bb,0xa232,0x5ac5,0x4b4c,0x79d7,0x685e,0x1ce1,0x0d68,0x3ff3,0x2e7a,
		0xe70e,0xf687,0xc41c,0xd595,0xa12a,0xb0a3,0x8238,0x93b1,0x6b46,0x7acf,0x4854,0x59dd,0x2d62,0x3ceb,0x0e70,0x1ff9,
		0xf78f,0xe606,0xd49d,0xc514,0xb1ab,0xa022,0x92b9,0x8330,0x7bc7,0x6a4e,0x58d5,0x495c,0x3de3,0x2c6a,0x1ef1,0x0f78
	};

	for (int i = 0; i < 39 ; i++) {
		short_c = 0x00ff & (unsigned short)packet[i];
		tmp = (crc_dstar_ffff & 0x00ff) ^ short_c;
		crc_dstar_ffff = (crc_dstar_ffff >> 8) ^ crc_tabccitt[tmp];
	}
	crc_dstar_ffff =  ~crc_dstar_ffff;
	tmp = crc_dstar_ffff;

	pfcs[0] = (unsigned char)(crc_dstar_ffff & 0xff);
	pfcs[1] = (unsigned char)((tmp >> 8) & 0xff);

	return;
}
