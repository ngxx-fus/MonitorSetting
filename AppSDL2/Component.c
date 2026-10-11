/* file-desc **************************************************************************************
 * @filename    Component.c
 * @brief       Component, button, and scrollable layer manipulation implementation.
 * @note        Provides base operations for Rect, Button, Circle, Line, Text, Table, and Layer_t.
 **************************************************************************************************/

/* headers ****************************************************************************************
 **************************************************************************************************/

#include "./__common__.h"
#include "./Component.h"
#include "./Event.h"

/* macros *****************************************************************************************
 **************************************************************************************************/

/* definitions ************************************************************************************
 **************************************************************************************************/

/* exported variables *****************************************************************************
 **************************************************************************************************/

/* public variables *******************************************************************************
 **************************************************************************************************/

/* private variables ******************************************************************************
 **************************************************************************************************/

/* exported functions *****************************************************************************
 **************************************************************************************************/

/* public functions *******************************************************************************
 **************************************************************************************************/

Err_t CompInitRect(Comp_t *p_comp, const_str_t label, CompDim_t dim, Color_t color, uint32_t radius, uint8_t is_filled);
Err_t CompInitButton(Comp_t *p_comp, const_str_t label, CompDim_t dim, Color_t color, union CompEvent_un *p_event_slot, EventCallback_t p_on_click);
Err_t CompInitCircle(Comp_t *p_comp, const_str_t label, CompDim_t dim, Color_t color, uint32_t radius, uint8_t is_filled);
Err_t CompInitLine(Comp_t *p_comp, const_str_t label, int32_t x1, int32_t y1, int32_t x2, int32_t y2, Color_t color, uint32_t thickness);
Err_t CompInitText(Comp_t *p_comp, const_str_t label, int32_t x, int32_t y, const_str_t text_str, Color_t color, uint32_t font_size);
Err_t CompSetTextAlign(CompTextCfg_t *p_text, TextAlign_t align, TextVAlign_t valign, uint8_t is_wrap);
Err_t CompInitTable(Comp_t *p_comp, const_str_t label, CompDim_t dim, Color_t color, uint32_t rows, uint32_t cols, uint8_t is_scrollable);
Err_t CompSetBorder(Comp_t *p_comp, uint32_t border_size, Color_t border_color);
Err_t CompSetPosition(Comp_t *p_comp, int32_t offset_x, int32_t offset_y);
Err_t CompScrollTable(Comp_t *p_comp, int32_t delta_x, int32_t delta_y);
Err_t LayerSetScrollable(Layer_t *p_layer, CompDim_t viewport, uint8_t is_scrollable);
Err_t LayerScroll(Layer_t *p_layer, int32_t delta_x, int32_t delta_y);
Err_t CompHitTest(const Comp_t *p_comp, int32_t x, int32_t y, uint8_t *p_is_hit);
Err_t CompCheckOverlap(const CompDim_t *p_dims, uint32_t count, uint8_t *p_is_overlap);
Err_t CompSnapMagnetsTogether(CompDim_t *p_dims, uint32_t count, uint32_t threshold);

/* private functions ******************************************************************************
 **************************************************************************************************/

static int32_t AbsInt32(int32_t val);
static uint8_t IsPairOverlap(const CompDim_t *p_a, const CompDim_t *p_b);
static void    ResolveAllOverlaps(CompDim_t *p_dims, uint32_t count);

/* function implementations ***********************************************************************
 **************************************************************************************************/

/* Initialize a rectangle component with dimension, color, corner radius, and fill mode
 * @param p_comp[out] Pointer to component instance to initialize
 * @param label[in] Component label string
 * @param dim[in] Initial dimensions and offset coordinates
 * @param color[in] ARGB color value
 * @param radius[in] Corner radius in pixels
 * @param is_filled[in] Non-zero to fill the rectangle, 0 for outline only
 * @return APP_SUCCESS on success, or error code on invalid parameters
 * @note Clamps rotation check to COMP_MAX_ROTATION_DEG (360 degrees)
 */
Err_t CompInitRect(Comp_t *p_comp, const_str_t label, CompDim_t dim, Color_t color, uint32_t radius, uint8_t is_filled) {
    #if (1U == PARAM_CHECK_EN)
    /* Validate component pointer and rotation range */
    if (NULL == p_comp) {
        APP_ERR("p_comp is NULL!");
        return APP_ERROR_NULL;
    }
    if (dim.Rotation > COMP_MAX_ROTATION_DEG) {
        APP_ERR("Rotation exceeds 360 degrees!");
        return APP_ERROR_INVALID_PARAM;
    }
    #endif /* (1U == PARAM_CHECK_EN) */

    p_comp->Class                       = COMP_CLASS_CONTAINER;
    p_comp->Label                       = label;
    p_comp->CastType                    = COMP_CAST_RECT;
    p_comp->Type.Rect.Color             = color;
    p_comp->Type.Rect.Dim               = dim;
    p_comp->Type.Rect.Border.Size       = 0U;
    p_comp->Type.Rect.Border.Color.w    = 0U;
    p_comp->Type.Rect.Radius            = radius;
    p_comp->Type.Rect.IsFilled          = is_filled;
    p_comp->Listeners.Count             = 0U;
    p_comp->Listeners.p_Events          = NULL;
    p_comp->p_ExtComps                  = NULL;
    p_comp->IsDraggable                 = 0U;
    p_comp->IsVisible                   = 1U;

    /* Return success after initializing rect component */
    return APP_SUCCESS;
}

/* Initialize a button component with a click event callback
 * @param p_comp[out] Pointer to button component instance
 * @param label[in] Button text/label string (also rendered inside the button)
 * @param dim[in] Button dimensions and position
 * @param color[in] Button background ARGB color
 * @param p_event_slot[out] Caller-provided CompEvent_t slot to bind the click callback
 * @param p_on_click[in] Callback invoked when the button is clicked (EVENT_TYPE_MOUSE_DOWN)
 * @return APP_SUCCESS on success, or error code
 * @note Sets Class = COMP_CLASS_BUTTON so DrawComponent renders the button label automatically
 */
Err_t CompInitButton(Comp_t *p_comp, const_str_t label, CompDim_t dim, Color_t color, union CompEvent_un *p_event_slot, EventCallback_t p_on_click) {
    Err_t   err;
    Color_t border_col = { .w = 0xFFFFFFFFU };

    #if (1U == PARAM_CHECK_EN)
    /* Validate component pointer */
    if (NULL == p_comp) {
        APP_ERR("p_comp is NULL!");
        return APP_ERROR_NULL;
    }
    #endif /* (1U == PARAM_CHECK_EN) */

    /* Initialize underlying rounded rectangle for the button */
    err = CompInitRect(p_comp, label, dim, color, 6U, 1U);
    if (APP_SUCCESS != err) {
        APP_ERR("Failed to init button rect!");
        return err;
    }

    p_comp->Class = COMP_CLASS_BUTTON;
    /* Set default white border for button */
    (void)CompSetBorder(p_comp, 1U, border_col);

    /* Bind click callback if event slot and callback function are provided */
    if ((NULL != p_event_slot) && (NULL != p_on_click)) {
        p_event_slot->Listener.Type       = EVENT_TYPE_MOUSE_DOWN;
        p_event_slot->Listener.p_CallBack = p_on_click;
        p_comp->Listeners.Count           = 1U;
        p_comp->Listeners.p_Events        = p_event_slot;
    }

    /* Return success after initializing button component */
    return APP_SUCCESS;
}

/* Initialize a circle component with center/bounding dimensions, color, radius, and fill mode
 * @param p_comp[out] Pointer to component instance to initialize
 * @param label[in] Component label string
 * @param dim[in] Bounding box dimensions and top-left offset
 * @param color[in] ARGB color value
 * @param radius[in] Circle radius in pixels
 * @param is_filled[in] Non-zero to fill the circle, 0 for outline only
 * @return APP_SUCCESS on success, or error code
 * @note Circle center is drawn at (OffsetX + Radius, OffsetY + Radius)
 */
Err_t CompInitCircle(Comp_t *p_comp, const_str_t label, CompDim_t dim, Color_t color, uint32_t radius, uint8_t is_filled) {
    #if (1U == PARAM_CHECK_EN)
    /* Validate component pointer */
    if (NULL == p_comp) {
        APP_ERR("p_comp is NULL!");
        return APP_ERROR_NULL;
    }
    #endif /* (1U == PARAM_CHECK_EN) */

    p_comp->Class                         = COMP_CLASS_CUSTOM;
    p_comp->Label                         = label;
    p_comp->CastType                      = COMP_CAST_CIRCLE;
    p_comp->Type.Circle.Color             = color;
    p_comp->Type.Circle.Dim               = dim;
    p_comp->Type.Circle.Border.Size       = 0U;
    p_comp->Type.Circle.Border.Color.w    = 0U;
    p_comp->Type.Circle.Radius            = radius;
    p_comp->Type.Circle.IsFilled          = is_filled;
    p_comp->Listeners.Count               = 0U;
    p_comp->Listeners.p_Events            = NULL;
    p_comp->p_ExtComps                    = NULL;
    p_comp->IsDraggable                   = 0U;
    p_comp->IsVisible                     = 1U;

    /* Return success after initializing circle component */
    return APP_SUCCESS;
}

/* Initialize a line component between (x1, y1) and (x2, y2)
 * @param p_comp[out] Pointer to component instance to initialize
 * @param label[in] Component label string
 * @param x1[in] Start X coordinate
 * @param y1[in] Start Y coordinate
 * @param x2[in] End X coordinate
 * @param y2[in] End Y coordinate
 * @param color[in] ARGB color value
 * @param thickness[in] Line thickness in pixels
 * @return APP_SUCCESS on success, or error code
 * @note Stores start point in Dim.OffsetX/Y and end point in Line.EndX/Y
 */
Err_t CompInitLine(Comp_t *p_comp, const_str_t label, int32_t x1, int32_t y1, int32_t x2, int32_t y2, Color_t color, uint32_t thickness) {
    #if (1U == PARAM_CHECK_EN)
    /* Validate component pointer */
    if (NULL == p_comp) {
        APP_ERR("p_comp is NULL!");
        return APP_ERROR_NULL;
    }
    #endif /* (1U == PARAM_CHECK_EN) */

    p_comp->Class                  = COMP_CLASS_CUSTOM;
    p_comp->Label                  = label;
    p_comp->CastType               = COMP_CAST_LINE;
    p_comp->Type.Line.Color        = color;
    p_comp->Type.Line.Dim.OffsetX  = x1;
    p_comp->Type.Line.Dim.OffsetY  = y1;
    p_comp->Type.Line.Dim.W        = (x2 >= x1) ? (uint32_t)(x2 - x1) : (uint32_t)(x1 - x2);
    p_comp->Type.Line.Dim.H        = (y2 >= y1) ? (uint32_t)(y2 - y1) : (uint32_t)(y1 - y2);
    p_comp->Type.Line.Dim.Rotation = 0U;
    p_comp->Type.Line.EndX         = x2;
    p_comp->Type.Line.EndY         = y2;
    p_comp->Type.Line.Thickness    = (0U == thickness) ? 1U : thickness;
    p_comp->Listeners.Count        = 0U;
    p_comp->Listeners.p_Events     = NULL;
    p_comp->p_ExtComps             = NULL;
    p_comp->IsDraggable            = 0U;
    p_comp->IsVisible              = 1U;

    /* Return success after initializing line component */
    return APP_SUCCESS;
}

/* Initialize a text component at (x, y)
 * @param p_comp[out] Pointer to component instance to initialize
 * @param label[in] Component label string
 * @param x[in] Horizontal position
 * @param y[in] Vertical position
 * @param text_str[in] Null-terminated string to display
 * @param color[in] ARGB color value
 * @param font_size[in] Font height in pixels
 * @return APP_SUCCESS on success, or error code
 * @note Can be used standalone or inside p_ExtComps of a parent component
 */
Err_t CompInitText(Comp_t *p_comp, const_str_t label, int32_t x, int32_t y, const_str_t text_str, Color_t color, uint32_t font_size) {
    #if (1U == PARAM_CHECK_EN)
    /* Validate component and text string pointers */
    if ((NULL == p_comp) || (NULL == text_str)) {
        APP_ERR("Null pointer in CompInitText!");
        return APP_ERROR_NULL;
    }
    #endif /* (1U == PARAM_CHECK_EN) */

    p_comp->Class                  = COMP_CLASS_LABEL;
    p_comp->Label                  = label;
    p_comp->CastType               = COMP_CAST_TEXT;
    p_comp->Type.Text.Color        = color;
    p_comp->Type.Text.Dim.OffsetX  = x;
    p_comp->Type.Text.Dim.OffsetY  = y;
    p_comp->Type.Text.Dim.W        = 0U;
    p_comp->Type.Text.Dim.H        = font_size;
    p_comp->Type.Text.Dim.Rotation = 0U;
    p_comp->Type.Text.TextStr      = text_str;
    p_comp->Type.Text.FontSize     = font_size;
    p_comp->Type.Text.Align        = TEXT_ALIGN_LEFT;
    p_comp->Type.Text.VAlign       = TEXT_VALIGN_TOP;
    p_comp->Type.Text.IsWrap       = 0U;
    p_comp->Listeners.Count        = 0U;
    p_comp->Listeners.p_Events     = NULL;
    p_comp->p_ExtComps             = NULL;
    p_comp->IsDraggable            = 0U;
    p_comp->IsVisible              = 1U;

    /* Return success after initializing text component */
    return APP_SUCCESS;
}

/* Configure horizontal alignment, vertical alignment, and word wrapping for a text configuration
 * @param p_text[in/out] Pointer to text configuration structure
 * @param align[in] Horizontal alignment (TEXT_ALIGN_LEFT, TEXT_ALIGN_CENTER, TEXT_ALIGN_RIGHT)
 * @param valign[in] Vertical alignment (TEXT_VALIGN_TOP, TEXT_VALIGN_MIDDLE, TEXT_VALIGN_BOTTOM)
 * @param is_wrap[in] Non-zero to enable automatic word wrapping within p_text->Dim.W
 * @return APP_SUCCESS on success, or error code
 * @note Works for both standalone COMP_CAST_TEXT components and child CompTextCfg_t in p_ExtComps
 */
Err_t CompSetTextAlign(CompTextCfg_t *p_text, TextAlign_t align, TextVAlign_t valign, uint8_t is_wrap) {
    #if (1U == PARAM_CHECK_EN)
    /* Validate text config pointer and alignment enum ranges */
    if (NULL == p_text) {
        APP_ERR("p_text is NULL!");
        return APP_ERROR_NULL;
    }
    if ((align > TEXT_ALIGN_RIGHT) || (valign > TEXT_VALIGN_BOTTOM)) {
        APP_ERR("Invalid text alignment enum value!");
        return APP_ERROR_INVALID_PARAM;
    }
    #endif /* (1U == PARAM_CHECK_EN) */

    p_text->Align  = align;
    p_text->VAlign = valign;
    p_text->IsWrap = is_wrap;

    /* Return success after updating text alignment and wrap settings */
    return APP_SUCCESS;
}

/* Initialize a scrollable table component
 * @param p_comp[out] Pointer to component instance to initialize
 * @param label[in] Component label string
 * @param dim[in] Table viewport dimensions and position
 * @param color[in] Background ARGB color
 * @param rows[in] Number of rows in the table
 * @param cols[in] Number of columns in the table
 * @param is_scrollable[in] Non-zero to enable scrolling
 * @return APP_SUCCESS on success, or error code
 * @note Scroll offsets default to (0, 0)
 */
Err_t CompInitTable(Comp_t *p_comp, const_str_t label, CompDim_t dim, Color_t color, uint32_t rows, uint32_t cols, uint8_t is_scrollable) {
    #if (1U == PARAM_CHECK_EN)
    /* Validate component pointer and non-zero rows/cols */
    if (NULL == p_comp) {
        APP_ERR("p_comp is NULL!");
        return APP_ERROR_NULL;
    }
    if ((0U == rows) || (0U == cols)) {
        APP_ERR("Table rows and cols must be non-zero!");
        return APP_ERROR_INVALID_PARAM;
    }
    #endif /* (1U == PARAM_CHECK_EN) */

    p_comp->Class                        = COMP_CLASS_TABLE;
    p_comp->Label                        = label;
    p_comp->CastType                     = COMP_CAST_SCROLL_TABLE;
    p_comp->Type.Table.Color             = color;
    p_comp->Type.Table.Dim               = dim;
    p_comp->Type.Table.Border.Size       = 1U;
    p_comp->Type.Table.Border.Color.w    = 0xFFFFFFFFU;
    p_comp->Type.Table.Rows              = rows;
    p_comp->Type.Table.Cols              = cols;
    p_comp->Type.Table.ScrollX           = 0;
    p_comp->Type.Table.ScrollY           = 0;
    p_comp->Type.Table.IsScrollable      = is_scrollable;
    p_comp->Listeners.Count              = 0U;
    p_comp->Listeners.p_Events           = NULL;
    p_comp->p_ExtComps                   = NULL;
    p_comp->IsDraggable                  = 0U;
    p_comp->IsVisible                    = 1U;

    /* Return success after initializing table component */
    return APP_SUCCESS;
}

/* Configure border size and color for a rectangle, circle, or table component
 * @param p_comp[in/out] Pointer to target component
 * @param border_size[in] Border thickness in pixels
 * @param border_color[in] Border ARGB color
 * @return APP_SUCCESS on success, or error status
 * @note Applies to COMP_CAST_RECT, COMP_CAST_CIRCLE, and COMP_CAST_SCROLL_TABLE
 */
Err_t CompSetBorder(Comp_t *p_comp, uint32_t border_size, Color_t border_color) {
    #if (1U == PARAM_CHECK_EN)
    /* Check if component pointer is valid */
    if (NULL == p_comp) {
        APP_ERR("p_comp is NULL!");
        return APP_ERROR_NULL;
    }
    #endif /* (1U == PARAM_CHECK_EN) */

    /* Update border fields according to component cast type */
    switch (p_comp->CastType) {
        case COMP_CAST_RECT:
            p_comp->Type.Rect.Border.Size  = border_size;
            p_comp->Type.Rect.Border.Color = border_color;
            /* Break after updating rect border */
            break;
        case COMP_CAST_CIRCLE:
            p_comp->Type.Circle.Border.Size  = border_size;
            p_comp->Type.Circle.Border.Color = border_color;
            /* Break after updating circle border */
            break;
        case COMP_CAST_SCROLL_TABLE:
            p_comp->Type.Table.Border.Size  = border_size;
            p_comp->Type.Table.Border.Color = border_color;
            /* Break after updating table border */
            break;
        default:
            APP_ERR("Component type does not support border!");
            return APP_ERROR_INVALID_PARAM;
    }

    /* Return success after setting border */
    return APP_SUCCESS;
}

/* Update the X/Y offset position of a component (used by layout and drag-and-drop)
 * @param p_comp[in/out] Pointer to target component
 * @param offset_x[in] New horizontal offset (row/x)
 * @param offset_y[in] New vertical offset (col/y)
 * @return APP_SUCCESS on success, or APP_ERROR_NULL
 * @note Updates child text positions proportionally when p_ExtComps are present
 */
Err_t CompSetPosition(Comp_t *p_comp, int32_t offset_x, int32_t offset_y) {
    int32_t  dx;
    int32_t  dy;
    uint32_t idx;

    #if (1U == PARAM_CHECK_EN)
    /* Validate target component pointer */
    if (NULL == p_comp) {
        APP_ERR("p_comp is NULL!");
        return APP_ERROR_NULL;
    }
    #endif /* (1U == PARAM_CHECK_EN) */

    dx = offset_x - p_comp->Type.Rect.Dim.OffsetX;
    dy = offset_y - p_comp->Type.Rect.Dim.OffsetY;

    p_comp->Type.Rect.Dim.OffsetX = offset_x;
    p_comp->Type.Rect.Dim.OffsetY = offset_y;

    /* Move attached child components along with parent component */
    if ((NULL != p_comp->p_ExtComps) && (NULL != p_comp->p_ExtComps->p_ExtComps)) {
        /* Iterate through child extension items to apply delta offset */
        for (idx = 0U; idx < p_comp->p_ExtComps->Count; idx++) {
            ExtComp_t *p_ext = &p_comp->p_ExtComps->p_ExtComps[idx];

            /* Check child cast type to shift its coordinates */
            if ((COMP_CAST_TEXT == p_ext->CastType) && (NULL != p_ext->CompPtr.p_Text)) {
                p_ext->CompPtr.p_Text->Dim.OffsetX += dx;
                p_ext->CompPtr.p_Text->Dim.OffsetY += dy;
            } else if ((COMP_CAST_RECT == p_ext->CastType) && (NULL != p_ext->CompPtr.p_Rect)) {
                p_ext->CompPtr.p_Rect->Dim.OffsetX += dx;
                p_ext->CompPtr.p_Rect->Dim.OffsetY += dy;
            } else if (NULL != p_ext->CompPtr.p_Comp) {
                /* Recursively shift nested Comp_t child */
                (void)CompSetPosition(p_ext->CompPtr.p_Comp,
                                      p_ext->CompPtr.p_Comp->Type.Rect.Dim.OffsetX + dx,
                                      p_ext->CompPtr.p_Comp->Type.Rect.Dim.OffsetY + dy);
            } else {
                /* Nothing to update for null child pointer */
            }
        }
    }

    /* Return success after updating position */
    return APP_SUCCESS;
}

/* Adjust scroll offsets for a scrollable table component
 * @param p_comp[in/out] Pointer to scrollable table component
 * @param delta_x[in] Horizontal scroll delta
 * @param delta_y[in] Vertical scroll delta
 * @return APP_SUCCESS on success, or error code if not a scrollable table
 * @note Only valid when CastType is COMP_CAST_SCROLL_TABLE and IsScrollable is set
 */
Err_t CompScrollTable(Comp_t *p_comp, int32_t delta_x, int32_t delta_y) {
    #if (1U == PARAM_CHECK_EN)
    /* Validate component pointer and scrollable table type */
    if (NULL == p_comp) {
        APP_ERR("p_comp is NULL!");
        return APP_ERROR_NULL;
    }
    if ((COMP_CAST_SCROLL_TABLE != p_comp->CastType) || (0U == p_comp->Type.Table.IsScrollable)) {
        APP_ERR("Component is not a scrollable table!");
        return APP_ERROR_INVALID_PARAM;
    }
    #endif /* (1U == PARAM_CHECK_EN) */

    p_comp->Type.Table.ScrollX += delta_x;
    p_comp->Type.Table.ScrollY += delta_y;

    /* Return success after updating scroll offsets */
    return APP_SUCCESS;
}

/* Configure a layer as scrollable within a given viewport rectangle
 * @param p_layer[in/out] Pointer to target layer
 * @param rect_clip[in] Clipping viewport dimensions and rotation (0-360 deg) for the layer
 * @param is_scrollable[in] Non-zero to enable layer scrolling
 * @return APP_SUCCESS on success, or error code
 * @note Allows an entire layer (with attached table/components) to scroll and rotate together
 */
Err_t LayerSetScrollable(Layer_t *p_layer, CompDim_t rect_clip, uint8_t is_scrollable) {
    #if (1U == PARAM_CHECK_EN)
    /* Validate layer pointer and rotation range (0 to 360 degrees) */
    if (NULL == p_layer) {
        APP_ERR("p_layer is NULL!");
        return APP_ERROR_NULL;
    }
    if (rect_clip.Rotation > COMP_MAX_ROTATION_DEG) {
        APP_ERR("Layer RectClip rotation exceeds 360 degrees!");
        return APP_ERROR_INVALID_PARAM;
    }
    #endif /* (1U == PARAM_CHECK_EN) */

    p_layer->RectClip     = rect_clip;
    p_layer->IsScrollable = is_scrollable;
    p_layer->ScrollX      = 0;
    p_layer->ScrollY      = 0;

    /* Return success after configuring scrollable layer RectClip */
    return APP_SUCCESS;
}

/* Scroll a scrollable layer by (delta_x, delta_y) without mutating local component (0,0) offsets
 * @param p_layer[in/out] Pointer to scrollable layer
 * @param delta_x[in] Horizontal scroll step in pixels
 * @param delta_y[in] Vertical scroll step in pixels
 * @return APP_SUCCESS on success, or error code
 * @note Components remain anchored to Layer (0,0); DrawLayer applies RectClip + ScrollX/Y
 */
Err_t LayerScroll(Layer_t *p_layer, int32_t delta_x, int32_t delta_y) {
    #if (1U == PARAM_CHECK_EN)
    /* Validate layer pointer and scrollable flag */
    if (NULL == p_layer) {
        APP_ERR("p_layer is NULL!");
        return APP_ERROR_NULL;
    }
    if (0U == p_layer->IsScrollable) {
        APP_ERR("Layer is not scrollable!");
        return APP_ERROR_INVALID_PARAM;
    }
    #endif /* (1U == PARAM_CHECK_EN) */

    p_layer->ScrollX += delta_x;
    p_layer->ScrollY += delta_y;

    /* Clamp ScrollX and ScrollY so layer origin (0,0) does not scroll past positive top-left */
    if (p_layer->ScrollX > 0) {
        p_layer->ScrollX = 0;
    }
    if (p_layer->ScrollY > 0) {
        p_layer->ScrollY = 0;
    }

    /* Return success after updating layer scroll offsets */
    return APP_SUCCESS;
}

/* Check whether a 2D point (x, y) lies inside the (optionally rotated) bounding box of a component
 * @param p_comp[in] Pointer to component to test
 * @param x[in] X coordinate to test in Layer (0,0) coordinates
 * @param y[in] Y coordinate to test in Layer (0,0) coordinates
 * @param p_is_hit[out] Outputs 1U if point is inside component, 0U otherwise
 * @return APP_SUCCESS on success, or APP_ERROR_NULL
 * @note Supports component rotation (0-360 degrees) by inverse-rotating (x, y) around component center
 */
Err_t CompHitTest(const Comp_t *p_comp, int32_t x, int32_t y, uint8_t *p_is_hit) {
    int32_t  comp_x;
    int32_t  comp_y;
    int32_t  comp_w;
    int32_t  comp_h;
    int32_t  test_x;
    int32_t  test_y;
    uint16_t rot_deg;

    #if (1U == PARAM_CHECK_EN)
    /* Validate input pointers */
    if ((NULL == p_comp) || (NULL == p_is_hit)) {
        APP_ERR("Null pointer in CompHitTest!");
        return APP_ERROR_NULL;
    }
    #endif /* (1U == PARAM_CHECK_EN) */

    /* Invisible components cannot be hit */
    if (0U == p_comp->IsVisible) {
        *p_is_hit = 0U;
        /* Return early when component is hidden */
        return APP_SUCCESS;
    }

    comp_x  = p_comp->Type.Rect.Dim.OffsetX;
    comp_y  = p_comp->Type.Rect.Dim.OffsetY;
    comp_w  = (int32_t)p_comp->Type.Rect.Dim.W;
    comp_h  = (int32_t)p_comp->Type.Rect.Dim.H;
    test_x  = x;
    test_y  = y;
    rot_deg = (uint16_t)(p_comp->Type.Rect.Dim.Rotation % 360U);

    /* Inverse-rotate test coordinates around component center if Rotation is non-zero */
    if (0U != rot_deg) {
        double cx    = (double)comp_x + ((double)comp_w * 0.5);
        double cy    = (double)comp_y + ((double)comp_h * 0.5);
        double dx    = (double)x - cx;
        double dy    = (double)y - cy;
        double rad   = (double)rot_deg * (M_PI / 180.0);
        double cos_a = SDL_cos(rad);
        double sin_a = SDL_sin(rad);

        test_x = (int32_t)(cx + (dx * cos_a) + (dy * sin_a));
        test_y = (int32_t)(cy - (dx * sin_a) + (dy * cos_a));
    }

    /* Check if unrotated point falls within [comp_x, comp_x + comp_w) and [comp_y, comp_y + comp_h) */
    if ((test_x >= comp_x) && (test_x < (comp_x + comp_w)) &&
        (test_y >= comp_y) && (test_y < (comp_y + comp_h))) {
        *p_is_hit = 1U;
    } else {
        *p_is_hit = 0U;
    }

    /* Return hit-test completion status */
    return APP_SUCCESS;
}

/* Compute absolute value of a signed 32-bit integer
 * @param val[in] Signed 32-bit value
 * @return Non-negative absolute value
 */
static int32_t AbsInt32(int32_t val) {
    /* Negate value if negative */
    if (val < 0) {
        /* Return negated positive value */
        return -val;
    }
    /* Return non-negative value directly */
    return val;
}

/* Check whether two rectangular CompDim_t bounds strictly overlap in interior area
 * @param p_a[in] First rectangle dimension
 * @param p_b[in] Second rectangle dimension
 * @return 1U if rectangles strictly overlap, 0U if separated or touching edge-to-edge
 */
static uint8_t IsPairOverlap(const CompDim_t *p_a, const CompDim_t *p_b) {
    /* Check intersection along both X and Y axes */
    if ((p_a->OffsetX < (p_b->OffsetX + (int32_t)p_b->W)) &&
        ((p_a->OffsetX + (int32_t)p_a->W) > p_b->OffsetX) &&
        (p_a->OffsetY < (p_b->OffsetY + (int32_t)p_b->H)) &&
        ((p_a->OffsetY + (int32_t)p_a->H) > p_b->OffsetY)) {
        /* Return 1U when interior areas intersect */
        return 1U;
    }
    /* Return 0U when no overlap exists */
    return 0U;
}

/* Push overlapping rectangles apart along their minimum penetration axis until no pairs overlap
 * @param p_dims[in/out] Array of component dimensions to separate
 * @param count[in] Number of elements in p_dims
 */
static void ResolveAllOverlaps(CompDim_t *p_dims, uint32_t count) {
    uint32_t pass;
    uint32_t i;
    uint32_t j;

    /* Run up to 4 relaxation passes to resolve multi-card chain overlaps */
    for (pass = 0U; pass < 4U; pass++) {
        uint8_t any_resolved = 0U;

        /* Iterate over all first cards i */
        for (i = 0U; i < (count - 1U); i++) {
            /* Iterate over all second cards j > i */
            for (j = i + 1U; j < count; j++) {
                /* Check if pair (i, j) currently overlaps */
                if (0U != IsPairOverlap(&p_dims[i], &p_dims[j])) {
                    int32_t ax1 = p_dims[i].OffsetX;
                    int32_t ay1 = p_dims[i].OffsetY;
                    int32_t ax2 = ax1 + (int32_t)p_dims[i].W;
                    int32_t ay2 = ay1 + (int32_t)p_dims[i].H;
                    int32_t bx1 = p_dims[j].OffsetX;
                    int32_t by1 = p_dims[j].OffsetY;
                    int32_t bx2 = bx1 + (int32_t)p_dims[j].W;
                    int32_t by2 = by1 + (int32_t)p_dims[j].H;

                    int32_t ov_x = ((ax2 < bx2) ? ax2 : bx2) - ((ax1 > bx1) ? ax1 : bx1);
                    int32_t ov_y = ((ay2 < by2) ? ay2 : by2) - ((ay1 > by1) ? ay1 : by1);
                    int32_t acx  = ax1 + ((int32_t)p_dims[i].W / 2);
                    int32_t acy  = ay1 + ((int32_t)p_dims[i].H / 2);
                    int32_t bcx  = bx1 + ((int32_t)p_dims[j].W / 2);
                    int32_t bcy  = by1 + ((int32_t)p_dims[j].H / 2);

                    /* Push card j out along the axis of minimum penetration */
                    if (ov_x <= ov_y) {
                        /* Separate horizontally to left or right edge of card i */
                        if (bcx >= acx) {
                            p_dims[j].OffsetX = ax2;
                        } else {
                            p_dims[j].OffsetX = ax1 - (int32_t)p_dims[j].W;
                        }
                    } else {
                        /* Separate vertically to top or bottom edge of card i */
                        if (bcy >= acy) {
                            p_dims[j].OffsetY = ay2;
                        } else {
                            p_dims[j].OffsetY = ay1 - (int32_t)p_dims[j].H;
                        }
                    }
                    any_resolved = 1U;
                }
            }
        }

        /* Stop early if no overlaps remained in this pass */
        if (0U == any_resolved) {
            /* Break out of relaxation loop */
            break;
        }
    }

    /* Return after resolving all overlaps */
    return;
}

/* Check if any two component dimensions in an array overlap
 * @param p_dims[in] Array of component dimensions
 * @param count[in] Number of elements in p_dims
 * @param p_is_overlap[out] Outputs 1U if at least one pair overlaps, 0U otherwise
 * @return APP_SUCCESS on success, or APP_ERROR_NULL on NULL pointers
 * @note Uses brute-force pairwise intersection check
 */
Err_t CompCheckOverlap(const CompDim_t *p_dims, uint32_t count, uint8_t *p_is_overlap) {
    uint32_t i;
    uint32_t j;

    #if (1U == PARAM_CHECK_EN)
    /* Validate input pointers */
    if ((NULL == p_dims) || (NULL == p_is_overlap)) {
        APP_ERR("Null pointer in CompCheckOverlap!");
        return APP_ERROR_NULL;
    }
    #endif /* (1U == PARAM_CHECK_EN) */

    *p_is_overlap = 0U;

    /* Check if array has fewer than 2 items */
    if (count < 2U) {
        /* Cannot overlap with fewer than 2 items */
        return APP_SUCCESS;
    }

    /* Iterate through all distinct pairs (i, j) */
    for (i = 0U; i < (count - 1U); i++) {
        /* Compare card i against every subsequent card j */
        for (j = i + 1U; j < count; j++) {
            /* Check if pair (i, j) intersects */
            if (0U != IsPairOverlap(&p_dims[i], &p_dims[j])) {
                *p_is_overlap = 1U;
                /* Return immediately on first detected overlap */
                return APP_SUCCESS;
            }
        }
    }

    /* Return success with no overlap found */
    return APP_SUCCESS;
}

/* Resolve overlaps and magnetically snap edges or vertices (corners) within threshold distance
 * @param p_dims[in/out] Array of component dimensions to snap and separate
 * @param count[in] Number of elements in p_dims
 * @param threshold[in] Maximum pixel distance for magnetic edge and corner snapping
 * @return APP_SUCCESS on success, or APP_ERROR_NULL if p_dims is NULL
 * @note Enforces 3 rules: (1) No overlap, (2) Corner-to-corner snap, (3) Edge-to-edge snap
 */
Err_t CompSnapMagnetsTogether(CompDim_t *p_dims, uint32_t count, uint32_t threshold) {
    uint32_t i;
    uint32_t j;
    int32_t  thresh;

    #if (1U == PARAM_CHECK_EN)
    /* Validate dimension array pointer */
    if (NULL == p_dims) {
        APP_ERR("p_dims is NULL in CompSnapMagnetsTogether!");
        return APP_ERROR_NULL;
    }
    #endif /* (1U == PARAM_CHECK_EN) */

    /* Return early if fewer than 2 components are provided */
    if (count < 2U) {
        /* Nothing to snap when count < 2 */
        return APP_SUCCESS;
    }

    thresh = (int32_t)threshold;

    /* Step 1: Separate any overlapping pairs along minimum penetration axis */
    ResolveAllOverlaps(p_dims, count);

    /* Step 2: Apply magnetic corner (vertex) snapping and edge snapping for each pair */
    for (i = 0U; i < (count - 1U); i++) {
        /* Check card i against every subsequent card j */
        for (j = i + 1U; j < count; j++) {
            int32_t ax1 = p_dims[i].OffsetX;
            int32_t ay1 = p_dims[i].OffsetY;
            int32_t ax2 = ax1 + (int32_t)p_dims[i].W;
            int32_t ay2 = ay1 + (int32_t)p_dims[i].H;

            int32_t bx1 = p_dims[j].OffsetX;
            int32_t by1 = p_dims[j].OffsetY;
            int32_t bx2 = bx1 + (int32_t)p_dims[j].W;
            int32_t by2 = by1 + (int32_t)p_dims[j].H;

            /* 4 corners (vertices) of card i and card j: TL, TR, BL, BR */
            int32_t  ax_v[4];
            int32_t  ay_v[4];
            int32_t  bx_v[4];
            int32_t  by_v[4];
            uint32_t ca;
            uint32_t cb;
            int32_t  best_dist      = (thresh * 2) + 1;
            int32_t  best_dx        = 0;
            int32_t  best_dy        = 0;
            uint8_t  corner_snapped = 0U;

            ax_v[0] = ax1; ay_v[0] = ay1;
            ax_v[1] = ax2; ay_v[1] = ay1;
            ax_v[2] = ax1; ay_v[2] = ay2;
            ax_v[3] = ax2; ay_v[3] = ay2;

            bx_v[0] = bx1; by_v[0] = by1;
            bx_v[1] = bx2; by_v[1] = by1;
            bx_v[2] = bx1; by_v[2] = by2;
            bx_v[3] = bx2; by_v[3] = by2;

            /* Step 2A: Check all 16 vertex (corner) pairs between card i and card j */
            for (ca = 0U; ca < 4U; ca++) {
                /* Compare corner ca of card i with corner cb of card j */
                for (cb = 0U; cb < 4U; cb++) {
                    int32_t dx   = ax_v[ca] - bx_v[cb];
                    int32_t dy   = ay_v[ca] - by_v[cb];
                    int32_t adx  = AbsInt32(dx);
                    int32_t ady  = AbsInt32(dy);
                    int32_t dist = adx + ady;

                    /* Check if both X and Y corner distances are within threshold */
                    if ((adx <= thresh) && (ady <= thresh) && (dist < best_dist)) {
                        CompDim_t cand = p_dims[j];
                        cand.OffsetX  += dx;
                        cand.OffsetY  += dy;

                        /* Only accept corner snap if it does not cause card j to overlap card i */
                        if (0U == IsPairOverlap(&p_dims[i], &cand)) {
                            best_dist      = dist;
                            best_dx        = dx;
                            best_dy        = dy;
                            corner_snapped = 1U;
                        }
                    }
                }
            }

            /* Apply vertex snap if a valid non-overlapping corner pair was within threshold */
            if (0U != corner_snapped) {
                p_dims[j].OffsetX += best_dx;
                p_dims[j].OffsetY += best_dy;
                /* Continue to next pair after snapping corners */
                continue;
            }

            /* Step 2B: Edge-to-edge magnetic snapping when corners are further than threshold */
            /* Check horizontal edge snap (Left of j to Right of i, or Right of j to Left of i) */
            if (((ay1 - thresh) <= by2) && ((by1 - thresh) <= ay2)) {
                /* Check if Left edge of j is near Right edge of i */
                if (AbsInt32(bx1 - ax2) <= thresh) {
                    p_dims[j].OffsetX = ax2;
                    /* Also snap top or bottom alignment if within threshold */
                    if (AbsInt32(by1 - ay1) <= thresh) {
                        p_dims[j].OffsetY = ay1;
                    } else if (AbsInt32(by2 - ay2) <= thresh) {
                        p_dims[j].OffsetY = ay2 - (int32_t)p_dims[j].H;
                    } else {
                        /* Keep current vertical offset along edge */
                    }
                } else if (AbsInt32(bx2 - ax1) <= thresh) {
                    /* Snap Right edge of j to Left edge of i */
                    p_dims[j].OffsetX = ax1 - (int32_t)p_dims[j].W;
                    /* Also snap top or bottom alignment if within threshold */
                    if (AbsInt32(by1 - ay1) <= thresh) {
                        p_dims[j].OffsetY = ay1;
                    } else if (AbsInt32(by2 - ay2) <= thresh) {
                        p_dims[j].OffsetY = ay2 - (int32_t)p_dims[j].H;
                    } else {
                        /* Keep current vertical offset along edge */
                    }
                } else {
                    /* Horizontal edges are beyond threshold */
                }
            }

            /* Refresh bx/by after potential horizontal edge snap */
            bx1 = p_dims[j].OffsetX;
            by1 = p_dims[j].OffsetY;
            bx2 = bx1 + (int32_t)p_dims[j].W;
            by2 = by1 + (int32_t)p_dims[j].H;

            /* Check vertical edge snap (Top of j to Bottom of i, or Bottom of j to Top of i) */
            if (((ax1 - thresh) <= bx2) && ((bx1 - thresh) <= ax2)) {
                /* Check if Top edge of j is near Bottom edge of i */
                if (AbsInt32(by1 - ay2) <= thresh) {
                    p_dims[j].OffsetY = ay2;
                    /* Also snap left or right alignment if within threshold */
                    if (AbsInt32(bx1 - ax1) <= thresh) {
                        p_dims[j].OffsetX = ax1;
                    } else if (AbsInt32(bx2 - ax2) <= thresh) {
                        p_dims[j].OffsetX = ax2 - (int32_t)p_dims[j].W;
                    } else {
                        /* Keep current horizontal offset along edge */
                    }
                } else if (AbsInt32(by2 - ay1) <= thresh) {
                    /* Snap Bottom edge of j to Top edge of i */
                    p_dims[j].OffsetY = ay1 - (int32_t)p_dims[j].H;
                    /* Also snap left or right alignment if within threshold */
                    if (AbsInt32(bx1 - ax1) <= thresh) {
                        p_dims[j].OffsetX = ax1;
                    } else if (AbsInt32(bx2 - ax2) <= thresh) {
                        p_dims[j].OffsetX = ax2 - (int32_t)p_dims[j].W;
                    } else {
                        /* Keep current horizontal offset along edge */
                    }
                } else {
                    /* Vertical edges are beyond threshold */
                }
            }
        }
    }

    /* Step 3: Final overlap resolution pass in case snapping pushed a card into a 3rd card */
    ResolveAllOverlaps(p_dims, count);

    /* Return success after snapping and resolving all cards */
    return APP_SUCCESS;
}

/* reserved ***************************************************************************************
 **************************************************************************************************/
