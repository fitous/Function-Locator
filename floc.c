#include "floc.h"
#include "tracker.h"
#include "hook.h"

#define MAX_CONTEXTS_COUNT 4
static FLOC_CTX* gContexts[MAX_CONTEXTS_COUNT] = { 0 };
static U32 gContextCount = 0;

FLOC_CTX* FLOC_ContextGet(FLOC_HANDLE const hHandle)
{
	if (gContexts[0] == (FLOC_CTX*)hHandle)
	{
		return (FLOC_CTX*)hHandle;
	}

	for (U32 i = 1; i < MAX_CONTEXTS_COUNT; i++)
	{
		if (gContexts[i] == (FLOC_CTX*)hHandle)
		{
			return (FLOC_CTX*)hHandle;
		}
	}

	return NULL;
}

void FLOC_ContextInsert(FLOC_CTX* const pCtx)
{
	for (U32 i = 0; i < MAX_CONTEXTS_COUNT; i++)
	{
		if (NULL == gContexts[i])
		{
			gContexts[i] = pCtx;
			gContextCount++;
			break;
		}
	}
}
void FLOC_ContextClear(FLOC_CTX const * const pCtx)
{
	for (U32 i = 0; i < MAX_CONTEXTS_COUNT; i++)
	{
		if (pCtx == gContexts[i])
		{
			gContexts[i] = NULL;
			gContextCount--;
			break;
		}
	}
}

U32 FLOC_ContextGetCount(void)
{
	return gContextCount;
}

U32 FLOC_ContextGetMax(void)
{
	return MAX_CONTEXTS_COUNT;
}

BOOL FLOC_BreakpointHandler(FLOC_CTX const * const pCtx, ADDRESS const aAddress, BYTE* const puOriginalByte)
{
	TRACKER* pTracker = NULL;
	for (U32 i = 0; i < pCtx->vecTrackers.uElemCount; i++)
	{
		pTracker = (TRACKER*)Vector_AddressOf(&(pCtx->vecTrackers), i);
		if (NULL != pTracker && aAddress == pTracker->aAddress)
		{
			break;
		}
	}
	if (NULL == pTracker)
	{
		return FALSE;
	}

	if (pCtx->bIsStepActive)
	{
		pTracker->bHit = TRUE;
	}
	
	/* Breakpoint will be removed inside Target_WaitForBreakpoint immediately after return from here. */
	pTracker->bEnabled = FALSE;

	*puOriginalByte = pTracker->u.bp.uOriginalByte;
	return TRUE;
}

void FLOC_DebugLoop(FLOC_CTX* const pCtx)
{
	if (!Target_DebuggerAttach(pCtx->pidTarget))
	{
		return;
	}

	while (!pCtx->bStopDebugLoop)
	{
		BOOL const bTargetDied = Target_WaitForBreakpoint(FLOC_BreakpointHandler, pCtx);
		if (bTargetDied)
		{
			pCtx->bDbgLoopRunning = FALSE;
			pCtx->bTargetDied = TRUE;
			break;
		}
	}

	Target_DebuggerDetach(pCtx->pidTarget);
	return;
}

BOOL FLOC_IsTargetAlive(PID const pidTarget)
{
	PROCESS const hProcess = Target_HandleAcquire(pidTarget);
	if (NULL == hProcess)
	{
		return FALSE;
	}
	Target_HandleRelease(hProcess);
	return TRUE;
}

void FLOC_TrackerRemove(TRACKER* const pTracker, PROCESS const hProcess)
{
	if (NULL == pTracker)
	{
		return;
	}

	if (TRACKER_TYPE_BREAKPOINT_SW == pTracker->eType)
	{
		Target_BreakpointRemoveDormant(hProcess, pTracker->aAddress, pTracker->u.bp.uOriginalByte);
	}
	/* Hooks will eventually remove themselves automatically inside the target process. */

	pTracker->bHit = FALSE;
	pTracker->bEnabled = FALSE;
	pTracker->eType = TRACKER_TYPE_DELETED;
	pTracker->aAddress = 0;
}

void FLOC_TrackerDisable(TRACKER* const pTracker, PROCESS const hProcess)
{
	if (NULL == pTracker)
	{
		return;
	}

	if (TRACKER_TYPE_BREAKPOINT_SW == pTracker->eType)
	{
		Target_BreakpointRemoveDormant(hProcess, pTracker->aAddress, pTracker->u.bp.uOriginalByte);
	}
	/* Hooks can simply be ignored. */

	pTracker->bEnabled = FALSE;
}

void FLOC_TrackerEnable(TRACKER* const pTracker, PROCESS const hProcess)
{
	if (NULL == pTracker)
	{
		return;
	}

	BOOL bRet = FALSE;
	if (TRACKER_TYPE_BREAKPOINT_SW == pTracker->eType)
	{
		bRet = Target_BreakpointAdd(hProcess, pTracker->aAddress);
	}
	else if (TRACKER_TYPE_HOOK_INLINE == pTracker->eType)
	{
		bRet = Hook_Enable(pTracker, hProcess);
	}

	pTracker->bEnabled = bRet;
}

void FLOC_StepFilterOut(FLOC_CTX* const pCtx, BOOL const bExecuted)
{
	PROCESS const hProcess = Target_HandleAcquire(pCtx->pidTarget);
	if (NULL == hProcess)
	{
		return;
	}
	
	VECTOR const* const pvecTrackers = &(pCtx->vecTrackers);
	U32 const uElemCount = pvecTrackers->uElemCount;
	for (U32 i = 0; i < uElemCount; i++)
	{
		TRACKER* const pTracker = (TRACKER*)Vector_AddressOf(pvecTrackers, i);
		if (NULL == pTracker || TRACKER_TYPE_DELETED == pTracker->eType)
		{
			continue;
		}

		/* Ignore trackers that were not executed but werent enabled in the first place. */
		if ((pTracker->bHit && bExecuted) || (!pTracker->bHit && !bExecuted && pTracker->bEnabled))
		{
			FLOC_TrackerRemove(pTracker, hProcess);
		}
		else
		{
			/* Prepare for the next step. */
			pTracker->bHit = FALSE;
		}
	}

	pCtx->bIsPendingReset = FALSE;
	Target_HandleRelease(hProcess);
}

BOOL FLOC_IsTargetDead(FLOC_CTX* const pCtx)
{
	if (pCtx->bTargetDied)
	{
		if (pCtx->bDbgLoopRunning && !pCtx->bForeignDebugLoop)
		{
			Thread_Close(pCtx->thrDebug);
		}
		pCtx->bDbgLoopRunning = FALSE;
		pCtx->bForeignDebugLoop = FALSE;
		pCtx->bIsPendingReset = FALSE;
		pCtx->bIsStepActive = FALSE;
		pCtx->bStopDebugLoop = FALSE;
		pCtx->pidTarget = 0;
		return TRUE;
	}
	return FALSE;
}
