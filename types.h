#ifndef TYPES_H
#define TYPES_H

typedef unsigned long long U64;
typedef unsigned long U32;
typedef int BOOL;
typedef unsigned char BYTE;

/* Usage for target (external) process. */
typedef unsigned long long ADDRESS;

#ifdef TRUE
#undef TRUE
#endif
#define TRUE (1)
#ifdef FALSE
#undef FALSE
#endif
#define FALSE (0)
#ifdef NULL
#undef NULL
#endif
#define NULL (0)

#endif /* TYPES_H */
