#ifndef TRACKER_H
#define TRACKER_H

#include "types.h"
#include "hook.h"

struct tdPOOL;
typedef struct tdPOOL POOL;

typedef enum tdTRACKER_TYPE {
	TRACKER_TYPE_DELETED,
	TRACKER_TYPE_BREAKPOINT_SW,
	TRACKER_TYPE_HOOK_INLINE
} TRACKER_TYPE;

typedef struct tdBREAKPOINT {
	BYTE uOriginalByte;
} BREAKPOINT;

typedef struct tdTRACKER {
	ADDRESS aAddress;
	TRACKER_TYPE eType;
	BOOL bEnabled;
	BOOL bHit;
	BYTE _padding[4];
	union UTRACKERTYPE {
		BREAKPOINT bp;
		HOOK hook;
	} u;
} TRACKER;

#endif /* TRACKER_H */
