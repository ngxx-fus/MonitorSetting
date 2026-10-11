#ifndef __APP_COMM_BITWISE_H__
#define __APP_COMM_BITWISE_H__

/* headers ****************************************************************************************
 **************************************************************************************************/
#include "stdint.h"

/* macros *****************************************************************************************
 **************************************************************************************************/

#define SINT64(n)   ((int64_t)(n))
#define SINT32(n)   ((int32_t)(n))
#define SINT16(n)   ((int16_t)(n))
#define SINT8(n)    ((int8_t)(n))
#define UINT64(n)   ((uint64_t)(n))
#define UINT32(n)   ((uint32_t)(n))
#define UINT16(n)   ((uint16_t)(n))
#define UINT8(n)    ((uint8_t)(n))

#define MASK64U(n)  (((n) >= 64U) ? UINT64(0xFFFFFFFFFFFFFFFFULL) : (((n) == 0U) ? UINT64(0U) : UINT64(0xFFFFFFFFFFFFFFFFULL >> (64U - (n)))))
#define MASK32U(n)  (((n) >= 32U) ? UINT32(0xFFFFFFFFU)           : (((n) == 0U) ? UINT32(0U) : UINT32(0xFFFFFFFFU           >> (32U - (n)))))
#define MASK16U(n)  (((n) >= 16U) ? UINT16(0xFFFFU)               : (((n) == 0U) ? UINT16(0U) : UINT16(0xFFFFU               >> (16U - (n)))))
#define MASK8U(n)   (((n) >= 8U)  ? UINT8(0xFFU)                  : (((n) == 0U) ? UINT8(0U)  : UINT8 (0xFFU                 >> (8U  - (n)))))

#define BIT64U(pos)                 (UINT64(1ULL) << ((pos) & 0x3FU))
#define BIT32U(pos)                 (UINT32(1U)   << ((pos) & 0x1FU))
#define BIT16U(pos)                 UINT16(UINT32(1U) << ((pos) & 0x0FU))
#define BIT8U(pos)                  UINT8 (UINT32(1U) << ((pos) & 0x07U))

#define BIT_SET(val, pos)           ((val) |=  (1ULL << (pos)))
#define BIT_CLR(val, pos)           ((val) &= ~(1ULL << (pos)))
#define BIT_TOGGLE(val, pos)        ((val) ^=  (1ULL << (pos)))
#define BIT_CHECK(val, pos)         (((val) >> (pos)) & 1U)

#define FIELD_MASK32U(pos, len)     (MASK32U(len) << ((pos) & 0x1FU))
#define FIELD_GET32U(val, pos, len) ((UINT32(val) >> ((pos) & 0x1FU)) & MASK32U(len))
#define FIELD_SET32U(val, pos, len, field_val) \
    ((UINT32(val) & ~FIELD_MASK32U(pos, len)) | ((UINT32(field_val) & MASK32U(len)) << ((pos) & 0x1FU)))

/* definitions ************************************************************************************
 **************************************************************************************************/

/* exported variables *****************************************************************************
 **************************************************************************************************/

/* exported functions *****************************************************************************
 **************************************************************************************************/

/* reserved ***************************************************************************************
 **************************************************************************************************/

#endif /*__APP_COMM_BITWISE_H__*/