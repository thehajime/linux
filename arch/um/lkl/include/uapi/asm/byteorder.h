/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __UM_LIBMODE_UAPI_BYTEORDER_H
#define __UM_LIBMODE_UAPI_BYTEORDER_H

#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#include <linux/byteorder/big_endian.h>
#elif __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#include <linux/byteorder/little_endian.h>
#else
#error "cannot detect endian of the target"
#endif

#endif
