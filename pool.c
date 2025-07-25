#include "pool.h"
#include "vector.h"

static BOOL Pool_IsNearAddress(POOL const* pPool, ADDRESS aAddress, U64 uDistance);
static BOOL Pool_CreateNear(POOL* pPool, ADDRESS aAddress, U64 uNearDistance, PROCESS hProcess);
static BOOL Pool_CreateAnywhere(POOL* pPool, PROCESS hProcess);
static void Pool_Free(POOL const* pPool, PROCESS hProcess);

static BOOL Pool_IsNearAddress(POOL const * const pPool, ADDRESS const aAddress, U64 const uDistance)
{
	if (NULL == pPool)
	{
		return FALSE;
	}

	/* Return true if the address is near +-uDistance of the pool's next free address */
	ADDRESS const aLowerLimit = (pPool->aCurrentFreeAddress > uDistance)
		? (pPool->aCurrentFreeAddress - uDistance)
		: 0;
	ADDRESS const aUpperLimit = pPool->aCurrentFreeAddress + uDistance;
	return (aAddress >= aLowerLimit && aAddress <= aUpperLimit);
}

static BOOL Pool_CreateNear(POOL* const pPool, ADDRESS const aAddress, U64 const uNearDistance, PROCESS const hProcess)
{
    U64 uSize;
    ADDRESS const aAlloc = Target_MemoryAllocExecNear(hProcess, aAddress, uNearDistance, 64, &uSize);
	if (NULL == aAlloc)
	{
		return FALSE;
	}
	pPool->aStartAddress = aAlloc;
	pPool->aCurrentFreeAddress = aAlloc;
	pPool->uPoolSize = uSize;
	pPool->uFreeSize = uSize;
	return TRUE;
}

static BOOL Pool_CreateAnywhere(POOL* const pPool, PROCESS const hProcess)
{
	/* Can hold a thousand of HOOK_MAX_LEN. */
	U64 const uPoolSize = 64 * 1000;

	ADDRESS const aAddress = Target_MemoryAllocExec(hProcess, uPoolSize);
	if (NULL == aAddress)
	{
		return FALSE;
	}
	pPool->aStartAddress = aAddress;
	pPool->aCurrentFreeAddress = aAddress;
	pPool->uPoolSize = uPoolSize;
	pPool->uFreeSize = uPoolSize;
	return TRUE;
}

POOL* Pool_FindOrCreateBest(VECTOR* const pVecPools, ADDRESS const aAddressNear, U64 const uRequiredSpace, U64 const uNearDistance, PROCESS const hProcess)
{
	if (NULL == pVecPools)
	{
		return NULL;
	}

	POOL* pPoolNear = NULL;
	POOL* pPoolFar = NULL;
	for (U32 i = 0; i < pVecPools->uElemCount; i++)
	{
		POOL* const pPool = (POOL*)Vector_AddressOf(pVecPools, i);
		if (NULL == pPool || pPool->uFreeSize < uRequiredSpace)
		{
			continue;
		}
		if (NULL == pPoolFar)
		{
			pPoolFar = pPool;
		}
		if (Pool_IsNearAddress(pPool, aAddressNear, uNearDistance))
		{
			pPoolNear = pPool;
			break;
		}
	}

	/* If no pool is near, try to create one. If that fails, fall back to a far one.*/
	POOL* pPoolBest = pPoolNear;
	if (NULL == pPoolBest)
	{
		POOL pool;
        BOOL bSuccess = Pool_CreateNear(&pool, aAddressNear, uNearDistance, hProcess);
        if (bSuccess && !Pool_IsNearAddress(&pool, aAddressNear, uNearDistance))
        {
            bSuccess = FALSE;
            Pool_Free(&pool, hProcess);
        }
		if (bSuccess && Vector_PushBackCopy(pVecPools, &pool))
		{
			pPoolBest = (POOL*)Vector_AddressOf(pVecPools, pVecPools->uElemCount - 1);
		}
		else
		{
			pPoolBest = pPoolFar;
		}
	}

	/* If we still dont have a pool, we have to create a new far one manually. */
	if (NULL == pPoolBest)
	{
		POOL pool;
		BOOL const bSuccess = Pool_CreateAnywhere(&pool, hProcess);
		if (!bSuccess || !Vector_PushBackCopy(pVecPools, &pool))
		{
			return NULL;
		}
		pPoolBest = (POOL*)Vector_AddressOf(pVecPools, pVecPools->uElemCount - 1);
	}

	return pPoolBest;
}

static void Pool_Free(POOL const * const pPool, PROCESS const hProcess)
{
	if (NULL == pPool)
	{
		return;
	}
	Target_MemoryFree(hProcess, pPool->aStartAddress);
}
