/* FCE Ultra - NES/Famicom Emulator
 *
 * Copyright notice for this file:
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

/* None of this code should use any of the iNES bank switching wrappers. */

#include "mapinc.h"

#include <array>

#define ABANKS MMC5SPRVPage
#define BBANKS MMC5BGVPage
#define SpriteON    (PPU[1] & 0x10)	//Show Sprite
#define ScreenON    (PPU[1] & 0x08)	//Show screen
#define PPUON       (PPU[1] & 0x18)	//PPU should operate
#define Sprite16    (PPU[0] & 0x20)	//Sprites 8x16/8x8

static void (*sfun)(int P);
static void (*psfun)(void);

void MMC5RunSound(int Count);
void MMC5RunSoundHQ(void);

static INLINE void MMC5SPRVROM_BANK1(uint32 A, uint32 V) {
	if (CHRptr[0]) {
		V &= CHRmask1[0];
		MMC5SPRVPage[(A) >> 10] = &CHRptr[0][(V) << 10] - (A);
	}
}

static INLINE void MMC5BGVROM_BANK1(uint32 A, uint32 V) {
	if (CHRptr[0]) {
		V &= CHRmask1[0]; MMC5BGVPage[(A) >> 10] = &CHRptr[0][(V) << 10] - (A);
	}
}

static INLINE void MMC5SPRVROM_BANK2(uint32 A, uint32 V) {
	if (CHRptr[0]) {
		V &= CHRmask2[0]; MMC5SPRVPage[(A) >> 10] = MMC5SPRVPage[((A) >> 10) + 1] = &CHRptr[0][(V) << 11] - (A);
	}
}
static INLINE void MMC5BGVROM_BANK2(uint32 A, uint32 V) {
	if (CHRptr[0]) {
		V &= CHRmask2[0]; MMC5BGVPage[(A) >> 10] = MMC5BGVPage[((A) >> 10) + 1] = &CHRptr[0][(V) << 11] - (A);
	}
}

static INLINE void MMC5SPRVROM_BANK4(uint32 A, uint32 V) {
	if (CHRptr[0]) {
		V &= CHRmask4[0]; MMC5SPRVPage[(A) >> 10] = MMC5SPRVPage[((A) >> 10) + 1] = MMC5SPRVPage[((A) >> 10) + 2] = MMC5SPRVPage[((A) >> 10) + 3] = &CHRptr[0][(V) << 12] - (A);
	}
}
static INLINE void MMC5BGVROM_BANK4(uint32 A, uint32 V) {
	if (CHRptr[0]) {
		V &= CHRmask4[0]; MMC5BGVPage[(A) >> 10] = MMC5BGVPage[((A) >> 10) + 1] = MMC5BGVPage[((A) >> 10) + 2] = MMC5BGVPage[((A) >> 10) + 3] = &CHRptr[0][(V) << 12] - (A);
	}
}

static INLINE void MMC5SPRVROM_BANK8(uint32 V) {
	if (CHRptr[0]) {
		V &= CHRmask8[0]; MMC5SPRVPage[0] = MMC5SPRVPage[1] = MMC5SPRVPage[2] = MMC5SPRVPage[3] = MMC5SPRVPage[4] = MMC5SPRVPage[5] = MMC5SPRVPage[6] = MMC5SPRVPage[7] = &CHRptr[0][(V) << 13];
	}
}
static INLINE void MMC5BGVROM_BANK8(uint32 V) {
	if (CHRptr[0]) {
		V &= CHRmask8[0]; MMC5BGVPage[0] = MMC5BGVPage[1] = MMC5BGVPage[2] = MMC5BGVPage[3] = MMC5BGVPage[4] = MMC5BGVPage[5] = MMC5BGVPage[6] = MMC5BGVPage[7] = &CHRptr[0][(V) << 13];
	}
}

static std::array<uint8,4> PRGBanks;
static uint8 WRAMPage;
static std::array<uint16,8> CHRBanksA;
static std::array<uint16,4> CHRBanksB;
static std::array<uint8,2> WRAMMaskEnable;
uint8 mmc5ABMode;                /* A=0, B=1 */

static uint8 IRQScanline, IRQEnable;
static uint8 CHRMode, NTAMirroring, NTFill, ATFill;

static uint8 MMC5IRQR;
static uint8 MMC5LineCounter;
static uint8 mmc5psize, mmc5vsize;
static std::array<uint8,2> mul;

static uint32 WRAMSIZE = 0;
static uint8 *WRAM = NULL;
static uint8 *MMC5fill = NULL;
static uint8 *ExRAM = NULL;
static uint8 MMC5battery = 0;

const int MMC5WRAMMAX = 1<<7; // 7 bits in register interface (real MMC5 has only 4 pins, however)
static uint8 MMC5WRAMsize=0; //configuration, not state
static uint8 MMC5WRAMIndex[MMC5WRAMMAX]; //configuration, not state

static std::array<uint8,4> MMC5ROMWrProtect;
static std::array<uint8,5> MMC5MemIn;

static void MMC5CHRA(void);
static void MMC5CHRB(void);

typedef struct __cartdata {
	uint32 crc32;
	uint8 size;
} cartdata;

#define MMC5SPRVRAMADR(V)   &MMC5SPRVPage[(V) >> 10][(V)]

uint8* MMC5BGVRAMADR(uint32 A)
{
	if(newppu)
	{
		if(Sprite16)
		{
			bool isPattern = PPUON != 0;
			if (ppuphase == PPUPHASE_OBJ && isPattern)
				return &ABANKS[(A) >> 10][(A)];
			if (ppuphase == PPUPHASE_BG && isPattern)
				return &BBANKS[(A) >> 10][(A)];
			else if(mmc5ABMode == 0)
				return &ABANKS[(A) >> 10][(A)];
			else 
				return &BBANKS[(A) >> 10][(A)];
		}
		else return &ABANKS[(A) >> 10][(A)];;
	}

	if (!Sprite16) {
		if (mmc5ABMode == 0)
			return &ABANKS[(A) >> 10][(A)];
		else
			return &BBANKS[(A) >> 10][(A)];
	} else return &BBANKS[(A) >> 10][(A)];
}

static void mmc5_PPUWrite(uint32 A, uint8 V) {
	uint32 tmp = A;
	extern uint8 PALRAM[0x20];
	extern uint8 UPALRAM[0x03];

	if (tmp >= 0x3F00) {
		if (!(tmp & 3)) {
			if (!(tmp & 0xC)) {
				PALRAM[0x00] = PALRAM[0x04] = PALRAM[0x08] = PALRAM[0x0C] = V & 0x3F;
				PALRAM[0x10] = PALRAM[0x14] = PALRAM[0x18] = PALRAM[0x1C] = V & 0x3F;
			}
			else
				UPALRAM[((tmp & 0xC) >> 2) - 1] = V & 0x3F;
		} else
			PALRAM[tmp & 0x1F] = V & 0x3F;
	} else if (tmp < 0x2000) {
		if (PPUCHRRAM & (1 << (tmp >> 10)))
			VPage[tmp >> 10][tmp] = V;
	} else {
		if (PPUNTARAM & (1 << ((tmp & 0xF00) >> 10)))
			vnapage[((tmp & 0xF00) >> 10)][tmp & 0x3FF] = V;
	}
}

extern uint32 NTRefreshAddr;
uint8 FASTCALL mmc5_PPURead(uint32 A)
{
	bool split = false;
	if(newppu)
	{
		if((MMC5HackSPMode&0x80) && !(MMC5HackCHRMode&2))
		{
			int target = MMC5HackSPMode&0x1f;
			int side = MMC5HackSPMode&0x40;
			int ht = NTRefreshAddr&31;

			if(side==0)
			{
				if(ht<target) split = true;
			}
			else
			{
				if(ht>=target) split = true;
			}
		}
	}

	if (A < 0x2000)
	{
		if(Sprite16)
		{
			bool isPattern = !!PPUON;
			if (ppuphase == PPUPHASE_OBJ && isPattern)
				return ABANKS[(A) >> 10][(A)];
			if (ppuphase == PPUPHASE_BG && isPattern)
			{
				if(split)
					return MMC5HackVROMPTR[MMC5HackSPPage*0x1000 + (A&0xFFF)];

				//uhhh call through to this more sophisticated function, only if it's really needed?
				//we should probably reuse it completely, if we can
				if (MMC5HackCHRMode == 1)
					return *FCEUPPU_GetCHR(A,NTRefreshAddr);

				return BBANKS[(A) >> 10][(A)];
			}
			else if(mmc5ABMode == 0)
				return ABANKS[(A) >> 10][(A)];
			else 
				return BBANKS[(A) >> 10][(A)];
		}
		else 
		{
			if (ppuphase == PPUPHASE_BG && ScreenON)
			{
				if(split)
					return MMC5HackVROMPTR[MMC5HackSPPage*0x1000 + (A&0xFFF)];

				//uhhh call through to this more sophisticated function, only if it's really needed?
				//we should probably reuse it completely, if we can
				if (MMC5HackCHRMode == 1)
					return *FCEUPPU_GetCHR(A,NTRefreshAddr);
			}

			return ABANKS[(A) >> 10][(A)];
		}
	}
	else
	{
		if(split)
		{
			static const int kHack = -1; //dunno if theres science to this or if it just fixes SDF (cant be bothered to think about it)
			int linetile = (newppu_get_scanline()+kHack)/8 + MMC5HackSPScroll;

			//REF NT: return 0x2000 | (v << 0xB) | (h << 0xA) | (vt << 5) | ht;
			//REF AT: return 0x2000 | (v << 0xB) | (h << 0xA) | 0x3C0 | ((vt & 0x1C) << 1) | ((ht & 0x1C) >> 2);

			if((A&0x3FF)>=0x3C0)
			{
				A &= ~(0x1C<<1); //mask off VT
				A |= (linetile&0x1C)<<1; //mask on adjusted VT
				return ExRAM[A & 0x3FF];
			}
			else
			{
				A &= ~((0x1F<<5) | (1<<0xB)); //mask off VT and V
				A |= (linetile&31)<<5; //mask on adjusted VT (V doesnt make any sense, I think)
				return ExRAM[A & 0x3FF];
			}
		}
		
		if (MMC5HackCHRMode == 1)
		{
			if((A&0x3FF)>=0x3C0)
			{
				uint8 byte = ExRAM[NTRefreshAddr & 0x3ff];
				//get attribute part and paste it 4x across the byte
				byte >>= 6;
				byte *= 0x55;
				return byte;
			} 
		}
			
		return vnapage[(A >> 10) & 0x3][A & 0x3FF];
	}
}

cartdata MMC5CartList[] =
{
	{ 0x6f4e4312, 4 }, /* Aoki Ookami to Shiroki Mejika - Genchou Hishi */
	{ 0x15fe6d0f, 2 }, /* Bandit Kings of Ancient China */
	{ 0x671f23a8, 0 }, /* Castlevania III - Dracula's Curse (E) */
	{ 0xcd4e7430, 0 }, /* Castlevania III - Dracula's Curse (KC) */
	{ 0xed2465be, 0 }, /* Castlevania III - Dracula's Curse (U) */
	{ 0xfe3488d1, 2 }, /* Daikoukai Jidai */
	{ 0x0ec6c023, 1 }, /* Gemfire */
	{ 0x0afb395e, 0 }, /* Gun Sight */
	{ 0x1ced086f, 2 }, /* Ishin no Arashi */
	{ 0x9cbadc25, 1 }, /* Just Breed */
	{ 0x6396b988, 2 }, /* L'Empereur (J) */
	{ 0x9c18762b, 2 }, /* L'Empereur (U) */
	{ 0xb0480ae9, 0 }, /* Laser Invasion */
	{ 0xb4735fac, 0 }, /* Metal Slader Glory */
	{ 0xf540677b, 4 }, /* Nobunaga no Yabou - Bushou Fuuun Roku */
	{ 0xeee9a682, 2 }, /* Nobunaga no Yabou - Sengoku Gunyuu Den (J) (PRG0) */
	{ 0xf9b4240f, 2 }, /* Nobunaga no Yabou - Sengoku Gunyuu Den (J) (PRG1) */
	{ 0x8ce478db, 2 }, /* Nobunaga's Ambition 2 */
	{ 0xf011e490, 4 }, /* Romance of The Three Kingdoms II */
	{ 0xbc80fb52, 1 }, /* Royal Blood */
	{ 0x184c2124, 4 }, /* Sangokushi II (J) (PRG0) */
	{ 0xee8e6553, 4 }, /* Sangokushi II (J) (PRG1) */
	{ 0xd532e98f, 1 }, /* Shin 4 Nin Uchi Mahjong - Yakuman Tengoku */
	{ 0x39f2ce4b, 2 }, /* Suikoden - Tenmei no Chikai */
	{ 0xbb7f829a, 0 }, /* Uchuu Keibitai SDF */
	{ 0xaca15643, 2 }, /* Uncharted Waters */
};

#define MMC5_NOCARTS (sizeof(MMC5CartList) / sizeof(MMC5CartList[0]))
int DetectMMC5WRAMSize(uint32 crc32) {
	for (size_t x = 0; x < MMC5_NOCARTS; x++) {
		if (crc32 == MMC5CartList[x].crc32) {
			if(MMC5CartList[x].size > 1)
				FCEU_printf(" >8KB external WRAM present.  Use UNIF if you hack the ROM image.\n");
			return(MMC5CartList[x].size * 8);
		}
	}
	return 64;
}

static void BuildWRAMSizeTable(void) {
	bool other = false; // non-standard configuration
	// fill first 8 entries
	int x;
	for (x = 0; x < 8; x++) {
		switch (MMC5WRAMsize) {
		case 0: MMC5WRAMIndex[x] = 255; break;                      //X,X,X,X,X,X,X,X
		case 1: MMC5WRAMIndex[x] = (x > 3) ? 255 : 0; break;        //0,0,0,0,X,X,X,X
		case 2: MMC5WRAMIndex[x] = (x & 4) >> 2; break;             //0,0,0,0,1,1,1,1
		case 4: MMC5WRAMIndex[x] = (x > 3) ? 255 : (x & 3); break;  //0,1,2,3,X,X,X,X
		case 8: MMC5WRAMIndex[x] = x; break;                        //0,1,2,3,4,5,6,7
		default: MMC5WRAMIndex[x] = x; other = true; break;         //0,1,2...
		}
	}
	// extend to fill complete table
	if (other)
	{
		for (x = 0; x < MMC5WRAMMAX && x < MMC5WRAMsize; ++x) MMC5WRAMIndex[x] = x; // linear mapping
		for (x = MMC5WRAMsize; x < MMC5WRAMMAX; ++x) MMC5WRAMIndex[x] = MMC5WRAMIndex[x-MMC5WRAMsize]; // repeat to fill table
		// theoretically the table fill should decompose into powers of two for possible mismatched SRAM combos,
		// but I don't want to complicate the code with unnecessary hypotheticals
	}
	else
	{
		for (x = 8; x < MMC5WRAMMAX; ++x) MMC5WRAMIndex[x] = MMC5WRAMIndex[x & 7]; // fill table, repeating groups of 8
	}
}

static void MMC5CHRA(void) {
	int x;
	switch (mmc5vsize & 3) {
	case 0:
		setchr8(CHRBanksA[7]);
		MMC5SPRVROM_BANK8(CHRBanksA[7]);
		break;
	case 1:
		setchr4(0x0000, CHRBanksA[3]);
		setchr4(0x1000, CHRBanksA[7]);
		MMC5SPRVROM_BANK4(0x0000, CHRBanksA[3]);
		MMC5SPRVROM_BANK4(0x1000, CHRBanksA[7]);
		break;
	case 2:
		setchr2(0x0000, CHRBanksA[1]);
		setchr2(0x0800, CHRBanksA[3]);
		setchr2(0x1000, CHRBanksA[5]);
		setchr2(0x1800, CHRBanksA[7]);
		MMC5SPRVROM_BANK2(0x0000, CHRBanksA[1]);
		MMC5SPRVROM_BANK2(0x0800, CHRBanksA[3]);
		MMC5SPRVROM_BANK2(0x1000, CHRBanksA[5]);
		MMC5SPRVROM_BANK2(0x1800, CHRBanksA[7]);
		break;
	case 3:
		for (x = 0; x < 8; x++) {
			setchr1(x << 10, CHRBanksA[x]);
			MMC5SPRVROM_BANK1(x << 10, CHRBanksA[x]);
	}
		break;
	}
}

static void MMC5CHRB(void) {
	int x;
	switch (mmc5vsize & 3) {
	case 0:
		setchr8(CHRBanksB[3]);
		MMC5BGVROM_BANK8(CHRBanksB[3]);
		break;
	case 1:
		setchr4(0x0000, CHRBanksB[3]);
		setchr4(0x1000, CHRBanksB[3]);
		MMC5BGVROM_BANK4(0x0000, CHRBanksB[3]);
		MMC5BGVROM_BANK4(0x1000, CHRBanksB[3]);
		break;
	case 2:
		setchr2(0x0000, CHRBanksB[1]);
		setchr2(0x0800, CHRBanksB[3]);
		setchr2(0x1000, CHRBanksB[1]);
		setchr2(0x1800, CHRBanksB[3]);
		MMC5BGVROM_BANK2(0x0000, CHRBanksB[1]);
		MMC5BGVROM_BANK2(0x0800, CHRBanksB[3]);
		MMC5BGVROM_BANK2(0x1000, CHRBanksB[1]);
		MMC5BGVROM_BANK2(0x1800, CHRBanksB[3]);
		break;
	case 3:
		for (x = 0; x < 8; x++) {
			setchr1(x << 10, CHRBanksB[x & 3]);
			MMC5BGVROM_BANK1(x << 10, CHRBanksB[x & 3]);
	}
		break;
	}
}

static void MMC5WRAM(uint32 A, uint32 V) {
	V = MMC5WRAMIndex[V & (MMC5WRAMMAX-1)];
	if (V != 255) {
		setprg8r(0x10, A, V);
		FCEU_CheatAddRAM(8, 0x6000, (WRAM + ((V * 8192) & (WRAMSIZE - 1))));
		MMC5MemIn[(A - 0x6000) >> 13] = 1;
	} else
		MMC5MemIn[(A - 0x6000) >> 13] = 0;
}

static void MMC5PRG(void) {
	int x;
	switch (mmc5psize & 3) {
	case 0:
		MMC5ROMWrProtect[0] = MMC5ROMWrProtect[1] = MMC5ROMWrProtect[2] = MMC5ROMWrProtect[3] = 1;
		setprg32(0x8000, ((PRGBanks[1] & 0x7F) >> 2));
		for (x = 0; x < 4; x++)
			MMC5MemIn[1 + x] = 1;
		break;
	case 1:
		if (PRGBanks[1] & 0x80) {
			MMC5ROMWrProtect[0] = MMC5ROMWrProtect[1] = 1;
			setprg16(0x8000, (PRGBanks[1] >> 1));
			MMC5MemIn[1] = MMC5MemIn[2] = 1;
		} else {
			MMC5ROMWrProtect[0] = MMC5ROMWrProtect[1] = 0;
			MMC5WRAM(0x8000, PRGBanks[1] & (MMC5WRAMMAX-1) & 0xFE);
			MMC5WRAM(0xA000, (PRGBanks[1] & (MMC5WRAMMAX-1) & 0xFE) + 1);
		}
		MMC5MemIn[3] = MMC5MemIn[4] = 1;
		MMC5ROMWrProtect[2] = MMC5ROMWrProtect[3] = 1;
		setprg16(0xC000, (PRGBanks[3] & 0x7F) >> 1);
		break;
	case 2:
		if (PRGBanks[1] & 0x80) {
			MMC5MemIn[1] = MMC5MemIn[2] = 1;
			MMC5ROMWrProtect[0] = MMC5ROMWrProtect[1] = 1;
			setprg16(0x8000, (PRGBanks[1] & 0x7F) >> 1);
		} else {
			MMC5ROMWrProtect[0] = MMC5ROMWrProtect[1] = 0;
			MMC5WRAM(0x8000, PRGBanks[1] & (MMC5WRAMMAX-1) & 0xFE);
			MMC5WRAM(0xA000, (PRGBanks[1] & (MMC5WRAMMAX-1) & 0xFE) + 1);
		}
		if (PRGBanks[2] & 0x80) {
			MMC5ROMWrProtect[2] = 1;
			MMC5MemIn[3] = 1;
			setprg8(0xC000, PRGBanks[2] & 0x7F);
		} else {
			MMC5ROMWrProtect[2] = 0;
			MMC5WRAM(0xC000, PRGBanks[2] & (MMC5WRAMMAX-1));
		}
		MMC5MemIn[4] = 1;
		MMC5ROMWrProtect[3] = 1;
		setprg8(0xE000, PRGBanks[3] & 0x7F);
		break;
	case 3:
		for (x = 0; x < 3; x++)
			if (PRGBanks[x] & 0x80) {
				MMC5ROMWrProtect[x] = 1;
				setprg8(0x8000 + (x << 13), PRGBanks[x] & 0x7F);
				MMC5MemIn[1 + x] = 1;
			} else {
				MMC5ROMWrProtect[x] = 0;
				MMC5WRAM(0x8000 + (x << 13), PRGBanks[x] & (MMC5WRAMMAX-1));
			}
		MMC5MemIn[4] = 1;
		MMC5ROMWrProtect[3] = 1;
		setprg8(0xE000, PRGBanks[3] & 0x7F);
		break;
	}
}

static DECLFW(Mapper5_write) {
	switch (A) {
		case 0x5100:
			mmc5psize = V;
			MMC5PRG();
			break;
		case 0x5101:
			mmc5vsize = V;
			if (!mmc5ABMode) {
				MMC5CHRB();
				MMC5CHRA();
			} else {
				MMC5CHRA();
				MMC5CHRB();
			}
			break;
		case 0x5102:
			WRAMMaskEnable[0] = V;
			break;
		case 0x5103:
			WRAMMaskEnable[1] = V;
			break;
		case 0x5104:
			CHRMode = V;
			MMC5HackCHRMode = V & 3;
			break;
		case 0x5105:
		{
			int x;
			for (x = 0; x < 4; x++) {
				switch ((V >> (x << 1)) & 3) {
				case 0: PPUNTARAM |= 1 << x; vnapage[x] = NTARAM; break;
				case 1: PPUNTARAM |= 1 << x; vnapage[x] = NTARAM + 0x400; break;
				case 2: PPUNTARAM |= 1 << x; vnapage[x] = ExRAM; break;
				case 3: PPUNTARAM &= ~(1 << x); vnapage[x] = MMC5fill; break;
				}
			}
			NTAMirroring = V;
			break;
		}
		case 0x5106:
			if (V != NTFill)
				FCEU_dwmemset(MMC5fill, (V | (V << 8) | (V << 16) | (V << 24)), 0x3c0);
			NTFill = V;
			break;
		case 0x5107:
			if (V != ATFill) {
				unsigned char moop = V | (V << 2) | (V << 4) | (V << 6);
				FCEU_dwmemset(MMC5fill + 0x3c0, moop | (moop << 8) | (moop << 16) | (moop << 24), 0x40);
			}
			ATFill = V;
			break;
		case 0x5113:
			WRAMPage = V;
			MMC5WRAM(0x6000, V & (MMC5WRAMMAX-1));
			break;
		case 0x5114:
		case 0x5115:
		case 0x5116:
		case 0x5117:
			PRGBanks[A & 3] = V;
			MMC5PRG();
			break;
		case 0x5120:
		case 0x5121:
		case 0x5122:
		case 0x5123:
		case 0x5124:
		case 0x5125:
		case 0x5126:
		case 0x5127:
			mmc5ABMode = 0;
			CHRBanksA[A & 7] = V | ((MMC50x5130 & 0x3) << 8);
			MMC5CHRA();
			break;
		case 0x5128:
		case 0x5129:
		case 0x512a:
		case 0x512b:
			mmc5ABMode = 1;
			CHRBanksB[A & 3] = V | ((MMC50x5130 & 0x3) << 8);
			MMC5CHRB();
			break;
		case 0x5130: MMC50x5130 = V; break;
		case 0x5200: MMC5HackSPMode = V; break;
		case 0x5201: MMC5HackSPScroll = (V >> 3) & 0x1F; break;
		case 0x5202: MMC5HackSPPage = V & 0x3F; break;
		case 0x5203: X6502_IRQEnd(FCEU_IQEXT); IRQScanline = V; break;
		case 0x5204: X6502_IRQEnd(FCEU_IQEXT); IRQEnable = V & 0x80; break;
		case 0x5205: mul[0] = V; break;
		case 0x5206: mul[1] = V; break;
		}
}

static DECLFR(MMC5_ReadROMRAM) {
	if (MMC5MemIn[(A - 0x6000) >> 13])
		return Page[A >> 11][A];
	else
		return X.DB;
}

static DECLFW(MMC5_WriteROMRAM) {
	if ((A >= 0x8000) && (MMC5ROMWrProtect[(A - 0x8000) >> 13]))
			return;
	if (MMC5MemIn[(A - 0x6000) >> 13])
		if (((WRAMMaskEnable[0] & 3) | ((WRAMMaskEnable[1] & 3) << 2)) == 6)
			Page[A >> 11][A] = V;
}

static DECLFW(MMC5_ExRAMWr) {
	if (MMC5HackCHRMode != 3)
		ExRAM[A & 0x3ff] = V;
}

static DECLFR(MMC5_ExRAMRd) {
	return ExRAM[A & 0x3ff];
}

static DECLFR(MMC5_read) {
	switch (A) {
	case 0x5204: {
		uint8 x;
		X6502_IRQEnd(FCEU_IQEXT);
		x = MMC5IRQR;
		#ifdef FCEUDEF_DEBUGGER
		if (!fceuindbg)
		#endif
		MMC5IRQR &= 0x40;
		return x;
		}
	case 0x5205:
		return(mul[0] * mul[1]);
	case 0x5206:
		return((mul[0] * mul[1]) >> 8);
	}
	return(X.DB);
}

void MMC5Synco(void) {
	int x;

	MMC5PRG();
	for (x = 0; x < 4; x++) {
		switch ((NTAMirroring >> (x << 1)) & 3) {
		case 0: PPUNTARAM |= 1 << x; vnapage[x] = NTARAM; break;
		case 1: PPUNTARAM |= 1 << x; vnapage[x] = NTARAM + 0x400; break;
		case 2: PPUNTARAM |= 1 << x; vnapage[x] = ExRAM; break;
		case 3: PPUNTARAM &= ~(1 << x); vnapage[x] = MMC5fill; break;
		}
	}
	MMC5WRAM(0x6000, WRAMPage & (MMC5WRAMMAX-1));
	if (!mmc5ABMode) {
		MMC5CHRB();
		MMC5CHRA();
	} else {
		MMC5CHRA();
		MMC5CHRB();
	}

	//in case the fill register changed, we need to overwrite the fill buffer
	FCEU_dwmemset(MMC5fill, NTFill | (NTFill << 8) | (NTFill << 16) | (NTFill << 24), 0x3c0);
	{
		unsigned char moop = ATFill | (ATFill << 2) | (ATFill << 4) | (ATFill << 6);
		FCEU_dwmemset(MMC5fill + 0x3c0, moop | (moop << 8) | (moop << 16) | (moop << 24), 0x40);
	}

	MMC5HackCHRMode = CHRMode & 3;

	//zero 17-apr-2013 - why the heck should this happen here? anything in a `synco` should be depending on the state.
	//im going to leave it commented out to see what happens
	//X6502_IRQEnd(FCEU_IQEXT);
}

void MMC5_hb(int scanline) {
	//zero 24-jul-2014 - revised for newer understanding, to fix metal slader glory credits. see r7371 in bizhawk
	
	int sl = scanline + 1;
	int ppuon = (PPU[1] & 0x18);

	if (!ppuon || sl >= 241)
	{
		// whenever rendering is off for any reason (vblank or forced disable
		// the irq counter resets, as well as the inframe flag (easily verifiable from software)
		MMC5IRQR &= ~0x40;
		MMC5IRQR &= ~0x80;
		MMC5LineCounter = 0;
		X6502_IRQEnd(FCEU_IQEXT);
		return;
	}

	if (!(MMC5IRQR&0x40))
	{
		MMC5IRQR |= 0x40;
		MMC5IRQR &= ~0x80;
		MMC5LineCounter = 0;
		X6502_IRQEnd(FCEU_IQEXT);
	}
	else
	{
		MMC5LineCounter++;
		if (MMC5LineCounter == IRQScanline)
		{
			MMC5IRQR |= 0x80;
			if (IRQEnable & 0x80)
				X6502_IRQBegin(FCEU_IQEXT);
		}
	}

}

void MMC5_StateRestore(int version) {
	MMC5Synco();
}

typedef struct {
	uint16 wl[2];
	uint8 env[2];
	uint8 enable;
	uint8 running;
	uint8 raw;
	uint8 rawcontrol;
	int32 dcount[2];
	int32 BC[3];
	int32 vcount[2];
} MMC5APU;

static MMC5APU MMC5Sound;


static void Do5PCM() {
	int32 V;
	int32 start, end;

	start = MMC5Sound.BC[2];
	end = (SOUNDTS << 16) / soundtsinc;
	if (end <= start) return;
	MMC5Sound.BC[2] = end;

	if (!(MMC5Sound.rawcontrol & 0x40) && MMC5Sound.raw)
		for (V = start; V < end; V++)
			Wave[V >> 4] += MMC5Sound.raw << 1;
}

static void Do5PCMHQ() {
	uint32 V;
	if (!(MMC5Sound.rawcontrol & 0x40) && MMC5Sound.raw)
		for (V = MMC5Sound.BC[2]; V < SOUNDTS; V++)
			WaveHi[V] += MMC5Sound.raw << 5;
	MMC5Sound.BC[2] = SOUNDTS;
}


static DECLFW(Mapper5_SW) {
	A &= 0x1F;

	GameExpSound.Fill = MMC5RunSound;
	GameExpSound.HiFill = MMC5RunSoundHQ;

	switch (A) {
	case 0x10: if (psfun) psfun(); MMC5Sound.rawcontrol = V; break;
	case 0x11: if (psfun) psfun(); MMC5Sound.raw = V; break;

	case 0x0:
	case 0x4:
		if (sfun) sfun(A >> 2);
		MMC5Sound.env[A >> 2] = V;
		break;
	case 0x2:
	case 0x6:
		if (sfun) sfun(A >> 2);
		MMC5Sound.wl[A >> 2] &= ~0x00FF;
		MMC5Sound.wl[A >> 2] |= V & 0xFF;
		break;
	case 0x3:
	case 0x7:
		MMC5Sound.wl[A >> 2] &= ~0x0700;
		MMC5Sound.wl[A >> 2] |= (V & 0x07) << 8;
		MMC5Sound.running |= 1 << (A >> 2);
		break;
	case 0x15:
		if (sfun) {
			sfun(0);
			sfun(1);
		}
		MMC5Sound.running &= V;
		MMC5Sound.enable = V;
		break;
	}
}

static void Do5SQ(int P) {
	static int tal[4] = { 1, 2, 4, 6 };
	int32 V, amp, rthresh, wl;
	int32 start, end;

	start = MMC5Sound.BC[P];
	end = (SOUNDTS << 16) / soundtsinc;
	if (end <= start) return;
	MMC5Sound.BC[P] = end;

	wl = MMC5Sound.wl[P] + 1;
	amp = (MMC5Sound.env[P] & 0xF) << 4;
	rthresh = tal[(MMC5Sound.env[P] & 0xC0) >> 6];

	if (wl >= 8 && (MMC5Sound.running & (P + 1))) {
		int dc, vc;

		wl <<= 18;
		dc = MMC5Sound.dcount[P];
		vc = MMC5Sound.vcount[P];

		for (V = start; V < end; V++) {
			if (dc < rthresh)
				Wave[V >> 4] += amp;
			vc -= nesincsize;
			while (vc <= 0) {
				vc += wl;
				dc = (dc + 1) & 7;
			}
		}
		MMC5Sound.dcount[P] = dc;
		MMC5Sound.vcount[P] = vc;
	}
}

static void Do5SQHQ(int P) {
	static int tal[4] = { 1, 2, 4, 6 };
	uint32 V;
	int32 amp, rthresh, wl;

	wl = MMC5Sound.wl[P] + 1;
	amp = ((MMC5Sound.env[P] & 0xF) << 8);
	rthresh = tal[(MMC5Sound.env[P] & 0xC0) >> 6];

	if (wl >= 8 && (MMC5Sound.running & (P + 1))) {
		int dc, vc;

		wl <<= 1;

		dc = MMC5Sound.dcount[P];
		vc = MMC5Sound.vcount[P];
		for (V = MMC5Sound.BC[P]; V < SOUNDTS; V++) {
			if (dc < rthresh)
				WaveHi[V] += amp;
			vc--;
			if (vc <= 0) { /* Less than zero when first started. */
				vc = wl;
				dc = (dc + 1) & 7;
			}
		}
		MMC5Sound.dcount[P] = dc;
		MMC5Sound.vcount[P] = vc;
	}
	MMC5Sound.BC[P] = SOUNDTS;
}

void MMC5RunSoundHQ(void) {
	Do5SQHQ(0);
	Do5SQHQ(1);
	Do5PCMHQ();
}

void MMC5HiSync(int32 ts) {
	int x;
	for (x = 0; x < 3; x++)
		MMC5Sound.BC[x] = ts;
}

void MMC5RunSound(int Count) {
	int x;
	Do5SQ(0);
	Do5SQ(1);
	Do5PCM();
	for (x = 0; x < 3; x++)
		MMC5Sound.BC[x] = Count;
}

void Mapper5_ESI(void) {
	GameExpSound.RChange = Mapper5_ESI;
	if (FSettings.SndRate) {
		if (FSettings.soundq >= 1) {
			sfun = Do5SQHQ;
			psfun = Do5PCMHQ;
		} else {
			sfun = Do5SQ;
			psfun = Do5PCM;
		}
	} else {
		sfun = 0;
		psfun = 0;
	}
	memset(MMC5Sound.BC, 0, sizeof(MMC5Sound.BC));
	memset(MMC5Sound.vcount, 0, sizeof(MMC5Sound.vcount));
	GameExpSound.HiSync = MMC5HiSync;
}

void NSFMMC5_Init(void) {
	memset(&MMC5Sound, 0, sizeof(MMC5Sound));
	mul[0] = mul[1] = 0;
	ExRAM = (uint8*)FCEU_gmalloc(1024);
	Mapper5_ESI();
	SetWriteHandler(0x5c00, 0x5fef, MMC5_ExRAMWr);
	SetReadHandler(0x5c00, 0x5fef, MMC5_ExRAMRd);
	MMC5HackCHRMode = 2;
	SetWriteHandler(0x5000, 0x5015, Mapper5_SW);
	SetWriteHandler(0x5205, 0x5206, Mapper5_write);
	SetReadHandler(0x5205, 0x5206, MMC5_read);
}

void NSFMMC5_Close(void) {
	if (WRAM)
		FCEU_gfree(WRAM);
	WRAM = NULL;
	MMC5WRAMsize = 0;
	FCEU_gfree(ExRAM);
	ExRAM = NULL;
}

static void GenMMC5Power(void) {

	PRGBanks.fill(0xFF);
	WRAMPage = 0;
	CHRBanksA.fill(0xFF);
	CHRBanksB.fill(0xFF);
	WRAMMaskEnable.fill(0xFF);
	mmc5ABMode = 0;
	IRQScanline = 0;
	IRQEnable = 0;
	CHRMode = 0;
	NTAMirroring = NTFill = ATFill = 0xFF;
	MMC5IRQR = 0;
	MMC5LineCounter = 0;
	mmc5psize = mmc5vsize = 3;
	mul.fill(0);

	MMC5ROMWrProtect.fill(0);
	MMC5MemIn.fill(0);

	// MMC5fill is and 8-bit tile index, and a 2-bit attribute implented as a mirrored nametable
	u8 nval = MMC5fill[0x000];
	u8 aval = MMC5fill[0x3C0] & 3; aval = aval | (aval << 2) | (aval << 4) | (aval << 6);
	FCEU_dwmemset(MMC5fill + 0x000, nval | (nval<<8) | (nval<<16) | (nval<<24), 0x3C0);
	FCEU_dwmemset(MMC5fill + 0x3C0, aval | (aval<<8) | (aval<<16) | (aval<<24), 0x040);

	if(MMC5battery == 0) {
		FCEU_MemoryRand(WRAM, MMC5WRAMsize * 8 * 1024);
		FCEU_MemoryRand(MMC5fill,1024);
		FCEU_MemoryRand(ExRAM,1024);
	}

	MMC5Synco();

	SetWriteHandler(0x4020, 0x5bff, Mapper5_write);
	SetReadHandler(0x4020, 0x5bff, MMC5_read);

	SetWriteHandler(0x5c00, 0x5fff, MMC5_ExRAMWr);
	SetReadHandler(0x5c00, 0x5fff, MMC5_ExRAMRd);

	SetWriteHandler(0x6000, 0xFFFF, MMC5_WriteROMRAM);
	SetReadHandler(0x6000, 0xFFFF, MMC5_ReadROMRAM);

	SetWriteHandler(0x5000, 0x5015, Mapper5_SW);
	SetWriteHandler(0x5205, 0x5206, Mapper5_write);
	SetReadHandler(0x5205, 0x5206, MMC5_read);

//	GameHBIRQHook=MMC5_hb;
//	FCEU_CheatAddRAM(8, 0x6000, WRAM);
	FCEU_CheatAddRAM(1, 0x5c00, ExRAM);
}

static SFORMAT MMC5_StateRegs[] = {
	{ &PRGBanks, 4, "PRGB" },
	{ &CHRBanksA, 16, "CHRA" },
	{ &CHRBanksB, 8, "CHRB" },
	{ &WRAMPage, 1, "WRMP" },
	{ &WRAMMaskEnable, 2, "WRME" },
	{ &mmc5ABMode, 1, "ABMD" },
	{ &IRQScanline, 1, "IRQS" },
	{ &IRQEnable, 1, "IRQE" },
	{ &CHRMode, 1, "CHRM" },
	{ &NTAMirroring, 1, "NTAM" },
	{ &NTFill, 1, "NTFL" },
	{ &ATFill, 1, "ATFL" },

	//zero 17-apr-2013 - added
	{ &MMC5IRQR, 1, "IRQR" },
	{ &MMC5LineCounter, 1, "LCTR" },
	{ &mmc5psize, 1, "PSIZ" },
	{ &mmc5vsize, 1, "VSIZ" },
	{ &mul, 2, "MUL2" },
	{ &MMC5ROMWrProtect, 4, "WRPR" },
	{ &MMC5MemIn, 5, "MEMI" },

	{ &MMC5Sound.wl[0], 2 | FCEUSTATE_RLSB, "SDW0" },
	{ &MMC5Sound.wl[1], 2 | FCEUSTATE_RLSB, "SDW1" },
	{ MMC5Sound.env, 2, "SDEV" },
	{ &MMC5Sound.enable, 1, "SDEN" },
	{ &MMC5Sound.running, 1, "SDRU" },
	{ &MMC5Sound.raw, 1, "SDRW" },
	{ &MMC5Sound.rawcontrol, 1, "SDRC" },

	//zero 17-apr-2013 - added
	{ &MMC5Sound.dcount[0], 4 | FCEUSTATE_RLSB, "DCT0" },
	{ &MMC5Sound.dcount[1], 4 | FCEUSTATE_RLSB, "DCT1" },
	{ &MMC5Sound.BC[0], 4 | FCEUSTATE_RLSB, "BC00" },
	{ &MMC5Sound.BC[1], 4 | FCEUSTATE_RLSB, "BC01" },
	{ &MMC5Sound.BC[2], 4 | FCEUSTATE_RLSB, "BC02" },
	{ &MMC5Sound.vcount[0], 4 | FCEUSTATE_RLSB, "VCT0" },
	{ &MMC5Sound.vcount[1], 4 | FCEUSTATE_RLSB, "VCT1" },
	{ 0 }
};

static void GenMMC5_Init(CartInfo *info, int wsize, int battery) {
	if (wsize) {
		WRAM = (uint8*)FCEU_malloc(wsize * 1024);
		FCEU_MemoryRand(WRAM, wsize * 1024);
		SetupCartPRGMapping(0x10, WRAM, wsize * 1024, 1);
		AddExState(WRAM, wsize * 1024, 0, "WRAM");
	}

	MMC5fill = (uint8*)FCEU_malloc(1024);
	ExRAM = (uint8*)FCEU_malloc(1024);

	FCEU_MemoryRand(MMC5fill,1024);
	FCEU_MemoryRand(ExRAM,1024);

	AddExState(ExRAM, 1024, 0, "ERAM");
	AddExState(&MMC5HackSPMode, 1, 0, "SPLM");
	AddExState(&MMC5HackSPScroll, 1, 0, "SPLS");
	AddExState(&MMC5HackSPPage, 1, 0, "SPLP");
	AddExState(&MMC50x5130, 1, 0, "5130");
	AddExState(MMC5_StateRegs, ~0, 0, 0);

	MMC5WRAMsize = wsize / 8;
	BuildWRAMSizeTable();
	GameStateRestore = MMC5_StateRestore;
	info->Power = GenMMC5Power;

	MMC5battery = battery;
	if (battery) {
		uint32 saveGameSize = 0;
		if (info->ines2)
		{
			saveGameSize = info->battery_wram_size;
		}
		else
		{
			//this is more complex than it looks because it MUST BE, I guess. is there an assumption that only 8KB of 16KB is battery backed? That's NES mappers for you
			//I added 64KB for the new 64KB homebrews
			if (wsize <= 16)
				saveGameSize = 8192;
			else if(wsize == 64)
				saveGameSize = 64*1024;
			else
				saveGameSize = 32768;
		}
		info->addSaveGameBuf( WRAM, saveGameSize );
	}

	MMC5HackVROMMask = CHRmask4[0];
	MMC5HackExNTARAMPtr = ExRAM;
	MMC5Hack = 1;
	MMC5HackVROMPTR = CHRptr[0];
	MMC5HackCHRMode = 0;
	MMC5HackSPMode = MMC5HackSPScroll = MMC5HackSPPage = 0;
	Mapper5_ESI();

	FFCEUX_PPURead = mmc5_PPURead;
	FFCEUX_PPUWrite = mmc5_PPUWrite;
}

void Mapper5_Init(CartInfo *info) {
	if (info->ines2)
	{
		WRAMSIZE = (info->wram_size + info->battery_wram_size) / 1024;
	}
	else
	{
		WRAMSIZE = DetectMMC5WRAMSize(info->CRC32);
	}
	GenMMC5_Init(info, WRAMSIZE, info->battery);
}

// ELROM seems to have 0KB of WRAM
// EKROM seems to have 8KB of WRAM, battery-backed
// ETROM seems to have 16KB of WRAM, battery-backed
// EWROM seems to have 32KB of WRAM, battery-backed

void ELROM_Init(CartInfo *info) {
	GenMMC5_Init(info, 0, 0);
}

void EKROM_Init(CartInfo *info) {
	GenMMC5_Init(info, 8, info->battery);
}

void ETROM_Init(CartInfo *info) {
	GenMMC5_Init(info, 16, info->battery);
}

void EWROM_Init(CartInfo *info) {
	GenMMC5_Init(info, 32, info->battery);
}
