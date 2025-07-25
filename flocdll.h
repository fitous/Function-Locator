#ifndef FLOCDLL_H
#define FLOCDLL_H

#if !defined(_M_X64) && !defined(__amd64__) && !defined(__x86_64__)
#error Invalid architecture. Only x86-64 is supported.
#endif

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#ifdef _WIN32
#define FLOC_EXPORT
#endif /* _WIN32 */

#ifdef LINUX
#define FLOC_EXPORT __attribute__((visibility("default")))
#endif /* LINUX */

#include "types.h"
#include "status.h"
#include "os.h"

struct tdFLOC_HANDLE;
typedef struct tdFLOC_HANDLE* FLOC_HANDLE;

struct tdVECTOR;
typedef struct tdVECTOR VECTOR;

FLOC_EXPORT FLOC_STATUS FLOCDLL_Initialize(FLOC_HANDLE* phHandle);
FLOC_EXPORT FLOC_STATUS FLOCDLL_Uninitialize(FLOC_HANDLE hHandle);
FLOC_EXPORT FLOC_STATUS FLOCDLL_TargetSet(FLOC_HANDLE hHandle, PID pidTarget);

FLOC_EXPORT FLOC_STATUS FLOCDLL_DebugLoopStart(FLOC_HANDLE hHandle);
FLOC_EXPORT FLOC_STATUS FLOCDLL_DebugLoopOverride(FLOC_HANDLE hHandle, BOOL bLoopRunning);
FLOC_EXPORT FLOC_STATUS FLOCDLL_DebugLoopStop(FLOC_HANDLE hHandle);
FLOC_EXPORT FLOC_STATUS FLOCDLL_CallExceptionBreakpointHandler(FLOC_HANDLE hHandle, PID pidTarget, TID tidThread, ADDRESS aAddress);

FLOC_EXPORT FLOC_STATUS FLOCDLL_TrackerAddBreakpoint(FLOC_HANDLE hHandle, ADDRESS aAddress);
FLOC_EXPORT FLOC_STATUS FLOCDLL_TrackerAddHook(FLOC_HANDLE hHandle, ADDRESS aAddress, U32 uFuncLen);
FLOC_EXPORT FLOC_STATUS FLOCDLL_TrackerRemove(FLOC_HANDLE hHandle, ADDRESS aAddress);
FLOC_EXPORT FLOC_STATUS FLOCDLL_TrackerEnable(FLOC_HANDLE hHandle, ADDRESS aAddress);
FLOC_EXPORT FLOC_STATUS FLOCDLL_TrackerDisable(FLOC_HANDLE hHandle, ADDRESS aAddress);

FLOC_EXPORT FLOC_STATUS FLOCDLL_TrackerAllGet(FLOC_HANDLE hHandle, VECTOR const ** ppVec);
FLOC_EXPORT FLOC_STATUS FLOCDLL_TrackerAllReset(FLOC_HANDLE hHandle);
FLOC_EXPORT FLOC_STATUS FLOCDLL_TrackerAllEnable(FLOC_HANDLE hHandle);
FLOC_EXPORT FLOC_STATUS FLOCDLL_TrackerAllDisable(FLOC_HANDLE hHandle);

FLOC_EXPORT FLOC_STATUS FLOCDLL_StepBegin(FLOC_HANDLE hHandle);
FLOC_EXPORT FLOC_STATUS FLOCDLL_StepEnd(FLOC_HANDLE hHandle);
FLOC_EXPORT FLOC_STATUS FLOCDLL_StepFilterOutExecuted(FLOC_HANDLE hHandle);
FLOC_EXPORT FLOC_STATUS FLOCDLL_StepFilterOutNotExecuted(FLOC_HANDLE hHandle);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* FLOCDLL_H */
