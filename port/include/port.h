#ifndef PORT_H
#define PORT_H

/**
 * @file port.h
 * PC-port internal API. Only port/src/ and #else branches of TARGET_N64
 * ifdefs in shared sources may include this.
 */

#include <ultra64.h>

/* --- main.c / options ----------------------------------------------------- */
struct portconfig {
    const char *rompath;    /* default "data/ge007.u.z64" */
    int selftest;           /* --selftest: load+CRC a known file, then exit */
    int stubstage;          /* --stub-stage: skip lvlStageLoad, empty dlists */
    int verbose;            /* --verbose: extra logging */
    const char *stage;      /* --stage <name|levelid>: boot straight into a stage */
    const char *hard;       /* --hard <0-3>: difficulty for --stage */
};
extern struct portconfig g_PortConfig;

/* --- rom.c ---------------------------------------------------------------- */
int portRomLoad(const char *path);          /* 0 on success */
extern u8 *g_PortRomData;
extern u32 g_PortRomSize;

/* --- memory.c ------------------------------------------------------------- */
void portMemInit(void);
void *portMemArenaStart(void);
/* low-4GB allocator: any buffer the game can point a u32 at (64-bit build) */
void *portLowAlloc(u32 size);
void portLowFree(void *ptr, u32 size);
u32 portMemArenaSize(void);

/* --- sched.c (cooperative pump) ------------------------------------------- */
void portSchedInit(void);   /* replaces schedulerInitThread() */
void portSchedPump(void);   /* one cooperative scheduling step; called by
                               blocking osRecvMesg/osSendMesg */

/* --- video ---------------------------------------------------------------- */
struct OSScTask_s;
void portVideoInit(void);
void portVideoProcessTask(struct OSScTask_s *task);

/* --- platform backend (SDL2 or headless null) ----------------------------- */
int  portPlatformInit(const char *title, int width, int height);
void portPlatformShutdown(void);
void portPlatformPresent(void *framebuffer); /* end-of-frame; clears/swaps */
void portPlatformPoll(void);                 /* window/input events; may exit() */
void portPlatformSleepMs(u32 ms);
u64  portPlatformTimeNs(void);               /* monotonic */

/* --- crctrace.c: golden CRC reporting (PORT_CRC_TRACE, phase 0) ------------ */
u32  portCrc32(const void *data, u32 len);
u32  portCrc32Update(u32 crc, const void *data, u32 len); /* seed 0xFFFFFFFF, xor-out yourself */
void portCrcTrace(const char *tag, u32 key, const void *data, u32 len);
void portCrcTraceValue(const char *tag, u32 key, u32 len, u32 crc);

/* built-in AI script arrays (defined at the tail of src/game/chraidata.c) */
struct PortAiArray {
    const char *name;
    const unsigned char *data;
    unsigned int len;
};
extern const struct PortAiArray g_PortAiArrays[];
extern const int g_PortAiArrayCount;

/* --- preprocess.c: load-time byteswaps (PORT_PREPROCESS sites) ------------- */
void portSwapU32InPlace(void *data, u32 size);
void portSwapTextBank(u32 *bank);
void portSwapBriefingData(void *data);

/* input (port/src/input_sdl.c) */
void portInputSetAimMode(s32 aiming);
s32 portInputConsumeMouseLook(f32 *dtheta, f32 *dverta);
s32 portInputConsumeWeaponScroll(void);      /* wheel/keys: +n cycle fwd, -n back */
s32 portInputConsumeWeaponSelect(void);      /* 1-based weapon slot, 0 = none */
void portSwapGlobalImagetable(void *data, u32 size);
void portSwapRarewareLogo(void *data, u32 size);
void portSwapAnimHeader(void *header, void *blobBase);

/* --- preprocess_model.c ----------------------------------------------------- */
struct ModelFileHeader;
void portPreprocessModelFile(struct ModelFileHeader *header, void *filedata, u32 vma, u32 size);
void *portModelFileBase(struct ModelFileHeader *hdr);

/* stage geometry (preprocess_bg.c) */
void portSwapBgHeaderProbe(void *buf);
void portSwapBgFile(void *data, u32 size);
void portSwapBgRoomVertices(void *ptr, s32 bytes);
void portSwapBgRoomGdl(void *ptr, s32 bytes);
void *portSwapStanFile(void *file); /* 64-bit: returns a relocated native copy */

/* stage setup file (preprocess_setup.c) */
void *portSwapSetupFile(void *data); /* 64-bit: returns a relocated native copy */
void *portSetupFileBlobBase(void);

/* audio (preprocess_audio.c, audio.c) */
void portSwapBankFile(void *data, u32 size, u32 tblRomBase);
void portSwapRareSeqHeader(void *data);
void portSwapRareSeqTable(void *data);
void portSwapCMidiHdr(void *data);
void portAudioFrame(void);
void portAudioPump(void);

/* --- input backend --------------------------------------------------------- */
void portInputInit(void);
void portInputRead(OSContPad *pads);         /* fills MAXCONTROLLERS pads */
int portMouselookGrabbed(void);              /* PORT_MOUSELOOK=1 and mouse captured */

/* --- time base -------------------------------------------------------------
 * The VR4300 counter runs at OS_CLOCK_RATE*3/4 = 46,875,000 Hz and
 * frametiming.c does arithmetic in those units, so the port fakes the same
 * rate from the monotonic clock. */
#define PORT_COUNTER_HZ 46875000ull

#endif /* PORT_H */
