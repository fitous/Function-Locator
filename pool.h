#ifndef POOL_H
#define POOL_H

#include "types.h"
#include "os.h"

struct tdVECTOR;
typedef struct tdVECTOR VECTOR;

typedef struct tdPOOL {
	ADDRESS aStartAddress;
	ADDRESS aCurrentFreeAddress;
	U64 uPoolSize;
	U64 uFreeSize;
} POOL;

POOL* Pool_FindOrCreateBest(VECTOR* pVecPools, ADDRESS aAddressNear, U64 uRequiredSpace, U64 uNearDistance, PROCESS hProcess);

#endif /* POOL_H */
