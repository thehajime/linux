/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef __UM_NOMMU_UAPI_BYTEORDER_H
#define __UM_NOMMU_UAPI_BYTEORDER_H

#if defined(CONFIG_BIG_ENDIAN)
#include <linux/byteorder/big_endian.h>
#else
#include <linux/byteorder/little_endian.h>
#endif

#endif
