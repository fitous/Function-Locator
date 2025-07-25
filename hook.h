#ifndef HOOK_H
#define HOOK_H

#include "types.h"
#include "os.h"

struct tdVECTOR;
typedef struct tdVECTOR VECTOR;

struct tdTRACKER;
typedef struct tdTRACKER TRACKER;

struct tdPOOL;
typedef struct tdPOOL POOL;

typedef struct tdHOOK {
	ADDRESS aHookAddress;
	U32 uJumpBytesLen;
	U32 uHitOffset;
	BYTE uJumpBytes[14];
	BYTE _padding[2];
} HOOK;

#define JUMP_REL32_LEN (5)
#define JUMP_ABS64_LEN (14)
#define JUMP_MAX_LEN JUMP_ABS64_LEN
#define HOOK_REL32_LEN (30)
#define HOOK_ABS64_LEN (64)
#define HOOK_MAX_LEN HOOK_ABS64_LEN

BOOL Hook_Create(VECTOR* pvecPools, TRACKER* pTracker, PROCESS hProcess, U32 uFuncLen);
BOOL Hook_Enable(TRACKER const * pTracker, PROCESS hProcess);
BOOL Hook_IsHit(TRACKER* pTracker, PROCESS hProcess);

#endif /* HOOK_H */
