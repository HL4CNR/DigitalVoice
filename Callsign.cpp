
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

#include <iostream>
#include <cstring>

#include "Callsign.h"

#define M17CHARACTERS " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-/."

CCallsign::CCallsign()
{
	memset(cs, 0, sizeof(cs));
	memset(code, 0, sizeof(code));
}

CCallsign::CCallsign(const std::string &callsign)
{
	SetCS(callsign);
}

CCallsign::CCallsign(const uint8_t *in)
{
	SetCode(in);
}

void CCallsign::SetCS(const std::string &callsign)
{
	const std::string m17_alphabet(M17CHARACTERS);
	memset(cs, 0, 10);
	memcpy(cs, callsign.c_str(), (callsign.size()<10) ? callsign.size() : 9);
	uint64_t encoded = 0;
	for( int i=0; cs[i]; i++ ) {
		auto pos = m17_alphabet.find(cs[i]);
		if (pos == std::string::npos) {
			pos = 0;
		}
		encoded *= 40;
		encoded += pos;
	}
	for (int i=0; i<6; i++) {
		code[i] = (encoded >> (8*(5-i)) & 0xFFU);
	}
}

void CCallsign::SetCode(const uint8_t *in)
{
	const std::string m17_alphabet(M17CHARACTERS);
	memset(cs, 0, 10);
	uint64_t coded = in[0];
	for (int i=1; i<6; i++)
		coded = (coded << 8) | in[i];
	if (coded > 0xee6b27ffffffu) {
		std::cerr << "Callsign code is too large, 0x" << std::hex << coded << std::endl;
		return;
	}
	memcpy(code, in, 6);
	int i = 0;
	while (coded) {
		cs[i++] = m17_alphabet[coded % 40];
		coded /= 40;
	}
}

const std::string CCallsign::GetCS(unsigned len) const
{
	if (len > 9)
		len = 9;
	std::string rval(cs);
	rval.resize(len, ' ');
	return rval;
}

char CCallsign::GetModule() const
{
	if (cs[9])
		return cs[9];
	else
		return ' ';
}

bool CCallsign::operator==(const CCallsign &rhs) const
{
	return (0 == memcmp(code, rhs.code, 6));
}
