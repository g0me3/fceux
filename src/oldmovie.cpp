#include "version.h"
#include "types.h"
#include "fceu.h"
#include "driver.h"
#include "netplay.h"

#include "oldmovie.h"
#include "movie.h"
#include "utils/xstring.h"

#include <fstream>

using namespace std;

#define MOVIE_MAGIC             0x1a4d4346

// still at 2 since the format itself is still compatible
// to detect which version the movie was made with, check the fceu version stored in the movie header
// (e.g against FCEU_VERSION_NUMERIC)
#define MOVIE_VERSION           2

static uint8 joop[4];
static uint8 joopcmd;
static uint32 framets = 0;
static uint32 frameptr = 0;
static uint8* moviedata = NULL;
static uint32 moviedatasize = 0;
static uint32 firstframeoffset = 0;
static uint32 savestate_offset = 0;

//Cache variables used for playback.
static uint32 nextts = 0;
static int32 nextd = 0;

// turn old ucs2 metadata into utf8
void convert_metadata(char* metadata, int metadata_size, uint8* tmp, int metadata_length)
{
	char* ptr = metadata;
	char* ptr_end = metadata + metadata_size - 1;
	int c_ptr = 0;
	while (ptr < ptr_end && c_ptr < metadata_length)
	{
		uint16 c = (tmp[c_ptr << 1] | (tmp[(c_ptr << 1) + 1] << 8));
		//mbg merge 7/17/06 changed to if..elseif
		if (c <= 0x7f)
			*ptr++ = (char)(c & 0x7f);
		else if (c <= 0x7FF)
			if (ptr + 1 >= ptr_end)
				ptr_end = ptr;
			else
			{
				*ptr++ = (0xc0 | (c >> 6));
				*ptr++ = (0x80 | (c & 0x3f));
			}
		else
			if (ptr + 2 >= ptr_end)
				ptr_end = ptr;
			else
			{
				*ptr++ = (0xe0 | (c >> 12));
				*ptr++ = (0x80 | ((c >> 6) & 0x3f));
				*ptr++ = (0x80 | (c & 0x3f));
			}

		c_ptr++;
	}
	*ptr = '\0';
}

//backwards compat
static void FCEUI_LoadMovie_v1(char *fname, int _read_only);

#define MOVIE_V1_HEADER_SIZE	32

static int movie_readchar()
{
	if (frameptr >= moviedatasize)
	{
		return -1;
	}
	return (int)(moviedata[frameptr++]);
}


static void _addjoy()
{
	while (nextts == framets || nextd == -1)
	{
		int tmp, ti;
		uint8 d;

		if (nextd != -1)
		{
			if (nextd & 0x80)
			{
				int command = nextd & 0x1F;
				if (command == FCEUNPCMD_RESET)
					joopcmd = MOVIECMD_RESET;
				if (command == FCEUNPCMD_POWER)
					joopcmd = MOVIECMD_POWER;
			}
			else
				joop[(nextd >> 3) & 0x3] ^= 1 << (nextd & 0x7);
		}


		tmp = movie_readchar();
		d = tmp;

		if (tmp < 0)
		{
			return;
		}

		nextts = 0;
		tmp >>= 5;
		tmp &= 0x3;
		ti = 0;

		int tmpfix = tmp;
		while (tmp--) { nextts |= movie_readchar() << (ti * 8); ti++; }

		// This fixes a bug in movies recorded before version 0.98.11
		// It's probably not necessary, but it may keep away CRAZY PEOPLE who recorded
		// movies on <= 0.98.10 and don't work on playback.
		if (tmpfix == 1 && !nextts)
		{
			nextts |= movie_readchar() << 8;
		}
		else if (tmpfix == 2 && !nextts) { nextts |= movie_readchar() << 16; }

		if (nextd != -1)
			framets = 0;
		nextd = d;
	}

	framets++;
}


EFCM_CONVERTRESULT convert_fcm(MovieData& md, std::string fname)
{
	//convert EVEN OLDER movies to fcm
	//fname = (char*)convertToFCM(fname,buffer);

	uint32 framecount;
	uint32 rerecord_count;
	int movieConvertOffset1 = 0, movieConvertOffset2 = 0, movieSyncHackOn = 0;


	EMUFILE* fp = FCEUD_UTF8_fstream(fname, "rb");
	if (!fp) return FCM_CONVERTRESULT_FAILOPEN;

	// read header
	uint32 magic = 0;
	uint32 version;
	uint8 flags[4];

	read32le(&magic, fp);
	if (magic != MOVIE_MAGIC)
	{
		delete fp;
		return FCM_CONVERTRESULT_FAILOPEN;
	}

	read32le(&version, fp);
	if (version == 1)
	{
		// attempt to load previous version's format
		//TODO
		delete fp;
		return FCM_CONVERTRESULT_OLDVERSION;
	}
	else if (version == MOVIE_VERSION)
	{
	}
	else
	{
		// unsupported version
		delete fp;
		return FCM_CONVERTRESULT_UNSUPPORTEDVERSION;
	}


	fp->fread((char*)&flags, 4);
	read32le(&framecount, fp);
	read32le(&rerecord_count, fp);
	read32le(&moviedatasize, fp);
	read32le(&savestate_offset, fp);
	read32le(&firstframeoffset, fp);

	//read header values
	fp->fread((char*)&md.romChecksum, 16);
	read32le((uint32*)&md.emuVersion, fp);

	md.romFilename = readNullTerminatedAscii(fp);

	md.comments.push_back(L"author  " + mbstowcs(readNullTerminatedAscii(fp)));

	if (flags[0] & MOVIE_FLAG_NOSYNCHACK)
		movieSyncHackOn = 0;
	else
		movieSyncHackOn = 1;

	if (flags[0] & MOVIE_FLAG_PAL)
		md.palFlag = true;

	bool initreset = false;
	if (flags[0] & MOVIE_FLAG_FROM_POWERON)
	{
		//don't need to load a savestate
	}
	else if (flags[0] & MOVIE_FLAG_FROM_RESET)
	{
		initreset = true;
	}
	else
	{
		delete fp;
		return FCM_CONVERTRESULT_STARTFROMSAVESTATENOTSUPPORTED;
	}

	//analyze input types?
	//ResetInputTypes();

	fp->fseek(firstframeoffset, SEEK_SET);
	moviedata = (uint8*)realloc(moviedata, moviedatasize);
	fp->fread((char*)moviedata, moviedatasize);

	frameptr = 0;
	memset(joop, 0, sizeof(joop));
	framets = 0;
	nextts = 0;
	nextd = -1;

	//prepare output structure
	md.rerecordCount = rerecord_count;
	md.records.resize(framecount);
	md.guid.newGuid();

	//begin decoding.
	//joymask is used to look for any joystick that has input.
	//if joysticks 3 or 4 have input then we will enable fourscore
	uint8 joymask[4] = { 0,0,0,0 };
	for (uint32 i = 0; i < framecount; i++)
	{
		joopcmd = 0;
		if (i == 0 && initreset)
			joopcmd = MOVIECMD_RESET;
		_addjoy();
		md.records[i].commands = joopcmd;
		for (int j = 0; j < 4; j++) {
			joymask[j] |= joop[j];
			md.records[i].joysticks[j] = joop[j];
		}
	}

	md.ports[2] = SIS_NONE;
	if (joymask[2] || joymask[3])
	{
		md.fourscore = true;
		md.ports[0] = md.ports[1] = SI_NONE;
	}
	else
	{
		md.fourscore = false;
		md.ports[0] = md.ports[1] = SI_GAMEPAD;
	}

	free(moviedata);
	moviedata = 0;

	delete fp;
	return FCM_CONVERTRESULT_SUCCESS;
}
