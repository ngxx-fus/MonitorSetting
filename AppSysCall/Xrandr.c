/* file-desc **************************************************************************************
 * @filename    Xrandr.c
 * @brief       xrandr CLI based monitor query/apply/verify implementation with automatic roll-back.
 * @note        Parses `xrandr --query` output. Applying runs one atomic `xrandr --output ...` command,
 *              then re-queries to verify the result; on any mismatch the backup is re-applied.
 **************************************************************************************************/

/* headers ****************************************************************************************
 **************************************************************************************************/

#include "./__common__.h"
#include "./Xrandr.h"
#include "./Exec.h"

/* macros *****************************************************************************************
 **************************************************************************************************/

#define XRANDR_QUERY_BUF_LEN            (65536U)

/* definitions ************************************************************************************
 **************************************************************************************************/

/* exported variables *****************************************************************************
 **************************************************************************************************/

/* public variables *******************************************************************************
 **************************************************************************************************/

/* private variables ******************************************************************************
 **************************************************************************************************/

/* Raw `xrandr --query` output buffer (kept static to avoid a 64 KB stack frame) */
static char g_XrandrQueryBuf[XRANDR_QUERY_BUF_LEN];

/* exported functions *****************************************************************************
 **************************************************************************************************/

/* public functions *******************************************************************************
 **************************************************************************************************/

Err_t SysCallXrandrQuery(MonitorList_t *p_list);
Err_t SysCallXrandrGetCurrentCfg(const MonitorList_t *p_list, MonitorCfgList_t *p_cfg);
Err_t SysCallXrandrFindClosestMode(const ConnectedMonitor_t *p_mon, int32_t want_w, int32_t want_h,
                                   int32_t want_freq, uint16_t *p_mode_idx, uint16_t *p_freq);
Err_t SysCallXrandrFindModeIdx(const ConnectedMonitor_t *p_mon, uint16_t w, uint16_t h, uint16_t *p_mode_idx);
Err_t SysCallXrandrBuildCmd(const MonitorCfgList_t *p_cfg, char *p_cmd, uint32_t cmd_size);
Err_t SysCallXrandrApply(const MonitorCfgList_t *p_cfg);
Err_t SysCallXrandrVerify(const MonitorCfgList_t *p_cfg, uint8_t *p_is_match);
Err_t SysCallXrandrApplySafe(const MonitorCfgList_t *p_new_cfg, const MonitorCfgList_t *p_backup_cfg);

/* private functions ******************************************************************************
 **************************************************************************************************/

static int32_t AbsI32(int32_t val);
static uint8_t IsValidLabel(const char *p_label);
static void    ParseOutputLine(char *p_line, MonitorList_t *p_list, ConnectedMonitor_t **pp_cur);
static void    ParseModeLine(char *p_line, ConnectedMonitor_t *p_mon);
static void    AddFreq(DispMode_t *p_mode, uint16_t freq);

/* function implementations ***********************************************************************
 **************************************************************************************************/

/* Absolute value of a signed 32-bit integer
 * @param val[in] Input value
 * @return |val|
 */
static int32_t AbsI32(int32_t val) {
    /* Negate negative values */
    return (val < 0) ? -val : val;
}

/* Check that an output label only contains shell-safe characters [A-Za-z0-9_.-]
 * @param p_label[in] Output label
 * @return 1U if safe to place into a shell command, 0U otherwise
 */
static uint8_t IsValidLabel(const char *p_label) {
    uint32_t idx;

    /* Reject empty labels */
    if ((NULL == p_label) || ('\0' == p_label[0])) {
        return 0U;
    }
    /* Reject any character outside the safe set */
    for (idx = 0U; '\0' != p_label[idx]; idx++) {
        char ch = p_label[idx];
        if ((0 == isalnum((unsigned char)ch)) && ('-' != ch) && ('_' != ch) && ('.' != ch)) {
            return 0U;
        }
    }
    /* Return 1U when all characters are safe */
    return 1U;
}

/* Append a refresh rate to a mode if it is not already present
 * @param p_mode[in/out] Target mode
 * @param freq[in] Refresh rate in Hz
 */
static void AddFreq(DispMode_t *p_mode, uint16_t freq) {
    uint16_t idx;

    /* Skip invalid or duplicated rates (e.g. 59.94 and 60.00 both round to 60) */
    if (0U == freq) {
        return;
    }
    for (idx = 0U; idx < p_mode->FreqNum; idx++) {
        if (freq == p_mode->Freq[idx]) {
            return;
        }
    }
    /* Append when there is room left */
    if (p_mode->FreqNum < SYSCALL_MAX_FREQS) {
        p_mode->Freq[p_mode->FreqNum] = freq;
        p_mode->FreqNum++;
    }
    return;
}

/* Parse an output header line such as "HDMI-1 connected primary 1920x1080+0+0 (normal ...) ..."
 * @param p_line[in/out] Line buffer (tokenized in place)
 * @param p_list[in/out] Monitor list to append to
 * @param pp_cur[out] Set to the new monitor if connected, NULL otherwise
 */
static void ParseOutputLine(char *p_line, MonitorList_t *p_list, ConnectedMonitor_t **pp_cur) {
    char               *p_save = NULL;
    char               *p_name;
    char               *p_status;
    char               *p_tok;
    ConnectedMonitor_t *p_mon;

    *pp_cur  = NULL;
    p_name   = strtok_r(p_line, " \t", &p_save);
    p_status = strtok_r(NULL, " \t", &p_save);

    /* Only "connected" outputs are tracked ("disconnected" and "Screen" lines are ignored) */
    if ((NULL == p_name) || (NULL == p_status) || (0 != strcmp(p_status, "connected"))) {
        return;
    }
    if (p_list->Count >= SYSCALL_MAX_MONITORS) {
        APP_WARN("Too many connected outputs, ignoring %s", p_name);
        return;
    }

    p_mon = &p_list->Monitors[p_list->Count];
    (void)memset(p_mon, 0, sizeof(*p_mon));
    (void)snprintf(p_mon->Label, sizeof(p_mon->Label), "%s", p_name);

    /* Scan remaining tokens for "primary" and the current geometry "WxH+X+Y" */
    while (NULL != (p_tok = strtok_r(NULL, " \t", &p_save))) {
        unsigned int w;
        unsigned int h;
        int          x;
        int          y;

        if (0 == strcmp(p_tok, "primary")) {
            p_mon->IsPrimary = 1U;
        } else if ((0U == p_mon->IsActive) && (4 == sscanf(p_tok, "%ux%u+%d+%d", &w, &h, &x, &y))) {
            p_mon->IsActive = 1U;
            p_mon->CurW     = (uint16_t)w;
            p_mon->CurH     = (uint16_t)h;
            p_mon->PosX     = (int32_t)x;
            p_mon->PosY     = (int32_t)y;
        } else if ('(' == p_tok[0]) {
            /* Stop at the rotation/reflection list "(normal left ...)" */
            break;
        } else {
            /* Ignore other tokens */
        }
    }

    p_list->Count++;
    *pp_cur = p_mon;
    return;
}

/* Parse a mode line such as "   1920x1080     60.00*+  50.00    59.94"
 * @param p_line[in/out] Line buffer (tokenized in place)
 * @param p_mon[in/out] Monitor owning this mode
 * @note Interlaced modes (e.g. "1920x1080i") are skipped
 */
static void ParseModeLine(char *p_line, ConnectedMonitor_t *p_mon) {
    char        *p_save = NULL;
    char        *p_tok;
    unsigned int w;
    unsigned int h;
    char         suffix = '\0';
    DispMode_t  *p_mode = NULL;
    uint16_t     idx;

    p_tok = strtok_r(p_line, " \t", &p_save);
    /* Skip lines that are not "WxH" mode lines */
    if ((NULL == p_tok) || (2 > sscanf(p_tok, "%ux%u%c", &w, &h, &suffix))) {
        return;
    }
    /* Skip interlaced / named variants */
    if ('\0' != suffix) {
        return;
    }

    /* Merge with an existing mode of the same size (xrandr may list duplicates) */
    for (idx = 0U; idx < p_mon->DisplayNum; idx++) {
        if ((p_mon->Displays[idx].W == (uint16_t)w) && (p_mon->Displays[idx].H == (uint16_t)h)) {
            p_mode = &p_mon->Displays[idx];
            /* Break once the existing mode is found */
            break;
        }
    }
    if (NULL == p_mode) {
        if (p_mon->DisplayNum >= SYSCALL_MAX_MODES) {
            return;
        }
        p_mode = &p_mon->Displays[p_mon->DisplayNum];
        (void)memset(p_mode, 0, sizeof(*p_mode));
        p_mode->W = (uint16_t)w;
        p_mode->H = (uint16_t)h;
        p_mon->DisplayNum++;
    }

    /* Each following token is a rate, optionally marked '*' (current) and/or '+' (preferred) */
    while (NULL != (p_tok = strtok_r(NULL, " \t", &p_save))) {
        double   rate = atof(p_tok);
        uint16_t hz   = (uint16_t)(rate + 0.5);

        AddFreq(p_mode, hz);
        if ((NULL != strchr(p_tok, '*')) && (0U != p_mon->IsActive)) {
            p_mon->CurFreq = hz;
        }
    }
    return;
}

/* Query all connected outputs and their supported modes via `xrandr --query`
 * @param p_list[out] Filled monitor list
 * @return APP_SUCCESS on success, APP_ERROR if xrandr is unavailable or fails
 */
Err_t SysCallXrandrQuery(MonitorList_t *p_list) {
    int32_t             exit_code = -1;
    char               *p_save    = NULL;
    char               *p_line;
    ConnectedMonitor_t *p_cur     = NULL;
    Err_t               err;

    #if (1U == PARAM_CHECK_EN)
    if (NULL == p_list) {
        APP_ERR("p_list is NULL!");
        return APP_ERROR_NULL;
    }
    #endif /* (1U == PARAM_CHECK_EN) */

    (void)memset(p_list, 0, sizeof(*p_list));

    err = SysCallExecCapture(SYSCALL_XRANDR_BIN " --query 2>/dev/null",
                             g_XrandrQueryBuf, XRANDR_QUERY_BUF_LEN, &exit_code);
    if (APP_SUCCESS != err) {
        APP_ERR("xrandr --query failed (exit=%d). Is DISPLAY set?", exit_code);
        return APP_ERROR;
    }

    /* Walk the output line by line: header lines start at column 0, mode lines are indented */
    for (p_line = strtok_r(g_XrandrQueryBuf, "\n", &p_save);
         NULL != p_line;
         p_line = strtok_r(NULL, "\n", &p_save)) {
        char tmp[SYSCALL_LINE_LEN];

        (void)snprintf(tmp, sizeof(tmp), "%s", p_line);
        if ((' ' == tmp[0]) || ('\t' == tmp[0])) {
            /* Mode line belongs to the most recent connected output */
            if (NULL != p_cur) {
                ParseModeLine(tmp, p_cur);
            }
        } else {
            ParseOutputLine(tmp, p_list, &p_cur);
        }
    }

    /* Return success after parsing all outputs */
    return APP_SUCCESS;
}

/* Build a configuration snapshot from the current xrandr state
 * @param p_list[in] Monitor list from SysCallXrandrQuery
 * @param p_cfg[out] Snapshot (inactive outputs are stored with IsEnabled = 0U)
 * @return APP_SUCCESS on success, or APP_ERROR_NULL
 */
Err_t SysCallXrandrGetCurrentCfg(const MonitorList_t *p_list, MonitorCfgList_t *p_cfg) {
    uint32_t idx;

    #if (1U == PARAM_CHECK_EN)
    if ((NULL == p_list) || (NULL == p_cfg)) {
        APP_ERR("Null pointer in SysCallXrandrGetCurrentCfg!");
        return APP_ERROR_NULL;
    }
    #endif /* (1U == PARAM_CHECK_EN) */

    (void)memset(p_cfg, 0, sizeof(*p_cfg));
    p_cfg->Count = p_list->Count;

    /* Copy each output's live state into the snapshot */
    for (idx = 0U; idx < p_list->Count; idx++) {
        const ConnectedMonitor_t *p_mon = &p_list->Monitors[idx];
        MonitorCfg_t             *p_c   = &p_cfg->Cfgs[idx];

        (void)snprintf(p_c->Label, sizeof(p_c->Label), "%s", p_mon->Label);
        p_c->IsEnabled = p_mon->IsActive;
        p_c->IsPrimary = p_mon->IsPrimary;
        p_c->W         = p_mon->CurW;
        p_c->H         = p_mon->CurH;
        p_c->Freq      = p_mon->CurFreq;
        p_c->PosX      = p_mon->PosX;
        p_c->PosY      = p_mon->PosY;
    }

    /* Return success after building snapshot */
    return APP_SUCCESS;
}

/* Find the index of a mode with an exact W x H
 * @param p_mon[in] Monitor to search
 * @param w[in] Width
 * @param h[in] Height
 * @param p_mode_idx[out] Index into p_mon->Displays[]
 * @return APP_SUCCESS if found, APP_ERROR otherwise
 */
Err_t SysCallXrandrFindModeIdx(const ConnectedMonitor_t *p_mon, uint16_t w, uint16_t h, uint16_t *p_mode_idx) {
    uint16_t idx;

    #if (1U == PARAM_CHECK_EN)
    if ((NULL == p_mon) || (NULL == p_mode_idx)) {
        APP_ERR("Null pointer in SysCallXrandrFindModeIdx!");
        return APP_ERROR_NULL;
    }
    #endif /* (1U == PARAM_CHECK_EN) */

    /* Linear search over the supported modes */
    for (idx = 0U; idx < p_mon->DisplayNum; idx++) {
        if ((p_mon->Displays[idx].W == w) && (p_mon->Displays[idx].H == h)) {
            *p_mode_idx = idx;
            return APP_SUCCESS;
        }
    }

    /* Return error when the mode is not supported */
    return APP_ERROR;
}

/* Choose the supported mode/rate closest to the requested values
 * @param p_mon[in] Monitor to search
 * @param want_w[in] Desired width  (< 0 = pick the largest area mode)
 * @param want_h[in] Desired height (< 0 = pick the largest area mode)
 * @param want_freq[in] Desired refresh rate in Hz (< 0 = pick the highest rate)
 * @param p_mode_idx[out] Selected index into p_mon->Displays[]
 * @param p_freq[out] Selected refresh rate in Hz
 * @return APP_SUCCESS on success, APP_ERROR if the monitor has no modes
 */
Err_t SysCallXrandrFindClosestMode(const ConnectedMonitor_t *p_mon, int32_t want_w, int32_t want_h,
                                   int32_t want_freq, uint16_t *p_mode_idx, uint16_t *p_freq) {
    uint16_t idx;
    uint16_t best_idx  = 0U;
    int64_t  best_cost = 0;
    uint8_t  is_first  = 1U;
    uint16_t best_freq = 0U;
    const DispMode_t *p_mode;

    #if (1U == PARAM_CHECK_EN)
    if ((NULL == p_mon) || (NULL == p_mode_idx) || (NULL == p_freq)) {
        APP_ERR("Null pointer in SysCallXrandrFindClosestMode!");
        return APP_ERROR_NULL;
    }
    #endif /* (1U == PARAM_CHECK_EN) */

    if (0U == p_mon->DisplayNum) {
        APP_ERR("Output %s reports no modes!", p_mon->Label);
        return APP_ERROR;
    }

    /* Pick the mode with max area (auto when want_w < 0 or want_h < 0) or min |dW| + |dH| (requested) */
    for (idx = 0U; idx < p_mon->DisplayNum; idx++) {
        int64_t cost;

        p_mode = &p_mon->Displays[idx];
        if ((want_w < 0) || (want_h < 0)) {
            cost = -((int64_t)p_mode->W * (int64_t)p_mode->H);
        } else {
            cost = (int64_t)AbsI32((int32_t)p_mode->W - want_w) + (int64_t)AbsI32((int32_t)p_mode->H - want_h);
        }
        if ((0U != is_first) || (cost < best_cost)) {
            best_cost = cost;
            best_idx  = idx;
            is_first  = 0U;
        } else {
            /* Keep the earlier (xrandr preferred order) candidate on ties */
        }
    }

    /* Pick the highest rate (auto) or the closest rate (requested) */
    p_mode = &p_mon->Displays[best_idx];
    for (idx = 0U; idx < p_mode->FreqNum; idx++) {
        uint16_t f = p_mode->Freq[idx];
        if (0U == idx) {
            best_freq = f;
        } else if (want_freq < 0) {
            best_freq = (f > best_freq) ? f : best_freq;
        } else if (AbsI32((int32_t)f - want_freq) < AbsI32((int32_t)best_freq - want_freq)) {
            best_freq = f;
        } else {
            /* Keep current best rate */
        }
    }

    *p_mode_idx = best_idx;
    *p_freq     = best_freq;

    /* Return success after selecting mode and rate */
    return APP_SUCCESS;
}

/* Build a single atomic xrandr command for a whole configuration
 * @param p_cfg[in] Configuration to apply
 * @param p_cmd[out] Command buffer
 * @param cmd_size[in] Size of p_cmd in bytes
 * @return APP_SUCCESS on success, APP_ERROR_INVALID_PARAM on unsafe labels or overflow
 */
Err_t SysCallXrandrBuildCmd(const MonitorCfgList_t *p_cfg, char *p_cmd, uint32_t cmd_size) {
    uint32_t idx;
    uint32_t used;
    int      n;

    #if (1U == PARAM_CHECK_EN)
    if ((NULL == p_cfg) || (NULL == p_cmd)) {
        APP_ERR("Null pointer in SysCallXrandrBuildCmd!");
        return APP_ERROR_NULL;
    }
    if ((0U == p_cfg->Count) || (p_cfg->Count > SYSCALL_MAX_MONITORS)) {
        APP_ERR("Invalid monitor count: %u", p_cfg->Count);
        return APP_ERROR_INVALID_PARAM;
    }
    #endif /* (1U == PARAM_CHECK_EN) */

    n = snprintf(p_cmd, cmd_size, "%s", SYSCALL_XRANDR_BIN);
    if ((n < 0) || ((uint32_t)n >= cmd_size)) {
        APP_ERR("Command buffer too small!");
        return APP_ERROR_INVALID_PARAM;
    }
    used = (uint32_t)n;

    /* Append one "--output" clause per monitor */
    for (idx = 0U; idx < p_cfg->Count; idx++) {
        const MonitorCfg_t *p_c = &p_cfg->Cfgs[idx];

        if (0U == IsValidLabel(p_c->Label)) {
            APP_ERR("Unsafe output label rejected: '%s'", p_c->Label);
            return APP_ERROR_INVALID_PARAM;
        }

        if (0U == p_c->IsEnabled) {
            n = snprintf(&p_cmd[used], cmd_size - used, " --output %s --off", p_c->Label);
        } else if (0U != p_c->Freq) {
            n = snprintf(&p_cmd[used], cmd_size - used, " --output %s --mode %ux%u --rate %u --pos %dx%d%s",
                         p_c->Label, p_c->W, p_c->H, p_c->Freq, (int)p_c->PosX, (int)p_c->PosY,
                         (0U != p_c->IsPrimary) ? " --primary" : "");
        } else {
            n = snprintf(&p_cmd[used], cmd_size - used, " --output %s --mode %ux%u --pos %dx%d%s",
                         p_c->Label, p_c->W, p_c->H, (int)p_c->PosX, (int)p_c->PosY,
                         (0U != p_c->IsPrimary) ? " --primary" : "");
        }
        if ((n < 0) || ((uint32_t)n >= (cmd_size - used))) {
            APP_ERR("Command buffer too small!");
            return APP_ERROR_INVALID_PARAM;
        }
        used += (uint32_t)n;
    }

    /* Return success after building command */
    return APP_SUCCESS;
}

/* Apply a configuration with a single xrandr command (no verification, no roll-back)
 * @param p_cfg[in] Configuration to apply
 * @return APP_SUCCESS if xrandr exited with 0, APP_ERROR otherwise
 */
Err_t SysCallXrandrApply(const MonitorCfgList_t *p_cfg) {
    char    cmd[SYSCALL_CMD_LEN];
    int32_t exit_code = -1;
    Err_t   err;

    #if (1U == PARAM_CHECK_EN)
    if (NULL == p_cfg) {
        APP_ERR("p_cfg is NULL!");
        return APP_ERROR_NULL;
    }
    #endif /* (1U == PARAM_CHECK_EN) */

    err = SysCallXrandrBuildCmd(p_cfg, cmd, sizeof(cmd));
    if (APP_SUCCESS != err) {
        APP_ERR("Failed to build xrandr command!");
        return err;
    }

    APP_INFO("Exec: %s", cmd);
    /* Run the command; SysCallExecScript logs non-zero exit codes */
    return SysCallExecScript(cmd, &exit_code);
}

/* Re-query xrandr and check that every enabled output matches the requested configuration
 * @param p_cfg[in] Expected configuration
 * @param p_is_match[out] 1U if live state matches p_cfg, 0U otherwise
 * @return APP_SUCCESS if the check ran, APP_ERROR if xrandr could not be queried
 */
Err_t SysCallXrandrVerify(const MonitorCfgList_t *p_cfg, uint8_t *p_is_match) {
    static MonitorList_t s_live;
    uint32_t             c_idx;
    uint32_t             m_idx;

    #if (1U == PARAM_CHECK_EN)
    if ((NULL == p_cfg) || (NULL == p_is_match)) {
        APP_ERR("Null pointer in SysCallXrandrVerify!");
        return APP_ERROR_NULL;
    }
    #endif /* (1U == PARAM_CHECK_EN) */

    *p_is_match = 0U;
    if (APP_SUCCESS != SysCallXrandrQuery(&s_live)) {
        APP_ERR("Verification query failed!");
        return APP_ERROR;
    }

    /* Compare each requested output with its live state */
    for (c_idx = 0U; c_idx < p_cfg->Count; c_idx++) {
        const MonitorCfg_t       *p_c   = &p_cfg->Cfgs[c_idx];
        const ConnectedMonitor_t *p_mon = NULL;

        for (m_idx = 0U; m_idx < s_live.Count; m_idx++) {
            if (0 == strcmp(s_live.Monitors[m_idx].Label, p_c->Label)) {
                p_mon = &s_live.Monitors[m_idx];
                /* Break once the matching output is found */
                break;
            }
        }
        if (NULL == p_mon) {
            APP_ERR("Verify: output %s disappeared", p_c->Label);
            return APP_SUCCESS;
        }
        if (p_mon->IsActive != p_c->IsEnabled) {
            APP_ERR("Verify: output %s enabled state mismatch", p_c->Label);
            return APP_SUCCESS;
        }
        /* Disabled outputs only need to be inactive */
        if (0U == p_c->IsEnabled) {
            continue;
        }
        if ((p_mon->CurW != p_c->W) || (p_mon->CurH != p_c->H) ||
            (p_mon->PosX != p_c->PosX) || (p_mon->PosY != p_c->PosY)) {
            APP_ERR("Verify: %s is %ux%u+%d+%d, expected %ux%u+%d+%d", p_c->Label,
                    p_mon->CurW, p_mon->CurH, (int)p_mon->PosX, (int)p_mon->PosY,
                    p_c->W, p_c->H, (int)p_c->PosX, (int)p_c->PosY);
            return APP_SUCCESS;
        }
        if ((0U != p_c->Freq) && (0U != p_mon->CurFreq) &&
            (AbsI32((int32_t)p_mon->CurFreq - (int32_t)p_c->Freq) > SYSCALL_FREQ_TOLERANCE_HZ)) {
            APP_ERR("Verify: %s runs at %u Hz, expected %u Hz", p_c->Label, p_mon->CurFreq, p_c->Freq);
            return APP_SUCCESS;
        }
    }

    *p_is_match = 1U;
    /* Return success after all outputs matched */
    return APP_SUCCESS;
}

/* Apply a new configuration, verify it, and roll back to p_backup_cfg on any failure
 * @param p_new_cfg[in] Configuration to apply
 * @param p_backup_cfg[in] Known-good configuration used for roll-back
 * @return APP_SUCCESS if the new configuration is active and verified,
 *         APP_ERROR if it failed (roll-back attempted; see log for roll-back result)
 */
Err_t SysCallXrandrApplySafe(const MonitorCfgList_t *p_new_cfg, const MonitorCfgList_t *p_backup_cfg) {
    uint8_t is_match = 0U;
    Err_t   err;

    #if (1U == PARAM_CHECK_EN)
    if ((NULL == p_new_cfg) || (NULL == p_backup_cfg)) {
        APP_ERR("Null pointer in SysCallXrandrApplySafe!");
        return APP_ERROR_NULL;
    }
    #endif /* (1U == PARAM_CHECK_EN) */

    err = SysCallXrandrApply(p_new_cfg);
    if (APP_SUCCESS == err) {
        err = SysCallXrandrVerify(p_new_cfg, &is_match);
    }

    /* New configuration is active and verified */
    if ((APP_SUCCESS == err) && (0U != is_match)) {
        APP_INFO("New monitor configuration applied and verified.");
        return APP_SUCCESS;
    }

    APP_ERR("Applying new configuration failed -> rolling back!");
    if (APP_SUCCESS != SysCallXrandrApply(p_backup_cfg)) {
        APP_ERR("Roll-back command failed! Manual intervention may be required.");
    } else if ((APP_SUCCESS != SysCallXrandrVerify(p_backup_cfg, &is_match)) || (0U == is_match)) {
        APP_ERR("Roll-back applied but verification mismatched.");
    } else {
        APP_INFO("Roll-back succeeded.");
    }

    /* Report failure of the requested configuration */
    return APP_ERROR;
}

/* reserved ***************************************************************************************
 **************************************************************************************************/

