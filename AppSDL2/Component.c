/* file-desc **************************************************************************************
 * @filename    Component.c
 * @brief       Component initialization, layout manipulation, and hit-testing implementation.
 * @note        Provides base operations for Rect, Circle, Line, Text, ScrollTable, and Comp_t.
 **************************************************************************************************/

/* headers ****************************************************************************************
 **************************************************************************************************/

#include "./__common__.h"
#include "./Component.h"

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
Err_t CompInitCircle(Comp_t *p_comp, const_str_t label, CompDim_t dim, Color_t color, uint32_t radius, uint8_t is_filled);
Err_t CompInitLine(Comp_t *p_comp, const_str_t label, int32_t x1, int32_t y1, int32_t x2, int32_t y2, Color_t color, uint32_t thickness);
Err_t CompInitText(Comp_t *p_comp, const_str_t label, int32_t x, int32_t y, const_str_t text_str, Color_t color, uint32_t font_size);
Err_t CompInitTable(Comp_t *p_comp, const_str_t label, CompDim_t dim, Color_t color, uint32_t rows, uint32_t cols, uint8_t is_scrollable);
Err_t CompSetBorder(Comp_t *p_comp, uint32_t border_size, Color_t border_color);
Err_t CompSetPosition(Comp_t *p_comp, int32_t offset_x, int32_t offset_y);
Err_t CompScrollTable(Comp_t *p_comp, int32_t delta_x, int32_t delta_y);
Err_t CompHitTest(const Comp_t *p_comp, int32_t x, int32_t y, uint8_t *p_is_hit);

/* private functions ******************************************************************************
 **************************************************************************************************/

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
 * @note Clamps rotation check to COMP_MAX_ROTATION_DEG (180 degrees)
 */
Err_t CompInitRect(Comp_t *p_comp, const_str_t label, CompDim_t dim, Color_t color, uint32_t radius, uint8_t is_filled) {
    #if (1U == PARAM_CHECK_EN)
    /* Validate component pointer and rotation range */
    if (NULL == p_comp) {
        APP_ERR("p_comp is NULL!");
        return APP_ERROR_NULL;
    }
    if (dim.Rotation > COMP_MAX_ROTATION_DEG) {
        APP_ERR("Rotation exceeds 180 degrees!");
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
    p_comp->Type.Text.Dim.W        = 120U;
    p_comp->Type.Text.Dim.H        = font_size;
    p_comp->Type.Text.Dim.Rotation = 0U;
    p_comp->Type.Text.TextStr      = text_str;
    p_comp->Type.Text.FontSize     = font_size;
    p_comp->Listeners.Count        = 0U;
    p_comp->Listeners.p_Events     = NULL;
    p_comp->p_ExtComps             = NULL;
    p_comp->IsDraggable            = 0U;
    p_comp->IsVisible              = 1U;

    /* Return success after initializing text component */
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

/* Check whether a 2D point (x, y) lies inside the bounding box of a component
 * @param p_comp[in] Pointer to component to test
 * @param x[in] X coordinate to test
 * @param y[in] Y coordinate to test
 * @param p_is_hit[out] Outputs 1U if point is inside component, 0U otherwise
 * @return APP_SUCCESS on success, or APP_ERROR_NULL
 * @note Used by mouse event dispatching and drag-and-drop hit detection
 */
Err_t CompHitTest(const Comp_t *p_comp, int32_t x, int32_t y, uint8_t *p_is_hit) {
    int32_t comp_x;
    int32_t comp_y;
    int32_t comp_w;
    int32_t comp_h;

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

    comp_x = p_comp->Type.Rect.Dim.OffsetX;
    comp_y = p_comp->Type.Rect.Dim.OffsetY;
    comp_w = (int32_t)p_comp->Type.Rect.Dim.W;
    comp_h = (int32_t)p_comp->Type.Rect.Dim.H;

    /* Check if point (x, y) falls within [comp_x, comp_x + comp_w) and [comp_y, comp_y + comp_h) */
    if ((x >= comp_x) && (x < (comp_x + comp_w)) &&
        (y >= comp_y) && (y < (comp_y + comp_h))) {
        *p_is_hit = 1U;
    } else {
        *p_is_hit = 0U;
    }

    /* Return hit-test completion status */
    return APP_SUCCESS;
}

/* reserved ***************************************************************************************
 **************************************************************************************************/
