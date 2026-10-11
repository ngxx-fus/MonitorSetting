/* file-desc **************************************************************************************
 * @filename    Xrandr.h
 * @brief       Query, apply, verify, and roll back monitor configuration via xrandr.
 * @note        Data layout follows mdf-app-syscall.md (ConnectedMonitor -> Displays -> Freq[]).
 **************************************************************************************************/

#ifndef __APP_SYSCALL_XRANDR_H__
#define __APP_SYSCALL_XRANDR_H__

/* headers ****************************************************************************************
 **************************************************************************************************/

#include "../AppCommon/All.h"
#include "./__config__.h"
#include <stdint.h>

/* macros *****************************************************************************************
 **************************************************************************************************/

/* definitions ************************************************************************************
 **************************************************************************************************/

/* One resolution supported by an output, with all of its refresh rates */
struct DispMode_st {
    uint16_t W;
    uint16_t H;
    uint16_t FreqNum;
    uint16_t Freq[SYSCALL_MAX_FREQS];   /* Rounded Hz, de-duplicated, in xrandr order */
};
typedef struct DispMode_st DispMode_t;

/* One connected output (e.g. "HDMI-1") and its supported displays (modes) */
struct ConnectedMonitor_st {
    char       Label[SYSCALL_LABEL_LEN]; /* Exact xrandr output name */
    uint8_t    IsActive;                 /* 1U if the output currently drives a mode */
    uint8_t    IsPrimary;                /* 1U if the output is the primary one */
    uint16_t   CurW;                     /* Current width  (valid if IsActive) */
    uint16_t   CurH;                     /* Current height (valid if IsActive) */
    uint16_t   CurFreq;                  /* Current refresh rate in Hz (0 if unknown) */
    int32_t    PosX;                     /* Current X position (valid if IsActive) */
    int32_t    PosY;                     /* Current Y position (valid if IsActive) */
    uint16_t   DisplayNum;               /* Number of valid entries in Displays[] */
    DispMode_t Displays[SYSCALL_MAX_MODES];
};
typedef struct ConnectedMonitor_st ConnectedMonitor_t;

/* All connected outputs reported by xrandr */
struct MonitorList_st {
    uint32_t           Count;
    ConnectedMonitor_t Monitors[SYSCALL_MAX_MONITORS];
};
typedef struct MonitorList_st MonitorList_t;

/* Desired (or snapshot) configuration of one output */
struct MonitorCfg_st {
    char     Label[SYSCALL_LABEL_LEN];
    uint8_t  IsEnabled;                 /* 0U -> "--off" */
    uint8_t  IsPrimary;
    uint16_t W;
    uint16_t H;
    uint16_t Freq;                      /* Hz, 0 = let xrandr choose */
    int32_t  PosX;
    int32_t  PosY;
};
typedef struct MonitorCfg_st MonitorCfg_t;

/* Configuration for a set of outputs */
struct MonitorCfgList_st {
    uint32_t     Count;
    MonitorCfg_t Cfgs[SYSCALL_MAX_MONITORS];
};
typedef struct MonitorCfgList_st MonitorCfgList_t;

/* exported variables *****************************************************************************
 **************************************************************************************************/

/* exported functions *****************************************************************************
 **************************************************************************************************/

Err_t SysCallXrandrQuery(MonitorList_t *p_list);
Err_t SysCallXrandrGetCurrentCfg(const MonitorList_t *p_list, MonitorCfgList_t *p_cfg);
Err_t SysCallXrandrFindClosestMode(const ConnectedMonitor_t *p_mon, int32_t want_w, int32_t want_h,
                                   int32_t want_freq, uint16_t *p_mode_idx, uint16_t *p_freq);
Err_t SysCallXrandrFindModeIdx(const ConnectedMonitor_t *p_mon, uint16_t w, uint16_t h, uint16_t *p_mode_idx);
Err_t SysCallXrandrBuildCmd(const MonitorCfgList_t *p_cfg, char *p_cmd, uint32_t cmd_size);
Err_t SysCallXrandrApply(const MonitorCfgList_t *p_cfg);
Err_t SysCallXrandrVerify(const MonitorCfgList_t *p_cfg, uint8_t *p_is_match);
Err_t SysCallXrandrApplySafe(const MonitorCfgList_t *p_new_cfg, const MonitorCfgList_t *p_backup_cfg);

/* reserved ***************************************************************************************
 **************************************************************************************************/

#endif /* __APP_SYSCALL_XRANDR_H__ */

