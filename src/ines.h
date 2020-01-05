/* FCE Ultra - NES/Famicom Emulator
 *
 * Copyright notice for this file:
 *  Copyright (C) 1998 Bero
 *  Copyright (C) 2002 Xodnizel
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

#ifndef _INES_H_
#define _INES_H_
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

//mbg merge 6/29/06
extern uint8 *ROM;
extern uint8 *VROM;
extern uint32 VROM_size;
extern uint32 ROM_size;
extern uint8 *ExtraNTARAM;
extern int iNesSave(); //bbit Edited: line added
extern int iNesSaveAs(char* name);
extern char LoadedRomFName[2048]; //bbit Edited: line added
extern const TMasterRomInfo* MasterRomInfo;
extern TMasterRomInfoParams MasterRomInfoParams;

//mbg merge 7/19/06 changed to c++ decl format
struct iNES_HEADER {
	char ID[4]; /*NES^Z*/        // 0-3
	uint8 ROM_size;              // 4
	uint8 VROM_size;             // 5
	uint8 ROM_type;              // 6
	uint8 ROM_type2;             // 7
	uint8 ROM_type3;             // 8
	uint8 Upper_ROM_VROM_size;   // 9
	uint8 RAM_size;              // 10
	uint8 VRAM_size;             // 11
	uint8 TV_system;             // 12
	uint8 VS_hardware;           // 13
	uint8 reserved[2];           // 14, 15

	void cleanup()
	{
		if(!memcmp((char*)(this) + 0x7, "DiskDude", 8) || !memcmp((char*)(this) + 0x7, "demiforce", 9))
			memset((char*)(this) + 0x7, 0, 0x9);

		if(!memcmp((char*)(this) + 0xA, "Ni03", 4))
		{
			if(!memcmp((char*)(this) + 0x7, "Dis", 3))
				memset((char*)(this) + 0x7, 0, 0x9);
			else
				memset((char*)(this) + 0xA, 0, 0x6);
		}
	}
};

extern struct iNES_HEADER head; //for mappers usage

typedef struct {
	char *name;
	int32 number;
	void (*init)(CartInfo *);
} BMAPPINGLocal;
#endif
