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
#define DEMO_LAYER_COUNT                (3U)
#define DEMO_MAX_COMPS_PER_LAYER        (6U)
#define DEMO_CARD_COUNT                 (3U)
#define DEMO_MAX_CARD_EVENTS            (4U)

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

/* 2D array of components: [3 layers][up to 6 components per layer] */
static Comp_t        g_LayerComps[DEMO_LAYER_COUNT][DEMO_MAX_COMPS_PER_LAYER];

/* Child text extension arrays (p_ExtComps) for the 3 draggable monitor cards */
static CompTextCfg_t g_CardTitle[DEMO_CARD_COUNT];
static ExtComp_t     g_CardExtItem[DEMO_CARD_COUNT];
static ExtCompList_t g_CardExtList[DEMO_CARD_COUNT];

/* Event listener arrays for the 3 draggable cards and 2 action buttons */
static CompEvent_t   g_CardEvent[DEMO_CARD_COUNT][DEMO_MAX_CARD_EVENTS];
static CompEvent_t   g_BtnModeEvent;
static CompEvent_t   g_BtnYesNoEvent;

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
static void InitDemoLayers(void);
static void InitDemoComponents(void);
static void BindDemoEvents(void);
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

    /* Assert component and event pointers are non-NULL */
    ASSERT_NOT_EQUAL(NULL, p_comp);
    ASSERT_NOT_EQUAL(NULL, p_event);

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

    /* Assert component and event pointers are non-NULL */
    ASSERT_NOT_EQUAL(NULL, p_comp);
    ASSERT_NOT_EQUAL(NULL, p_event);

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
    /* Assert component and event pointers are non-NULL */
    ASSERT_NOT_EQUAL(NULL, p_comp);
    ASSERT_NOT_EQUAL(NULL, p_event);

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
    /* Assert component and event pointers are non-NULL */
    ASSERT_NOT_EQUAL(NULL, p_comp);
    ASSERT_NOT_EQUAL(NULL, p_event);

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

    /* Assert button and event pointers are non-NULL */
    ASSERT_NOT_EQUAL(NULL, p_comp);
    ASSERT_NOT_EQUAL(NULL, p_event);

    next_mode = (WIN_MODE_FLOATING == g_MainWindow.Mode) ? WIN_MODE_MAXIMIZED : WIN_MODE_FLOATING;
    /* Queue window mode switch to be executed on the main thread */
    (void)WindowQueuePushMode(next_mode);
    /* Also emit a custom user signal to demonstrate EVENT_TYPE_USER_SIGNAL */
    (void)EventSendUserSignal(100U + (uint32_t)next_mode, NULL);

    /* Return after queuing window mode toggle */
    return;
}

/* Click callback for the Quick Yes/No Dialog button component
 * @param p_comp[in/out] Button component that was clicked
 * @param p_event[in] Mouse click event data
 * @note Opens WindowShowYesNoDialog and logs user selection
 */
static void OnBtnYesNoClick(Comp_t *p_comp, const EventData_t *p_event) {
    uint8_t is_yes = 0U;
    Err_t   err;

    /* Assert button and event pointers are non-NULL */
    ASSERT_NOT_EQUAL(NULL, p_comp);
    ASSERT_NOT_EQUAL(NULL, p_event);

    /* Show quick Yes/No confirmation dialog */
    err = WindowShowYesNoDialog("Apply Monitor Layout?",
                                "Do you want to apply the selected monitor configuration?",
                                &is_yes);
    ASSERT_EQUAL(APP_SUCCESS, err);
    APP_INFO("Quick Yes/No Dialog result: %s", (0U != is_yes) ? "YES" : "NO");

    /* Return after dialog closes */
    return;
}

/* Step 1: Initialize all 3 layers in g_MainWindow and attach their component arrays
 * @note Layer 2 is configured with a scrollable RectClip relative to MainWindow (0, 0)
 */
static void InitDemoLayers(void) {
    /* Layer 2 viewport (RectClip) is positioned relative to (0, 0) of MainWindow */
    CompDim_t layer1_clip = { .W = 700U, .H = 200U, .OffsetX = 50, .OffsetY = 100, .Rotation = 0 };
    CompDim_t layer2_clip = { .W = 700U, .H = 180U, .OffsetX = 50, .OffsetY = 280, .Rotation = 0U };

    /* Layer 0: Base background panel, header line, status indicator, and 2 action buttons */
    g_MainWindow.Layers[0].IsVisible = 1U;
    g_MainWindow.Layers[0].CompCount = 6U;
    g_MainWindow.Layers[0].p_Comps   = g_LayerComps[0];

    /* Layer 1: Three draggable monitor selection cards */
    g_MainWindow.Layers[1].IsVisible = 1U;
    g_MainWindow.Layers[1].CompCount = DEMO_CARD_COUNT;
    g_MainWindow.Layers[1].p_Comps   = g_LayerComps[1];
    (void)LayerSetScrollable(&g_MainWindow.Layers[1], layer1_clip, 1U);

    /* Layer 2: Scrollable viewport layer containing the MonitorTable component */
    g_MainWindow.Layers[2].IsVisible = 1U;
    g_MainWindow.Layers[2].CompCount = 1U;
    g_MainWindow.Layers[2].p_Comps   = g_LayerComps[2];
    (void)LayerSetScrollable(&g_MainWindow.Layers[2], layer2_clip, 1U);

    /* Return after configuring all layers */
    return;
}

/* Step 2: Initialize all components inside Layer 0, Layer 1, and Layer 2
 * @note Uses g_LayerComps[layer_idx][comp_idx] and attaches child text extensions for cards
 */
static void InitDemoComponents(void) {
    uint32_t card_idx;

    /* Layer 0 dimensions and colors */
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
    Color_t   white_col   = { .w = 0xFFFFFFFFU };

    /* Layer 1 card lookup tables for labels, titles, and colors */
    const_str_t card_labels[DEMO_CARD_COUNT] = {
        "MonitorCard_1",
        "MonitorCard_2",
        "MonitorCard_3"
    };
    const_str_t card_titles[DEMO_CARD_COUNT] = {
        "[Monitor 1: HDMI-1\n1920x1080]",
        "[Monitor 2: DP-1\n2560x1440]",
        "[Monitor 3: USB-C\n3840x2160]"
    };
    Color_t card_colors[DEMO_CARD_COUNT] = {
        { .w = 0xDD1E4E8CU },
        { .w = 0xDD2B6CB0U },
        { .w = 0xDD2C7A7BU }
    };

    /* Layer 2 table dimensions (positioned at (0, 0) of Layer 2) and color */
    CompDim_t table_dim = { .W = 700U, .H = 300U, .OffsetX = 0, .OffsetY = 0, .Rotation = 0U };
    Color_t   table_col = { .w = 0xFF1A202CU };

    /* --- Layer 0 Components --- */
    /* Comp [0][0]: Main container rounded rectangle */
    (void)CompInitRect(&g_LayerComps[0][0], "MainPanel", panel_dim, panel_col, 10U, 1U);
    (void)CompSetBorder(&g_LayerComps[0][0], 1U, border_col);

    /* Comp [0][1]: Header separator line */
    (void)CompInitLine(&g_LayerComps[0][1], "HeaderLine", 50, 82, 750, 82, line_col, 2U);

    /* Comp [0][2]: Background thread pulsing status circle */
    (void)CompInitCircle(&g_LayerComps[0][2], "StatusCircle", circle_dim, circle_col, 10U, 1U);
    (void)CompSetBorder(&g_LayerComps[0][2], 1U, text_col);

    /* Comp [0][3]: Status header text */
    (void)CompInitText(&g_LayerComps[0][3], "HeaderText", 82, 56, g_StatusMsgBuf, text_col, 14U);

    /* Comp [0][4]: Button to toggle Maximize / Floating mode (event bound in BindDemoEvents) */
    (void)CompInitButton(&g_LayerComps[0][4], "Max/Float", btn_mod_dim, btn_col, NULL, NULL);

    /* Comp [0][5]: Button to open Quick Yes/No Dialog (event bound in BindDemoEvents) */
    (void)CompInitButton(&g_LayerComps[0][5], "Apply (Y/N)", btn_yn_dim, btn_col, NULL, NULL);

    /* --- Layer 1 Components (3 Draggable Monitor Cards relative to Layer 1 (0, 0)) --- */
    for (card_idx = 0U; card_idx < DEMO_CARD_COUNT; card_idx++) {
        CompDim_t card_dim = {
            .W        = 220U,
            .H        = 120U,
            .OffsetX  = 10 + ((int32_t)card_idx * 230),
            .OffsetY  = 40,
            .Rotation = 0U
        };

        /* Initialize card rectangle and mark as draggable button */
        (void)CompInitRect(&g_LayerComps[1][card_idx],
                           card_labels[card_idx],
                           card_dim,
                           card_colors[card_idx],
                           8U,
                           1U);
        (void)CompSetBorder(&g_LayerComps[1][card_idx], 1U, white_col);
        g_LayerComps[1][card_idx].Class       = COMP_CLASS_BUTTON;
        g_LayerComps[1][card_idx].IsDraggable = 1U;

        /* Attach centered and wrapped child text label to card via p_ExtComps */
        g_CardTitle[card_idx].Color    = white_col;
        g_CardTitle[card_idx].Dim      = card_dim;
        g_CardTitle[card_idx].TextStr  = card_titles[card_idx];
        g_CardTitle[card_idx].FontSize = 14U;
        (void)CompSetTextAlign(&g_CardTitle[card_idx], TEXT_ALIGN_CENTER, TEXT_VALIGN_MIDDLE, 1U);

        g_CardExtItem[card_idx].CastType       = COMP_CAST_TEXT;
        g_CardExtItem[card_idx].CompPtr.p_Text = &g_CardTitle[card_idx];
        g_CardExtList[card_idx].Count          = 1U;
        g_CardExtList[card_idx].p_ExtComps     = &g_CardExtItem[card_idx];
        g_LayerComps[1][card_idx].p_ExtComps   = &g_CardExtList[card_idx];
    }

    /* --- Layer 2 Component (Scrollable Table at (0, 0) of Layer 2) --- */
    (void)CompInitTable(&g_LayerComps[2][0], "MonitorTable", table_dim, table_col, 10U, 5U, 1U);
    (void)CompSetBorder(&g_LayerComps[2][0], 1U, line_col);

    /* Return after initializing all components */
    return;
}

/* Step 3: Attach event listeners to buttons in Layer 0 and draggable cards in Layer 1
 * @note Binds click, drag-start, drop, monitor, and user-signal callbacks
 */
static void BindDemoEvents(void) {
    uint32_t card_idx;

    /* Bind click callback to Layer 0 Comp 4 ("Max/Float" button) */
    g_BtnModeEvent.Listener.Type        = EVENT_TYPE_MOUSE_DOWN;
    g_BtnModeEvent.Listener.p_CallBack  = OnBtnModeClick;
    g_LayerComps[0][4].Listeners.Count    = 1U;
    g_LayerComps[0][4].Listeners.p_Events = &g_BtnModeEvent;

    /* Bind click callback to Layer 0 Comp 5 ("Apply (Y/N)" button) */
    g_BtnYesNoEvent.Listener.Type       = EVENT_TYPE_MOUSE_DOWN;
    g_BtnYesNoEvent.Listener.p_CallBack = OnBtnYesNoClick;
    g_LayerComps[0][5].Listeners.Count    = 1U;
    g_LayerComps[0][5].Listeners.p_Events = &g_BtnYesNoEvent;

    /* Bind drag/drop and system listeners to each card in Layer 1 */
    for (card_idx = 0U; card_idx < DEMO_CARD_COUNT; card_idx++) {
        g_CardEvent[card_idx][0].Listener.Type       = EVENT_TYPE_DRAG_START;
        g_CardEvent[card_idx][0].Listener.p_CallBack = OnCardDragStart;
        g_CardEvent[card_idx][1].Listener.Type       = EVENT_TYPE_DROP;
        g_CardEvent[card_idx][1].Listener.p_CallBack = OnCardDrop;

        /* Card 0 also listens for periodic EVENT_TYPE_MONITOR and custom EVENT_TYPE_USER_SIGNAL */
        if (0U == card_idx) {
            g_CardEvent[card_idx][2].Listener.Type       = EVENT_TYPE_MONITOR;
            g_CardEvent[card_idx][2].Listener.p_CallBack = OnMonitorEvent;
            g_CardEvent[card_idx][3].Listener.Type       = EVENT_TYPE_USER_SIGNAL;
            g_CardEvent[card_idx][3].Listener.p_CallBack = OnUserSignalEvent;
            g_LayerComps[1][card_idx].Listeners.Count    = 4U;
        } else {
            g_LayerComps[1][card_idx].Listeners.Count    = 2U;
        }
        g_LayerComps[1][card_idx].Listeners.p_Events = g_CardEvent[card_idx];
    }

    /* Return after binding all component event listeners */
    return;
}

/* Background housekeeping thread that sleeps on condition variable and pushes UI update actions
 * @param p_arg[in] Pointer to MiscThreadCtx_t
 * @return Thread exit status (0)
 * @note Uses SDL_CondWaitTimeout so it can be woken up immediately on shutdown
 */
static int MiscBackgroundThreadEntry(void *p_arg) {
    MiscThreadCtx_t *p_ctx = (MiscThreadCtx_t *)p_arg;

    /* Assert thread context pointer is non-NULL */
    ASSERT_NOT_EQUAL(NULL, p_ctx);

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

/* Application entry point setting up single-instance guard, layers, components, events, and threads
 * @param argc[in] Argument count
 * @param argv[in] Argument vector
 * @return 0 on clean exit, non-zero on initialization failure
 * @note Press ESC or close the window to exit cleanly
 */
int main(int argc, char *argv[]) {
    Err_t       err;
    uint8_t     is_primary         = 1U;
    uint8_t     quit_flag          = 0U;
    SDL_Thread *p_misc_thread      = NULL;
    int         misc_thread_status = 0;

    PARAMETTER_NOT_USED(argc);
    PARAMETTER_NOT_USED(argv);

    /* 0. Enforce single instance: if already running, toggle the 1st instance window and exit */
    err = WindowEnsureSingleInstance(&is_primary);
    if ((APP_SUCCESS != err) || (0U == is_primary)) {
        /* Exit secondary instance after signaling primary instance */
        return 0;
    }

    /* 1. Initialize main SDL2 window, renderer, UI mutex, and wakeup event */
    err = WindowInit("MonitorSelection - AppSDL2 Demo (ESC to Quit)", DEMO_WIN_WIDTH, DEMO_WIN_HEIGHT);
    ASSERT_EQUAL(APP_SUCCESS, err);

    /* 2. Initialize Layers -> Initialize Components -> Bind Events */
    InitDemoLayers();
    InitDemoComponents();
    BindDemoEvents();

    /* 3. Perform initial full window render */
    (void)WindowUpdateAll();

    /* 4. Start Sub-Thread 1: Dedicated Event Listener Thread (sleeps until events arrive) */
    err = EventListenerThreadStart();
    ASSERT_EQUAL(APP_SUCCESS, err);

    /* 5. Start Sub-Thread 2: Miscellaneous Background Thread with sleep/wakeup condition */
    g_MiscCtx.IsRunning      = 1U;
    g_MiscCtx.p_Mutex        = SDL_CreateMutex();
    g_MiscCtx.p_Cond         = SDL_CreateCond();
    g_MiscCtx.p_StatusCircle = &g_LayerComps[0][2];
    g_MiscCtx.p_StatusText   = &g_LayerComps[0][3];
    g_MiscCtx.TickCount      = 0U;
    p_misc_thread = SDL_CreateThread(MiscBackgroundThreadEntry, "MiscBackgroundThread", &g_MiscCtx);
    ASSERT_NOT_EQUAL(NULL, p_misc_thread);

    APP_INFO("Main loop started. Running ./main again in another terminal will toggle this window.");

    /* 6. Main Thread Loop: Sleep until woken by an OS event or a sub-thread queue wakeup */
    while (0U == quit_flag) {
        /* Sleep until an OS SDL2 event or WindowWakeupMainThread signal arrives */
        (void)EventWaitOSOnMainThread(&quit_flag, 0U);

        /* Drain and execute any pending UI update actions from sub-threads */
        (void)WindowQueueProcessAll();
    }

    /* 7. Wake up and join MiscBackgroundThread cleanly */
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
