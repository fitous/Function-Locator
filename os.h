#ifndef OS_H
#define OS_H

#include "types.h"

#ifdef _WIN32
typedef unsigned long PID;
typedef unsigned long TID;
typedef void* THREAD;
typedef void (*THREAD_INIT_FUNC)(void*);
typedef BOOL (*BREAKPOINT_HANDLER_FUNC)(void*, ADDRESS, BYTE*);
typedef void* PROCESS;
#define DISTANCE_NEAR (0x7FFFFFFF) /* 2GB - 1 */
#endif /* _WIN32 */

#ifdef LINUX
#error "Linux support not implemented"
#endif /* LINUX */

BOOL Process_CheckPrivileges(void);

void* Memory_Alloc(U64 uSize);
BOOL Memory_Free(void* address);
void* Memory_Copy(void* pDest, void const* pSrc, U64 uLen);

BOOL Target_Is64bit(PID pidTarget);
BOOL Target_DebuggerAttach(PID pidTarget);
BOOL Target_DebuggerDetach(PID pidTarget);
BOOL Target_IsDebuggerAttached(PID pidTarget, BOOL* pbDebuggerPresent);

BOOL Target_WaitForBreakpoint(BREAKPOINT_HANDLER_FUNC pBreakpointHandler, void* pParam);
BOOL Target_DebugBreak(PID pidTarget);

BOOL Target_BreakpointAdd(PROCESS hProcess, ADDRESS aAddress);
void Target_BreakpointRemoveTriggered(PID pidTarget, TID tidThread, ADDRESS aAddress, BYTE uOriginalByte);
void Target_BreakpointRemoveDormant(PROCESS hProcess, ADDRESS aAddress, BYTE uOriginalByte);

PROCESS Target_HandleAcquire(PID pidTarget);
BOOL Target_HandleRelease(PROCESS hProcess);

BOOL Target_MemoryRead(PROCESS hProcess, ADDRESS aSrc, void* pDest, U64 uLen);
BOOL Target_MemoryWrite(PROCESS hProcess, ADDRESS aDest, void const * pSrc, U64 uLen);
BOOL Target_MemoryWriteFlush(PROCESS hProcess, ADDRESS aDest, void const * pSrc, U64 uLen);
BOOL Target_MemoryUnprotect(PROCESS hProcess, ADDRESS address, U64 uLen);
ADDRESS Target_MemoryAllocExec(PROCESS hProcess, U64 uLen);
ADDRESS Target_MemoryAllocExecNear(PROCESS hProcess, ADDRESS aAddressNear, U64 uNearDistance, U64 uMinimumSize, U64* puSize);
void Target_MemoryFree(PROCESS hProcess, ADDRESS address);

BOOL Thread_Start(THREAD_INIT_FUNC fnFunc, void* pParam, THREAD* pThread);
BOOL Thread_WaitExit(THREAD hThread, U32 uTimeoutMS);
BOOL Thread_Close(THREAD hThread);

#endif /* OS_H */
