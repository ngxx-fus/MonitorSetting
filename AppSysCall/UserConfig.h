/* file-desc **************************************************************************************
 * @filename    UserConfig.h
 * @brief       Load and save user display preferences (disp.yml) and application config (app.yml).
 * @note        Follows DisplayPreferSettings and App YAML schema in mdf-app-syscall.md.
 **************************************************************************************************/

#ifndef __APP_SYSCALL_USER_CONFIG_H__
#define __APP_SYSCALL_USER_CONFIG_H__

/* headers ****************************************************************************************
 **************************************************************************************************/

#include "../AppCommon/All.h"
#include "./__config__.h"
#include "./Xrandr.h"
#include <stdint.h>

/* macros *****************************************************************************************
 **************************************************************************************************/

#define USER_CFG_DEFAULT_DISP_PATH      "UserConfig/disp.yml"
#define USER_CFG_DEFAULT_APP_PATH       "UserConfig/app.yml"
#define USER_CFG_MAX_LAYERS             (3U)
#define USER_CFG_MAX_COMPS              (12U)

/* definitions ************************************************************************************
 **************************************************************************************************/

/* Preferred display setting entry loaded from or saved to disp.yml (Disp0, Disp1, Disp2...) */
struct DispPreferItem_st {
    char    Label[SYSCALL_LABEL_LEN];   /* Target monitor connector name (e.g. "eDP-1", or "" = auto index) */
    int32_t Enabled;                    /* -1 = auto (default 1 = ON), 1 = ON, 0 = OFF (--off) */
    int32_t Primary;                    /* -1 = auto, 1 = Primary (--primary), 0 = Non-primary */
    int32_t W;                          /* -1 = auto select max available width */
    int32_t H;                          /* -1 = auto select max available height */
    int32_t Freq;                       /* -1 = auto select max available refresh rate */
    int32_t PosX;                       /* -1 = skip / use default UI position */
    int32_t PosY;                       /* -1 = skip / use default UI position */
};
typedef struct DispPreferItem_st DispPreferItem_t;

/* Collection of preferred display settings from disp.yml */
struct DispPreferConfig_st {
    uint32_t         Count;
    DispPreferItem_t Disp[SYSCALL_MAX_MONITORS];
};
typedef struct DispPreferConfig_st DispPreferConfig_t;

/* Per-component geometry and style override loaded from Layers in app.yml */
struct AppCompYamlCfg_st {
    uint8_t  HasOffsetX;
    uint8_t  HasOffsetY;
    uint8_t  HasW;
    uint8_t  HasH;
    uint8_t  HasColor;
    uint8_t  HasExtTextColor;
    uint8_t  HasBorderSize;
    uint8_t  HasBorderColor;
    uint8_t  HasRadius;
    uint8_t  HasFilled;
    uint8_t  HasThickness;
    uint8_t  HasEndX;
    uint8_t  HasEndY;
    uint8_t  HasFontSize;
    uint8_t  HasFont;
    uint8_t  HasBold;
    uint8_t  HasItalic;
    uint8_t  HasAlign;
    uint8_t  HasVAlign;
    uint8_t  HasVisible;
    int32_t  OffsetX;
    int32_t  OffsetY;
    uint32_t W;
    uint32_t H;
    uint32_t Color;
    uint32_t ExtTextColor;
    uint32_t BorderSize;
    uint32_t BorderColor;
    uint32_t Radius;
    uint8_t  IsFilled;
    uint32_t Thickness;
    int32_t  EndX;
    int32_t  EndY;
    uint16_t FontSize;
    char     Font[128];
    uint8_t  IsBold;
    uint8_t  IsItalic;
    uint8_t  Align;                     /* 0 = LEFT, 1 = CENTER, 2 = RIGHT */
    uint8_t  VAlign;                    /* 0 = TOP,  1 = MIDDLE/CENTER, 2 = BOTTOM */
    uint8_t  IsVisible;
};
typedef struct AppCompYamlCfg_st AppCompYamlCfg_t;

/* Application-level settings loaded from app.yml */
struct AppYamlConfig_st {
    char             PreferDispPath[SYSCALL_PATH_LEN];
    uint32_t         SnapThresholdPx;
    char             WinTitle[128];
    char             WinFont[128];
    uint8_t          WinHasBold;
    uint8_t          WinIsBold;
    uint8_t          WinHasItalic;
    uint8_t          WinIsItalic;
    uint32_t         WinW;
    uint32_t         WinH;
    uint8_t          WinIsVisible;
    uint8_t          WinIsMaximized;
    AppCompYamlCfg_t Comps[USER_CFG_MAX_LAYERS][USER_CFG_MAX_COMPS];
    uint32_t         ApplyCmdNum;
    char             ApplyCmds[SYSCALL_MAX_APPLY_CMDS][SYSCALL_LINE_LEN];
};
typedef struct AppYamlConfig_st AppYamlConfig_t;

/* exported variables *****************************************************************************
 **************************************************************************************************/

/* exported functions *****************************************************************************
 **************************************************************************************************/

Err_t                   SysCallDispPreferInitDefault(DispPreferConfig_t *p_pref, uint32_t count);
Err_t                   SysCallDispPreferLoad(const_str_t path, DispPreferConfig_t *p_pref);
Err_t                   SysCallDispPreferSave(const_str_t path, const DispPreferConfig_t *p_pref);
const DispPreferItem_t *SysCallDispPreferFindMatch(const DispPreferConfig_t *p_pref,
                                                   const_str_t               label,
                                                   uint32_t                  fallback_idx);
Err_t                   SysCallDispPreferApplyToMonitors(const DispPreferConfig_t *p_pref,
                                                         const MonitorList_t      *p_mon_list,
                                                         MonitorCfgList_t         *p_out_cfg);
Err_t                   SysCallAppConfigLoad(const_str_t path, AppYamlConfig_t *p_app_cfg);
Err_t                   SysCallAppRunApplyCallbacks(const AppYamlConfig_t *p_app_cfg);

/* reserved ***************************************************************************************
 **************************************************************************************************/

#endif /* __APP_SYSCALL_USER_CONFIG_H__ */

