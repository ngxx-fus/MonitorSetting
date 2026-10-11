/* file-desc **************************************************************************************
 * @filename    Window.h
 * @brief       Main window management, UI mutex/cond synchronization, and update queue APIs.
 * @note        Supports sleeping main thread wakeup via custom SDL event and condition variable.
 **************************************************************************************************/

#ifndef __APP_SDL2_WINDOW_H__
#define __APP_SDL2_WINDOW_H__

/* headers ****************************************************************************************
 **************************************************************************************************/

#include "./__config__.h"
#include "./Component.h"

/* macros *****************************************************************************************
 **************************************************************************************************/

/* definitions ************************************************************************************
 **************************************************************************************************/

/* Window update action command codes */
enum WinActionCmd_e {
    WIN_ACTION_NONE                     = (0U),
    WIN_ACTION_UPDATE_COMP              = (1U),
    WIN_ACTION_UPDATE_ALL               = (2U),
    WIN_ACTION_SHOW_WINDOW              = (3U),
    WIN_ACTION_HIDE_WINDOW              = (4U),
    WIN_ACTION_RESIZE_WINDOW            = (5U)
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
    uint64_t Raw;
};
typedef union WinActionType_un WinActionType_t;

/* Single action entry in g_MainWindowUpdateQueue */
struct WinUpdateAction_st {
    WinActionType_t Type;
    Comp_t         *p_Comp;
};
typedef struct WinUpdateAction_st WinUpdateAction_t;

/* Ring-buffer queue structure for g_MainWindowUpdateQueue with mutex and condition variable */
struct MainWindowUpdateQueue_st {
    WinUpdateAction_t Actions[APP_SDL2_MAX_QUEUE_ACTIONS];
    uint32_t          Head;
    uint32_t          Tail;
    uint32_t          Count;
    void             *p_Mutex;
    void             *p_Cond;
    uint32_t          WakeupEventType;
};
typedef struct MainWindowUpdateQueue_st MainWindowUpdateQueue_t;

/* Main window state structure containing layers and UI render lock */
struct MainWindow_st {
    const_str_t Title;
    uint32_t    Width;
    uint32_t    Height;
    uint8_t     IsVisible;
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

Err_t WindowInit(const_str_t title, uint32_t width, uint32_t height);
Err_t WindowDeinit(void);
Err_t WindowLockUI(void);
Err_t WindowUnlockUI(void);
Err_t WindowWakeupMainThread(void);
Err_t WindowShow(void);
Err_t WindowHide(void);
Err_t WindowResize(uint32_t width, uint32_t height);
Err_t WindowUpdateAll(void);
Err_t WindowQueuePush(WinActionCmd_t cmd, Comp_t *p_comp);
Err_t WindowQueuePushResize(uint32_t width, uint32_t height);
Err_t WindowQueueProcessAll(void);

/* reserved ***************************************************************************************
 **************************************************************************************************/

#endif /* __APP_SDL2_WINDOW_H__ */
