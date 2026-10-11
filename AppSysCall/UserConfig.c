/* file-desc **************************************************************************************
 * @filename    UserConfig.c
 * @brief       Implementation of YAML configuration loader and writer for disp.yml and app.yml.
 * @note        Supports -1/empty defaults and automatic closest/maximum mode resolution.
 **************************************************************************************************/

/* headers ****************************************************************************************
 **************************************************************************************************/

#include "./__common__.h"
#include "./UserConfig.h"
#include "./Exec.h"
#include <pthread.h>
#include <strings.h>

/* macros *****************************************************************************************
 **************************************************************************************************/

#define USER_CFG_FILE_BUF_LEN           (65536U)

/* definitions ************************************************************************************
 **************************************************************************************************/

/* Payload passed to detached background thread when executing ApplyCallBack commands */
struct ApplyCallbackJob_st {
    uint32_t CmdNum;
    char     Cmds[SYSCALL_MAX_APPLY_CMDS][SYSCALL_LINE_LEN];
};
typedef struct ApplyCallbackJob_st ApplyCallbackJob_t;

/* exported variables *****************************************************************************
 **************************************************************************************************/

/* public variables *******************************************************************************
 **************************************************************************************************/

/* private variables ******************************************************************************
 **************************************************************************************************/

static char g_YamlFileBuf[USER_CFG_FILE_BUF_LEN];

/* exported functions *****************************************************************************
 **************************************************************************************************/

/* public functions *******************************************************************************
 **************************************************************************************************/

Err_t                   SysCallDispPreferInitDefault(DispPreferConfig_t *p_pref, uint32_t count);
Err_t                   SysCallDispPreferLoad(const_str_t path, DispPreferConfig_t *p_pref);
Err_t                   SysCallDispPreferSave(const_str_t path, const DispPreferConfig_t *p_pref);
const DispPreferItem_t *SysCallDispPreferFindMatch(const DispPreferConfig_t *p_pref,
                                                   const_str_t               label,
                                                   uint32_t                  fallback_idx);
Err_t                   SysCallDispPreferApplyToMonitors(const DispPreferConfig_t *p_pref,
                                                         const MonitorList_t      *p_mon_list,
                                                         MonitorCfgList_t         *p_out_cfg);
Err_t                   SysCallAppConfigLoad(const_str_t path, AppYamlConfig_t *p_app_cfg);
Err_t                   SysCallAppRunApplyCallbacks(const AppYamlConfig_t *p_app_cfg);

/* private functions ******************************************************************************
 **************************************************************************************************/

static char   *TrimLeading(char *p_str);
static void    StripCommentAndNewline(char *p_line);
static void    UnescapeYamlString(char *p_str);
static int     ParseOptionalInt32(const char *p_val_str, int32_t *p_out_val);
static uint8_t ParseHorizAlign(const char *p_val_str);
static uint8_t ParseVertAlign(const char *p_val_str);
static void   *ApplyCallbackWorkerEntry(void *p_arg);

/* function implementations ***********************************************************************
 **************************************************************************************************/

/* Advance pointer past leading whitespace and YAML list hyphens
 * @param p_str[in] Input string pointer
 * @return Pointer to first key character
 */
static char *TrimLeading(char *p_str) {
    /* Skip spaces and tabs */
    while (('\0' != *p_str) && ((' ' == *p_str) || ('\t' == *p_str))) {
        p_str++;
    }
    /* Skip leading YAML list marker "- " if present */
    if ('-' == *p_str) {
        p_str++;
        while (('\0' != *p_str) && ((' ' == *p_str) || ('\t' == *p_str))) {
            p_str++;
        }
    }
    /* Return trimmed pointer */
    return p_str;
}

/* Strip inline '#' comments (outside double quotes) and trailing CR/LF characters from a line
 * @param p_line[in/out] Mutable line string
 */
static void StripCommentAndNewline(char *p_line) {
    uint8_t in_quotes = 0U;
    char   *p_cur     = p_line;
    size_t  len;

    /* Scan line and terminate at first '#' that is not inside double quotes */
    while ('\0' != *p_cur) {
        if ('"' == *p_cur) {
            in_quotes = (0U == in_quotes) ? 1U : 0U;
        } else if (('#') == *p_cur && (0U == in_quotes)) {
            *p_cur = '\0';
            /* Break once unquoted comment start is truncated */
            break;
        } else {
            /* Keep normal character */
        }
        p_cur++;
    }

    len = strlen(p_line);
    /* Trim trailing whitespace, CR, and LF */
    while ((len > 0U) &&
           ((' ' == p_line[len - 1U]) ||
            ('\t' == p_line[len - 1U]) ||
            ('\r' == p_line[len - 1U]) ||
            ('\n' == p_line[len - 1U]))) {
        p_line[len - 1U] = '\0';
        len--;
    }

    /* Return after stripping comment and trailing whitespace */
    return;
}

/* Convert escaped "\\n" and "\\t" sequences in a YAML string into actual newline/tab characters
 * @param p_str[in/out] Mutable string to unescape in-place
 */
static void UnescapeYamlString(char *p_str) {
    char *p_src = p_str;
    char *p_dst = p_str;

    /* Walk string and collapse backslash escape sequences */
    while ('\0' != *p_src) {
        if (('\\' == p_src[0]) && ('n' == p_src[1])) {
            *p_dst++ = '\n';
            p_src   += 2;
        } else if (('\\' == p_src[0]) && ('t' == p_src[1])) {
            *p_dst++ = '\t';
            p_src   += 2;
        } else {
            *p_dst++ = *p_src++;
        }
    }
    *p_dst = '\0';

    /* Return after unescaping string in-place */
    return;
}

/* Parse an integer value after ':' in a YAML line; empty string defaults to -1
 * @param p_val_str[in] Value substring after ':'
 * @param p_out_val[out] Parsed integer value (-1 if empty)
 * @return 1 if parsed or empty, 0 on syntax error
 */
static int ParseOptionalInt32(const char *p_val_str, int32_t *p_out_val) {
    char *p_end = NULL;
    long  val;

    /* Skip leading whitespace */
    while (('\0' != *p_val_str) && ((' ' == *p_val_str) || ('\t' == *p_val_str))) {
        p_val_str++;
    }

    /* Empty value means default -1 per disp.yml specification */
    if ('\0' == *p_val_str) {
        *p_out_val = -1;
        /* Return 1 for valid empty default */
        return 1;
    }

    val = strtol(p_val_str, &p_end, 10);
    if (p_end == p_val_str) {
        /* Return 0 when no digits were found */
        return 0;
    }

    *p_out_val = (int32_t)val;
    /* Return 1 after parsing integer */
    return 1;
}

/* Parse horizontal alignment keyword ("Left", "Center", "Middle", "Right") into 0/1/2
 * @param p_val_str[in] Alignment string from YAML
 * @return 0 for LEFT, 1 for CENTER, 2 for RIGHT
 */
static uint8_t ParseHorizAlign(const char *p_val_str) {
    /* Map Center/Middle -> 1U, Right -> 2U, default Left -> 0U */
    if ((0 == strcasecmp(p_val_str, "CENTER")) || (0 == strcasecmp(p_val_str, "MIDDLE"))) {
        /* Return CENTER alignment enum value */
        return 1U;
    }
    if (0 == strcasecmp(p_val_str, "RIGHT")) {
        /* Return RIGHT alignment enum value */
        return 2U;
    }

    /* Default to LEFT alignment */
    return 0U;
}

/* Parse vertical alignment keyword ("Top", "Center", "Middle", "Bottom") into 0/1/2
 * @param p_val_str[in] Vertical alignment string from YAML
 * @return 0 for TOP, 1 for MIDDLE/CENTER, 2 for BOTTOM
 */
static uint8_t ParseVertAlign(const char *p_val_str) {
    /* Map Center/Middle -> 1U, Bottom -> 2U, default Top -> 0U */
    if ((0 == strcasecmp(p_val_str, "CENTER")) || (0 == strcasecmp(p_val_str, "MIDDLE"))) {
        /* Return MIDDLE vertical alignment enum value */
        return 1U;
    }
    if (0 == strcasecmp(p_val_str, "BOTTOM")) {
        /* Return BOTTOM vertical alignment enum value */
        return 2U;
    }

    /* Default to TOP vertical alignment */
    return 0U;
}

/* Initialize a DispPreferConfig_t structure with all fields set to -1
 * @param p_pref[out] Pointer to preference structure
 * @param count[in] Number of display slots to initialize
 * @return APP_SUCCESS on success, or error code
 */
Err_t SysCallDispPreferInitDefault(DispPreferConfig_t *p_pref, uint32_t count) {
    uint32_t idx;

    #if (1U == PARAM_CHECK_EN)
    /* Validate pointer and count */
    if (NULL == p_pref) {
        APP_ERR("p_pref is NULL!");
        return APP_ERROR_NULL;
    }
    if (count > SYSCALL_MAX_MONITORS) {
        APP_ERR("count exceeds SYSCALL_MAX_MONITORS!");
        return APP_ERROR_INVALID_PARAM;
    }
    #endif /* (1U == PARAM_CHECK_EN) */

    (void)memset(p_pref, 0, sizeof(*p_pref));
    p_pref->Count = count;

    /* Set all child fields to -1 by default */
    for (idx = 0U; idx < SYSCALL_MAX_MONITORS; idx++) {
        p_pref->Disp[idx].Label[0] = '\0';
        p_pref->Disp[idx].Enabled  = -1;
        p_pref->Disp[idx].Primary  = -1;
        p_pref->Disp[idx].W        = -1;
        p_pref->Disp[idx].H        = -1;
        p_pref->Disp[idx].Freq     = -1;
        p_pref->Disp[idx].PosX     = -1;
        p_pref->Disp[idx].PosY     = -1;
    }

    /* Return success after default initialization */
    return APP_SUCCESS;
}

/* Load DisplayPreferSettings from a disp.yml file
 * @param path[in] Path to disp.yml
 * @param p_pref[out] Loaded display preferences
 * @return APP_SUCCESS on success, or error code if file cannot be read
 */
Err_t SysCallDispPreferLoad(const_str_t path, DispPreferConfig_t *p_pref) {
    Err_t    err;
    char    *p_save    = NULL;
    char    *p_line;
    int32_t  cur_disp  = -1;
    uint32_t max_disp  = 0U;

    #if (1U == PARAM_CHECK_EN)
    /* Validate input pointers */
    if ((NULL == path) || (NULL == p_pref)) {
        APP_ERR("Null pointer in SysCallDispPreferLoad!");
        return APP_ERROR_NULL;
    }
    #endif /* (1U == PARAM_CHECK_EN) */

    /* Initialize all slots to -1 first */
    (void)SysCallDispPreferInitDefault(p_pref, 3U);

    err = SysCallReadFile(path, g_YamlFileBuf, sizeof(g_YamlFileBuf), NULL);
    if (APP_SUCCESS != err) {
        APP_ERR("Failed to read disp.yml from %s", path);
        return err;
    }

    /* Parse each line of disp.yml */
    for (p_line = strtok_r(g_YamlFileBuf, "\n", &p_save);
         NULL != p_line;
         p_line = strtok_r(NULL, "\n", &p_save)) {
        char  line_copy[SYSCALL_LINE_LEN];
        char *p_key;
        char *p_colon;

        (void)snprintf(line_copy, sizeof(line_copy), "%s", p_line);
        StripCommentAndNewline(line_copy);
        p_key = TrimLeading(line_copy);

        /* Skip empty or comment-only lines */
        if ('\0' == *p_key) {
            /* Continue to next line */
            continue;
        }

        p_colon = strchr(p_key, ':');
        if (NULL == p_colon) {
            /* Continue if line has no key-value colon */
            continue;
        }

        *p_colon = '\0';
        /* Check if line starts a new Disp<N> block */
        if (0 == strncmp(p_key, "Disp", 4U)) {
            int idx = atoi(&p_key[4]);
            if ((idx >= 0) && ((uint32_t)idx < SYSCALL_MAX_MONITORS)) {
                cur_disp = idx;
                if (((uint32_t)idx + 1U) > max_disp) {
                    max_disp = (uint32_t)idx + 1U;
                }
            }
        } else if ((cur_disp >= 0) && ((uint32_t)cur_disp < SYSCALL_MAX_MONITORS)) {
            int32_t           val    = -1;
            DispPreferItem_t *p_item = &p_pref->Disp[cur_disp];

            /* Parse string Label or integer child property of current Disp<N> */
            if (0 == strcmp(p_key, "Label")) {
                char *p_val = p_colon + 1;
                while (('\0' != *p_val) && ((' ' == *p_val) || ('\t' == *p_val))) {
                    p_val++;
                }
                if ('"' == *p_val) {
                    size_t vlen;
                    p_val++;
                    vlen = strlen(p_val);
                    if ((vlen > 0U) && ('"' == p_val[vlen - 1U])) {
                        p_val[vlen - 1U] = '\0';
                    }
                }
                (void)snprintf(p_item->Label, sizeof(p_item->Label), "%s", p_val);
            } else if (0 != ParseOptionalInt32(p_colon + 1, &val)) {
                if (0 == strcmp(p_key, "Enabled")) {
                    p_item->Enabled = val;
                } else if (0 == strcmp(p_key, "Primary")) {
                    p_item->Primary = val;
                } else if (0 == strcmp(p_key, "W")) {
                    p_item->W = val;
                } else if (0 == strcmp(p_key, "H")) {
                    p_item->H = val;
                } else if (0 == strcmp(p_key, "Freq")) {
                    p_item->Freq = val;
                } else if (0 == strcmp(p_key, "PosX")) {
                    p_item->PosX = val;
                } else if (0 == strcmp(p_key, "PosY")) {
                    p_item->PosY = val;
                } else {
                    /* Ignore unknown key */
                }
            } else {
                /* Ignore non-integer value on numeric field */
            }
        } else {
            /* Ignore top-level keys such as DisplayPreferSettings */
        }
    }

    if (max_disp > p_pref->Count) {
        p_pref->Count = max_disp;
    }

    /* Return success after loading disp.yml */
    return APP_SUCCESS;
}

/* Save DisplayPreferSettings back to a disp.yml file with RC-style documentation comments
 * @param path[in] Destination disp.yml path
 * @param p_pref[in] Display preferences to serialize
 * @return APP_SUCCESS on success, or error code
 */
Err_t SysCallDispPreferSave(const_str_t path, const DispPreferConfig_t *p_pref) {
    uint32_t idx;
    uint32_t used = 0U;
    int      n;

    #if (1U == PARAM_CHECK_EN)
    /* Validate pointers */
    if ((NULL == path) || (NULL == p_pref)) {
        APP_ERR("Null pointer in SysCallDispPreferSave!");
        return APP_ERROR_NULL;
    }
    #endif /* (1U == PARAM_CHECK_EN) */

    n = snprintf(g_YamlFileBuf, sizeof(g_YamlFileBuf),
                 "# =============================================================================\n"
                 "# MonitorSelection - Display Preference Configuration (disp.yml)\n"
                 "# =============================================================================\n"
                 "# CAN I EDIT VALUES? -> YES! Edit any field below; loaded on startup.\n"
                 "# CAN I ADD ITEMS?   -> YES! Up to Disp0 .. Disp7 (SYSCALL_MAX_MONITORS = 8).\n"
                 "# NOTE: Clicking 'Apply' in the UI automatically saves current settings here.\n"
                 "#\n"
                 "# Quick Property Reference (RC-style examples):\n"
                 "#   - Label   : Connector name (e.g. \"eDP-1\", \"DP-2\", \"HDMI-1\"), or \"\" = auto\n"
                 "#   - Enabled : 1 = ON (active), 0 = OFF (--off, black card), -1/empty = auto ON\n"
                 "#   - Primary : 1 = Primary (--primary), 0 = Non-primary, -1/empty = auto primary\n"
                 "#   - W / H   : Resolution in px (e.g. 1920 / 1080), or -1/empty = auto MAX mode\n"
                 "#   - Freq    : Refresh rate in Hz (e.g. 60, 100, 144), or -1/empty = auto MAX Hz\n"
                 "#   - PosX/Y  : Real xrandr offset in px from Origin (0,0), or -1/empty = auto\n"
                 "# =============================================================================\n\n"
                 "DisplayPreferSettings:\n");
    if ((n < 0) || ((uint32_t)n >= sizeof(g_YamlFileBuf))) {
        APP_ERR("Buffer overflow while formatting disp.yml header!");
        return APP_ERROR;
    }
    used = (uint32_t)n;

    /* Format each Disp<N> entry with concise inline guidance */
    for (idx = 0U; idx < p_pref->Count; idx++) {
        const DispPreferItem_t *p_it = &p_pref->Disp[idx];

        n = snprintf(&g_YamlFileBuf[used], sizeof(g_YamlFileBuf) - used,
                     "    # --- Monitor Slot #%u (Disp%u) [EDITABLE: YES] ---\n"
                     "    # Example: Label: \"eDP-1\" | Set W: -1, H: -1, Freq: -1 for auto MAX mode\n"
                     "    - Disp%u:\n"
                     "        - Label:   \"%s\"\n"
                     "        - Enabled: %d\n"
                     "        - Primary: %d\n"
                     "        - W:       %d\n"
                     "        - H:       %d\n"
                     "        - Freq:    %d\n"
                     "        - PosX:    %d\n"
                     "        - PosY:    %d\n",
                     idx, idx, idx,
                     p_it->Label,
                     (int)p_it->Enabled,
                     (int)p_it->Primary,
                     (int)p_it->W,
                     (int)p_it->H,
                     (int)p_it->Freq,
                     (int)p_it->PosX,
                     (int)p_it->PosY);
        if ((n < 0) || ((uint32_t)n >= (sizeof(g_YamlFileBuf) - used))) {
            APP_ERR("Buffer overflow while formatting Disp%u!", idx);
            return APP_ERROR;
        }
        used += (uint32_t)n;
    }

    /* Write formatted YAML content atomically to disk */
    return SysCallWriteFile(path, g_YamlFileBuf);
}

/* Find the preferred display item matching a monitor connector Label, or fall back to unnamed slot
 * @param p_pref[in] Loaded display preference configuration
 * @param label[in] Connected monitor label (e.g. "eDP-1", "DP-2", "HDMI-1")
 * @param fallback_idx[in] Fallback slot index if slot has no Label specified
 * @return Pointer to matching DispPreferItem_t, or NULL (auto -1 defaults) if no match
 */
const DispPreferItem_t *SysCallDispPreferFindMatch(const DispPreferConfig_t *p_pref,
                                                   const_str_t               label,
                                                   uint32_t                  fallback_idx) {
    uint32_t k;

    /* Validate preference structure pointer */
    if (NULL == p_pref) {
        /* Return NULL when preference config is NULL */
        return NULL;
    }

    /* 1. Priority match: search for an entry whose Label exactly matches the monitor's connector name */
    if ((NULL != label) && ('\0' != label[0])) {
        for (k = 0U; k < p_pref->Count; k++) {
            if (('\0' != p_pref->Disp[k].Label[0]) &&
                (0 == strcmp(p_pref->Disp[k].Label, label))) {
                /* Return exact Label match */
                return &p_pref->Disp[k];
            }
        }
    }

    /* 2. Fallback match: only use fallback_idx if its Label was left empty ("" = wildcard slot) */
    if ((fallback_idx < p_pref->Count) && ('\0' == p_pref->Disp[fallback_idx].Label[0])) {
        /* Return unnamed wildcard slot at fallback_idx */
        return &p_pref->Disp[fallback_idx];
    }

    /* 3. No match found: return NULL so caller applies safe Auto (-1) defaults (ON + Max WxH@Freq) */
    return NULL;
}

/* Resolve DisplayPreferSettings against a MonitorList_t to produce a valid MonitorCfgList_t
 * @param p_pref[in] User preferred settings
 * @param p_mon_list[in] Connected monitors and their supported modes
 * @param p_out_cfg[out] Resolved monitor configurations ready for UI or xrandr
 * @return APP_SUCCESS on success, or error code
 */
Err_t SysCallDispPreferApplyToMonitors(const DispPreferConfig_t *p_pref,
                                       const MonitorList_t      *p_mon_list,
                                       MonitorCfgList_t         *p_out_cfg) {
    uint32_t idx;
    uint8_t  has_primary = 0U;
    int32_t  auto_x      = 0;

    #if (1U == PARAM_CHECK_EN)
    /* Validate input and output pointers */
    if ((NULL == p_pref) || (NULL == p_mon_list) || (NULL == p_out_cfg)) {
        APP_ERR("Null pointer in SysCallDispPreferApplyToMonitors!");
        return APP_ERROR_NULL;
    }
    #endif /* (1U == PARAM_CHECK_EN) */

    (void)memset(p_out_cfg, 0, sizeof(*p_out_cfg));
    p_out_cfg->Count = p_mon_list->Count;

    /* Match each connected monitor by Label (or fallback index) in p_pref */
    for (idx = 0U; idx < p_mon_list->Count; idx++) {
        const ConnectedMonitor_t *p_mon   = &p_mon_list->Monitors[idx];
        const DispPreferItem_t   *p_match = SysCallDispPreferFindMatch(p_pref, p_mon->Label, idx);
        MonitorCfg_t             *p_cfg   = &p_out_cfg->Cfgs[idx];
        uint16_t                  m_idx   = 0U;
        uint16_t                  freq    = 60U;
        int32_t                   want_e  = -1;
        int32_t                   want_p  = -1;
        int32_t                   want_w  = -1;
        int32_t                   want_h  = -1;
        int32_t                   want_f  = -1;
        int32_t                   pos_x   = -1;
        int32_t                   pos_y   = -1;

        if (NULL != p_match) {
            want_e = p_match->Enabled;
            want_p = p_match->Primary;
            want_w = p_match->W;
            want_h = p_match->H;
            want_f = p_match->Freq;
            pos_x  = p_match->PosX;
            pos_y  = p_match->PosY;
        }

        (void)snprintf(p_cfg->Label, sizeof(p_cfg->Label), "%s", p_mon->Label);
        p_cfg->IsEnabled = (0 == want_e) ? 0U : 1U;
        if ((0U != p_cfg->IsEnabled) && (0U == has_primary) &&
            ((1 == want_p) || ((want_p < 0) && (0U != p_mon->IsPrimary)))) {
            p_cfg->IsPrimary = 1U;
            has_primary      = 1U;
        } else {
            p_cfg->IsPrimary = 0U;
        }

        if (APP_SUCCESS == SysCallXrandrFindClosestMode(p_mon, want_w, want_h, want_f, &m_idx, &freq)) {
            p_cfg->W    = p_mon->Displays[m_idx].W;
            p_cfg->H    = p_mon->Displays[m_idx].H;
            p_cfg->Freq = freq;
        } else {
            p_cfg->W    = (p_mon->CurW > 0U) ? p_mon->CurW : 1920U;
            p_cfg->H    = (p_mon->CurH > 0U) ? p_mon->CurH : 1080U;
            p_cfg->Freq = (p_mon->CurFreq > 0U) ? p_mon->CurFreq : 60U;
        }

        p_cfg->PosX = (pos_x >= 0) ? pos_x : auto_x;
        p_cfg->PosY = (pos_y >= 0) ? pos_y : 0;
        if (0U != p_cfg->IsEnabled) {
            auto_x = p_cfg->PosX + (int32_t)p_cfg->W;
        }
    }

    /* Guarantee at least one enabled monitor is marked Primary */
    if (0U == has_primary) {
        for (idx = 0U; idx < p_out_cfg->Count; idx++) {
            if (0U != p_out_cfg->Cfgs[idx].IsEnabled) {
                p_out_cfg->Cfgs[idx].IsPrimary = 1U;
                /* Break once first enabled monitor is marked primary */
                break;
            }
        }
    }

    /* Return success after resolving preferences */
    return APP_SUCCESS;
}

/* Load top-level application configuration, UI component overrides, and ApplyCallBack from app.yml
 * @param path[in] Path to app.yml
 * @param p_app_cfg[out] Parsed application settings
 * @return APP_SUCCESS on success, or error code
 */
Err_t SysCallAppConfigLoad(const_str_t path, AppYamlConfig_t *p_app_cfg) {
    Err_t   err;
    char   *p_save       = NULL;
    char   *p_line;
    uint8_t in_main_win  = 0U;
    uint8_t in_layers    = 0U;
    uint8_t in_apply_cb  = 0U;
    uint8_t in_sub_block = 0U;
    int32_t cur_layer    = -1;
    int32_t cur_comp     = -1;

    #if (1U == PARAM_CHECK_EN)
    /* Validate pointers */
    if ((NULL == path) || (NULL == p_app_cfg)) {
        APP_ERR("Null pointer in SysCallAppConfigLoad!");
        return APP_ERROR_NULL;
    }
    #endif /* (1U == PARAM_CHECK_EN) */

    /* Initialize sensible defaults before parsing file */
    (void)memset(p_app_cfg, 0, sizeof(*p_app_cfg));
    (void)snprintf(p_app_cfg->PreferDispPath, sizeof(p_app_cfg->PreferDispPath), "%s", USER_CFG_DEFAULT_DISP_PATH);
    p_app_cfg->SnapThresholdPx = 25U;
    (void)snprintf(p_app_cfg->WinTitle, sizeof(p_app_cfg->WinTitle), "MonitorSelection");
    p_app_cfg->WinW            = 980U;
    p_app_cfg->WinH            = 540U;
    p_app_cfg->WinIsVisible    = 1U;
    p_app_cfg->WinIsMaximized  = 0U;
    p_app_cfg->ApplyCmdNum     = 0U;

    err = SysCallReadFile(path, g_YamlFileBuf, sizeof(g_YamlFileBuf), NULL);
    if (APP_SUCCESS != err) {
        APP_WARN("Could not read %s, using built-in defaults.", path);
        return err;
    }

    /* Scan lines of app.yml for App, MainWindow, Layers/Comps, and ApplyCallBack settings */
    for (p_line = strtok_r(g_YamlFileBuf, "\n", &p_save);
         NULL != p_line;
         p_line = strtok_r(NULL, "\n", &p_save)) {
        char  line_copy[SYSCALL_LINE_LEN];
        char *p_key;
        char *p_colon;
        char *p_val;

        (void)snprintf(line_copy, sizeof(line_copy), "%s", p_line);
        StripCommentAndNewline(line_copy);
        p_key = TrimLeading(line_copy);
        if ('\0' == *p_key) {
            /* Skip blank line */
            continue;
        }

        p_colon = strchr(p_key, ':');
        if (NULL == p_colon) {
            /* Skip line without colon */
            continue;
        }
        *p_colon = '\0';
        p_val    = p_colon + 1;
        while (('\0' != *p_val) && ((' ' == *p_val) || ('\t' == *p_val))) {
            p_val++;
        }
        /* Strip surrounding double quotes if present */
        if ('"' == *p_val) {
            size_t vlen;
            p_val++;
            vlen = strlen(p_val);
            if ((vlen > 0U) && ('"' == p_val[vlen - 1U])) {
                p_val[vlen - 1U] = '\0';
            }
        }

        if (0 == strcmp(p_key, "MainWindow")) {
            in_main_win  = 1U;
            in_layers    = 0U;
            in_apply_cb  = 0U;
        } else if (0 == strcmp(p_key, "Layers")) {
            in_layers    = 1U;
            in_apply_cb  = 0U;
            cur_layer    = -1;
            cur_comp     = -1;
            in_sub_block = 0U;
        } else if (0 == strcmp(p_key, "ApplyCallBack")) {
            in_apply_cb  = 1U;
            in_main_win  = 0U;
            in_layers    = 0U;
        } else if (0 == strcmp(p_key, "PreferDispPath")) {
            (void)snprintf(p_app_cfg->PreferDispPath, sizeof(p_app_cfg->PreferDispPath), "%s", p_val);
        } else if (0 == strcmp(p_key, "SnapThresholdPx")) {
            p_app_cfg->SnapThresholdPx = (uint32_t)strtoul(p_val, NULL, 10);
        } else if (0U != in_apply_cb) {
            if (0 == strcmp(p_key, "CmdNum")) {
                uint32_t parsed_num = (uint32_t)strtoul(p_val, NULL, 10);
                if (parsed_num > SYSCALL_MAX_APPLY_CMDS) {
                    parsed_num = SYSCALL_MAX_APPLY_CMDS;
                }
                if (parsed_num > p_app_cfg->ApplyCmdNum) {
                    p_app_cfg->ApplyCmdNum = parsed_num;
                }
            } else if ((0 == strncmp(p_key, "cmd", 3U)) || (0 == strncmp(p_key, "Cmd", 3U))) {
                int cmd_idx = atoi(&p_key[3]);
                if ((cmd_idx >= 0) && ((uint32_t)cmd_idx < SYSCALL_MAX_APPLY_CMDS)) {
                    (void)snprintf(p_app_cfg->ApplyCmds[cmd_idx],
                                   sizeof(p_app_cfg->ApplyCmds[cmd_idx]),
                                   "%s",
                                   p_val);
                    if (((uint32_t)cmd_idx + 1U) > p_app_cfg->ApplyCmdNum) {
                        p_app_cfg->ApplyCmdNum = (uint32_t)cmd_idx + 1U;
                    }
                }
            } else {
                /* Ignore unknown key inside ApplyCallBack */
            }
        } else if ((0U != in_main_win) && (0U == in_layers)) {
            if (0 == strcmp(p_key, "Title")) {
                (void)snprintf(p_app_cfg->WinTitle, sizeof(p_app_cfg->WinTitle), "%s", p_val);
            } else if (0 == strcmp(p_key, "Color")) {
                p_app_cfg->WinColor    = (uint32_t)strtoul(p_val, NULL, 0);
                p_app_cfg->WinHasColor = 1U;
            } else if (0 == strcmp(p_key, "Font")) {
                (void)snprintf(p_app_cfg->WinFont, sizeof(p_app_cfg->WinFont), "%s", p_val);
            } else if (0 == strcmp(p_key, "Bold")) {
                p_app_cfg->WinIsBold  = (uint8_t)strtoul(p_val, NULL, 10);
                p_app_cfg->WinHasBold = 1U;
            } else if (0 == strcmp(p_key, "Italic")) {
                p_app_cfg->WinIsItalic  = (uint8_t)strtoul(p_val, NULL, 10);
                p_app_cfg->WinHasItalic = 1U;
            } else if (0 == strcmp(p_key, "W")) {
                p_app_cfg->WinW = (uint32_t)strtoul(p_val, NULL, 10);
            } else if (0 == strcmp(p_key, "H")) {
                p_app_cfg->WinH = (uint32_t)strtoul(p_val, NULL, 10);
            } else if (0 == strcmp(p_key, "IsVisible")) {
                p_app_cfg->WinIsVisible = (uint8_t)strtoul(p_val, NULL, 10);
            } else if (0 == strcmp(p_key, "Mode")) {
                p_app_cfg->WinIsMaximized = (0 == strcmp(p_val, "MAXIMIZED")) ? 1U : 0U;
            } else {
                /* Ignore other keys */
            }
        } else if (0U != in_layers) {
            /* Track Layer<L> and Comp<C> blocks inside Layers */
            if ((0 == strncmp(p_key, "Layer", 5U)) &&
                (p_key[5] >= '0') && (p_key[5] <= '9')) {
                cur_layer    = atoi(&p_key[5]);
                cur_comp     = -1;
                in_sub_block = 0U;
            } else if ((0 == strncmp(p_key, "Comp", 4U)) &&
                       (p_key[4] >= '0') && (p_key[4] <= '9')) {
                cur_comp     = atoi(&p_key[4]);
                in_sub_block = 0U;
            } else if ((cur_layer >= 0) && ((uint32_t)cur_layer < USER_CFG_MAX_LAYERS) &&
                       (cur_comp  >= 0) && ((uint32_t)cur_comp  < USER_CFG_MAX_COMPS)  &&
                       (0 == strcmp(p_key, "Label"))) {
                if ('\0' != p_val[0]) {
                    AppCompYamlCfg_t *p_cc = &p_app_cfg->Comps[cur_layer][cur_comp];
                    (void)snprintf(p_cc->Label, sizeof(p_cc->Label), "%s", p_val);
                    p_cc->HasLabel = 1U;
                }
            } else if ((cur_layer >= 0) && ((uint32_t)cur_layer < USER_CFG_MAX_LAYERS) &&
                       (cur_comp  >= 0) && ((uint32_t)cur_comp  < USER_CFG_MAX_COMPS)  &&
                       (0 == strcmp(p_key, "Font"))) {
                /* Allow Font override under either Type: or ExtComps -> ExtComp0: */
                if ('\0' != p_val[0]) {
                    AppCompYamlCfg_t *p_cc = &p_app_cfg->Comps[cur_layer][cur_comp];
                    (void)snprintf(p_cc->Font, sizeof(p_cc->Font), "%s", p_val);
                    p_cc->HasFont = 1U;
                }
            } else if ((cur_layer >= 0) && ((uint32_t)cur_layer < USER_CFG_MAX_LAYERS) &&
                       (cur_comp  >= 0) && ((uint32_t)cur_comp  < USER_CFG_MAX_COMPS)  &&
                       (0 == strcmp(p_key, "TextStrFmt"))) {
                /* Allow TextStrFmt template under either Type: or ExtComps -> ExtComp0: */
                if ('\0' != p_val[0]) {
                    AppCompYamlCfg_t *p_cc = &p_app_cfg->Comps[cur_layer][cur_comp];
                    (void)snprintf(p_cc->TextStrFmt, sizeof(p_cc->TextStrFmt), "%s", p_val);
                    UnescapeYamlString(p_cc->TextStrFmt);
                    p_cc->HasTextStrFmt = 1U;
                }
            } else if ((cur_layer >= 0) && ((uint32_t)cur_layer < USER_CFG_MAX_LAYERS) &&
                       (cur_comp  >= 0) && ((uint32_t)cur_comp  < USER_CFG_MAX_COMPS)  &&
                       (0 == strcmp(p_key, "TextStr"))) {
                /* Allow TextStr override under either Type: or ExtComps -> ExtComp0: */
                if ('\0' != p_val[0]) {
                    AppCompYamlCfg_t *p_cc = &p_app_cfg->Comps[cur_layer][cur_comp];
                    (void)snprintf(p_cc->TextStr, sizeof(p_cc->TextStr), "%s", p_val);
                    UnescapeYamlString(p_cc->TextStr);
                    p_cc->HasTextStr = 1U;
                }
            } else if ((cur_layer >= 0) && ((uint32_t)cur_layer < USER_CFG_MAX_LAYERS) &&
                       (cur_comp  >= 0) && ((uint32_t)cur_comp  < USER_CFG_MAX_COMPS)  &&
                       (0 == strcmp(p_key, "FontSize"))) {
                /* Allow FontSize override under either Type: or ExtComps -> ExtComp0: */
                AppCompYamlCfg_t *p_cc = &p_app_cfg->Comps[cur_layer][cur_comp];
                p_cc->FontSize    = (uint16_t)strtoul(p_val, NULL, 10);
                p_cc->HasFontSize = 1U;
            } else if ((cur_layer >= 0) && ((uint32_t)cur_layer < USER_CFG_MAX_LAYERS) &&
                       (cur_comp  >= 0) && ((uint32_t)cur_comp  < USER_CFG_MAX_COMPS)  &&
                       (0 == strcmp(p_key, "Bold"))) {
                /* Allow Format -> Bold override under Type: or ExtComps -> ExtComp0: */
                AppCompYamlCfg_t *p_cc = &p_app_cfg->Comps[cur_layer][cur_comp];
                p_cc->IsBold  = (uint8_t)strtoul(p_val, NULL, 10);
                p_cc->HasBold = 1U;
            } else if ((cur_layer >= 0) && ((uint32_t)cur_layer < USER_CFG_MAX_LAYERS) &&
                       (cur_comp  >= 0) && ((uint32_t)cur_comp  < USER_CFG_MAX_COMPS)  &&
                       (0 == strcmp(p_key, "Italic"))) {
                /* Allow Format -> Italic override under Type: or ExtComps -> ExtComp0: */
                AppCompYamlCfg_t *p_cc = &p_app_cfg->Comps[cur_layer][cur_comp];
                p_cc->IsItalic  = (uint8_t)strtoul(p_val, NULL, 10);
                p_cc->HasItalic = 1U;
            } else if ((cur_layer >= 0) && ((uint32_t)cur_layer < USER_CFG_MAX_LAYERS) &&
                       (cur_comp  >= 0) && ((uint32_t)cur_comp  < USER_CFG_MAX_COMPS)  &&
                       ('\0' != p_val[0]) &&
                       ((0 == strcmp(p_key, "Horizona"))   ||
                        (0 == strcmp(p_key, "Horizontal")) ||
                        (0 == strcmp(p_key, "Align")))) {
                /* Allow Format -> Algin -> Horizona / Horizontal or direct Align override */
                AppCompYamlCfg_t *p_cc = &p_app_cfg->Comps[cur_layer][cur_comp];
                p_cc->Align    = ParseHorizAlign(p_val);
                p_cc->HasAlign = 1U;
            } else if ((cur_layer >= 0) && ((uint32_t)cur_layer < USER_CFG_MAX_LAYERS) &&
                       (cur_comp  >= 0) && ((uint32_t)cur_comp  < USER_CFG_MAX_COMPS)  &&
                       ('\0' != p_val[0]) &&
                       ((0 == strcmp(p_key, "Vertical")) || (0 == strcmp(p_key, "VAlign")))) {
                /* Allow Format -> Algin -> Vertical or direct VAlign override */
                AppCompYamlCfg_t *p_cc = &p_app_cfg->Comps[cur_layer][cur_comp];
                p_cc->VAlign    = ParseVertAlign(p_val);
                p_cc->HasVAlign = 1U;
            } else if (0 == strcmp(p_key, "Border")) {
                in_sub_block = 1U;
            } else if (0 == strcmp(p_key, "ExtComps")) {
                in_sub_block = 2U;
            } else if ((0 == strcmp(p_key, "RectClip")) ||
                       (0 == strcmp(p_key, "Listeners"))) {
                in_sub_block = 3U;
            } else if ((cur_layer >= 0) && ((uint32_t)cur_layer < USER_CFG_MAX_LAYERS) &&
                       (cur_comp  >= 0) && ((uint32_t)cur_comp  < USER_CFG_MAX_COMPS)  &&
                       (1U == in_sub_block)) {
                /* Inside Border: block */
                AppCompYamlCfg_t *p_cc = &p_app_cfg->Comps[cur_layer][cur_comp];
                if (0 == strcmp(p_key, "Size")) {
                    p_cc->BorderSize    = (uint32_t)strtoul(p_val, NULL, 10);
                    p_cc->HasBorderSize = 1U;
                } else if (0 == strcmp(p_key, "Color")) {
                    p_cc->BorderColor    = (uint32_t)strtoul(p_val, NULL, 0);
                    p_cc->HasBorderColor = 1U;
                } else {
                    /* Ignore other Border sub-keys */
                }
            } else if ((cur_layer >= 0) && ((uint32_t)cur_layer < USER_CFG_MAX_LAYERS) &&
                       (cur_comp  >= 0) && ((uint32_t)cur_comp  < USER_CFG_MAX_COMPS)  &&
                       (2U == in_sub_block)) {
                /* Inside ExtComps: block */
                AppCompYamlCfg_t *p_cc = &p_app_cfg->Comps[cur_layer][cur_comp];
                if (0 == strcmp(p_key, "Color")) {
                    p_cc->ExtTextColor    = (uint32_t)strtoul(p_val, NULL, 0);
                    p_cc->HasExtTextColor = 1U;
                } else {
                    /* Ignore other ExtComps sub-keys (Font/FontSize/Format handled above) */
                }
            } else if ((cur_layer >= 0) && ((uint32_t)cur_layer < USER_CFG_MAX_LAYERS) &&
                       (cur_comp  >= 0) && ((uint32_t)cur_comp  < USER_CFG_MAX_COMPS)  &&
                       (0U == in_sub_block)) {
                AppCompYamlCfg_t *p_cc = &p_app_cfg->Comps[cur_layer][cur_comp];

                if (0 == strcmp(p_key, "OffsetX")) {
                    p_cc->OffsetX      = (int32_t)strtol(p_val, NULL, 10);
                    p_cc->HasOffsetX   = 1U;
                } else if (0 == strcmp(p_key, "OffsetY")) {
                    p_cc->OffsetY      = (int32_t)strtol(p_val, NULL, 10);
                    p_cc->HasOffsetY   = 1U;
                } else if (0 == strcmp(p_key, "W")) {
                    p_cc->W            = (uint32_t)strtoul(p_val, NULL, 10);
                    p_cc->HasW         = 1U;
                } else if (0 == strcmp(p_key, "H")) {
                    p_cc->H            = (uint32_t)strtoul(p_val, NULL, 10);
                    p_cc->HasH         = 1U;
                } else if (0 == strcmp(p_key, "Color")) {
                    p_cc->Color        = (uint32_t)strtoul(p_val, NULL, 0);
                    p_cc->HasColor     = 1U;
                } else if (0 == strcmp(p_key, "Radius")) {
                    p_cc->Radius       = (uint32_t)strtoul(p_val, NULL, 10);
                    p_cc->HasRadius    = 1U;
                } else if (0 == strcmp(p_key, "IsFilled")) {
                    p_cc->IsFilled     = (uint8_t)strtoul(p_val, NULL, 10);
                    p_cc->HasFilled    = 1U;
                } else if (0 == strcmp(p_key, "Thickness")) {
                    p_cc->Thickness    = (uint32_t)strtoul(p_val, NULL, 10);
                    p_cc->HasThickness = 1U;
                } else if (0 == strcmp(p_key, "EndX")) {
                    p_cc->EndX         = (int32_t)strtol(p_val, NULL, 10);
                    p_cc->HasEndX      = 1U;
                } else if (0 == strcmp(p_key, "EndY")) {
                    p_cc->EndY         = (int32_t)strtol(p_val, NULL, 10);
                    p_cc->HasEndY      = 1U;
                } else if (0 == strcmp(p_key, "IsVisible")) {
                    p_cc->IsVisible    = (uint8_t)strtoul(p_val, NULL, 10);
                    p_cc->HasVisible   = 1U;
                } else {
                    /* Ignore other component sub-keys */
                }
            } else {
                /* Ignore sub-block keys */
            }
        } else {
            /* Ignore unknown top-level keys */
        }
    }

    /* Return success after loading app.yml */
    return APP_SUCCESS;
}

/* Detached worker thread entry that runs post-Apply shell commands without blocking the UI
 * @param p_arg[in] Heap-allocated ApplyCallbackJob_t pointer (freed before thread exit)
 * @return NULL on completion
 */
static void *ApplyCallbackWorkerEntry(void *p_arg) {
    ApplyCallbackJob_t *p_job = (ApplyCallbackJob_t *)p_arg;
    uint32_t            idx;

    /* Guard against NULL job pointer */
    if (NULL == p_job) {
        /* Exit thread if job is NULL */
        return NULL;
    }

    /* Execute each non-empty callback command sequentially in background thread */
    for (idx = 0U; idx < p_job->CmdNum; idx++) {
        int   exit_code = 0;
        Err_t err;

        if ('\0' == p_job->Cmds[idx][0]) {
            /* Skip empty command slot */
            continue;
        }

        APP_INFO("Background ApplyCallBack cmd%u: %s", idx, p_job->Cmds[idx]);
        err = SysCallExecScript(p_job->Cmds[idx], &exit_code);
        if (APP_SUCCESS != err) {
            APP_WARN("ApplyCallBack cmd%u exited with code %d: %s",
                     idx, exit_code, p_job->Cmds[idx]);
        }
    }

    free(p_job);

    /* Return NULL when background callback thread finishes */
    return NULL;
}

/* Asynchronously execute all non-empty post-Apply shell commands on a detached background thread
 * @param p_app_cfg[in] Loaded application configuration containing ApplyCmds[]
 * @return APP_SUCCESS once background thread is spawned, or error code
 */
Err_t SysCallAppRunApplyCallbacks(const AppYamlConfig_t *p_app_cfg) {
    uint32_t            limit;
    uint32_t            idx;
    uint8_t             has_non_empty = 0U;
    ApplyCallbackJob_t *p_job         = NULL;
    pthread_t           worker_tid;

    #if (1U == PARAM_CHECK_EN)
    /* Validate configuration pointer */
    if (NULL == p_app_cfg) {
        APP_ERR("p_app_cfg is NULL in SysCallAppRunApplyCallbacks!");
        return APP_ERROR_NULL;
    }
    #endif /* (1U == PARAM_CHECK_EN) */

    limit = (p_app_cfg->ApplyCmdNum <= SYSCALL_MAX_APPLY_CMDS)
            ? p_app_cfg->ApplyCmdNum : SYSCALL_MAX_APPLY_CMDS;

    /* Check if at least one non-empty command exists before spawning a thread */
    for (idx = 0U; idx < limit; idx++) {
        if ('\0' != p_app_cfg->ApplyCmds[idx][0]) {
            has_non_empty = 1U;
            /* Break once a valid command is found */
            break;
        }
    }
    if (0U == has_non_empty) {
        /* Return immediately when no commands are configured */
        return APP_SUCCESS;
    }

    p_job = (ApplyCallbackJob_t *)calloc(1U, sizeof(ApplyCallbackJob_t));
    if (NULL == p_job) {
        APP_ERR("Out of memory allocating ApplyCallbackJob_t!");
        return APP_ERROR;
    }

    p_job->CmdNum = limit;
    (void)memcpy(p_job->Cmds, p_app_cfg->ApplyCmds, sizeof(p_job->Cmds));

    /* Spawn detached background thread so UI never freezes while scripts execute */
    if (0 != pthread_create(&worker_tid, NULL, ApplyCallbackWorkerEntry, p_job)) {
        free(p_job);
        APP_ERR("Failed to spawn detached ApplyCallBack worker thread!");
        return APP_ERROR;
    }
    (void)pthread_detach(worker_tid);

    /* Return immediately while callbacks run in the background */
    return APP_SUCCESS;
}

/* reserved ***************************************************************************************
 **************************************************************************************************/


