#ifndef _CO_TYPES_DEF_H_
#define _CO_TYPES_DEF_H_

/****************************************************************************
 *
 * Common definitions
 *
 ****************************************************************************/

#ifdef FALSE
#undef FALSE
#endif
#ifdef TRUE
#undef TRUE
#endif

typedef enum { FALSE = 0, TRUE = 1 } Boolean;
#if 0
typedef unsigned char               BOOL;
typedef unsigned char               U8;
typedef unsigned short              U16;
typedef unsigned int                U32;
typedef signed char                 S8;
typedef signed short                S16;
typedef signed int                  S32;
typedef unsigned char               u8;
typedef unsigned short              u16;
typedef unsigned long               u32;
typedef unsigned long long          u64;
typedef signed char                 s8;
typedef signed short                s16;
typedef signed long                 s32;
typedef signed long long            s64;
typedef unsigned char               byte;           /*  unsigned 8-bit data     */
typedef unsigned short              word;           /*  unsigned 16-bit data    */
typedef unsigned long               dword;          /*  unsigned 32-bit data    */
#endif
#endif /* _CO_TYPES_DEF_H_ */
