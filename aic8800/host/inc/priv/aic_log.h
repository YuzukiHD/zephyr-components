/*
 * Copyright (C) 2018-2020 AICSemi Ltd.
 *
 * All Rights Reserved
 */


#ifndef _AIC_LOG_H_
#define _AIC_LOG_H_

#include <aic_types.h>
#include <porting.h>
#include "rtos_al.h"

#ifdef PLATFORM_ALLWIN_RT_THREAD
#include <log.h>

#ifdef AIC_LOG_DEBUG_ON

#define AIC_LOG_PRINTF(fmt, ...)	__log(fmt, ##__VA_ARGS__)
#define AIC_LOG_TRACE(fmt, ...)	    __msg(fmt, ##__VA_ARGS__)
#define AIC_LOG_DEBUG(fmt, ...)   	__msg(fmt, ##__VA_ARGS__)
#define AIC_LOG_ERROR(fmt, ...)     __err(fmt, ##__VA_ARGS__)
#define AIC_LOG_INFO(fmt, ...)      __log(fmt, ##__VA_ARGS__)

#else

#define AIC_LOG_PRINTF(...)		do { } while(0)
#define AIC_LOG_TRACE(...)		do { } while(0)
#define AIC_LOG_DEBUG(...)		do { } while(0)
#define AIC_LOG_ERROR(...)		do { } while(0)
#define AIC_LOG_INFO(...)		do { } while(0)

#endif

#define aic_dbg 	printf
#if !defined(printk) && !defined(__ZEPHYR__)
#define printk 		printf
#endif
#endif

#ifdef PLATFORM_ALLWIN_FREERTOS
#include <stdio.h>

#ifdef __ZEPHYR__
/* the Zephyr logging system, see aic_log_redirect.h */
#define AIC_LOG_PRINTF(fmt, ...)	aic_log(AIC_LVL_INF, fmt, ##__VA_ARGS__)
#define AIC_LOG_TRACE(fmt, ...)	    aic_log(AIC_LVL_DBG, fmt, ##__VA_ARGS__)
#define AIC_LOG_DEBUG(fmt, ...)   	aic_log(AIC_LVL_DBG, fmt, ##__VA_ARGS__)
#define AIC_LOG_ERROR(fmt, ...)     aic_log(AIC_LVL_ERR, fmt, ##__VA_ARGS__)
#define AIC_LOG_INFO(fmt, ...)      aic_log(AIC_LVL_INF, fmt, ##__VA_ARGS__)

#define aic_dbg(fmt, ...)	aic_log(AIC_LVL_DBG, fmt, ##__VA_ARGS__)
#else

#ifdef AIC_LOG_DEBUG_ON

#define AIC_LOG_PRINTF(fmt, ...)	printf(fmt, ##__VA_ARGS__)
#define AIC_LOG_TRACE(fmt, ...)	    printf(fmt, ##__VA_ARGS__)
#define AIC_LOG_DEBUG(fmt, ...)   	printf(fmt, ##__VA_ARGS__)
#define AIC_LOG_ERROR(fmt, ...)     printf(fmt, ##__VA_ARGS__)
#define AIC_LOG_INFO(fmt, ...)      printf(fmt, ##__VA_ARGS__)

#else

#define AIC_LOG_PRINTF(...)		do { } while(0)
#define AIC_LOG_TRACE(...)		do { } while(0)
#define AIC_LOG_DEBUG(...)		do { } while(0)
#define AIC_LOG_ERROR(...)		do { } while(0)
#define AIC_LOG_INFO(...)		do { } while(0)

#endif

#define aic_dbg 	printf
#if !defined(printk) && !defined(__ZEPHYR__)
#define printk 		printf
#endif
#endif /* __ZEPHYR__ */
#endif

#if 0
#ifdef __AIC_UNIX_SIM__
#include <stdio.h>
#endif

// ===============================================================================================
//											Global
// ===============================================================================================

//
// note	:
//		The 'sys_init_prnf' is to note that it is in system init stage now. All 'log printf' func
// can NOT be used during this stage because the log module is NOT inited yet and the 'log mutex'
// is NOT inited yet.
//
#define 	sys_init_prnf		hal_print

#define		AIC_LOG_DMSG_FILENAME	"aic-uart.dmsg"
#define		AIC_LOG_MAX_TAG			16
#define		AIC_LOG_MAX_PATH			256
#define		AIC_LOG_MAX_FILENAME		48		// without directory path
#define		AIC_LOG_DMSG_MAX_ARGS	16

// global variables for acceleration
#define		AIC_PRN_BUF_MAX			256
#define		AIC_PRN_TAG_MAX			LOG_MAX_TAG
#define		AIC_ACC_BUF_MAX			(2*PRN_BUF_MAX + 3*PRN_TAG_MAX)






// ===============================================================================================
//									Level & Modules definition
// ===============================================================================================

#ifdef AIC_LOG_DEBUG_ON

extern uint32_t g_log_module;
extern uint32_t g_log_min_level;

#define AIC_LOG_PLATFORM_DIAG(x)   do { hal_print x; } while(0)
#define AIC_LOG_DEBUGF(debug, message) do { \
                                    if ( \
                                   ((debug) & g_log_module) && \
                                   ((int16_t)((debug) & AIC_LOG_LEVEL_MASK_LEVEL) >= (int16_t)g_log_min_level)) { \
                                    AIC_LOG_PLATFORM_DIAG(message); \
                                    } \
                                    } while(0)


#else  /* AIC_LOG_DEBUG_ON */
#define AIC_LOG_DEBUGF(debug, message) do { ;} while(0)
#endif /* AIC_LOG_DEBUG_ON */

//=====================================================
//Abandon
//=====================================================


// AIC_LOG_LEVEL_FATAL:
// -  print "program halt!!!" msg & terminate program immerdiately;
// -  the 'AIC_LOG_LEVEL_FATAL' will always execute, ignore the influence of 'module' & 'level' settings.
#define AIC_LOG_LEVEL_ON		0x0					// runtime option : show all level msg
#define AIC_LOG_LEVEL_TRACE		0x1
#define AIC_LOG_LEVEL_DEBUG		0x2
#define AIC_LOG_LEVEL_INFO		0x3
#define AIC_LOG_LEVEL_WARN		0x4
#define AIC_LOG_LEVEL_FAIL		0x5
#define AIC_LOG_LEVEL_ERROR		0x6
#define AIC_LOG_LEVEL_FATAL		0x7
#define AIC_LOG_LEVEL_OFF		(AIC_LOG_LEVEL_FATAL+1)	// runtime option : show NO  level msg

extern const char*	g_log_lvl_tag[AIC_LOG_LEVEL_OFF];

// switch to turn on/off the message belongs to the 'module'
// max # of module id define : 0~31
typedef enum {
    AIC_LOG_MODULE_EMPTY        = 0,    // 0
    AIC_LOG_MODULE_MRX,                 // 1
    AIC_LOG_MODULE_MTX,                 // 2
    AIC_LOG_MODULE_EDCA,                // 3
    AIC_LOG_MODULE_PBUF,                // 4
    AIC_LOG_MODULE_L3L4,                // 5
    AIC_LOG_MODULE_MGMT,                // 6
    AIC_LOG_MODULE_FRAG,                // 7
    AIC_LOG_MODULE_DEFRAG,              // 8
    AIC_LOG_MODULE_MLME,                // 9
    AIC_LOG_MODULE_CMD,                 // 10 Command Engine
    AIC_LOG_MODULE_WPA,                 // 11
    AIC_LOG_MODULE_MAIN,                // 12
    AIC_LOG_MODULE_ALL                  // 13
} AIC_LOG_MODULE_E;
extern const char*	g_log_mod_tag[AIC_LOG_MODULE_ALL];


#define AIC_LOG_MODULE_MASK(n)			(((n) == AIC_LOG_MODULE_ALL) ? 0xffffffffU :   (0x00000001U << (int)(n)))
#define AIC_LOG_MODULE_I_MASK(n)		(((n) == AIC_LOG_MODULE_ALL) ? 0x00000000U : (~(0x00000001U << (int)(n))))

#ifdef __AIC_UNIX_SIM__
#define AIC_LOG_LEVEL_SET(n)			{ AIC_LOG_MUTEX_LOCK(); g_log_prnf_cfg.lvl = (n);					AIC_LOG_MUTEX_UNLOCK(); }

#define AIC_LOG_MODULE_TURN_ON(n)		{ AIC_LOG_MUTEX_LOCK();	g_log_prnf_cfg.mod |= AIC_LOG_MODULE_MASK(n);	AIC_LOG_MUTEX_UNLOCK(); }
#define AIC_LOG_MODULE_TURN_OFF(n)		{ AIC_LOG_MUTEX_LOCK();	g_log_prnf_cfg.mod &= AIC_LOG_MODULE_I_MASK(n);	AIC_LOG_MUTEX_UNLOCK(); }

#define AIC_LOG_FILELINE_TURN_ON()		{ AIC_LOG_MUTEX_LOCK(); g_log_prnf_cfg.fl = 1;						AIC_LOG_MUTEX_UNLOCK(); }
#define AIC_LOG_FILELINE_TURN_OFF()		{ AIC_LOG_MUTEX_LOCK(); g_log_prnf_cfg.fl = 0;						AIC_LOG_MUTEX_UNLOCK(); }

#define AIC_LOG_TAG_LEVEL_TURN_ON()		{ AIC_LOG_MUTEX_LOCK(); g_log_prnf_cfg.prn_tag_lvl = true;			AIC_LOG_MUTEX_UNLOCK(); }
#define AIC_LOG_TAG_LEVEL_TURN_OFF()	{ AIC_LOG_MUTEX_LOCK(); g_log_prnf_cfg.prn_tag_lvl = false;			AIC_LOG_MUTEX_UNLOCK(); }
#define AIC_LOG_TAG_LEVEL_TURN(x)		{ AIC_LOG_MUTEX_LOCK(); g_log_prnf_cfg.prn_tag_lvl = (x);			AIC_LOG_MUTEX_UNLOCK(); }

#define AIC_LOG_TAG_MODULE_TURN_ON()	{ AIC_LOG_MUTEX_LOCK(); g_log_prnf_cfg.prn_tag_mod = true;			AIC_LOG_MUTEX_UNLOCK(); }
#define AIC_LOG_TAG_MODULE_TURN_OFF()	{ AIC_LOG_MUTEX_LOCK(); g_log_prnf_cfg.prn_tag_mod = false;			AIC_LOG_MUTEX_UNLOCK(); }
#define AIC_LOG_TAG_MODULE_TURN(x)		{ AIC_LOG_MUTEX_LOCK(); g_log_prnf_cfg.prn_tag_mod = (x);			AIC_LOG_MUTEX_UNLOCK(); }

#define AIC_LOG_TAG_SUPPRESS_ON() 		{ AIC_LOG_MUTEX_LOCK(); g_log_prnf_cfg.prn_tag_sprs = true;			AIC_LOG_MUTEX_UNLOCK(); }
#define AIC_LOG_TAG_SUPPRESS_OFF()		{ AIC_LOG_MUTEX_LOCK(); g_log_prnf_cfg.prn_tag_sprs = false;		AIC_LOG_MUTEX_UNLOCK(); }
#endif

// ===============================================================================================
//											log mutex
// ===============================================================================================
// just for debugging use
#define		AIC_LOG_MUTEX_DBGMSG	0
#define		AIC_LOG_MUTEX_DBG		0

#if (AIC_LOG_MUTEX_DBG == 0)
#define _os_mutex			rtos_mutex
#define _os_mutex_init		rtos_mutex_create
#define _os_mutex_lock		rtos_mutex_lock
#define _os_mutex_unlock	rtos_mutex_unlock
#else
#define _os_mutex			uint32_t
#define _os_mutex_init(x)	(g_log_mutex = 0)
#define _os_mutex_lock(x)	(g_log_mutex++)
#define _os_mutex_unlock(x)	(g_log_mutex--)
#endif


extern _os_mutex			g_log_mutex;
extern uint32_t					g_dbg_mutex;
//if (!gOsFromISR)
#define AIC_LOG_MUTEX_INIT()			\
{									\
	if (AIC_LOG_MUTEX_DBGMSG)  hal_print("%s() #%d: log mutex init   = %d\n\r", __FUNCTION__, __LINE__, (g_dbg_mutex = 0)); \
	_os_mutex_init(&g_log_mutex,"g_log_mutex");	\
}
#define AIC_LOG_MUTEX_LOCK()			\
{									\
	if (!gOsFromISR)                \
    	_os_mutex_lock(g_log_mutex);\
	if (AIC_LOG_MUTEX_DBGMSG)	hal_print("%s() #%d: log mutex lock   = %d\n\r", __FUNCTION__, __LINE__, (++g_dbg_mutex)); \
}
#define AIC_LOG_MUTEX_UNLOCK()			\
{									\
	if (AIC_LOG_MUTEX_DBGMSG)	hal_print("%s() #%d: log mutex unlock = %d\n\r", __FUNCTION__, __LINE__, (--g_dbg_mutex)); \
	if (!gOsFromISR)				\
	_os_mutex_unlock(g_log_mutex);	\
}

// ===============================================================================================
//							log prnf cfg & output stream descriptor
// ===============================================================================================
typedef struct {
    uint32_t		lvl;
    uint32_t 	mod;
    uint32_t	    fl;

    uint32_t     prn_tag_lvl;
    uint32_t     prn_tag_mod;
    uint32_t     prn_tag_sprs;
    uint32_t     chk_tag_sprs;	// won't check 'level' & 'module', ONLY used by log_printf().
    // this is used when log_printf() is inside LOG_TAG_SUPPRESS_ON() & LOG_TAG_SUPPRESS_OFF()
} log_prnf_cfg_st;
extern log_prnf_cfg_st	g_log_prnf_cfg;


#define AIC_LOG_OUT_HOST_TERM			0x01	// host terminal
#define AIC_LOG_OUT_HOST_FILE			0x02	// host file
#define AIC_LOG_OUT_SOC_TERM			0x10	// soc terminal (UART)
#define AIC_LOG_OUT_SOC_HOST_TERM		0x20	// soc -> host terminal (DbgView)
#define AIC_LOG_OUT_SOC_HOST_FILE		0x40	// soc -> host file

#ifdef __AIC_UNIX_SIM__
#define	AIC_LOG_OUT_DST_IS_OPEN(x)		(g_log_out.dst_open & (x))
#define AIC_LOG_OUT_DST_OPEN(x)			{ AIC_LOG_MUTEX_LOCK(); g_log_out.dst_open |= (x);		AIC_LOG_MUTEX_UNLOCK(); }
#define AIC_LOG_OUT_DST_CLOSE(x)		{ AIC_LOG_MUTEX_LOCK(); g_log_out.dst_open &= (~(x));	AIC_LOG_MUTEX_UNLOCK(); }

#define AIC_LOG_OUT_DST_IS_CUR_ON(x)	(g_log_out.dst_cur & (x))
#define AIC_LOG_OUT_DST_CUR_ON(x)		{ AIC_LOG_MUTEX_LOCK(); g_log_out.dst_cur |= (x);		AIC_LOG_MUTEX_UNLOCK(); }
#define AIC_LOG_OUT_DST_CUR_OFF(x)		{ AIC_LOG_MUTEX_LOCK(); g_log_out.dst_cur &= (~(x));	AIC_LOG_MUTEX_UNLOCK(); }
#endif
// #define LOG_OUT_FILE_MODE		"a+"	// mode for fopen()
#define AIC_LOG_OUT_FILE_MODE		"w"	// mode for fopen()

typedef struct {
    uint8_t		dst_open;			// opened  destination
    uint8_t		dst_cur;			// current destination
    void	*fp;				// the log file ptr  on host side
    uint8_t		path[AIC_LOG_MAX_PATH];	// the log file path on host side
} log_out_desc_st;


#ifdef __AIC_UNIX_SIM__
extern log_out_desc_st	g_log_out;
#define AIC_LOG_out_dst_open		_sim_out_dst_open
#define AIC_LOG_out_dst_close		_sim_out_dst_close
#define AIC_LOG_out_dst_turn_on		_sim_out_dst_turn_on
#define AIC_LOG_out_dst_turn_off	_sim_out_dst_turn_off

extern bool	AIC_LOG_out_dst_open(uint8_t _dst, const uint8_t *_path);
extern bool	AIC_LOG_out_dst_close(uint8_t _dst);
extern bool	AIC_LOG_out_dst_turn_on(uint8_t _dst);
extern bool	AIC_LOG_out_dst_turn_off(uint8_t _dst);
extern void AIC_LOG_out_desc_dump(void);
extern bool AIC_LOG_out_init(void);
#endif


// ===============================================================================================
//								printf & fatal func & quick macro
// ===============================================================================================

// 'LM' means 'level & module'
#ifdef AIC_LOG_DEBUG_ON
#define AIC_LOG_PRINTF_LM(l, m, fmt, ...)	(((l) == AIC_LOG_LEVEL_FATAL) ? aic_fatal(m, __func__, __LINE__, fmt, ##__VA_ARGS__) : aic_printf((l), (m), __FILE__, __LINE__, fmt, ##__VA_ARGS__))
#else
#define AIC_LOG_PRINTF_LM(l, m, fmt, ...)	(((l) == AIC_LOG_LEVEL_FATAL) ? AIC_LOG_PRINTF(fmt, ##__VA_ARGS__) : AIC_LOG_PRINTF(fmt, ##__VA_ARGS__))
#endif
#define log_printf hal_print
// quick macro
#ifndef __AIC_UNIX_SIM__
#define AIC_LOG_PRINTF hal_print
#define AIC_LOG_TRACE hal_print
#define AIC_LOG_DEBUG hal_print
#define AIC_LOG_ERROR hal_print
#define AIC_LOG_INFO hal_print
#else
#define AIC_LOG_PRINTF		printf
#define AIC_LOG_TRACE	    printf
#define AIC_LOG_DEBUG   	printf
#define AIC_LOG_ERROR       printf
#define AIC_LOG_INFO        printf
#endif
#define AIC_LOG_WARN(fmt,   ...)		AIC_LOG_PRINTF_LM(AIC_LOG_LEVEL_WARN,	AIC_LOG_MODULE_EMPTY, fmt, ##__VA_ARGS__)
#define AIC_LOG_FAIL(fmt,   ...)		AIC_LOG_PRINTF_LM(AIC_LOG_LEVEL_FAIL,	AIC_LOG_MODULE_EMPTY, fmt, ##__VA_ARGS__)
#define AIC_LOG_FATAL(fmt,  ...)	    AIC_LOG_PRINTF_LM(AIC_LOG_LEVEL_FATAL,	AIC_LOG_MODULE_EMPTY, fmt, ##__VA_ARGS__)
// 'M' means 'module'
#define AIC_LOG_PRINTF_M(m, fmt, ...)	AIC_LOG_PRINTF_LM(AIC_LOG_LEVEL_ON,					   m, fmt, ##__VA_ARGS__)
#define AIC_LOG_TRACE_M(m,  fmt, ...)	AIC_LOG_PRINTF_LM(AIC_LOG_LEVEL_TRACE,				   m, fmt, ##__VA_ARGS__)
#define AIC_LOG_DEBUG_M(m,  fmt, ...)	AIC_LOG_PRINTF_LM(AIC_LOG_LEVEL_DEBUG,				   m, fmt, ##__VA_ARGS__)
#define AIC_LOG_INFO_M(m,   fmt, ...)	AIC_LOG_PRINTF_LM(AIC_LOG_LEVEL_INFO,				   m, fmt, ##__VA_ARGS__)
#define AIC_LOG_WARN_M(m,   fmt, ...)	AIC_LOG_PRINTF_LM(AIC_LOG_LEVEL_WARN,				   m, fmt, ##__VA_ARGS__)
#define AIC_LOG_FAIL_M(m,   fmt, ...)	AIC_LOG_PRINTF_LM(AIC_LOG_LEVEL_FAIL,				   m, fmt, ##__VA_ARGS__)
#define AIC_LOG_ERROR_M(m,  fmt, ...)	AIC_LOG_PRINTF_LM(AIC_LOG_LEVEL_ERROR,				   m, fmt, ##__VA_ARGS__)
#define AIC_LOG_FATAL_M(m,  fmt, ...)   AIC_LOG_PRINTF_LM(AIC_LOG_LEVEL_FATAL,				   m, fmt, ##__VA_ARGS__)

#define aic_dbg 	printf
#define printk		printf

// ===============================================================================================
//										dbgmsg stripping
// ===============================================================================================

#define	AIC_LOG_DMSG_MAX_ARGS	16


#define T(str)	str

// printf string to SOC_HOST_TERM (defaultly, DbgView in WIN32)
#if (defined _WIN32)
#define _prnf_soc_host_term		OutputDebugStringA
#else
#define _prnf_soc_host_term		log_printf
#endif

#define AIC_LOG_EVT_PRNF_BUF_MAX	(AIC_LOG_DMSG_MAX_ARGS*sizeof(uint32_t) + 32)	// 32 -> rsvd space for safety
#define AIC_LOG_EVT_PRNF_MAXLEN		(sizeof(log_evt_prnf_st) + AIC_LOG_EVT_PRNF_BUF_MAX)

// ===============================================================================================
//											Misc
// ===============================================================================================
extern void AIC_LOG_init(bool tag_level, bool tag_mod, uint32_t level, uint32_t mod_mask, bool fileline);

#endif

#endif	// _AIC_LOG_H_

