#include "flocdll.h"
#include "floc.h"
#include "vector.h"
#include "tracker.h"
#include "pool.h"
#include "hook.h"

FLOC_STATUS FLOCDLL_Initialize(FLOC_HANDLE* const phHandle)
{
	if (!Process_CheckPrivileges())
	{
		return FLOC_STATUS_INSUFFICIENT_PRIVILEGES;
	}
	
	if (FLOC_ContextGetCount() >= FLOC_ContextGetMax())
	{
		return FLOC_STATUS_TOO_MANY_CONTEXTS;
	}

	FLOC_CTX* const pCtx = Memory_Alloc(sizeof(FLOC_CTX));
	if (NULL == pCtx)
	{
		return FLOC_STATUS_MEMORY_ALLOC_FAIL;
	}

	pCtx->pidTarget = 0;
	pCtx->bForeignDebugLoop = FALSE;
	pCtx->bIsStepActive = FALSE;
	pCtx->bDbgLoopRunning = FALSE;
	pCtx->bIsPendingReset = FALSE;
	pCtx->thrDebug = 0;
	pCtx->bStopDebugLoop = FALSE;
	pCtx->bTargetDied = FALSE;

	VECTOR* const pvecTrackers = &(pCtx->vecTrackers);
	if (!Vector_Init(pvecTrackers, sizeof(TRACKER), 2000))
	{
		Memory_Free(pCtx);
		return FLOC_STATUS_MEMORY_ALLOC_FAIL;
	}	
	VECTOR* const pvecPools = &(pCtx->vecPools);
	if (!Vector_Init(pvecPools, sizeof(POOL), 10))
	{
		Vector_Free(pvecTrackers);
		Memory_Free(pCtx);
		return FLOC_STATUS_MEMORY_ALLOC_FAIL;
	}

	FLOC_ContextInsert(pCtx);
	*phHandle = (FLOC_HANDLE)pCtx;
	return FLOC_STATUS_SUCCESS;
}

FLOC_STATUS FLOCDLL_Uninitialize(FLOC_HANDLE const hHandle)
{
	FLOC_CTX* const pCtx = FLOC_ContextGet(hHandle);
	if (NULL == pCtx)
	{
		return FLOC_STATUS_INVALID_HANDLE;
	}

	FLOCDLL_TrackerAllDisable(hHandle);

	if (pCtx->bDbgLoopRunning 
		&& !pCtx->bForeignDebugLoop 
		&& FLOC_STATUS_SUCCESS != FLOCDLL_DebugLoopStop(hHandle))
	{
		return FLOC_STATUS_DEBUG_LOOP_STOP_FAIL;
	}

	Vector_Free(&(pCtx->vecTrackers));
	Vector_Free(&(pCtx->vecPools));
	Memory_Free(hHandle);
	FLOC_ContextClear(pCtx);

	return FLOC_STATUS_SUCCESS;
}

FLOC_STATUS FLOCDLL_TargetSet(FLOC_HANDLE const hHandle, PID const pidTarget)
{
	FLOC_CTX* const pCtx = FLOC_ContextGet(hHandle);
	if (NULL == pCtx)
	{
		return FLOC_STATUS_INVALID_HANDLE;
	}	

	if (0 != pCtx->pidTarget)
	{
		return FLOC_STATUS_TARGET_ALREADY_SET;
	}
	if (!FLOC_IsTargetAlive(pidTarget))
	{
		return FLOC_STATUS_INVALID_TARGET;
	}
	if (!Target_Is64bit(pidTarget))
	{
		return FLOC_STATUS_TARGET_NOT_64BIT;
	}

	pCtx->pidTarget = pidTarget;
	return FLOC_STATUS_SUCCESS;
}

FLOC_STATUS FLOCDLL_DebugLoopStart(FLOC_HANDLE const hHandle)
{
	FLOC_CTX* const pCtx = FLOC_ContextGet(hHandle);
	if (NULL == pCtx)
	{
		return FLOC_STATUS_INVALID_HANDLE;
	}

	if (pCtx->bDbgLoopRunning || pCtx->bForeignDebugLoop)
	{
		return FLOC_STATUS_DEBUG_LOOP_ALREADY_RUNNING;
	}
	if (0 == pCtx->pidTarget)
	{
		return FLOC_STATUS_TARGET_NOT_SET;
	}
	if (!FLOC_IsTargetAlive(pCtx->pidTarget))
	{
		return FLOC_STATUS_INVALID_TARGET;
	}

	BOOL bPresent = FALSE;
	BOOL bRet = Target_IsDebuggerAttached(pCtx->pidTarget, &bPresent);
	if (!bRet)
	{
		return FLOC_STATUS_TARGET_CANNOT_CHECK_DEBUGGER;
	}
	if (bPresent)
	{
		return FLOC_STATUS_DEBUGGER_ALREADY_ATTACHED;
	}

	THREAD thrDebug;
	bRet = Thread_Start(FLOC_DebugLoop, pCtx, &thrDebug);
	if (!bRet)
	{
		return FLOC_STATUS_DEBUG_THREAD_START_FAIL;
	}

	pCtx->bDbgLoopRunning = TRUE;
	pCtx->thrDebug = thrDebug;
	pCtx->bForeignDebugLoop = FALSE;

	return FLOC_STATUS_SUCCESS;
}

FLOC_STATUS FLOCDLL_DebugLoopStop(FLOC_HANDLE const hHandle)
{
	FLOC_CTX* const pCtx = FLOC_ContextGet(hHandle);
	if (NULL == pCtx)
	{
		return FLOC_STATUS_INVALID_HANDLE;
	}

	if (FLOC_IsTargetDead(pCtx))
	{
		return FLOC_STATUS_TARGET_DIED;
	}
	if (!pCtx->bDbgLoopRunning)
	{
		return FLOC_STATUS_DEBUG_LOOP_ALREADY_STOPPED;
	}
	if (pCtx->bForeignDebugLoop)
	{
		return FLOC_STATUS_DEBUG_LOOP_FOREIGN;
	}

	PROCESS const hProcess = Target_HandleAcquire(pCtx->pidTarget);
	for (U32 i = 0; i < pCtx->vecTrackers.uElemCount; i++)
	{
		TRACKER* const pTracker = (TRACKER*)Vector_AddressOf(&(pCtx->vecTrackers), i);
		if (NULL == pTracker || pTracker->eType != TRACKER_TYPE_BREAKPOINT_SW)
		{
			continue;
		}
		if (pTracker->bEnabled)
		{
			if (NULL == hProcess)
			{
				return FLOC_STATUS_PROCESS_HANDLE_ACQUIRE_FAIL;
			}
			FLOC_TrackerDisable(pTracker, hProcess);
		}
	}
	Target_HandleRelease(hProcess);

	pCtx->bStopDebugLoop = TRUE;

	BOOL bRet = Thread_WaitExit(pCtx->thrDebug, 100);
	if (!bRet)
	{
		bRet = Target_DebugBreak(pCtx->pidTarget);
		if (!bRet)
		{
			return FLOC_STATUS_DEBUG_BREAK_FAIL;
		}
		bRet = Thread_WaitExit(pCtx->thrDebug, 500);
		if (!bRet)
		{
			return FLOC_STATUS_DEBUG_LOOP_STOP_FAIL;
		}
	}

	Thread_Close(pCtx->thrDebug);
	pCtx->bDbgLoopRunning = FALSE;
	pCtx->bStopDebugLoop = FALSE;
	return FLOC_STATUS_SUCCESS;
}

FLOC_STATUS FLOCDLL_DebugLoopOverride(FLOC_HANDLE const hHandle, BOOL const bLoopRunning)
{
	FLOC_CTX* const pCtx = FLOC_ContextGet(hHandle);
	if (NULL == pCtx)
	{
		return FLOC_STATUS_INVALID_HANDLE;
	}
	
	BOOL const bForeign = pCtx->bForeignDebugLoop;
	BOOL const bRunning = pCtx->bDbgLoopRunning;
	BOOL const bOverride = bLoopRunning;

	BOOL bPresent = FALSE;
	BOOL bRet = Target_IsDebuggerAttached(pCtx->pidTarget, &bPresent);
	if (!bRet)
	{
		return FLOC_STATUS_TARGET_CANNOT_CHECK_DEBUGGER;
	}

	/* Caller correctly claims that they turned off their debugger. */
	if (!bPresent && bForeign && bRunning && !bOverride)
	{
		pCtx->bForeignDebugLoop = bLoopRunning;
		pCtx->bDbgLoopRunning = FALSE;
		return FLOC_STATUS_SUCCESS;
	}

	/* Caller correctly claims that they turned on their debugger. */
	if (bPresent && !bForeign && !bRunning && bOverride)
	{
		pCtx->bForeignDebugLoop = bLoopRunning;
		pCtx->bDbgLoopRunning = TRUE;
		return FLOC_STATUS_SUCCESS;
	}

	/* Caller signals that they installed a debugger but we did not find any. */
	if (!bPresent && bOverride)
	{
		return FLOC_STATUS_DEBUGGER_NOT_FOUND;
	}
	
	/* Caller signals that they installed a debugger but we already run our own. */
	if (bPresent && !bForeign && bRunning && bOverride)
	{
		return FLOC_STATUS_FLOC_DEBUG_LOOP_IS_RUNNING;
	}
	
	/* Caller claims they stopped their debugger but it is still attached. */
	if (bPresent && bForeign && bRunning && !bOverride)
	{
		return FLOC_STATUS_DEBUGGER_RUNNING;
	}

	/* Every other case does not have a specific error code. */
	return FLOC_STATUS_ILLOGICAL_OVERRIDE;

}

FLOC_STATUS FLOCDLL_CallExceptionBreakpointHandler(FLOC_HANDLE const hHandle, PID const pidProcess, TID const tidThread, ADDRESS const aAddress)
{
	FLOC_CTX const * const pCtx = FLOC_ContextGet(hHandle);
	if (NULL == pCtx)
	{
		return FLOC_STATUS_INVALID_HANDLE;
	}

	if (!pCtx->bForeignDebugLoop)
	{
		return FLOC_STATUS_FOREIGN_DEBUGGER_NOT_ATTACHED;
	}
	if (!pCtx->bDbgLoopRunning)
	{
		return FLOC_STATUS_DEBUG_LOOP_STOPPED;
	}

	BYTE uOriginalByte = 0;
	BOOL const bShouldRemove = FLOC_BreakpointHandler(pCtx, aAddress, &uOriginalByte);
	if (bShouldRemove)
	{
		Target_BreakpointRemoveTriggered(pidProcess, tidThread, aAddress, uOriginalByte);
	}

	return FLOC_STATUS_SUCCESS;
}

FLOC_STATUS FLOCDLL_TrackerAddBreakpoint(FLOC_HANDLE const hHandle, ADDRESS const aAddress)
{
	FLOC_CTX* const pCtx = FLOC_ContextGet(hHandle);
	if (NULL == pCtx)
	{
		return FLOC_STATUS_INVALID_HANDLE;
	}
	if (0 == pCtx->pidTarget)
	{
		return FLOC_STATUS_TARGET_NOT_SET;
	}

	for (U32 i = 0; i < pCtx->vecTrackers.uElemCount; i++)
	{
		TRACKER const * const pTracker = (TRACKER*)Vector_AddressOf(&(pCtx->vecTrackers), i);
		if (NULL == pTracker)
		{
			continue;
		}
		if (aAddress == pTracker->aAddress)
		{
			return FLOC_STATUS_TRACKER_ALREADY_EXISTS;
		}
	}

	TRACKER tracker;
	tracker.aAddress = aAddress;
	tracker.eType = TRACKER_TYPE_BREAKPOINT_SW;
	tracker.bEnabled = FALSE;
	tracker.bHit = FALSE;

	BYTE uOriginalByte = 0;
	PROCESS const hProcess = Target_HandleAcquire(pCtx->pidTarget);
	if (NULL == hProcess)
	{
		return FLOC_STATUS_PROCESS_HANDLE_ACQUIRE_FAIL;
	}
	if (!Target_MemoryRead(hProcess, tracker.aAddress, &uOriginalByte, 1))
	{
		Target_HandleRelease(hProcess);
		return FLOC_STATUS_MEMORY_READ_FAIL;
	}
	Target_HandleRelease(hProcess);
	tracker.u.bp.uOriginalByte = uOriginalByte;	

	VECTOR* const pvecTrackers = &(pCtx->vecTrackers);
	if (!Vector_PushBackCopy(pvecTrackers, &tracker))
	{
		return FLOC_STATUS_VECTOR_PUSHBACK_FAIL;
	}

	return FLOC_STATUS_SUCCESS;
}

FLOC_STATUS FLOCDLL_TrackerAddHook(FLOC_HANDLE const hHandle, ADDRESS const aAddress, U32 const uFuncLen)
{
	FLOC_CTX* const pCtx = FLOC_ContextGet(hHandle);
	if (NULL == pCtx)
	{
		return FLOC_STATUS_INVALID_HANDLE;
	}
	if (0 == pCtx->pidTarget)
	{
		return FLOC_STATUS_TARGET_NOT_SET;
	}

	for (U32 i = 0; i < pCtx->vecTrackers.uElemCount; i++)
	{
		TRACKER const * const pTracker = (TRACKER*)Vector_AddressOf(&(pCtx->vecTrackers), i);
		if (NULL == pTracker)
		{
			continue;
		}
		if (aAddress == pTracker->aAddress)
		{
			return FLOC_STATUS_TRACKER_ALREADY_EXISTS;
		}
	}

	TRACKER tracker;
	tracker.aAddress = aAddress;
	tracker.eType = TRACKER_TYPE_HOOK_INLINE;
	tracker.bEnabled = FALSE;
	tracker.bHit = FALSE;

	PROCESS const hProcess = Target_HandleAcquire(pCtx->pidTarget);
	if (NULL == hProcess)
	{
		return FLOC_STATUS_PROCESS_HANDLE_ACQUIRE_FAIL;
	}
	VECTOR* const pvecPools = &(pCtx->vecPools);
	if (!Hook_Create(pvecPools, &tracker, hProcess, uFuncLen))
	{
		Target_HandleRelease(hProcess);
		return FLOC_STATUS_HOOK_CREATE_FAIL;
	}
	Target_HandleRelease(hProcess);

	VECTOR* const pvecTrackers = &(pCtx->vecTrackers);
	if (!Vector_PushBackCopy(pvecTrackers, &tracker))
	{
		return FLOC_STATUS_VECTOR_PUSHBACK_FAIL;
	}

	return FLOC_STATUS_SUCCESS;
}

FLOC_STATUS FLOCDLL_TrackerRemove(FLOC_HANDLE const hHandle, ADDRESS const aAddress)
{
	FLOC_CTX const * const pCtx = FLOC_ContextGet(hHandle);
	if (NULL == pCtx)
	{
		return FLOC_STATUS_INVALID_HANDLE;
	}
	
	PROCESS const hProcess = Target_HandleAcquire(pCtx->pidTarget);
	if (NULL == hProcess)
	{
		return FLOC_STATUS_PROCESS_HANDLE_ACQUIRE_FAIL;
	}

	VECTOR const* const pvecTrackers = &(pCtx->vecTrackers);
	U32 const uElemCount = pvecTrackers->uElemCount;
	for (U32 i = 0; i < uElemCount; i++)
	{
		TRACKER* const pTracker = (TRACKER*)Vector_AddressOf(pvecTrackers, i);
		if (NULL == pTracker || aAddress != pTracker->aAddress || TRACKER_TYPE_DELETED == pTracker->eType)
		{
			continue;
		}
		FLOC_TrackerRemove(pTracker, hProcess);
		Target_HandleRelease(hProcess);
		return FLOC_STATUS_SUCCESS;
	}

	Target_HandleRelease(hProcess);
	return FLOC_STATUS_TRACKER_NOT_FOUND;
}

FLOC_EXPORT FLOC_STATUS FLOCDLL_TrackerEnable(FLOC_HANDLE const hHandle, ADDRESS const aAddress)
{
	FLOC_CTX const * const pCtx = FLOC_ContextGet(hHandle);
	if (NULL == pCtx)
	{
		return FLOC_STATUS_INVALID_HANDLE;
	}

	PROCESS const hProcess = Target_HandleAcquire(pCtx->pidTarget);
	if (NULL == hProcess)
	{
		return FLOC_STATUS_PROCESS_HANDLE_ACQUIRE_FAIL;
	}

	VECTOR const* const pvecTrackers = &(pCtx->vecTrackers);
	U32 const uElemCount = pvecTrackers->uElemCount;
	for (U32 i = 0; i < uElemCount; i++)
	{
		TRACKER* const pTracker = (TRACKER*)Vector_AddressOf(pvecTrackers, i);
		if (NULL == pTracker || aAddress != pTracker->aAddress || TRACKER_TYPE_DELETED == pTracker->eType)
		{
			continue;
		}
		if (pTracker->bEnabled)
		{
			Target_HandleRelease(hProcess);
			return FLOC_STATUS_SUCCESS;
		}
		if (TRACKER_TYPE_BREAKPOINT_SW == pTracker->eType && (!pCtx->bDbgLoopRunning || pCtx->bStopDebugLoop))
		{
			Target_HandleRelease(hProcess);
			return FLOC_STATUS_ENABLING_BREAKPOINT_WITHOUT_DEBUGGING;
		}
		FLOC_TrackerEnable(pTracker, hProcess);
		Target_HandleRelease(hProcess);
		return FLOC_STATUS_SUCCESS;
	}

	Target_HandleRelease(hProcess);
	return FLOC_STATUS_TRACKER_NOT_FOUND;
}

FLOC_EXPORT FLOC_STATUS FLOCDLL_TrackerDisable(FLOC_HANDLE const hHandle, ADDRESS const aAddress)
{
	FLOC_CTX const * const pCtx = FLOC_ContextGet(hHandle);
	if (NULL == pCtx)
	{
		return FLOC_STATUS_INVALID_HANDLE;
	}

	PROCESS const hProcess = Target_HandleAcquire(pCtx->pidTarget);
	if (NULL == hProcess)
	{
		return FLOC_STATUS_PROCESS_HANDLE_ACQUIRE_FAIL;
	}

	VECTOR const * const pvecTrackers = &(pCtx->vecTrackers);
	U32 const uElemCount = pvecTrackers->uElemCount;
	for (U32 i = 0; i < uElemCount; i++)
	{
		TRACKER* const pTracker = (TRACKER*)Vector_AddressOf(pvecTrackers, i);
		if (NULL == pTracker || aAddress != pTracker->aAddress || TRACKER_TYPE_DELETED == pTracker->eType)
		{
			continue;
		}
		if (!pTracker->bEnabled)
		{
			Target_HandleRelease(hProcess);
			return FLOC_STATUS_SUCCESS;
		}
		FLOC_TrackerDisable(pTracker, hProcess);
		Target_HandleRelease(hProcess);
		return FLOC_STATUS_SUCCESS;
	}

	Target_HandleRelease(hProcess);
	return FLOC_STATUS_TRACKER_NOT_FOUND;
}

FLOC_STATUS FLOCDLL_TrackerAllGet(FLOC_HANDLE const hHandle, VECTOR const ** const ppVec)
{
	FLOC_CTX const * const pCtx = FLOC_ContextGet(hHandle);
	if (NULL == pCtx)
	{
		return FLOC_STATUS_INVALID_HANDLE;
	}
	*ppVec = &(pCtx->vecTrackers);
	return FLOC_STATUS_SUCCESS;
}

FLOC_STATUS FLOCDLL_TrackerAllReset(FLOC_HANDLE const hHandle)
{
	FLOC_CTX* const pCtx = FLOC_ContextGet(hHandle);
	if (NULL == pCtx)
	{
		return FLOC_STATUS_INVALID_HANDLE;
	}

	if (pCtx->bIsStepActive)
	{
		return FLOC_STATUS_STEP_ACTIVE;
	}

	VECTOR const * const pvecTrackers = &(pCtx->vecTrackers);
	U32 const uElemCount = pvecTrackers->uElemCount;
	for (U32 i = 0; i < uElemCount; i++)
	{
		TRACKER* const pTracker = (TRACKER*)Vector_AddressOf(pvecTrackers, i);
		if (NULL == pTracker)
		{
			continue;
		}
		pTracker->bHit = FALSE;
	}

	pCtx->bIsPendingReset = FALSE;
	return FLOC_STATUS_SUCCESS;
}

FLOC_STATUS FLOCDLL_TrackerAllEnable(FLOC_HANDLE const hHandle)
{
	FLOC_CTX const * const pCtx = FLOC_ContextGet(hHandle);
	if (NULL == pCtx)
	{
		return FLOC_STATUS_INVALID_HANDLE;
	}

	PROCESS const hProcess = Target_HandleAcquire(pCtx->pidTarget);
	if (NULL == hProcess)
	{
		return FLOC_STATUS_PROCESS_HANDLE_ACQUIRE_FAIL;
	}

	FLOC_STATUS status = FLOC_STATUS_SUCCESS;

	VECTOR const* const pvecTrackers = &(pCtx->vecTrackers);
	U32 const uElemCount = pvecTrackers->uElemCount;
	for (U32 i = 0; i < uElemCount; i++)
	{
		TRACKER* const pTracker = (TRACKER*)Vector_AddressOf(pvecTrackers, i);
		if (NULL == pTracker || TRACKER_TYPE_DELETED == pTracker->eType)
		{
			continue;
		}
		if (pTracker->bEnabled)
		{
			continue;
		}
		if (TRACKER_TYPE_BREAKPOINT_SW == pTracker->eType && (!pCtx->bDbgLoopRunning || pCtx->bStopDebugLoop))
		{
			status = FLOC_STATUS_ENABLING_BREAKPOINT_WITHOUT_DEBUGGING;
			continue;
		}
		FLOC_TrackerEnable(pTracker, hProcess);
	}
	
	Target_HandleRelease(hProcess);
	return status;
}

FLOC_STATUS FLOCDLL_TrackerAllDisable(FLOC_HANDLE const hHandle)
{
	FLOC_CTX const * const pCtx = FLOC_ContextGet(hHandle);
	if (NULL == pCtx)
	{
		return FLOC_STATUS_INVALID_HANDLE;
	}

	if (pCtx->bIsStepActive)
	{
		return FLOC_STATUS_STEP_ACTIVE;
	}

	PROCESS const hProcess = Target_HandleAcquire(pCtx->pidTarget);
	if (NULL == hProcess)
	{
		return FLOC_STATUS_PROCESS_HANDLE_ACQUIRE_FAIL;
	}

	for (U32 i = 0; i < pCtx->vecTrackers.uElemCount; i++)
	{
		TRACKER* const pTracker = (TRACKER*)Vector_AddressOf(&(pCtx->vecTrackers), i);
		if (NULL == pTracker || TRACKER_TYPE_DELETED == pTracker->eType)
		{
			continue;
		}
		if (pTracker->bEnabled)
		{
			FLOC_TrackerDisable(pTracker, hProcess);
		}
	}

	Target_HandleRelease(hProcess);
	return FLOC_STATUS_SUCCESS;
}

FLOC_STATUS FLOCDLL_StepBegin(FLOC_HANDLE const hHandle)
{
	FLOC_CTX * const pCtx = FLOC_ContextGet(hHandle);
	if (NULL == pCtx)
	{
		return FLOC_STATUS_INVALID_HANDLE;
	}

	if (pCtx->bIsStepActive)
	{
		return FLOC_STATUS_STEP_ALREADY_ACTIVE;
	}
	if (FLOC_IsTargetDead(pCtx))
	{
		return FLOC_STATUS_TARGET_DIED;
	}
	if (pCtx->bIsPendingReset && FLOC_STATUS_SUCCESS != FLOCDLL_TrackerAllReset(hHandle))
	{
		return FLOC_STATUS_TRACKER_RESET_FAIL;
	}
	
	pCtx->bIsPendingReset = FALSE;
	pCtx->bIsStepActive = TRUE;
	return FLOC_STATUS_SUCCESS;
}

FLOC_STATUS FLOCDLL_StepEnd(FLOC_HANDLE const hHandle)
{
	FLOC_CTX* const pCtx = FLOC_ContextGet(hHandle);
	if (NULL == pCtx)
	{
		return FLOC_STATUS_INVALID_HANDLE;
	}

	if (FLOC_IsTargetDead(pCtx))
	{
		return FLOC_STATUS_TARGET_DIED;
	}
	if (!pCtx->bIsStepActive)
	{
		return FLOC_STATUS_STEP_ALREADY_STOPPED;
	}

	pCtx->bIsStepActive = FALSE;
	pCtx->bIsPendingReset = TRUE;

	PROCESS const hProcess = Target_HandleAcquire(pCtx->pidTarget);
	if (NULL == hProcess)
	{
		return FLOC_STATUS_PROCESS_HANDLE_ACQUIRE_FAIL;
	}
	VECTOR const * const pvecTrackers = &(pCtx->vecTrackers);
	for (U32 i = 0; i < pvecTrackers->uElemCount; i++)
	{
		TRACKER* const pTracker = (TRACKER*)Vector_AddressOf(pvecTrackers, i);

		/* While breakpoints are aware of the Step status when triggered, hooks are not. */
		if (NULL == pTracker || TRACKER_TYPE_HOOK_INLINE != pTracker->eType || !pTracker->bEnabled)
		{
			continue;
		}
		pTracker->bHit = Hook_IsHit(pTracker, hProcess);
	}
	Target_HandleRelease(hProcess);

	return FLOC_STATUS_SUCCESS;
}

FLOC_STATUS FLOCDLL_StepFilterOutExecuted(FLOC_HANDLE const hHandle)
{
	FLOC_CTX* const pCtx = FLOC_ContextGet(hHandle);
	if (NULL == pCtx)
	{
		return FLOC_STATUS_INVALID_HANDLE;
	}
	if (pCtx->bIsStepActive)
	{
		return FLOC_STATUS_STEP_ACTIVE;
	}
	FLOC_StepFilterOut(pCtx, TRUE);
	return FLOC_STATUS_SUCCESS;
}

FLOC_STATUS FLOCDLL_StepFilterOutNotExecuted(FLOC_HANDLE const hHandle)
{
	FLOC_CTX* const pCtx = FLOC_ContextGet(hHandle);
	if (NULL == pCtx)
	{
		return FLOC_STATUS_INVALID_HANDLE;
	}
	if (pCtx->bIsStepActive)
	{
		return FLOC_STATUS_STEP_ACTIVE;
	}
	FLOC_StepFilterOut(pCtx, FALSE);
	return FLOC_STATUS_SUCCESS;
}
