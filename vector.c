#include "vector.h"
#include "os.h"

static BOOL Vector_IsFull(VECTOR const * pVec);
static BOOL Vector_Grow(VECTOR* pVec);

BOOL Vector_Init(VECTOR* const pVec, U32 const uElemSize, U32 const uInitialElemCapacity)
{
	if (0 == uElemSize || 0 == uInitialElemCapacity)
	{
		return FALSE;
	}

	U64 const uBytes = (U64)(uElemSize) * uInitialElemCapacity;
	pVec->pData = Memory_Alloc(uBytes);
	if (NULL == pVec->pData)
	{
		return FALSE;
	}

	pVec->uElemCapacity = uInitialElemCapacity;
	pVec->uElemSize = uElemSize;
	pVec->uElemCount = 0;
	return TRUE;
}

static BOOL Vector_IsFull(VECTOR const * const pVec)
{
	return pVec->uElemCount >= pVec->uElemCapacity;
}

static BOOL Vector_Grow(VECTOR* const pVec)
{
	U32 const uOldBytes = pVec->uElemCapacity * pVec->uElemSize;
	U32 const uNewCapacity = pVec->uElemCapacity * 2;
	U32 const uNewBytes = uNewCapacity * pVec->uElemSize;

	void* const pNewData = Memory_Alloc(uNewBytes);
	if (NULL == pNewData)
	{
		return FALSE;
	}

	Memory_Copy(pNewData, pVec->pData, uOldBytes);
	Memory_Free(pVec->pData);

	pVec->uElemCapacity = uNewCapacity;
	pVec->pData = pNewData;
	return TRUE;
}

BOOL Vector_PushBackCopy(VECTOR* const pVec, void const * const pElem)
{
	if (Vector_IsFull(pVec) && !Vector_Grow(pVec))
	{
		return FALSE;
	}

	U32 const uLastIndexPlusOne = pVec->uElemCount;
	void* const uDest = Vector_AddressOf(pVec, uLastIndexPlusOne);
	if (NULL == uDest)
	{
		return FALSE;
	}

	Memory_Copy(uDest, pElem, pVec->uElemSize);
	pVec->uElemCount++;
	return TRUE;
}

void* Vector_AddressOf(VECTOR const * pVec, U32 const uIndex)
{
	BYTE* const pRes = ((BYTE*)pVec->pData + ((U64)uIndex * pVec->uElemSize));
	BYTE const * const pBegin = (BYTE*)pVec->pData;
	BYTE const * const pEnd = pBegin + ((U64)pVec->uElemCapacity * pVec->uElemSize);
	BOOL const valid = pRes >= pBegin && pRes < pEnd;
	return valid ? pRes : NULL;
}

BOOL Vector_Free(VECTOR* const pVec)
{
	BOOL const ret = Memory_Free(pVec->pData);
	pVec->pData = NULL;
	pVec->uElemCapacity = 0;
	pVec->uElemSize = 0;
	return ret;
}
