#ifndef __APP_COMM_ERR_H__
#define __APP_COMM_ERR_H__

#include "stdint.h"

#define APP_STAT_NUM            (5)

enum AppStatus_e { 
    APP_SUCCESS                 = (0U),
    APP_ERROR                   = (0x80000000U),
    APP_ERROR_NULL              = (0x80000001U),
    APP_ERROR_INVALID_PARAM     = (0x80000002U),
    APP_ERROR_MALOC_FAILED      = (0x80000003U),
};

typedef enum AppStatus_e   Err_t;
typedef enum AppStatus_e   Return_t;

/* String of an error code*/
extern const char * Err2Str[];

#endif /* __APP_COMM_ERR_H__ */