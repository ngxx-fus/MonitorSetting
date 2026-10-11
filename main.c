/* file-desc **************************************************************************************
 * @filename    main.c
 * @brief       Demo application showcasing AppCommon and AppSDL2 multi-layer UI and thread safety.
 * @note        Demonstrates:
 *              - Thread 1 (Main): Sleeps in EventWaitOSOnMainThread, wakes on OS event or queue push
 *              - Thread 2 (EventListenerThread): Sleeps on SDL_cond, wakes on event arrival
 *              - Thread 3 (MiscBackgroundThread): Sleeps on SDL_CondWaitTimeout, wakes on stop/timer
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

static char g_StatusMsgBuf[64] = "Background Tick: 0 (Drag the blue Monitor card!)";

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
                           "Background Thread Tick: %u | Drag cards or scroll table",
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

/* Application entry point setting up layers, components, listeners, and sleeping/waking threads
 * @param argc[in] Argument count
 * @param argv[in] Argument vector
 * @return 0 on clean exit, non-zero on initialization failure
 * @note Press ESC or close the window to exit cleanly
 */
int main(int argc, char *argv[]) {
    Err_t          err;
    uint8_t        quit_flag = 0U;
    SDL_Thread    *p_misc_thread;
    int            misc_thread_status = 0;

    /* Layer 0 components: Background panel, divider line, status circle, status text, scroll table */
    Comp_t         layer0_comps[5];
    /* Layer 1 components: Two draggable monitor selection cards */
    Comp_t         layer1_comps[2];

    /* Child text extensions (p_ExtComps) for the two draggable monitor cards */
    CompTextCfg_t  card0_title_cfg;
    ExtComp_t      card0_ext_item;
    ExtCompList_t  card0_ext_list;

    CompTextCfg_t  card1_title_cfg;
    ExtComp_t      card1_ext_item;
    ExtCompList_t  card1_ext_list;

    /* Event listener arrays for the draggable cards */
    CompEvent_t    card0_events[3];
    CompEvent_t    card1_events[2];

    (void)argc;
    (void)argv;

    /* 1. Initialize main SDL2 window, renderer, UI mutex, and wakeup event */
    err = WindowInit("MonitorSelection - AppSDL2 Demo (ESC to Quit)", DEMO_WIN_WIDTH, DEMO_WIN_HEIGHT);
    if (APP_SUCCESS != err) {
        APP_ERR("WindowInit failed with status: %s", Err2Str(err));
        return -1;
    }

    /* 2. Configure Layer 0 (Base UI elements) */
    {
        CompDim_t panel_dim  = { .W = 740U, .H = 460U, .OffsetX = 30,  .OffsetY = 30,  .Rotation = 0U };
        CompDim_t circle_dim = { .W = 24U,  .H = 24U,  .OffsetX = 50,  .OffsetY = 48,  .Rotation = 0U };
        CompDim_t table_dim  = { .W = 700U, .H = 180U, .OffsetX = 50,  .OffsetY = 280, .Rotation = 0U };
        Color_t   panel_col  = { .w = 0xFF232936U };
        Color_t   border_col = { .w = 0xFF4A5568U };
        Color_t   line_col   = { .w = 0xFF00B4D8U };
        Color_t   circle_col = { .w = 0xFF00E676U };
        Color_t   text_col   = { .w = 0xFFE2E8F0U };
        Color_t   table_col  = { .w = 0xFF1A202CU };

        /* Comp 0: Main container rounded rectangle */
        (void)CompInitRect(&layer0_comps[0], "MainPanel", panel_dim, panel_col, 10U, 1U);
        (void)CompSetBorder(&layer0_comps[0], 1U, border_col);

        /* Comp 1: Header separator line */
        (void)CompInitLine(&layer0_comps[1], "HeaderLine", 50, 82, 750, 82, line_col, 2U);

        /* Comp 2: Background thread pulsing status circle */
        (void)CompInitCircle(&layer0_comps[2], "StatusCircle", circle_dim, circle_col, 10U, 1U);
        (void)CompSetBorder(&layer0_comps[2], 1U, text_col);

        /* Comp 3: Status header text */
        (void)CompInitText(&layer0_comps[3], "HeaderText", 82, 54, g_StatusMsgBuf, text_col, 14U);

        /* Comp 4: Scrollable table (use mouse wheel to scroll rows/columns) */
        (void)CompInitTable(&layer0_comps[4], "MonitorTable", table_dim, table_col, 10U, 5U, 1U);
        (void)CompSetBorder(&layer0_comps[4], 1U, line_col);

        g_MainWindow.Layers[0].IsVisible = 1U;
        g_MainWindow.Layers[0].CompCount = 5U;
        g_MainWindow.Layers[0].p_Comps   = layer0_comps;
    }

    /* 3. Configure Layer 1 (Draggable Monitor Cards with p_ExtComps and Event Listeners) */
    {
        CompDim_t card0_dim  = { .W = 240U, .H = 130U, .OffsetX = 80,  .OffsetY = 110, .Rotation = 0U };
        CompDim_t card1_dim  = { .W = 240U, .H = 130U, .OffsetX = 380, .OffsetY = 110, .Rotation = 0U };
        Color_t   card0_col  = { .w = 0xDD1E4E8CU };
        Color_t   card1_col  = { .w = 0xDD2B6CB0U };
        Color_t   white_col  = { .w = 0xFFFFFFFFU };

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

        /* Attach listeners to Card 0 */
        card0_events[0].Listener.Type       = EVENT_TYPE_DRAG_START;
        card0_events[0].Listener.p_CallBack = OnCardDragStart;
        card0_events[1].Listener.Type       = EVENT_TYPE_DROP;
        card0_events[1].Listener.p_CallBack = OnCardDrop;
        card0_events[2].Listener.Type       = EVENT_TYPE_MONITOR;
        card0_events[2].Listener.p_CallBack = OnMonitorEvent;
        layer1_comps[0].Listeners.Count     = 3U;
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

    /* 4. Perform initial full window render */
    (void)WindowUpdateAll();

    /* 5. Start Sub-Thread 1: Dedicated Event Listener Thread (sleeps until events arrive) */
    err = EventListenerThreadStart();
    if (APP_SUCCESS != err) {
        APP_ERR("Failed to start EventListenerThread: %s", Err2Str(err));
        (void)WindowDeinit();
        return -1;
    }

    /* 6. Start Sub-Thread 2: Miscellaneous Background Thread with sleep/wakeup condition */
    g_MiscCtx.IsRunning      = 1U;
    g_MiscCtx.p_Mutex        = SDL_CreateMutex();
    g_MiscCtx.p_Cond         = SDL_CreateCond();
    g_MiscCtx.p_StatusCircle = &layer0_comps[2];
    g_MiscCtx.p_StatusText   = &layer0_comps[3];
    g_MiscCtx.TickCount      = 0U;
    p_misc_thread = SDL_CreateThread(MiscBackgroundThreadEntry, "MiscBackgroundThread", &g_MiscCtx);

    APP_INFO("Main loop started. Threads sleep when idle and wake up on event/message.");

    /* 7. Main Thread Loop: Sleep until woken by an OS event or a sub-thread queue wakeup */
    while (0U == quit_flag) {
        /* Sleep until an OS SDL2 event or WindowWakeupMainThread signal arrives */
        (void)EventWaitOSOnMainThread(&quit_flag, 0U);

        /* Drain and execute any pending UI update actions from sub-threads */
        (void)WindowQueueProcessAll();
    }

    /* 8. Wake up and join MiscBackgroundThread cleanly */
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
