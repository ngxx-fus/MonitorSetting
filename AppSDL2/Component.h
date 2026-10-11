/* file-desc **************************************************************************************
 * @filename    Component.h
 * @brief       UI component, scrollable layer, button, and child extension definitions for AppSDL2.
 * @note        Implements the Layer -> Comp -> Type / Listeners / p_ExtComps hierarchy.
 **************************************************************************************************/

#ifndef __APP_SDL2_COMPONENT_H__
#define __APP_SDL2_COMPONENT_H__

/* headers ****************************************************************************************
 **************************************************************************************************/

#include "../AppCommon/All.h"

/* macros *****************************************************************************************
 **************************************************************************************************/

#define COMP_MAX_ROTATION_DEG           (180U)

/* definitions ************************************************************************************
 **************************************************************************************************/

/* Forward declarations */
struct Comp_st;
struct EventData_st;
union CompEvent_un;

/* Callback function pointer signature for component event listeners */
typedef void (*EventCallback_t)(struct Comp_st *p_comp, const struct EventData_st *p_event);

/* Component class classification */
enum CompClass_e {
    COMP_CLASS_NONE                     = (0U),
    COMP_CLASS_CONTAINER                = (1U),
    COMP_CLASS_BUTTON                   = (2U),
    COMP_CLASS_LABEL                    = (3U),
    COMP_CLASS_TABLE                    = (4U),
    COMP_CLASS_CUSTOM                   = (5U)
};
typedef enum CompClass_e    CompClass_t;

/* Enum to identify the concrete cast/shape type of a component */
enum CompCastType_e {
    COMP_CAST_RECT                      = (0U),
    COMP_CAST_TEXT                      = (1U),
    COMP_CAST_LINE                      = (2U),
    COMP_CAST_CIRCLE                    = (3U),
    COMP_CAST_SCROLL_TABLE              = (4U)
};
typedef enum CompCastType_e CompCastType_t;

/* Component dimension and placement configuration */
struct CompDim_st {
    uint32_t W;
    uint32_t H;
    int32_t  OffsetX;
    int32_t  OffsetY;
    uint16_t Rotation;                  /* 0 to 180 degrees */
};
typedef struct CompDim_st   CompDim_t;

/* Component border configuration */
struct CompBorder_st {
    uint32_t Size;
    Color_t  Color;                     /* AARRGGBB */
};
typedef struct CompBorder_st CompBorder_t;

/* Rectangle component configuration */
struct CompRectCfg_st {
    Color_t      Color;                 /* AARRGGBB */
    CompDim_t    Dim;
    CompBorder_t Border;
    uint32_t     Radius;
    uint8_t      IsFilled;
};
typedef struct CompRectCfg_st CompRectCfg_t;

/* Circle component configuration */
struct CompCircleCfg_st {
    Color_t      Color;
    CompDim_t    Dim;
    CompBorder_t Border;
    uint32_t     Radius;
    uint8_t      IsFilled;
};
typedef struct CompCircleCfg_st CompCircleCfg_t;

/* Line component configuration */
struct CompLineCfg_st {
    Color_t      Color;
    CompDim_t    Dim;
    int32_t      EndX;
    int32_t      EndY;
    uint32_t     Thickness;
};
typedef struct CompLineCfg_st CompLineCfg_t;

/* Text component configuration */
struct CompTextCfg_st {
    Color_t      Color;
    CompDim_t    Dim;
    const_str_t  TextStr;
    uint32_t     FontSize;
};
typedef struct CompTextCfg_st CompTextCfg_t;

/* Scrollable table component configuration */
struct CompTableCfg_st {
    Color_t      Color;
    CompDim_t    Dim;
    CompBorder_t Border;
    uint32_t     Rows;
    uint32_t     Cols;
    int32_t      ScrollX;
    int32_t      ScrollY;
    uint8_t      IsScrollable;
};
typedef struct CompTableCfg_st CompTableCfg_t;

/* Layer*.Comp*.Type union as specified by MDF */
union CompType_un {
    CompRectCfg_t   Rect;
    CompCircleCfg_t Circle;
    CompLineCfg_t   Line;
    CompTextCfg_t   Text;
    CompTableCfg_t  Table;
};
typedef union CompType_un   CompType_t;

/* Union pointer to external/child component */
union ExtCompPtr_un {
    void           *p_Raw;
    struct Comp_st *p_Comp;
    CompTextCfg_t  *p_Text;
    CompRectCfg_t  *p_Rect;
    CompTableCfg_t *p_Table;
};
typedef union ExtCompPtr_un ExtCompPtr_t;

/* Child/external component entry (struct + union pointer) */
struct ExtComp_st {
    CompCastType_t CastType;
    ExtCompPtr_t   CompPtr;
};
typedef struct ExtComp_st   ExtComp_t;

/* Child/external component collection */
struct ExtCompList_st {
    uint32_t   Count;
    ExtComp_t *p_ExtComps;
};
typedef struct ExtCompList_st ExtCompList_t;

/* Listener list attached to a component */
struct CompListeners_st {
    uint32_t            Count;
    union CompEvent_un *p_Events;
};
typedef struct CompListeners_st CompListeners_t;

/* Main Component structure (Layer*.Comp*) */
struct Comp_st {
    CompClass_t     Class;
    const_str_t     Label;
    CompCastType_t  CastType;
    CompType_t      Type;
    CompListeners_t Listeners;
    ExtCompList_t  *p_ExtComps;
    uint8_t         IsDraggable;
    uint8_t         IsVisible;
};
typedef struct Comp_st      Comp_t;

/* Layer structure supporting RectClip (in MainWindow coords) and local (0,0) components */
struct Layer_st {
    uint32_t  LayerId;
    uint8_t   IsVisible;
    uint8_t   IsScrollable;
    int32_t   ScrollX;
    int32_t   ScrollY;
    CompDim_t RectClip;                 /* Positioned relative to (0, 0) of MainWindow */
    uint32_t  CompCount;
    Comp_t   *p_Comps;                  /* Components positioned relative to (0, 0) of Layer */
};
typedef struct Layer_st     Layer_t;

/* exported variables *****************************************************************************
 **************************************************************************************************/

/* exported functions *****************************************************************************
 **************************************************************************************************/

Err_t CompInitRect(Comp_t *p_comp, const_str_t label, CompDim_t dim, Color_t color, uint32_t radius, uint8_t is_filled);
Err_t CompInitButton(Comp_t *p_comp, const_str_t label, CompDim_t dim, Color_t color, union CompEvent_un *p_event_slot, EventCallback_t p_on_click);
Err_t CompInitCircle(Comp_t *p_comp, const_str_t label, CompDim_t dim, Color_t color, uint32_t radius, uint8_t is_filled);
Err_t CompInitLine(Comp_t *p_comp, const_str_t label, int32_t x1, int32_t y1, int32_t x2, int32_t y2, Color_t color, uint32_t thickness);
Err_t CompInitText(Comp_t *p_comp, const_str_t label, int32_t x, int32_t y, const_str_t text_str, Color_t color, uint32_t font_size);
Err_t CompInitTable(Comp_t *p_comp, const_str_t label, CompDim_t dim, Color_t color, uint32_t rows, uint32_t cols, uint8_t is_scrollable);
Err_t CompSetBorder(Comp_t *p_comp, uint32_t border_size, Color_t border_color);
Err_t CompSetPosition(Comp_t *p_comp, int32_t offset_x, int32_t offset_y);
Err_t CompScrollTable(Comp_t *p_comp, int32_t delta_x, int32_t delta_y);
Err_t LayerSetScrollable(Layer_t *p_layer, CompDim_t rect_clip, uint8_t is_scrollable);
Err_t LayerScroll(Layer_t *p_layer, int32_t delta_x, int32_t delta_y);
Err_t CompHitTest(const Comp_t *p_comp, int32_t x, int32_t y, uint8_t *p_is_hit);

/* reserved ***************************************************************************************
 **************************************************************************************************/

#endif /* __APP_SDL2_COMPONENT_H__ */
