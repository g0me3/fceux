/* FCE Ultra - NES/Famicom Emulator
 *
 * Copyright notice for this file:
 *  Copyright (C) 1998 BERO
 *  Copyright (C) 2003 Xodnizel
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
#include "ppu.h"
#include "nsf.h"
#include "sound.h"
#include "file.h"
#include "utils/endian.h"
#include "utils/memory.h"
		 
#include "cart.h"
#include "palette.h"
#include "state.h"
#include "video.h"
#include "input.h"
#include "driver.h"
#include "debug.h"
		 
#include <cstring>
#include <cstdio>
#include <cstdlib>

#define VBlankON    (PPU[0] & 0x80)	//Generate VBlank NMI
#define Sprite16    (PPU[0] & 0x20)	//Sprites 8x16/8x8
#define BGAdrHI     (PPU[0] & 0x10)	//BG pattern adr $0000/$1000
#define SpAdrHI     (PPU[0] & 0x08)	//Sprite pattern adr $0000/$1000
#define INC32       (PPU[0] & 0x04)	//auto increment 1/32

#define SpriteON    (PPU[1] & 0x10)	//Show Sprite
#define ScreenON    (PPU[1] & 0x08)	//Show screen
#define PPUON       (PPU[1] & 0x18)	//PPU should operate
#define GRAYSCALE   (PPU[1] & 0x01)	//Grayscale (AND palette entries with 0x30)

#define SpriteLeft8 (PPU[1] & 0x04)
#define BGLeft8     (PPU[1] & 0x02)

#define PPU_status  (PPU[2])

#define READPAL(ofs)    (PALRAM[(ofs)] & (GRAYSCALE ? 0x30 : 0xFF))
#define READUPAL(ofs)   (UPALRAM[(ofs)] & (GRAYSCALE ? 0x30 : 0xFF))

static void FetchSpriteData(void);
static void RefreshLine(int lastpixel);
static void RefreshSprites(void);
static void CopySprites(uint8 *target);

static void Fixit1(void);

int test = 0;

struct PPUSTATUS {
	int32 sl;
	int32 cycle, end_cycle;
};

static int ppudead = 1;
static int kook = 0;
int fceuindbg = 0;

//mbg 6/23/08
//make the no-bg fill color configurable
//0xFF shall indicate to use palette[0]
uint8 gNoBGFillColor = 0xFF;

uint8 VRAMBuffer = 0, PPUGenLatch = 0;
uint8 *vnapage[4];
uint8 PPUNTARAM = 0;
uint8 PPUCHRRAM = 0;

//Color deemphasis emulation.  Joy...
static uint8 deemp = 0;
static int deempcnt[8];

void (*GameHBIRQHook)(void), (*GameHBIRQHook2)(void);

uint8 vtoggle = 0;
uint8 XOffset = 0;
uint8 SpriteDMA = 0; // $4014 / Writing $xx copies 256 bytes by reading from $xx00-$xxFF and writing to $2004 (OAM data)

uint32 TempAddr = 0, RefreshAddr = 0, DummyRead = 0, NTRefreshAddr = 0;

static int maxsprites = 8;

//scanline is equal to the current visible scanline we're on.
int scanline;
int g_rasterpos;
static uint32 scanlines_per_frame;

#define SPRAM_SIZE 0x200
#define PPUR_SIZE 16

uint8 PPU[PPUR_SIZE];
uint8 PPUSPL;
uint8 NTARAM[0x800], PALRAM[0x20], SPRAM[SPRAM_SIZE], SPRBUF[0x100];
uint8 UPALRAM[0x03];//for 0x4/0x8/0xC addresses in palette, the ones in
					//0x20 are 0 to not break fceu rendering.

#define VRAMADR(V)          &VPage[(V) >> 10][(V)]

uint8 READPAL_MOTHEROFALL(uint32 A)
{
	if(!(A & 3)) {
		if(!(A & 0xC))
			return READPAL(0x00);
		else
			return READUPAL(((A & 0xC) >> 2) - 1);
	}
	else
		return READPAL(A & 0x1F);
}

//this duplicates logic which is embedded in the ppu rendering code
//which figures out where to get CHR data from depending on various hack modes
//mostly involving mmc5.
//this might be incomplete.
uint8* FCEUPPU_GetCHR(uint32 vadr, uint32 refreshaddr) {
	return VRAMADR(vadr);
}

//likewise for ATTR
int FCEUPPU_GetAttr(int ntnum, int xt, int yt) {
	int attraddr = 0x3C0 + ((yt >> 2) << 3) + (xt >> 2);
	int temp = (((yt & 2) << 1) + (xt & 2));
	int refreshaddr = xt + yt * 32;
	return (vnapage[ntnum][attraddr] & (3 << temp)) >> temp;
}

//new ppu-----
inline void FFCEUX_PPUWrite_Default(uint32 A, uint8 V) {
	uint32 tmp = A;

	if (tmp < 0x2000) {
		if (PPUCHRRAM & (1 << (tmp >> 10)))
			VPage[tmp >> 10][tmp] = V;
	} else if (tmp < 0x3F00) {
		if (PPUNTARAM & (1 << ((tmp & 0xF00) >> 10)))
			vnapage[((tmp & 0xF00) >> 10)][tmp & 0x3FF] = V;
	} else {
		if (!(tmp & 3)) {
			if (!(tmp & 0xC)) {
				PALRAM[0x00] = PALRAM[0x04] = PALRAM[0x08] = PALRAM[0x0C] = V & 0x3F;
				PALRAM[0x10] = PALRAM[0x14] = PALRAM[0x18] = PALRAM[0x1C] = V & 0x3F;
			}
			else
				UPALRAM[((tmp & 0xC) >> 2) - 1] = V & 0x3F;
		} else
			PALRAM[tmp & 0x1F] = V & 0x3F;
	}
}

volatile int rendercount, vromreadcount, undefinedvromcount, LogAddress = -1;
unsigned char *cdloggervdata = NULL;
unsigned int cdloggerVideoDataSize = 0;

int GetCHRAddress(int A) {
	if (cdloggerVideoDataSize) {
		int result = &VPage[A >> 10][A] - CHRptr[0];
		if ((result >= 0) && (result < (int)cdloggerVideoDataSize))
			return result;
	} else
		if(A < 0x2000) return A;
	return -1;
}

int GetCHROffset(uint8 *ptr) {
	int result = ptr - CHRptr[0];
	if (cdloggerVideoDataSize) {
		if ((result >= 0) && (result < (int)cdloggerVideoDataSize))
			return result;
	} else {
		if ((result >= 0) && (result < 0x2000))
			return result;
	}
	return -1;
}

#define RENDER_LOG(tmp) { \
		if (debug_loggingCD) \
		{ \
			int addr = GetCHRAddress(tmp); \
			if (addr != -1)	\
			{ \
				if (!(cdloggervdata[addr] & 1))	\
				{ \
					cdloggervdata[addr] |= 1; \
					if(cdloggerVideoDataSize) { \
						if (!(cdloggervdata[addr] & 2)) undefinedvromcount--; \
						rendercount++; \
					} \
				} \
			} \
		} \
}

#define RENDER_LOGP(tmp) { \
		if (debug_loggingCD) \
		{ \
			int addr = GetCHROffset(tmp); \
			if (addr != -1)	\
			{ \
				if (!(cdloggervdata[addr] & 1))	\
				{ \
					cdloggervdata[addr] |= 1; \
					if(cdloggerVideoDataSize) { \
						if (!(cdloggervdata[addr] & 2)) undefinedvromcount--; \
						rendercount++; \
					} \
				} \
			} \
		} \
}

uint8 FASTCALL FFCEUX_PPURead_Default(uint32 A) {
	uint32 tmp = A;

	if (tmp < 0x2000) {
		return VPage[tmp >> 10][tmp];
	} else if (tmp < 0x3F00) {
		return vnapage[(tmp >> 10) & 0x3][tmp & 0x3FF];
	} else {
		uint8 ret;
		if (!(tmp & 3)) {
			if (!(tmp & 0xC))
				ret = READPAL(0x00);
			else
				ret = READUPAL(((tmp & 0xC) >> 2) - 1);
		} else
			ret = READPAL(tmp & 0x1F);
		return ret;
	}
}


uint8 (FASTCALL *FFCEUX_PPURead)(uint32 A) = 0;
void (*FFCEUX_PPUWrite)(uint32 A, uint8 V) = 0;

#define CALL_PPUREAD(A) (FFCEUX_PPURead(A))

#define CALL_PPUWRITE(A, V) (FFCEUX_PPUWrite ? FFCEUX_PPUWrite(A, V) : FFCEUX_PPUWrite_Default(A, V))

void ppu_getScroll(int &xpos, int &ypos) {
	xpos = ((RefreshAddr & 0x400) >> 2) | ((RefreshAddr & 0x1F) << 3) | XOffset;
	ypos = ((RefreshAddr & 0x3E0) >> 2) | ((RefreshAddr & 0x7000) >> 12);
	if (RefreshAddr & 0x800) ypos += 240;
}

static DECLFR(A2002) {
	uint8 ret;

	FCEUPPU_LineUpdate();
	ret = PPU_status;
	ret |= PPUGenLatch & 0x1F;

#ifdef FCEUDEF_DEBUGGER
	if (!fceuindbg)
#endif
	{
		vtoggle = 0;
		PPU_status &= 0x7F;
		PPUGenLatch = ret;
	}

	return ret;
}

static DECLFR(A2004) {
	FCEUPPU_LineUpdate();
	return PPUGenLatch;
}

static DECLFR(A200x) {	/* Not correct for $2004 reads. */
	FCEUPPU_LineUpdate();
	return PPUGenLatch;
}

static DECLFR(A2007) {
	uint8 ret;
	uint32 tmp = RefreshAddr & 0x3FFF;

	if (debug_loggingCD) {
		if (!DummyRead && (LogAddress != -1)) {
			if (!(cdloggervdata[LogAddress] & 2)) {
				cdloggervdata[LogAddress] |= 2;
				if ((!(cdloggervdata[LogAddress] & 1)) && cdloggerVideoDataSize) undefinedvromcount--;
				vromreadcount++;
			}
		} else
			DummyRead = 0;
	}

	//OLDPPU
	FCEUPPU_LineUpdate();

	if (tmp >= 0x3F00) {	// Palette RAM tied directly to the output data, without VRAM buffer
		if (!(tmp & 3)) {
			if (!(tmp & 0xC))
				ret = READPAL(0x00);
			else
				ret = READUPAL(((tmp & 0xC) >> 2) - 1);
		} else
			ret = READPAL(tmp & 0x1F);
		#ifdef FCEUDEF_DEBUGGER
		if (!fceuindbg)
		#endif
		{
			if ((tmp - 0x1000) < 0x2000)
				VRAMBuffer = VPage[(tmp - 0x1000) >> 10][tmp - 0x1000];
			else
				VRAMBuffer = vnapage[((tmp - 0x1000) >> 10) & 0x3][(tmp - 0x1000) & 0x3FF];
		}
	} else {
		ret = VRAMBuffer;
		#ifdef FCEUDEF_DEBUGGER
		if (!fceuindbg)
		#endif
		{
			PPUGenLatch = VRAMBuffer;
			if (tmp < 0x2000) {
				if (debug_loggingCD)
					LogAddress = GetCHRAddress(tmp);
				VRAMBuffer = VPage[tmp >> 10][tmp];
			} else if (tmp < 0x3F00)
				VRAMBuffer = vnapage[(tmp >> 10) & 0x3][tmp & 0x3FF];
		}

	#ifdef FCEUDEF_DEBUGGER
		if (!fceuindbg)
	#endif
		{
			if ((ScreenON || SpriteON) && (scanline < 240)) {
				uint32 rad = RefreshAddr;
				if ((rad & 0x7000) == 0x7000) {
					rad ^= 0x7000;
					if ((rad & 0x3E0) == 0x3A0)
						rad ^= 0xBA0;
					else if ((rad & 0x3E0) == 0x3e0)
						rad ^= 0x3e0;
					else
						rad += 0x20;
				} else
					rad += 0x1000;
				RefreshAddr = rad;
			} else {
				if (INC32)
					RefreshAddr += 32;
				else
					RefreshAddr++;
			}
		}
	}
	return ret;
}

static DECLFW(B2000) {
	FCEUPPU_LineUpdate();
	PPUGenLatch = V;

	if (!(PPU[0] & 0x80) && (V & 0x80) && (PPU_status & 0x80))
		TriggerNMI2();

	PPU[0] = V;
	TempAddr &= 0xF3FF;
	TempAddr |= (V & 3) << 10;
}

static DECLFW(B2001) {
	FCEUPPU_LineUpdate();
	if (paldeemphswap)
		V = (V&0x9F)|((V&0x40)>>1)|((V&0x20)<<1);
	PPUGenLatch = V;
	PPU[1] = V;
	if (V & 0xE0)
		deemp = V >> 5;
}

static DECLFW(B2002) {
	PPUGenLatch = V;
}

static DECLFW(B2003) {
	PPUGenLatch = V;
	PPU[3] = V;
	PPUSPL = V & 0x7;
}

static DECLFW(B2004) {
	PPUGenLatch = V;
	if ((PPUSPL | (PPU[8] << 8)) >= 8) {
		if ((PPU[3] | (PPU[8] << 8)) >= 8)
			SPRAM[PPU[3] | (PPU[8] << 8)] = V;
	} else {
		SPRAM[PPUSPL] = V;
	}
	PPU[3]++;
	if (!PPU[3])
		PPU[8] ^= 1;
	PPUSPL++;
}

static DECLFW(B2008) {
	PPU[8] = V;
}

static DECLFW(B2005) {
	uint32 tmp = TempAddr;
	FCEUPPU_LineUpdate();
	PPUGenLatch = V;
	if (!vtoggle) {
		tmp &= 0xFFE0;
		tmp |= V >> 3;
		XOffset = V & 7;
	} else {
		tmp &= 0x8C1F;
		tmp |= ((V & ~0x7) << 2);
		tmp |= (V & 7) << 12;
	}
	TempAddr = tmp;
	vtoggle ^= 1;
}


static DECLFW(B2006) {
	FCEUPPU_LineUpdate();

	PPUGenLatch = V;
	if (!vtoggle) {
		TempAddr &= 0x00FF;
		TempAddr |= (V & 0x3f) << 8;
	} else {
		TempAddr &= 0xFF00;
		TempAddr |= V;
		RefreshAddr = TempAddr;
		DummyRead = 1;
	}
	vtoggle ^= 1;
}

static DECLFW(B2007) {
	uint32 tmp = RefreshAddr & 0x3FFF;

	if (debug_loggingCD) {
		if(!cdloggerVideoDataSize && (tmp < 0x2000))
			cdloggervdata[tmp] = 0;
	}

	PPUGenLatch = V;
	if (tmp < 0x2000) {
		if (PPUCHRRAM & (1 << (tmp >> 10)))
			VPage[tmp >> 10][tmp] = V;
	} else if (tmp < 0x3F00) {
		if (PPUNTARAM & (1 << ((tmp & 0xF00) >> 10)))
			vnapage[((tmp & 0xF00) >> 10)][tmp & 0x3FF] = V;
	} else {
		if (!(tmp & 3)) {
			if (!(tmp & 0xC))
				PALRAM[0x00] = PALRAM[0x04] = PALRAM[0x08] = PALRAM[0x0C] = V & 0x3F;
			else
				UPALRAM[((tmp & 0xC) >> 2) - 1] = V & 0x3F;
		} else
			PALRAM[tmp & 0x1F] = V & 0x3F;
	}
	if (INC32)
		RefreshAddr += 32;
	else
		RefreshAddr++;
}

//static DECLFW(B4014) {
//	uint32 t = V << 8;
//	int x;
//
//	for (x = 0; x < 256; x++)
//		X6502_DMW(0x2004, X6502_DMR(t + x));
//	
//	SpriteDMA = V;
//}

#define PAL(c)  ((c) + cc)

#define GETLASTPIXEL    (PAL ? ((timestamp * 48 - linestartts) / 15) : ((timestamp * 48 - linestartts) >> 4))

static uint8 *Pline, *Plinef;
static int firsttile;
int linestartts;	//no longer static so the debugger can see it
static int tofix = 0;

static void ResetRL(uint8 *target) {
	memset(target, 0xFF, 256);
	InputScanlineHook(0, 0, 0, 0);
	Plinef = target;
	Pline = target;
	firsttile = 0;
	linestartts = timestamp * 48 + X.count;
	tofix = 0;
	FCEUPPU_LineUpdate();
	tofix = 1;
}

static uint8 sprlinebuf[256 + 8];

void FCEUPPU_LineUpdate(void) {
#ifdef FCEUDEF_DEBUGGER
	if (!fceuindbg)
#endif
	if (Pline) {
		int l = GETLASTPIXEL;
		RefreshLine(l);
	}
}

static bool rendersprites = true, renderbg = true;

void FCEUI_SetRenderPlanes(bool sprites, bool bg) {
	rendersprites = sprites;
	renderbg = bg;
}

void FCEUI_GetRenderPlanes(bool& sprites, bool& bg) {
	sprites = rendersprites;
	bg = renderbg;
}

static void CheckSpriteHit(int p);

static void EndRL(void) {
	RefreshLine(272);
	if (tofix)
		Fixit1();
	CheckSpriteHit(272);
	Pline = 0;
}

static int32 sphitx;
static uint8 sphitdata;

static void CheckSpriteHit(int p) {
	int l = p - 16;
	int x;

	if (sphitx == 0x100) return;

	for (x = sphitx; x < (sphitx + 8) && x < l; x++) {
		if ((sphitdata & (0x80 >> (x - sphitx))) && !(Plinef[x] & 64) && x < 255) {
			PPU_status |= 0x40;
			sphitx = 0x100;
			break;
		}
	}
}

//spork the world.  Any sprites on this line? Then this will be set to 1.
//Needed for zapper emulation and *gasp* sprite emulation.
static int spork = 0;

// lasttile is really "second to last tile."
static void RefreshLine(int lastpixel) {
	static uint32 pshift[4];
	static uint32 atlatch;
	uint32 smorkus = RefreshAddr;

	#define RefreshAddr smorkus
	uint32 vofs;
	int X1;

	register uint8 *P = Pline;
	int lasttile = lastpixel >> 3;
	int numtiles;
	static int norecurse = 0;	// Yeah, recursion would be bad.
	if (norecurse) return;

	if (sphitx != 0x100 && !(PPU_status & 0x40)) {
		if ((sphitx < (lastpixel - 16)) && !(sphitx < ((lasttile - 2) * 8)))
			lasttile++;
	}

	if (lasttile > 34) lasttile = 34;
	numtiles = lasttile - firsttile;

	if (numtiles <= 0) return;

	P = Pline;
	vofs = ((RefreshAddr >> 12) & 7);

	if (!ScreenON && !SpriteON) {
		uint32 tem;
		tem = READPAL(0) | (READPAL(0) << 8) | (READPAL(0) << 16) | (READPAL(0) << 24);
		tem |= 0x40404040;
		FCEU_dwmemset(Pline, tem, numtiles * 8);
		P += numtiles * 8;
		Pline = P;

		firsttile = lasttile;

		#define TOFIXNUM (272 - 0x4)
		if (lastpixel >= TOFIXNUM && tofix) {
			Fixit1();
			tofix = 0;
		}

		if ((lastpixel - 16) >= 0) {
			InputScanlineHook(Plinef, spork ? sprlinebuf : 0, linestartts, lasttile * 8 - 16);
		}
		return;
	}

	//Priority bits, needed for sprite emulation.
	PALRAM[0] |= 64;
	PALRAM[4] |= 64;
	PALRAM[8] |= 64;
	PALRAM[0xC] |= 64;

	for (X1 = firsttile; X1 < lasttile; X1++) {
		register uint8 cc;
		register uint8 zz;
		uint32 vadr;
		uint8 *C;
		if (X1 >= 2) {
			uint8 *S = PALRAM;
			P[0] = S[(pshift[0] >> (8 - XOffset)) & 0xF];
			P[1] = S[((pshift[0] >> (8 - XOffset)) >> 4) & 0xF];
			P[2] = S[(pshift[1] >> (8 - XOffset)) & 0xF];
			P[3] = S[((pshift[1] >> (8 - XOffset)) >> 4) & 0xF];
			P[4] = S[(pshift[2] >> (8 - XOffset)) & 0xF];
			P[5] = S[((pshift[2] >> (8 - XOffset)) >> 4) & 0xF];
			P[6] = S[(pshift[3] >> (8 - XOffset)) & 0xF];
			P[7] = S[((pshift[3] >> (8 - XOffset)) >> 4) & 0xF];
			P += 8;
		}
		zz = RefreshAddr & 0x1F;
		C = vnapage[(RefreshAddr >> 10) & 3];
		vadr = C[RefreshAddr & 0x3ff];
		vadr = ((vadr << 5) + (vofs << 2));				// Fetch name table byte.

		cc = C[0x3c0 + (zz >> 2) + ((RefreshAddr & 0x380) >> 4)];	// Fetch attribute table byte.
		cc = (cc >> ((zz & 2) + ((RefreshAddr & 0x40) >> 4))) & 3;
			
		pshift[0] <<= 8;
		pshift[1] <<= 8;
		pshift[2] <<= 8;
		pshift[3] <<= 8;

		C = &CHRptr[0][((ppu20xx[0x20] + cc) * 8192) + vadr];

		pshift[0] |= C[0];
		pshift[1] |= C[1];
		pshift[2] |= C[2];
		pshift[3] |= C[3];

		if ((RefreshAddr & 0x1f) == 0x1f)
			RefreshAddr ^= 0x41F;
		else
			RefreshAddr++;
	}

#undef vofs
#undef RefreshAddr

	//Reverse changes made before.
	PALRAM[0] &= 63;
	PALRAM[4] &= 63;
	PALRAM[8] &= 63;
	PALRAM[0xC] &= 63;

	RefreshAddr = smorkus;
	if (firsttile <= 2 && 2 < lasttile && !(PPU[1] & 2)) {
		uint32 tem;
		tem = READPAL(0) | (READPAL(0) << 8) | (READPAL(0) << 16) | (READPAL(0) << 24);
		tem |= 0x40404040;
		*(uint32*)Plinef = *(uint32*)(Plinef + 4) = tem;
	}

	if (!ScreenON) {
		uint32 tem;
		int tstart, tcount;
		tem = READPAL(0) | (READPAL(0) << 8) | (READPAL(0) << 16) | (READPAL(0) << 24);
		tem |= 0x40404040;

		tcount = lasttile - firsttile;
		tstart = firsttile - 2;
		if (tstart < 0) {
			tcount += tstart;
			tstart = 0;
		}
		if (tcount > 0)
			FCEU_dwmemset(Plinef + tstart * 8, tem, tcount * 8);
	}

	if (lastpixel >= TOFIXNUM && tofix) {
		Fixit1();
		tofix = 0;
	}

	//This only works right because of a hack earlier in this function.
	CheckSpriteHit(lastpixel);

	if ((lastpixel - 16) >= 0) {
		InputScanlineHook(Plinef, spork ? sprlinebuf : 0, linestartts, lasttile * 8 - 16);
	}
	Pline = P;
	firsttile = lasttile;
}

static INLINE void Fixit2(void) {
	if (ScreenON || SpriteON) {
		uint32 rad = RefreshAddr;
		rad &= 0xFBE0;
		rad |= TempAddr & 0x041f;
		RefreshAddr = rad;
	}
}

static void Fixit1(void) {
	if (ScreenON || SpriteON) {
		uint32 rad = RefreshAddr;

		if ((rad & 0x7000) == 0x7000) {
			rad ^= 0x7000;
			if ((rad & 0x3E0) == 0x3A0)
				rad ^= 0xBA0;
			else if ((rad & 0x3E0) == 0x3e0)
				rad ^= 0x3e0;
			else
				rad += 0x20;
		} else
			rad += 0x1000;
		RefreshAddr = rad;
	}
}

static void DoLine(void) {
	if (scanline >= 240 && scanline != totalscanlines) {
		X6502_Run(256 + 69);
		scanline++;
		X6502_Run(16);
		return;
	}

	int x;
	uint8 *target = XBuf + ((scanline < 240 ? scanline : 240) << 8);
	u8* dtarget = XDBuf + ((scanline < 240 ? scanline : 240) << 8);

	X6502_Run(256);
	EndRL();

	if (!renderbg) {	// User asked to not display background data.
		uint32 tem;
		uint8 col;
		if (gNoBGFillColor == 0xFF)
			col = READPAL(0);
		else col = gNoBGFillColor;
		tem = col | (col << 8) | (col << 16) | (col << 24);
		tem |= 0x40404040; 
		FCEU_dwmemset(target, tem, 256);
	}

	if (SpriteON)
		CopySprites(target);

	//greyscale handling (mask some bits off the color) ? ? ?
	if (ScreenON || SpriteON)
	{
		if (PPU[1] & 0x01) {
			for (x = 63; x >= 0; x--)
				*(uint32*)&target[x << 2] = (*(uint32*)&target[x << 2]) & 0x30303030;
		}
	}

	//some pathetic attempts at deemph
	if ((PPU[1] >> 5) == 0x7) {
		for (x = 63; x >= 0; x--)
			*(uint32*)&target[x << 2] = ((*(uint32*)&target[x << 2]) & 0x3f3f3f3f) | 0xc0c0c0c0;
	} else if (PPU[1] & 0xE0)
		for (x = 63; x >= 0; x--)
			*(uint32*)&target[x << 2] = (*(uint32*)&target[x << 2]) | 0x40404040;
	else
		for (x = 63; x >= 0; x--)
			*(uint32*)&target[x << 2] = ((*(uint32*)&target[x << 2]) & 0x3f3f3f3f) | 0x80808080;

	//write the actual deemph
	for (x = 63; x >= 0; x--)
		*(uint32*)&dtarget[x << 2] = ((PPU[1]>>5)<<0)|((PPU[1]>>5)<<8)|((PPU[1]>>5)<<16)|((PPU[1]>>5)<<24);

	sphitx = 0x100;

	if (ScreenON || SpriteON)
		FetchSpriteData();

	if (GameHBIRQHook && (ScreenON || SpriteON) && ((PPU[0] & 0x38) != 0x18)) {
		X6502_Run(6);
		Fixit2();
		X6502_Run(4);
		GameHBIRQHook();
		X6502_Run(85 - 16 - 10);
	} else {
		X6502_Run(6);	// Tried 65, caused problems with Slalom(maybe others)
		Fixit2();
		X6502_Run(85 - 6 - 16);

		// A semi-hack for Star Trek: 25th Anniversary
		if (GameHBIRQHook && (ScreenON || SpriteON) && ((PPU[0] & 0x38) != 0x18))
			GameHBIRQHook();
	}

	DEBUG(FCEUD_UpdateNTView(scanline, 0));

	if (SpriteON)
		RefreshSprites();
	if (GameHBIRQHook2 && (ScreenON || SpriteON))
		GameHBIRQHook2();
	scanline++;
	if (scanline < 240) {
		ResetRL(XBuf + (scanline << 8));
	}
	X6502_Run(16);
}

#define V_FLIP  0x80
#define H_FLIP  0x40
#define SP_BACK 0x20

typedef struct {
	uint8 y, no, atr, x;
} SPR;

typedef struct {
	uint8 ca[2], atr, x;
} SPRB;

void FCEUI_DisableSpriteLimitation(int a) {
	maxsprites = a ? 64 : 8;
}

static uint8 numsprites, SpriteBlurp;
static void FetchSpriteData(void) {
	uint8 ns, sb;
	SPR *spr;
	uint8 H;
	int n;
	int vofs;
	uint8 P0 = PPU[0];

	spr = (SPR*)SPRAM;
	H = 8;

	ns = sb = 0;

	vofs = (uint32)(P0 & 0x8 & (((P0 & 0x20) ^ 0x20) >> 2)) << 9;
	H += (P0 & 0x20) >> 2;

		for (n = 63; n >= 0; n--, spr++) {
			if ((uint32)(scanline - spr->y) >= H) continue;
			if (ns < maxsprites) {
				SPRB dst;
				uint8 *C;
				int t;
				uint32 vadr;
				if (n == 63) sb = 1;


				t = (int)scanline - (spr->y);

				if (Sprite16)
					vadr = ((spr->no & 1) << 12) + ((spr->no & 0xFE) << 4);
				else
					vadr = (spr->no << 4) + vofs;

				if (spr->atr & V_FLIP) {
					vadr += 7;
					vadr -= t;
					vadr += (P0 & 0x20) >> 1;
					vadr -= t & 8;
				} else {
					vadr += t;
					vadr += t & 8;
				}

				/* Fix this geniestage hack */
				C = VRAMADR(vadr);

				if (SpriteON)
					RENDER_LOGP(C);
				dst.ca[0] = C[0];
				if (SpriteON)
					RENDER_LOGP(C + 8);
				dst.ca[1] = C[8];
				dst.x = spr->x;
				dst.atr = spr->atr;

				*(uint32*)&SPRBUF[ns << 2] = *(uint32*)&dst;

				ns++;
			} else {
				PPU_status |= 0x20;
				break;
			}
		}

	//Handle case when >8 sprites per scanline option is enabled.
	if (ns > 8) PPU_status |= 0x20;
	numsprites = ns;
	SpriteBlurp = sb;
}

static void RefreshSprites(void) {
	int n;
	SPRB *spr;

	spork = 0;
	if (!numsprites) return;

	FCEU_dwmemset(sprlinebuf, 0x80808080, 256);
	numsprites--;
	spr = (SPRB*)SPRBUF + numsprites;

	for (n = numsprites; n >= 0; n--, spr--) {
		uint32 pixdata;
		uint8 J, atr;

		int x = spr->x;
		uint8 *C;
		int VB;

		pixdata = 0xffffffff;  // ppulut1[spr->ca[0]] | ppulut2[spr->ca[1];
		J = spr->ca[0] | spr->ca[1];
		atr = spr->atr;

		if (J) {
			if (n == 0 && SpriteBlurp && !(PPU_status & 0x40)) {
				sphitx = x;
				sphitdata = J;
				if (atr & H_FLIP)
					sphitdata = ((J << 7) & 0x80) |
								((J << 5) & 0x40) |
								((J << 3) & 0x20) |
								((J << 1) & 0x10) |
								((J >> 1) & 0x08) |
								((J >> 3) & 0x04) |
								((J >> 5) & 0x02) |
								((J >> 7) & 0x01);
			}

			C = sprlinebuf + x;
			VB = (0x10) + ((atr & 3) << 2);

			if (atr & SP_BACK) {
				if (atr & H_FLIP) {
					if (J & 0x80) C[7] = READPAL(VB | (pixdata & 3)) | 0x40;
					pixdata >>= 4;
					if (J & 0x40) C[6] = READPAL(VB | (pixdata & 3)) | 0x40;
					pixdata >>= 4;
					if (J & 0x20) C[5] = READPAL(VB | (pixdata & 3)) | 0x40;
					pixdata >>= 4;
					if (J & 0x10) C[4] = READPAL(VB | (pixdata & 3)) | 0x40;
					pixdata >>= 4;
					if (J & 0x08) C[3] = READPAL(VB | (pixdata & 3)) | 0x40;
					pixdata >>= 4;
					if (J & 0x04) C[2] = READPAL(VB | (pixdata & 3)) | 0x40;
					pixdata >>= 4;
					if (J & 0x02) C[1] = READPAL(VB | (pixdata & 3)) | 0x40;
					pixdata >>= 4;
					if (J & 0x01) C[0] = READPAL(VB | pixdata) | 0x40;
				} else {
					if (J & 0x80) C[0] = READPAL(VB | (pixdata & 3)) | 0x40;
					pixdata >>= 4;
					if (J & 0x40) C[1] = READPAL(VB | (pixdata & 3)) | 0x40;
					pixdata >>= 4;
					if (J & 0x20) C[2] = READPAL(VB | (pixdata & 3)) | 0x40;
					pixdata >>= 4;
					if (J & 0x10) C[3] = READPAL(VB | (pixdata & 3)) | 0x40;
					pixdata >>= 4;
					if (J & 0x08) C[4] = READPAL(VB | (pixdata & 3)) | 0x40;
					pixdata >>= 4;
					if (J & 0x04) C[5] = READPAL(VB | (pixdata & 3)) | 0x40;
					pixdata >>= 4;
					if (J & 0x02) C[6] = READPAL(VB | (pixdata & 3)) | 0x40;
					pixdata >>= 4;
					if (J & 0x01) C[7] = READPAL(VB | pixdata) | 0x40;
				}
			} else {
				if (atr & H_FLIP) {
					if (J & 0x80) C[7] = READPAL(VB | (pixdata & 3));
					pixdata >>= 4;
					if (J & 0x40) C[6] = READPAL(VB | (pixdata & 3));
					pixdata >>= 4;
					if (J & 0x20) C[5] = READPAL(VB | (pixdata & 3));
					pixdata >>= 4;
					if (J & 0x10) C[4] = READPAL(VB | (pixdata & 3));
					pixdata >>= 4;
					if (J & 0x08) C[3] = READPAL(VB | (pixdata & 3));
					pixdata >>= 4;
					if (J & 0x04) C[2] = READPAL(VB | (pixdata & 3));
					pixdata >>= 4;
					if (J & 0x02) C[1] = READPAL(VB | (pixdata & 3));
					pixdata >>= 4;
					if (J & 0x01) C[0] = READPAL(VB | pixdata);
				} else {
					if (J & 0x80) C[0] = READPAL(VB | (pixdata & 3));
					pixdata >>= 4;
					if (J & 0x40) C[1] = READPAL(VB | (pixdata & 3));
					pixdata >>= 4;
					if (J & 0x20) C[2] = READPAL(VB | (pixdata & 3));
					pixdata >>= 4;
					if (J & 0x10) C[3] = READPAL(VB | (pixdata & 3));
					pixdata >>= 4;
					if (J & 0x08) C[4] = READPAL(VB | (pixdata & 3));
					pixdata >>= 4;
					if (J & 0x04) C[5] = READPAL(VB | (pixdata & 3));
					pixdata >>= 4;
					if (J & 0x02) C[6] = READPAL(VB | (pixdata & 3));
					pixdata >>= 4;
					if (J & 0x01) C[7] = READPAL(VB | pixdata);
				}
			}
		}
	}
	SpriteBlurp = 0;
	spork = 1;
}

static void CopySprites(uint8 *target) {
	uint8 *P = target;

	if (!spork) return;
	
	spork = 0;

	if (!rendersprites) return;						// User asked to not display sprites.

	if(!SpriteON) return;

	for(int i = 0; i < 256; i++)
	{
		uint8 t = sprlinebuf[i];
		if(!(t&0x80))
			if (!(t & 0x40) || (P[i] & 0x40))		// Normal sprite || behind bg sprite
				P[i] = t;
	}
}

void FCEUPPU_SetVideoSystem(int w) {
	if (w) {
		scanlines_per_frame = dendy ? 262: 312;
		FSettings.FirstSLine = FSettings.UsrFirstSLine[1];
		FSettings.LastSLine = FSettings.UsrLastSLine[1];
	} else {
		scanlines_per_frame = 262;
		FSettings.FirstSLine = FSettings.UsrFirstSLine[0];
		FSettings.LastSLine = FSettings.UsrLastSLine[0];
	}
}

void PPU_ResetHooks() {
	FFCEUX_PPURead = FFCEUX_PPURead_Default;
}

void FCEUPPU_Reset(void) {
	VRAMBuffer = PPU[0] = PPU[1] = PPU_status = PPU[3] = PPU[8] = 0;
	PPUSPL = 0;
	PPUGenLatch = 0;
	RefreshAddr = TempAddr = 0;
	vtoggle = 0;
	ppudead = 2;
	kook = 0;
}

void FCEUPPU_Power(void) {
	int x;

	memset(NTARAM, 0x00, 0x800);
	memset(PALRAM, 0x00, 0x20);
	memset(UPALRAM, 0x00, 0x03);
	memset(SPRAM, 0x00, SPRAM_SIZE);
	FCEUPPU_Reset();

	for (x = 0x2000; x < 0x4000; x += 8) {
		ARead[x] = A200x;
		BWrite[x] = B2000;
		ARead[x + 1] = A200x;
		BWrite[x + 1] = B2001;
		ARead[x + 2] = A2002;
		BWrite[x + 2] = B2002;
		ARead[x + 3] = A200x;
		BWrite[x + 3] = B2003;
		ARead[x + 4] = A2004;
		BWrite[x + 4] = B2004;
		ARead[x + 5] = A200x;
		BWrite[x + 5] = B2005;
		ARead[x + 6] = A200x;
		BWrite[x + 6] = B2006;
		ARead[x + 7] = A2007;
		BWrite[x + 7] = B2007;
	}
	BWrite[0x2008] = B2008;
//	BWrite[0x4014] = B4014;
}

int FCEUPPU_Loop(int skip) {
	//Needed for Knight Rider, possibly others.
	if (ppudead) {
		memset(XBuf, 0x80, 256 * 240);
		X6502_Run(scanlines_per_frame * (256 + 85));
		ppudead--;
	} else {
		X6502_Run(256 + 85);
		PPU_status |= 0x80;

		//Not sure if this is correct.  According to Matt Conte and my own tests, it is.
		//Timing is probably off, though.
		//NOTE:  Not having this here breaks a Super Donkey Kong game.
		PPU[3] = PPU[8] = PPUSPL = 0;

		//I need to figure out the true nature and length of this delay.
		X6502_Run(12);
		if (GameInfo->type == GIT_NSF)
			DoNSFFrame();
		else {
			if (VBlankON)
				TriggerNMI();
		}
		X6502_Run((scanlines_per_frame - 242) * (256 + 85) - 12);
		if (overclock_enabled && vblankscanlines) {
			if (!DMC_7bit || !skip_7bit_overclocking) {
				overclocking = 1;
				X6502_Run(vblankscanlines * (256 + 85) - 12);
				overclocking = 0;
			}
		}
		PPU_status &= 0x1f;
		X6502_Run(256);

		if (ScreenON || SpriteON) {
			if (GameHBIRQHook && ((PPU[0] & 0x38) != 0x18))
				GameHBIRQHook();
			if (GameHBIRQHook2)
				GameHBIRQHook2();
		}
		X6502_Run(85 - 16);
		if (ScreenON || SpriteON) {
			RefreshAddr = TempAddr;
		}

		//Clean this stuff up later.
		spork = numsprites = 0;
		ResetRL(XBuf);

		X6502_Run(16 - kook);
		kook ^= 1;

		if (GameInfo->type == GIT_NSF)
			X6502_Run((256 + 85) * normalscanlines);
		else {
			deemp = PPU[1] >> 5;

			// manual samples can't play correctly with overclocking
			if (DMC_7bit && skip_7bit_overclocking) // 7bit sample started before 240th line
				totalscanlines = normalscanlines;
			else
				totalscanlines = normalscanlines + (overclock_enabled ? postrenderscanlines : 0);

			for (scanline = 0; scanline < totalscanlines; ) {	//scanline is incremented in  DoLine.  Evil. :/
				deempcnt[deemp]++;
				if (scanline < 240)
					DEBUG(FCEUD_UpdatePPUView(scanline, 1));
				DoLine();

				if (scanline < normalscanlines || scanline == totalscanlines)
					overclocking = 0;
				else {
					if (DMC_7bit && skip_7bit_overclocking) // 7bit sample started after 240th line
						break;
					overclocking = 1;
				}
			}
			DMC_7bit = 0;

			//deemph nonsense, kept for complicated reasons (see SetNESDeemph_OldHacky implementation)
			int maxref = 0;
			for (int x = 1, max = 0; x < 7; x++) {
				if (deempcnt[x] > max) {
					max = deempcnt[x];
					maxref = x;
				}
				deempcnt[x] = 0;
			}
			SetNESDeemph_OldHacky(maxref, 0);
		}
	}	//else... to if(ppudead)

	return(1);
}

int (*PPU_MASTER)(int skip) = FCEUPPU_Loop;

static uint16 TempAddrT, RefreshAddrT;

void FCEUPPU_LoadState(int version) {
	TempAddr = TempAddrT;
	RefreshAddr = RefreshAddrT;
}

SFORMAT FCEUPPU_STATEINFO[] = {
	{ NTARAM, 0x800, "NTAR" },
	{ PALRAM, 0x20, "PRAM" },
	{ SPRAM, SPRAM_SIZE, "SPRA" },
	{ PPU, PPUR_SIZE, "PPUR" },
	{ &kook, 1, "KOOK" },
	{ &ppudead, 1, "DEAD" },
	{ &PPUSPL, 1, "PSPL" },
	{ &XOffset, 1, "XOFF" },
	{ &vtoggle, 1, "VTGL" },
	{ &RefreshAddrT, 2 | FCEUSTATE_RLSB, "RADD" },
	{ &TempAddrT, 2 | FCEUSTATE_RLSB, "TADD" },
	{ &VRAMBuffer, 1, "VBUF" },
	{ &PPUGenLatch, 1, "PGEN" },
	{ 0 }
};

void FCEUPPU_SaveState(void) {
	TempAddrT = TempAddr;
	RefreshAddrT = RefreshAddr;
}

uint32 FCEUPPU_PeekAddress()
{
	return RefreshAddr & 0x3FFF;
}
