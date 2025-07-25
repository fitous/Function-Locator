#ifndef FLOC_H
#define FLOC_H

#include "types.h"
#include "vector.h"
#include "os.h"

struct tdTRACKER;
typedef struct tdTRACKER TRACKER;

typedef struct tdFLOC_HANDLE* FLOC_HANDLE;

typedef struct tdFLOC_CTX {
	VECTOR vecTrackers;
	VECTOR vecPools;
	THREAD thrDebug;
	PID pidTarget;
	BOOL bForeignDebugLoop;
	BOOL bIsStepActive;
	BOOL bDbgLoopRunning;
	BOOL bIsPendingReset;
	BOOL bStopDebugLoop;
	BOOL bTargetDied;
	BYTE _padding[4];
} FLOC_CTX;

FLOC_CTX* FLOC_ContextGet(FLOC_HANDLE hHandle);
U32 FLOC_ContextGetCount(void);
U32 FLOC_ContextGetMax(void);
void FLOC_ContextInsert(FLOC_CTX* pCtx);
void FLOC_ContextClear(FLOC_CTX const * pCtx);

BOOL FLOC_BreakpointHandler(FLOC_CTX const * pCtx, ADDRESS aAddress, BYTE* puOriginalByte);
void FLOC_DebugLoop(FLOC_CTX* pCtx);
BOOL FLOC_IsTargetDead(FLOC_CTX* pCtx);
BOOL FLOC_IsTargetAlive(PID pidTarget);

void FLOC_StepFilterOut(FLOC_CTX* pCtx, BOOL bExecuted);
void FLOC_TrackerRemove(TRACKER* pTracker, PROCESS hProcess);
void FLOC_TrackerDisable(TRACKER* pTracker, PROCESS hProcess);
void FLOC_TrackerEnable(TRACKER* pTracker, PROCESS hProcess);

#endif /* FLOC_H */
