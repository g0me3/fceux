/* FCE Ultra - NES/Famicom Emulator
 *
 * Copyright notice for this file:
 *  Copyright (C) 1998 Bero
 *  Copyright (C) 2002 Xodnizel
 *  Copyright (C) 2020 CaH4e3
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#ifndef _BIN_H_
#define _BIN_H_
#include <stdlib.h>
#include <string.h>
#include <map>

struct TMasterRomInfo
{
	uint64 md5lower;
	const char* params;
};

class TMasterRomInfoParams : public std::map<std::string,std::string>
{
public:
	bool ContainsKey(const std::string& key) { return find(key) != end(); }
};

extern const TMasterRomInfo* MasterRomInfo;
extern TMasterRomInfoParams MasterRomInfoParams;

#endif
