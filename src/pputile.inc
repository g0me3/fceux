
uint8 *C;
//register uint8 cc;
uint32 vadr;
register uint8 zz;

if (X1 >= 2) {
	uint8 *S = PALRAM;
//	uint32 pixdata;

//	pixdata = ppulut1[(pshift[0] >> (8 - XOffset)) & 0xFF] | ppulut2[(pshift[1] >> (8 - XOffset)) & 0xFF];
//	pixdata |= ppulut3[XOffset | (atlatch << 3)];

//	P[0] = S[pixdata & 0xF];
//	pixdata >>= 4;
//	P[1] = S[pixdata & 0xF];
//	pixdata >>= 4;
//	P[2] = S[pixdata & 0xF];
//	pixdata >>= 4;
//	P[3] = S[pixdata & 0xF];
//	pixdata >>= 4;
//	P[4] = S[pixdata & 0xF];
//	pixdata >>= 4;
//	P[5] = S[pixdata & 0xF];
//	pixdata >>= 4;
//	P[6] = S[pixdata & 0xF];
//	pixdata >>= 4;
//	P[7] = S[pixdata & 0xF];

	P[0] = S[pshift[0] & 0xF];
	P[1] = S[(pshift[0] >> 4) & 0xF];
	P[2] = S[pshift[1] & 0xF];
	P[3] = S[(pshift[1] >> 4) & 0xF];
	P[4] = S[pshift[2] & 0xF];
	P[5] = S[(pshift[2] >> 4) & 0xF];
	P[6] = S[pshift[3] & 0xF];
	P[7] = S[(pshift[3] >> 4) & 0xF];

	P += 8;
}

	zz = RefreshAddr & 0x1F;
	C = vnapage[(RefreshAddr >> 10) & 3];
	vadr = (C[RefreshAddr & 0x3ff] << 5) + (vofs << 1);				// Fetch name table byte.

#ifdef PPUT_HOOK
	PPU_hook(0x2000 | (RefreshAddr & 0xfff));
#endif

//	cc = C[0x3c0 + (zz >> 2) + ((RefreshAddr & 0x380) >> 4)];	// Fetch attribute table byte.
//	cc = ((cc >> ((zz & 2) + ((RefreshAddr & 0x40) >> 4))) & 3);

//	atlatch >>= 2;
//	atlatch |= cc << 2;

	pshift[0] <<= 8;
	pshift[1] <<= 8;

	C = VRAMADR(vadr);

#ifdef PPUT_HOOK
	PPU_hook(vadr);
#endif

	pshift[0] |= C[0];
	pshift[1] |= C[1];
	pshift[2] |= C[2];
	pshift[3] |= C[3];

if ((RefreshAddr & 0x1f) == 0x1f)
	RefreshAddr ^= 0x41F;
else
	RefreshAddr++;

#ifdef PPUT_HOOK
	PPU_hook(0x2000 | (RefreshAddr & 0xfff));
#endif

