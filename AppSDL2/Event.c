/* file-desc **************************************************************************************
 * @filename    Event.c
 * @brief       Sleeping event listener sub-thread, user signals, button hit-test, and drag/drop.
 * @note        Uses SDL_cond + SDL_mutex so event-thread and main-thread sleep until woken up.
 **************************************************************************************************/

/* headers ****************************************************************************************
 **************************************************************************************************/

#include "./__common__.h"
#include "./Event.h"
#include "./Window.h"

/* macros *****************************************************************************************
 **************************************************************************************************/

/* definitions ************************************************************************************
 **************************************************************************************************/

/* Internal thread-safe circular queue with condition variable for sleeping/waking EventListenerThread */
struct EventRingQueue_st {
    EventData_t   Events[APP_SDL2_MAX_EVENT_QUEUE];
    CircleQueue_t Queue;
    SDL_mutex    *p_Mutex;
    SDL_cond     *p_Cond;
    SDL_Thread   *p_Thread;
    uint8_t       IsRunning;
};
typedef struct EventRingQueue_st EventRingQueue_t;

/* exported variables *****************************************************************************
 **************************************************************************************************/

/* public variables *******************************************************************************
 **************************************************************************************************/

DragDropCtx_t g_DragDropCtx = {
    .IsDragging       = 0U,
    .p_DraggedComp    = NULL,
    .p_DropTargetComp = NULL,
    .GrabOffsetX      = 0,
    .GrabOffsetY      = 0
};

/* private variables ******************************************************************************
 **************************************************************************************************/

static EventRingQueue_t g_EventQueue = {
    .p_Mutex   = NULL,
    .p_Cond    = NULL,
    .p_Thread  = NULL,
    .IsRunning = 0U
};

/* Active parent layer of the currently dragged component */
static const Layer_t *gp_DraggedLayer = NULL;

/* exported functions *****************************************************************************
 **************************************************************************************************/

/* public functions *******************************************************************************
 **************************************************************************************************/

Err_t EventDispatchToComp(Comp_t *p_comp, const EventData_t *p_event);
Err_t EventHandleDragDrop(Layer_t *p_layers, uint32_t layer_count, const EventData_t *p_event);
Err_t EventDispatchToLayers(Layer_t *p_layers, uint32_t layer_count, const EventData_t *p_event);
Err_t EventPushToListenerQueue(const EventData_t *p_event);
Err_t EventSendUserSignal(uint32_t sig_code, void *p_user_data);
Err_t EventPollOSOnMainThread(uint8_t *p_quit_requested);
Err_t EventWaitOSOnMainThread(uint8_t *p_quit_requested, uint32_t timeout_ms);
Err_t EventListenerThreadStart(void);
Err_t EventListenerThreadStop(void);

/* private functions ******************************************************************************
 **************************************************************************************************/

static void MapWindowToLayerCoords(const Layer_t *p_layer,
                                   int32_t        win_x,
                                   int32_t        win_y,
                                   int32_t       *p_local_x,
                                   int32_t       *p_local_y,
                                   uint8_t       *p_in_clip);
static void TranslateAndForwardSdlEvent(const SDL_Event *p_sdl_ev, uint8_t *p_quit_requested);
static int  EventListenerThreadEntry(void *p_arg);

/* function implementations ***********************************************************************
 **************************************************************************************************/

/* Map MainWindow (0,0) mouse coordinates into Layer (0,0) local coordinates (handling RectClip rotation)
 * @param p_layer[in] Target layer whose RectClip and ScrollX/Y are used
 * @param win_x[in] Mouse X in MainWindow (0,0) coordinates
 * @param win_y[in] Mouse Y in MainWindow (0,0) coordinates
 * @param p_local_x[out] Mapped X coordinate in Layer (0,0) space
 * @param p_local_y[out] Mapped Y coordinate in Layer (0,0) space
 * @param p_in_clip[out] Outputs 1U if point lies inside the (optionally rotated) RectClip, 0U otherwise
 */
static void MapWindowToLayerCoords(const Layer_t *p_layer,
                                   int32_t        win_x,
                                   int32_t        win_y,
                                   int32_t       *p_local_x,
                                   int32_t       *p_local_y,
                                   uint8_t       *p_in_clip) {
    int32_t  unrot_x = win_x;
    int32_t  unrot_y = win_y;
    uint16_t rot_deg = (uint16_t)(p_layer->RectClip.Rotation % 360U);

    /* Inverse-rotate window point around RectClip center if Layer rotation is non-zero */
    if (0U != rot_deg) {
        double clip_w = (p_layer->RectClip.W > 0U) ? (double)p_layer->RectClip.W : (double)g_MainWindow.Width;
        double clip_h = (p_layer->RectClip.H > 0U) ? (double)p_layer->RectClip.H : (double)g_MainWindow.Height;
        double cx     = (double)p_layer->RectClip.OffsetX + (clip_w * 0.5);
        double cy     = (double)p_layer->RectClip.OffsetY + (clip_h * 0.5);
        double dx     = (double)win_x - cx;
        double dy     = (double)win_y - cy;
        double rad    = (double)rot_deg * (M_PI / 180.0);
        double cos_a  = SDL_cos(rad);
        double sin_a  = SDL_sin(rad);

        unrot_x = (int32_t)(cx + (dx * cos_a) + (dy * sin_a));
        unrot_y = (int32_t)(cy - (dx * sin_a) + (dy * cos_a));
    }

    *p_in_clip = 1U;
    /* Check if unrotated point lies inside Layer RectClip bounds */
    if ((p_layer->RectClip.W > 0U) && (p_layer->RectClip.H > 0U)) {
        if ((unrot_x < p_layer->RectClip.OffsetX) ||
            (unrot_x >= (p_layer->RectClip.OffsetX + (int32_t)p_layer->RectClip.W)) ||
            (unrot_y < p_layer->RectClip.OffsetY) ||
            (unrot_y >= (p_layer->RectClip.OffsetY + (int32_t)p_layer->RectClip.H))) {
            *p_in_clip = 0U;
        }
    }

    *p_local_x = unrot_x - p_layer->RectClip.OffsetX - p_layer->ScrollX;
    *p_local_y = unrot_y - p_layer->RectClip.OffsetY - p_layer->ScrollY;

    /* Return after mapping coordinates */
    return;
}

/* Sequentially execute all registered callbacks on a component matching the incoming event type
 * @param p_comp[in/out] Target component whose listeners will be checked
 * @param p_event[in] Event data received from OS/SDL2
 * @return APP_SUCCESS on success, or error code if arguments are NULL
 * @note Skips execution if the component has no listeners
 */
Err_t EventDispatchToComp(Comp_t *p_comp, const EventData_t *p_event) {
    uint32_t idx;

    #if (1U == PARAM_CHECK_EN)
    /* Validate component and event pointers */
    if ((NULL == p_comp) || (NULL == p_event)) {
        APP_ERR("Null pointer passed to EventDispatchToComp!");
        return APP_ERROR_NULL;
    }
    #endif /* (1U == PARAM_CHECK_EN) */

    /* Check if component has any registered event listeners */
    if ((0U == p_comp->Listeners.Count) || (NULL == p_comp->Listeners.p_Events)) {
        /* Return early when no listeners are attached */
        return APP_SUCCESS;
    }

    /* Iterate through all registered listeners to invoke matching callbacks sequentially */
    for (idx = 0U; idx < p_comp->Listeners.Count; idx++) {
        CompEvent_t *p_listener = &p_comp->Listeners.p_Events[idx];

        /* Check if listener matches incoming event type and has a valid callback */
        if ((p_listener->Listener.Type == p_event->Type) &&
            (NULL != p_listener->Listener.p_CallBack)) {
            /* Invoke registered event listener callback */
            p_listener->Listener.p_CallBack(p_comp, p_event);
        }
    }

    /* Return success after dispatching to all matching listeners */
    return APP_SUCCESS;
}

/* Process drag-and-drop state transitions (grab on mouse down, move on drag, drop on mouse up)
 * @param p_layers[in/out] Array of layers to search for draggable/drop-target components
 * @param layer_count[in] Number of layers in p_layers
 * @param p_event[in] Mouse event data
 * @return APP_SUCCESS on success, or error status
 * @note Enqueues a window update action whenever a dragged component changes position
 */
Err_t EventHandleDragDrop(Layer_t *p_layers, uint32_t layer_count, const EventData_t *p_event) {
    int32_t     layer_idx;
    int32_t     comp_idx;
    uint8_t     is_hit;
    EventData_t drag_ev;

    #if (1U == PARAM_CHECK_EN)
    /* Validate layer array and event pointer */
    if ((NULL == p_layers) || (NULL == p_event)) {
        APP_ERR("Null pointer in EventHandleDragDrop!");
        return APP_ERROR_NULL;
    }
    #endif /* (1U == PARAM_CHECK_EN) */

    /* Handle drag state transitions based on mouse event type */
    if (EVENT_TYPE_MOUSE_DOWN == p_event->Type) {
        /* Search layers from top-most to bottom-most for a hit draggable component */
        for (layer_idx = (int32_t)layer_count - 1; layer_idx >= 0; layer_idx--) {
            Layer_t *p_layer = &p_layers[layer_idx];
            int32_t  local_x = 0;
            int32_t  local_y = 0;
            uint8_t  in_clip = 1U;

            /* Skip invisible layers */
            if (0U == p_layer->IsVisible) {
                /* Continue to next layer */
                continue;
            }

            /* Map MainWindow mouse coordinates to Layer (0,0) local space (including RectClip rotation) */
            MapWindowToLayerCoords(p_layer, p_event->MouseX, p_event->MouseY, &local_x, &local_y, &in_clip);
            if (0U == in_clip) {
                /* Mouse is outside this layer's RectClip */
                continue;
            }

            /* Iterate components in reverse order (top-most first) */
            for (comp_idx = (int32_t)p_layer->CompCount - 1; comp_idx >= 0; comp_idx--) {
                Comp_t *p_comp = &p_layer->p_Comps[comp_idx];

                /* Skip non-draggable components */
                if (0U == p_comp->IsDraggable) {
                    /* Continue to next component */
                    continue;
                }

                /* Perform hit test in Layer (0,0) local coordinates */
                is_hit = 0U;
                (void)CompHitTest(p_comp, local_x, local_y, &is_hit);

                /* Begin dragging if mouse pressed inside draggable component */
                if (0U != is_hit) {
                    g_DragDropCtx.IsDragging    = 1U;
                    g_DragDropCtx.p_DraggedComp = p_comp;
                    g_DragDropCtx.GrabOffsetX   = local_x - p_comp->Type.Rect.Dim.OffsetX;
                    g_DragDropCtx.GrabOffsetY   = local_y - p_comp->Type.Rect.Dim.OffsetY;
                    gp_DraggedLayer             = p_layer;

                    drag_ev      = *p_event;
                    drag_ev.Type = EVENT_TYPE_DRAG_START;
                    /* Notify component of drag start */
                    (void)EventDispatchToComp(p_comp, &drag_ev);
                    /* Push redraw request to main window update queue */
                    (void)WindowQueuePush(WIN_ACTION_UPDATE_ALL, p_comp);
                    /* Return once top-most hit draggable component is grabbed */
                    return APP_SUCCESS;
                }
            }
        }
    } else if ((EVENT_TYPE_MOUSE_MOVE == p_event->Type) && (0U != g_DragDropCtx.IsDragging)) {
        /* Assert dragged component pointer is valid when IsDragging is active */
        ASSERT_NOT_EQUAL(NULL, g_DragDropCtx.p_DraggedComp);
        {
            int32_t local_x = p_event->MouseX;
            int32_t local_y = p_event->MouseY;
            uint8_t in_clip = 1U;
            int32_t new_x;
            int32_t new_y;

            if (NULL != gp_DraggedLayer) {
                MapWindowToLayerCoords(gp_DraggedLayer,
                                       p_event->MouseX,
                                       p_event->MouseY,
                                       &local_x,
                                       &local_y,
                                       &in_clip);
            }

            new_x = local_x - g_DragDropCtx.GrabOffsetX;
            new_y = local_y - g_DragDropCtx.GrabOffsetY;

            /* Acquire UI lock briefly while mutating component coordinates */
            (void)WindowLockUI();
            /* Update component position in Layer (0,0) coordinates to follow mouse cursor */
            (void)CompSetPosition(g_DragDropCtx.p_DraggedComp, new_x, new_y);
            /* Release UI lock immediately after coordinate update */
            (void)WindowUnlockUI();

            drag_ev      = *p_event;
            drag_ev.Type = EVENT_TYPE_DRAG_MOVE;
            /* Notify component of drag movement */
            (void)EventDispatchToComp(g_DragDropCtx.p_DraggedComp, &drag_ev);

            /* Queue full window redraw on g_MainWindowUpdateQueue (wakes main thread) */
            (void)WindowQueuePush(WIN_ACTION_UPDATE_ALL, g_DragDropCtx.p_DraggedComp);
        }
    } else if ((EVENT_TYPE_MOUSE_UP == p_event->Type) && (0U != g_DragDropCtx.IsDragging)) {
        /* Assert dragged component pointer is valid before completing drop operation */
        ASSERT_NOT_EQUAL(NULL, g_DragDropCtx.p_DraggedComp);
        drag_ev      = *p_event;
        drag_ev.Type = EVENT_TYPE_DROP;
        /* Notify dragged component that drop occurred */
        (void)EventDispatchToComp(g_DragDropCtx.p_DraggedComp, &drag_ev);

        /* Queue final window update on g_MainWindowUpdateQueue (wakes main thread) */
        (void)WindowQueuePush(WIN_ACTION_UPDATE_ALL, g_DragDropCtx.p_DraggedComp);

        g_DragDropCtx.IsDragging       = 0U;
        g_DragDropCtx.p_DraggedComp    = NULL;
        g_DragDropCtx.p_DropTargetComp = NULL;
        gp_DraggedLayer                = NULL;
    } else {
        /* No drag-and-drop action required for other event types */
    }

    /* Return success after processing drag-and-drop state */
    return APP_SUCCESS;
}

/* Dispatch an incoming event across all visible layers and their components
 * @param p_layers[in/out] Pointer to array of layers
 * @param layer_count[in] Number of layers
 * @param p_event[in] Incoming event to dispatch
 * @return APP_SUCCESS on success, or error status
 * @note Converts MainWindow mouse coords into Layer (0,0) coords using Layer RectClip and Rotation
 */
Err_t EventDispatchToLayers(Layer_t *p_layers, uint32_t layer_count, const EventData_t *p_event) {
    uint32_t layer_idx;
    uint32_t comp_idx;

    #if (1U == PARAM_CHECK_EN)
    /* Validate layer array and event pointer */
    if ((NULL == p_layers) || (NULL == p_event)) {
        APP_ERR("Null pointer in EventDispatchToLayers!");
        return APP_ERROR_NULL;
    }
    #endif /* (1U == PARAM_CHECK_EN) */

    /* Update drag-and-drop state machine first */
    (void)EventHandleDragDrop(p_layers, layer_count, p_event);

    /* Iterate through all layers to dispatch event to component listeners */
    for (layer_idx = 0U; layer_idx < layer_count; layer_idx++) {
        Layer_t *p_layer      = &p_layers[layer_idx];
        uint8_t  in_rect_clip = 1U;
        int32_t  local_x      = 0;
        int32_t  local_y      = 0;

        /* Skip hidden layers */
        if (0U == p_layer->IsVisible) {
            /* Continue to next layer */
            continue;
        }

        /* Map MainWindow mouse position to Layer (0,0) local coordinates and check RectClip */
        MapWindowToLayerCoords(p_layer, p_event->MouseX, p_event->MouseY, &local_x, &local_y, &in_rect_clip);

        /* Scroll entire layer if layer is marked IsScrollable and mouse wheel is inside RectClip */
        if ((EVENT_TYPE_MOUSE_WHEEL == p_event->Type) &&
            (0U != p_layer->IsScrollable) &&
            (0U != in_rect_clip)) {
            (void)WindowLockUI();
            (void)LayerScroll(p_layer, p_event->WheelDeltaX * 16, p_event->WheelDeltaY * 16);
            (void)WindowUnlockUI();
            (void)WindowQueuePush(WIN_ACTION_UPDATE_ALL, NULL);
        }

        /* Iterate through all components in the current layer */
        for (comp_idx = 0U; comp_idx < p_layer->CompCount; comp_idx++) {
            Comp_t *p_comp = &p_layer->p_Comps[comp_idx];

            /* Scroll standalone table only when parent layer itself is not scrollable */
            if ((EVENT_TYPE_MOUSE_WHEEL == p_event->Type) &&
                (0U == p_layer->IsScrollable) &&
                (COMP_CAST_SCROLL_TABLE == p_comp->CastType) &&
                (0U != p_comp->Type.Table.IsScrollable) &&
                (0U != in_rect_clip)) {
                (void)WindowLockUI();
                (void)CompScrollTable(p_comp, p_event->WheelDeltaX * 16, p_event->WheelDeltaY * 16);
                (void)WindowUnlockUI();
                (void)WindowQueuePush(WIN_ACTION_UPDATE_ALL, p_comp);
            }

            /* For mouse button events, require cursor to be inside Layer RectClip and component */
            if ((EVENT_TYPE_MOUSE_DOWN == p_event->Type) || (EVENT_TYPE_MOUSE_UP == p_event->Type)) {
                uint8_t is_hit = 0U;
                if (0U == in_rect_clip) {
                    /* Skip if mouse click was outside this layer's RectClip */
                    continue;
                }
                (void)CompHitTest(p_comp, local_x, local_y, &is_hit);
                if (0U == is_hit) {
                    /* Skip click callback if mouse cursor was outside this component */
                    continue;
                }
            }

            /* Dispatch event to current component listeners */
            (void)EventDispatchToComp(p_comp, p_event);
        }
    }

    /* Return success after dispatching event to all layers */
    return APP_SUCCESS;
}

/* Push an event into the Event Listener circular queue and wake up EventListenerThread
 * @param p_event[in] Event payload to enqueue
 * @return APP_SUCCESS on success, or error code
 * @note Uses CircleQueue_t (overwrite enabled) and coalesces consecutive mouse-move events
 */
Err_t EventPushToListenerQueue(const EventData_t *p_event) {
    Err_t err = APP_SUCCESS;

    #if (1U == PARAM_CHECK_EN)
    /* Validate event pointer */
    if (NULL == p_event) {
        APP_ERR("p_event is NULL!");
        return APP_ERROR_NULL;
    }
    #endif /* (1U == PARAM_CHECK_EN) */

    /* Lock event queue mutex if initialized */
    if (NULL != g_EventQueue.p_Mutex) {
        (void)SDL_LockMutex(g_EventQueue.p_Mutex);
    }

    /* Coalesce consecutive mouse move events so high-frequency motion does not flood the queue */
    if ((EVENT_TYPE_MOUSE_MOVE == p_event->Type) && (g_EventQueue.Queue.Count > 0U)) {
        uint32_t last_idx = (g_EventQueue.Queue.Tail + g_EventQueue.Queue.Capacity - 1U) %
                            g_EventQueue.Queue.Capacity;
        /* Overwrite the last queued event in-place if it is also a mouse move event */
        if (EVENT_TYPE_MOUSE_MOVE == g_EventQueue.Events[last_idx].Type) {
            g_EventQueue.Events[last_idx] = *p_event;
        } else {
            /* Push mouse move into circular queue (overwrites oldest if full) */
            err = CircleQueuePush(&g_EventQueue.Queue, p_event);
        }
    } else {
        /* Push event into circular queue (overwrites oldest if full) */
        err = CircleQueuePush(&g_EventQueue.Queue, p_event);
    }

    /* Signal condition variable to wake up sleeping EventListenerThread */
    if (NULL != g_EventQueue.p_Cond) {
        (void)SDL_CondSignal(g_EventQueue.p_Cond);
    }

    /* Unlock event queue mutex */
    if (NULL != g_EventQueue.p_Mutex) {
        (void)SDL_UnlockMutex(g_EventQueue.p_Mutex);
    }

    /* Return push status */
    return err;
}

/* Send a custom user signal event into the Event Listener queue
 * @param sig_code[in] Application-defined signal code
 * @param p_user_data[in] Optional user payload pointer
 * @return APP_SUCCESS on success, or error code
 * @note Wakes up EventListenerThread to execute EVENT_TYPE_USER_SIGNAL callbacks
 */
Err_t EventSendUserSignal(uint32_t sig_code, void *p_user_data) {
    EventData_t usr_ev;

    usr_ev.Type        = EVENT_TYPE_USER_SIGNAL;
    usr_ev.MouseX      = 0;
    usr_ev.MouseY      = 0;
    usr_ev.WheelDeltaX = 0;
    usr_ev.WheelDeltaY = 0;
    usr_ev.KeyCode     = 0U;
    usr_ev.SysCode     = sig_code;
    usr_ev.p_UserData  = p_user_data;

    /* Push custom user signal to listener queue */
    return EventPushToListenerQueue(&usr_ev);
}

/* Translate a single SDL_Event and push it to the Event Listener thread queue
 * @param p_sdl_ev[in] Pointer to raw SDL_Event
 * @param p_quit_requested[out] Set to 1U if SDL_QUIT or ESC key is detected
 * @note Internal helper shared by EventPollOSOnMainThread and EventWaitOSOnMainThread
 */
static void TranslateAndForwardSdlEvent(const SDL_Event *p_sdl_ev, uint8_t *p_quit_requested) {
    EventData_t app_ev;

    /* Assert non-null internal helper pointers */
    ASSERT_NOT_EQUAL(NULL, p_sdl_ev);
    ASSERT_NOT_EQUAL(NULL, p_quit_requested);

    app_ev.Type        = EVENT_TYPE_NONE;
    app_ev.MouseX      = 0;
    app_ev.MouseY      = 0;
    app_ev.WheelDeltaX = 0;
    app_ev.WheelDeltaY = 0;
    app_ev.KeyCode     = 0U;
    app_ev.SysCode     = 0U;
    app_ev.p_UserData  = NULL;

    /* Translate SDL_Event type to AppSDL2 EventType_t */
    switch (p_sdl_ev->type) {
        case SDL_QUIT:
            *p_quit_requested = 1U;
            /* Break on SDL_QUIT */
            break;
        case SDL_MOUSEBUTTONDOWN:
            app_ev.Type   = EVENT_TYPE_MOUSE_DOWN;
            app_ev.MouseX = p_sdl_ev->button.x;
            app_ev.MouseY = p_sdl_ev->button.y;
            /* Break on mouse button down */
            break;
        case SDL_MOUSEBUTTONUP:
            app_ev.Type   = EVENT_TYPE_MOUSE_UP;
            app_ev.MouseX = p_sdl_ev->button.x;
            app_ev.MouseY = p_sdl_ev->button.y;
            /* Break on mouse button up */
            break;
        case SDL_MOUSEMOTION:
            app_ev.Type   = EVENT_TYPE_MOUSE_MOVE;
            app_ev.MouseX = p_sdl_ev->motion.x;
            app_ev.MouseY = p_sdl_ev->motion.y;
            /* Break on mouse motion */
            break;
        case SDL_MOUSEWHEEL:
            app_ev.Type        = EVENT_TYPE_MOUSE_WHEEL;
            app_ev.WheelDeltaX = p_sdl_ev->wheel.x;
            app_ev.WheelDeltaY = p_sdl_ev->wheel.y;
            /* Query current mouse cursor position in MainWindow (0,0) coordinates */
            (void)SDL_GetMouseState(&app_ev.MouseX, &app_ev.MouseY);
            /* Break on mouse wheel */
            break;
        case SDL_KEYDOWN:
            /* Check if ESC key was pressed to request exit */
            if (SDLK_ESCAPE == p_sdl_ev->key.keysym.sym) {
                *p_quit_requested = 1U;
            }
            app_ev.Type    = EVENT_TYPE_KEY_DOWN;
            app_ev.KeyCode = (uint32_t)p_sdl_ev->key.keysym.sym;
            /* Break on key down */
            break;
        case SDL_KEYUP:
            app_ev.Type    = EVENT_TYPE_KEY_UP;
            app_ev.KeyCode = (uint32_t)p_sdl_ev->key.keysym.sym;
            /* Break on key up */
            break;
        case SDL_WINDOWEVENT:
            /* Trigger window redraw if exposed or resized */
            if ((SDL_WINDOWEVENT_EXPOSED == p_sdl_ev->window.event) ||
                (SDL_WINDOWEVENT_SIZE_CHANGED == p_sdl_ev->window.event)) {
                (void)WindowQueuePush(WIN_ACTION_UPDATE_ALL, NULL);
            }
            /* Break on window event */
            break;
        default:
            /* Custom wakeup event or unhandled SDL event */
            break;
    }

    /* Forward translated event to the Event Listener sub-thread queue */
    if (EVENT_TYPE_NONE != app_ev.Type) {
        (void)EventPushToListenerQueue(&app_ev);
    }

    /* Return after translating event */
    return;
}

/* Poll pending SDL2 OS events on the main thread without blocking
 * @param p_quit_requested[out] Set to 1U when SDL_QUIT or ESC is received
 * @return APP_SUCCESS on success, or error code
 * @note Must be called from the thread that created the SDL2 window
 */
Err_t EventPollOSOnMainThread(uint8_t *p_quit_requested) {
    SDL_Event sdl_ev;

    #if (1U == PARAM_CHECK_EN)
    /* Validate quit flag pointer */
    if (NULL == p_quit_requested) {
        APP_ERR("p_quit_requested is NULL!");
        return APP_ERROR_NULL;
    }
    #endif /* (1U == PARAM_CHECK_EN) */

    /* Drain all pending SDL events from OS message pump */
    while (0 != SDL_PollEvent(&sdl_ev)) {
        /* Translate and push event to listener queue */
        TranslateAndForwardSdlEvent(&sdl_ev, p_quit_requested);
    }

    /* Return success after polling SDL events */
    return APP_SUCCESS;
}

/* Put the main thread to sleep until woken up by an OS event or a sub-thread wakeup message
 * @param p_quit_requested[out] Set to 1U when SDL_QUIT or ESC is received
 * @param timeout_ms[in] Maximum sleep duration in milliseconds (0U to sleep indefinitely)
 * @return APP_SUCCESS on wakeup, or error code
 * @note Woken immediately when any sub-thread calls WindowQueuePush / WindowWakeupMainThread
 */
Err_t EventWaitOSOnMainThread(uint8_t *p_quit_requested, uint32_t timeout_ms) {
    SDL_Event sdl_ev;
    int       wait_res;

    #if (1U == PARAM_CHECK_EN)
    /* Validate quit flag pointer */
    if (NULL == p_quit_requested) {
        APP_ERR("p_quit_requested is NULL!");
        return APP_ERROR_NULL;
    }
    #endif /* (1U == PARAM_CHECK_EN) */

    /* Check if single-instance toggle signal was received from another process */
    (void)WindowCheckSingleInstanceSignal();

    /* Sleep until an SDL event or sub-thread wakeup arrives */
    if (0U == timeout_ms) {
        /* Sleep indefinitely until woken by an event or message */
        wait_res = SDL_WaitEvent(&sdl_ev);
    } else {
        /* Sleep up to timeout_ms milliseconds */
        wait_res = SDL_WaitEventTimeout(&sdl_ev, (int)timeout_ms);
    }

    /* Re-check single-instance toggle signal after waking up */
    (void)WindowCheckSingleInstanceSignal();

    /* Process the waking event and drain any remaining queued events */
    if (1 == wait_res) {
        TranslateAndForwardSdlEvent(&sdl_ev, p_quit_requested);
        /* Drain any additional events that arrived simultaneously */
        (void)EventPollOSOnMainThread(p_quit_requested);
    }

    /* Return success after waking up and forwarding events */
    return APP_SUCCESS;
}

/* Worker function for the Event Listener sub-thread (sleeps on p_Cond when idle)
 * @param p_arg[in] Unused thread user parameter
 * @return Thread exit code (0)
 * @note Sleeps with 0% CPU usage until woken by EventPushToListenerQueue or shutdown
 */
static int EventListenerThreadEntry(void *p_arg) {
    (void)p_arg;

    /* Run event listener loop while thread is active */
    while (0U != g_EventQueue.IsRunning) {
        EventData_t ev;
        uint8_t     has_ev = 0U;

        /* Lock mutex to inspect or wait on event queue */
        if (NULL != g_EventQueue.p_Mutex) {
            (void)SDL_LockMutex(g_EventQueue.p_Mutex);

            /* Put EventListenerThread to sleep while circular queue is empty and thread is running */
            while ((0U == g_EventQueue.Queue.Count) && (0U != g_EventQueue.IsRunning)) {
                /* Wait on condition variable (releases mutex while sleeping) */
                (void)SDL_CondWait(g_EventQueue.p_Cond, g_EventQueue.p_Mutex);
            }

            /* Pop one event from circular queue after waking up */
            if (APP_SUCCESS == CircleQueuePop(&g_EventQueue.Queue, &ev)) {
                has_ev = 1U;
            }

            /* Unlock mutex before executing callbacks */
            (void)SDL_UnlockMutex(g_EventQueue.p_Mutex);
        }

        /* Dispatch popped event sequentially across layers */
        if (0U != has_ev) {
            (void)EventDispatchToLayers(g_MainWindow.Layers, g_MainWindow.LayerCount, &ev);
        }
    }

    /* Return 0 when Event Listener thread terminates */
    return 0;
}

/* Start the dedicated Event Listener sub-thread with mutex and condition variable
 * @return APP_SUCCESS on success, or APP_ERROR_INIT_FAILED on thread creation failure
 * @note Initializes CircleQueue_t, SDL_mutex, SDL_cond, and spawns "EventListenerThread"
 */
Err_t EventListenerThreadStart(void) {
    /* Return early if thread is already running */
    if (0U != g_EventQueue.IsRunning) {
        /* Thread already active */
        return APP_SUCCESS;
    }

    /* Create mutex and condition variable for sleeping/waking the event thread */
    g_EventQueue.p_Mutex = SDL_CreateMutex();
    g_EventQueue.p_Cond  = SDL_CreateCond();
    if ((NULL == g_EventQueue.p_Mutex) || (NULL == g_EventQueue.p_Cond)) {
        APP_ERR("Failed to create event queue mutex/cond!");
        return APP_ERROR_INIT_FAILED;
    }

    /* Initialize circular queue with overwrite enabled (allow_overwrite = 1U) */
    (void)CircleQueueInit(&g_EventQueue.Queue,
                          g_EventQueue.Events,
                          (uint32_t)sizeof(EventData_t),
                          APP_SDL2_MAX_EVENT_QUEUE,
                          1U);
    g_EventQueue.IsRunning = 1U;

    /* Spawn SDL event listener worker thread */
    g_EventQueue.p_Thread = SDL_CreateThread(EventListenerThreadEntry, "EventListenerThread", NULL);
    if (NULL == g_EventQueue.p_Thread) {
        g_EventQueue.IsRunning = 0U;
        SDL_DestroyCond(g_EventQueue.p_Cond);
        SDL_DestroyMutex(g_EventQueue.p_Mutex);
        g_EventQueue.p_Cond  = NULL;
        g_EventQueue.p_Mutex = NULL;
        APP_ERR("Failed to create EventListenerThread!");
        return APP_ERROR_INIT_FAILED;
    }

    /* Return success after starting Event Listener thread */
    return APP_SUCCESS;
}

/* Stop, wake up, and join the dedicated Event Listener sub-thread
 * @return APP_SUCCESS on success
 * @note Broadcasts on p_Cond to wake the sleeping thread so it exits cleanly
 */
Err_t EventListenerThreadStop(void) {
    /* Lock mutex, clear running flag, and broadcast wakeup signal */
    if (NULL != g_EventQueue.p_Mutex) {
        (void)SDL_LockMutex(g_EventQueue.p_Mutex);
        g_EventQueue.IsRunning = 0U;
        if (NULL != g_EventQueue.p_Cond) {
            (void)SDL_CondBroadcast(g_EventQueue.p_Cond);
        }
        (void)SDL_UnlockMutex(g_EventQueue.p_Mutex);
    } else {
        g_EventQueue.IsRunning = 0U;
    }

    /* Wait for thread termination if thread handle is valid */
    if (NULL != g_EventQueue.p_Thread) {
        int thread_status = 0;
        SDL_WaitThread(g_EventQueue.p_Thread, &thread_status);
        g_EventQueue.p_Thread = NULL;
    }

    /* Destroy condition variable if allocated */
    if (NULL != g_EventQueue.p_Cond) {
        SDL_DestroyCond(g_EventQueue.p_Cond);
        g_EventQueue.p_Cond = NULL;
    }

    /* Destroy mutex if allocated */
    if (NULL != g_EventQueue.p_Mutex) {
        SDL_DestroyMutex(g_EventQueue.p_Mutex);
        g_EventQueue.p_Mutex = NULL;
    }

    /* Return success after stopping Event Listener thread */
    return APP_SUCCESS;
}

/* reserved ***************************************************************************************
 **************************************************************************************************/
