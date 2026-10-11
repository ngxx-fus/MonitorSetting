/* file-desc **************************************************************************************
 * @filename    main.c
 * @brief       Monitor Selection & Layout Configuration Application (AppCommon + AppSDL2 + AppSysCall).
 * @note        Implements:
 *              - Single-instance lock & window toggle
 *              - Multi-threaded sleeping/waking architecture (Main, EventListener, MiscBackground, CardMonitor)
 *              - AppSysCall integration: xrandr query/apply/rollback + UserConfig (app.yml, disp.yml)
 *              - Non-overlapping draggable monitor cards with edge/corner magnetic snapping
 *              - Auto top-left active monitor origin (0, 0) & relative POS(x, y) calculation
 *              - Right-side vertical Property Panel for ON/OFF toggle, resolution & refresh rate editing
 **************************************************************************************************/

/* headers ****************************************************************************************
 **************************************************************************************************/

#include "AppCommon/All.h"
#include "AppSDL2/All.h"
#include "AppSysCall/All.h"
#include <SDL2/SDL.h>

/* macros *****************************************************************************************
 **************************************************************************************************/

#define DEMO_WIN_WIDTH                  (800U)
#define DEMO_WIN_HEIGHT                 (520U)
#define DEMO_LAYER_COUNT                (3U)
#define DEMO_MAX_COMPS_PER_LAYER        (12U)
#define DEMO_CARD_COUNT                 (3U)
#define DEMO_MAX_CARD_EVENTS            (5U)
#define DEMO_SNAP_THRESHOLD_PX          (25U)
#define DEMO_PROP_BTN_COUNT             (6U)

/* definitions ************************************************************************************
 **************************************************************************************************/

/* Context shared with the miscellaneous background worker thread */
struct MiscThreadCtx_st {
    uint8_t    IsRunning;
    SDL_mutex *p_Mutex;
    SDL_cond  *p_Cond;
    Comp_t    *p_StatusCircle;
    Comp_t    *p_StatusText;
    uint32_t   TickCount;
};
typedef struct MiscThreadCtx_st MiscThreadCtx_t;

/* Context passed to the Card Monitor sub-thread */
struct CardMonitorCtx_st {
    uint8_t        IsRunning;
    SDL_mutex     *p_Mutex;
    SDL_cond      *p_Cond;
    Comp_t        *p_Cards;      /* Points to &g_LayerComps[1][0] */
    CompTextCfg_t *p_CardTitles; /* Points to &g_CardTitle[0] */
    uint32_t       CardCount;    /* DEMO_CARD_COUNT (3U) */
};
typedef struct CardMonitorCtx_st CardMonitorCtx_t;

/* exported variables *****************************************************************************
 **************************************************************************************************/

/* public variables *******************************************************************************
 **************************************************************************************************/

/* private variables ******************************************************************************
 **************************************************************************************************/

static MiscThreadCtx_t g_MiscCtx = {
    .IsRunning      = 0U,
    .p_Mutex        = NULL,
    .p_Cond         = NULL,
    .p_StatusCircle = NULL,
    .p_StatusText   = NULL,
    .TickCount      = 0U
};

static CardMonitorCtx_t g_CardMonCtx = {
    .IsRunning    = 0U,
    .p_Mutex      = NULL,
    .p_Cond       = NULL,
    .p_Cards      = NULL,
    .p_CardTitles = NULL,
    .CardCount    = 0U
};

/* AppSysCall runtime state: YAML config, detected/simulated monitors, active & rollback configs */
static AppYamlConfig_t    g_AppYamlCfg;
static DispPreferConfig_t g_DispPreferCfg;
static MonitorList_t      g_MonitorList;
static MonitorCfgList_t   g_ActiveCfg;
static MonitorCfgList_t   g_BackupCfg;
static CompDim_t          g_BackupCardDims[DEMO_CARD_COUNT];

/* Selected mode index and refresh rate index per monitor card for the Property Panel */
static uint16_t g_CardModeIdx[DEMO_CARD_COUNT] = { 0U, 0U, 0U };
static uint16_t g_CardFreqIdx[DEMO_CARD_COUNT] = { 0U, 0U, 0U };

/* Default active colors for each monitor card and black color when monitor is OFF */
static const Color_t g_CardActiveColors[DEMO_CARD_COUNT] = {
    { .w = 0xDD1E4E8CU },
    { .w = 0xDD2B6CB0U },
    { .w = 0xDD2C7A7BU }
};
static const Color_t g_CardOffBlackColor = { .w = 0xFF0A0A0AU };

/* Relative coordinates POS(x, y) from the auto-detected top-left active origin card */
static int32_t  g_CardRelPosX[DEMO_CARD_COUNT] = { 0, 0, 0 };
static int32_t  g_CardRelPosY[DEMO_CARD_COUNT] = { 0, 0, 0 };
static uint32_t g_OriginCardIdx                = 0U;

/* Currently selected monitor card index (updated on click, drag-start, or drop) */
static uint32_t g_SelectedCardIdx              = 0U;

/* Index of the most recently dragged/dropped card so it snaps against stationary cards */
static uint32_t g_LastMovedCardIdx             = (DEMO_CARD_COUNT - 1U);

/* Dynamic text buffers for the 3 monitor cards */
static char g_CardDynamicText[DEMO_CARD_COUNT][96];

/* Dynamic text buffers for the status bar and right-side Property Panel */
static char g_StatusMsgBuf[96]     = "Ready | Click or drag a monitor card to edit properties";
static char g_PropTitleBuf[48]     = "PROP: HDMI-1";
static char g_PropInfoBuf[160]     = "";
static char g_PropResValBuf[48]    = "Res: 1920x1080";
static char g_PropFreqValBuf[48]   = "Freq: 60 Hz";
static char g_PropPowerBtnBuf[32]  = "State: ON (Click OFF)";
static char g_PropPrimaryBtnBuf[32]= "Primary: YES";

/* 2D array of components: [3 layers][up to 12 components per layer] */
static Comp_t        g_LayerComps[DEMO_LAYER_COUNT][DEMO_MAX_COMPS_PER_LAYER];

/* Child text extension arrays (p_ExtComps) for the 3 draggable monitor cards */
static CompTextCfg_t g_CardTitle[DEMO_CARD_COUNT];
static ExtComp_t     g_CardExtItem[DEMO_CARD_COUNT];
static ExtCompList_t g_CardExtList[DEMO_CARD_COUNT];

/* Child text extension arrays for the 2 top buttons (Roll-back, Apply) */
static CompTextCfg_t g_TopBtnTitle[2U];
static ExtComp_t     g_TopBtnExtItem[2U];
static ExtCompList_t g_TopBtnExtList[2U];

/* Child text extension arrays for the 6 Property Panel buttons (-Res, +Res, -Freq, +Freq, ON/OFF, Primary) */
static CompTextCfg_t g_PropBtnTitle[DEMO_PROP_BTN_COUNT];
static ExtComp_t     g_PropBtnExtItem[DEMO_PROP_BTN_COUNT];
static ExtCompList_t g_PropBtnExtList[DEMO_PROP_BTN_COUNT];

/* Event listener arrays for the 3 draggable cards, 2 top buttons, and 6 property buttons */
static CompEvent_t   g_CardEvent[DEMO_CARD_COUNT][DEMO_MAX_CARD_EVENTS];
static CompEvent_t   g_BtnRollbackEvent;
static CompEvent_t   g_BtnApplyEvent;
static CompEvent_t   g_BtnPropEvent[DEMO_PROP_BTN_COUNT];

/* exported functions *****************************************************************************
 **************************************************************************************************/

/* public functions *******************************************************************************
 **************************************************************************************************/

int main(int argc, char *argv[]);

/* private functions ******************************************************************************
 **************************************************************************************************/

static void                    InitMonitorDataAndConfig(void);
static void                    SyncCardModeIndicesFromCfg(uint32_t card_idx);
static const AppCompYamlCfg_t *GetLayer1CardYamlCfg(uint32_t card_idx);
static void                    FormatMonitorTemplateStr(const char *p_fmt,
                                                        uint32_t    card_idx,
                                                        char       *p_out,
                                                        size_t      out_cap);
static void UpdateRelativePositionsAndTexts(void);
static void UpdatePropertyPanelTexts(void);
static void SelectMonitorCard(uint32_t card_idx);
static void WakeCardMonitorThread(void);
static void OnCardSelectClick(Comp_t *p_comp, const EventData_t *p_event);
static void OnCardDragStart(Comp_t *p_comp, const EventData_t *p_event);
static void OnCardDrop(Comp_t *p_comp, const EventData_t *p_event);
static void OnMonitorEvent(Comp_t *p_comp, const EventData_t *p_event);
static void OnUserSignalEvent(Comp_t *p_comp, const EventData_t *p_event);
static void OnBtnRollbackClick(Comp_t *p_comp, const EventData_t *p_event);
static void OnBtnApplyClick(Comp_t *p_comp, const EventData_t *p_event);
static void OnBtnResPrevClick(Comp_t *p_comp, const EventData_t *p_event);
static void OnBtnResNextClick(Comp_t *p_comp, const EventData_t *p_event);
static void OnBtnFreqPrevClick(Comp_t *p_comp, const EventData_t *p_event);
static void OnBtnFreqNextClick(Comp_t *p_comp, const EventData_t *p_event);
static void OnBtnPowerToggleClick(Comp_t *p_comp, const EventData_t *p_event);
static void OnBtnPrimaryClick(Comp_t *p_comp, const EventData_t *p_event);
static void MakeSnapMagnetsTogether(CompDim_t card[], uint32_t num, uint32_t threshold);
static int  CardMonitorThreadEntry(void *p_arg);
static void InitDemoLayers(void);
static void InitDemoComponents(void);
static void BindDemoEvents(void);
static int  MiscBackgroundThreadEntry(void *p_arg);

/* function implementations ***********************************************************************
 **************************************************************************************************/

/* Synchronize g_CardModeIdx[card_idx] and g_CardFreqIdx[card_idx] with g_ActiveCfg.Cfgs[card_idx]
 * @param card_idx[in] Monitor card index (0 .. DEMO_CARD_COUNT - 1)
 * @note Ensures valid indices into g_MonitorList.Monitors[card_idx].Displays[]
 */
static void SyncCardModeIndicesFromCfg(uint32_t card_idx) {
    const ConnectedMonitor_t *p_mon;
    const MonitorCfg_t       *p_cfg;
    uint16_t                  m_idx     = 0U;
    uint16_t                  f_idx     = 0U;
    uint16_t                  best_freq = 60U;
    uint16_t                  k;

    /* Guard against out-of-range card index */
    if (card_idx >= DEMO_CARD_COUNT) {
        /* Return immediately if index is invalid */
        return;
    }

    p_mon = &g_MonitorList.Monitors[card_idx];
    p_cfg = &g_ActiveCfg.Cfgs[card_idx];

    /* Find closest mode and refresh rate in p_mon->Displays[] */
    if (APP_SUCCESS == SysCallXrandrFindClosestMode(p_mon,
                                                    (int32_t)p_cfg->W,
                                                    (int32_t)p_cfg->H,
                                                    (int32_t)p_cfg->Freq,
                                                    &m_idx,
                                                    &best_freq)) {
        g_CardModeIdx[card_idx] = m_idx;
        for (k = 0U; k < p_mon->Displays[m_idx].FreqNum; k++) {
            if (p_mon->Displays[m_idx].Freq[k] == best_freq) {
                f_idx = k;
                /* Break once matching frequency slot is found */
                break;
            }
        }
        g_CardFreqIdx[card_idx]         = f_idx;
        g_ActiveCfg.Cfgs[card_idx].W    = p_mon->Displays[m_idx].W;
        g_ActiveCfg.Cfgs[card_idx].H    = p_mon->Displays[m_idx].H;
        g_ActiveCfg.Cfgs[card_idx].Freq = best_freq;
    }

    /* Return after syncing mode and frequency indices */
    return;
}

/* Query physical monitors via AppSysCall, populate fallback slots if < 3, and load YAML configs
 * @note Initializes g_AppYamlCfg, g_MonitorList, g_DispPreferCfg, g_ActiveCfg, and g_BackupCfg
 */
static void InitMonitorDataAndConfig(void) {
    static const char *s_default_names[DEMO_CARD_COUNT] = { "HDMI-1", "DP-1", "USB-C" };
    static const uint16_t s_res_list[4][2] = {
        { 3840U, 2160U },
        { 2560U, 1440U },
        { 1920U, 1080U },
        { 1280U, 720U  }
    };
    static const uint16_t s_freq_list[4] = { 60U, 75U, 120U, 144U };
    static const uint16_t s_init_mode[DEMO_CARD_COUNT] = { 2U, 1U, 0U };
    uint32_t idx;
    uint16_t m_idx;
    uint16_t f_idx;

    /* 1. Load application configuration from UserConfig/app.yml and set global default font */
    (void)SysCallAppConfigLoad(USER_CFG_DEFAULT_APP_PATH, &g_AppYamlCfg);
    if ('\0' != g_AppYamlCfg.WinFont[0]) {
        (void)DrawSetDefaultFont(g_AppYamlCfg.WinFont);
    }

    /* 2. Query physical monitors via xrandr */
    if (APP_SUCCESS != SysCallXrandrQuery(&g_MonitorList)) {
        g_MonitorList.Count = 0U;
    }

    /* 3. Ensure at least DEMO_CARD_COUNT (3) monitors exist so all 3 UI cards have full mode tables */
    for (idx = 0U; idx < DEMO_CARD_COUNT; idx++) {
        ConnectedMonitor_t *p_mon = &g_MonitorList.Monitors[idx];

        /* Populate synthetic monitor capabilities if slot was not detected by xrandr */
        if ((idx >= g_MonitorList.Count) || (0U == p_mon->DisplayNum)) {
            uint16_t def_m = s_init_mode[idx];

            (void)snprintf(p_mon->Label, sizeof(p_mon->Label), "%s", s_default_names[idx]);
            p_mon->IsActive   = 1U;
            p_mon->IsPrimary  = (0U == idx) ? 1U : 0U;
            p_mon->DisplayNum = 4U;

            /* Populate 4 standard resolutions and 4 refresh rates per resolution */
            for (m_idx = 0U; m_idx < 4U; m_idx++) {
                p_mon->Displays[m_idx].W       = s_res_list[m_idx][0];
                p_mon->Displays[m_idx].H       = s_res_list[m_idx][1];
                p_mon->Displays[m_idx].FreqNum = 4U;
                for (f_idx = 0U; f_idx < 4U; f_idx++) {
                    p_mon->Displays[m_idx].Freq[f_idx] = s_freq_list[f_idx];
                }
            }

            p_mon->CurW    = p_mon->Displays[def_m].W;
            p_mon->CurH    = p_mon->Displays[def_m].H;
            p_mon->CurFreq = 60U;
            p_mon->PosX    = (int32_t)(idx * 165U);
            p_mon->PosY    = 40;
        }
    }
    if (g_MonitorList.Count < DEMO_CARD_COUNT) {
        g_MonitorList.Count = DEMO_CARD_COUNT;
    }

    /* 4. Load preferred display settings from disp.yml and apply to g_ActiveCfg */
    if (APP_SUCCESS == SysCallDispPreferLoad(g_AppYamlCfg.PreferDispPath, &g_DispPreferCfg)) {
        (void)SysCallDispPreferApplyToMonitors(&g_DispPreferCfg, &g_MonitorList, &g_ActiveCfg);
    } else {
        (void)SysCallDispPreferInitDefault(&g_DispPreferCfg, DEMO_CARD_COUNT);
        (void)SysCallXrandrGetCurrentCfg(&g_MonitorList, &g_ActiveCfg);
    }

    /* 5. Synchronize mode/frequency indices and initialize rollback snapshot */
    for (idx = 0U; idx < DEMO_CARD_COUNT; idx++) {
        SyncCardModeIndicesFromCfg(idx);
    }
    g_BackupCfg = g_ActiveCfg;

    /* Return after initializing monitor data and configuration */
    return;
}

/* Resolve the Layer 1 YAML component configuration for a monitor card, matching by Label first
 * and falling back to unclaimed Comp slots in Layer 1
 * @param card_idx[in] Monitor card index (0 .. DEMO_CARD_COUNT - 1)
 * @return Pointer to matching AppCompYamlCfg_t in g_AppYamlCfg.Comps[1]
 */
static const AppCompYamlCfg_t *GetLayer1CardYamlCfg(uint32_t card_idx) {
    uint32_t    j;
    uint32_t    m;
    uint32_t    unmatched_rank = 0U;
    uint32_t    unclaimed_seen = 0U;
    const char *p_want_label;

    if (card_idx >= DEMO_CARD_COUNT) {
        return &g_AppYamlCfg.Comps[1][0];
    }

    p_want_label = g_ActiveCfg.Cfgs[card_idx].Label;

    /* 1. Try exact Label match against Layer 1 Comp0..Comp2 in app.yml */
    if ('\0' != p_want_label[0]) {
        for (j = 0U; j < DEMO_CARD_COUNT; j++) {
            const AppCompYamlCfg_t *p_cc = &g_AppYamlCfg.Comps[1][j];
            if ((0U != p_cc->HasLabel) && (0 == strcmp(p_cc->Label, p_want_label))) {
                return p_cc;
            }
        }
    }

    /* 2. Count how many earlier cards (m < card_idx) also had no exact Label match */
    for (m = 0U; m < card_idx; m++) {
        uint8_t m_matched = 0U;
        for (j = 0U; j < DEMO_CARD_COUNT; j++) {
            const AppCompYamlCfg_t *p_cc = &g_AppYamlCfg.Comps[1][j];
            if ((0U != p_cc->HasLabel) &&
                ('\0' != g_ActiveCfg.Cfgs[m].Label[0]) &&
                (0 == strcmp(p_cc->Label, g_ActiveCfg.Cfgs[m].Label))) {
                m_matched = 1U;
                break;
            }
        }
        if (0U == m_matched) {
            unmatched_rank++;
        }
    }

    /* 3. Pick the unmatched_rank-th Comp slot in Layer 1 not claimed by any connected monitor */
    for (j = 0U; j < DEMO_CARD_COUNT; j++) {
        const AppCompYamlCfg_t *p_cc      = &g_AppYamlCfg.Comps[1][j];
        uint8_t                 claimed   = 0U;

        if ((0U != p_cc->HasLabel) && ('\0' != p_cc->Label[0])) {
            for (m = 0U; m < DEMO_CARD_COUNT; m++) {
                if (('\0' != g_ActiveCfg.Cfgs[m].Label[0]) &&
                    (0 == strcmp(p_cc->Label, g_ActiveCfg.Cfgs[m].Label))) {
                    claimed = 1U;
                    break;
                }
            }
        }

        if (0U == claimed) {
            if (unclaimed_seen == unmatched_rank) {
                return p_cc;
            }
            unclaimed_seen++;
        }
    }

    /* 4. Fallback to direct index */
    return &g_AppYamlCfg.Comps[1][card_idx];
}

/* Expand a monitor text format template containing placeholders into an output buffer
 * Supported placeholders:
 *   <Label>   : Monitor label (e.g. "HDMI-1")
 *   <Primary> : "*" if primary, "" otherwise
 *   <Res>     : "<W>x<H>" when enabled, or "OFF" when disabled
 *   <W>       : Width in pixels (e.g. "1920")
 *   <H>       : Height in pixels (e.g. "1080")
 *   <Freq>    : Refresh rate in Hz (e.g. "60")
 *   <PosX>    : Relative display X coordinate (e.g. "0")
 *   <PosY>    : Relative display Y coordinate (e.g. "1080")
 *   <State>   : "ON" or "OFF"
 * @param p_fmt[in]      Format template string (e.g. "<Label>\n<Res>@<Freq>\nPOS(<PosX>, <PosY>)")
 * @param card_idx[in]   Monitor card index (0 .. DEMO_CARD_COUNT - 1)
 * @param p_out[out]     Destination buffer
 * @param out_cap[in]    Destination buffer capacity in bytes
 */
static void FormatMonitorTemplateStr(const char *p_fmt,
                                     uint32_t    card_idx,
                                     char       *p_out,
                                     size_t      out_cap) {
    const MonitorCfg_t *p_cfg;
    const char         *p_cur;
    size_t              out_len = 0U;
    char                tok_buf[64];

    /* Guard against NULL pointers or invalid index */
    if ((NULL == p_fmt) || (NULL == p_out) || (0U == out_cap) || (card_idx >= DEMO_CARD_COUNT)) {
        return;
    }

    p_cfg = &g_ActiveCfg.Cfgs[card_idx];
    p_cur = p_fmt;

    /* Scan format string and replace <Token> placeholders */
    while (('\0' != *p_cur) && ((out_len + 1U) < out_cap)) {
        const char *p_rep   = NULL;
        size_t      tok_len = 0U;

        if (0 == strncmp(p_cur, "<Label>", 7U)) {
            p_rep   = p_cfg->Label;
            tok_len = 7U;
        } else if (0 == strncmp(p_cur, "<Primary>", 9U)) {
            p_rep   = ((0U != p_cfg->IsEnabled) && (0U != p_cfg->IsPrimary)) ? "*" : "";
            tok_len = 9U;
        } else if (0 == strncmp(p_cur, "<Res>", 5U)) {
            if (0U != p_cfg->IsEnabled) {
                (void)snprintf(tok_buf, sizeof(tok_buf), "%ux%u", (uint32_t)p_cfg->W, (uint32_t)p_cfg->H);
            } else {
                (void)snprintf(tok_buf, sizeof(tok_buf), "OFF");
            }
            p_rep   = tok_buf;
            tok_len = 5U;
        } else if (0 == strncmp(p_cur, "<W>", 3U)) {
            (void)snprintf(tok_buf, sizeof(tok_buf), "%u", (uint32_t)p_cfg->W);
            p_rep   = tok_buf;
            tok_len = 3U;
        } else if (0 == strncmp(p_cur, "<H>", 3U)) {
            (void)snprintf(tok_buf, sizeof(tok_buf), "%u", (uint32_t)p_cfg->H);
            p_rep   = tok_buf;
            tok_len = 3U;
        } else if (0 == strncmp(p_cur, "<Freq>", 6U)) {
            if (0U != p_cfg->IsEnabled) {
                (void)snprintf(tok_buf, sizeof(tok_buf), "%u", (uint32_t)p_cfg->Freq);
            } else {
                (void)snprintf(tok_buf, sizeof(tok_buf), "-");
            }
            p_rep   = tok_buf;
            tok_len = 6U;
        } else if (0 == strncmp(p_cur, "<PosX>", 6U)) {
            (void)snprintf(tok_buf, sizeof(tok_buf), "%d", g_CardRelPosX[card_idx]);
            p_rep   = tok_buf;
            tok_len = 6U;
        } else if (0 == strncmp(p_cur, "<PosY>", 6U)) {
            (void)snprintf(tok_buf, sizeof(tok_buf), "%d", g_CardRelPosY[card_idx]);
            p_rep   = tok_buf;
            tok_len = 6U;
        } else if (0 == strncmp(p_cur, "<State>", 7U)) {
            p_rep   = (0U != p_cfg->IsEnabled) ? "ON" : "OFF";
            tok_len = 7U;
        } else {
            p_rep   = NULL;
            tok_len = 0U;
        }

        if (NULL != p_rep) {
            while (('\0' != *p_rep) && ((out_len + 1U) < out_cap)) {
                p_out[out_len] = *p_rep;
                out_len++;
                p_rep++;
            }
            p_cur += tok_len;
        } else {
            p_out[out_len] = *p_cur;
            out_len++;
            p_cur++;
        }
    }
    p_out[out_len] = '\0';

    /* Return after expanding template placeholders */
    return;
}

/* Refresh the right-side Property Panel texts and button colors for the currently selected card
 * @note Must be called with UI lock held or during single-threaded initialization
 */
static void UpdatePropertyPanelTexts(void) {
    uint32_t                  sel   = (g_SelectedCardIdx < DEMO_CARD_COUNT) ? g_SelectedCardIdx : 0U;
    const ConnectedMonitor_t *p_mon = &g_MonitorList.Monitors[sel];
    const MonitorCfg_t       *p_cfg = &g_ActiveCfg.Cfgs[sel];

    /* Format Property Panel header title */
    if ((0U != g_AppYamlCfg.Comps[2][1].HasTextStrFmt) && ('\0' != g_AppYamlCfg.Comps[2][1].TextStrFmt[0])) {
        FormatMonitorTemplateStr(g_AppYamlCfg.Comps[2][1].TextStrFmt, sel, g_PropTitleBuf, sizeof(g_PropTitleBuf));
    } else {
        (void)snprintf(g_PropTitleBuf,
                       sizeof(g_PropTitleBuf),
                       "PROP: %s %s",
                       p_mon->Label,
                       (0U == p_cfg->IsEnabled) ? "[OFF]" : ((0U != p_cfg->IsPrimary) ? "[PRIMARY]" : ""));
    }

    /* Format Property Panel summary info (Index, Status, Relative POS, Mode count) */
    if ((0U != g_AppYamlCfg.Comps[2][3].HasTextStrFmt) && ('\0' != g_AppYamlCfg.Comps[2][3].TextStrFmt[0])) {
        FormatMonitorTemplateStr(g_AppYamlCfg.Comps[2][3].TextStrFmt, sel, g_PropInfoBuf, sizeof(g_PropInfoBuf));
    } else if (0U != p_cfg->IsEnabled) {
        (void)snprintf(g_PropInfoBuf,
                       sizeof(g_PropInfoBuf),
                       "Display #%u (%s)\nRel POS: (%d, %d)\nModes: %u | Primary: %s",
                       sel + 1U,
                       (sel == g_OriginCardIdx) ? "Top-Left 0,0" : "Relative",
                       g_CardRelPosX[sel],
                       g_CardRelPosY[sel],
                       (uint32_t)p_mon->DisplayNum,
                       (0U != p_cfg->IsPrimary) ? "YES" : "NO");
    } else {
        (void)snprintf(g_PropInfoBuf,
                       sizeof(g_PropInfoBuf),
                       "Display #%u (Disabled)\nRel POS: (OFF)\nModes: %u | Status: OFF",
                       sel + 1U,
                       (uint32_t)p_mon->DisplayNum);
    }

    /* Format current resolution string */
    if ((0U != g_AppYamlCfg.Comps[2][4].HasTextStrFmt) && ('\0' != g_AppYamlCfg.Comps[2][4].TextStrFmt[0])) {
        FormatMonitorTemplateStr(g_AppYamlCfg.Comps[2][4].TextStrFmt, sel, g_PropResValBuf, sizeof(g_PropResValBuf));
    } else {
        (void)snprintf(g_PropResValBuf,
                       sizeof(g_PropResValBuf),
                       "Res: %ux%u",
                       (uint32_t)p_cfg->W,
                       (uint32_t)p_cfg->H);
    }

    /* Format current refresh rate string */
    if ((0U != g_AppYamlCfg.Comps[2][5].HasTextStrFmt) && ('\0' != g_AppYamlCfg.Comps[2][5].TextStrFmt[0])) {
        FormatMonitorTemplateStr(g_AppYamlCfg.Comps[2][5].TextStrFmt, sel, g_PropFreqValBuf, sizeof(g_PropFreqValBuf));
    } else {
        (void)snprintf(g_PropFreqValBuf,
                       sizeof(g_PropFreqValBuf),
                       "Freq: %u Hz",
                       (uint32_t)p_cfg->Freq);
    }

    /* Update ON/OFF power toggle button label and background color (Comp [2][10]) */
    if (0U != p_cfg->IsEnabled) {
        (void)snprintf(g_PropPowerBtnBuf, sizeof(g_PropPowerBtnBuf), "State: ON (Click OFF)");
        g_LayerComps[2][10].Type.Rect.Color.w = 0xFF2C7A7BU;
    } else {
        (void)snprintf(g_PropPowerBtnBuf, sizeof(g_PropPowerBtnBuf), "State: OFF (Click ON)");
        g_LayerComps[2][10].Type.Rect.Color.w = 0xFF1A202CU;
    }

    /* Update Primary selection button label and background color (Comp [2][11]) */
    if (0U == p_cfg->IsEnabled) {
        (void)snprintf(g_PropPrimaryBtnBuf, sizeof(g_PropPrimaryBtnBuf), "Primary: N/A (OFF)");
        g_LayerComps[2][11].Type.Rect.Color.w = 0xFF1A202CU;
    } else if (0U != p_cfg->IsPrimary) {
        (void)snprintf(g_PropPrimaryBtnBuf, sizeof(g_PropPrimaryBtnBuf), "Primary: YES (Active)");
        g_LayerComps[2][11].Type.Rect.Color.w = 0xFF2B6CB0U;
    } else {
        (void)snprintf(g_PropPrimaryBtnBuf, sizeof(g_PropPrimaryBtnBuf), "Set as Primary");
        g_LayerComps[2][11].Type.Rect.Color.w = 0xFF2D3748U;
    }

    /* Return after updating Property Panel strings */
    return;
}

/* Auto-detect the top-left-most active monitor card as (0, 0), compute real display POS(x, y)
 * by accumulating monitor W/H across adjacent cards, and update card colors & texts
 * @note If a monitor is OFF (IsEnabled == 0), its card turns black (0xFF0A0A0A) and shows OFF
 */
static void UpdateRelativePositionsAndTexts(void) {
    uint32_t idx;
    uint32_t pass;
    uint32_t j;
    uint32_t origin_idx               = 0U;
    uint8_t  found_on                 = 0U;
    uint8_t  has_active_primary       = 0U;
    int32_t  best_score               = 0;
    int32_t  origin_x                 = 0;
    int32_t  origin_y                 = 0;
    uint8_t  resolved[DEMO_CARD_COUNT] = { 0U, 0U, 0U };

    /* 1. Find the top-left-most ENABLED card (minimum OffsetX + OffsetY, tie-broken by OffsetY) */
    for (idx = 0U; idx < DEMO_CARD_COUNT; idx++) {
        int32_t cur_x;
        int32_t cur_y;
        int32_t cur_score;

        if (0U == g_ActiveCfg.Cfgs[idx].IsEnabled) {
            g_ActiveCfg.Cfgs[idx].IsPrimary = 0U;
            /* Skip disabled (OFF) monitors when picking the active top-left origin */
            continue;
        }

        if (0U != g_ActiveCfg.Cfgs[idx].IsPrimary) {
            if (0U == has_active_primary) {
                has_active_primary = 1U;
            } else {
                g_ActiveCfg.Cfgs[idx].IsPrimary = 0U;
            }
        }

        cur_x     = g_LayerComps[1][idx].Type.Rect.Dim.OffsetX;
        cur_y     = g_LayerComps[1][idx].Type.Rect.Dim.OffsetY;
        cur_score = cur_x + cur_y;

        if ((0U == found_on) ||
            (cur_score < best_score) ||
            ((cur_score == best_score) && (cur_y < g_LayerComps[1][origin_idx].Type.Rect.Dim.OffsetY))) {
            best_score = cur_score;
            origin_idx = idx;
            found_on   = 1U;
        }
    }

    /* If no enabled monitor is currently marked Primary, default to the top-left origin monitor */
    if ((0U == has_active_primary) && (0U != found_on)) {
        g_ActiveCfg.Cfgs[origin_idx].IsPrimary = 1U;
    }

    g_OriginCardIdx              = origin_idx;
    origin_x                     = g_LayerComps[1][origin_idx].Type.Rect.Dim.OffsetX;
    origin_y                     = g_LayerComps[1][origin_idx].Type.Rect.Dim.OffsetY;
    g_CardRelPosX[origin_idx]    = 0;
    g_CardRelPosY[origin_idx]    = 0;
    resolved[origin_idx]         = 1U;

    /* 2. Propagate real display pixel coordinates from resolved cards along snapped edges */
    for (pass = 0U; pass < DEMO_CARD_COUNT; pass++) {
        for (idx = 0U; idx < DEMO_CARD_COUNT; idx++) {
            int32_t  ix;
            int32_t  iy;
            int32_t  iw;
            int32_t  ih;
            int32_t  best_dist = 0x7FFFFFFF;
            uint32_t best_j    = origin_idx;
            uint8_t  found_adj = 0U;

            /* Skip cards that are already resolved or disabled (OFF) */
            if ((0U != resolved[idx]) || (0U == g_ActiveCfg.Cfgs[idx].IsEnabled)) {
                continue;
            }

            ix = g_LayerComps[1][idx].Type.Rect.Dim.OffsetX;
            iy = g_LayerComps[1][idx].Type.Rect.Dim.OffsetY;
            iw = (int32_t)g_LayerComps[1][idx].Type.Rect.Dim.W;
            ih = (int32_t)g_LayerComps[1][idx].Type.Rect.Dim.H;

            /* Find the closest already-resolved active card j */
            for (j = 0U; j < DEMO_CARD_COUNT; j++) {
                int32_t jx;
                int32_t jy;
                int32_t dist;

                if (0U == resolved[j]) {
                    continue;
                }
                jx   = g_LayerComps[1][j].Type.Rect.Dim.OffsetX;
                jy   = g_LayerComps[1][j].Type.Rect.Dim.OffsetY;
                dist = ((ix >= jx) ? (ix - jx) : (jx - ix)) + ((iy >= jy) ? (iy - jy) : (jy - iy));
                if ((0U == found_adj) || (dist < best_dist)) {
                    best_dist = dist;
                    best_j    = j;
                    found_adj = 1U;
                }
            }

            if (0U != found_adj) {
                int32_t jx    = g_LayerComps[1][best_j].Type.Rect.Dim.OffsetX;
                int32_t jy    = g_LayerComps[1][best_j].Type.Rect.Dim.OffsetY;
                int32_t jw    = (int32_t)g_LayerComps[1][best_j].Type.Rect.Dim.W;
                int32_t jh    = (int32_t)g_LayerComps[1][best_j].Type.Rect.Dim.H;
                int32_t dx    = ix - jx;
                int32_t dy    = iy - jy;
                int32_t rx    = g_CardRelPosX[best_j];
                int32_t ry    = g_CardRelPosY[best_j];
                int32_t mon_w = (int32_t)g_ActiveCfg.Cfgs[best_j].W;
                int32_t mon_h = (int32_t)g_ActiveCfg.Cfgs[best_j].H;
                int32_t cur_w = (int32_t)g_ActiveCfg.Cfgs[idx].W;
                int32_t cur_h = (int32_t)g_ActiveCfg.Cfgs[idx].H;

                /* Compute horizontal real display coordinate by adding monitor width when placed to the right */
                if (dx >= (jw - 8)) {
                    rx += mon_w + (((dx - jw) * mon_w) / ((jw > 0) ? jw : 155));
                } else if (dx <= -(iw - 8)) {
                    rx -= cur_w + (((-dx - iw) * cur_w) / ((iw > 0) ? iw : 155));
                } else {
                    rx += (dx * mon_w) / ((jw > 0) ? jw : 155);
                }

                /* Compute vertical real display coordinate by adding monitor height when placed below */
                if (dy >= (jh - 8)) {
                    ry += mon_h + (((dy - jh) * mon_h) / ((jh > 0) ? jh : 95));
                } else if (dy <= -(ih - 8)) {
                    ry -= cur_h + (((-dy - ih) * cur_h) / ((ih > 0) ? ih : 95));
                } else {
                    ry += (dy * mon_h) / ((jh > 0) ? jh : 95);
                }

                g_CardRelPosX[idx] = rx;
                g_CardRelPosY[idx] = ry;
                resolved[idx]      = 1U;
            }
        }
    }

    /* 3. Normalize so minimum active PosX/PosY is >= 0 and format each card's text & color */
    {
        int32_t min_rx = 0;
        int32_t min_ry = 0;
        for (idx = 0U; idx < DEMO_CARD_COUNT; idx++) {
            if (0U != g_ActiveCfg.Cfgs[idx].IsEnabled) {
                if (g_CardRelPosX[idx] < min_rx) {
                    min_rx = g_CardRelPosX[idx];
                }
                if (g_CardRelPosY[idx] < min_ry) {
                    min_ry = g_CardRelPosY[idx];
                }
            }
        }
        if ((min_rx < 0) || (min_ry < 0)) {
            for (idx = 0U; idx < DEMO_CARD_COUNT; idx++) {
                if (0U != g_ActiveCfg.Cfgs[idx].IsEnabled) {
                    g_CardRelPosX[idx] -= min_rx;
                    g_CardRelPosY[idx] -= min_ry;
                }
            }
        }
    }

    for (idx = 0U; idx < DEMO_CARD_COUNT; idx++) {
        const AppCompYamlCfg_t *p_card_cc = GetLayer1CardYamlCfg(idx);

        if (0U != g_ActiveCfg.Cfgs[idx].IsEnabled) {
            g_ActiveCfg.Cfgs[idx].PosX           = g_CardRelPosX[idx];
            g_ActiveCfg.Cfgs[idx].PosY           = g_CardRelPosY[idx];
            g_LayerComps[1][idx].Type.Rect.Color = g_CardActiveColors[idx];
            g_CardTitle[idx].Color.w             = 0xFFFFFFFFU;
            if ((0U != p_card_cc->HasTextStrFmt) && ('\0' != p_card_cc->TextStrFmt[0])) {
                FormatMonitorTemplateStr(p_card_cc->TextStrFmt,
                                         idx,
                                         g_CardDynamicText[idx],
                                         sizeof(g_CardDynamicText[idx]));
            } else {
                (void)snprintf(g_CardDynamicText[idx],
                               sizeof(g_CardDynamicText[idx]),
                               "%s%s | %ux%u@%u\nPOS(%d, %d)",
                               g_ActiveCfg.Cfgs[idx].Label,
                               (0U != g_ActiveCfg.Cfgs[idx].IsPrimary) ? "*" : "",
                               (uint32_t)g_ActiveCfg.Cfgs[idx].W,
                               (uint32_t)g_ActiveCfg.Cfgs[idx].H,
                               (uint32_t)g_ActiveCfg.Cfgs[idx].Freq,
                               g_CardRelPosX[idx],
                               g_CardRelPosY[idx]);
            }
        } else {
            /* Monitor is OFF: set card background color to pure dark black and show OFF state */
            int32_t off_rx = ((g_LayerComps[1][idx].Type.Rect.Dim.OffsetX - origin_x) *
                              (int32_t)g_ActiveCfg.Cfgs[origin_idx].W) / 155;
            int32_t off_ry = ((g_LayerComps[1][idx].Type.Rect.Dim.OffsetY - origin_y) *
                              (int32_t)g_ActiveCfg.Cfgs[origin_idx].H) / 95;
            g_CardRelPosX[idx]                   = off_rx;
            g_CardRelPosY[idx]                   = off_ry;
            g_ActiveCfg.Cfgs[idx].PosX           = off_rx;
            g_ActiveCfg.Cfgs[idx].PosY           = off_ry;
            g_ActiveCfg.Cfgs[idx].IsPrimary      = 0U;
            g_LayerComps[1][idx].Type.Rect.Color = g_CardOffBlackColor;
            g_CardTitle[idx].Color.w             = 0xFFA0AEC0U;
            if ((0U != p_card_cc->HasTextStrFmt) && ('\0' != p_card_cc->TextStrFmt[0])) {
                FormatMonitorTemplateStr(p_card_cc->TextStrFmt,
                                         idx,
                                         g_CardDynamicText[idx],
                                         sizeof(g_CardDynamicText[idx]));
            } else {
                (void)snprintf(g_CardDynamicText[idx],
                               sizeof(g_CardDynamicText[idx]),
                               "%s | OFF\nPOS(%d, %d)",
                               g_ActiveCfg.Cfgs[idx].Label,
                               off_rx,
                               off_ry);
            }
        }
        g_CardTitle[idx].TextStr = g_CardDynamicText[idx];
    }

    /* 4. Keep the right-side Property Panel synchronized with the latest relative coordinates */
    UpdatePropertyPanelTexts();

    /* Return after updating relative positions, colors, and card labels */
    return;
}

/* Select a monitor card, highlight its border, and refresh the right-side Property Panel
 * @param card_idx[in] Index of the card to select (0 .. DEMO_CARD_COUNT - 1)
 * @note Must be called with UI lock held
 */
static void SelectMonitorCard(uint32_t card_idx) {
    Color_t  sel_border  = { .w = 0xFF00E5FFU };
    Color_t  norm_border = { .w = 0xFFFFFFFFU };
    uint32_t idx;

    /* Clamp card_idx to valid range */
    if (card_idx >= DEMO_CARD_COUNT) {
        card_idx = 0U;
    }

    g_SelectedCardIdx = card_idx;

    /* Highlight the selected card with a cyan border and restore white border on other cards */
    for (idx = 0U; idx < DEMO_CARD_COUNT; idx++) {
        if (idx == g_SelectedCardIdx) {
            (void)CompSetBorder(&g_LayerComps[1][idx], 2U, sel_border);
        } else {
            (void)CompSetBorder(&g_LayerComps[1][idx], 1U, norm_border);
        }
    }

    /* Refresh right-side Property Panel texts for the newly selected card */
    UpdatePropertyPanelTexts();

    /* Return after updating card selection */
    return;
}

/* Signal CardMonitorThread to wake up immediately and run snapping + label updates
 * @note Thread-safe wakeup helper
 */
static void WakeCardMonitorThread(void) {
    /* Broadcast condition variable if CardMonitorThread synchronization primitives are ready */
    if ((NULL != g_CardMonCtx.p_Mutex) && (NULL != g_CardMonCtx.p_Cond)) {
        (void)SDL_LockMutex(g_CardMonCtx.p_Mutex);
        (void)SDL_CondBroadcast(g_CardMonCtx.p_Cond);
        (void)SDL_UnlockMutex(g_CardMonCtx.p_Mutex);
    }

    /* Return after waking CardMonitorThread */
    return;
}

/* Callback invoked when user clicks on a monitor card to select it in the Property Panel
 * @param p_comp[in/out] Card component that was clicked
 * @param p_event[in] Mouse down event details
 * @note Updates g_SelectedCardIdx and refreshes the right-side Property Panel
 */
static void OnCardSelectClick(Comp_t *p_comp, const EventData_t *p_event) {
    uint32_t idx;

    /* Assert component and event pointers are non-NULL */
    ASSERT_NOT_EQUAL(NULL, p_comp);
    ASSERT_NOT_EQUAL(NULL, p_event);

    /* Lock UI to identify clicked card and update Property Panel */
    (void)WindowLockUI();
    for (idx = 0U; idx < DEMO_CARD_COUNT; idx++) {
        if (&g_LayerComps[1][idx] == p_comp) {
            SelectMonitorCard(idx);
            /* Break once clicked card is found */
            break;
        }
    }
    (void)WindowUnlockUI();

    (void)WindowQueuePush(WIN_ACTION_UPDATE_ALL, NULL);

    /* Return after selecting card */
    return;
}

/* Callback invoked when user starts dragging a draggable card component
 * @param p_comp[in/out] Component being dragged
 * @param p_event[in] Drag start event details
 * @note Selects the card, highlights its border in yellow while dragging, and sets g_LastMovedCardIdx
 */
static void OnCardDragStart(Comp_t *p_comp, const EventData_t *p_event) {
    Color_t  drag_border = { .w = 0xFFFFFF00U };
    uint32_t idx;

    /* Assert component and event pointers are non-NULL */
    ASSERT_NOT_EQUAL(NULL, p_comp);
    ASSERT_NOT_EQUAL(NULL, p_event);

    /* Lock UI briefly to select the grabbed card and highlight its border */
    (void)WindowLockUI();
    for (idx = 0U; idx < DEMO_CARD_COUNT; idx++) {
        if (&g_LayerComps[1][idx] == p_comp) {
            g_LastMovedCardIdx = idx;
            SelectMonitorCard(idx);
            /* Break once matching card index is found */
            break;
        }
    }
    (void)CompSetBorder(p_comp, 2U, drag_border);
    (void)WindowUnlockUI();

    APP_INFO("Drag started on '%s' at (%d, %d)",
             p_comp->Label, p_event->MouseX, p_event->MouseY);

    /* Return after handling drag start */
    return;
}

/* Callback invoked when user drops a dragged card component
 * @param p_comp[in/out] Component that was dropped
 * @param p_event[in] Drop event details
 * @note Keeps the dropped card selected in the Property Panel and wakes CardMonitorThread to snap
 */
static void OnCardDrop(Comp_t *p_comp, const EventData_t *p_event) {
    uint32_t idx;

    /* Assert component and event pointers are non-NULL */
    ASSERT_NOT_EQUAL(NULL, p_comp);
    ASSERT_NOT_EQUAL(NULL, p_event);

    /* Lock UI briefly to select dropped card and restore its cyan selected border */
    (void)WindowLockUI();
    for (idx = 0U; idx < DEMO_CARD_COUNT; idx++) {
        if (&g_LayerComps[1][idx] == p_comp) {
            g_LastMovedCardIdx = idx;
            SelectMonitorCard(idx);
            /* Break once matching card index is found */
            break;
        }
    }
    (void)WindowUnlockUI();

    /* Wake up CardMonitorThread immediately so edge/corner snapping runs with zero lag */
    WakeCardMonitorThread();

    APP_INFO("Dropped '%s' at new offset (%d, %d)",
             p_comp->Label,
             p_comp->Type.Rect.Dim.OffsetX,
             p_comp->Type.Rect.Dim.OffsetY);

    /* Return after handling drop event */
    return;
}

/* Callback invoked when a background monitor event is dispatched
 * @param p_comp[in/out] Component receiving the monitor event
 * @param p_event[in] Event data containing SysCode tick
 * @note Logs periodic monitor status check
 */
static void OnMonitorEvent(Comp_t *p_comp, const EventData_t *p_event) {
    /* Assert component and event pointers are non-NULL */
    ASSERT_NOT_EQUAL(NULL, p_comp);
    ASSERT_NOT_EQUAL(NULL, p_event);

    APP_INFO("Monitor listener on '%s' received SysCode=%u",
             p_comp->Label, p_event->SysCode);

    /* Return after logging monitor event */
    return;
}

/* Callback invoked when a custom user signal (EVENT_TYPE_USER_SIGNAL) is received
 * @param p_comp[in/out] Component receiving the user signal
 * @param p_event[in] Event data containing signal code in SysCode
 * @note Demonstrates custom user signal handling
 */
static void OnUserSignalEvent(Comp_t *p_comp, const EventData_t *p_event) {
    /* Assert component and event pointers are non-NULL */
    ASSERT_NOT_EQUAL(NULL, p_comp);
    ASSERT_NOT_EQUAL(NULL, p_event);

    APP_INFO("Custom user signal received on '%s' (sig_code=%u)",
             p_comp->Label, p_event->SysCode);

    /* Return after handling user signal */
    return;
}

/* Click callback for the "Roll-back" button in Layer 0
 * @param p_comp[in/out] Button component that was clicked
 * @param p_event[in] Mouse click event data
 * @note Restores g_ActiveCfg and card positions from g_BackupCfg / g_BackupCardDims and applies via xrandr
 */
static void OnBtnRollbackClick(Comp_t *p_comp, const EventData_t *p_event) {
    uint32_t idx;

    /* Assert button and event pointers are non-NULL */
    ASSERT_NOT_EQUAL(NULL, p_comp);
    ASSERT_NOT_EQUAL(NULL, p_event);

    /* Lock UI and restore configuration + card positions from last backup snapshot */
    (void)WindowLockUI();
    g_ActiveCfg = g_BackupCfg;
    for (idx = 0U; idx < DEMO_CARD_COUNT; idx++) {
        SyncCardModeIndicesFromCfg(idx);
        (void)CompSetPosition(&g_LayerComps[1][idx],
                              g_BackupCardDims[idx].OffsetX,
                              g_BackupCardDims[idx].OffsetY);
    }
    UpdateRelativePositionsAndTexts();
    (void)snprintf(g_StatusMsgBuf, sizeof(g_StatusMsgBuf),
                   "Rolled back monitor layout & modes to previous snapshot.");
    (void)WindowUnlockUI();

    /* Apply the rolled-back configuration via AppSysCall xrandr */
    (void)SysCallXrandrApply(&g_ActiveCfg);

    /* Execute post-Apply callback commands (e.g. background/polybar reload) from app.yml */
    (void)SysCallAppRunApplyCallbacks(&g_AppYamlCfg);

    /* Emit user signal and wake CardMonitorThread / Main Thread to redraw */
    (void)EventSendUserSignal(200U, NULL);
    WakeCardMonitorThread();
    (void)WindowQueuePush(WIN_ACTION_UPDATE_ALL, NULL);

    APP_INFO("Roll-back executed: restored previous monitor configuration.");

    /* Return after rollback completes */
    return;
}

/* Click callback for the "Apply" button in Layer 0
 * @param p_comp[in/out] Button component that was clicked
 * @param p_event[in] Mouse click event data
 * @note Confirms via Yes/No dialog, applies via SysCallXrandrApplySafe, saves disp.yml, and runs ApplyCallBack
 */
static void OnBtnApplyClick(Comp_t *p_comp, const EventData_t *p_event) {
    uint8_t  is_yes = 0U;
    Err_t    err;
    uint32_t idx;

    /* Assert button and event pointers are non-NULL */
    ASSERT_NOT_EQUAL(NULL, p_comp);
    ASSERT_NOT_EQUAL(NULL, p_event);

    /* Show quick Yes/No confirmation dialog before applying display changes */
    err = WindowShowYesNoDialog("Apply Monitor Layout?",
                                "Apply current monitor positions, ON/OFF states, resolutions, and refresh rates?",
                                &is_yes);
    ASSERT_EQUAL(APP_SUCCESS, err);

    if (0U == is_yes) {
        APP_INFO("Apply cancelled by user.");
        /* Return without modifying system display configuration */
        return;
    }

    /* Lock UI to snapshot current card positions and build DispPreferConfig_t for disp.yml */
    (void)WindowLockUI();
    UpdateRelativePositionsAndTexts();
    g_DispPreferCfg.Count = DEMO_CARD_COUNT;
    for (idx = 0U; idx < DEMO_CARD_COUNT; idx++) {
        (void)snprintf(g_DispPreferCfg.Disp[idx].Label,
                       sizeof(g_DispPreferCfg.Disp[idx].Label),
                       "%s",
                       g_ActiveCfg.Cfgs[idx].Label);
        g_DispPreferCfg.Disp[idx].Enabled = (int32_t)g_ActiveCfg.Cfgs[idx].IsEnabled;
        g_DispPreferCfg.Disp[idx].Primary = (int32_t)g_ActiveCfg.Cfgs[idx].IsPrimary;
        g_DispPreferCfg.Disp[idx].W       = (int32_t)g_ActiveCfg.Cfgs[idx].W;
        g_DispPreferCfg.Disp[idx].H       = (int32_t)g_ActiveCfg.Cfgs[idx].H;
        g_DispPreferCfg.Disp[idx].Freq    = (int32_t)g_ActiveCfg.Cfgs[idx].Freq;
        g_DispPreferCfg.Disp[idx].PosX    = g_CardRelPosX[idx];
        g_DispPreferCfg.Disp[idx].PosY    = g_CardRelPosY[idx];
        g_BackupCardDims[idx]             = g_LayerComps[1][idx].Type.Rect.Dim;
    }
    g_BackupCfg = g_ActiveCfg;
    (void)WindowUnlockUI();

    /* Save updated user display preferences to UserConfig/disp.yml */
    err = SysCallDispPreferSave(g_AppYamlCfg.PreferDispPath, &g_DispPreferCfg);
    if (APP_SUCCESS != err) {
        APP_WARN("Could not save disp.yml to '%s'", g_AppYamlCfg.PreferDispPath);
    }

    /* Apply configuration via xrandr with automatic rollback protection */
    err = SysCallXrandrApplySafe(&g_ActiveCfg, &g_BackupCfg);

    /* Execute post-Apply shell callback commands (e.g. wallpaper/polybar/bspwm reload) from app.yml */
    (void)SysCallAppRunApplyCallbacks(&g_AppYamlCfg);

    (void)WindowLockUI();
    if (APP_SUCCESS == err) {
        (void)snprintf(g_StatusMsgBuf, sizeof(g_StatusMsgBuf),
                       "Applied & saved disp.yml (Origin: %s)",
                       g_ActiveCfg.Cfgs[g_OriginCardIdx].Label);
    } else {
        (void)snprintf(g_StatusMsgBuf, sizeof(g_StatusMsgBuf),
                       "Saved disp.yml (xrandr dry/sim mode)");
    }
    (void)WindowUnlockUI();

    (void)WindowQueuePush(WIN_ACTION_UPDATE_ALL, NULL);

    /* Return after applying and saving configuration */
    return;
}

/* Click callback for the "< Res" button in the right-side Property Panel
 * @param p_comp[in/out] Button component that was clicked
 * @param p_event[in] Mouse click event data
 * @note Selects the previous available resolution for the currently selected monitor card
 */
static void OnBtnResPrevClick(Comp_t *p_comp, const EventData_t *p_event) {
    uint32_t                  sel;
    const ConnectedMonitor_t *p_mon;
    uint16_t                  m_idx;

    /* Assert button and event pointers are non-NULL */
    ASSERT_NOT_EQUAL(NULL, p_comp);
    ASSERT_NOT_EQUAL(NULL, p_event);

    (void)WindowLockUI();
    sel   = (g_SelectedCardIdx < DEMO_CARD_COUNT) ? g_SelectedCardIdx : 0U;
    p_mon = &g_MonitorList.Monitors[sel];
    if (p_mon->DisplayNum > 0U) {
        m_idx = g_CardModeIdx[sel];
        m_idx = (0U == m_idx) ? (p_mon->DisplayNum - 1U) : (m_idx - 1U);
        g_CardModeIdx[sel] = m_idx;
        g_CardFreqIdx[sel] = 0U;
        g_ActiveCfg.Cfgs[sel].W    = p_mon->Displays[m_idx].W;
        g_ActiveCfg.Cfgs[sel].H    = p_mon->Displays[m_idx].H;
        g_ActiveCfg.Cfgs[sel].Freq = (p_mon->Displays[m_idx].FreqNum > 0U)
                                     ? p_mon->Displays[m_idx].Freq[0] : 60U;
        UpdateRelativePositionsAndTexts();
    }
    (void)WindowUnlockUI();

    (void)WindowQueuePush(WIN_ACTION_UPDATE_ALL, NULL);

    /* Return after cycling resolution backward */
    return;
}

/* Click callback for the "Res >" button in the right-side Property Panel
 * @param p_comp[in/out] Button component that was clicked
 * @param p_event[in] Mouse click event data
 * @note Selects the next available resolution for the currently selected monitor card
 */
static void OnBtnResNextClick(Comp_t *p_comp, const EventData_t *p_event) {
    uint32_t                  sel;
    const ConnectedMonitor_t *p_mon;
    uint16_t                  m_idx;

    /* Assert button and event pointers are non-NULL */
    ASSERT_NOT_EQUAL(NULL, p_comp);
    ASSERT_NOT_EQUAL(NULL, p_event);

    (void)WindowLockUI();
    sel   = (g_SelectedCardIdx < DEMO_CARD_COUNT) ? g_SelectedCardIdx : 0U;
    p_mon = &g_MonitorList.Monitors[sel];
    if (p_mon->DisplayNum > 0U) {
        m_idx = (uint16_t)((g_CardModeIdx[sel] + 1U) % p_mon->DisplayNum);
        g_CardModeIdx[sel] = m_idx;
        g_CardFreqIdx[sel] = 0U;
        g_ActiveCfg.Cfgs[sel].W    = p_mon->Displays[m_idx].W;
        g_ActiveCfg.Cfgs[sel].H    = p_mon->Displays[m_idx].H;
        g_ActiveCfg.Cfgs[sel].Freq = (p_mon->Displays[m_idx].FreqNum > 0U)
                                     ? p_mon->Displays[m_idx].Freq[0] : 60U;
        UpdateRelativePositionsAndTexts();
    }
    (void)WindowUnlockUI();

    (void)WindowQueuePush(WIN_ACTION_UPDATE_ALL, NULL);

    /* Return after cycling resolution forward */
    return;
}

/* Click callback for the "< Freq" button in the right-side Property Panel
 * @param p_comp[in/out] Button component that was clicked
 * @param p_event[in] Mouse click event data
 * @note Selects the previous available refresh rate for the currently selected monitor card
 */
static void OnBtnFreqPrevClick(Comp_t *p_comp, const EventData_t *p_event) {
    uint32_t                  sel;
    const ConnectedMonitor_t *p_mon;
    uint16_t                  m_idx;
    uint16_t                  f_num;
    uint16_t                  f_idx;

    /* Assert button and event pointers are non-NULL */
    ASSERT_NOT_EQUAL(NULL, p_comp);
    ASSERT_NOT_EQUAL(NULL, p_event);

    (void)WindowLockUI();
    sel   = (g_SelectedCardIdx < DEMO_CARD_COUNT) ? g_SelectedCardIdx : 0U;
    p_mon = &g_MonitorList.Monitors[sel];
    m_idx = g_CardModeIdx[sel];
    if ((m_idx < p_mon->DisplayNum) && (p_mon->Displays[m_idx].FreqNum > 0U)) {
        f_num = p_mon->Displays[m_idx].FreqNum;
        f_idx = g_CardFreqIdx[sel];
        f_idx = (0U == f_idx) ? (f_num - 1U) : (f_idx - 1U);
        g_CardFreqIdx[sel]         = f_idx;
        g_ActiveCfg.Cfgs[sel].Freq = p_mon->Displays[m_idx].Freq[f_idx];
        UpdateRelativePositionsAndTexts();
    }
    (void)WindowUnlockUI();

    (void)WindowQueuePush(WIN_ACTION_UPDATE_ALL, NULL);

    /* Return after cycling refresh rate backward */
    return;
}

/* Click callback for the "Freq >" button in the right-side Property Panel
 * @param p_comp[in/out] Button component that was clicked
 * @param p_event[in] Mouse click event data
 * @note Selects the next available refresh rate for the currently selected monitor card
 */
static void OnBtnFreqNextClick(Comp_t *p_comp, const EventData_t *p_event) {
    uint32_t                  sel;
    const ConnectedMonitor_t *p_mon;
    uint16_t                  m_idx;
    uint16_t                  f_num;
    uint16_t                  f_idx;

    /* Assert button and event pointers are non-NULL */
    ASSERT_NOT_EQUAL(NULL, p_comp);
    ASSERT_NOT_EQUAL(NULL, p_event);

    (void)WindowLockUI();
    sel   = (g_SelectedCardIdx < DEMO_CARD_COUNT) ? g_SelectedCardIdx : 0U;
    p_mon = &g_MonitorList.Monitors[sel];
    m_idx = g_CardModeIdx[sel];
    if ((m_idx < p_mon->DisplayNum) && (p_mon->Displays[m_idx].FreqNum > 0U)) {
        f_num = p_mon->Displays[m_idx].FreqNum;
        f_idx = (uint16_t)((g_CardFreqIdx[sel] + 1U) % f_num);
        g_CardFreqIdx[sel]         = f_idx;
        g_ActiveCfg.Cfgs[sel].Freq = p_mon->Displays[m_idx].Freq[f_idx];
        UpdateRelativePositionsAndTexts();
    }
    (void)WindowUnlockUI();

    (void)WindowQueuePush(WIN_ACTION_UPDATE_ALL, NULL);

    /* Return after cycling refresh rate forward */
    return;
}

/* Click callback for the ON/OFF power toggle button in the right-side Property Panel
 * @param p_comp[in/out] Button component that was clicked
 * @param p_event[in] Mouse click event data
 * @note Toggles IsEnabled for the selected monitor (preventing turning OFF the last active monitor)
 */
static void OnBtnPowerToggleClick(Comp_t *p_comp, const EventData_t *p_event) {
    uint32_t sel;
    uint32_t idx;
    uint32_t active_cnt = 0U;

    /* Assert button and event pointers are non-NULL */
    ASSERT_NOT_EQUAL(NULL, p_comp);
    ASSERT_NOT_EQUAL(NULL, p_event);

    (void)WindowLockUI();
    sel = (g_SelectedCardIdx < DEMO_CARD_COUNT) ? g_SelectedCardIdx : 0U;

    /* Count how many monitors are currently enabled */
    for (idx = 0U; idx < DEMO_CARD_COUNT; idx++) {
        if (0U != g_ActiveCfg.Cfgs[idx].IsEnabled) {
            active_cnt++;
        }
    }

    /* Prevent disabling the last remaining active monitor so at least 1 screen stays ON */
    if ((0U != g_ActiveCfg.Cfgs[sel].IsEnabled) && (active_cnt <= 1U)) {
        (void)snprintf(g_StatusMsgBuf, sizeof(g_StatusMsgBuf),
                       "Cannot turn OFF '%s': at least 1 monitor must remain ON!",
                       g_ActiveCfg.Cfgs[sel].Label);
    } else {
        g_ActiveCfg.Cfgs[sel].IsEnabled = (0U != g_ActiveCfg.Cfgs[sel].IsEnabled) ? 0U : 1U;
        UpdateRelativePositionsAndTexts();
        (void)snprintf(g_StatusMsgBuf, sizeof(g_StatusMsgBuf),
                       "Monitor '%s' set to %s.",
                       g_ActiveCfg.Cfgs[sel].Label,
                       (0U != g_ActiveCfg.Cfgs[sel].IsEnabled) ? "ON" : "OFF (Black)");
    }
    (void)WindowUnlockUI();

    (void)WindowQueuePush(WIN_ACTION_UPDATE_ALL, NULL);

    /* Return after toggling monitor ON/OFF state */
    return;
}

/* Click callback for the Primary display selection button in the right-side Property Panel
 * @param p_comp[in/out] Button component that was clicked
 * @param p_event[in] Mouse click event data
 * @note Sets the currently selected active monitor as the sole Primary display (--primary)
 */
static void OnBtnPrimaryClick(Comp_t *p_comp, const EventData_t *p_event) {
    uint32_t sel;
    uint32_t idx;

    /* Assert button and event pointers are non-NULL */
    ASSERT_NOT_EQUAL(NULL, p_comp);
    ASSERT_NOT_EQUAL(NULL, p_event);

    (void)WindowLockUI();
    sel = (g_SelectedCardIdx < DEMO_CARD_COUNT) ? g_SelectedCardIdx : 0U;

    /* Only an enabled (ON) monitor can be set as Primary */
    if (0U == g_ActiveCfg.Cfgs[sel].IsEnabled) {
        (void)snprintf(g_StatusMsgBuf, sizeof(g_StatusMsgBuf),
                       "Cannot set '%s' as Primary while it is OFF!",
                       g_ActiveCfg.Cfgs[sel].Label);
    } else {
        for (idx = 0U; idx < DEMO_CARD_COUNT; idx++) {
            g_ActiveCfg.Cfgs[idx].IsPrimary = (idx == sel) ? 1U : 0U;
        }
        UpdateRelativePositionsAndTexts();
        (void)snprintf(g_StatusMsgBuf, sizeof(g_StatusMsgBuf),
                       "Monitor '%s' set as PRIMARY display.",
                       g_ActiveCfg.Cfgs[sel].Label);
    }
    (void)WindowUnlockUI();

    (void)WindowQueuePush(WIN_ACTION_UPDATE_ALL, NULL);

    /* Return after updating Primary monitor selection */
    return;
}

/* Prevent card overlap and magnetically snap edges or corners (vertices) within threshold
 * @param card[in/out] Array of card dimensions to resolve and snap
 * @param num[in] Number of elements in card[]
 * @param threshold[in] Maximum pixel distance for edge and corner magnetic snapping
 * @note Places g_LastMovedCardIdx at the end of the solve order so the moved card snaps to stationary cards
 */
static void MakeSnapMagnetsTogether(CompDim_t card[], uint32_t num, uint32_t threshold) {
    uint32_t  moved_idx;
    CompDim_t tmp_dim;

    /* Validate input array and minimum card count */
    if ((NULL == card) || (num < 2U)) {
        /* Nothing to snap when array is NULL or has fewer than 2 cards */
        return;
    }

    moved_idx = (g_LastMovedCardIdx < num) ? g_LastMovedCardIdx : (num - 1U);

    /* Swap the most recently moved card to index (num - 1) so stationary cards act as anchors */
    if (moved_idx != (num - 1U)) {
        tmp_dim           = card[num - 1U];
        card[num - 1U]    = card[moved_idx];
        card[moved_idx]   = tmp_dim;
    }

    /* Run AppSDL2 overlap resolution + corner snapping + edge snapping */
    (void)CompSnapMagnetsTogether(card, num, threshold);

    /* Restore original card array ordering after solving */
    if (moved_idx != (num - 1U)) {
        tmp_dim           = card[num - 1U];
        card[num - 1U]    = card[moved_idx];
        card[moved_idx]   = tmp_dim;
    }

    /* Return after snapping and resolving card positions */
    return;
}

/* Sub-thread monitoring the 3 cards: prevents overlap, snaps edges/corners, and updates card labels
 * @param p_arg[in] Pointer to CardMonitorCtx_t
 * @return Thread exit status (0)
 * @note Wakes up every 100ms or immediately when OnCardDrop signals g_CardMonCtx.p_Cond
 */
static int CardMonitorThreadEntry(void *p_arg) {
    CardMonitorCtx_t *p_ctx = (CardMonitorCtx_t *)p_arg;
    CompDim_t         card_dims[DEMO_CARD_COUNT];
    uint32_t          idx;
    uint32_t          snap_thresh;

    /* Assert thread context pointer is valid */
    ASSERT_NOT_EQUAL(NULL, p_ctx);

    /* Loop until main thread sets IsRunning = 0U */
    while (0U != p_ctx->IsRunning) {
        /* 1. Sleep for 100ms or until woken immediately by OnCardDrop / shutdown */
        (void)SDL_LockMutex(p_ctx->p_Mutex);
        if (0U != p_ctx->IsRunning) {
            (void)SDL_CondWaitTimeout(p_ctx->p_Cond, p_ctx->p_Mutex, 100U);
        }
        (void)SDL_UnlockMutex(p_ctx->p_Mutex);

        /* Exit immediately if shutdown was requested */
        if (0U == p_ctx->IsRunning) {
            /* Break out of monitoring loop */
            break;
        }

        /* 2. Lock UI mutex before inspecting and snapping the 3 cards */
        (void)WindowLockUI();

        /* Copy current card dimensions into local array */
        for (idx = 0U; idx < p_ctx->CardCount; idx++) {
            card_dims[idx] = p_ctx->p_Cards[idx].Type.Rect.Dim;
        }

        /* When user is not actively dragging, resolve overlaps and snap edges/corners */
        if (0U == g_DragDropCtx.IsDragging) {
            snap_thresh = (g_AppYamlCfg.SnapThresholdPx > 0U)
                          ? g_AppYamlCfg.SnapThresholdPx : DEMO_SNAP_THRESHOLD_PX;
            MakeSnapMagnetsTogether(card_dims, p_ctx->CardCount, snap_thresh);

            /* Apply snapped coordinates back to each card and its child text via CompSetPosition */
            for (idx = 0U; idx < p_ctx->CardCount; idx++) {
                if ((card_dims[idx].OffsetX != p_ctx->p_Cards[idx].Type.Rect.Dim.OffsetX) ||
                    (card_dims[idx].OffsetY != p_ctx->p_Cards[idx].Type.Rect.Dim.OffsetY)) {
                    (void)CompSetPosition(&p_ctx->p_Cards[idx],
                                          card_dims[idx].OffsetX,
                                          card_dims[idx].OffsetY);
                }
            }
        }

        /* Recompute top-left active origin (0, 0), relative POS(x, y), card colors/texts, and Property Panel */
        UpdateRelativePositionsAndTexts();

        (void)WindowUnlockUI();

        /* 3. Enqueue redraw request to wake up Main Thread */
        (void)WindowQueuePush(WIN_ACTION_UPDATE_ALL, NULL);
    }

    /* Return 0 when thread terminates */
    return 0;
}

/* Step 1: Initialize all 3 layers in g_MainWindow and attach their component arrays
 * @note Layer 0 = Header & Main Frame, Layer 1 = Monitor Canvas (Left), Layer 2 = Property Panel (Right)
 */
static void InitDemoLayers(void) {
    /* Layer 1 viewport: Left monitor arrangement canvas relative to (0, 0) of MainWindow */
    CompDim_t layer1_clip = { .W = 500U, .H = 380U, .OffsetX = 40,  .OffsetY = 95, .Rotation = 0U };
    /* Layer 2 viewport: Right vertical Property Panel relative to (0, 0) of MainWindow */
    CompDim_t layer2_clip = { .W = 210U, .H = 380U, .OffsetX = 550, .OffsetY = 95, .Rotation = 0U };

    /* Layer 0: Base background panel, CanvasBg, header line, status indicator, Roll-back, and Apply */
    g_MainWindow.Layers[0].IsVisible = 1U;
    g_MainWindow.Layers[0].CompCount = 7U;
    g_MainWindow.Layers[0].p_Comps   = g_LayerComps[0];

    /* Layer 1: Left canvas containing the 3 draggable monitor cards (clips to layer1_clip) */
    g_MainWindow.Layers[1].IsVisible = 1U;
    g_MainWindow.Layers[1].CompCount = DEMO_CARD_COUNT;
    g_MainWindow.Layers[1].p_Comps   = g_LayerComps[1];
    (void)LayerSetScrollable(&g_MainWindow.Layers[1], layer1_clip, 0U);

    /* Layer 2: Right vertical Property Panel for inspecting/changing resolution, freq, ON/OFF & Primary */
    g_MainWindow.Layers[2].IsVisible = 1U;
    g_MainWindow.Layers[2].CompCount = 12U;
    g_MainWindow.Layers[2].p_Comps   = g_LayerComps[2];
    (void)LayerSetScrollable(&g_MainWindow.Layers[2], layer2_clip, 0U);

    /* Return after configuring all layers */
    return;
}

/* Step 2: Initialize all components inside Layer 0, Layer 1, and Layer 2
 * @note Uses g_LayerComps[layer_idx][comp_idx] and attaches child text extensions for buttons/cards
 */
static void InitDemoComponents(void) {
    uint32_t card_idx;
    uint32_t btn_idx;

    /* Layer 0 dimensions and colors (includes rounded CanvasBg for the 3 monitors + Roll-back & Apply) */
    CompDim_t panel_dim     = { .W = 740U, .H = 460U, .OffsetX = 30,  .OffsetY = 30, .Rotation = 0U };
    CompDim_t canvas_bg_dim = { .W = 500U, .H = 380U, .OffsetX = 40,  .OffsetY = 95, .Rotation = 0U };
    CompDim_t circle_dim    = { .W = 24U,  .H = 24U,  .OffsetX = 50,  .OffsetY = 48, .Rotation = 0U };
    CompDim_t btn_rb_dim    = { .W = 115U, .H = 28U,  .OffsetX = 505, .OffsetY = 46, .Rotation = 0U };
    CompDim_t btn_app_dim   = { .W = 115U, .H = 28U,  .OffsetX = 630, .OffsetY = 46, .Rotation = 0U };
    Color_t   panel_col     = { .w = 0xFF232936U };
    Color_t   canvas_bg_col = { .w = 0xFF1A202CU };
    Color_t   border_col    = { .w = 0xFF4A5568U };
    Color_t   line_col      = { .w = 0xFF00B4D8U };
    Color_t   circle_col    = { .w = 0xFF00E676U };
    Color_t   text_col      = { .w = 0xFFE2E8F0U };
    Color_t   btn_rb_col    = { .w = 0xFF4A5568U };
    Color_t   btn_app_col   = { .w = 0xFF2B6CB0U };
    Color_t   white_col     = { .w = 0xFFFFFFFFU };
    Color_t   cyan_col      = { .w = 0xFF00E5FFU };

    /* Layer 2 (Right-Side Vertical Property Panel) local dimensions relative to Layer 2 (0, 0) */
    CompDim_t prop_bg_dim  = { .W = 210U, .H = 380U, .OffsetX = 0,   .OffsetY = 0,   .Rotation = 0U };
    CompDim_t prop_btn_dims[DEMO_PROP_BTN_COUNT] = {
        { .W = 90U,  .H = 30U, .OffsetX = 12,  .OffsetY = 166, .Rotation = 0U }, /* < Res   */
        { .W = 90U,  .H = 30U, .OffsetX = 108, .OffsetY = 166, .Rotation = 0U }, /* Res >   */
        { .W = 90U,  .H = 30U, .OffsetX = 12,  .OffsetY = 236, .Rotation = 0U }, /* < Freq  */
        { .W = 90U,  .H = 30U, .OffsetX = 108, .OffsetY = 236, .Rotation = 0U }, /* Freq >  */
        { .W = 186U, .H = 30U, .OffsetX = 12,  .OffsetY = 290, .Rotation = 0U }, /* ON/OFF  */
        { .W = 186U, .H = 30U, .OffsetX = 12,  .OffsetY = 332, .Rotation = 0U }  /* Primary */
    };
    const_str_t prop_btn_labels[DEMO_PROP_BTN_COUNT] = {
        "< Res",
        "Res >",
        "< Freq",
        "Freq >",
        g_PropPowerBtnBuf,
        g_PropPrimaryBtnBuf
    };
    Color_t prop_bg_col    = { .w = 0xFF1A202CU };
    Color_t prop_btn_col   = { .w = 0xFF2D3748U };

    /* --- Layer 0 Components --- */
    /* Comp [0][0]: Main container rounded rectangle */
    (void)CompInitRect(&g_LayerComps[0][0], "MainPanel", panel_dim, panel_col, 10U, 1U);
    (void)CompSetBorder(&g_LayerComps[0][0], 1U, border_col);

    /* Comp [0][1]: Header separator line */
    (void)CompInitLine(&g_LayerComps[0][1], "HeaderLine", 40, 82, 760, 82, line_col, 2U);

    /* Comp [0][2]: Background thread pulsing status circle */
    (void)CompInitCircle(&g_LayerComps[0][2], "StatusCircle", circle_dim, circle_col, 10U, 1U);
    (void)CompSetBorder(&g_LayerComps[0][2], 1U, text_col);

    /* Comp [0][3]: Status header text */
    (void)CompInitText(&g_LayerComps[0][3], "HeaderText", 82, 54, g_StatusMsgBuf, text_col, 13U);
    g_LayerComps[0][3].Type.Text.Dim.W = 668U;
    g_LayerComps[0][3].Type.Text.Dim.H = 20U;

    /* Comp [0][4]: "Roll-back" button */
    (void)CompInitButton(&g_LayerComps[0][4], "Roll-back", btn_rb_dim, btn_rb_col, NULL, NULL);
    g_TopBtnTitle[0].Color              = white_col;
    g_TopBtnTitle[0].Dim                = btn_rb_dim;
    g_TopBtnTitle[0].TextStr            = "Roll-back";
    g_TopBtnTitle[0].FontSize           = 13U;
    (void)CompSetTextAlign(&g_TopBtnTitle[0], TEXT_ALIGN_CENTER, TEXT_VALIGN_MIDDLE, 0U);
    g_TopBtnExtItem[0].CastType         = COMP_CAST_TEXT;
    g_TopBtnExtItem[0].CompPtr.p_Text   = &g_TopBtnTitle[0];
    g_TopBtnExtList[0].Count            = 1U;
    g_TopBtnExtList[0].p_ExtComps       = &g_TopBtnExtItem[0];
    g_LayerComps[0][4].p_ExtComps       = &g_TopBtnExtList[0];

    /* Comp [0][5]: "Apply" button */
    (void)CompInitButton(&g_LayerComps[0][5], "Apply", btn_app_dim, btn_app_col, NULL, NULL);
    g_TopBtnTitle[1].Color              = white_col;
    g_TopBtnTitle[1].Dim                = btn_app_dim;
    g_TopBtnTitle[1].TextStr            = "Apply";
    g_TopBtnTitle[1].FontSize           = 13U;
    (void)CompSetTextAlign(&g_TopBtnTitle[1], TEXT_ALIGN_CENTER, TEXT_VALIGN_MIDDLE, 0U);
    g_TopBtnExtItem[1].CastType         = COMP_CAST_TEXT;
    g_TopBtnExtItem[1].CompPtr.p_Text   = &g_TopBtnTitle[1];
    g_TopBtnExtList[1].Count            = 1U;
    g_TopBtnExtList[1].p_ExtComps       = &g_TopBtnExtItem[1];
    g_LayerComps[0][5].p_ExtComps       = &g_TopBtnExtList[1];

    /* Comp [0][6]: Rounded container frame for the 3-monitor layout canvas (Radius = 8) */
    (void)CompInitRect(&g_LayerComps[0][6], "CanvasPanelBg", canvas_bg_dim, canvas_bg_col, 8U, 1U);
    (void)CompSetBorder(&g_LayerComps[0][6], 1U, line_col);

    /* --- Layer 1 Components (3 Draggable Monitor Cards relative to Layer 1 (0, 0)) --- */
    for (card_idx = 0U; card_idx < DEMO_CARD_COUNT; card_idx++) {
        int32_t                 init_x  = (card_idx < 2U) ? (10 + ((int32_t)card_idx * 155)) : 10;
        int32_t                 init_y  = (card_idx < 2U) ? 25 : 120;
        const DispPreferItem_t *p_match = SysCallDispPreferFindMatch(&g_DispPreferCfg,
                                                                     g_ActiveCfg.Cfgs[card_idx].Label,
                                                                     card_idx);

        /* Convert saved real display PosX/PosY from matched disp.yml entry into UI canvas coordinates */
        if ((NULL != p_match) && (p_match->PosX >= 0) && (p_match->PosY >= 0)) {
            int32_t ref_w    = (g_ActiveCfg.Cfgs[0].W > 0U) ? (int32_t)g_ActiveCfg.Cfgs[0].W : 1920;
            int32_t ref_h    = (g_ActiveCfg.Cfgs[0].H > 0U) ? (int32_t)g_ActiveCfg.Cfgs[0].H : 1080;
            int32_t scaled_x = 10 + ((p_match->PosX * 155) / ref_w);
            int32_t scaled_y = 25 + ((p_match->PosY * 95) / ref_h);

            if ((scaled_x >= 0) && (scaled_x < 345) && (scaled_y >= 0) && (scaled_y < 285)) {
                init_x = scaled_x;
                init_y = scaled_y;
            }
        }

        CompDim_t card_dim = {
            .W        = 155U,
            .H        = 95U,
            .OffsetX  = init_x,
            .OffsetY  = init_y,
            .Rotation = 0U
        };

        /* Initialize card rectangle and mark as draggable button */
        (void)CompInitRect(&g_LayerComps[1][card_idx],
                           g_ActiveCfg.Cfgs[card_idx].Label,
                           card_dim,
                           g_CardActiveColors[card_idx],
                           8U,
                           1U);
        (void)CompSetBorder(&g_LayerComps[1][card_idx], 1U, white_col);
        g_LayerComps[1][card_idx].Class       = COMP_CLASS_BUTTON;
        g_LayerComps[1][card_idx].IsDraggable = 1U;

        /* Attach centered and wrapped child text label to card via p_ExtComps */
        g_CardTitle[card_idx].Color    = white_col;
        g_CardTitle[card_idx].Dim      = card_dim;
        g_CardTitle[card_idx].TextStr  = g_CardDynamicText[card_idx];
        g_CardTitle[card_idx].FontSize = 12U;
        (void)CompSetTextAlign(&g_CardTitle[card_idx], TEXT_ALIGN_CENTER, TEXT_VALIGN_MIDDLE, 1U);

        g_CardExtItem[card_idx].CastType       = COMP_CAST_TEXT;
        g_CardExtItem[card_idx].CompPtr.p_Text = &g_CardTitle[card_idx];
        g_CardExtList[card_idx].Count          = 1U;
        g_CardExtList[card_idx].p_ExtComps     = &g_CardExtItem[card_idx];
        g_LayerComps[1][card_idx].p_ExtComps   = &g_CardExtList[card_idx];

        /* Save initial card dimensions for Roll-back */
        g_BackupCardDims[card_idx] = card_dim;
    }

    /* --- Layer 2 Components (Right-Side Vertical Property Panel relative to Layer 2 (0, 0)) --- */
    /* Comp [2][0]: Property panel container background (rounded Radius = 8) */
    (void)CompInitRect(&g_LayerComps[2][0], "PropPanelBg", prop_bg_dim, prop_bg_col, 8U, 1U);
    (void)CompSetBorder(&g_LayerComps[2][0], 1U, line_col);

    /* Comp [2][1]: Property panel title */
    (void)CompInitText(&g_LayerComps[2][1], "PropTitle", 12, 14, g_PropTitleBuf, cyan_col, 14U);
    g_LayerComps[2][1].Type.Text.Dim.W = 186U;
    g_LayerComps[2][1].Type.Text.Dim.H = 20U;

    /* Comp [2][2]: Separator line under property panel title */
    (void)CompInitLine(&g_LayerComps[2][2], "PropSepLine", 10, 38, 200, 38, border_col, 1U);

    /* Comp [2][3]: Multi-line monitor info summary (Display #, Origin/Relative, POS, Mode count) */
    (void)CompInitText(&g_LayerComps[2][3], "PropInfo", 12, 48, g_PropInfoBuf, text_col, 12U);
    g_LayerComps[2][3].Type.Text.Dim.W = 186U;
    g_LayerComps[2][3].Type.Text.Dim.H = 85U;
    (void)CompSetTextAlign(&g_LayerComps[2][3].Type.Text, TEXT_ALIGN_LEFT, TEXT_VALIGN_TOP, 1U);

    /* Comp [2][4]: Current resolution label */
    (void)CompInitText(&g_LayerComps[2][4], "PropResLabel", 12, 142, g_PropResValBuf, white_col, 13U);
    g_LayerComps[2][4].Type.Text.Dim.W = 186U;
    g_LayerComps[2][4].Type.Text.Dim.H = 18U;

    /* Comp [2][5]: Current refresh rate label */
    (void)CompInitText(&g_LayerComps[2][5], "PropFreqLabel", 12, 212, g_PropFreqValBuf, white_col, 13U);
    g_LayerComps[2][5].Type.Text.Dim.W = 186U;
    g_LayerComps[2][5].Type.Text.Dim.H = 18U;

    /* Comp [2][6..11]: 6 interactive buttons (< Res, Res >, < Freq, Freq >, State: ON/OFF, Primary) */
    for (btn_idx = 0U; btn_idx < DEMO_PROP_BTN_COUNT; btn_idx++) {
        Comp_t *p_btn = &g_LayerComps[2][6U + btn_idx];

        (void)CompInitButton(p_btn,
                             prop_btn_labels[btn_idx],
                             prop_btn_dims[btn_idx],
                             prop_btn_col,
                             NULL,
                             NULL);

        g_PropBtnTitle[btn_idx].Color            = white_col;
        g_PropBtnTitle[btn_idx].Dim              = prop_btn_dims[btn_idx];
        g_PropBtnTitle[btn_idx].TextStr          = prop_btn_labels[btn_idx];
        g_PropBtnTitle[btn_idx].FontSize         = 12U;
        (void)CompSetTextAlign(&g_PropBtnTitle[btn_idx], TEXT_ALIGN_CENTER, TEXT_VALIGN_MIDDLE, 0U);

        g_PropBtnExtItem[btn_idx].CastType       = COMP_CAST_TEXT;
        g_PropBtnExtItem[btn_idx].CompPtr.p_Text = &g_PropBtnTitle[btn_idx];
        g_PropBtnExtList[btn_idx].Count          = 1U;
        g_PropBtnExtList[btn_idx].p_ExtComps     = &g_PropBtnExtItem[btn_idx];
        p_btn->p_ExtComps                        = &g_PropBtnExtList[btn_idx];
    }

    /* Apply optional position, size, color, Font, FontSize, and Format overrides from UserConfig/app.yml */
    {
        uint32_t l_idx;
        uint32_t c_idx;

        for (l_idx = 0U; l_idx < USER_CFG_MAX_LAYERS; l_idx++) {
            for (c_idx = 0U; c_idx < g_MainWindow.Layers[l_idx].CompCount; c_idx++) {
                const AppCompYamlCfg_t *p_cc   = (1U == l_idx) ? GetLayer1CardYamlCfg(c_idx)
                                                               : &g_AppYamlCfg.Comps[l_idx][c_idx];
                Comp_t                 *p_comp = &g_LayerComps[l_idx][c_idx];
                CompTextCfg_t          *p_txt  = NULL;

                /* Resolve target CompTextCfg_t for either direct TEXT or button/card ExtComp0 text */
                if (COMP_CAST_TEXT == p_comp->CastType) {
                    p_txt = &p_comp->Type.Text;
                } else if ((NULL != p_comp->p_ExtComps) &&
                           (p_comp->p_ExtComps->Count > 0U) &&
                           (NULL != p_comp->p_ExtComps->p_ExtComps) &&
                           (COMP_CAST_TEXT == p_comp->p_ExtComps->p_ExtComps[0].CastType)) {
                    p_txt = p_comp->p_ExtComps->p_ExtComps[0].CompPtr.p_Text;
                } else {
                    p_txt = NULL;
                }

                /* Apply Font, FontSize, and Format (Bold, Italic, Align, VAlign) to any text component */
                if (NULL != p_txt) {
                    if ((0U != p_cc->HasFont) && ('\0' != p_cc->Font[0])) {
                        p_txt->FontName = p_cc->Font;
                    }
                    if (0U != p_cc->HasFontSize) {
                        p_txt->FontSize = p_cc->FontSize;
                    }
                    if (0U != p_cc->HasBold) {
                        p_txt->IsBold = p_cc->IsBold;
                    } else if (0U != g_AppYamlCfg.WinHasBold) {
                        p_txt->IsBold = g_AppYamlCfg.WinIsBold;
                    }
                    if (0U != p_cc->HasItalic) {
                        p_txt->IsItalic = p_cc->IsItalic;
                    } else if (0U != g_AppYamlCfg.WinHasItalic) {
                        p_txt->IsItalic = g_AppYamlCfg.WinIsItalic;
                    }
                    if (0U != p_cc->HasAlign) {
                        p_txt->Align = (TextAlign_t)p_cc->Align;
                    }
                    if (0U != p_cc->HasVAlign) {
                        p_txt->VAlign = (TextVAlign_t)p_cc->VAlign;
                    }
                    if ((COMP_CAST_TEXT == p_comp->CastType) && (0U != p_cc->HasW) && (p_cc->W > 0U)) {
                        p_txt->Dim.W = p_cc->W;
                    }
                    if ((COMP_CAST_TEXT == p_comp->CastType) && (0U != p_cc->HasH) && (p_cc->H > 0U)) {
                        p_txt->Dim.H = p_cc->H;
                    }
                    if ((COMP_CAST_TEXT != p_comp->CastType) && (0U != p_cc->HasExtTextColor)) {
                        p_txt->Color.w = p_cc->ExtTextColor;
                    }
                    if ((1U != l_idx) && (0U != p_cc->HasTextStr) && ('\0' != p_cc->TextStr[0])) {
                        if (p_txt->TextStr == g_StatusMsgBuf) {
                            (void)snprintf(g_StatusMsgBuf,
                                           sizeof(g_StatusMsgBuf),
                                           "%.*s",
                                           (int)(sizeof(g_StatusMsgBuf) - 1U),
                                           p_cc->TextStr);
                        } else if ((p_txt->TextStr != g_PropTitleBuf) &&
                                   (p_txt->TextStr != g_PropInfoBuf) &&
                                   (p_txt->TextStr != g_PropResValBuf) &&
                                   (p_txt->TextStr != g_PropFreqValBuf) &&
                                   (p_txt->TextStr != g_PropPowerBtnBuf) &&
                                   (p_txt->TextStr != g_PropPrimaryBtnBuf)) {
                            p_txt->TextStr = p_cc->TextStr;
                        }
                    }
                }

                /* Layer 1 card positions and colors are driven dynamically by disp.yml & active state */
                if (1U == l_idx) {
                    if (0U != p_cc->HasRadius) {
                        p_comp->Type.Rect.Radius = p_cc->Radius;
                    }
                    continue;
                }

                if (0U != p_cc->HasVisible) {
                    p_comp->IsVisible = p_cc->IsVisible;
                }
                if ((0U != p_cc->HasOffsetX) && (0U != p_cc->HasOffsetY)) {
                    (void)CompSetPosition(p_comp, p_cc->OffsetX, p_cc->OffsetY);
                }

                /* Apply Color, Border, Radius, IsFilled, Thickness, EndX/EndY, and W/H by CastType */
                switch (p_comp->CastType) {
                    case COMP_CAST_RECT:
                        if (0U != p_cc->HasColor) {
                            p_comp->Type.Rect.Color.w = p_cc->Color;
                        }
                        if (0U != p_cc->HasRadius) {
                            p_comp->Type.Rect.Radius = p_cc->Radius;
                        }
                        if (0U != p_cc->HasFilled) {
                            p_comp->Type.Rect.IsFilled = p_cc->IsFilled;
                        }
                        if ((0U != p_cc->HasW) && (p_cc->W > 0U)) {
                            p_comp->Type.Rect.Dim.W = p_cc->W;
                            if (NULL != p_txt) {
                                p_txt->Dim.W = p_cc->W;
                            }
                        }
                        if ((0U != p_cc->HasH) && (p_cc->H > 0U)) {
                            p_comp->Type.Rect.Dim.H = p_cc->H;
                            if (NULL != p_txt) {
                                p_txt->Dim.H = p_cc->H;
                            }
                        }
                        if (0U != p_cc->HasBorderSize) {
                            p_comp->Type.Rect.Border.Size = p_cc->BorderSize;
                        }
                        if (0U != p_cc->HasBorderColor) {
                            p_comp->Type.Rect.Border.Color.w = p_cc->BorderColor;
                        }
                        break;
                    case COMP_CAST_CIRCLE:
                        if (0U != p_cc->HasColor) {
                            p_comp->Type.Circle.Color.w = p_cc->Color;
                        }
                        if (0U != p_cc->HasRadius) {
                            p_comp->Type.Circle.Radius = p_cc->Radius;
                        }
                        if (0U != p_cc->HasFilled) {
                            p_comp->Type.Circle.IsFilled = p_cc->IsFilled;
                        }
                        if (0U != p_cc->HasBorderSize) {
                            p_comp->Type.Circle.Border.Size = p_cc->BorderSize;
                        }
                        if (0U != p_cc->HasBorderColor) {
                            p_comp->Type.Circle.Border.Color.w = p_cc->BorderColor;
                        }
                        break;
                    case COMP_CAST_LINE:
                        if (0U != p_cc->HasColor) {
                            p_comp->Type.Line.Color.w = p_cc->Color;
                        }
                        if (0U != p_cc->HasThickness) {
                            p_comp->Type.Line.Thickness = p_cc->Thickness;
                        }
                        if (0U != p_cc->HasEndX) {
                            p_comp->Type.Line.EndX = p_cc->EndX;
                        }
                        if (0U != p_cc->HasEndY) {
                            p_comp->Type.Line.EndY = p_cc->EndY;
                        }
                        break;
                    case COMP_CAST_TEXT:
                        if (0U != p_cc->HasColor) {
                            p_comp->Type.Text.Color.w = p_cc->Color;
                        }
                        break;
                    default:
                        /* Ignore other CastTypes */
                        break;
                }
            }
        }
    }

    /* Select Card 0 by default and compute initial relative positions + card/property texts */
    SelectMonitorCard(0U);
    UpdateRelativePositionsAndTexts();

    /* Return after initializing all components */
    return;
}

/* Step 3: Attach event listeners to buttons in Layer 0, cards in Layer 1, and prop buttons in Layer 2
 * @note Binds click, drag-start, drop, monitor, and user-signal callbacks
 */
static void BindDemoEvents(void) {
    uint32_t card_idx;
    EventCallback_t prop_callbacks[DEMO_PROP_BTN_COUNT] = {
        OnBtnResPrevClick,
        OnBtnResNextClick,
        OnBtnFreqPrevClick,
        OnBtnFreqNextClick,
        OnBtnPowerToggleClick,
        OnBtnPrimaryClick
    };
    uint32_t btn_idx;

    /* Bind click callback to Layer 0 Comp 4 ("Roll-back" button) */
    g_BtnRollbackEvent.Listener.Type        = EVENT_TYPE_MOUSE_DOWN;
    g_BtnRollbackEvent.Listener.p_CallBack  = OnBtnRollbackClick;
    g_LayerComps[0][4].Listeners.Count      = 1U;
    g_LayerComps[0][4].Listeners.p_Events   = &g_BtnRollbackEvent;

    /* Bind click callback to Layer 0 Comp 5 ("Apply" button) */
    g_BtnApplyEvent.Listener.Type           = EVENT_TYPE_MOUSE_DOWN;
    g_BtnApplyEvent.Listener.p_CallBack     = OnBtnApplyClick;
    g_LayerComps[0][5].Listeners.Count      = 1U;
    g_LayerComps[0][5].Listeners.p_Events   = &g_BtnApplyEvent;

    /* Bind click, drag/drop, and system listeners to each card in Layer 1 */
    for (card_idx = 0U; card_idx < DEMO_CARD_COUNT; card_idx++) {
        g_CardEvent[card_idx][0].Listener.Type       = EVENT_TYPE_MOUSE_DOWN;
        g_CardEvent[card_idx][0].Listener.p_CallBack = OnCardSelectClick;
        g_CardEvent[card_idx][1].Listener.Type       = EVENT_TYPE_DRAG_START;
        g_CardEvent[card_idx][1].Listener.p_CallBack = OnCardDragStart;
        g_CardEvent[card_idx][2].Listener.Type       = EVENT_TYPE_DROP;
        g_CardEvent[card_idx][2].Listener.p_CallBack = OnCardDrop;

        /* Card 0 also listens for periodic EVENT_TYPE_MONITOR and custom EVENT_TYPE_USER_SIGNAL */
        if (0U == card_idx) {
            g_CardEvent[card_idx][3].Listener.Type       = EVENT_TYPE_MONITOR;
            g_CardEvent[card_idx][3].Listener.p_CallBack = OnMonitorEvent;
            g_CardEvent[card_idx][4].Listener.Type       = EVENT_TYPE_USER_SIGNAL;
            g_CardEvent[card_idx][4].Listener.p_CallBack = OnUserSignalEvent;
            g_LayerComps[1][card_idx].Listeners.Count    = 5U;
        } else {
            g_LayerComps[1][card_idx].Listeners.Count    = 3U;
        }
        g_LayerComps[1][card_idx].Listeners.p_Events = g_CardEvent[card_idx];
    }

    /* Bind click callbacks to the 5 Property Panel buttons in Layer 2 (Comps 6..10) */
    for (btn_idx = 0U; btn_idx < DEMO_PROP_BTN_COUNT; btn_idx++) {
        g_BtnPropEvent[btn_idx].Listener.Type             = EVENT_TYPE_MOUSE_DOWN;
        g_BtnPropEvent[btn_idx].Listener.p_CallBack       = prop_callbacks[btn_idx];
        g_LayerComps[2][6U + btn_idx].Listeners.Count     = 1U;
        g_LayerComps[2][6U + btn_idx].Listeners.p_Events  = &g_BtnPropEvent[btn_idx];
    }

    /* Return after binding all component event listeners */
    return;
}

/* Background housekeeping thread that sleeps on condition variable and pushes UI update actions
 * @param p_arg[in] Pointer to MiscThreadCtx_t
 * @return Thread exit status (0)
 * @note Uses SDL_CondWaitTimeout so it can be woken up immediately on shutdown
 */
static int MiscBackgroundThreadEntry(void *p_arg) {
    MiscThreadCtx_t *p_ctx = (MiscThreadCtx_t *)p_arg;

    /* Assert thread context pointer is non-NULL */
    ASSERT_NOT_EQUAL(NULL, p_ctx);

    /* Execute periodic background tasks while application is running */
    while (0U != p_ctx->IsRunning) {
        /* Sleep for up to 500ms or wake immediately if signaled via p_Cond */
        if ((NULL != p_ctx->p_Mutex) && (NULL != p_ctx->p_Cond)) {
            (void)SDL_LockMutex(p_ctx->p_Mutex);
            if (0U != p_ctx->IsRunning) {
                (void)SDL_CondWaitTimeout(p_ctx->p_Cond, p_ctx->p_Mutex, 500U);
            }
            (void)SDL_UnlockMutex(p_ctx->p_Mutex);
        } else {
            SDL_Delay(500U);
        }

        /* Re-check running flag after waking up */
        if (0U == p_ctx->IsRunning) {
            /* Break immediately if shutdown was requested */
            break;
        }

        p_ctx->TickCount++;

        /* Lock UI mutex briefly while updating status circle pulse */
        (void)WindowLockUI();
        if (NULL != p_ctx->p_StatusCircle) {
            if (0U == (p_ctx->TickCount & 1U)) {
                p_ctx->p_StatusCircle->Type.Circle.Color.w = 0xFF00E676U;
            } else {
                p_ctx->p_StatusCircle->Type.Circle.Color.w = 0xFF00E5FFU;
            }
        }
        (void)WindowUnlockUI();

        /* Push update action to g_MainWindowUpdateQueue (wakes sleeping main thread) */
        (void)WindowQueuePush(WIN_ACTION_UPDATE_COMP, p_ctx->p_StatusCircle);

        /* Every 6 ticks (~3 seconds), push a synthetic EVENT_TYPE_MONITOR (wakes EventListenerThread) */
        if (0U == (p_ctx->TickCount % 6U)) {
            EventData_t mon_ev = {
                .Type        = EVENT_TYPE_MONITOR,
                .MouseX      = 0,
                .MouseY      = 0,
                .WheelDeltaX = 0,
                .WheelDeltaY = 0,
                .KeyCode     = 0U,
                .SysCode     = p_ctx->TickCount,
                .p_UserData  = NULL
            };
            /* Push monitor event and wake sleeping EventListenerThread */
            (void)EventPushToListenerQueue(&mon_ev);
        }
    }

    /* Return 0 when background thread exits */
    return 0;
}

/* Application entry point setting up single-instance guard, SysCall config, UI layers, and threads
 * @param argc[in] Argument count
 * @param argv[in] Argument vector
 * @return 0 on clean exit, non-zero on initialization failure
 * @note Press ESC or close the window to exit cleanly
 */
int main(int argc, char *argv[]) {
    Err_t       err;
    uint8_t     is_primary             = 1U;
    uint8_t     quit_flag              = 0U;
    SDL_Thread *p_misc_thread          = NULL;
    int         misc_thread_status     = 0;
    SDL_Thread *p_card_mon_thread      = NULL;
    int         card_mon_thread_status = 0;

    PARAMETTER_NOT_USED(argc);
    PARAMETTER_NOT_USED(argv);

    /* 0. Enforce single instance: if already running, toggle the 1st instance window and exit */
    err = WindowEnsureSingleInstance(&is_primary);
    if ((APP_SUCCESS != err) || (0U == is_primary)) {
        /* Exit secondary instance after signaling primary instance */
        return 0;
    }

    /* 1. Load YAML configs (app.yml, disp.yml) and query/populate xrandr monitors via AppSysCall */
    InitMonitorDataAndConfig();

    /* 2. Initialize main SDL2 window, renderer, UI mutex, and wakeup event */
    err = WindowInit(g_AppYamlCfg.WinTitle, g_AppYamlCfg.WinW, g_AppYamlCfg.WinH);
    ASSERT_EQUAL(APP_SUCCESS, err);
    if (0U != g_AppYamlCfg.WinHasColor) {
        g_MainWindow.BgColor.w = g_AppYamlCfg.WinColor;
    }

    /* 3. Initialize Layers -> Initialize Components -> Bind Events */
    InitDemoLayers();
    InitDemoComponents();
    BindDemoEvents();

    /* 4. Perform initial full window render */
    (void)WindowUpdateAll();

    /* 5. Start Sub-Thread 1: Dedicated Event Listener Thread (sleeps until events arrive) */
    err = EventListenerThreadStart();
    ASSERT_EQUAL(APP_SUCCESS, err);

    /* 6. Start Sub-Thread 2: Miscellaneous Background Thread with sleep/wakeup condition */
    g_MiscCtx.IsRunning      = 1U;
    g_MiscCtx.p_Mutex        = SDL_CreateMutex();
    g_MiscCtx.p_Cond         = SDL_CreateCond();
    g_MiscCtx.p_StatusCircle = &g_LayerComps[0][2];
    g_MiscCtx.p_StatusText   = &g_LayerComps[0][3];
    g_MiscCtx.TickCount      = 0U;
    p_misc_thread = SDL_CreateThread(MiscBackgroundThreadEntry, "MiscBackgroundThread", &g_MiscCtx);
    ASSERT_NOT_EQUAL(NULL, p_misc_thread);

    /* 7. Start Sub-Thread 3: Card Monitor Thread (overlap prevention, edge/corner snap, relative POS) */
    g_CardMonCtx.IsRunning    = 1U;
    g_CardMonCtx.p_Mutex      = SDL_CreateMutex();
    g_CardMonCtx.p_Cond       = SDL_CreateCond();
    g_CardMonCtx.p_Cards      = &g_LayerComps[1][0];
    g_CardMonCtx.p_CardTitles = &g_CardTitle[0];
    g_CardMonCtx.CardCount    = DEMO_CARD_COUNT;
    p_card_mon_thread = SDL_CreateThread(CardMonitorThreadEntry, "CardMonitorThread", &g_CardMonCtx);
    ASSERT_NOT_EQUAL(NULL, p_card_mon_thread);

    APP_INFO("Main loop started. Running ./AppExec again in another terminal will toggle this window.");

    /* 8. Main Thread Loop: Sleep until woken by an OS event or a sub-thread queue wakeup */
    while (0U == quit_flag) {
        /* Sleep until an OS SDL2 event or WindowWakeupMainThread signal arrives */
        (void)EventWaitOSOnMainThread(&quit_flag, 0U);

        /* Drain and execute any pending UI update actions from sub-threads */
        (void)WindowQueueProcessAll();
    }

    /* 9. Wake up and join MiscBackgroundThread cleanly */
    if (NULL != g_MiscCtx.p_Mutex) {
        (void)SDL_LockMutex(g_MiscCtx.p_Mutex);
        g_MiscCtx.IsRunning = 0U;
        if (NULL != g_MiscCtx.p_Cond) {
            (void)SDL_CondBroadcast(g_MiscCtx.p_Cond);
        }
        (void)SDL_UnlockMutex(g_MiscCtx.p_Mutex);
    } else {
        g_MiscCtx.IsRunning = 0U;
    }

    /* Wait for MiscBackgroundThread to finish */
    if (NULL != p_misc_thread) {
        SDL_WaitThread(p_misc_thread, &misc_thread_status);
    }
    if (NULL != g_MiscCtx.p_Cond) {
        SDL_DestroyCond(g_MiscCtx.p_Cond);
        g_MiscCtx.p_Cond = NULL;
    }
    if (NULL != g_MiscCtx.p_Mutex) {
        SDL_DestroyMutex(g_MiscCtx.p_Mutex);
        g_MiscCtx.p_Mutex = NULL;
    }

    /* 10. Wake up and join CardMonitorThread cleanly */
    if (NULL != g_CardMonCtx.p_Mutex) {
        (void)SDL_LockMutex(g_CardMonCtx.p_Mutex);
        g_CardMonCtx.IsRunning = 0U;
        if (NULL != g_CardMonCtx.p_Cond) {
            (void)SDL_CondBroadcast(g_CardMonCtx.p_Cond);
        }
        (void)SDL_UnlockMutex(g_CardMonCtx.p_Mutex);
    } else {
        g_CardMonCtx.IsRunning = 0U;
    }
    if (NULL != p_card_mon_thread) {
        SDL_WaitThread(p_card_mon_thread, &card_mon_thread_status);
    }
    if (NULL != g_CardMonCtx.p_Cond) {
        SDL_DestroyCond(g_CardMonCtx.p_Cond);
        g_CardMonCtx.p_Cond = NULL;
    }
    if (NULL != g_CardMonCtx.p_Mutex) {
        SDL_DestroyMutex(g_CardMonCtx.p_Mutex);
        g_CardMonCtx.p_Mutex = NULL;
    }

    /* Stop and wake up EventListenerThread */
    (void)EventListenerThreadStop();

    /* Destroy SDL2 window and synchronization primitives */
    (void)WindowDeinit();

    APP_INFO("Application exited cleanly.");

    /* Return 0 on normal termination */
    return 0;
}
