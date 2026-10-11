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

#define DRAW_FONT_CACHE_MAX             (24U)
#define DRAW_FONT_NAME_MAX              (128U)
#define DRAW_FONT_PATH_MAX              (256U)

/* definitions ************************************************************************************
 **************************************************************************************************/

/* Cached SDL_ttf font entry keyed by (Name, Size) */
struct FontCacheEntry_st {
    uint8_t   IsUsed;
    char      Name[DRAW_FONT_NAME_MAX];
    uint32_t  Size;
    TTF_Font *p_Font;
};
typedef struct FontCacheEntry_st FontCacheEntry_t;

/* exported variables *****************************************************************************
 **************************************************************************************************/

/* public variables *******************************************************************************
 **************************************************************************************************/

/* private variables ******************************************************************************
 **************************************************************************************************/

/* Active layer origin offset in MainWindow coordinates (RectClip.Offset + Layer.Scroll) */
static int32_t          g_LayerOriginX         = 0;
static int32_t          g_LayerOriginY         = 0;
static uint8_t          g_InCompRotationTarget = 0U;
static char             g_DefaultFontName[DRAW_FONT_NAME_MAX] = "";
static FontCacheEntry_t g_FontCache[DRAW_FONT_CACHE_MAX];

/* exported functions *****************************************************************************
 **************************************************************************************************/

/* public functions *******************************************************************************
 **************************************************************************************************/

Err_t DrawSetDefaultFont(const_str_t font_name);
Err_t DrawRect(const CompRectCfg_t *p_rect);
Err_t DrawCircle(const CompCircleCfg_t *p_circle);
Err_t DrawLine(const CompLineCfg_t *p_line);
Err_t DrawText(const CompTextCfg_t *p_text);
Err_t DrawScrollTable(const CompTableCfg_t *p_table);
Err_t DrawComponent(const Comp_t *p_comp);
Err_t DrawLayer(const Layer_t *p_layer);

/* private functions ******************************************************************************
 **************************************************************************************************/

static void      ResolveFontFileViaFcMatch(const char *p_family, char *p_out_path, size_t out_len);
static TTF_Font *GetCachedTtfFont(const_str_t font_name, uint32_t font_size);

/* function implementations ***********************************************************************
 **************************************************************************************************/

/* Resolve a font family name (e.g. "CodeNewRoman Nerd Font") to a .ttf/.otf file path via fc-match
 * @param p_family[in] Font family name or direct file path
 * @param p_out_path[out] Resolved file path buffer
 * @param out_len[in] Size of p_out_path buffer
 */
static void ResolveFontFileViaFcMatch(const char *p_family, char *p_out_path, size_t out_len) {
    char  cmd[512];
    FILE *p_pipe;

    p_out_path[0] = '\0';
    if ((NULL == p_family) || ('\0' == p_family[0])) {
        /* Return empty path if family name is empty */
        return;
    }

    /* If p_family already looks like a direct file path, copy it directly */
    if ('/' == p_family[0]) {
        (void)snprintf(p_out_path, out_len, "%s", p_family);
        /* Return direct file path */
        return;
    }

    (void)snprintf(cmd, sizeof(cmd), "fc-match -f \"%%{file}\" \"%s\" 2>/dev/null", p_family);
    p_pipe = popen(cmd, "r");
    if (NULL != p_pipe) {
        if (NULL != fgets(p_out_path, (int)out_len, p_pipe)) {
            size_t len = strlen(p_out_path);
            while ((len > 0U) &&
                   (('\n' == p_out_path[len - 1U]) || ('\r' == p_out_path[len - 1U]))) {
                p_out_path[len - 1U] = '\0';
                len--;
            }
        }
        (void)pclose(p_pipe);
    }

    /* Return after resolving font path */
    return;
}

/* Lookup or open a cached TTF_Font for the requested font family/path and pixel size
 * @param font_name[in] Component-specific font name, or NULL/"" to use g_DefaultFontName
 * @param font_size[in] Font pixel size (defaults to 13 if 0)
 * @return Pointer to opened TTF_Font, or NULL if no TTF font is configured/available
 */
static TTF_Font *GetCachedTtfFont(const_str_t font_name, uint32_t font_size) {
    const char *p_eff_name = NULL;
    uint32_t    eff_size   = (font_size > 0U) ? font_size : 13U;
    uint32_t    idx;
    uint32_t    free_slot  = DRAW_FONT_CACHE_MAX;
    char        font_path[DRAW_FONT_PATH_MAX];
    TTF_Font   *p_font     = NULL;

    /* Select component-specific FontName first, or fall back to MainWindow default font */
    if ((NULL != font_name) && ('\0' != font_name[0])) {
        p_eff_name = font_name;
    } else if ('\0' != g_DefaultFontName[0]) {
        p_eff_name = g_DefaultFontName;
    } else {
        /* Return NULL to use built-in bitmap font when no font name is configured */
        return NULL;
    }

    /* Ensure SDL_ttf is initialized */
    if (0 == TTF_WasInit()) {
        if (0 != TTF_Init()) {
            /* Return NULL if SDL_ttf cannot be initialized */
            return NULL;
        }
    }

    /* Check cache for an existing (Name, Size) entry */
    for (idx = 0U; idx < DRAW_FONT_CACHE_MAX; idx++) {
        if (0U != g_FontCache[idx].IsUsed) {
            if ((g_FontCache[idx].Size == eff_size) &&
                (0 == strcmp(g_FontCache[idx].Name, p_eff_name))) {
                /* Return cached TTF_Font pointer */
                return g_FontCache[idx].p_Font;
            }
        } else if (DRAW_FONT_CACHE_MAX == free_slot) {
            free_slot = idx;
        } else {
            /* Keep scanning cache */
        }
    }

    /* Resolve font family name to .ttf/.otf file path and open via SDL_ttf */
    ResolveFontFileViaFcMatch(p_eff_name, font_path, sizeof(font_path));
    if ('\0' != font_path[0]) {
        p_font = TTF_OpenFont(font_path, (int)eff_size);
        if (NULL != p_font) {
            APP_INFO("Loaded TTF font '%s' (%u px) -> %s", p_eff_name, eff_size, font_path);
        }
    }

    /* Store result in cache (even if NULL, to avoid re-running fc-match every frame) */
    if (free_slot < DRAW_FONT_CACHE_MAX) {
        g_FontCache[free_slot].IsUsed = 1U;
        (void)snprintf(g_FontCache[free_slot].Name, sizeof(g_FontCache[free_slot].Name), "%s", p_eff_name);
        g_FontCache[free_slot].Size   = eff_size;
        g_FontCache[free_slot].p_Font = p_font;
    }

    /* Return opened TTF_Font pointer */
    return p_font;
}

/* Set the default window-wide font family name or .ttf/.otf file path
 * @param font_name[in] Font family name (e.g. "CodeNewRoman Nerd Font") or path
 * @return APP_SUCCESS on success, or APP_ERROR_NULL if font_name is NULL
 */
Err_t DrawSetDefaultFont(const_str_t font_name) {
    #if (1U == PARAM_CHECK_EN)
    /* Validate font_name pointer */
    if (NULL == font_name) {
        APP_ERR("font_name is NULL in DrawSetDefaultFont!");
        return APP_ERROR_NULL;
    }
    #endif /* (1U == PARAM_CHECK_EN) */

    (void)snprintf(g_DefaultFontName, sizeof(g_DefaultFontName), "%s", font_name);

    /* Return success after updating default font name */
    return APP_SUCCESS;
}

/* Draw a rectangle with optional corner radius, fill, border, and 0-360 degree rotation
 * @param p_rect[in] Pointer to rectangle configuration (coordinates relative to Layer (0,0))
 * @return APP_SUCCESS on success, or error code
 * @note Color format is ARGB (AARRGGBB); rotates around rect center when Dim.Rotation != 0
 */
Err_t DrawRect(const CompRectCfg_t *p_rect) {
    SDL_Renderer *p_ren;
    SDL_Texture  *p_rot_tex   = NULL;
    SDL_Texture  *p_prev_tgt  = NULL;
    int16_t       x1;
    int16_t       y1;
    int16_t       x2;
    int16_t       y2;
    int16_t       rad;
    uint16_t      rot_deg;

    #if (1U == PARAM_CHECK_EN)
    /* Validate rectangle configuration pointer */
    if (NULL == p_rect) {
        APP_ERR("p_rect is NULL!");
        return APP_ERROR_NULL;
    }
    #endif /* (1U == PARAM_CHECK_EN) */

    p_ren = (SDL_Renderer *)g_MainWindow.p_SdlRenderer;
    /* Check if SDL renderer is active */
    if ((NULL == p_ren) || (0U == p_rect->Dim.W) || (0U == p_rect->Dim.H)) {
        /* Return early when running headless or when dimensions are zero */
        return APP_SUCCESS;
    }

    rot_deg = (uint16_t)(p_rect->Dim.Rotation % 360U);

    /* If standalone DrawRect has non-zero rotation, render into an offscreen target texture */
    if ((0U == g_InCompRotationTarget) && (0U != rot_deg)) {
        p_rot_tex = SDL_CreateTexture(p_ren,
                                      SDL_PIXELFORMAT_ARGB8888,
                                      SDL_TEXTUREACCESS_TARGET,
                                      (int)p_rect->Dim.W,
                                      (int)p_rect->Dim.H);
        if (NULL != p_rot_tex) {
            (void)SDL_SetTextureBlendMode(p_rot_tex, SDL_BLENDMODE_BLEND);
            p_prev_tgt = SDL_GetRenderTarget(p_ren);
            (void)SDL_SetRenderTarget(p_ren, p_rot_tex);
            (void)SDL_SetRenderDrawColor(p_ren, 0U, 0U, 0U, 0U);
            (void)SDL_RenderClear(p_ren);
        }
    }

    if (NULL != p_rot_tex) {
        x1 = 0;
        y1 = 0;
        x2 = (int16_t)((int32_t)p_rect->Dim.W - 1);
        y2 = (int16_t)((int32_t)p_rect->Dim.H - 1);
    } else {
        x1 = (int16_t)(g_LayerOriginX + p_rect->Dim.OffsetX);
        y1 = (int16_t)(g_LayerOriginY + p_rect->Dim.OffsetY);
        x2 = (int16_t)(g_LayerOriginX + p_rect->Dim.OffsetX + (int32_t)p_rect->Dim.W - 1);
        y2 = (int16_t)(g_LayerOriginY + p_rect->Dim.OffsetY + (int32_t)p_rect->Dim.H - 1);
    }
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

    /* Composite rotated texture back onto previous target if offscreen rotation was used */
    if (NULL != p_rot_tex) {
        SDL_Rect dst_rect;
        dst_rect.x = g_LayerOriginX + p_rect->Dim.OffsetX;
        dst_rect.y = g_LayerOriginY + p_rect->Dim.OffsetY;
        dst_rect.w = (int)p_rect->Dim.W;
        dst_rect.h = (int)p_rect->Dim.H;

        (void)SDL_SetRenderTarget(p_ren, p_prev_tgt);
        (void)SDL_RenderCopyEx(p_ren, p_rot_tex, NULL, &dst_rect, (double)rot_deg, NULL, SDL_FLIP_NONE);
        SDL_DestroyTexture(p_rot_tex);
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

/* Draw a text string component relative to Layer (0,0) with TTF font, newline, wrap, and alignment
 * @param p_text[in] Pointer to text configuration
 * @return APP_SUCCESS on success, or APP_ERROR_NULL
 * @note Uses p_text->FontName (or MainWindow default font via SDL2_ttf), with 8x8 bitmap fallback
 */
Err_t DrawText(const CompTextCfg_t *p_text) {
    SDL_Renderer *p_ren;
    TTF_Font     *p_ttf      = NULL;
    char          line_buf[32][128];
    uint32_t      line_len[32];
    int32_t       line_px_w[32];
    uint32_t      line_count = 0U;
    uint32_t      max_chars  = 127U;
    const char   *p_cur;
    int32_t       char_w     = 8;
    int32_t       line_h     = 14;
    int32_t       glyph_h    = 8;
    int32_t       total_h;
    int32_t       start_y;
    uint32_t      idx;

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

    /* Obtain cached TTF_Font for p_text->FontName (or Window default font) and p_text->FontSize */
    p_ttf = GetCachedTtfFont(p_text->FontName, p_text->FontSize);
    if (NULL != p_ttf) {
        int ttf_style = TTF_STYLE_NORMAL;
        int adv_w     = 0;
        int adv_h     = 0;

        /* Apply Bold and Italic style flags from Format configuration */
        if (0U != p_text->IsBold) {
            ttf_style |= TTF_STYLE_BOLD;
        }
        if (0U != p_text->IsItalic) {
            ttf_style |= TTF_STYLE_ITALIC;
        }
        if (TTF_GetFontStyle(p_ttf) != ttf_style) {
            TTF_SetFontStyle(p_ttf, ttf_style);
        }

        if ((0 == TTF_SizeUTF8(p_ttf, "M", &adv_w, &adv_h)) && (adv_w > 0)) {
            char_w  = adv_w;
            glyph_h = adv_h;
        }
        line_h = TTF_FontLineSkip(p_ttf);
        if (line_h < glyph_h) {
            line_h = glyph_h + 2;
        }
    }

    /* Calculate maximum characters per line when wrapping is enabled and Dim.W is valid */
    if ((0U != p_text->IsWrap) && (p_text->Dim.W >= (uint32_t)char_w)) {
        max_chars = p_text->Dim.W / (uint32_t)char_w;
        if (max_chars > 127U) {
            max_chars = 127U;
        }
        if (0U == max_chars) {
            max_chars = 1U;
        }
    }

    p_cur = p_text->TextStr;
    /* Split text into lines by '\n' and optional word-wrap width */
    while (('\0' != *p_cur) && (line_count < 32U)) {
        uint32_t count      = 0U;
        uint32_t last_space = 0U;
        uint8_t  has_space  = 0U;

        /* Scan characters for current line up to '\n', '\0', or max_chars */
        while (('\0' != p_cur[count]) && ('\n' != p_cur[count]) && (count < max_chars)) {
            if (' ' == p_cur[count]) {
                last_space = count;
                has_space  = 1U;
            }
            count++;
        }

        /* If line hit max_chars without reaching '\0' or '\n', wrap at last space if available */
        if ((count == max_chars) && ('\0' != p_cur[count]) && ('\n' != p_cur[count]) && (' ' != p_cur[count])) {
            if ((0U != has_space) && (last_space > 0U)) {
                count = last_space;
            }
        }

        /* Copy current line segment into line_buf */
        (void)memcpy(line_buf[line_count], p_cur, count);
        line_buf[line_count][count] = '\0';
        line_len[line_count]        = count;
        if ((NULL != p_ttf) && (count > 0U)) {
            int tw = 0;
            int th = 0;
            (void)TTF_SizeUTF8(p_ttf, line_buf[line_count], &tw, &th);
            line_px_w[line_count] = tw;
        } else {
            line_px_w[line_count] = (int32_t)count * char_w;
        }
        line_count++;

        p_cur += count;
        /* Skip explicit newline or wrapped space delimiter */
        if ('\n' == *p_cur) {
            p_cur++;
        } else if ((0U != p_text->IsWrap) && (' ' == *p_cur)) {
            p_cur++;
        } else {
            /* Continue from next character */
        }
    }

    /* Handle empty string gracefully */
    if (0U == line_count) {
        /* Nothing to draw for empty string */
        return APP_SUCCESS;
    }

    /* Calculate total block height */
    total_h = ((int32_t)line_count * line_h) - (line_h - glyph_h);

    /* Compute starting Y coordinate based on vertical alignment (VAlign) */
    start_y = g_LayerOriginY + p_text->Dim.OffsetY;
    if (p_text->Dim.H > 0U) {
        if (TEXT_VALIGN_MIDDLE == p_text->VAlign) {
            start_y += ((int32_t)p_text->Dim.H - total_h) / 2;
        } else if (TEXT_VALIGN_BOTTOM == p_text->VAlign) {
            start_y += (int32_t)p_text->Dim.H - total_h;
        } else {
            /* TEXT_VALIGN_TOP uses OffsetY directly */
        }
    }

    /* Render each formatted line with horizontal alignment (Align) */
    for (idx = 0U; idx < line_count; idx++) {
        int32_t l_width = line_px_w[idx];
        int32_t draw_x  = g_LayerOriginX + p_text->Dim.OffsetX;
        int32_t draw_y  = start_y + ((int32_t)idx * line_h);

        if (0U == line_len[idx]) {
            /* Skip empty line */
            continue;
        }

        /* Adjust X coordinate if horizontal alignment is CENTER or RIGHT and Dim.W is non-zero */
        if (p_text->Dim.W > 0U) {
            if (TEXT_ALIGN_CENTER == p_text->Align) {
                draw_x += ((int32_t)p_text->Dim.W - l_width) / 2;
            } else if (TEXT_ALIGN_RIGHT == p_text->Align) {
                draw_x += (int32_t)p_text->Dim.W - l_width;
            } else {
                /* TEXT_ALIGN_LEFT uses OffsetX directly */
            }
        }

        /* Render crisp anti-aliased TTF text if font is loaded, otherwise fallback to SDL2_gfx */
        if (NULL != p_ttf) {
            SDL_Color    fg_col;
            SDL_Surface *p_surf;

            fg_col.r = p_text->Color.b.r;
            fg_col.g = p_text->Color.b.g;
            fg_col.b = p_text->Color.b.b;
            fg_col.a = p_text->Color.b.a;

            p_surf = TTF_RenderUTF8_Blended(p_ttf, line_buf[idx], fg_col);
            if (NULL != p_surf) {
                SDL_Texture *p_tex = SDL_CreateTextureFromSurface(p_ren, p_surf);
                if (NULL != p_tex) {
                    SDL_Rect dst;
                    dst.x = draw_x;
                    dst.y = draw_y;
                    dst.w = p_surf->w;
                    dst.h = p_surf->h;
                    (void)SDL_SetTextureAlphaMod(p_tex, fg_col.a);
                    (void)SDL_RenderCopy(p_ren, p_tex, NULL, &dst);
                    SDL_DestroyTexture(p_tex);
                }
                SDL_FreeSurface(p_surf);
            }
        } else {
            (void)stringRGBA(p_ren,
                             (int16_t)draw_x,
                             (int16_t)draw_y,
                             line_buf[idx],
                             p_text->Color.b.r, p_text->Color.b.g,
                             p_text->Color.b.b, p_text->Color.b.a);
        }
    }

    /* Return success after drawing multi-line aligned text */
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
 * @note Skips rendering if IsVisible is 0; rotates component + children around center when Rotation != 0
 */
Err_t DrawComponent(const Comp_t *p_comp) {
    Err_t         err;
    uint32_t      ext_idx;
    SDL_Renderer *p_ren;
    SDL_Texture  *p_comp_tex       = NULL;
    SDL_Texture  *p_prev_tgt       = NULL;
    SDL_Rect      prev_clip;
    SDL_bool      had_clip         = SDL_FALSE;
    int32_t       prev_origin_x    = 0;
    int32_t       prev_origin_y    = 0;
    uint8_t       prev_in_comp_rot = 0U;
    uint16_t      comp_rot         = 0U;

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

    p_ren    = (SDL_Renderer *)g_MainWindow.p_SdlRenderer;
    comp_rot = (uint16_t)(p_comp->Type.Rect.Dim.Rotation % 360U);

    /* If component has non-zero rotation and valid W/H, render component + children into a target texture */
    if ((NULL != p_ren) &&
        (0U != comp_rot) &&
        (p_comp->Type.Rect.Dim.W > 0U) &&
        (p_comp->Type.Rect.Dim.H > 0U)) {
        p_comp_tex = SDL_CreateTexture(p_ren,
                                       SDL_PIXELFORMAT_ARGB8888,
                                       SDL_TEXTUREACCESS_TARGET,
                                       (int)p_comp->Type.Rect.Dim.W,
                                       (int)p_comp->Type.Rect.Dim.H);
        if (NULL != p_comp_tex) {
            (void)SDL_SetTextureBlendMode(p_comp_tex, SDL_BLENDMODE_BLEND);
            p_prev_tgt       = SDL_GetRenderTarget(p_ren);
            had_clip         = SDL_RenderIsClipEnabled(p_ren);
            if (SDL_TRUE == had_clip) {
                SDL_RenderGetClipRect(p_ren, &prev_clip);
            }
            prev_origin_x    = g_LayerOriginX;
            prev_origin_y    = g_LayerOriginY;
            prev_in_comp_rot = g_InCompRotationTarget;

            (void)SDL_SetRenderTarget(p_ren, p_comp_tex);
            (void)SDL_SetRenderDrawColor(p_ren, 0U, 0U, 0U, 0U);
            (void)SDL_RenderClear(p_ren);

            /* Shift origin so component top-left maps to (0, 0) inside p_comp_tex */
            g_LayerOriginX         = -p_comp->Type.Rect.Dim.OffsetX;
            g_LayerOriginY         = -p_comp->Type.Rect.Dim.OffsetY;
            g_InCompRotationTarget = 1U;
        }
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
            if (NULL != p_comp_tex) {
                (void)SDL_SetRenderTarget(p_ren, p_prev_tgt);
                if (SDL_TRUE == had_clip) {
                    (void)SDL_RenderSetClipRect(p_ren, &prev_clip);
                }
                g_LayerOriginX         = prev_origin_x;
                g_LayerOriginY         = prev_origin_y;
                g_InCompRotationTarget = prev_in_comp_rot;
                SDL_DestroyTexture(p_comp_tex);
            }
            APP_ERR("Unsupported component CastType!");
            return APP_ERROR_INVALID_PARAM;
    }

    /* Check if primary component draw failed */
    if (APP_SUCCESS != err) {
        if (NULL != p_comp_tex) {
            (void)SDL_SetRenderTarget(p_ren, p_prev_tgt);
            if (SDL_TRUE == had_clip) {
                (void)SDL_RenderSetClipRect(p_ren, &prev_clip);
            }
            g_LayerOriginX         = prev_origin_x;
            g_LayerOriginY         = prev_origin_y;
            g_InCompRotationTarget = prev_in_comp_rot;
            SDL_DestroyTexture(p_comp_tex);
        }
        APP_ERR("Failed to draw primary component!");
        return err;
    }

    /* Automatically draw centered button label if Class is COMP_CLASS_BUTTON and p_ExtComps is NULL */
    if ((COMP_CLASS_BUTTON == p_comp->Class) &&
        (COMP_CAST_RECT == p_comp->CastType) &&
        (NULL == p_comp->p_ExtComps) &&
        (NULL != p_comp->Label)) {
        CompTextCfg_t btn_txt;
        btn_txt.Color.w  = 0xFFFFFFFFU;
        btn_txt.Dim      = p_comp->Type.Rect.Dim;
        btn_txt.TextStr  = p_comp->Label;
        btn_txt.FontName = NULL;
        btn_txt.FontSize = 12U;
        btn_txt.Align    = TEXT_ALIGN_CENTER;
        btn_txt.VAlign   = TEXT_VALIGN_MIDDLE;
        btn_txt.IsWrap   = 1U;
        /* Render centered and wrapped button label text */
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

    /* Restore render target and draw rotated component texture if offscreen target was used */
    if (NULL != p_comp_tex) {
        SDL_Rect dst_rect;
        dst_rect.x = prev_origin_x + p_comp->Type.Rect.Dim.OffsetX;
        dst_rect.y = prev_origin_y + p_comp->Type.Rect.Dim.OffsetY;
        dst_rect.w = (int)p_comp->Type.Rect.Dim.W;
        dst_rect.h = (int)p_comp->Type.Rect.Dim.H;

        (void)SDL_SetRenderTarget(p_ren, p_prev_tgt);
        if (SDL_TRUE == had_clip) {
            (void)SDL_RenderSetClipRect(p_ren, &prev_clip);
        }
        g_LayerOriginX         = prev_origin_x;
        g_LayerOriginY         = prev_origin_y;
        g_InCompRotationTarget = prev_in_comp_rot;

        (void)SDL_RenderCopyEx(p_ren, p_comp_tex, NULL, &dst_rect, (double)comp_rot, NULL, SDL_FLIP_NONE);
        SDL_DestroyTexture(p_comp_tex);
    }

    /* Return success after drawing component and its children */
    return APP_SUCCESS;
}

/* Render all components inside a layer using MainWindow RectClip and Layer (0,0) component origin
 * @param p_layer[in] Pointer to layer to render
 * @return APP_SUCCESS on success, or error code
 * @note Layer RectClip is relative to MainWindow (0,0) and supports 0-360 deg Rotation; components are relative to Layer (0,0)
 */
Err_t DrawLayer(const Layer_t *p_layer) {
    uint32_t      comp_idx;
    SDL_Renderer *p_ren;
    uint8_t       has_rect_clip = 0U;
    uint16_t      layer_rot     = 0U;

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

    p_ren     = (SDL_Renderer *)g_MainWindow.p_SdlRenderer;
    layer_rot = (uint16_t)(p_layer->RectClip.Rotation % 360U);

    /* Assert that component array pointer is valid when CompCount > 0 */
    if (p_layer->CompCount > 0U) {
        ASSERT_NOT_EQUAL(NULL, p_layer->p_Comps);
    }

    /* If Layer RectClip has non-zero rotation, render layer into an offscreen target texture and rotate it */
    if ((NULL != p_ren) && (0U != layer_rot)) {
        int          tex_w;
        int          tex_h;
        SDL_Texture *p_layer_tex;
        SDL_Texture *p_prev_tgt;

        tex_w = (p_layer->RectClip.W > 0U) ? (int)p_layer->RectClip.W : (int)g_MainWindow.Width;
        tex_h = (p_layer->RectClip.H > 0U) ? (int)p_layer->RectClip.H : (int)g_MainWindow.Height;

        p_layer_tex = SDL_CreateTexture(p_ren,
                                        SDL_PIXELFORMAT_ARGB8888,
                                        SDL_TEXTUREACCESS_TARGET,
                                        tex_w,
                                        tex_h);
        if (NULL != p_layer_tex) {
            SDL_Rect dst_rect;

            (void)SDL_SetTextureBlendMode(p_layer_tex, SDL_BLENDMODE_BLEND);
            p_prev_tgt = SDL_GetRenderTarget(p_ren);
            (void)SDL_SetRenderTarget(p_ren, p_layer_tex);
            (void)SDL_SetRenderDrawColor(p_ren, 0U, 0U, 0U, 0U);
            (void)SDL_RenderClear(p_ren);

            /* Inside offscreen texture, Layer (0,0) is shifted only by ScrollX/Y */
            g_LayerOriginX = p_layer->ScrollX;
            g_LayerOriginY = p_layer->ScrollY;

            /* Render all components in the layer into the offscreen texture */
            for (comp_idx = 0U; comp_idx < p_layer->CompCount; comp_idx++) {
                (void)DrawComponent(&p_layer->p_Comps[comp_idx]);
            }

            /* Draw rounded scrollable viewport border inside offscreen texture so it rotates with the layer */
            if ((0U != p_layer->IsScrollable) &&
                (p_layer->RectClip.W > 0U) &&
                (p_layer->RectClip.H > 0U)) {
                (void)roundedRectangleRGBA(p_ren,
                                           0,
                                           0,
                                           (int16_t)(tex_w - 1),
                                           (int16_t)(tex_h - 1),
                                           8,
                                           0U, 180U, 216U, 255U);
            }

            /* Restore main render target and composite rotated layer viewport */
            (void)SDL_SetRenderTarget(p_ren, p_prev_tgt);

            dst_rect.x = p_layer->RectClip.OffsetX;
            dst_rect.y = p_layer->RectClip.OffsetY;
            dst_rect.w = tex_w;
            dst_rect.h = tex_h;
            (void)SDL_RenderCopyEx(p_ren,
                                   p_layer_tex,
                                   NULL,
                                   &dst_rect,
                                   (double)layer_rot,
                                   NULL,
                                   SDL_FLIP_NONE);
            SDL_DestroyTexture(p_layer_tex);

            g_LayerOriginX = 0;
            g_LayerOriginY = 0;
            /* Return after rendering rotated layer */
            return APP_SUCCESS;
        }
    }

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

    /* Iterate through all components in the layer to render them in order */
    for (comp_idx = 0U; comp_idx < p_layer->CompCount; comp_idx++) {
        /* Draw current component relative to Layer (0,0) */
        (void)DrawComponent(&p_layer->p_Comps[comp_idx]);
    }

    /* Reset clipping and draw a rounded frame around scrollable RectClip in MainWindow coords */
    if ((0U != has_rect_clip) && (NULL != p_ren)) {
        (void)SDL_RenderSetClipRect(p_ren, NULL);
        if (0U != p_layer->IsScrollable) {
            (void)roundedRectangleRGBA(p_ren,
                                       (int16_t)p_layer->RectClip.OffsetX,
                                       (int16_t)p_layer->RectClip.OffsetY,
                                       (int16_t)(p_layer->RectClip.OffsetX + (int32_t)p_layer->RectClip.W - 1),
                                       (int16_t)(p_layer->RectClip.OffsetY + (int32_t)p_layer->RectClip.H - 1),
                                       8,
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
