/* file-desc **************************************************************************************
 * @filename    Window.c
 * @brief       Main window lifecycle, single-instance guard, modes, Yes/No dialog, and queue.
 * @note        Uses POSIX signals (SIGUSR1/SIGUSR2) for single-instance toggle and user signals.
 **************************************************************************************************/

/* headers ****************************************************************************************
 **************************************************************************************************/

#include "./__common__.h"
#include "./Window.h"
#include "./Draw.h"
#include "./Event.h"
#include <signal.h>
#include <unistd.h>

/* macros *****************************************************************************************
 **************************************************************************************************/

/* definitions ************************************************************************************
 **************************************************************************************************/

/* exported variables *****************************************************************************
 **************************************************************************************************/

/* public variables *******************************************************************************
 **************************************************************************************************/

MainWindow_t g_MainWindow = {
    .Title         = "MonitorSelection",
    .Width         = APP_SDL2_DEFAULT_WIN_W,
    .Height        = APP_SDL2_DEFAULT_WIN_H,
    .IsVisible     = 0U,
    .Mode          = WIN_MODE_FLOATING,
    .LayerCount    = 0U,
    .p_SdlWindow   = NULL,
    .p_SdlRenderer = NULL,
    .p_UiMutex     = NULL
};

MainWindowUpdateQueue_t g_MainWindowUpdateQueue = {
    .p_Mutex         = NULL,
    .p_Cond          = NULL,
    .WakeupEventType = 0U
};

/* private variables ******************************************************************************
 **************************************************************************************************/

static volatile sig_atomic_t g_SigTogglePending = 0;
static volatile sig_atomic_t g_SigUserPending   = 0;

/* exported functions *****************************************************************************
 **************************************************************************************************/

/* public functions *******************************************************************************
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
Err_t WindowQueuePushMode(WinMode_t mode);
Err_t WindowQueuePushResize(uint32_t width, uint32_t height);
Err_t WindowQueueProcessAll(void);

/* private functions ******************************************************************************
 **************************************************************************************************/

static void OnPosixSignalReceived(int sig_num);

/* function implementations ***********************************************************************
 **************************************************************************************************/

/* POSIX signal handler for single-instance toggle (SIGUSR1) and custom user signal (SIGUSR2)
 * @param sig_num[in] Signal number received from OS
 * @note Sets atomic flags processed safely on the main thread
 */
static void OnPosixSignalReceived(int sig_num) {
    /* Check which POSIX signal was delivered */
    if (SIGUSR1 == sig_num) {
        g_SigTogglePending = 1;
    } else if (SIGUSR2 == sig_num) {
        g_SigUserPending = 1;
    } else {
        /* Ignore other signals */
    }

    /* Return from signal handler */
    return;
}

/* Enforce single application instance; if already running, signal the 1st instance to toggle
 * @param p_is_primary[out] Outputs 1U if this process is the 1st instance, 0U if secondary
 * @return APP_SUCCESS on success, or error code
 * @note Secondary instance sends SIGUSR1 to the primary instance PID and exits
 */
Err_t WindowEnsureSingleInstance(uint8_t *p_is_primary) {
    FILE *p_file;
    int   existing_pid = 0;

    #if (1U == PARAM_CHECK_EN)
    /* Validate output flag pointer */
    if (NULL == p_is_primary) {
        APP_ERR("p_is_primary is NULL!");
        return APP_ERROR_NULL;
    }
    #endif /* (1U == PARAM_CHECK_EN) */

    *p_is_primary = 1U;

    /* Check if a PID lock file already exists */
    p_file = fopen(APP_SDL2_SINGLE_INST_PID_FILE, "r");
    if (NULL != p_file) {
        if (1 == fscanf(p_file, "%d", &existing_pid)) {
            /* Check if existing process is alive and not this process */
            if ((existing_pid > 0) && (existing_pid != (int)getpid()) &&
                (0 == kill((pid_t)existing_pid, 0))) {
                /* Send SIGUSR1 to toggle the 1st instance window */
                (void)kill((pid_t)existing_pid, SIGUSR1);
                (void)fclose(p_file);
                *p_is_primary = 0U;
                APP_INFO("Primary instance (PID %d) is already running. Toggled primary window.",
                         existing_pid);
                /* Return immediately so secondary instance can exit */
                return APP_SUCCESS;
            }
        }
        (void)fclose(p_file);
    }

    /* Write current process PID as the primary instance */
    p_file = fopen(APP_SDL2_SINGLE_INST_PID_FILE, "w");
    if (NULL != p_file) {
        (void)fprintf(p_file, "%d\n", (int)getpid());
        (void)fclose(p_file);
    }

    /* Register POSIX signal handlers for single-instance toggle and custom user signal */
    (void)signal(SIGUSR1, OnPosixSignalReceived);
    (void)signal(SIGUSR2, OnPosixSignalReceived);

    /* Return success as primary instance */
    return APP_SUCCESS;
}

/* Check and process any pending POSIX signals (SIGUSR1 window toggle / SIGUSR2 custom signal)
 * @return APP_SUCCESS on success
 * @note Called by the main thread loop
 */
Err_t WindowCheckSingleInstanceSignal(void) {
    /* Check if SIGUSR1 toggle signal was received */
    if (0 != g_SigTogglePending) {
        g_SigTogglePending = 0;
        /* Queue window visibility toggle */
        (void)WindowQueuePush(WIN_ACTION_TOGGLE_WINDOW, NULL);
    }

    /* Check if SIGUSR2 custom user signal was received */
    if (0 != g_SigUserPending) {
        g_SigUserPending = 0;
        /* Forward OS custom signal into EventListenerThread */
        (void)EventSendUserSignal((uint32_t)SIGUSR2, NULL);
    }

    /* Return success after checking signals */
    return APP_SUCCESS;
}

/* Initialize the SDL2 main window, renderer, UI mutex, and wakeup event for g_MainWindowUpdateQueue
 * @param title[in] Window title string
 * @param width[in] Initial window width in pixels
 * @param height[in] Initial window height in pixels
 * @return APP_SUCCESS on success, or error code on invalid parameters / SDL failure
 * @note Registers custom SDL wakeup event so sub-threads can wake the main thread from sleep
 */
Err_t WindowInit(const_str_t title, uint32_t width, uint32_t height) {
    uint32_t      idx;
    SDL_Window   *p_win;
    SDL_Renderer *p_ren;

    #if (1U == PARAM_CHECK_EN)
    /* Validate window title and dimensions */
    if ((NULL == title) || (0U == width) || (0U == height)) {
        APP_ERR("Invalid window initialization parameters!");
        return APP_ERROR_INVALID_PARAM;
    }
    #endif /* (1U == PARAM_CHECK_EN) */

    /* Initialize SDL2 video, events, and timer subsystems */
    if (0 != SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_EVENTS)) {
        APP_ERR("SDL_Init failed: %s", SDL_GetError());
        return APP_ERROR_INIT_FAILED;
    }

    /* Initialize SDL_ttf subsystem if available */
    (void)TTF_Init();

    /* Set X11 window type hint to dialog so tiling WMs (e.g. bspwm/i3) float the window */
    (void)SDL_SetHint("SDL_VIDEO_X11_NET_WM_WINDOW_TYPE", "_NET_WM_WINDOW_TYPE_DIALOG");

    /* Create floating SDL2 main window (without SDL_WINDOW_RESIZABLE so tiling WMs float it) */
    p_win = SDL_CreateWindow(title,
                             SDL_WINDOWPOS_CENTERED,
                             SDL_WINDOWPOS_CENTERED,
                             (int)width,
                             (int)height,
                             SDL_WINDOW_SHOWN);
    if (NULL == p_win) {
        APP_ERR("SDL_CreateWindow failed: %s", SDL_GetError());
        SDL_Quit();
        return APP_ERROR_INIT_FAILED;
    }

    /* Lock min/max size to initial floating dimensions so tiling WMs keep it floating */
    SDL_SetWindowMinimumSize(p_win, (int)width, (int)height);
    SDL_SetWindowMaximumSize(p_win, (int)width, (int)height);
    SDL_SetWindowResizable(p_win, SDL_FALSE);

    /* Create hardware-accelerated or software fallback SDL2 renderer with target-texture support */
    p_ren = SDL_CreateRenderer(p_win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC | SDL_RENDERER_TARGETTEXTURE);
    if (NULL == p_ren) {
        /* Fall back to software renderer if hardware acceleration is unavailable */
        p_ren = SDL_CreateRenderer(p_win, -1, SDL_RENDERER_SOFTWARE | SDL_RENDERER_TARGETTEXTURE);
    }
    if (NULL == p_ren) {
        APP_ERR("SDL_CreateRenderer failed: %s", SDL_GetError());
        SDL_DestroyWindow(p_win);
        SDL_Quit();
        return APP_ERROR_INIT_FAILED;
    }

    g_MainWindow.Title         = title;
    g_MainWindow.Width         = width;
    g_MainWindow.Height        = height;
    g_MainWindow.IsVisible     = 1U;
    g_MainWindow.Mode          = WIN_MODE_FLOATING;
    g_MainWindow.LayerCount    = APP_SDL2_MAX_LAYERS;
    g_MainWindow.p_SdlWindow   = (void *)p_win;
    g_MainWindow.p_SdlRenderer = (void *)p_ren;
    g_MainWindow.p_UiMutex     = (void *)SDL_CreateMutex();
    ASSERT_NOT_EQUAL(NULL, g_MainWindow.p_UiMutex);

    /* Reset all layers in g_MainWindow */
    for (idx = 0U; idx < APP_SDL2_MAX_LAYERS; idx++) {
        g_MainWindow.Layers[idx].LayerId          = idx;
        g_MainWindow.Layers[idx].IsVisible        = 1U;
        g_MainWindow.Layers[idx].IsScrollable     = 0U;
        g_MainWindow.Layers[idx].ScrollX          = 0;
        g_MainWindow.Layers[idx].ScrollY          = 0;
        g_MainWindow.Layers[idx].RectClip.W       = 0U;
        g_MainWindow.Layers[idx].RectClip.H       = 0U;
        g_MainWindow.Layers[idx].RectClip.OffsetX = 0;
        g_MainWindow.Layers[idx].RectClip.OffsetY = 0;
        g_MainWindow.Layers[idx].RectClip.Rotation = 0U;
        g_MainWindow.Layers[idx].CompCount        = 0U;
        g_MainWindow.Layers[idx].p_Comps          = NULL;
    }

    /* Initialize circular queue for UI update actions (allow_overwrite = 1U) */
    ASSERT_EQUAL(APP_SUCCESS,
                 CircleQueueInit(&g_MainWindowUpdateQueue.Queue,
                                 g_MainWindowUpdateQueue.Actions,
                                 (uint32_t)sizeof(WinUpdateAction_t),
                                 APP_SDL2_MAX_QUEUE_ACTIONS,
                                 1U));
    g_MainWindowUpdateQueue.p_Mutex         = (void *)SDL_CreateMutex();
    g_MainWindowUpdateQueue.p_Cond          = (void *)SDL_CreateCond();
    g_MainWindowUpdateQueue.WakeupEventType = SDL_RegisterEvents(1);
    ASSERT_NOT_EQUAL(NULL, g_MainWindowUpdateQueue.p_Mutex);
    ASSERT_NOT_EQUAL(NULL, g_MainWindowUpdateQueue.p_Cond);

    /* Return success after initializing main window */
    return APP_SUCCESS;
}

/* Destroy the SDL2 main window, synchronization primitives, PID file, and renderer resources
 * @return APP_SUCCESS on success
 * @note Safe to call when shutting down the application
 */
Err_t WindowDeinit(void) {
    /* Remove single-instance PID lock file */
    (void)unlink(APP_SDL2_SINGLE_INST_PID_FILE);

    /* Destroy queue condition variable if allocated */
    if (NULL != g_MainWindowUpdateQueue.p_Cond) {
        SDL_DestroyCond((SDL_cond *)g_MainWindowUpdateQueue.p_Cond);
        g_MainWindowUpdateQueue.p_Cond = NULL;
    }

    /* Destroy queue mutex if allocated */
    if (NULL != g_MainWindowUpdateQueue.p_Mutex) {
        SDL_DestroyMutex((SDL_mutex *)g_MainWindowUpdateQueue.p_Mutex);
        g_MainWindowUpdateQueue.p_Mutex = NULL;
    }

    /* Destroy UI mutex if allocated */
    if (NULL != g_MainWindow.p_UiMutex) {
        SDL_DestroyMutex((SDL_mutex *)g_MainWindow.p_UiMutex);
        g_MainWindow.p_UiMutex = NULL;
    }

    /* Destroy SDL renderer if allocated */
    if (NULL != g_MainWindow.p_SdlRenderer) {
        SDL_DestroyRenderer((SDL_Renderer *)g_MainWindow.p_SdlRenderer);
        g_MainWindow.p_SdlRenderer = NULL;
    }

    /* Destroy SDL window if allocated */
    if (NULL != g_MainWindow.p_SdlWindow) {
        SDL_DestroyWindow((SDL_Window *)g_MainWindow.p_SdlWindow);
        g_MainWindow.p_SdlWindow = NULL;
    }

    g_MainWindow.IsVisible = 0U;
    TTF_Quit();
    SDL_Quit();

    /* Return success after deinitializing main window */
    return APP_SUCCESS;
}

/* Acquire the UI data mutex before modifying shared component/layer properties
 * @return APP_SUCCESS on lock acquisition, or APP_ERROR on failure
 * @note Prevents race conditions between sub-thread callbacks and main-thread rendering
 */
Err_t WindowLockUI(void) {
    /* Lock UI mutex if initialized */
    if (NULL != g_MainWindow.p_UiMutex) {
        if (0 != SDL_LockMutex((SDL_mutex *)g_MainWindow.p_UiMutex)) {
            APP_ERR("SDL_LockMutex failed on p_UiMutex!");
            return APP_ERROR;
        }
    }

    /* Return success after acquiring UI lock */
    return APP_SUCCESS;
}

/* Release the UI data mutex after modifying shared component/layer properties
 * @return APP_SUCCESS on unlock, or APP_ERROR on failure
 * @note Must be paired with WindowLockUI
 */
Err_t WindowUnlockUI(void) {
    /* Unlock UI mutex if initialized */
    if (NULL != g_MainWindow.p_UiMutex) {
        if (0 != SDL_UnlockMutex((SDL_mutex *)g_MainWindow.p_UiMutex)) {
            APP_ERR("SDL_UnlockMutex failed on p_UiMutex!");
            return APP_ERROR;
        }
    }

    /* Return success after releasing UI lock */
    return APP_SUCCESS;
}

/* Push a custom wakeup event to the SDL event queue to wake a sleeping main thread
 * @return APP_SUCCESS on success
 * @note Allows main thread to sleep in SDL_WaitEvent without busy polling
 */
Err_t WindowWakeupMainThread(void) {
    /* Check if custom wakeup event type was registered */
    if ((0U != g_MainWindowUpdateQueue.WakeupEventType) &&
        ((uint32_t)-1 != g_MainWindowUpdateQueue.WakeupEventType)) {
        SDL_Event wake_ev;
        SDL_zero(wake_ev);
        wake_ev.type = g_MainWindowUpdateQueue.WakeupEventType;
        /* Push custom event to wake SDL_WaitEvent on the main thread */
        (void)SDL_PushEvent(&wake_ev);
    }

    /* Signal queue condition variable for any thread waiting on p_Cond */
    if (NULL != g_MainWindowUpdateQueue.p_Cond) {
        (void)SDL_CondSignal((SDL_cond *)g_MainWindowUpdateQueue.p_Cond);
    }

    /* Return success after signaling wakeup */
    return APP_SUCCESS;
}

/* Show the main window on screen and raise focus
 * @return APP_SUCCESS on success
 * @note Marks g_MainWindow.IsVisible as 1U and calls SDL_ShowWindow + SDL_RaiseWindow
 */
Err_t WindowShow(void) {
    g_MainWindow.IsVisible = 1U;
    /* Show and focus underlying SDL window if valid */
    if (NULL != g_MainWindow.p_SdlWindow) {
        SDL_ShowWindow((SDL_Window *)g_MainWindow.p_SdlWindow);
        SDL_RaiseWindow((SDL_Window *)g_MainWindow.p_SdlWindow);
    }

    /* Return success after showing window */
    return APP_SUCCESS;
}

/* Hide the main window from screen
 * @return APP_SUCCESS on success
 * @note Marks g_MainWindow.IsVisible as 0U and calls SDL_HideWindow
 */
Err_t WindowHide(void) {
    g_MainWindow.IsVisible = 0U;
    /* Hide underlying SDL window if valid */
    if (NULL != g_MainWindow.p_SdlWindow) {
        SDL_HideWindow((SDL_Window *)g_MainWindow.p_SdlWindow);
    }

    /* Return success after hiding window */
    return APP_SUCCESS;
}

/* Toggle the main window between shown and hidden states
 * @return APP_SUCCESS on success
 * @note Invoked when a 2nd application instance is launched
 */
Err_t WindowToggle(void) {
    /* Hide if currently visible, otherwise show and focus */
    if (0U != g_MainWindow.IsVisible) {
        return WindowHide();
    }

    /* Show window if currently hidden */
    return WindowShow();
}

/* Switch the main window display mode between floating and maximized
 * @param mode[in] Target window mode (WIN_MODE_FLOATING or WIN_MODE_MAXIMIZED)
 * @return APP_SUCCESS on success, or APP_ERROR_INVALID_PARAM
 * @note Works across both standard stacking WMs and tiling WMs (e.g. bspwm/i3)
 */
Err_t WindowSetMode(WinMode_t mode) {
    SDL_Window *p_win = (SDL_Window *)g_MainWindow.p_SdlWindow;

    /* Apply requested window mode */
    switch (mode) {
        case WIN_MODE_FLOATING:
            g_MainWindow.Mode = WIN_MODE_FLOATING;
            if (NULL != p_win) {
                SDL_RestoreWindow(p_win);
                SDL_SetWindowMinimumSize(p_win, (int)g_MainWindow.Width, (int)g_MainWindow.Height);
                SDL_SetWindowMaximumSize(p_win, (int)g_MainWindow.Width, (int)g_MainWindow.Height);
                SDL_SetWindowSize(p_win, (int)g_MainWindow.Width, (int)g_MainWindow.Height);
                SDL_SetWindowResizable(p_win, SDL_FALSE);
                SDL_SetWindowPosition(p_win, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
            }
            /* Break after restoring floating window */
            break;
        case WIN_MODE_MAXIMIZED:
            g_MainWindow.Mode = WIN_MODE_MAXIMIZED;
            if (NULL != p_win) {
                SDL_Rect usable_bounds;
                int      disp_idx = SDL_GetWindowDisplayIndex(p_win);

                if (disp_idx < 0) {
                    disp_idx = 0;
                }
                SDL_SetWindowResizable(p_win, SDL_TRUE);
                SDL_SetWindowMinimumSize(p_win, (int)g_MainWindow.Width, (int)g_MainWindow.Height);
                SDL_SetWindowMaximumSize(p_win, 16384, 16384);
                /* Expand floating window to fill usable desktop bounds (supports bspwm/tiling WMs) */
                if (0 == SDL_GetDisplayUsableBounds(disp_idx, &usable_bounds)) {
                    SDL_SetWindowPosition(p_win, usable_bounds.x, usable_bounds.y);
                    SDL_SetWindowSize(p_win, usable_bounds.w, usable_bounds.h);
                }
                SDL_MaximizeWindow(p_win);
            }
            /* Break after maximizing window */
            break;
        default:
            APP_ERR("Unsupported window mode!");
            return APP_ERROR_INVALID_PARAM;
    }

    /* Trigger full redraw after changing window mode */
    return WindowUpdateAll();
}

/* Adjust the width and height of the main window
 * @param width[in] New window width in pixels
 * @param height[in] New window height in pixels
 * @return APP_SUCCESS on success, or APP_ERROR_INVALID_PARAM if width/height is 0
 * @note Updates g_MainWindow dimensions and triggers a full redraw
 */
Err_t WindowResize(uint32_t width, uint32_t height) {
    #if (1U == PARAM_CHECK_EN)
    /* Validate non-zero window dimensions */
    if ((0U == width) || (0U == height)) {
        APP_ERR("Window width and height must be non-zero!");
        return APP_ERROR_INVALID_PARAM;
    }
    #endif /* (1U == PARAM_CHECK_EN) */

    g_MainWindow.Width  = width;
    g_MainWindow.Height = height;

    /* Resize SDL window if initialized */
    if (NULL != g_MainWindow.p_SdlWindow) {
        SDL_SetWindowMinimumSize((SDL_Window *)g_MainWindow.p_SdlWindow, (int)width, (int)height);
        if (WIN_MODE_FLOATING == g_MainWindow.Mode) {
            SDL_SetWindowMaximumSize((SDL_Window *)g_MainWindow.p_SdlWindow, (int)width, (int)height);
        }
        SDL_SetWindowSize((SDL_Window *)g_MainWindow.p_SdlWindow, (int)width, (int)height);
    }

    /* Re-render all window layers after resize */
    return WindowUpdateAll();
}

/* Display a modal Yes/No quick confirmation dialog box
 * @param title[in] Dialog title string
 * @param message[in] Dialog prompt message
 * @param p_is_yes[out] Outputs 1U if user clicked Yes, 0U if user clicked No or closed dialog
 * @return APP_SUCCESS on success, or error code
 * @note Uses SDL_ShowMessageBox with Yes and No buttons
 */
Err_t WindowShowYesNoDialog(const_str_t title, const_str_t message, uint8_t *p_is_yes) {
    int button_id = 0;
    const SDL_MessageBoxButtonData buttons[2] = {
        { SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 1, "Yes" },
        { SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, "No"  }
    };
    SDL_MessageBoxData msgbox_data;

    #if (1U == PARAM_CHECK_EN)
    /* Validate dialog parameters */
    if ((NULL == title) || (NULL == message) || (NULL == p_is_yes)) {
        APP_ERR("Null parameter in WindowShowYesNoDialog!");
        return APP_ERROR_NULL;
    }
    #endif /* (1U == PARAM_CHECK_EN) */

    msgbox_data.flags       = SDL_MESSAGEBOX_INFORMATION;
    msgbox_data.window      = (SDL_Window *)g_MainWindow.p_SdlWindow;
    msgbox_data.title       = title;
    msgbox_data.message     = message;
    msgbox_data.numbuttons  = 2;
    msgbox_data.buttons     = buttons;
    msgbox_data.colorScheme = NULL;

    /* Show native SDL2 Yes/No modal dialog */
    if (0 != SDL_ShowMessageBox(&msgbox_data, &button_id)) {
        APP_ERR("SDL_ShowMessageBox failed: %s", SDL_GetError());
        *p_is_yes = 0U;
        return APP_ERROR;
    }

    *p_is_yes = (1 == button_id) ? 1U : 0U;

    /* Return success after user responds to dialog */
    return APP_SUCCESS;
}

/* Redraw all visible layers and components in the main window using non-blocking UI lock
 * @return APP_SUCCESS on success
 * @note Uses SDL_TryLockMutex to avoid UI freeze if a sub-thread holds p_UiMutex; re-queues if busy
 */
Err_t WindowUpdateAll(void) {
    uint32_t      layer_idx;
    SDL_Renderer *p_ren;

    /* Skip rendering if the main window is currently hidden */
    if (0U == g_MainWindow.IsVisible) {
        /* Return early when window is hidden */
        return APP_SUCCESS;
    }

    /* Try acquiring UI mutex without blocking the main thread */
    if (NULL != g_MainWindow.p_UiMutex) {
        int lock_res = SDL_TryLockMutex((SDL_mutex *)g_MainWindow.p_UiMutex);
        if (0 != lock_res) {
            /* Sub-thread is currently updating component tree; re-queue redraw to avoid UI lock */
            (void)WindowQueuePush(WIN_ACTION_UPDATE_ALL, NULL);
            /* Return immediately to keep UI responsive */
            return APP_SUCCESS;
        }
    }

    p_ren = (SDL_Renderer *)g_MainWindow.p_SdlRenderer;
    /* Clear background to dark slate before drawing layers */
    if (NULL != p_ren) {
        (void)SDL_SetRenderDrawColor(p_ren, 24U, 28U, 36U, 255U);
        (void)SDL_RenderClear(p_ren);
    }

    /* Render each layer in ascending z-order */
    for (layer_idx = 0U; layer_idx < g_MainWindow.LayerCount; layer_idx++) {
        /* Draw all visible components in the current layer */
        (void)DrawLayer(&g_MainWindow.Layers[layer_idx]);
    }

    /* Release UI mutex before presenting frame */
    if (NULL != g_MainWindow.p_UiMutex) {
        (void)SDL_UnlockMutex((SDL_mutex *)g_MainWindow.p_UiMutex);
    }

    /* Present rendered frame to the screen */
    if (NULL != p_ren) {
        SDL_RenderPresent(p_ren);
    }

    /* Return success after presenting updated frame */
    return APP_SUCCESS;
}

/* Push an update action from a sub-thread into g_MainWindowUpdateQueue and wake main thread
 * @param cmd[in] Action command type
 * @param p_comp[in] Pointer to destination component (may be NULL for global window actions)
 * @return APP_SUCCESS on success, or error code
 * @note Protected by g_MainWindowUpdateQueue.p_Mutex and wakes sleeping main thread
 */
Err_t WindowQueuePush(WinActionCmd_t cmd, Comp_t *p_comp) {
    WinUpdateAction_t action;
    Err_t             err;

    #if (1U == PARAM_CHECK_EN)
    /* Validate command type */
    if (WIN_ACTION_NONE == cmd) {
        APP_ERR("Invalid window action command!");
        return APP_ERROR_INVALID_PARAM;
    }
    #endif /* (1U == PARAM_CHECK_EN) */

    action.Type.Raw = 0ULL;
    action.Type.Cmd = cmd;
    action.p_Comp   = p_comp;

    /* Lock queue mutex if available */
    if (NULL != g_MainWindowUpdateQueue.p_Mutex) {
        (void)SDL_LockMutex((SDL_mutex *)g_MainWindowUpdateQueue.p_Mutex);
    }

    /* Push action into circular queue (overwrites oldest if full) */
    err = CircleQueuePush(&g_MainWindowUpdateQueue.Queue, &action);

    /* Unlock queue mutex */
    if (NULL != g_MainWindowUpdateQueue.p_Mutex) {
        (void)SDL_UnlockMutex((SDL_mutex *)g_MainWindowUpdateQueue.p_Mutex);
    }

    /* Return error if push failed */
    if (APP_SUCCESS != err) {
        return err;
    }

    /* Wake up sleeping main thread so it processes the queued UI action immediately */
    (void)WindowWakeupMainThread();

    /* Return success after queuing action */
    return APP_SUCCESS;
}

/* Push a window mode change action from a sub-thread into g_MainWindowUpdateQueue and wake main thread
 * @param mode[in] Target window mode (WIN_MODE_FLOATING or WIN_MODE_MAXIMIZED)
 * @return APP_SUCCESS on success, or error status
 * @note Ensures SDL2 window mode changes execute safely on the main thread
 */
Err_t WindowQueuePushMode(WinMode_t mode) {
    WinUpdateAction_t action;
    Err_t             err;

    #if (1U == PARAM_CHECK_EN)
    /* Validate mode enum */
    if ((WIN_MODE_FLOATING != mode) && (WIN_MODE_MAXIMIZED != mode)) {
        APP_ERR("Invalid window mode in WindowQueuePushMode!");
        return APP_ERROR_INVALID_PARAM;
    }
    #endif /* (1U == PARAM_CHECK_EN) */

    action.Type.Raw          = 0ULL;
    action.Type.ModeCfg.Cmd  = WIN_ACTION_SET_MODE;
    action.Type.ModeCfg.Mode = mode;
    action.p_Comp            = NULL;

    /* Lock queue mutex */
    if (NULL != g_MainWindowUpdateQueue.p_Mutex) {
        (void)SDL_LockMutex((SDL_mutex *)g_MainWindowUpdateQueue.p_Mutex);
    }

    /* Push mode action into circular queue (overwrites oldest if full) */
    err = CircleQueuePush(&g_MainWindowUpdateQueue.Queue, &action);

    /* Unlock queue mutex */
    if (NULL != g_MainWindowUpdateQueue.p_Mutex) {
        (void)SDL_UnlockMutex((SDL_mutex *)g_MainWindowUpdateQueue.p_Mutex);
    }

    /* Return error if push failed */
    if (APP_SUCCESS != err) {
        return err;
    }

    /* Wake up sleeping main thread */
    (void)WindowWakeupMainThread();

    /* Return success after queuing mode action */
    return APP_SUCCESS;
}

/* Push a window resize action from a sub-thread into g_MainWindowUpdateQueue and wake main thread
 * @param width[in] Target window width
 * @param height[in] Target window height
 * @return APP_SUCCESS on success, or error status
 * @note Uses the Resize struct member of WinActionType_un
 */
Err_t WindowQueuePushResize(uint32_t width, uint32_t height) {
    WinUpdateAction_t action;
    Err_t             err;

    #if (1U == PARAM_CHECK_EN)
    /* Validate non-zero dimensions */
    if ((0U == width) || (0U == height)) {
        APP_ERR("Invalid resize dimensions!");
        return APP_ERROR_INVALID_PARAM;
    }
    #endif /* (1U == PARAM_CHECK_EN) */

    action.Type.Raw           = 0ULL;
    action.Type.Resize.Cmd    = WIN_ACTION_RESIZE_WINDOW;
    action.Type.Resize.Width  = width;
    action.Type.Resize.Height = height;
    action.p_Comp             = NULL;

    /* Lock queue mutex */
    if (NULL != g_MainWindowUpdateQueue.p_Mutex) {
        (void)SDL_LockMutex((SDL_mutex *)g_MainWindowUpdateQueue.p_Mutex);
    }

    /* Push resize action into circular queue (overwrites oldest if full) */
    err = CircleQueuePush(&g_MainWindowUpdateQueue.Queue, &action);

    /* Unlock queue mutex */
    if (NULL != g_MainWindowUpdateQueue.p_Mutex) {
        (void)SDL_UnlockMutex((SDL_mutex *)g_MainWindowUpdateQueue.p_Mutex);
    }

    /* Return error if push failed */
    if (APP_SUCCESS != err) {
        return err;
    }

    /* Wake up sleeping main thread */
    (void)WindowWakeupMainThread();

    /* Return success after queuing resize action */
    return APP_SUCCESS;
}

/* Drain and execute all pending actions in g_MainWindowUpdateQueue on the main thread
 * @return APP_SUCCESS on success
 * @note Coalesces multiple redraw requests into a single frame presentation
 */
Err_t WindowQueueProcessAll(void) {
    uint8_t need_full_redraw = 0U;

    /* Process queued actions until g_MainWindowUpdateQueue is empty */
    while (1) {
        WinUpdateAction_t action;
        Err_t             pop_err;

        /* Lock queue mutex to pop next action */
        if (NULL != g_MainWindowUpdateQueue.p_Mutex) {
            (void)SDL_LockMutex((SDL_mutex *)g_MainWindowUpdateQueue.p_Mutex);
        }

        /* Pop oldest action from circular queue */
        pop_err = CircleQueuePop(&g_MainWindowUpdateQueue.Queue, &action);

        /* Unlock queue mutex */
        if (NULL != g_MainWindowUpdateQueue.p_Mutex) {
            (void)SDL_UnlockMutex((SDL_mutex *)g_MainWindowUpdateQueue.p_Mutex);
        }

        /* Exit loop when no more actions remain */
        if (APP_SUCCESS != pop_err) {
            /* Break out of queue drain loop */
            break;
        }

        /* Execute queued action based on command type */
        switch (action.Type.Cmd) {
            case WIN_ACTION_UPDATE_COMP:
            case WIN_ACTION_UPDATE_ALL:
                need_full_redraw = 1U;
                /* Break after marking frame dirty */
                break;
            case WIN_ACTION_SHOW_WINDOW:
                /* Show the main window */
                (void)WindowShow();
                need_full_redraw = 1U;
                /* Break after showing window */
                break;
            case WIN_ACTION_HIDE_WINDOW:
                /* Hide the main window */
                (void)WindowHide();
                /* Break after hiding window */
                break;
            case WIN_ACTION_TOGGLE_WINDOW:
                /* Toggle main window visibility */
                (void)WindowToggle();
                need_full_redraw = g_MainWindow.IsVisible;
                /* Break after toggling window */
                break;
            case WIN_ACTION_SET_MODE:
                /* Set window mode (floating/maximize) */
                (void)WindowSetMode(action.Type.ModeCfg.Mode);
                /* Break after setting window mode */
                break;
            case WIN_ACTION_RESIZE_WINDOW:
                /* Resize the main window */
                (void)WindowResize(action.Type.Resize.Width, action.Type.Resize.Height);
                /* Break after resizing window */
                break;
            default:
                /* Ignore unknown action command */
                break;
        }
    }

    /* Perform a single coalesced redraw if any update action was processed */
    if (0U != need_full_redraw) {
        (void)WindowUpdateAll();
    }

    /* Return success after processing all queued actions */
    return APP_SUCCESS;
}

/* reserved ***************************************************************************************
 **************************************************************************************************/
