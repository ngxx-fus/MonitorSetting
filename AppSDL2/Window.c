/* file-desc **************************************************************************************
 * @filename    Window.c
 * @brief       Main window lifecycle, non-blocking UI lock, and thread-wakeup queue implementation.
 * @note        Uses SDL_mutex, SDL_cond, and SDL_PushEvent to wake up the sleeping main thread.
 **************************************************************************************************/

/* headers ****************************************************************************************
 **************************************************************************************************/

#include "./__common__.h"
#include "./Window.h"
#include "./Draw.h"

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
    .LayerCount    = 0U,
    .p_SdlWindow   = NULL,
    .p_SdlRenderer = NULL,
    .p_UiMutex     = NULL
};

MainWindowUpdateQueue_t g_MainWindowUpdateQueue = {
    .Head            = 0U,
    .Tail            = 0U,
    .Count           = 0U,
    .p_Mutex         = NULL,
    .p_Cond          = NULL,
    .WakeupEventType = 0U
};

/* private variables ******************************************************************************
 **************************************************************************************************/

/* exported functions *****************************************************************************
 **************************************************************************************************/

/* public functions *******************************************************************************
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

/* private functions ******************************************************************************
 **************************************************************************************************/

/* function implementations ***********************************************************************
 **************************************************************************************************/

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

    /* Create resizable SDL2 main window */
    p_win = SDL_CreateWindow(title,
                             SDL_WINDOWPOS_CENTERED,
                             SDL_WINDOWPOS_CENTERED,
                             (int)width,
                             (int)height,
                             SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    if (NULL == p_win) {
        APP_ERR("SDL_CreateWindow failed: %s", SDL_GetError());
        SDL_Quit();
        return APP_ERROR_INIT_FAILED;
    }

    /* Create hardware-accelerated or software fallback SDL2 renderer */
    p_ren = SDL_CreateRenderer(p_win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (NULL == p_ren) {
        /* Fall back to software renderer if hardware acceleration is unavailable */
        p_ren = SDL_CreateRenderer(p_win, -1, SDL_RENDERER_SOFTWARE);
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
    g_MainWindow.LayerCount    = APP_SDL2_MAX_LAYERS;
    g_MainWindow.p_SdlWindow   = (void *)p_win;
    g_MainWindow.p_SdlRenderer = (void *)p_ren;
    g_MainWindow.p_UiMutex     = (void *)SDL_CreateMutex();

    /* Reset all layers in g_MainWindow */
    for (idx = 0U; idx < APP_SDL2_MAX_LAYERS; idx++) {
        g_MainWindow.Layers[idx].LayerId   = idx;
        g_MainWindow.Layers[idx].IsVisible = 1U;
        g_MainWindow.Layers[idx].CompCount = 0U;
        g_MainWindow.Layers[idx].p_Comps   = NULL;
    }

    g_MainWindowUpdateQueue.Head            = 0U;
    g_MainWindowUpdateQueue.Tail            = 0U;
    g_MainWindowUpdateQueue.Count           = 0U;
    g_MainWindowUpdateQueue.p_Mutex         = (void *)SDL_CreateMutex();
    g_MainWindowUpdateQueue.p_Cond          = (void *)SDL_CreateCond();
    g_MainWindowUpdateQueue.WakeupEventType = SDL_RegisterEvents(1);

    /* Return success after initializing main window */
    return APP_SUCCESS;
}

/* Destroy the SDL2 main window, synchronization primitives, and renderer resources
 * @return APP_SUCCESS on success
 * @note Safe to call when shutting down the application
 */
Err_t WindowDeinit(void) {
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

/* Show the main window on screen
 * @return APP_SUCCESS on success
 * @note Marks g_MainWindow.IsVisible as 1U and calls SDL_ShowWindow
 */
Err_t WindowShow(void) {
    g_MainWindow.IsVisible = 1U;
    /* Show underlying SDL window if valid */
    if (NULL != g_MainWindow.p_SdlWindow) {
        SDL_ShowWindow((SDL_Window *)g_MainWindow.p_SdlWindow);
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
        SDL_SetWindowSize((SDL_Window *)g_MainWindow.p_SdlWindow, (int)width, (int)height);
    }

    /* Re-render all window layers after resize */
    return WindowUpdateAll();
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
 * @return APP_SUCCESS on success, or APP_ERROR_QUEUE_FULL if queue is full
 * @note Protected by g_MainWindowUpdateQueue.p_Mutex and wakes sleeping main thread
 */
Err_t WindowQueuePush(WinActionCmd_t cmd, Comp_t *p_comp) {
    uint32_t tail;

    #if (1U == PARAM_CHECK_EN)
    /* Validate command type */
    if (WIN_ACTION_NONE == cmd) {
        APP_ERR("Invalid window action command!");
        return APP_ERROR_INVALID_PARAM;
    }
    #endif /* (1U == PARAM_CHECK_EN) */

    /* Lock queue mutex if available */
    if (NULL != g_MainWindowUpdateQueue.p_Mutex) {
        (void)SDL_LockMutex((SDL_mutex *)g_MainWindowUpdateQueue.p_Mutex);
    }

    /* Check if g_MainWindowUpdateQueue has available capacity */
    if (g_MainWindowUpdateQueue.Count >= APP_SDL2_MAX_QUEUE_ACTIONS) {
        /* Unlock mutex before returning queue full error */
        if (NULL != g_MainWindowUpdateQueue.p_Mutex) {
            (void)SDL_UnlockMutex((SDL_mutex *)g_MainWindowUpdateQueue.p_Mutex);
        }
        APP_ERR("g_MainWindowUpdateQueue is full!");
        return APP_ERROR_QUEUE_FULL;
    }

    tail = g_MainWindowUpdateQueue.Tail;
    g_MainWindowUpdateQueue.Actions[tail].Type.Cmd = cmd;
    g_MainWindowUpdateQueue.Actions[tail].p_Comp   = p_comp;
    g_MainWindowUpdateQueue.Tail = (tail + 1U) % APP_SDL2_MAX_QUEUE_ACTIONS;
    g_MainWindowUpdateQueue.Count++;

    /* Unlock queue mutex */
    if (NULL != g_MainWindowUpdateQueue.p_Mutex) {
        (void)SDL_UnlockMutex((SDL_mutex *)g_MainWindowUpdateQueue.p_Mutex);
    }

    /* Wake up sleeping main thread so it processes the queued UI action immediately */
    (void)WindowWakeupMainThread();

    /* Return success after queuing action */
    return APP_SUCCESS;
}

/* Push a window resize action from a sub-thread into g_MainWindowUpdateQueue and wake main thread
 * @param width[in] Target window width
 * @param height[in] Target window height
 * @return APP_SUCCESS on success, or error status
 * @note Uses the Resize struct member of WinActionType_un
 */
Err_t WindowQueuePushResize(uint32_t width, uint32_t height) {
    uint32_t tail;

    #if (1U == PARAM_CHECK_EN)
    /* Validate non-zero dimensions */
    if ((0U == width) || (0U == height)) {
        APP_ERR("Invalid resize dimensions!");
        return APP_ERROR_INVALID_PARAM;
    }
    #endif /* (1U == PARAM_CHECK_EN) */

    /* Lock queue mutex */
    if (NULL != g_MainWindowUpdateQueue.p_Mutex) {
        (void)SDL_LockMutex((SDL_mutex *)g_MainWindowUpdateQueue.p_Mutex);
    }

    /* Check queue capacity */
    if (g_MainWindowUpdateQueue.Count >= APP_SDL2_MAX_QUEUE_ACTIONS) {
        if (NULL != g_MainWindowUpdateQueue.p_Mutex) {
            (void)SDL_UnlockMutex((SDL_mutex *)g_MainWindowUpdateQueue.p_Mutex);
        }
        APP_ERR("g_MainWindowUpdateQueue is full!");
        return APP_ERROR_QUEUE_FULL;
    }

    tail = g_MainWindowUpdateQueue.Tail;
    g_MainWindowUpdateQueue.Actions[tail].Type.Resize.Cmd    = WIN_ACTION_RESIZE_WINDOW;
    g_MainWindowUpdateQueue.Actions[tail].Type.Resize.Width  = width;
    g_MainWindowUpdateQueue.Actions[tail].Type.Resize.Height = height;
    g_MainWindowUpdateQueue.Actions[tail].p_Comp             = NULL;
    g_MainWindowUpdateQueue.Tail = (tail + 1U) % APP_SDL2_MAX_QUEUE_ACTIONS;
    g_MainWindowUpdateQueue.Count++;

    /* Unlock queue mutex */
    if (NULL != g_MainWindowUpdateQueue.p_Mutex) {
        (void)SDL_UnlockMutex((SDL_mutex *)g_MainWindowUpdateQueue.p_Mutex);
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
        uint8_t           has_action = 0U;

        /* Lock queue mutex to pop next action */
        if (NULL != g_MainWindowUpdateQueue.p_Mutex) {
            (void)SDL_LockMutex((SDL_mutex *)g_MainWindowUpdateQueue.p_Mutex);
        }

        /* Pop one action if queue is non-empty */
        if (g_MainWindowUpdateQueue.Count > 0U) {
            action = g_MainWindowUpdateQueue.Actions[g_MainWindowUpdateQueue.Head];
            g_MainWindowUpdateQueue.Head = (g_MainWindowUpdateQueue.Head + 1U) % APP_SDL2_MAX_QUEUE_ACTIONS;
            g_MainWindowUpdateQueue.Count--;
            has_action = 1U;
        }

        /* Unlock queue mutex */
        if (NULL != g_MainWindowUpdateQueue.p_Mutex) {
            (void)SDL_UnlockMutex((SDL_mutex *)g_MainWindowUpdateQueue.p_Mutex);
        }

        /* Exit loop when no more actions remain */
        if (0U == has_action) {
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
