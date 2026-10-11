/* file-desc **************************************************************************************
 * @filename    Draw.c
 * @brief       Implementation of SDL2 drawing primitives and recursive component rendering.
 * @note        Renders components and their p_ExtComps children in ARGB color mode via SDL2_gfx.
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
 * @param p_rect[in] Pointer to rectangle configuration
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

    x1  = (int16_t)p_rect->Dim.OffsetX;
    y1  = (int16_t)p_rect->Dim.OffsetY;
    x2  = (int16_t)(p_rect->Dim.OffsetX + (int32_t)p_rect->Dim.W);
    y2  = (int16_t)(p_rect->Dim.OffsetY + (int32_t)p_rect->Dim.H);
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
 * @param p_circle[in] Pointer to circle configuration
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
    cx  = (int16_t)(p_circle->Dim.OffsetX + (int32_t)rad);
    cy  = (int16_t)(p_circle->Dim.OffsetY + (int32_t)rad);

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

/* Draw a line between (OffsetX, OffsetY) and (EndX, EndY)
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

    /* Draw thick line via SDL2_gfx */
    (void)thickLineRGBA(p_ren,
                        (int16_t)p_line->Dim.OffsetX,
                        (int16_t)p_line->Dim.OffsetY,
                        (int16_t)p_line->EndX,
                        (int16_t)p_line->EndY,
                        (uint8_t)p_line->Thickness,
                        p_line->Color.b.r, p_line->Color.b.g,
                        p_line->Color.b.b, p_line->Color.b.a);

    /* Return success after drawing line */
    return APP_SUCCESS;
}

/* Draw a text string component
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

    /* Draw text string at (OffsetX, OffsetY) */
    (void)stringRGBA(p_ren,
                     (int16_t)p_text->Dim.OffsetX,
                     (int16_t)p_text->Dim.OffsetY,
                     p_text->TextStr,
                     p_text->Color.b.r, p_text->Color.b.g,
                     p_text->Color.b.b, p_text->Color.b.a);

    /* Return success after drawing text */
    return APP_SUCCESS;
}

/* Draw a scrollable table component with clipped cells and grid lines
 * @param p_table[in] Pointer to scrollable table configuration
 * @return APP_SUCCESS on success, or APP_ERROR_NULL
 * @note Applies SDL clip rect and renders scrolled grid rows/columns
 */
Err_t DrawScrollTable(const CompTableCfg_t *p_table) {
    SDL_Renderer *p_ren;
    SDL_Rect      clip_rect;
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

    clip_rect.x = p_table->Dim.OffsetX;
    clip_rect.y = p_table->Dim.OffsetY;
    clip_rect.w = (int)p_table->Dim.W;
    clip_rect.h = (int)p_table->Dim.H;

    /* Draw table background box */
    (void)boxRGBA(p_ren,
                  (int16_t)clip_rect.x,
                  (int16_t)clip_rect.y,
                  (int16_t)(clip_rect.x + clip_rect.w),
                  (int16_t)(clip_rect.y + clip_rect.h),
                  p_table->Color.b.r, p_table->Color.b.g,
                  p_table->Color.b.b, p_table->Color.b.a);

    /* Enable clipping to table viewport for scrollable content */
    (void)SDL_RenderSetClipRect(p_ren, &clip_rect);

    cell_w = (p_table->Cols > 0U) ? ((int32_t)p_table->Dim.W / (int32_t)p_table->Cols) : 80;
    cell_h = 28;

    /* Iterate over table rows to render grid cells */
    for (r_idx = 0U; r_idx < p_table->Rows; r_idx++) {
        /* Iterate over table columns in current row */
        for (c_idx = 0U; c_idx < p_table->Cols; c_idx++) {
            int32_t cx = clip_rect.x + ((int32_t)c_idx * cell_w) + p_table->ScrollX;
            int32_t cy = clip_rect.y + ((int32_t)r_idx * cell_h) + p_table->ScrollY;

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

    /* Reset clipping rectangle back to full window */
    (void)SDL_RenderSetClipRect(p_ren, NULL);

    /* Draw outer border of the scrollable table */
    (void)rectangleRGBA(p_ren,
                        (int16_t)clip_rect.x,
                        (int16_t)clip_rect.y,
                        (int16_t)(clip_rect.x + clip_rect.w),
                        (int16_t)(clip_rect.y + clip_rect.h),
                        p_table->Border.Color.b.r,
                        p_table->Border.Color.b.g,
                        p_table->Border.Color.b.b,
                        p_table->Border.Color.b.a);

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

/* Render all components inside a layer
 * @param p_layer[in] Pointer to layer to render
 * @return APP_SUCCESS on success, or error code
 * @note Skips layer if IsVisible is 0
 */
Err_t DrawLayer(const Layer_t *p_layer) {
    uint32_t comp_idx;

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

    /* Iterate through all components in the layer to render them in order */
    for (comp_idx = 0U; comp_idx < p_layer->CompCount; comp_idx++) {
        /* Draw current component in the layer */
        (void)DrawComponent(&p_layer->p_Comps[comp_idx]);
    }

    /* Return success after rendering entire layer */
    return APP_SUCCESS;
}

/* reserved ***************************************************************************************
 **************************************************************************************************/
