
void FCEUPPU_Reset(void);
void FCEUPPU_Power(void);
int FCEUPPU_Loop(int skip);

void FCEUPPU_LineUpdate();
void FCEUPPU_SetVideoSystem(int w);

extern void (*GameHBIRQHook)(void), (*GameHBIRQHook2)(void);

/* For cart.c and banksw.h, mostly */
extern uint8 NTARAM[0x800], *vnapage[4];
extern uint8 PPUNTARAM;
extern uint8 PPUCHRRAM;

void FCEUPPU_SaveState(void);
void FCEUPPU_LoadState(int version);
uint32 FCEUPPU_PeekAddress();
uint8* FCEUPPU_GetCHR(uint32 vadr, uint32 refreshaddr);
int FCEUPPU_GetAttr(int ntnum, int xt, int yt);
void ppu_getScroll(int &xpos, int &ypos);


#ifdef _MSC_VER
#define FASTCALL __fastcall
#else
#define FASTCALL
#endif

void PPU_ResetHooks();
extern uint8 (FASTCALL *FFCEUX_PPURead)(uint32 A);
extern void (*FFCEUX_PPUWrite)(uint32 A, uint8 V);
extern uint8 FASTCALL FFCEUX_PPURead_Default(uint32 A);
void FFCEUX_PPUWrite_Default(uint32 A, uint8 V);

// Oregon expanded 8K RAM on board
#define RAM_SIZE 0x2000
#define RAM_MASK RAM_SIZE-1
#define SPRAM_SIZE 0x200
#define PPUR_SIZE 16

extern int g_rasterpos;
extern uint8 PPU[PPUR_SIZE];
extern bool DMC_7bit;
extern bool paldeemphswap;

extern uint8 cpu410x[0x10];
extern uint8 ppu20xx[0x80];
extern uint8 apu40xx[0x40];

enum PPUPHASE {
	PPUPHASE_VBL, PPUPHASE_BG, PPUPHASE_OBJ
};

extern PPUPHASE ppuphase;
