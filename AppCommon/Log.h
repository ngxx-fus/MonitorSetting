#ifndef __APP_COMMON_LOG_H__
#define __APP_COMMON_LOG_H__

/* headers ****************************************************************************************
 **************************************************************************************************/

#include "./__config__.h"
#include "stdio.h"

/* macros *****************************************************************************************
 **************************************************************************************************/

#ifndef CONF_LOG_EN
    #ifdef APP_LOG_EN
        #define CONF_LOG_EN APP_LOG_EN
    #else
        #define CONF_LOG_EN (0U)
    #endif
#endif

#define LOG_STR_HELPER(x)   #x
#define LOG_STR(x)          LOG_STR_HELPER(x)

#if (1U == CONF_LOG_EN)
    #define APP_LOG(__fmt__, ...)       SystemLog(__FILE__ ":" LOG_STR(__LINE__) ":",      __fmt__, ##__VA_ARGS__)
    #define APP_ERR(__fmt__, ...)       SystemLog(__FILE__ ":" LOG_STR(__LINE__) ":ERR:",  __fmt__, ##__VA_ARGS__)
    #define APP_INFO(__fmt__, ...)      SystemLog(__FILE__ ":" LOG_STR(__LINE__) ":INFO:", __fmt__, ##__VA_ARGS__)
    #define APP_WARN(__fmt__, ...)      SystemLog(__FILE__ ":" LOG_STR(__LINE__) ":WARN:", __fmt__, ##__VA_ARGS__)
#else 
    #define APP_LOG(__fmt__, ...)
    #define APP_ERR(__fmt__, ...)
    #define APP_INFO(__fmt__, ...)
    #define APP_WARN(__fmt__, ...)
#endif /* (1U == CONF_LOG_EN) */

/* definitions ************************************************************************************
 **************************************************************************************************/

typedef const char *    const_str_t;

/* exported variables *****************************************************************************
 **************************************************************************************************/

/* exported functions *****************************************************************************
 **************************************************************************************************/

void SystemLog(const_str_t pos, const_str_t format, ...);

/* reserved ***************************************************************************************
 **************************************************************************************************/

#endif /* __APP_COMMON_LOG_H__ */