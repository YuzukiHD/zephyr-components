#ifndef _ASM_ERRNO_H
#define _ASM_ERRNO_H

#define	ENOMEM		12	/* Out of memory */
#define	EFAULT		14	/* Bad address */

#define	ENODEV		19	/* No such device */
#define	EINVAL		22	/* Invalid argument */
#define	ENOMSG		35	/* No message of desired type */
#define	EIDRM		36	/* Identifier removed */
#define	EL2NSYNC	38	/* Level 2 not synchronized */
#define ETIME		62	/* Timer expired */
#define	ENOSR		63	/* Out of streams resources */
#define	ENOLINK		67	/* Link has been severed */
#define	ECOMM		70	/* Communication error on send */
#define	EPROTO		71	/* Protocol error */
#define	EOVERFLOW	79	/* Value too large for defined data type */
#define	EILSEQ		88	/* Illegal byte sequence */
#define	EREMOTEIO	140	/* Remote I/O error */

#define	E2BIG		 7	/* Argument list too long */

#endif //_ASM_ERRNO_H
