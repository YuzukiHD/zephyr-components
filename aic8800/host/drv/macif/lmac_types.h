/**
 ****************************************************************************************
 *
 * @file co_types.h
 *
 * @brief This file replaces the need to include stdint or stdbool typical headers,
 *        which may not be available in all toolchains, and adds new types
 *
 * Copyright (C) RivieraWaves 2009-2019
 *
 * $Rev: $
 *
 ****************************************************************************************
 */

#ifndef _LMAC_TYPES_H_
#define _LMAC_TYPES_H_


/**
 ****************************************************************************************
 * @addtogroup CO_INT
 * @ingroup COMMON
 * @brief Common integer standard types (removes use of stdint)
 *
 * @{
 ****************************************************************************************
 */

#include "co_bool.h"
#ifdef CONFIG_USB_SUPPORT
#include <asm/types.h>
#elif CONFIG_SDIO_SUPPORT
#endif
#include "aic_types.h"

/*
 * DEFINES
 ****************************************************************************************
 */

typedef unsigned char u8_l;
typedef signed char s8_l;
typedef bool bool_l;
typedef unsigned short u16_l;
typedef signed short s16_l;
typedef unsigned int u32_l;
typedef signed int s32_l;
typedef unsigned long long u64_l;

/// @} CO_INT
#endif // _LMAC_TYPES_H_
