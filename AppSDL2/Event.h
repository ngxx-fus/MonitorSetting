/* file-desc **************************************************************************************
 * @filename    Event.h
 * @brief       Event listener types, callback union, sleeping dispatcher thread, and drag-and-drop.
 * @note        Supports sleeping/waking event and main threads via SDL_cond and SDL_WaitEvent.
 **************************************************************************************************/

#ifndef __APP_SDL2_EVENT_H__
#define __APP_SDL2_EVENT_H__

/* headers ****************************************************************************************
 **************************************************************************************************/

#include "./Component.h"

/* macros *****************************************************************************************
 **************************************************************************************************/

#define APP_SDL2_MAX_EVENT_QUEUE        (64U)

/* definitions ************************************************************************************
 **************************************************************************************************/

/* Enumeration of supported listener event categories (mouse/keyboard/system/acpi/monitor/usb/drag) */
enum EventType_e {
    EVENT_TYPE_NONE                     = (0U),
    EVENT_TYPE_MOUSE_DOWN               = (1U),
    EVENT_TYPE_MOUSE_UP                 = (2U),
    EVENT_TYPE_MOUSE_MOVE               = (3U),
    EVENT_TYPE_MOUSE_WHEEL              = (4U),
    EVENT_TYPE_KEY_DOWN                 = (5U),
    EVENT_TYPE_KEY_UP                   = (6U),
    EVENT_TYPE_SYSTEM                   = (7U),
    EVENT_TYPE_ACPI                     = (8U),
    EVENT_TYPE_MONITOR                  = (9U),
    EVENT_TYPE_USB                      = (10U),
    EVENT_TYPE_DRAG_START               = (11U),
    EVENT_TYPE_DRAG_MOVE                = (12U),
    EVENT_TYPE_DROP                     = (13U)
};
typedef enum EventType_e    EventType_t;

/* Event payload passed to listener callbacks */
struct EventData_st {
    EventType_t Type;
    int32_t     MouseX;
    int32_t     MouseY;
    int32_t     WheelDeltaX;
    int32_t     WheelDeltaY;
    uint32_t    KeyCode;
    uint32_t    SysCode;
    void       *p_UserData;
};
typedef struct EventData_st EventData_t;

/* Callback function pointer signature for component event listeners */
typedef void (*EventCallback_t)(Comp_t *p_comp, const EventData_t *p_event);

/* Layer*.Comp*.Listeners.Event* union as required by MDF */
union CompEvent_un {
    struct {
        EventType_t     Type;
        EventCallback_t p_CallBack;
    } Listener;
    uint64_t Raw[2];
};
typedef union CompEvent_un  CompEvent_t;

/* Drag-and-drop state tracking structure */
struct DragDropCtx_st {
    uint8_t  IsDragging;
    Comp_t  *p_DraggedComp;
    Comp_t  *p_DropTargetComp;
    int32_t  GrabOffsetX;
    int32_t  GrabOffsetY;
};
typedef struct DragDropCtx_st DragDropCtx_t;

/* exported variables *****************************************************************************
 **************************************************************************************************/

extern DragDropCtx_t g_DragDropCtx;

/* exported functions *****************************************************************************
 **************************************************************************************************/

Err_t EventDispatchToComp(Comp_t *p_comp, const EventData_t *p_event);
Err_t EventHandleDragDrop(Layer_t *p_layers, uint32_t layer_count, const EventData_t *p_event);
Err_t EventDispatchToLayers(Layer_t *p_layers, uint32_t layer_count, const EventData_t *p_event);
Err_t EventPushToListenerQueue(const EventData_t *p_event);
Err_t EventPollOSOnMainThread(uint8_t *p_quit_requested);
Err_t EventWaitOSOnMainThread(uint8_t *p_quit_requested, uint32_t timeout_ms);
Err_t EventListenerThreadStart(void);
Err_t EventListenerThreadStop(void);

/* reserved ***************************************************************************************
 **************************************************************************************************/

#endif /* __APP_SDL2_EVENT_H__ */
