/* FCE Ultra - NES/Famicom Emulator
 *
 * Copyright notice for this file:
 *  Copyright (C) 2007-2010 CaH4e3
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
 *
 * OneBus Derived System
 *
 * Known issues:
 * - Demo mode hangs
 * - CPU freq and scanline timings unknown
 * -
 * 
 */

#include "mapinc.h"

// General Purpose Registers
uint8 cpu410x[0x10], ppu20xx[0x80], apu40xx[0x40];

// IRQ Registers
static uint8 IRQCount, IRQa, IRQReload;
#define IRQLatch cpu410x[0x1]	// accc cccc, a = 0, AD12 switching, a = 1, HSYNC switching

// MMC3 Registers
static uint8 inv_hack = 0;		// some OneBus Systems have swapped PRG reg commans in MMC3 inplementation,
								// trying to autodetect unusual behavior, due not to add a new mapper.
#define mmc3cmd  cpu410x[0x5]	// pcv- ----, p - program swap, c - video swap, v - internal VRAM enable
#define mirror   cpu410x[0x6]	// ---- ---m, m = 0 - H, m = 1 - V

// APU Registers
static uint8 pcm_enable = 0, pcm_irq = 0;
static int16 pcm_addr, pcm_size, pcm_latch, pcm_clock = 0xE1;

static writefunc defapuwrite[64];
static readfunc defapuread[64];

static SFORMAT StateRegs[] =
{
	{ cpu410x, 16, "REGC" },
	{ ppu20xx, 128, "REGS" },
	{ apu40xx, 64, "REGA" },
	{ &IRQReload, 1, "IRQR" },
	{ &IRQCount, 1, "IRQC" },
	{ &IRQa, 1, "IRQA" },
	{ &pcm_enable, 1, "PCME" },
	{ &pcm_irq, 1, "PCMI" },
	{ &pcm_addr, 2, "PCMA" },
	{ &pcm_size, 2, "PCMS" },
	{ &pcm_latch, 2, "PCML" },
	{ &pcm_clock, 2, "PCMC" },
	{ 0 }
};

static void PSync(void) {
	uint8 bankmode = cpu410x[0xb] & 7;
	uint8 mask = (bankmode == 0x7) ? (0xff) : (0x3f >> bankmode);
	uint32 block = ((cpu410x[0x0] & 0xf0) << 4) + (cpu410x[0xa] & (~mask));
	uint32 pswap = (mmc3cmd & 0x40) << 8;

	uint8 bank0 = cpu410x[0x7 ^ inv_hack];
	uint8 bank1 = cpu410x[0x8 ^ inv_hack];
	uint8 bank2 = (cpu410x[0xb] & 0x40) ? (cpu410x[0x9]) : (~1);
	uint8 bank3 = ~0;

	setprg8(0x8000 ^ pswap, block | (bank0 & mask));
	setprg8(0xa000, block | (bank1 & mask));
	setprg8(0xc000 ^ pswap, block | (bank2 & mask));
	setprg8(0xe000, block | (bank3 & mask));
}

static void CSync(void) {

	setchr8(ppu20xx[0x20]);

//	setchr8(0x0000, ppu20xx[0x20] << 3);
//	setchr1(0x1000, ppu20xx[0x22] << 3);

//	setchr2(0x0000, ppu20xx[0x20]);
//	setchr2(0x0800, ppu20xx[0x21]);
//	setchr2(0x1000, ppu20xx[0x22]);
//	setchr2(0x1800, ppu20xx[0x23]);

	setmirror((mirror ^ 1) & 1);
}

static void Sync(void) {
	PSync();
	CSync();
}

static DECLFW(UNLOneBusWriteCPU410X) {
//	FCEU_printf("CPU %04x:%04x\n",A,V);
	switch (A & 0xf) {
	case 0x1: IRQLatch = V & 0xfe; break;	// не по даташиту
	case 0x2: IRQReload = 1; break;
	case 0x3: X6502_IRQEnd(FCEU_IQEXT); IRQa = 0; break;
	case 0x4: IRQa = 1; break;
	default:
		cpu410x[A & 0xf] = V;
		Sync();
	}
}

static DECLFW(UNLOneBusWritePPU20XX) {
//	FCEU_printf("PPU %04x:%04x\n",A,V);
	ppu20xx[A - 0x2010 + 0x10] = V;
	Sync();
}

//static DECLFW(UNLOneBusWriteMMC3) {
//	FCEU_printf("MMC %04x:%04x\n",A,V);
//	switch (A & 0xe001) {
//	case 0x8000: mmc3cmd = (mmc3cmd & 0x38) | (V & 0xc7); Sync(); break;
//	case 0x8001:
//	{
//		switch (mmc3cmd & 7) {
//		case 0: ppu20xx[0x16] = V; CSync(); break;
//		case 1: ppu20xx[0x17] = V; CSync(); break;
//		case 2: ppu20xx[0x12] = V; CSync(); break;
//		case 3: ppu20xx[0x13] = V; CSync(); break;
//		case 4: ppu20xx[0x14] = V; CSync(); break;
//		case 5: ppu20xx[0x15] = V; CSync(); break;
//		case 6: cpu410x[0x17] = V; PSync(); break;
//		case 7: cpu410x[0x18] = V; PSync(); break;
//		}
//		break;
//	}
//	case 0xa000: mirror = V; CSync(); break;
//	case 0xc000: IRQLatch = V & 0xfe; break;
//	case 0xc001: IRQReload = 1; break;
//	case 0xe000: X6502_IRQEnd(FCEU_IQEXT); IRQa = 0; break;
//	case 0xe001: IRQa = 1; break;
//	}
//}

static void UNLOneBusIRQHook(void) {
	uint32 count = IRQCount;
	if (!count || IRQReload) {
		IRQCount = IRQLatch;
		IRQReload = 0;
	} else
		IRQCount--;
	if (count && !IRQCount) {
		if (IRQa)
			X6502_IRQBegin(FCEU_IQEXT);
	}
}

static DECLFW(UNLOneBusWriteUnk) {
//	FCEU_printf("w.%04x:%02x\n",A,V);
}
	
static DECLFR(UNLOneBusReadUnk) {
//	FCEU_printf("r.%04x\n", A);
	return 0xff;
}

#define I2C_4148		0
#define I2C_414A		2
#define I2C_414B		3

#define I2C_CLK_MASK 0x20
#define I2C_DAT_MASK 0x10
#define I2C_RW_MASK	 0x10

#define MODE_READ		0
#define MODE_WRITE		1

#define PIN_LO			0
#define PIN_HI			1
#define PIN_RISE		2
#define PIN_FALL		3

#define STATE_IDLE		0
#define STATE_ADDR		1
#define STATE_DATA		2
#define STATE_WAIT		3
#define STATE_DONE		4

static u8 i2c[4];
static u8 i2c_cl_prev, i2c_cl_pin, i2c_cl_pin_prev;
static u8 i2c_da_prev, i2c_da_pin;
static u8 i2c_bit, i2c_byte;
static u8 i2c_state, i2c_addr, i2c_out;

// Don't know what exactly this device is, but it looks as some kind of simplified i2c controller or something
// it used to hardware protection check at start and furthrer sometimes in the game.
// very similar to regular i2c eeprom, but without address selection, separate R/W control lines, etc..
// also, they uses only single byte address internally, so this may be simplified in code just by 
// keeping one single byte written instead of 256.

static DECLFW(UNLOneBusWriteI2C_PROT) {
	i2c[A - 0x4148] = V;
	u8 i2c_rw_mode = (i2c[I2C_4148] & I2C_RW_MASK) / I2C_RW_MASK;

	u8 i2c_cl_cur = (i2c[I2C_414A] & I2C_CLK_MASK) / I2C_CLK_MASK;
	if (i2c_cl_cur && !i2c_cl_prev)
		i2c_cl_pin = PIN_RISE;
	else if (!i2c_cl_cur && i2c_cl_prev)
		i2c_cl_pin = PIN_FALL;
	else
		i2c_cl_pin = i2c_cl_cur;
	i2c_cl_prev = i2c_cl_cur;

	u8 i2c_da_cur = (i2c[I2C_414A] & I2C_DAT_MASK) / I2C_DAT_MASK;
	if (i2c_da_cur && !i2c_da_prev)
		i2c_da_pin = PIN_RISE;
	else if (!i2c_da_cur && i2c_da_prev)
		i2c_da_pin = PIN_FALL;
	else
		i2c_da_pin = i2c_da_cur;
	i2c_da_prev = i2c_da_cur;

//	FCEU_printf("w.%04x->%02x (%04x)\n", A, V, X.PC);

	if (i2c_rw_mode == MODE_WRITE) {
		if ((i2c_state == STATE_IDLE) && (i2c_cl_pin == PIN_HI) && (i2c_da_pin == PIN_FALL)) {	// START
//			FCEU_printf("w start\n");
			i2c_state = STATE_ADDR;
			i2c_bit = 0;
			i2c_byte = 0;
		}
		else if ((i2c_state == STATE_WAIT) && (i2c_cl_pin == PIN_HI) && (i2c_da_pin == PIN_RISE)) {	// STOP
//			FCEU_printf("w stop\n");
			i2c_state = STATE_DONE;
			i2c_bit = 0;
			i2c_byte = 0;
		}
		else if ((i2c_cl_pin == PIN_FALL) && (i2c_cl_pin_prev == PIN_RISE)) {	// BIT WRITE
			if (i2c_bit < 8) {	// SEND DATA
//				FCEU_printf("w.b %d\n", i2c_da_cur);
				i2c_byte <<= 1;
				i2c_byte |= i2c_da_cur;
				i2c_bit++;
			} else {			// ACK, apply data
//				FCEU_printf("ack ");
				if (i2c_state == STATE_ADDR) {
//					FCEU_printf("w.a %02x\n", i2c_byte);
					i2c_addr = i2c_byte;
					i2c_state = STATE_DATA;
					i2c_bit = 0;
					i2c_byte = 0;
				} else if (i2c_state == STATE_DATA) {
// PROT HACK! read out value is not the same but somehow morphed.
					i2c_out = ((((0 - i2c_byte) & 0x0F) << 4) | (((0 - i2c_byte) & 0xF0) >> 4));
					FCEU_printf("w.d %02x (out = %02x)\n", i2c_byte, i2c_out);
					i2c_state = STATE_WAIT;
					i2c_bit = 0;
					i2c_byte = 0;
				}
			}
		}
	} else if (i2c_rw_mode == MODE_READ) {
	    if ((i2c_state == STATE_DONE) && (i2c_cl_pin == PIN_HI) && (i2c_da_pin == PIN_RISE)) {	// STOP
//			FCEU_printf("r stop\n");
			i2c_state = STATE_IDLE;
			i2c_bit = 0;
			i2c_byte = 0;	
		}
	}

//	FCEU_printf("mode = %d state = %d cl = %d, da = %d bit = %d byte = %02x\n", i2c_rw_mode, i2c_state, i2c_cl_pin, i2c_da_pin, i2c_bit, i2c_byte);

	i2c_cl_pin_prev = i2c_cl_pin;
}

static DECLFR(UNLOneBusReadI2C_PROT) {
//	FCEU_printf("r.%04x (%04x)\n", A, X.PC);
	u8 ret;
	if ((i2c_state == STATE_DONE) && (i2c_cl_pin == PIN_RISE) && (A == 0x414B)) {
		if (i2c_bit < 8) {
			ret = ((i2c_out >> (7 - i2c_bit)) & 1) << 4;
			i2c_bit++;
		} else {
			ret = 0;
			i2c_bit = 0;
		}
//		FCEU_printf("r.%d\n", ret);
		return ret;
	} else
		return i2c[A - 0x4148];
}

// this seems to be one of registers for I2C audio player
// there are also some controls (probably for audio volume change on pin 6
// also they put the demo switch there as well it seems on pin 4,
// pin4 = 0 - demo game, 1 - regular game
// pin6 = 1 - volume control release, 0 - volume control pressed
static DECLFR(UNLOneBusRead4153) {
	return 0x10 | 0x40;
}

// somehow this is appears to affect NMI. mostly upon writing 0 to the SPI circuit
// (they forcibly waiting for nmi then)... not sure if this cause a NMI irq all the time when writing,
// but let's see.
static DECLFW(UNLOneBusWrite4153) {
	if (V & 1)
		TriggerNMI();
}

static DECLFW(UNLOneBusWriteAPU40XX) {
	int x;
	//	if(((A & 0x3f)!=0x16) && ((apu40xx[0x30] & 0x10) || ((A & 0x3f)>0x17)))FCEU_printf("APU %04x:%04x\n",A,V);
	apu40xx[A & 0x3f] = V;
	switch (A & 0x3f) {
	case 0x12:
		if (apu40xx[0x30] & 0x10) {
			pcm_addr = V << 6;
		}
		break;
	case 0x13:
		if (apu40xx[0x30] & 0x10) {
			pcm_size = (V << 4) + 1;
		}
		break;
	case 0x14:	// DMA hookup, Oregon hardware uses more DMA destinations like it does some other OneBus systems
		for (x = 0; x < 256; x++) {
			uint32 t = V << 8;
			if (apu40xx[0x34])	// destination control register, 0 is regular sprite DMA, 1 is PPU dma.
				X6502_DMW(0x2007, X6502_DMR(t + x));
			else
				X6502_DMW(0x2004, X6502_DMR(t + x));
		}
		break;
	case 0x15:
		if (apu40xx[0x30] & 0x10) {
			pcm_enable = V & 0x10;
			if (pcm_irq) {
				X6502_IRQEnd(FCEU_IQEXT);
				pcm_irq = 0;
			}
			if (pcm_enable)
				pcm_latch = pcm_clock;
			V &= 0xef;
		}
		break;
	}
	defapuwrite[A & 0x3f](A, V);
}

static DECLFR(UNLOneBusReadAPU40XX) {
	uint8 result = defapuread[A & 0x3f](A);
//	FCEU_printf("read %04x, %02x\n",A,result);
	switch (A & 0x3f) {
	case 0x15:
		if (apu40xx[0x30] & 0x10) {
			result = (result & 0x7f) | pcm_irq;
		}
		break;
	}
	return result;
}

static void UNLOneBusCpuHook(int a) {
	if (pcm_enable) {
		pcm_latch -= a;
		if (pcm_latch <= 0) {
			pcm_latch += pcm_clock;
			pcm_size--;
			if (pcm_size < 0) {
				pcm_irq = 0x80;
				pcm_enable = 0;
				X6502_IRQBegin(FCEU_IQEXT);
			} else {
				uint16 addr = pcm_addr | ((apu40xx[0x30]^3) << 14);
				uint8 raw_pcm = ARead[addr](addr) >> 1;
				defapuwrite[0x11](0x4011, raw_pcm);
				pcm_addr++;
				pcm_addr &= 0x7FFF;
			}
		}
	}
}

static void UNLOneBusPower(void) {
	uint32 i;
	IRQReload = IRQCount = IRQa = 0;

	memset(cpu410x, 0x00, sizeof(cpu410x));
	memset(ppu20xx, 0x00, sizeof(ppu20xx));
	memset(apu40xx, 0x00, sizeof(apu40xx));

	SetupCartCHRMapping(0, PRGptr[0], PRGsize[0], 0);

	for (i = 0; i < 64; i++) {
		defapuread[i] = GetReadHandler(0x4000 | i);
		defapuwrite[i] = GetWriteHandler(0x4000 | i);
	}

	SetReadHandler(0x4000, 0x403f, UNLOneBusReadAPU40XX);
	SetWriteHandler(0x4000, 0x403f, UNLOneBusWriteAPU40XX);

	i2c[0] = i2c[1] = i2c[2] = i2c[3] = 0;
	i2c_cl_prev = i2c_cl_pin = i2c_cl_pin_prev = 0;
	i2c_da_prev = i2c_da_pin = 0;
	i2c_state = STATE_IDLE;

	SetWriteHandler(0x4110, 0x41ff, UNLOneBusWriteUnk);
	SetReadHandler(0x4110, 0x41ff, UNLOneBusReadUnk);

	SetWriteHandler(0x4148, 0x414B, UNLOneBusWriteI2C_PROT);
	SetReadHandler(0x4148, 0x414B, UNLOneBusReadI2C_PROT);
	
	SetWriteHandler(0x4153, 0x4153, UNLOneBusWrite4153);
	SetReadHandler(0x4153, 0x4153, UNLOneBusRead4153);

	SetReadHandler(0x8000, 0xFFFF, CartBR);
	SetWriteHandler(0x2010, 0x205f, UNLOneBusWritePPU20XX);
	SetWriteHandler(0x4100, 0x410f, UNLOneBusWriteCPU410X);
//	SetWriteHandler(0x8000, 0xffff, UNLOneBusWriteMMC3);

	Sync();
}

static void UNLOneBusReset(void) {
	IRQReload = IRQCount = IRQa = 0;

	i2c[0] = i2c[1] = i2c[2] = i2c[3] = 0;
	i2c_cl_prev = i2c_cl_pin = i2c_cl_pin_prev = 0;
	i2c_da_prev = i2c_da_pin = 0;
	i2c_state = STATE_IDLE;

	memset(cpu410x, 0x00, sizeof(cpu410x));
	memset(ppu20xx, 0x00, sizeof(ppu20xx));
	memset(apu40xx, 0x00, sizeof(apu40xx));

	Sync();
}

static void StateRestore(int version) {
	Sync();
}

void UNLOneBus_Init(CartInfo *info) {
	info->Power = UNLOneBusPower;
	info->Reset = UNLOneBusReset;

	GameHBIRQHook = UNLOneBusIRQHook;
	MapIRQHook = UNLOneBusCpuHook;
	GameStateRestore = StateRestore;
	AddExState(&StateRegs, ~0, 0, 0);
}
