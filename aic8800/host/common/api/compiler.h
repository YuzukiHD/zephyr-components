#ifndef _AIC_COMPILER_H_
#define _AIC_COMPILER_H_

#ifndef __INLINE
#define __INLINE     __inline
#endif /* _COMPILER_H_ */

#ifndef __STATIC_INLINE
#define __STATIC_INLINE   static __inline
#endif /* __STATIC_INLINE */

#ifndef pdTRUE
#define pdTRUE  1
#endif
#ifndef pdFALSE
#define pdFALSE 0
#endif

#if defined(__CC_ARM)
#pragma anon_unions
#endif

#define dbg_vsnprintf vsnprintf


#endif /* _AIC_COMPILER_H_ */
