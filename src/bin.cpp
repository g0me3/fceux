/* FCE Ultra - NES/Famicom Emulator
 *
 * Copyright notice for this file:
 *  Copyright (C) 1998 BERO
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

#include "types.h"
#include "x6502.h"
#include "fceu.h"
#include "cart.h"
#include "ppu.h"

#include "bin.h"
#include "unif.h"
#include "state.h"
#include "file.h"
#include "utils/general.h"
#include "utils/memory.h"
#include "utils/crc32.h"
#include "utils/md5.h"
#include "utils/xstring.h"
#include "cheat.h"
#include "driver.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

static CartInfo BinROM;

extern uint8 *ROM;
extern uint8 *VROM;
extern uint32 VROM_size;
extern uint32 ROM_size;

extern uint8 Mirroring;
extern char LoadedRomFName[2048];

static int MapperNo = 0;

void BinROMGI(GI h) {
	switch (h) {
	case GI_RESETSAVE: {
		FCEU_ClearGameSave(&BinROM);
		break;
	}
	case GI_RESETM2: {
		if (BinROM.Reset)
			BinROM.Reset();
		break;
	}
	case GI_POWER: {
		if (BinROM.Power)
			BinROM.Power();
		break;
	}
	case GI_CLOSE: {
		FCEU_SaveGameSave(&BinROM);
		if (BinROM.Close)
			BinROM.Close();
		if (ROM) {
			free(ROM);
			ROM = NULL;
		}
		break;
	}
	}
}

uint32 BinROMGameCRC32 = 0;

int BinROMLoad(const char *name, FCEUFILE *fp) {
	struct md5_context md5;

	memset(&BinROM, 0, sizeof(BinROM));

	MapperNo = 256;
	Mirroring = 0;

	ROM_size = FCEU_fgetsize(fp) >> 14;
	VROM_size = 0;

	if ((ROM = (uint8*)FCEU_malloc(ROM_size << 14)) == NULL)
		return 0;

	ResetCartMapping();
	ResetExState(0, 0);

	SetupCartPRGMapping(0, ROM, ROM_size << 14, 0);

	FCEU_fseek(fp, 0, SEEK_SET);
	FCEU_fread(ROM, 0x4000, ROM_size, fp);

	md5_starts(&md5);
	md5_update(&md5, ROM, ROM_size << 14);

	BinROMGameCRC32 = CalcCRC32(0, ROM, ROM_size << 14);

	md5_finish(&md5, BinROM.MD5);
	memcpy(&GameInfo->MD5, &BinROM.MD5, sizeof(BinROM.MD5));

	BinROM.CRC32 = BinROMGameCRC32;

	FCEU_printf(" PRG ROM:  %3d 16Kib\n", ROM_size);
	FCEU_printf(" ROM CRC32:  0x%08lx\n", BinROMGameCRC32);
	{
		int x;
		FCEU_printf(" ROM MD5:  0x");
		for (x = 0; x < 16; x++)
			FCEU_printf("%02x", BinROM.MD5[x]);
		FCEU_printf("\n");
	}

	char* mappername = "Oregon Trail (tm) by Basic Fun (c) 2017";

	FCEU_printf(" Mapper #:  %d\n", MapperNo);
	FCEU_printf(" Mapper name: %s\n", mappername);

	SetupCartMirroring(Mirroring & 1, 1, 0);	// always hardwired

	BinROM.battery = 0;
	BinROM.mirror = Mirroring;

	UNLOneBus_Init(&BinROM);

	BinROM.vram_size = 0;
	UNIFchrrama = VROM;

	GameInfo->mappernum = MapperNo;
	FCEU_LoadGameSave(&BinROM);

	strcpy(LoadedRomFName, name); //bbit edited: line added

	// Extract Filename only. Should account for Windows/Unix this way.
	if (strrchr(name, '/')) {
		name = strrchr(name, '/') + 1;
	}
	else if (strrchr(name, '\\')) {
		name = strrchr(name, '\\') + 1;
	}

	GameInterface = BinROMGI;
	currCartInfo = &BinROM;
	FCEU_printf("\n");

	FCEUI_SetVidSystem(0);
	return 1;
}
