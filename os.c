#include "os.h"

typedef struct tdTHREAD_INIT_INFO {
	THREAD_INIT_FUNC fnFunc;
	void* pParam;
} THREAD_INIT_INFO;

#define INT3_BYTE (0xCC)

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

BOOL WINAPI DllMain(HANDLE hHandle, DWORD dwReason, LPVOID lpReserved);
static BOOL Process_EnableDebugPrivilege(void);
static DWORD WINAPI Thread_Init(void* lpParam);
static ADDRESS FindPrevFreeRegion(PROCESS hProcess, ADDRESS aAddress, ADDRESS aMin, U32 uAllocationGranularity, U64* puRegionSize);
static ADDRESS FindNextFreeRegion(PROCESS hProcess, ADDRESS aAddress, ADDRESS aMax, U32 uAllocationGranularity, U64* puRegionSize);

BOOL WINAPI DllMain(HANDLE const hHandle, DWORD const dwReason, LPVOID const lpReserved)
{
	(void)lpReserved;
	if (DLL_PROCESS_ATTACH == dwReason)
	{
		DisableThreadLibraryCalls((HMODULE)hHandle);
	}
	return TRUE;
}

static BOOL Process_EnableDebugPrivilege(void)
{
	HANDLE hToken = NULL;
	if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken))
	{
		return FALSE;
	}
	LUID luid;
	if (!LookupPrivilegeValue(NULL, SE_DEBUG_NAME, &luid))
	{
		CloseHandle(hToken);
		return FALSE;
	}

	TOKEN_PRIVILEGES tp;
	tp.PrivilegeCount = 1;
	tp.Privileges[0].Luid = luid;
	tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

	BOOL const bRes = AdjustTokenPrivileges(hToken, FALSE, &tp, sizeof(TOKEN_PRIVILEGES), NULL, NULL);
	CloseHandle(hToken);
	return bRes;
}

BOOL Process_CheckPrivileges(void)
{
	return Process_EnableDebugPrivilege();
}

void* Memory_Alloc(U64 const uSize)
{
    return HeapAlloc(GetProcessHeap(), 0, uSize);
}

BOOL Memory_Free(void* const pAddress)
{
    return HeapFree(GetProcessHeap(), 0, pAddress);
}

void* Memory_Copy(void* const pDest, void const * const pSrc, U64 uLen)
{
    BYTE* d = pDest;
    BYTE const* s = pSrc;
    while (uLen--)
        *d++ = *s++;
    return pDest;
}

BOOL Target_Is64bit(PID const pidTarget)
{
	HANDLE const hProcess = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, pidTarget);
	if (NULL == hProcess)
	{
		return FALSE;
	}

	BOOL bIsWow64bit = FALSE;
	if (!IsWow64Process(hProcess, &bIsWow64bit))
	{
		CloseHandle(hProcess);
		return FALSE;
	}

	CloseHandle(hProcess);
	return !bIsWow64bit;
}

BOOL Target_DebuggerAttach(PID const pidTarget)
{
	if (!DebugActiveProcess(pidTarget))
	{
		return FALSE;
	}
	if (!DebugSetProcessKillOnExit(FALSE))
	{
		DebugActiveProcessStop(pidTarget);
		return FALSE;
	}
	return TRUE;
}

BOOL Target_DebuggerDetach(PID const pidTarget)
{
    return DebugActiveProcessStop(pidTarget);
}

BOOL Target_IsDebuggerAttached(PID const pidTarget, BOOL* const pbDebuggerPresent)
{
	HANDLE const hProcess = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, pidTarget);
	if (hProcess == NULL)
	{
		return FALSE;
	}

	BOOL bIsDbgPresent = FALSE;
	if (!CheckRemoteDebuggerPresent(hProcess, &bIsDbgPresent))
	{
		CloseHandle(hProcess);
		return FALSE;
	}

	CloseHandle(hProcess);
	*pbDebuggerPresent = bIsDbgPresent;
	return TRUE;
}

BOOL Target_MemoryRead(PROCESS const hProcess, ADDRESS const aSrc, void* const pDest, U64 const uLen)
{
	return ReadProcessMemory(hProcess, (LPCVOID)aSrc, pDest, uLen, NULL);
}

BOOL Target_MemoryWrite(PROCESS const hProcess, ADDRESS const aDest, void const * const pSrc, U64 const uLen)
{
	return WriteProcessMemory(hProcess, (LPVOID)aDest, pSrc, uLen, NULL);
}

BOOL Target_MemoryWriteFlush(PROCESS const hProcess, ADDRESS const aDest, void const * const pSrc, U64 const uLen)
{
	if (!WriteProcessMemory(hProcess, (LPVOID)aDest, pSrc, uLen, NULL))
	{
		return FALSE;
	}
	FlushInstructionCache(hProcess, (LPCVOID)aDest, uLen);
	return TRUE;
}

BOOL Target_MemoryUnprotect(PROCESS const hProcess, ADDRESS const address, U64 const uLen)
{
	DWORD dwOldProtect;
	return VirtualProtectEx(hProcess, (LPVOID)address, uLen, PAGE_EXECUTE_READWRITE, &dwOldProtect);
}

ADDRESS Target_MemoryAllocExec(PROCESS const hProcess, U64 const uLen)
{
	return (ADDRESS)VirtualAllocEx(hProcess, NULL, uLen, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
}

static ADDRESS FindPrevFreeRegion(PROCESS const hProcess, ADDRESS const aAddress, ADDRESS const aMin, U32 const uAllocationGranularity, U64* const puRegionSize)
{
	ADDRESS aTry = aAddress;
	aTry -= aTry % uAllocationGranularity;
	aTry -= uAllocationGranularity;
	while (aTry >= aMin)
	{
		MEMORY_BASIC_INFORMATION mbi;
		if (0 == VirtualQueryEx(hProcess, (LPVOID)aTry, &mbi, sizeof(mbi)))
		{
			break;
		}
		if (MEM_FREE == mbi.State)
		{
			*puRegionSize = mbi.RegionSize;
			return aTry;
		}
		if ((U64)mbi.AllocationBase < uAllocationGranularity)
		{
			break;
		}
		aTry = (U64)mbi.AllocationBase - uAllocationGranularity;
	}

	return NULL;
}

static ADDRESS FindNextFreeRegion(PROCESS const hProcess, ADDRESS const aAddress, ADDRESS const aMax, U32 const uAllocationGranularity, U64* const puRegionSize)
{
	ADDRESS aTry = aAddress;
	aTry -= aTry % uAllocationGranularity;
	aTry += uAllocationGranularity;

	while (aTry <= aMax)
	{
		MEMORY_BASIC_INFORMATION mbi;
		if (0 == VirtualQueryEx(hProcess, (LPVOID)aTry, &mbi, sizeof(mbi)))
		{
			break;
		}
		if (MEM_FREE == mbi.State)
		{
			*puRegionSize = mbi.RegionSize;
			return aTry;
		}
		aTry = (U64)mbi.BaseAddress + mbi.RegionSize;
		aTry += uAllocationGranularity - 1;
		aTry -= aTry % uAllocationGranularity;
	}

	return NULL;
}

ADDRESS Target_MemoryAllocExecNear(PROCESS const hProcess, ADDRESS const aAddressNear, U64 const uNearDistance, U64 const uMinimumSize, U64* const puSize)
{
	SYSTEM_INFO si;
	GetSystemInfo(&si);
	ADDRESS aMin = (ADDRESS)si.lpMinimumApplicationAddress;
	ADDRESS aMax = (ADDRESS)si.lpMaximumApplicationAddress;

	if (aMin < aAddressNear - uNearDistance)
	{
		aMin = aAddressNear - uNearDistance;
	}
	if (aMax > aAddressNear + uNearDistance)
	{
		aMax = aAddressNear + uNearDistance;
	}

	ADDRESS aAlloc = NULL;
	ADDRESS aCurrent = aAddressNear;
	U64 uRegionSize = 0;
	while (aCurrent >= aMin)
	{
		aCurrent = FindPrevFreeRegion(hProcess, aCurrent, aMin, si.dwAllocationGranularity, &uRegionSize);
		if (NULL == aCurrent)
		{
			break;
		}
		if (uRegionSize < uMinimumSize)
		{
			continue;
		}
		aAlloc = (ADDRESS)VirtualAllocEx(hProcess, (LPVOID)aCurrent, uRegionSize, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
		if (NULL != aAlloc)
		{
			*puSize = uRegionSize;
			return aAlloc;
		}
	}	

	aCurrent = aAddressNear;
	while (aCurrent <= aMax)
	{
		aCurrent = FindNextFreeRegion(hProcess, aCurrent, aMax, si.dwAllocationGranularity, &uRegionSize);
		if (NULL == aCurrent)
		{
			break;
		}
		if (uRegionSize < uMinimumSize)
		{
			continue;
		}
		aAlloc = (ADDRESS)VirtualAllocEx(hProcess, (LPVOID)aCurrent, uRegionSize, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
		if (NULL != aAlloc)
		{
			*puSize = uRegionSize;
			return aAlloc;
		}
	}

	return NULL;
}

BOOL Target_DebugBreak(PID const pidTarget)
{
	HANDLE const hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pidTarget);
	if (NULL == hProcess)
	{
		return FALSE;
	}
	BOOL const bRes = DebugBreakProcess(hProcess);
	CloseHandle(hProcess);
	return !!bRes;
}

BOOL Target_BreakpointAdd(PROCESS const hProcess, ADDRESS const aAddress)
{
	BYTE const byte = INT3_BYTE;
	if (!WriteProcessMemory(hProcess, (LPVOID)aAddress, &byte, 1, NULL))
	{
		return FALSE;
	}
	FlushInstructionCache(hProcess, (LPCVOID)aAddress, 1);
	return TRUE;
}

void Target_BreakpointRemoveTriggered(PID const pidProcess, TID const tidThread, ADDRESS const aAddress, BYTE const uOriginalByte)
{
	HANDLE const hThread = OpenThread(THREAD_ALL_ACCESS, FALSE, tidThread);
	if (NULL == hThread)
	{
		return;
	}

	CONTEXT threadContext;
	threadContext.ContextFlags = CONTEXT_ALL;
	if (!GetThreadContext(hThread, &threadContext))
	{
		goto ret;
	}
	threadContext.Rip -= 1;
	if (!SetThreadContext(hThread, &threadContext))
	{
		goto ret;
	}

	BYTE const byte = uOriginalByte;
	HANDLE const hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pidProcess);
	if (NULL == hProcess)
	{
		goto ret;
	}

	WriteProcessMemory(hProcess, (LPVOID)aAddress, &byte, 1, NULL);
	FlushInstructionCache(hProcess, (LPCVOID)aAddress, 1);
	CloseHandle(hProcess);

ret:
	CloseHandle(hThread);
	return;
}

void Target_BreakpointRemoveDormant(PROCESS const hProcess, ADDRESS const aAddress, BYTE const uOriginalByte)
{
	BYTE const byte = uOriginalByte;
	WriteProcessMemory(hProcess, (LPVOID)aAddress, &byte, 1, NULL);
	FlushInstructionCache(hProcess, (LPCVOID)aAddress, 1);
}

PROCESS Target_HandleAcquire(PID const pidProcess)
{
	HANDLE const hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pidProcess);
	if (NULL == hProcess)
	{
		return NULL;
	}
	return hProcess;
}

BOOL Target_HandleRelease(PROCESS process)
{
	return CloseHandle(process);
}

BOOL Thread_Start(THREAD_INIT_FUNC const fnFunc, void* const pParam, THREAD* const pThread)
{
	THREAD_INIT_INFO* const pInitInfo = Memory_Alloc(sizeof(THREAD_INIT_INFO));
	if (NULL == pInitInfo)
	{
		*pThread = NULL;
		return FALSE;
	}

	pInitInfo->fnFunc = fnFunc;
	pInitInfo->pParam = pParam;
	
	HANDLE const hThread = CreateThread(NULL, 0, Thread_Init, pInitInfo, 0, NULL);
	if (NULL == hThread)
	{
		*pThread = NULL;
		return FALSE;
	}

	*pThread = hThread;
	return TRUE;
}

static DWORD WINAPI Thread_Init(void* lpParam)
{
	THREAD_INIT_INFO* const pInitInfo = (THREAD_INIT_INFO*)lpParam;
	pInitInfo->fnFunc(pInitInfo->pParam);
	Memory_Free(pInitInfo);
	return 0;
}

BOOL Thread_WaitExit(THREAD const hThread, U32 const uTimeoutMS)
{
	return WAIT_OBJECT_0 == WaitForSingleObject(hThread, uTimeoutMS);
}

BOOL Thread_Close(THREAD const hThread)
{
	return CloseHandle(hThread);
}

BOOL Target_WaitForBreakpoint(BREAKPOINT_HANDLER_FUNC const pBreakpointHandler, void* const pParam)
{
	DEBUG_EVENT debugEvent;
	DWORD dwContinueStatus = DBG_CONTINUE;
	BOOL bTargetDied = FALSE;

	WaitForDebugEvent(&debugEvent, INFINITE);

	switch(debugEvent.dwDebugEventCode)
	{
		case EXCEPTION_DEBUG_EVENT:
			if(EXCEPTION_BREAKPOINT == debugEvent.u.Exception.ExceptionRecord.ExceptionCode)
			{
				ADDRESS const aAddress = (ADDRESS)debugEvent.u.Exception.ExceptionRecord.ExceptionAddress;
				BYTE uOriginalByte = 0;
				BOOL bRemoveBreakpoint = pBreakpointHandler(pParam, aAddress, &uOriginalByte);
				if (bRemoveBreakpoint)
				{
					Target_BreakpointRemoveTriggered(debugEvent.dwProcessId, debugEvent.dwThreadId, aAddress, uOriginalByte);
				}
			}
			else
			{
				dwContinueStatus = DBG_EXCEPTION_NOT_HANDLED;
			}
			break;

		case CREATE_THREAD_DEBUG_EVENT:
			if (NULL != debugEvent.u.CreateThread.hThread)
			{
				CloseHandle(debugEvent.u.CreateThread.hThread);
			}
			break;

		case CREATE_PROCESS_DEBUG_EVENT:
			if (NULL != debugEvent.u.CreateProcessInfo.hFile)
			{
				CloseHandle(debugEvent.u.CreateProcessInfo.hFile);
			}
			if (NULL != debugEvent.u.CreateProcessInfo.hProcess)
			{
				CloseHandle(debugEvent.u.CreateProcessInfo.hProcess);
			}
			if (NULL != debugEvent.u.CreateProcessInfo.hThread)
			{
				CloseHandle(debugEvent.u.CreateProcessInfo.hThread);
			}
			break;

		case LOAD_DLL_DEBUG_EVENT:
			if (NULL != debugEvent.u.LoadDll.hFile)
			{
				CloseHandle(debugEvent.u.LoadDll.hFile);
			}
			break;

		case EXIT_PROCESS_DEBUG_EVENT:
		case RIP_EVENT:
			bTargetDied = TRUE;
			break;

		default:
			break;
	}

	ContinueDebugEvent(debugEvent.dwProcessId, debugEvent.dwThreadId, dwContinueStatus);
	return bTargetDied;
}

void Target_MemoryFree(PROCESS const hProcess, ADDRESS const address)
{
	VirtualFreeEx(hProcess, (LPVOID)address, 0, MEM_RELEASE);
}

#endif /* _WIN32 */

#ifdef LINUX
#error "Linux support is not implemented"
#endif /* LINUX */
