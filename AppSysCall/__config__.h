/* file-desc **************************************************************************************
 * @filename    __config__.h
 * @brief       Private configuration macros for the AppSysCall module.
 * @note        Shared internally across AppSysCall sources.
 **************************************************************************************************/

#ifndef __APP_SYSCALL_CONFIG_H__
#define __APP_SYSCALL_CONFIG_H__

/* headers ****************************************************************************************
 **************************************************************************************************/

#include "../AppCommon/__config__.h"

/* macros *****************************************************************************************
 **************************************************************************************************/

#ifndef PARAM_CHECK_EN
    #ifdef APP_PARAM_CHECK
        #define PARAM_CHECK_EN          APP_PARAM_CHECK
    #else
        #define PARAM_CHECK_EN          (1U)
    #endif
#endif

#define SYSCALL_MAX_MONITORS            (8U)        /* Max connected outputs tracked */
#define SYSCALL_MAX_MODES               (48U)       /* Max resolutions per output */
#define SYSCALL_MAX_FREQS               (12U)       /* Max refresh rates per resolution */
#define SYSCALL_LABEL_LEN               (32U)       /* Output name length (e.g. "HDMI-1") */
#define SYSCALL_LINE_LEN                (512U)      /* Max length of one text line */
#define SYSCALL_CMD_LEN                 (2048U)     /* Max length of one shell command */
#define SYSCALL_PATH_LEN                (256U)      /* Max length of a file path */
#define SYSCALL_XRANDR_BIN              "xrandr"    /* xrandr executable */
#define SYSCALL_FREQ_TOLERANCE_HZ       (1)         /* Allowed refresh mismatch when verifying */
#define SYSCALL_MAX_APPLY_CMDS          (32U)       /* Max post-Apply callback commands in app.yml */


/* definitions ************************************************************************************
 **************************************************************************************************/

/* exported variables *****************************************************************************
 **************************************************************************************************/

/* exported functions *****************************************************************************
 **************************************************************************************************/

/* reserved ***************************************************************************************
 **************************************************************************************************/

#endif /* __APP_SYSCALL_CONFIG_H__ */

