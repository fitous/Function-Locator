#include "hook.h"
#include "pool.h"
#include "tracker.h"
#include "vector.h"

typedef signed long I32;

static BOOL CreateHookRel32(TRACKER* pTracker, POOL* pPool, PROCESS hProcess);
static BOOL CreateHookAbs64(TRACKER* pTracker, POOL* pPool, PROCESS hProcess);
static I32 CalcSignedDisplacement32(U64 a, U64 b);

static I32 CalcSignedDisplacement32(U64 const a, U64 const b)
{
	U64 const uAbsDiff = (a > b) ? (a - b) : (b - a);
	return (a > b) ? (-1) * (I32)uAbsDiff : (I32)uAbsDiff;
}

static BOOL CreateHookRel32(TRACKER* const pTracker, POOL* const pPool, PROCESS const hProcess)
{
	ADDRESS const aFunction = pTracker->aAddress;
	ADDRESS const aHook = pTracker->u.hook.aHookAddress;
	U32 const uJumpLen = JUMP_REL32_LEN;
	pTracker->u.hook.uJumpBytesLen = uJumpLen;

	BYTE bufOriginalBytes[JUMP_REL32_LEN];
	BYTE bufJump[JUMP_REL32_LEN];
	BYTE bufHook[30];

	if (!Target_MemoryRead(hProcess, aFunction, bufOriginalBytes, uJumpLen))
	{
		return FALSE;
	}

	/*
	 * JUMP TO HOOK
	 * 
	 * 0: E9 xx xx xx xx
	 * jmp rel32 (RIP = RIP + rel32)
	 * xx is displacement from RIP to aHook
	 */	
	bufJump[0] = 0xE9;
	I32 iDisplacement = CalcSignedDisplacement32(aFunction + uJumpLen, aHook);
	*(I32*)&bufJump[1] = iDisplacement;
	Memory_Copy(pTracker->u.hook.uJumpBytes, bufJump, uJumpLen);

	/*
	 * SET HIT BYTE, REMOVE HOOK, JUMP BACK
	 * 
	 * 0x0: C6 05 xx xx xx xx 01 
	 * mov BYTE PTR [rip+xx], 0x1
	 * xx is displacement from RIP to hit byte
	 *
	 * 0x7: C7 05 xx xx xx xx AA BB CC DD 
	 * mov DWORD PTR [rip+xx], 0xDDCCBBAA
	 * xx is displacement from RIP to aFunction
	 * AA-DD are function original bytes 0-4
	 * 
	 * 0x11: C6 05 xx xx xx xx EE
	 * mov BYTE PTR [rip+xx], 0xEE
	 * xx is displacement from RIP to (aFunction + 4)
	 * EE is fifth original function byte
	 *
	 * 0x18: E9 xx xx xx xx
	 * jmp rel32 (RIP = RIP + rel32)
	 * xx is displacement from RIP to aFunction
	 * 
	 * 0x1D: hit byte
	 */
	bufHook[0x0] = 0xC6;
	bufHook[0x1] = 0x05;
	*(I32*)&bufHook[2] = CalcSignedDisplacement32(aHook + 0x07, aHook + 0x1D);
	bufHook[0x6] = 0x01;

	bufHook[0x7] = 0xC7;
	bufHook[0x8] = 0x05;
	*(I32*)&bufHook[0x9] = CalcSignedDisplacement32(aHook + 0x11, aFunction);

	bufHook[0xD] = bufOriginalBytes[0];
	bufHook[0xE] = bufOriginalBytes[1];
	bufHook[0xF] = bufOriginalBytes[2];
	bufHook[0x10] = bufOriginalBytes[3];

	bufHook[0x11] = 0xC6;
	bufHook[0x12] = 0x05;
	*(I32*)&bufHook[0x13] = CalcSignedDisplacement32(aHook + 0x18, aFunction + 4);
	bufHook[0x17] = bufOriginalBytes[4];

	bufHook[0x18] = 0xE9;
	*(I32*)&bufHook[0x19] = CalcSignedDisplacement32(aHook + 0x1D, aFunction);

	bufHook[0x1D] = FALSE;

	pTracker->u.hook.uHitOffset = 0x1D;

	if (!Target_MemoryWriteFlush(hProcess, pPool->aCurrentFreeAddress, bufHook, sizeof(bufHook)))
	{
		return FALSE;
	}
	if (!Target_MemoryUnprotect(hProcess, pTracker->aAddress, pTracker->u.hook.uJumpBytesLen))
	{
		return FALSE;
	}

	pPool->uFreeSize -= sizeof(bufHook);
	pPool->aCurrentFreeAddress += sizeof(bufHook);
	return TRUE;
}

static BOOL CreateHookAbs64(TRACKER* const pTracker, POOL* const pPool, PROCESS const hProcess)
{
	ADDRESS const aFunction = pTracker->aAddress;
	ADDRESS const aHook = pTracker->u.hook.aHookAddress;
	U32 const uJumpLen = JUMP_ABS64_LEN;
	pTracker->u.hook.uJumpBytesLen = uJumpLen;

	BYTE bufOriginalBytes[JUMP_ABS64_LEN + 2];
	BYTE bufJump[JUMP_ABS64_LEN];
	BYTE bufHook[64];

	if (!Target_MemoryRead(hProcess, aFunction, bufOriginalBytes, uJumpLen + 2))
	{
		return FALSE;
	}

	/*
	 * JUMP TO HOOK
	 * 0: FF 25 00 00 00 00 xx xx xx xx xx xx xx xx
	 * FF /4, jmp r/m64 (RIP = [RIP+rel32] = [RIP+0])
	 * zeroed out rel32 to read address from [RIP]
	 * xx follows after opcode, is the absolute address of hook
	 */
	bufJump[0x0] = 0xFF;
	bufJump[0x1] = 0x25;
	bufJump[0x2] = 0x00;
	bufJump[0x3] = 0x00;
	bufJump[0x4] = 0x00;
	bufJump[0x5] = 0x00;
	*(U64*)&bufJump[0x6] = aHook;
	Memory_Copy(pTracker->u.hook.uJumpBytes, bufJump, uJumpLen);

	/*
	 * SET HIT BYTE, REMOVE HOOK, JUMP BACK
	 * 0x0: C6 05 xx xx xx xx 01
	 * mov BYTE PTR [rip+xx], 0x1
	 * xx is displacement from RIP to bHIT byte
	 *
	 * 0x7: 50
	 * pushq rax
	 * 
	 * 0x8: 48 B8 77 66 55 44 33 22 11 00
	 * movabs rax, 0x0011223344556677
	 * 11-77 are function original bytes 0-7
	 *
	 * 0x12: 48 A3 xx xx xx xx xx xx xx xx
	 * movabs xx, rax
	 * xx is absolute address of aFunction
	 * 
	 * 0x1C: 48 B8 FF EE DD CC BB AA 99 88
	 * movabs rax, 0x8899AABBCCDDEEFF
	 * 88-FF are function original bytes 8-F
	 *
	 * 0x26: 48 A3 xx xx xx xx xx xx xx xx
	 * movabs xx, rax
	 * xx is absolute address of aFunction + 8
	 * 
	 * 0x30: 58
	 * popq rax
	 *
	 * 0x31: FF 25 00 00 00 00 xx xx xx xx xx xx xx xx
	 * FF /4, jmp r/m64 (RIP = [RIP+rel32] = [RIP+0])
	 * zeroed out rel32 to read address from [RIP]
	 * xx follows after opcode, is the absolute address of aFunction
	 *
	 * 0x3F: bHIT
	 */
	bufHook[0x0] = 0xC6;
	bufHook[0x1] = 0x05;
	*(I32*)&bufHook[2] = CalcSignedDisplacement32(aHook + 0x07, aHook + 0x3F);
	bufHook[0x6] = 0x01;

	bufHook[0x7] = 0x50;

	bufHook[0x8] = 0x48;
	bufHook[0x9] = 0xB8;
	bufHook[0xA] = bufOriginalBytes[0];
	bufHook[0xB] = bufOriginalBytes[1];
	bufHook[0xC] = bufOriginalBytes[2];
	bufHook[0xD] = bufOriginalBytes[3];
	bufHook[0xE] = bufOriginalBytes[4];
	bufHook[0xF] = bufOriginalBytes[5];
	bufHook[0x10] = bufOriginalBytes[6];
	bufHook[0x11] = bufOriginalBytes[7];

	bufHook[0x12] = 0x48;
	bufHook[0x13] = 0xA3;
	*(U64*)&bufHook[0x14] = aFunction;

	bufHook[0x1C] = 0x48;
	bufHook[0x1D] = 0xB8;
	bufHook[0x1E] = bufOriginalBytes[8];
	bufHook[0x1F] = bufOriginalBytes[9];
	bufHook[0x20] = bufOriginalBytes[10];
	bufHook[0x21] = bufOriginalBytes[11];
	bufHook[0x22] = bufOriginalBytes[12];
	bufHook[0x23] = bufOriginalBytes[13];
	bufHook[0x24] = bufOriginalBytes[14];
	bufHook[0x25] = bufOriginalBytes[15];

	bufHook[0x26] = 0x48;
	bufHook[0x27] = 0xA3;
	*(U64*)&bufHook[0x28] = aFunction + 8;

	bufHook[0x30] = 0x58;

	bufHook[0x31] = 0xFF;
	bufHook[0x32] = 0x25;
	bufHook[0x33] = 0x00;
	bufHook[0x34] = 0x00;
	bufHook[0x35] = 0x00;
	bufHook[0x36] = 0x00;
	*(U64*)&bufHook[0x37] = aFunction;

	bufHook[0x3F] = FALSE;

	pTracker->u.hook.uHitOffset = 0x3F;

	if (!Target_MemoryWriteFlush(hProcess, pPool->aCurrentFreeAddress, bufHook, sizeof(bufHook)))
	{
		return FALSE;
	}
	if (!Target_MemoryUnprotect(hProcess, pTracker->aAddress, pTracker->u.hook.uJumpBytesLen))
	{
		return FALSE;
	}

	pPool->uFreeSize -= sizeof(bufHook);
	pPool->aCurrentFreeAddress += sizeof(bufHook);
	return TRUE;
}

BOOL Hook_Create(VECTOR* const pvecPools, TRACKER* const pTracker, PROCESS const hProcess, U32 const uFuncLen)
{
	if (NULL == pvecPools || NULL == pTracker)
	{
		return FALSE;
	}
	
	ADDRESS const aFunction = pTracker->aAddress;
	POOL* const pPool = Pool_FindOrCreateBest(pvecPools, aFunction, HOOK_MAX_LEN, DISTANCE_NEAR - HOOK_MAX_LEN, hProcess);
	if (NULL == pPool)
	{
		return FALSE;
	}

	ADDRESS const aHook = pPool->aCurrentFreeAddress;
	BOOL const bNear = (aHook > aFunction)
		? ((aHook - aFunction) < (DISTANCE_NEAR - HOOK_MAX_LEN))
		: ((aFunction - aHook) < (DISTANCE_NEAR - HOOK_MAX_LEN));		
	U32 const uJumpLen = bNear ? JUMP_REL32_LEN : JUMP_ABS64_LEN;
	if (uJumpLen > uFuncLen)
	{
		return FALSE;
	}

	pTracker->u.hook.aHookAddress = aHook;

	BOOL bRet = FALSE;
	if (bNear)
	{
		bRet = CreateHookRel32(pTracker, pPool, hProcess);
	}
	else
	{
		bRet = CreateHookAbs64(pTracker, pPool, hProcess);
	}
	return bRet;
}

BOOL Hook_Enable(TRACKER const * const pTracker, PROCESS const hProcess)
{
	if (NULL == pTracker)
	{
		return FALSE;
	}
	BYTE const zero = 0;
	if (!Target_MemoryWriteFlush(hProcess, pTracker->u.hook.aHookAddress + pTracker->u.hook.uHitOffset, &zero, 1))
	{
		return FALSE;
	}
	return Target_MemoryWriteFlush(hProcess, pTracker->aAddress, pTracker->u.hook.uJumpBytes, pTracker->u.hook.uJumpBytesLen);
}

BOOL Hook_IsHit(TRACKER* const pTracker, PROCESS const hProcess)
{
	if (NULL == pTracker)
	{
		return FALSE;
	}
	BYTE bHit = FALSE;
	BOOL const bRet = Target_MemoryRead(hProcess, pTracker->u.hook.aHookAddress + pTracker->u.hook.uHitOffset, &bHit, 1);
	if (bRet && bHit)
	{
		/* 
		 * If bHit is true, it means the hook removed itself as part of the hook code.
		 * We need to force reenable in TrackerAllEnable / show correct info in GUI. 
		 * Breakpoints do this automatically.
		 */
		pTracker->bEnabled = FALSE;
		return TRUE;
	}
	return FALSE;
}
