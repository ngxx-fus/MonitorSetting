/* file-desc **************************************************************************************
 * @filename    Log.c
 * @brief       System logging implementation.
 * @note        Outputs formatted log messages with timestamp and file:line position.
 **************************************************************************************************/

/* headers ****************************************************************************************
 **************************************************************************************************/

#include "time.h"
#include "./Log.h"
#include "./__common__.h"

/* macros *****************************************************************************************
 **************************************************************************************************/

#define LOG_TIME_BUF_SIZE   (20U)
#define LOG_MSG_BUF_SIZE    (512U)

#ifndef PARAM_CHECK_EN
    #ifdef APP_PARAM_CHECK
        #define PARAM_CHECK_EN  APP_PARAM_CHECK
    #else
        #define PARAM_CHECK_EN  (0U)
    #endif
#endif

/* definitions ************************************************************************************
 **************************************************************************************************/

/* exported variables *****************************************************************************
 **************************************************************************************************/

/* public variables *******************************************************************************
 **************************************************************************************************/

/* private variables ******************************************************************************
 **************************************************************************************************/

/* exported functions *****************************************************************************
 **************************************************************************************************/

/* public functions *******************************************************************************
 **************************************************************************************************/

void SystemLog(const_str_t pos, const_str_t format, ...);

/* private functions ******************************************************************************
 **************************************************************************************************/

/* function implementations ***********************************************************************
 **************************************************************************************************/

/* Output a formatted log message with timestamp and source position
 * @param pos[in] Source file, line, and tag string (path/to/file:line:tag:)
 * @param format[in] Printf-style format string for the log message
 * @note Format: [YYYY/MM/DD HH:MM:SS] path/to/file:line:tag: <message>
 */
void SystemLog(const_str_t pos, const_str_t format, ...) {
    time_t raw_time;
    struct tm *time_info;
    char time_buf[LOG_TIME_BUF_SIZE];
    char msg_buf[LOG_MSG_BUF_SIZE];
    va_list args;

    #if (1U == PARAM_CHECK_EN)
    /* Check if input pointer parameters are NULL */
    if ((NULL == pos) || (NULL == format)) {
        /* Abort logging if mandatory arguments are NULL */
        return;
    }
    #endif /* (1U == PARAM_CHECK_EN) */

    /* Get current system epoch time */
    time(&raw_time);
    /* Convert epoch time to local time structure */
    time_info = localtime(&raw_time);

    /* Check if localtime conversion succeeded */
    if (NULL != time_info) {
        /* Format timestamp as YYYY/MM/DD HH:MM:SS */
        strftime(time_buf, sizeof(time_buf), "%Y/%m/%d %H:%M:%S", time_info);
    } else {
        /* Use fallback timestamp string on conversion failure */
        snprintf(time_buf, sizeof(time_buf), "0000/00/00 00:00:00");
    }

    /* Format variadic log message into buffer */
    va_start(args, format);
    vsnprintf(msg_buf, sizeof(msg_buf), format, args);
    va_end(args);

    /* Output formatted log line (pos already includes trailing colon) */
    LOG_ENGINE("[%s] %s %s\n", time_buf, pos, msg_buf);

    /* Return after printing the formatted log line */
    return;
}

/* reserved ***************************************************************************************
 **************************************************************************************************/