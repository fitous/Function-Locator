#ifndef VECTOR_H
#define VECTOR_H

#include "types.h"

typedef struct tdVECTOR {
    void* pData;
    U32 uElemCount;
    U32 uElemSize;
    U32 uElemCapacity;
    BYTE _padding[4];
} VECTOR;

BOOL Vector_Init(VECTOR* pVec, U32 uElemSize, U32 uInitialElemCapacity);
void* Vector_AddressOf(VECTOR const * pVec, U32 uIndex);
BOOL Vector_PushBackCopy(VECTOR* pVec, void const* pElem);
BOOL Vector_Free(VECTOR* pVec);

#endif /* VECTOR_H */
