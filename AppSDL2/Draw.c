/* file-desc **************************************************************************************
 * @filename    Draw.c
 * @brief       Implementation of SDL2 drawing primitives and recursive component rendering.
 * @note        Components are drawn relative to Layer (0,0); Layer RectClip is in MainWindow (0,0).
 **************************************************************************************************/

/* headers ****************************************************************************************
 **************************************************************************************************/

#include "./__common__.h"
#include "./Draw.h"
#include "./Window.h"

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

/* Active layer origin offset in MainWindow coordinates (RectClip.Offset + Layer.Scroll) */
static int32_t g_LayerOriginX = 0;
static int32_t g_LayerOriginY = 0;

/* exported functions *****************************************************************************
 **************************************************************************************************/

/* public functions *******************************************************************************
 **************************************************************************************************/

Err_t DrawRect(const CompRectCfg_t *p_rect);
Err_t DrawCircle(const CompCircleCfg_t *p_circle);
Err_t DrawLine(const CompLineCfg_t *p_line);
Err_t DrawText(const CompTextCfg_t *p_text);
Err_t DrawScrollTable(const CompTableCfg_t *p_table);
Err_t DrawComponent(const Comp_t *p_comp);
Err_t DrawLayer(const Layer_t *p_layer);

/* private functions ******************************************************************************
 **************************************************************************************************/

/* function implementations ***********************************************************************
 **************************************************************************************************/

/* Draw a rectangle with optional corner radius, fill, and border
 * @param p_rect[in] Pointer to rectangle configuration (coordinates relative to Layer (0,0))
 * @return APP_SUCCESS on success, or error code
 * @note Color format is ARGB (AARRGGBB)
 */
Err_t DrawRect(const CompRectCfg_t *p_rect) {
    SDL_Renderer *p_ren;
    int16_t       x1;
    int16_t       y1;
    int16_t       x2;
    int16_t       y2;
    int16_t       rad;

    #if (1U == PARAM_CHECK_EN)
    /* Validate rectangle configuration pointer */
    if (NULL == p_rect) {
        APP_ERR("p_rect is NULL!");
        return APP_ERROR_NULL;
    }
    #endif /* (1U == PARAM_CHECK_EN) */

    p_ren = (SDL_Renderer *)g_MainWindow.p_SdlRenderer;
    /* Check if SDL renderer is active */
    if (NULL == p_ren) {
        /* Return early when running headless or before renderer init */
        return APP_SUCCESS;
    }

    x1  = (int16_t)(g_LayerOriginX + p_rect->Dim.OffsetX);
    y1  = (int16_t)(g_LayerOriginY + p_rect->Dim.OffsetY);
    x2  = (int16_t)(g_LayerOriginX + p_rect->Dim.OffsetX + (int32_t)p_rect->Dim.W);
    y2  = (int16_t)(g_LayerOriginY + p_rect->Dim.OffsetY + (int32_t)p_rect->Dim.H);
    rad = (int16_t)p_rect->Radius;

    /* Check whether rectangle should be filled or outlined */
    if (0U != p_rect->IsFilled) {
        /* Check if corner radius is applied */
        if (rad > 0) {
            /* Draw filled rounded rectangle */
            (void)roundedBoxRGBA(p_ren, x1, y1, x2, y2, rad,
                                 p_rect->Color.b.r, p_rect->Color.b.g,
                                 p_rect->Color.b.b, p_rect->Color.b.a);
        } else {
            /* Draw filled sharp rectangle */
            (void)boxRGBA(p_ren, x1, y1, x2, y2,
                          p_rect->Color.b.r, p_rect->Color.b.g,
                          p_rect->Color.b.b, p_rect->Color.b.a);
        }
    } else {
        /* Check if corner radius is applied for outline */
        if (rad > 0) {
            /* Draw outlined rounded rectangle */
            (void)roundedRectangleRGBA(p_ren, x1, y1, x2, y2, rad,
                                       p_rect->Color.b.r, p_rect->Color.b.g,
                                       p_rect->Color.b.b, p_rect->Color.b.a);
        } else {
            /* Draw outlined sharp rectangle */
            (void)rectangleRGBA(p_ren, x1, y1, x2, y2,
                                p_rect->Color.b.r, p_rect->Color.b.g,
                                p_rect->Color.b.b, p_rect->Color.b.a);
        }
    }

    /* Draw border outline if border size is greater than zero */
    if (p_rect->Border.Size > 0U) {
        /* Check if rounded border is needed */
        if (rad > 0) {
            /* Render rounded border */
            (void)roundedRectangleRGBA(p_ren, x1, y1, x2, y2, rad,
                                       p_rect->Border.Color.b.r, p_rect->Border.Color.b.g,
                                       p_rect->Border.Color.b.b, p_rect->Border.Color.b.a);
        } else {
            /* Render sharp border */
            (void)rectangleRGBA(p_ren, x1, y1, x2, y2,
                                p_rect->Border.Color.b.r, p_rect->Border.Color.b.g,
                                p_rect->Border.Color.b.b, p_rect->Border.Color.b.a);
        }
    }

    /* Return success after drawing rectangle */
    return APP_SUCCESS;
}

/* Draw a circle with optional fill and border
 * @param p_circle[in] Pointer to circle configuration (coordinates relative to Layer (0,0))
 * @return APP_SUCCESS on success, or APP_ERROR_NULL
 * @note Color format is ARGB (AARRGGBB)
 */
Err_t DrawCircle(const CompCircleCfg_t *p_circle) {
    SDL_Renderer *p_ren;
    int16_t       cx;
    int16_t       cy;
    int16_t       rad;

    #if (1U == PARAM_CHECK_EN)
    /* Validate circle configuration pointer */
    if (NULL == p_circle) {
        APP_ERR("p_circle is NULL!");
        return APP_ERROR_NULL;
    }
    #endif /* (1U == PARAM_CHECK_EN) */

    p_ren = (SDL_Renderer *)g_MainWindow.p_SdlRenderer;
    /* Check if SDL renderer is active */
    if (NULL == p_ren) {
        /* Return early if renderer is not initialized */
        return APP_SUCCESS;
    }

    rad = (int16_t)p_circle->Radius;
    cx  = (int16_t)(g_LayerOriginX + p_circle->Dim.OffsetX + (int32_t)rad);
    cy  = (int16_t)(g_LayerOriginY + p_circle->Dim.OffsetY + (int32_t)rad);

    /* Render filled or outlined circle based on IsFilled flag */
    if (0U != p_circle->IsFilled) {
        /* Draw filled circle via SDL2_gfx */
        (void)filledCircleRGBA(p_ren, cx, cy, rad,
                               p_circle->Color.b.r, p_circle->Color.b.g,
                               p_circle->Color.b.b, p_circle->Color.b.a);
    } else {
        /* Draw circle outline via SDL2_gfx */
        (void)circleRGBA(p_ren, cx, cy, rad,
                         p_circle->Color.b.r, p_circle->Color.b.g,
                         p_circle->Color.b.b, p_circle->Color.b.a);
    }

    /* Draw circle border if configured */
    if (p_circle->Border.Size > 0U) {
        /* Draw outer border ring */
        (void)circleRGBA(p_ren, cx, cy, rad,
                         p_circle->Border.Color.b.r, p_circle->Border.Color.b.g,
                         p_circle->Border.Color.b.b, p_circle->Border.Color.b.a);
    }

    /* Return success after drawing circle */
    return APP_SUCCESS;
}

/* Draw a line between (OffsetX, OffsetY) and (EndX, EndY) relative to Layer (0,0)
 * @param p_line[in] Pointer to line configuration
 * @return APP_SUCCESS on success, or APP_ERROR_NULL
 * @note Supports configurable line thickness and ARGB color
 */
Err_t DrawLine(const CompLineCfg_t *p_line) {
    SDL_Renderer *p_ren;

    #if (1U == PARAM_CHECK_EN)
    /* Validate line configuration pointer */
    if (NULL == p_line) {
        APP_ERR("p_line is NULL!");
        return APP_ERROR_NULL;
    }
    #endif /* (1U == PARAM_CHECK_EN) */

    p_ren = (SDL_Renderer *)g_MainWindow.p_SdlRenderer;
    /* Check if SDL renderer is active */
    if (NULL == p_ren) {
        /* Return early if renderer is NULL */
        return APP_SUCCESS;
    }

    /* Draw thick line via SDL2_gfx translated by Layer origin */
    (void)thickLineRGBA(p_ren,
                        (int16_t)(g_LayerOriginX + p_line->Dim.OffsetX),
                        (int16_t)(g_LayerOriginY + p_line->Dim.OffsetY),
                        (int16_t)(g_LayerOriginX + p_line->EndX),
                        (int16_t)(g_LayerOriginY + p_line->EndY),
                        (uint8_t)p_line->Thickness,
                        p_line->Color.b.r, p_line->Color.b.g,
                        p_line->Color.b.b, p_line->Color.b.a);

    /* Return success after drawing line */
    return APP_SUCCESS;
}

/* Draw a text string component relative to Layer (0,0)
 * @param p_text[in] Pointer to text configuration
 * @return APP_SUCCESS on success, or APP_ERROR_NULL
 * @note Uses SDL2_gfx stringRGBA built-in font renderer
 */
Err_t DrawText(const CompTextCfg_t *p_text) {
    SDL_Renderer *p_ren;

    #if (1U == PARAM_CHECK_EN)
    /* Validate text configuration pointer */
    if (NULL == p_text) {
        APP_ERR("p_text is NULL!");
        return APP_ERROR_NULL;
    }
    #endif /* (1U == PARAM_CHECK_EN) */

    p_ren = (SDL_Renderer *)g_MainWindow.p_SdlRenderer;
    /* Check if renderer and text string are valid */
    if ((NULL == p_ren) || (NULL == p_text->TextStr)) {
        /* Skip rendering if renderer or string is NULL */
        return APP_SUCCESS;
    }

    /* Draw text string at Layer-translated (OffsetX, OffsetY) */
    (void)stringRGBA(p_ren,
                     (int16_t)(g_LayerOriginX + p_text->Dim.OffsetX),
                     (int16_t)(g_LayerOriginY + p_text->Dim.OffsetY),
                     p_text->TextStr,
                     p_text->Color.b.r, p_text->Color.b.g,
                     p_text->Color.b.b, p_text->Color.b.a);

    /* Return success after drawing text */
    return APP_SUCCESS;
}

/* Draw a table component relative to Layer (0,0) while preserving any active Layer RectClip
 * @param p_table[in] Pointer to table configuration
 * @return APP_SUCCESS on success, or APP_ERROR_NULL
 * @note Preserves parent Layer clip rect if already active
 */
Err_t DrawScrollTable(const CompTableCfg_t *p_table) {
    SDL_Renderer *p_ren;
    SDL_Rect      prev_clip;
    SDL_bool      had_clip;
    int32_t       base_x;
    int32_t       base_y;
    uint32_t      r_idx;
    uint32_t      c_idx;
    int32_t       cell_w;
    int32_t       cell_h;
    char          cell_label[32];

    #if (1U == PARAM_CHECK_EN)
    /* Validate table configuration pointer */
    if (NULL == p_table) {
        APP_ERR("p_table is NULL!");
        return APP_ERROR_NULL;
    }
    #endif /* (1U == PARAM_CHECK_EN) */

    p_ren = (SDL_Renderer *)g_MainWindow.p_SdlRenderer;
    /* Check if SDL renderer is active */
    if (NULL == p_ren) {
        /* Return early if renderer is NULL */
        return APP_SUCCESS;
    }

    /* Save existing Layer clip state before rendering table */
    had_clip = SDL_RenderIsClipEnabled(p_ren);
    if (SDL_TRUE == had_clip) {
        SDL_RenderGetClipRect(p_ren, &prev_clip);
    }

    base_x = g_LayerOriginX + p_table->Dim.OffsetX;
    base_y = g_LayerOriginY + p_table->Dim.OffsetY;

    /* Draw table background box */
    (void)boxRGBA(p_ren,
                  (int16_t)base_x,
                  (int16_t)base_y,
                  (int16_t)(base_x + (int32_t)p_table->Dim.W),
                  (int16_t)(base_y + (int32_t)p_table->Dim.H),
                  p_table->Color.b.r, p_table->Color.b.g,
                  p_table->Color.b.b, p_table->Color.b.a);

    /* If parent layer had no clip rect, clip to the table's own viewport */
    if (SDL_FALSE == had_clip) {
        SDL_Rect tbl_clip;
        tbl_clip.x = base_x;
        tbl_clip.y = base_y;
        tbl_clip.w = (int)p_table->Dim.W;
        tbl_clip.h = (int)p_table->Dim.H;
        (void)SDL_RenderSetClipRect(p_ren, &tbl_clip);
    }

    cell_w = (p_table->Cols > 0U) ? ((int32_t)p_table->Dim.W / (int32_t)p_table->Cols) : 80;
    cell_h = 28;

    /* Iterate over table rows to render grid cells */
    for (r_idx = 0U; r_idx < p_table->Rows; r_idx++) {
        /* Iterate over table columns in current row */
        for (c_idx = 0U; c_idx < p_table->Cols; c_idx++) {
            int32_t cx = base_x + ((int32_t)c_idx * cell_w) + p_table->ScrollX;
            int32_t cy = base_y + ((int32_t)r_idx * cell_h) + p_table->ScrollY;

            /* Draw cell border */
            (void)rectangleRGBA(p_ren,
                                (int16_t)cx,
                                (int16_t)cy,
                                (int16_t)(cx + cell_w),
                                (int16_t)(cy + cell_h),
                                p_table->Border.Color.b.r,
                                p_table->Border.Color.b.g,
                                p_table->Border.Color.b.b,
                                p_table->Border.Color.b.a);

            /* Format and draw cell coordinate label */
            (void)snprintf(cell_label, sizeof(cell_label), "R%uC%u", r_idx, c_idx);
            (void)stringRGBA(p_ren,
                             (int16_t)(cx + 6),
                             (int16_t)(cy + 10),
                             cell_label,
                             p_table->Border.Color.b.r,
                             p_table->Border.Color.b.g,
                             p_table->Border.Color.b.b,
                             p_table->Border.Color.b.a);
        }
    }

    /* Restore previous Layer clip rect (or disable clip if none was active) */
    if (SDL_TRUE == had_clip) {
        (void)SDL_RenderSetClipRect(p_ren, &prev_clip);
    } else {
        (void)SDL_RenderSetClipRect(p_ren, NULL);
    }

    /* Return success after drawing scrollable table */
    return APP_SUCCESS;
}

/* Render a single component and all of its attached p_ExtComps children
 * @param p_comp[in] Pointer to component to render
 * @return APP_SUCCESS on success, or error code
 * @note Skips rendering if IsVisible is 0
 */
Err_t DrawComponent(const Comp_t *p_comp) {
    Err_t    err;
    uint32_t ext_idx;

    #if (1U == PARAM_CHECK_EN)
    /* Validate component pointer */
    if (NULL == p_comp) {
        APP_ERR("p_comp is NULL!");
        return APP_ERROR_NULL;
    }
    #endif /* (1U == PARAM_CHECK_EN) */

    /* Check if component is visible before drawing */
    if (0U == p_comp->IsVisible) {
        /* Skip invisible component */
        return APP_SUCCESS;
    }

    /* Dispatch drawing primitive based on component CastType */
    switch (p_comp->CastType) {
        case COMP_CAST_RECT:
            /* Render rectangle component */
            err = DrawRect(&p_comp->Type.Rect);
            /* Break after drawing rect */
            break;
        case COMP_CAST_CIRCLE:
            /* Render circle component */
            err = DrawCircle(&p_comp->Type.Circle);
            /* Break after drawing circle */
            break;
        case COMP_CAST_LINE:
            /* Render line component */
            err = DrawLine(&p_comp->Type.Line);
            /* Break after drawing line */
            break;
        case COMP_CAST_TEXT:
            /* Render text component */
            err = DrawText(&p_comp->Type.Text);
            /* Break after drawing text */
            break;
        case COMP_CAST_SCROLL_TABLE:
            /* Render scrollable table component */
            err = DrawScrollTable(&p_comp->Type.Table);
            /* Break after drawing table */
            break;
        default:
            APP_ERR("Unsupported component CastType!");
            return APP_ERROR_INVALID_PARAM;
    }

    /* Check if primary component draw failed */
    if (APP_SUCCESS != err) {
        APP_ERR("Failed to draw primary component!");
        return err;
    }

    /* Automatically draw centered button label if Class is COMP_CLASS_BUTTON and p_ExtComps is NULL */
    if ((COMP_CLASS_BUTTON == p_comp->Class) &&
        (COMP_CAST_RECT == p_comp->CastType) &&
        (NULL == p_comp->p_ExtComps) &&
        (NULL != p_comp->Label)) {
        CompTextCfg_t btn_txt;
        btn_txt.Color.w      = 0xFFFFFFFFU;
        btn_txt.Dim.OffsetX  = p_comp->Type.Rect.Dim.OffsetX + 12;
        btn_txt.Dim.OffsetY  = p_comp->Type.Rect.Dim.OffsetY + ((int32_t)p_comp->Type.Rect.Dim.H / 2) - 4;
        btn_txt.Dim.W        = p_comp->Type.Rect.Dim.W;
        btn_txt.Dim.H        = 12U;
        btn_txt.Dim.Rotation = 0U;
        btn_txt.TextStr      = p_comp->Label;
        btn_txt.FontSize     = 12U;
        /* Render button label text */
        (void)DrawText(&btn_txt);
    }

    /* Check if component has child/extension components (p_ExtComps) */
    if ((NULL != p_comp->p_ExtComps) && (NULL != p_comp->p_ExtComps->p_ExtComps)) {
        /* Iterate over all child extension components to render them */
        for (ext_idx = 0U; ext_idx < p_comp->p_ExtComps->Count; ext_idx++) {
            const ExtComp_t *p_ext = &p_comp->p_ExtComps->p_ExtComps[ext_idx];

            /* Render child extension based on its CastType */
            switch (p_ext->CastType) {
                case COMP_CAST_TEXT:
                    /* Draw child text config if non-NULL */
                    if (NULL != p_ext->CompPtr.p_Text) {
                        (void)DrawText(p_ext->CompPtr.p_Text);
                    }
                    /* Break after child text draw */
                    break;
                case COMP_CAST_RECT:
                    /* Draw child rect config if non-NULL */
                    if (NULL != p_ext->CompPtr.p_Rect) {
                        (void)DrawRect(p_ext->CompPtr.p_Rect);
                    }
                    /* Break after child rect draw */
                    break;
                default:
                    /* Draw generic nested Comp_t if non-NULL */
                    if (NULL != p_ext->CompPtr.p_Comp) {
                        (void)DrawComponent(p_ext->CompPtr.p_Comp);
                    }
                    /* Break after generic child draw */
                    break;
            }
        }
    }

    /* Return success after drawing component and its children */
    return APP_SUCCESS;
}

/* Render all components inside a layer using MainWindow RectClip and Layer (0,0) component origin
 * @param p_layer[in] Pointer to layer to render
 * @return APP_SUCCESS on success, or error code
 * @note Layer RectClip is relative to MainWindow (0,0); components are relative to Layer (0,0)
 */
Err_t DrawLayer(const Layer_t *p_layer) {
    uint32_t      comp_idx;
    SDL_Renderer *p_ren;
    uint8_t       has_rect_clip = 0U;

    #if (1U == PARAM_CHECK_EN)
    /* Validate layer pointer */
    if (NULL == p_layer) {
        APP_ERR("p_layer is NULL!");
        return APP_ERROR_NULL;
    }
    #endif /* (1U == PARAM_CHECK_EN) */

    /* Check if layer is visible */
    if (0U == p_layer->IsVisible) {
        /* Return early when layer is hidden */
        return APP_SUCCESS;
    }

    p_ren = (SDL_Renderer *)g_MainWindow.p_SdlRenderer;

    /* Set layer origin in MainWindow coordinates: RectClip offset + Layer scroll offset */
    g_LayerOriginX = p_layer->RectClip.OffsetX + p_layer->ScrollX;
    g_LayerOriginY = p_layer->RectClip.OffsetY + p_layer->ScrollY;

    /* Apply RectClip in MainWindow (0,0) coordinates if configured */
    if ((NULL != p_ren) && (p_layer->RectClip.W > 0U) && (p_layer->RectClip.H > 0U)) {
        SDL_Rect layer_clip;
        layer_clip.x = p_layer->RectClip.OffsetX;
        layer_clip.y = p_layer->RectClip.OffsetY;
        layer_clip.w = (int)p_layer->RectClip.W;
        layer_clip.h = (int)p_layer->RectClip.H;
        (void)SDL_RenderSetClipRect(p_ren, &layer_clip);
        has_rect_clip = 1U;
    }

    /* Assert that component array pointer is valid when CompCount > 0 */
    if (p_layer->CompCount > 0U) {
        ASSERT_NOT_EQUAL(NULL, p_layer->p_Comps);
    }

    /* Iterate through all components in the layer to render them in order */
    for (comp_idx = 0U; comp_idx < p_layer->CompCount; comp_idx++) {
        /* Draw current component relative to Layer (0,0) */
        (void)DrawComponent(&p_layer->p_Comps[comp_idx]);
    }

    /* Reset clipping and draw a subtle frame around scrollable RectClip in MainWindow coords */
    if ((0U != has_rect_clip) && (NULL != p_ren)) {
        (void)SDL_RenderSetClipRect(p_ren, NULL);
        if (0U != p_layer->IsScrollable) {
            (void)rectangleRGBA(p_ren,
                                (int16_t)p_layer->RectClip.OffsetX,
                                (int16_t)p_layer->RectClip.OffsetY,
                                (int16_t)(p_layer->RectClip.OffsetX + (int32_t)p_layer->RectClip.W),
                                (int16_t)(p_layer->RectClip.OffsetY + (int32_t)p_layer->RectClip.H),
                                0U, 180U, 216U, 255U);
        }
    }

    g_LayerOriginX = 0;
    g_LayerOriginY = 0;

    /* Return success after rendering entire layer */
    return APP_SUCCESS;
}

/* reserved ***************************************************************************************
 **************************************************************************************************/
