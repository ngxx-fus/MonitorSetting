/* file-desc **************************************************************************************
 * @filename    main.c
 * @brief       Demo application showcasing all AppCommon and AppSDL2 MDF features.
 * @note        Demonstrates:
 *              - Single-instance lock & toggle (running ./main again toggles the 1st window)
 *              - 3-thread sleeping/waking architecture (Main, EventListener, MiscBackground)
 *              - Multi-layer UI with Scrollable Layer/Table, Draggable Cards, and Clickable Buttons
 *              - Quick Yes/No Dialog (WindowShowYesNoDialog) & Maximize/Floating Window Modes
 *              - Custom User Signal handling (EVENT_TYPE_USER_SIGNAL)
 **************************************************************************************************/

/* headers ****************************************************************************************
 **************************************************************************************************/

#include "AppCommon/All.h"
#include "AppSDL2/All.h"
#include <SDL2/SDL.h>

/* macros *****************************************************************************************
 **************************************************************************************************/

#define DEMO_WIN_WIDTH                  (800U)
#define DEMO_WIN_HEIGHT                 (520U)

/* definitions ************************************************************************************
 **************************************************************************************************/

/* Context shared with the miscellaneous background worker thread */
struct MiscThreadCtx_st {
    uint8_t    IsRunning;
    SDL_mutex *p_Mutex;
    SDL_cond  *p_Cond;
    Comp_t    *p_StatusCircle;
    Comp_t    *p_StatusText;
    uint32_t   TickCount;
};
typedef struct MiscThreadCtx_st MiscThreadCtx_t;

/* exported variables *****************************************************************************
 **************************************************************************************************/

/* public variables *******************************************************************************
 **************************************************************************************************/

/* private variables ******************************************************************************
 **************************************************************************************************/

static MiscThreadCtx_t g_MiscCtx = {
    .IsRunning      = 0U,
    .p_Mutex        = NULL,
    .p_Cond         = NULL,
    .p_StatusCircle = NULL,
    .p_StatusText   = NULL,
    .TickCount      = 0U
};

static char g_StatusMsgBuf[80] = "Background Tick: 0 | Drag cards, click buttons, or scroll";

/* exported functions *****************************************************************************
 **************************************************************************************************/

/* public functions *******************************************************************************
 **************************************************************************************************/

int main(int argc, char *argv[]);

/* private functions ******************************************************************************
 **************************************************************************************************/

static void OnCardDragStart(Comp_t *p_comp, const EventData_t *p_event);
static void OnCardDrop(Comp_t *p_comp, const EventData_t *p_event);
static void OnMonitorEvent(Comp_t *p_comp, const EventData_t *p_event);
static void OnUserSignalEvent(Comp_t *p_comp, const EventData_t *p_event);
static void OnBtnModeClick(Comp_t *p_comp, const EventData_t *p_event);
static void OnBtnYesNoClick(Comp_t *p_comp, const EventData_t *p_event);
static int  MiscBackgroundThreadEntry(void *p_arg);

/* function implementations ***********************************************************************
 **************************************************************************************************/

/* Callback invoked when user starts dragging a draggable card component
 * @param p_comp[in/out] Component being dragged
 * @param p_event[in] Drag start event details
 * @note Highlights the border color in yellow while dragging (protected by WindowLockUI)
 */
static void OnCardDragStart(Comp_t *p_comp, const EventData_t *p_event) {
    Color_t active_border = { .w = 0xFFFFFF00U };

    /* Check if component and event pointers are valid */
    if ((NULL == p_comp) || (NULL == p_event)) {
        /* Abort callback on NULL pointer */
        return;
    }

    /* Lock UI briefly to update border style */
    (void)WindowLockUI();
    (void)CompSetBorder(p_comp, 2U, active_border);
    (void)WindowUnlockUI();

    APP_INFO("Drag started on '%s' at (%d, %d)",
             p_comp->Label, p_event->MouseX, p_event->MouseY);

    /* Return after handling drag start */
    return;
}

/* Callback invoked when user drops a dragged card component
 * @param p_comp[in/out] Component that was dropped
 * @param p_event[in] Drop event details
 * @note Restores border color to white after drop (protected by WindowLockUI)
 */
static void OnCardDrop(Comp_t *p_comp, const EventData_t *p_event) {
    Color_t normal_border = { .w = 0xFFFFFFFFU };

    /* Check if component and event pointers are valid */
    if ((NULL == p_comp) || (NULL == p_event)) {
        /* Abort callback on NULL pointer */
        return;
    }

    /* Lock UI briefly to restore default white border */
    (void)WindowLockUI();
    (void)CompSetBorder(p_comp, 1U, normal_border);
    (void)WindowUnlockUI();

    APP_INFO("Dropped '%s' at new offset (%d, %d)",
             p_comp->Label,
             p_comp->Type.Rect.Dim.OffsetX,
             p_comp->Type.Rect.Dim.OffsetY);

    /* Return after handling drop event */
    return;
}

/* Callback invoked when a background monitor event is dispatched
 * @param p_comp[in/out] Component receiving the monitor event
 * @param p_event[in] Event data containing SysCode tick
 * @note Logs periodic monitor status check
 */
static void OnMonitorEvent(Comp_t *p_comp, const EventData_t *p_event) {
    /* Validate pointers before logging */
    if ((NULL == p_comp) || (NULL == p_event)) {
        /* Return early on NULL pointer */
        return;
    }

    APP_INFO("Monitor listener on '%s' received SysCode=%u",
             p_comp->Label, p_event->SysCode);

    /* Return after logging monitor event */
    return;
}

/* Callback invoked when a custom user signal (EVENT_TYPE_USER_SIGNAL) is received
 * @param p_comp[in/out] Component receiving the user signal
 * @param p_event[in] Event data containing signal code in SysCode
 * @note Demonstrates custom user signal handling
 */
static void OnUserSignalEvent(Comp_t *p_comp, const EventData_t *p_event) {
    /* Validate pointers before logging */
    if ((NULL == p_comp) || (NULL == p_event)) {
        /* Return early on NULL pointer */
        return;
    }

    APP_INFO("Custom user signal received on '%s' (sig_code=%u)",
             p_comp->Label, p_event->SysCode);

    /* Return after handling user signal */
    return;
}

/* Click callback for the Toggle Maximize/Floating button component
 * @param p_comp[in/out] Button component that was clicked
 * @param p_event[in] Mouse click event data
 * @note Toggles g_MainWindow between WIN_MODE_FLOATING and WIN_MODE_MAXIMIZED
 */
static void OnBtnModeClick(Comp_t *p_comp, const EventData_t *p_event) {
    WinMode_t next_mode;

    (void)p_comp;
    (void)p_event;

    next_mode = (WIN_MODE_FLOATING == g_MainWindow.Mode) ? WIN_MODE_MAXIMIZED : WIN_MODE_FLOATING;
    /* Apply new window mode */
    (void)WindowSetMode(next_mode);
    /* Also emit a custom user signal to demonstrate EVENT_TYPE_USER_SIGNAL */
    (void)EventSendUserSignal(100U + (uint32_t)next_mode, NULL);

    /* Return after toggling window mode */
    return;
}

/* Click callback for the Quick Yes/No Dialog button component
 * @param p_comp[in/out] Button component that was clicked
 * @param p_event[in] Mouse click event data
 * @note Opens WindowShowYesNoDialog and logs user selection
 */
static void OnBtnYesNoClick(Comp_t *p_comp, const EventData_t *p_event) {
    uint8_t is_yes = 0U;

    (void)p_comp;
    (void)p_event;

    /* Show quick Yes/No confirmation dialog */
    if (APP_SUCCESS == WindowShowYesNoDialog("Apply Monitor Layout?",
                                             "Do you want to apply the selected monitor configuration?",
                                             &is_yes)) {
        APP_INFO("Quick Yes/No Dialog result: %s", (0U != is_yes) ? "YES" : "NO");
    }

    /* Return after dialog closes */
    return;
}

/* Background housekeeping thread that sleeps on condition variable and pushes UI update actions
 * @param p_arg[in] Pointer to MiscThreadCtx_t
 * @return Thread exit status (0)
 * @note Uses SDL_CondWaitTimeout so it can be woken up immediately on shutdown
 */
static int MiscBackgroundThreadEntry(void *p_arg) {
    MiscThreadCtx_t *p_ctx = (MiscThreadCtx_t *)p_arg;

    /* Validate thread context pointer */
    if (NULL == p_ctx) {
        /* Exit thread if context is NULL */
        return -1;
    }

    /* Execute periodic background tasks while application is running */
    while (0U != p_ctx->IsRunning) {
        /* Sleep for up to 500ms or wake immediately if signaled via p_Cond */
        if ((NULL != p_ctx->p_Mutex) && (NULL != p_ctx->p_Cond)) {
            (void)SDL_LockMutex(p_ctx->p_Mutex);
            if (0U != p_ctx->IsRunning) {
                (void)SDL_CondWaitTimeout(p_ctx->p_Cond, p_ctx->p_Mutex, 500U);
            }
            (void)SDL_UnlockMutex(p_ctx->p_Mutex);
        } else {
            SDL_Delay(500U);
        }

        /* Re-check running flag after waking up */
        if (0U == p_ctx->IsRunning) {
            /* Break immediately if shutdown was requested */
            break;
        }

        p_ctx->TickCount++;

        /* Lock UI mutex briefly while updating status circle and text buffer */
        (void)WindowLockUI();
        if (NULL != p_ctx->p_StatusCircle) {
            if (0U == (p_ctx->TickCount & 1U)) {
                p_ctx->p_StatusCircle->Type.Circle.Color.w = 0xFF00E676U;
            } else {
                p_ctx->p_StatusCircle->Type.Circle.Color.w = 0xFF00E5FFU;
            }
        }
        if (NULL != p_ctx->p_StatusText) {
            (void)snprintf(g_StatusMsgBuf, sizeof(g_StatusMsgBuf),
                           "Background Tick: %u | Drag cards, click buttons, or scroll",
                           p_ctx->TickCount);
        }
        (void)WindowUnlockUI();

        /* Push update action to g_MainWindowUpdateQueue (wakes sleeping main thread) */
        (void)WindowQueuePush(WIN_ACTION_UPDATE_COMP, p_ctx->p_StatusCircle);

        /* Every 6 ticks (~3 seconds), push a synthetic EVENT_TYPE_MONITOR (wakes EventListenerThread) */
        if (0U == (p_ctx->TickCount % 6U)) {
            EventData_t mon_ev = {
                .Type        = EVENT_TYPE_MONITOR,
                .MouseX      = 0,
                .MouseY      = 0,
                .WheelDeltaX = 0,
                .WheelDeltaY = 0,
                .KeyCode     = 0U,
                .SysCode     = p_ctx->TickCount,
                .p_UserData  = NULL
            };
            /* Push monitor event and wake sleeping EventListenerThread */
            (void)EventPushToListenerQueue(&mon_ev);
        }
    }

    /* Return 0 when background thread exits */
    return 0;
}

/* Application entry point setting up single-instance guard, layers, components, and threads
 * @param argc[in] Argument count
 * @param argv[in] Argument vector
 * @return 0 on clean exit, non-zero on initialization failure
 * @note Press ESC or close the window to exit cleanly
 */
int main(int argc, char *argv[]) {
    Err_t          err;
    uint8_t        is_primary = 1U;
    uint8_t        quit_flag  = 0U;
    SDL_Thread    *p_misc_thread;
    int            misc_thread_status = 0;

    /* Layer 0 components: Background panel, divider line, status circle, status text, 2 buttons */
    Comp_t         layer0_comps[6];
    /* Layer 1 components: Two draggable monitor selection cards */
    Comp_t         layer1_comps[2];
    /* Layer 2 components: Scrollable layer containing a table component */
    Comp_t         layer2_comps[1];

    /* Child text extensions (p_ExtComps) for the two draggable monitor cards */
    CompTextCfg_t  card0_title_cfg;
    ExtComp_t      card0_ext_item;
    ExtCompList_t  card0_ext_list;

    CompTextCfg_t  card1_title_cfg;
    ExtComp_t      card1_ext_item;
    ExtCompList_t  card1_ext_list;

    /* Event listener slots for cards and buttons */
    CompEvent_t    card0_events[4];
    CompEvent_t    card1_events[2];
    CompEvent_t    btn_mode_event;
    CompEvent_t    btn_yesno_event;

    (void)argc;
    (void)argv;

    /* 0. Enforce single instance: if already running, toggle the 1st instance window and exit */
    err = WindowEnsureSingleInstance(&is_primary);
    if ((APP_SUCCESS != err) || (0U == is_primary)) {
        /* Exit secondary instance after signaling primary instance */
        return 0;
    }

    /* 1. Initialize main SDL2 window, renderer, UI mutex, and wakeup event */
    err = WindowInit("MonitorSelection - AppSDL2 Demo (ESC to Quit)", DEMO_WIN_WIDTH, DEMO_WIN_HEIGHT);
    if (APP_SUCCESS != err) {
        APP_ERR("WindowInit failed with status: %s", Err2Str(err));
        return -1;
    }

    /* 2. Configure Layer 0 (Base UI elements + Action Buttons) */
    {
        CompDim_t panel_dim   = { .W = 740U, .H = 460U, .OffsetX = 30,  .OffsetY = 30, .Rotation = 0U };
        CompDim_t circle_dim  = { .W = 24U,  .H = 24U,  .OffsetX = 50,  .OffsetY = 48, .Rotation = 0U };
        CompDim_t btn_mod_dim = { .W = 115U, .H = 28U,  .OffsetX = 505, .OffsetY = 46, .Rotation = 0U };
        CompDim_t btn_yn_dim  = { .W = 115U, .H = 28U,  .OffsetX = 630, .OffsetY = 46, .Rotation = 0U };
        Color_t   panel_col   = { .w = 0xFF232936U };
        Color_t   border_col  = { .w = 0xFF4A5568U };
        Color_t   line_col    = { .w = 0xFF00B4D8U };
        Color_t   circle_col  = { .w = 0xFF00E676U };
        Color_t   text_col    = { .w = 0xFFE2E8F0U };
        Color_t   btn_col     = { .w = 0xFF2D3748U };

        /* Comp 0: Main container rounded rectangle */
        (void)CompInitRect(&layer0_comps[0], "MainPanel", panel_dim, panel_col, 10U, 1U);
        (void)CompSetBorder(&layer0_comps[0], 1U, border_col);

        /* Comp 1: Header separator line */
        (void)CompInitLine(&layer0_comps[1], "HeaderLine", 50, 82, 750, 82, line_col, 2U);

        /* Comp 2: Background thread pulsing status circle */
        (void)CompInitCircle(&layer0_comps[2], "StatusCircle", circle_dim, circle_col, 10U, 1U);
        (void)CompSetBorder(&layer0_comps[2], 1U, text_col);

        /* Comp 3: Status header text */
        (void)CompInitText(&layer0_comps[3], "HeaderText", 82, 56, g_StatusMsgBuf, text_col, 14U);

        /* Comp 4: Button to toggle Maximize / Floating mode */
        (void)CompInitButton(&layer0_comps[4], "Max/Float", btn_mod_dim, btn_col, &btn_mode_event, OnBtnModeClick);

        /* Comp 5: Button to open Quick Yes/No Dialog */
        (void)CompInitButton(&layer0_comps[5], "Apply (Y/N)", btn_yn_dim, btn_col, &btn_yesno_event, OnBtnYesNoClick);

        g_MainWindow.Layers[0].IsVisible = 1U;
        g_MainWindow.Layers[0].CompCount = 6U;
        g_MainWindow.Layers[0].p_Comps   = layer0_comps;
    }

    /* 3. Configure Layer 1 (Draggable Monitor Cards with p_ExtComps and Event Listeners) */
    {
        CompDim_t card0_dim = { .W = 240U, .H = 130U, .OffsetX = 80,  .OffsetY = 110, .Rotation = 0U };
        CompDim_t card1_dim = { .W = 240U, .H = 130U, .OffsetX = 380, .OffsetY = 110, .Rotation = 0U };
        Color_t   card0_col = { .w = 0xDD1E4E8CU };
        Color_t   card1_col = { .w = 0xDD2B6CB0U };
        Color_t   white_col = { .w = 0xFFFFFFFFU };

        /* Draggable Card 0: Monitor 1 */
        (void)CompInitRect(&layer1_comps[0], "MonitorCard_1", card0_dim, card0_col, 8U, 1U);
        (void)CompSetBorder(&layer1_comps[0], 1U, white_col);
        layer1_comps[0].Class       = COMP_CLASS_BUTTON;
        layer1_comps[0].IsDraggable = 1U;

        /* Attach child text label to Card 0 via p_ExtComps */
        card0_title_cfg.Color        = white_col;
        card0_title_cfg.Dim.OffsetX  = card0_dim.OffsetX + 24;
        card0_title_cfg.Dim.OffsetY  = card0_dim.OffsetY + 58;
        card0_title_cfg.Dim.W        = 200U;
        card0_title_cfg.Dim.H        = 16U;
        card0_title_cfg.Dim.Rotation = 0U;
        card0_title_cfg.TextStr      = "[Monitor 1: HDMI-1 1920x1080]";
        card0_title_cfg.FontSize     = 14U;

        card0_ext_item.CastType       = COMP_CAST_TEXT;
        card0_ext_item.CompPtr.p_Text = &card0_title_cfg;
        card0_ext_list.Count          = 1U;
        card0_ext_list.p_ExtComps     = &card0_ext_item;
        layer1_comps[0].p_ExtComps    = &card0_ext_list;

        /* Attach listeners to Card 0 (including custom user signal listener) */
        card0_events[0].Listener.Type       = EVENT_TYPE_DRAG_START;
        card0_events[0].Listener.p_CallBack = OnCardDragStart;
        card0_events[1].Listener.Type       = EVENT_TYPE_DROP;
        card0_events[1].Listener.p_CallBack = OnCardDrop;
        card0_events[2].Listener.Type       = EVENT_TYPE_MONITOR;
        card0_events[2].Listener.p_CallBack = OnMonitorEvent;
        card0_events[3].Listener.Type       = EVENT_TYPE_USER_SIGNAL;
        card0_events[3].Listener.p_CallBack = OnUserSignalEvent;
        layer1_comps[0].Listeners.Count     = 4U;
        layer1_comps[0].Listeners.p_Events  = card0_events;

        /* Draggable Card 1: Monitor 2 */
        (void)CompInitRect(&layer1_comps[1], "MonitorCard_2", card1_dim, card1_col, 8U, 1U);
        (void)CompSetBorder(&layer1_comps[1], 1U, white_col);
        layer1_comps[1].Class       = COMP_CLASS_BUTTON;
        layer1_comps[1].IsDraggable = 1U;

        /* Attach child text label to Card 1 via p_ExtComps */
        card1_title_cfg.Color        = white_col;
        card1_title_cfg.Dim.OffsetX  = card1_dim.OffsetX + 24;
        card1_title_cfg.Dim.OffsetY  = card1_dim.OffsetY + 58;
        card1_title_cfg.Dim.W        = 200U;
        card1_title_cfg.Dim.H        = 16U;
        card1_title_cfg.Dim.Rotation = 0U;
        card1_title_cfg.TextStr      = "[Monitor 2: DP-1 2560x1440]";
        card1_title_cfg.FontSize     = 14U;

        card1_ext_item.CastType       = COMP_CAST_TEXT;
        card1_ext_item.CompPtr.p_Text = &card1_title_cfg;
        card1_ext_list.Count          = 1U;
        card1_ext_list.p_ExtComps     = &card1_ext_item;
        layer1_comps[1].p_ExtComps    = &card1_ext_list;

        /* Attach listeners to Card 1 */
        card1_events[0].Listener.Type       = EVENT_TYPE_DRAG_START;
        card1_events[0].Listener.p_CallBack = OnCardDragStart;
        card1_events[1].Listener.Type       = EVENT_TYPE_DROP;
        card1_events[1].Listener.p_CallBack = OnCardDrop;
        layer1_comps[1].Listeners.Count     = 2U;
        layer1_comps[1].Listeners.p_Events  = card1_events;

        g_MainWindow.Layers[1].IsVisible = 1U;
        g_MainWindow.Layers[1].CompCount = 2U;
        g_MainWindow.Layers[1].p_Comps   = layer1_comps;
    }

    /* 4. Configure Layer 2 (Scrollable Layer with attached Table component) */
    {
        /* Layer RectClip is positioned relative to (0, 0) of MainWindow */
        CompDim_t rect_clip = { .W = 700U, .H = 180U, .OffsetX = 50, .OffsetY = 280, .Rotation = 0U };
        /* Component inside Layer 2 is positioned relative to (0, 0) of Layer 2 */
        CompDim_t table_dim = { .W = 700U, .H = 300U, .OffsetX = 0,  .OffsetY = 0,   .Rotation = 0U };
        Color_t   table_col = { .w = 0xFF1A202CU };
        Color_t   line_col  = { .w = 0xFF00B4D8U };

        (void)CompInitTable(&layer2_comps[0], "MonitorTable", table_dim, table_col, 10U, 5U, 1U);
        (void)CompSetBorder(&layer2_comps[0], 1U, line_col);

        g_MainWindow.Layers[2].IsVisible = 1U;
        g_MainWindow.Layers[2].CompCount = 1U;
        g_MainWindow.Layers[2].p_Comps   = layer2_comps;
        (void)LayerSetScrollable(&g_MainWindow.Layers[2], rect_clip, 1U);
    }

    /* 5. Perform initial full window render */
    (void)WindowUpdateAll();

    /* 6. Start Sub-Thread 1: Dedicated Event Listener Thread (sleeps until events arrive) */
    err = EventListenerThreadStart();
    if (APP_SUCCESS != err) {
        APP_ERR("Failed to start EventListenerThread: %s", Err2Str(err));
        (void)WindowDeinit();
        return -1;
    }

    /* 7. Start Sub-Thread 2: Miscellaneous Background Thread with sleep/wakeup condition */
    g_MiscCtx.IsRunning      = 1U;
    g_MiscCtx.p_Mutex        = SDL_CreateMutex();
    g_MiscCtx.p_Cond         = SDL_CreateCond();
    g_MiscCtx.p_StatusCircle = &layer0_comps[2];
    g_MiscCtx.p_StatusText   = &layer0_comps[3];
    g_MiscCtx.TickCount      = 0U;
    p_misc_thread = SDL_CreateThread(MiscBackgroundThreadEntry, "MiscBackgroundThread", &g_MiscCtx);

    APP_INFO("Main loop started. Running ./main again in another terminal will toggle this window.");

    /* 8. Main Thread Loop: Sleep until woken by an OS event or a sub-thread queue wakeup */
    while (0U == quit_flag) {
        /* Sleep until an OS SDL2 event or WindowWakeupMainThread signal arrives */
        (void)EventWaitOSOnMainThread(&quit_flag, 0U);

        /* Drain and execute any pending UI update actions from sub-threads */
        (void)WindowQueueProcessAll();
    }

    /* 9. Wake up and join MiscBackgroundThread cleanly */
    if (NULL != g_MiscCtx.p_Mutex) {
        (void)SDL_LockMutex(g_MiscCtx.p_Mutex);
        g_MiscCtx.IsRunning = 0U;
        if (NULL != g_MiscCtx.p_Cond) {
            (void)SDL_CondBroadcast(g_MiscCtx.p_Cond);
        }
        (void)SDL_UnlockMutex(g_MiscCtx.p_Mutex);
    } else {
        g_MiscCtx.IsRunning = 0U;
    }

    /* Wait for MiscBackgroundThread to finish */
    if (NULL != p_misc_thread) {
        SDL_WaitThread(p_misc_thread, &misc_thread_status);
    }
    if (NULL != g_MiscCtx.p_Cond) {
        SDL_DestroyCond(g_MiscCtx.p_Cond);
        g_MiscCtx.p_Cond = NULL;
    }
    if (NULL != g_MiscCtx.p_Mutex) {
        SDL_DestroyMutex(g_MiscCtx.p_Mutex);
        g_MiscCtx.p_Mutex = NULL;
    }

    /* Stop and wake up EventListenerThread */
    (void)EventListenerThreadStop();

    /* Destroy SDL2 window and synchronization primitives */
    (void)WindowDeinit();

    APP_INFO("Application exited cleanly.");

    /* Return 0 on normal termination */
    return 0;
}

/* reserved ***************************************************************************************
 **************************************************************************************************/
