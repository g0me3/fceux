/* FCE Ultra - NES/Famicom Emulator
 *
 * Copyright notice for this file:
 *  Copyright (C) 1998 BERO
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

#include "types.h"
#include "x6502.h"
#include "fceu.h"
#include "cart.h"
#include "ppu.h"

#include "ines.h"
#include "unif.h"
#include "state.h"
#include "file.h"
#include "utils/general.h"
#include "utils/memory.h"
#include "utils/crc32.h"
#include "utils/md5.h"
#include "utils/xstring.h"
#include "cheat.h"
#include "vsuni.h"
#include "driver.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

extern SFORMAT FCEUVSUNI_STATEINFO[];

//mbg merge 6/29/06 - these need to be global
uint8 *trainerpoo = NULL;
uint8 *ROM = NULL;
uint8 *VROM = NULL;
uint8 *ExtraNTARAM = NULL;
iNES_HEADER head;

static CartInfo iNESCart;

uint8 Mirroring = 0;
uint32 ROM_size = 0;
uint32 VROM_size = 0;
char LoadedRomFName[2048]; //mbg merge 7/17/06 added

static int CHRRAMSize = -1;
static int iNES_Init(int num);

static int MapperNo = 0;

int iNES2 = 0;

static DECLFR(TrainerRead) {
	return(trainerpoo[A & 0x1FF]);
}

static void iNES_ExecPower() {
	if (CHRRAMSize != -1)
		FCEU_MemoryRand(VROM, CHRRAMSize);

	if (iNESCart.Power)
		iNESCart.Power();

	if (trainerpoo) {
		int x;
		for (x = 0; x < 512; x++) {
			X6502_DMW(0x7000 + x, trainerpoo[x]);
			if (X6502_DMR(0x7000 + x) != trainerpoo[x]) {
				SetReadHandler(0x7000, 0x71FF, TrainerRead);
				break;
			}
		}
	}
}

void iNESGI(GI h) { //bbit edited: removed static keyword
	switch (h) {
	case GI_RESETSAVE:
		FCEU_ClearGameSave(&iNESCart);
		break;

	case GI_RESETM2:
		if (iNESCart.Reset)
			iNESCart.Reset();
		break;
	case GI_POWER:
		iNES_ExecPower();
		break;
	case GI_CLOSE:
	{
		FCEU_SaveGameSave(&iNESCart);
		if (iNESCart.Close)
			iNESCart.Close();
		if (ROM) {
			free(ROM);
			ROM = NULL;
		}
		if (VROM) {
			free(VROM);
			VROM = NULL;
		}
		if (trainerpoo) {
			free(trainerpoo);
			trainerpoo = NULL;
		}
		if (ExtraNTARAM) {
			free(ExtraNTARAM);
			ExtraNTARAM = NULL;
		}
	}
	break;
	}
}

uint32 iNESGameCRC32 = 0;

struct CRCMATCH {
	uint32 crc;
	char *name;
};

struct INPSEL {
	uint32 crc32;
	ESI input1;
	ESI input2;
	ESIFC inputfc;
};

static void SetInput(void) {
	static struct INPSEL moo[] =
	{
		{0x19b0a9f1,	SI_GAMEPAD,		SI_ZAPPER,		SIFC_NONE		},	// 6-in-1 (MGC-023)(Unl)[!]
		{0x29de87af,	SI_GAMEPAD,		SI_GAMEPAD,		SIFC_FTRAINERB	},	// Aerobics Studio
		{0xd89e5a67,	SI_UNSET,		SI_UNSET,		SIFC_ARKANOID	},	// Arkanoid (J)
		{0x0f141525,	SI_UNSET,		SI_UNSET,		SIFC_ARKANOID	},	// Arkanoid 2(J)
		{0x32fb0583,	SI_UNSET,		SI_ARKANOID,	SIFC_NONE		},	// Arkanoid(NES)
		{0x60ad090a,	SI_GAMEPAD,		SI_GAMEPAD,		SIFC_FTRAINERA	},	// Athletic World
		{0x48ca0ee1,	SI_GAMEPAD,		SI_GAMEPAD,		SIFC_BWORLD		},	// Barcode World
		{0x4318a2f8,	SI_UNSET,		SI_ZAPPER,		SIFC_NONE		},	// Barker Bill's Trick Shooting
		{0x6cca1c1f,	SI_GAMEPAD,		SI_GAMEPAD,		SIFC_FTRAINERB	},	// Dai Undoukai
		{0x24598791,	SI_UNSET,		SI_ZAPPER,		SIFC_NONE		},	// Duck Hunt
		{0xd5d6eac4,	SI_UNSET,		SI_UNSET,		SIFC_SUBORKB	},	// Edu (As)
		{0xe9a7fe9e,	SI_UNSET,		SI_MOUSE,		SIFC_NONE		},	// Educational Computer 2000
		{0x8f7b1669,	SI_UNSET,		SI_UNSET,		SIFC_SUBORKB	},	// FP BASIC 3.3 by maxzhou88
		{0xf7606810,	SI_UNSET,		SI_UNSET,		SIFC_FKB		},	// Family BASIC 2.0A
		{0x895037bc,	SI_UNSET,		SI_UNSET,		SIFC_FKB		},	// Family BASIC 2.1a
		{0xb2530afc,	SI_UNSET,		SI_UNSET,		SIFC_FKB		},	// Family BASIC 3.0
		{0xea90f3e2,	SI_GAMEPAD,		SI_GAMEPAD,		SIFC_FTRAINERB	},	// Family Trainer:  Running Stadium
		{0xbba58be5,	SI_GAMEPAD,		SI_GAMEPAD,		SIFC_FTRAINERB	},	// Family Trainer: Manhattan Police
		{0x3e58a87e,	SI_UNSET,		SI_ZAPPER,		SIFC_NONE		},	// Freedom Force
		{0xd9f45be9,	SI_GAMEPAD,		SI_GAMEPAD,		SIFC_QUIZKING	},	// Gimme a Break ...
		{0x1545bd13,	SI_GAMEPAD,		SI_GAMEPAD,		SIFC_QUIZKING	},	// Gimme a Break ... 2
		{0x4e959173,	SI_UNSET,		SI_ZAPPER,		SIFC_NONE		},	// Gotcha! - The Sport!
		{0xbeb8ab01,	SI_UNSET,		SI_ZAPPER,		SIFC_NONE		},	// Gumshoe
		{0xff24d794,	SI_UNSET,		SI_ZAPPER,		SIFC_NONE		},	// Hogan's Alley
		{0x21f85681,	SI_GAMEPAD,		SI_GAMEPAD,		SIFC_HYPERSHOT	},	// Hyper Olympic (Gentei Ban)
		{0x980be936,	SI_GAMEPAD,		SI_GAMEPAD,		SIFC_HYPERSHOT	},	// Hyper Olympic
		{0x915a53a7,	SI_GAMEPAD,		SI_GAMEPAD,		SIFC_HYPERSHOT	},	// Hyper Sports
		{0x9fae4d46,	SI_GAMEPAD,		SI_GAMEPAD,		SIFC_MAHJONG	},	// Ide Yousuke Meijin no Jissen Mahjong
		{0x7b44fb2a,	SI_GAMEPAD,		SI_GAMEPAD,		SIFC_MAHJONG	},	// Ide Yousuke Meijin no Jissen Mahjong 2
		{0x2f128512,	SI_GAMEPAD,		SI_GAMEPAD,		SIFC_FTRAINERA	},	// Jogging Race
		{0xbb33196f,	SI_UNSET,		SI_UNSET,		SIFC_FKB		},	// Keyboard Transformer
		{0x8587ee00,	SI_UNSET,		SI_UNSET,		SIFC_FKB		},	// Keyboard Transformer
		{0x543ab532,	SI_UNSET,		SI_UNSET,		SIFC_SUBORKB	},	// LIKO Color Lines
		{0x368c19a8,	SI_UNSET,		SI_UNSET,		SIFC_SUBORKB	},	// LIKO Study Cartridge
		{0x5ee6008e,	SI_UNSET,		SI_ZAPPER,		SIFC_NONE		},	// Mechanized Attack
		{0x370ceb65,	SI_GAMEPAD,		SI_GAMEPAD,		SIFC_FTRAINERB	},	// Meiro Dai Sakusen
		{0x3a1694f9,	SI_GAMEPAD,		SI_GAMEPAD,		SIFC_4PLAYER	},	// Nekketsu Kakutou Densetsu
		{0x9d048ea4,	SI_GAMEPAD,		SI_GAMEPAD,		SIFC_OEKAKIDS	},	// Oeka Kids
		{0x2a6559a1,	SI_UNSET,		SI_ZAPPER,		SIFC_NONE		},	// Operation Wolf (J)
		{0xedc3662b,	SI_UNSET,		SI_ZAPPER,		SIFC_NONE		},	// Operation Wolf
		{0x912989dc,	SI_UNSET,		SI_UNSET,		SIFC_FKB		},	// Playbox BASIC
		{0x9044550e,	SI_GAMEPAD,		SI_GAMEPAD,		SIFC_FTRAINERA	},	// Rairai Kyonshizu
		{0xea90f3e2,	SI_GAMEPAD,		SI_GAMEPAD,		SIFC_FTRAINERB	},	// Running Stadium
		{0x851eb9be,	SI_GAMEPAD,		SI_ZAPPER,		SIFC_NONE		},	// Shooting Range
		{0x6435c095,	SI_GAMEPAD,		SI_POWERPADB,	SIFC_UNSET		},	// Short Order/Eggsplode
		{0xc043a8df,	SI_UNSET,		SI_MOUSE,		SIFC_NONE		},	// Shu Qi Yu - Shu Xue Xiao Zhuan Yuan (Ch)
		{0x2cf5db05,	SI_UNSET,		SI_MOUSE,		SIFC_NONE		},	// Shu Qi Yu - Zhi Li Xiao Zhuan Yuan (Ch)
		{0xad9c63e2,	SI_GAMEPAD,		SI_UNSET,		SIFC_SHADOW		},	// Space Shadow
		{0x61d86167,	SI_GAMEPAD,		SI_POWERPADB,	SIFC_UNSET		},	// Street Cop
		{0xabb2f974,	SI_UNSET,		SI_UNSET,		SIFC_SUBORKB	},	// Study and Game 32-in-1
		{0x41ef9ac4,	SI_UNSET,		SI_UNSET,		SIFC_SUBORKB	},	// Subor
		{0x8b265862,	SI_UNSET,		SI_UNSET,		SIFC_SUBORKB	},	// Subor
		{0x82f1fb96,	SI_UNSET,		SI_UNSET,		SIFC_SUBORKB	},	// Subor 1.0 Russian
		{0x9f8f200a,	SI_GAMEPAD,		SI_GAMEPAD,		SIFC_FTRAINERA	},	// Super Mogura Tataki!! - Pokkun Moguraa
		{0xd74b2719,	SI_GAMEPAD,		SI_POWERPADB,	SIFC_UNSET		},	// Super Team Games
		{0x74bea652,	SI_GAMEPAD,		SI_ZAPPER,		SIFC_NONE		},	// Supergun 3-in-1
		{0x5e073a1b,	SI_UNSET,		SI_UNSET,		SIFC_SUBORKB	},	// Supor English (Chinese)
		{0x589b6b0d,	SI_UNSET,		SI_UNSET,		SIFC_SUBORKB	},	// SuporV20
		{0x41401c6d,	SI_UNSET,		SI_UNSET,		SIFC_SUBORKB	},	// SuporV40
		{0x23d17f5e,	SI_GAMEPAD,		SI_ZAPPER,		SIFC_NONE		},	// The Lone Ranger
		{0xc3c0811d,	SI_GAMEPAD,		SI_GAMEPAD,		SIFC_OEKAKIDS	},	// The two "Oeka Kids" games
		{0xde8fd935,	SI_UNSET,		SI_ZAPPER,		SIFC_NONE		},	// To the Earth
		{0x47232739,	SI_GAMEPAD,		SI_GAMEPAD,		SIFC_TOPRIDER	},	// Top Rider
		{0x8a12a7d9,	SI_GAMEPAD,		SI_GAMEPAD,		SIFC_FTRAINERB	},	// Totsugeki Fuuun Takeshi Jou
		{0xb8b9aca3,	SI_UNSET,		SI_ZAPPER,		SIFC_NONE		},	// Wild Gunman
		{0x5112dc21,	SI_UNSET,		SI_ZAPPER,		SIFC_NONE		},	// Wild Gunman
		{0xaf4010ea,	SI_GAMEPAD,		SI_POWERPADB,	SIFC_UNSET		},	// World Class Track Meet
		{0x67b126b9,	SI_GAMEPAD,		SI_GAMEPAD,		SIFC_FAMINETSYS },	// Famicom Network System
		{0x00000000,	SI_UNSET,		SI_UNSET,		SIFC_UNSET		}
	};
	int x = 0;

	while (moo[x].input1 >= 0 || moo[x].input2 >= 0 || moo[x].inputfc >= 0) {
		if (moo[x].crc32 == iNESGameCRC32) {
			GameInfo->input[0] = moo[x].input1;
			GameInfo->input[1] = moo[x].input2;
			GameInfo->inputfc = moo[x].inputfc;
			break;
		}
		x++;
	}
}

#define INESB_INCOMPLETE  1
#define INESB_CORRUPT     2
#define INESB_HACKED      4

struct BADINF {
	uint64 md5partial;
	char *name;
	uint32 type;
};

static struct BADINF BadROMImages[] =
{
	#include "ines-bad.h"
};

void CheckBad(uint64 md5partial) {
	int32 x = 0;
	while (BadROMImages[x].name) {
		if (BadROMImages[x].md5partial == md5partial) {
			FCEU_PrintError("The copy game you have loaded, \"%s\", is bad, and will not work properly in FCEUX.", BadROMImages[x].name);
			return;
		}
		x++;
	}
}


struct CHINF {
	uint32 crc32;
	int32 mapper;
	int32 mirror;
	const char* params;
};

static const TMasterRomInfo sMasterRomInfo[] = {
	{ 0x62b51b108a01d2beULL, "bonus=0" }, //4-in-1 (FK23C8021)[p1][!].nes
	{ 0x8bb48490d8d22711ULL, "bonus=0" }, //4-in-1 (FK23C8033)[p1][!].nes
	{ 0xc75888d7b48cd378ULL, "bonus=0" }, //4-in-1 (FK23C8043)[p1][!].nes
	{ 0xf81a376fa54fdd69ULL, "bonus=0" }, //4-in-1 (FK23Cxxxx, S-0210A PCB)[p1][!].nes
	{ 0xa37eb9163e001a46ULL, "bonus=0" }, //4-in-1 (FK23C8026) [p1][!].nes
	{ 0xde5ce25860233f7eULL, "bonus=0" }, //4-in-1 (FK23C8045) [p1][!].nes
	{ 0x5b3aa4cdc484a088ULL, "bonus=0" }, //4-in-1 (FK23C8056) [p1][!].nes
	{ 0x9342bf9bae1c798aULL, "bonus=0" }, //4-in-1 (FK23C8079) [p1][!].nes
	{ 0x164eea6097a1e313ULL, "busc=1" }, //Cybernoid - The Fighting Machine (U)[!].nes -- needs bus conflict emulation
};
const TMasterRomInfo* MasterRomInfo;
TMasterRomInfoParams MasterRomInfoParams;

static void CheckHInfo(void) {
	/* ROM images that have the battery-backed bit set in the header that really
	don't have battery-backed RAM is not that big of a problem, so I'll
	treat this differently by only listing games that should have battery-backed RAM.

	Lower 64 bits of the MD5 hash.
	*/

	static uint64 savie[] =
	{
		0xc04361e499748382ULL,	/* AD&D Heroes of the Lance */
		0xb72ee2337ced5792ULL,	/* AD&D Hillsfar */
		0x2b7103b7a27bd72fULL,	/* AD&D Pool of Radiance */
		0x498c10dc463cfe95ULL,	/* Battle Fleet */
		0x854d7947a3177f57ULL,	/* Crystalis */
		0x4a1f5336b86851b6ULL,	/* DW */
		0xb0bcc02c843c1b79ULL,	/* DW */
		0x2dcf3a98c7937c22ULL,	/* DW 2 */
		0x98e55e09dfcc7533ULL,	/* DW 4*/
		0x733026b6b72f2470ULL,	/* Dw 3 */
		0x6917ffcaca2d8466ULL,	/* Famista '90 */
		0x8da46db592a1fcf4ULL,	/* Faria */
		0xedba17a2c4608d20ULL,	/* Final Fantasy */
		0x91a6846d3202e3d6ULL,	/* Final Fantasy */
		0x012df596e2b31174ULL,	/* Final Fantasy 1+2 */
		0xf6b359a720549ecdULL,	/* Final Fantasy 2 */
		0x5a30da1d9b4af35dULL,	/* Final Fantasy 3 */
		0xd63dcc68c2b20adcULL,	/* Final Fantasy J */
		0x2ee3417ba8b69706ULL,	/* Hydlide 3*/
		0xebbce5a54cf3ecc0ULL,	/* Justbreed */
		0x6a858da551ba239eULL,	/* Kaijuu Monogatari */
		0x2db8f5d16c10b925ULL,	/* Kyonshiizu 2 */
		0x04a31647de80fdabULL,	/* Legend of Zelda */
		0x94b9484862a26cbaULL,	/* Legend of Zelda */
		0xa40666740b7d22feULL,	/* Mindseeker */
		0x82000965f04a71bbULL,	/* Mirai Shinwa Jarvas */
		0x77b811b2760104b9ULL,	/* Mouryou Senki Madara */
		0x11b69122efe86e8cULL,	/* RPG Jinsei Game */
		0x9aa1dc16c05e7de5ULL,	/* Startropics */
		0x1b084107d0878bd0ULL,	/* Startropics 2*/
		0xa70b495314f4d075ULL,	/* Ys 3 */
		0x836c0ff4f3e06e45ULL,	/* Zelda 2 */
		0						/* Abandon all hope if the game has 0 in the lower 64-bits of its MD5 hash */
	};

	static struct CHINF moo[] =
	{
		#include "ines-correct.h"
	};
	int32 tofix = 0, x, mask;
	uint64 partialmd5 = 0;

	for (x = 0; x < 8; x++)
		partialmd5 |= (uint64)iNESCart.MD5[15 - x] << (x * 8);
	CheckBad(partialmd5);

	MasterRomInfo = NULL;
	for (int i = 0; i < ARRAY_SIZE(sMasterRomInfo); i++) {
		const TMasterRomInfo& info = sMasterRomInfo[i];
		if (info.md5lower != partialmd5)
			continue;

		MasterRomInfo = &info;
		if (!info.params) break;

		std::vector<std::string> toks = tokenize_str(info.params, ",");
		for (int j = 0; j < (int)toks.size(); j++) {
			std::vector<std::string> parts = tokenize_str(toks[j], "=");
			MasterRomInfoParams[parts[0]] = parts[1];
		}
		break;
	}

	x = 0;
	do {
		if (moo[x].crc32 == iNESGameCRC32) {
			if (moo[x].mapper >= 0) {
				if (moo[x].mapper & 0x800 && VROM_size) {
					VROM_size = 0;
					free(VROM);
					VROM = NULL;
					tofix |= 8;
				}
				if (moo[x].mapper & 0x1000)
					mask = 0xFFF;
				else
					mask = 0xFF;
				if (MapperNo != (moo[x].mapper & mask)) {
					tofix |= 1;
					MapperNo = moo[x].mapper & mask;
				}
			}
			if (moo[x].mirror >= 0) {
				if (moo[x].mirror == 8) {
					if (Mirroring == 2) {	/* Anything but hard-wired(four screen). */
						tofix |= 2;
						Mirroring = 0;
					}
				} else if (Mirroring != moo[x].mirror) {
					if (Mirroring != (moo[x].mirror & ~4))
						if ((moo[x].mirror & ~4) <= 2)	/* Don't complain if one-screen mirroring
														needs to be set(the iNES header can't
														hold this information).
														*/
							tofix |= 2;
					Mirroring = moo[x].mirror;
				}
			}
			break;
		}
		x++;
	} while (moo[x].mirror >= 0 || moo[x].mapper >= 0);

	x = 0;
	while (savie[x] != 0) {
		if (savie[x] == partialmd5) {
			if (!(head.ROM_type & 2)) {
				tofix |= 4;
				head.ROM_type |= 2;
			}
		}
		x++;
	}

	/* Games that use these iNES mappers tend to have the four-screen bit set
	when it should not be.
	*/
	if ((MapperNo == 118 || MapperNo == 24 || MapperNo == 26) && (Mirroring == 2)) {
		Mirroring = 0;
		tofix |= 2;
	}

	/* Four-screen mirroring implicitly set. */
	if (MapperNo == 99)
		Mirroring = 2;

	if (tofix) {
		char gigastr[768];
		strcpy(gigastr, "The iNES header contains incorrect information.  For now, the information will be corrected in RAM.  ");
		if (tofix & 1)
			sprintf(gigastr + strlen(gigastr), "The mapper number should be set to %d.  ", MapperNo);
		if (tofix & 2) {
			char *mstr[3] = { "Horizontal", "Vertical", "Four-screen" };
			sprintf(gigastr + strlen(gigastr), "Mirroring should be set to \"%s\".  ", mstr[Mirroring & 3]);
		}
		if (tofix & 4)
			strcat(gigastr, "The battery-backed bit should be set.  ");
		if (tofix & 8)
			strcat(gigastr, "This game should not have any CHR ROM.  ");
		strcat(gigastr, "\n");
		FCEU_printf("%s", gigastr);
	}
}

typedef struct {
	int32 mapper;
	void (*init)(CartInfo *);
} NewMI;

//this is for games that is not the a power of 2
//mapper based for now...
//not really accurate but this works since games
//that are not in the power of 2 tends to come
//in obscure mappers themselves which supports such
//size
//Cah4e3 25.10.19: iNES 2.0 attempts to cover all
// boards including UNIF boards with non power 2 
// total rom size (a lot of them with a couple of 
// roms different sizes (may vary a lot)
// so we need either add here ALL ines 2.0 mappers 
// with not power2 roms or change logic here
// to something more unified for ines 2.0 specific
static int not_power2[] =
{
	53, 198, 228, 547
};

BMAPPINGLocal bmap[] = {
	{"ONE-BUS Systems",		256, UNLOneBus_Init},
	{"",					0, NULL}
};

int iNESLoad(const char *name, FCEUFILE *fp, int OverwriteVidMode) {
	struct md5_context md5;

	if (FCEU_fread(&head, 1, 16, fp) != 16 || memcmp(&head, "NES\x1A", 4))
		return 0;
	
	head.cleanup();

	memset(&iNESCart, 0, sizeof(iNESCart));

	iNES2 = ((head.ROM_type2 & 0x0C) == 0x08);
	if(iNES2)
	{
		iNESCart.ines2 = true;
		iNESCart.wram_size = (head.RAM_size & 0x0F)?(64 << (head.RAM_size & 0x0F)):0;
		iNESCart.battery_wram_size = (head.RAM_size & 0xF0)?(64 << ((head.RAM_size & 0xF0)>>4)):0;
		iNESCart.vram_size = (head.VRAM_size & 0x0F)?(64 << (head.VRAM_size & 0x0F)):0;
		iNESCart.battery_vram_size = (head.VRAM_size & 0xF0)?(64 << ((head.VRAM_size & 0xF0)>>4)):0;
		iNESCart.submapper = head.ROM_type3 >> 4;
	}

	MapperNo = (head.ROM_type >> 4);
	MapperNo |= (head.ROM_type2 & 0xF0);
	if(iNES2) MapperNo |= ((head.ROM_type3 & 0x0F) << 8);
	
	if (head.ROM_type & 8) {
		Mirroring = 2;
	} else
		Mirroring = (head.ROM_type & 1);

	int not_round_size = head.ROM_size;
	if(iNES2) not_round_size |= ((head.Upper_ROM_VROM_size & 0x0F) << 8);
	
	if (!head.ROM_size && !iNES2)
		ROM_size = 256;
	else
		ROM_size = uppow2(not_round_size);

	VROM_size = uppow2(head.VROM_size | (iNES2?((head.Upper_ROM_VROM_size & 0xF0)<<4):0));

	int round = true;
	for (int i = 0; i != sizeof(not_power2) / sizeof(not_power2[0]); ++i) {
		//for games not to the power of 2, so we just read enough
		//prg rom from it, but we have to keep ROM_size to the power of 2
		//since PRGCartMapping wants ROM_size to be to the power of 2
		//so instead if not to power of 2, we just use head.ROM_size when
		//we use FCEU_read
		if (not_power2[i] == MapperNo) {
			round = false;
			break;
		}
	}

	if ((ROM = (uint8*)FCEU_malloc(ROM_size << 14)) == NULL)
		return 0;
	memset(ROM, 0xFF, ROM_size << 14);

	if (VROM_size) {
		if ((VROM = (uint8*)FCEU_malloc(VROM_size << 13)) == NULL) {
			free(ROM);
			ROM = NULL;
			return 0;
		}
		memset(VROM, 0xFF, VROM_size << 13);
	}

	if (head.ROM_type & 4) {	/* Trainer */
		trainerpoo = (uint8*)FCEU_gmalloc(512);
		FCEU_fread(trainerpoo, 512, 1, fp);
	}

	ResetCartMapping();
	ResetExState(0, 0);

	SetupCartPRGMapping(0, ROM, ROM_size << 14, 0);

	FCEU_fread(ROM, 0x4000, (round) ? ROM_size : not_round_size, fp);

	if (VROM_size)
		FCEU_fread(VROM, 0x2000, VROM_size, fp);

	md5_starts(&md5);
	md5_update(&md5, ROM, ROM_size << 14);

	iNESGameCRC32 = CalcCRC32(0, ROM, ROM_size << 14);

	if (VROM_size) {
		iNESGameCRC32 = CalcCRC32(iNESGameCRC32, VROM, VROM_size << 13);
		md5_update(&md5, VROM, VROM_size << 13);
	}
	md5_finish(&md5, iNESCart.MD5);
	memcpy(&GameInfo->MD5, &iNESCart.MD5, sizeof(iNESCart.MD5));

	iNESCart.CRC32 = iNESGameCRC32;

	FCEU_printf(" PRG ROM:  %3d x 16KiB\n", (round) ? ROM_size: not_round_size);
	FCEU_printf(" CHR ROM:  %3d x  8KiB\n", head.VROM_size);
	FCEU_printf(" ROM CRC32:  0x%08lx\n", iNESGameCRC32);
	{
		int x;
		FCEU_printf(" ROM MD5:  0x");
		for(x=0;x<16;x++)
			FCEU_printf("%02x",iNESCart.MD5[x]);
		FCEU_printf("\n");
	}

	char* mappername = "Not Listed";

	for (int mappertest = 0; mappertest < (sizeof bmap / sizeof bmap[0]) - 1; mappertest++) {
		if (bmap[mappertest].number == MapperNo) {
			mappername = bmap[mappertest].name;
			break;
		}
	}

	FCEU_printf(" Mapper #:  %d\n", MapperNo);
	FCEU_printf(" Mapper name: %s\n", mappername);
	FCEU_printf(" Mirroring: %s\n", Mirroring == 2 ? "None (Four-screen)" : Mirroring ? "Vertical" : "Horizontal");
	FCEU_printf(" Battery-backed: %s\n", (head.ROM_type & 2) ? "Yes" : "No");
	FCEU_printf(" Trained: %s\n", (head.ROM_type & 4) ? "Yes" : "No");
	if(iNES2) 
	{
		FCEU_printf(" NES2.0 Extensions\n");
		FCEU_printf(" Sub Mapper #: %d\n", iNESCart.submapper);
		FCEU_printf(" Total WRAM size: %d\n", iNESCart.wram_size + iNESCart.battery_wram_size);
		FCEU_printf(" Total VRAM size: %d\n", iNESCart.vram_size + iNESCart.battery_vram_size);
		if(head.ROM_type & 2)
		{
			FCEU_printf(" WRAM backed by battery: %d\n", iNESCart.battery_wram_size);
			FCEU_printf(" VRAM backed by battery: %d\n", iNESCart.battery_vram_size);
		}
	}

	SetInput();
	CheckHInfo();
	{
		int x;
		uint64 partialmd5 = 0;

		for (x = 0; x < 8; x++) {
			partialmd5 |= (uint64)iNESCart.MD5[7 - x] << (x * 8);
		}

		FCEU_VSUniCheck(partialmd5, &MapperNo, &Mirroring);
	}
	/* Must remain here because above functions might change value of
	VROM_size and free(VROM).
	*/
	if (VROM_size)
		SetupCartCHRMapping(0, VROM, VROM_size * 0x2000, 0);

	if (Mirroring == 2) {
		ExtraNTARAM = (uint8*)FCEU_gmalloc(2048);
		SetupCartMirroring(4, 1, ExtraNTARAM);
	} else if (Mirroring >= 0x10)
		SetupCartMirroring(2 + (Mirroring & 1), 1, 0);
	else
		SetupCartMirroring(Mirroring & 1, (Mirroring & 4) >> 2, 0);

	iNESCart.battery = (head.ROM_type & 2) ? 1 : 0;
	iNESCart.mirror = Mirroring;

	if (!iNES_Init(MapperNo))
		FCEU_PrintError("iNES mapper #%d is not supported at all.", MapperNo);

	GameInfo->mappernum = MapperNo;
	FCEU_LoadGameSave(&iNESCart);

	strcpy(LoadedRomFName, name); //bbit edited: line added

	// Extract Filename only. Should account for Windows/Unix this way.
	if (strrchr(name, '/')) {
		name = strrchr(name, '/') + 1;
	} else if (strrchr(name, '\\')) {
		name = strrchr(name, '\\') + 1;
	}

	GameInterface = iNESGI;
	currCartInfo = &iNESCart;
	FCEU_printf("\n");

	// since apparently the iNES format doesn't store this information,
	// guess if the settings should be PAL or NTSC from the ROM name
	// TODO: MD5 check against a list of all known PAL games instead?
	if (iNES2) {
		FCEUI_SetVidSystem(((head.TV_system & 3) == 1) ? 1 : 0);
	} else if (OverwriteVidMode) {
		if (strstr(name, "(E)") || strstr(name, "(e)")
			|| strstr(name, "(Europe)") || strstr(name, "(PAL)")
			|| strstr(name, "(F)") || strstr(name, "(f)")
			|| strstr(name, "(G)") || strstr(name, "(g)")
			|| strstr(name, "(I)") || strstr(name, "(i)"))
			FCEUI_SetVidSystem(1);
		else
			FCEUI_SetVidSystem(0);
	}
	return 1;
}

// bbit edited: the whole function below was added
int iNesSave() {
	char name[2048];

	strcpy(name, LoadedRomFName);
	if (strcmp(name + strlen(name) - 4, ".nes") != 0) { //para edit
		strcat(name, ".nes");
	}

	return iNesSaveAs(name);
}

int iNesSaveAs(char* name)
{
	//adelikat: TODO: iNesSave() and this have pretty much the same code, outsource the common code to a single function
	//caitsith2: done. iNesSave() now gets filename and calls iNesSaveAs with that filename.
	FILE *fp;

	if (GameInfo->type != GIT_CART) return 0;
	if (GameInterface != iNESGI) return 0;

	fp = fopen(name, "wb");
	if (!fp)
		return 0;

	if (fwrite(&head, 1, 16, fp) != 16)
	{
		fclose(fp);
		return 0;
	}

	if (head.ROM_type & 4)
	{
		/* Trainer */
		fwrite(trainerpoo, 512, 1, fp);
	}

	fwrite(ROM, 0x4000, ROM_size, fp);

	if (head.VROM_size)
		fwrite(VROM, 0x2000, head.VROM_size, fp);

	fclose(fp);
	return 1;
}

//para edit: added function below
char *iNesShortFName() {
	char *ret;

	if (!(ret = strrchr(LoadedRomFName, '\\')))
	{
		if (!(ret = strrchr(LoadedRomFName, '/')))
			return 0;
	}
	return ret + 1;
}

static int iNES_Init(int num) {
	BMAPPINGLocal *tmp = bmap;

	CHRRAMSize = -1;

	if (GameInfo->type == GIT_VSUNI)
		AddExState(FCEUVSUNI_STATEINFO, ~0, 0, 0);

	while (tmp->init) {
		if (num == tmp->number) {
			UNIFchrrama = 0;	// need here for compatibility with UNIF mapper code
			if (!VROM_size) {
				if(!iNESCart.ines2)
				{
					switch (num) {	// FIXME, mapper or game data base with the board parameters and ROM/RAM sizes
					case 13:  CHRRAMSize = 16 * 1024; break;
					case 6:
					case 29:
					case 30:
					case 45:
					case 96:  CHRRAMSize = 32 * 1024; break;
					case 176: CHRRAMSize = 128 * 1024; break;
					default:  CHRRAMSize = 8 * 1024; break;
					}
					iNESCart.vram_size = CHRRAMSize;
				}
				else
				{
					CHRRAMSize = iNESCart.battery_vram_size + iNESCart.vram_size;
				}
				if ((VROM = (uint8*)FCEU_dmalloc(CHRRAMSize)) == NULL) return 0;
				FCEU_MemoryRand(VROM, CHRRAMSize);

				UNIFchrrama = VROM;
				if(CHRRAMSize == 0)
				{
					//probably a mistake. 
					//but (for chrram): "Use of $00 with no CHR ROM implies that the game is wired to map nametable memory in CHR space. The value $00 MUST NOT be used if a mapper isn't defined to allow this. "
					//well, i'm not going to do that now. we'll save it for when it's needed
					//"it's only mapper 218 and no other mappers"
				}
				else
				{
					SetupCartCHRMapping(0, VROM, CHRRAMSize, 1);
					AddExState(VROM, CHRRAMSize, 0, "CHRR");
				}
			}
			if (head.ROM_type & 8)
				AddExState(ExtraNTARAM, 2048, 0, "EXNR");
			tmp->init(&iNESCart);
			return 1;
		}
		tmp++;
	}
	return 0;
}
