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

#define MASK64U(n)  UINT64(0xFFFFFFFFFFFFFFFFLLU >> (64-((n)&0x3F)))
#define MASK32U(n)  UINT32(          0xFFFFFFFFU >> (32-((n)&0x1F)))
#define MASK16U(n)  UINT16(          0xFFFFFFFFU >> (16-((n)&0xF )))
#define MASK8U(n)   UINT8 (          0xFFFFFFFFU >> (8 -((n)&0x7 )))

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