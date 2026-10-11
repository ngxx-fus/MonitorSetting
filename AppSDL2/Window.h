/* file-desc **************************************************************************************
 * @filename    Window.h
 * @brief       Main window modes, single-instance guard, Yes/No dialog, and update queue APIs.
 * @note        Supports floating/maximize modes, single-instance toggle, and quick Yes/No dialog.
 **************************************************************************************************/

#ifndef __APP_SDL2_WINDOW_H__
#define __APP_SDL2_WINDOW_H__

/* headers ****************************************************************************************
 **************************************************************************************************/

#include "./__config__.h"
#include "./Component.h"

/* macros *****************************************************************************************
 **************************************************************************************************/

#define APP_SDL2_SINGLE_INST_PID_FILE   "/tmp/MonitorSelection_AppSDL2.pid"

/* definitions ************************************************************************************
 **************************************************************************************************/

/* Window display mode (floating vs maximized) */
enum WinMode_e {
    WIN_MODE_FLOATING                   = (0U),
    WIN_MODE_MAXIMIZED                  = (1U)
};
typedef enum WinMode_e      WinMode_t;

/* Window update action command codes */
enum WinActionCmd_e {
    WIN_ACTION_NONE                     = (0U),
    WIN_ACTION_UPDATE_COMP              = (1U),
    WIN_ACTION_UPDATE_ALL               = (2U),
    WIN_ACTION_SHOW_WINDOW              = (3U),
    WIN_ACTION_HIDE_WINDOW              = (4U),
    WIN_ACTION_RESIZE_WINDOW            = (5U),
    WIN_ACTION_TOGGLE_WINDOW            = (6U),
    WIN_ACTION_SET_MODE                 = (7U)
};
typedef enum WinActionCmd_e WinActionCmd_t;

/* Action type union inside g_MainWindowUpdateQueue */
union WinActionType_un {
    WinActionCmd_t Cmd;
    struct {
        WinActionCmd_t Cmd;
        uint32_t       Width;
        uint32_t       Height;
    } Resize;
    struct {
        WinActionCmd_t Cmd;
        WinMode_t      Mode;
    } ModeCfg;
    uint64_t Raw;
};
typedef union WinActionType_un WinActionType_t;

/* Single action entry in g_MainWindowUpdateQueue */
struct WinUpdateAction_st {
    WinActionType_t Type;
    Comp_t         *p_Comp;
};
typedef struct WinUpdateAction_st WinUpdateAction_t;

/* Ring-buffer queue structure for g_MainWindowUpdateQueue backed by CircleQueue_t */
struct MainWindowUpdateQueue_st {
    WinUpdateAction_t Actions[APP_SDL2_MAX_QUEUE_ACTIONS];
    CircleQueue_t     Queue;
    void             *p_Mutex;
    void             *p_Cond;
    uint32_t          WakeupEventType;
};
typedef struct MainWindowUpdateQueue_st MainWindowUpdateQueue_t;

/* Main window state structure containing layers, mode, and UI render lock */
struct MainWindow_st {
    const_str_t Title;
    uint32_t    Width;
    uint32_t    Height;
    uint8_t     IsVisible;
    WinMode_t   Mode;
    uint32_t    LayerCount;
    Layer_t     Layers[APP_SDL2_MAX_LAYERS];
    void       *p_SdlWindow;
    void       *p_SdlRenderer;
    void       *p_UiMutex;
};
typedef struct MainWindow_st MainWindow_t;

/* exported variables *****************************************************************************
 **************************************************************************************************/

extern MainWindow_t            g_MainWindow;
extern MainWindowUpdateQueue_t g_MainWindowUpdateQueue;

/* exported functions *****************************************************************************
 **************************************************************************************************/

Err_t WindowEnsureSingleInstance(uint8_t *p_is_primary);
Err_t WindowCheckSingleInstanceSignal(void);
Err_t WindowInit(const_str_t title, uint32_t width, uint32_t height);
Err_t WindowDeinit(void);
Err_t WindowLockUI(void);
Err_t WindowUnlockUI(void);
Err_t WindowWakeupMainThread(void);
Err_t WindowShow(void);
Err_t WindowHide(void);
Err_t WindowToggle(void);
Err_t WindowSetMode(WinMode_t mode);
Err_t WindowResize(uint32_t width, uint32_t height);
Err_t WindowShowYesNoDialog(const_str_t title, const_str_t message, uint8_t *p_is_yes);
Err_t WindowUpdateAll(void);
Err_t WindowQueuePush(WinActionCmd_t cmd, Comp_t *p_comp);
Err_t WindowQueuePushResize(uint32_t width, uint32_t height);
Err_t WindowQueueProcessAll(void);

/* reserved ***************************************************************************************
 **************************************************************************************************/

#endif /* __APP_SDL2_WINDOW_H__ */
