/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __UM_LIBMODE_UAPI_BYTEORDER_H
#define __UM_LIBMODE_UAPI_BYTEORDER_H

#if defined(CONFIG_BIG_ENDIAN)
#include <linux/byteorder/big_endian.h>
#else
#include <linux/byteorder/little_endian.h>
#endif

#endif
