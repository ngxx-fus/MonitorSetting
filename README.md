# MonitorSelection

A multi-monitor layout and display property management application written in **C (C11)** powered by **SDL2** and **xrandr**, following the modular MDF architecture and coding standards defined in [GEMINI.md](file:///home/fus/Documents/MonitorSelection/GEMINI.md).

---

## 1. Key Features
- **Intelligent Drag-and-Drop & Magnetic Snapping (Anti-Overlap & Edge/Corner Snap)**:
  - Monitor cards (`eDP-1`, `DP-2`, `HDMI-1`, ...) never overlap one another.
  - Automatic edge snapping and corner/vertex snapping when within `SnapThresholdPx` (`25 px`).
- **Automatic Top-Left `(0, 0)` Origin & Real Resolution Accumulation**:
  - Automatically picks the top-left-most enabled (`ON`) monitor card as the primary coordinate origin `POS(0, 0)`.
  - Computes real display pixel coordinates `POS(<PosX>, <PosY>)` by accumulating the actual resolution (`W`, `H`) of adjacent monitors.
  - Supports customizable dynamic text templates via `TextStrFmt` in [UserConfig/app.yml](file:///home/fus/Documents/MonitorSelection/UserConfig/app.yml) with placeholders: `<Label>`, `<Primary>`, `<Res>`, `<W>`, `<H>`, `<Freq>`, `<PosX>`, `<PosY>`, `<State>`.
- **Right-Side Vertical Property Panel (`Layer 2`)**:
  - Clicking or dragging/dropping a monitor card automatically populates the right-side `PROP` panel (`Layer 2`) with that monitor's live settings.
  - Interactively cycle through supported resolutions (`< Res`, `Res >`), refresh rates (`< Freq`, `Freq >`), power state (`State: ON/OFF`), and primary monitor status (`Primary`).
- **Declarative UI & Display Configuration (`UserConfig/app.yml` & `UserConfig/disp.yml`)**:
  - Full 32-bit ARGB color support (`0xAARRGGBB`, including true `Alpha = 0x00` transparency on X11 compositors) for both the main window (`MainWindow -> Color`) and individual components (`Comp`).
  - Per-component `Font` selection (resolved via `fc-match` with `:weight=regular|bold:slant=roman|italic`), `FontSize`, and `Format` (`Bold`, `Italic`, `Algin -> Vertical / Horizona`).
  - Post-Apply shell callback commands (`ApplyCallBack`: `cmd0..cmd31`) executed asynchronously on a **detached background worker thread** so the UI never freezes.
- **Single-Instance Guard & Multi-Threaded Event Engine**:
  - Enforces a single running instance; launching `./AppExec` a second time sends `SIGUSR1` to toggle (hide/show) the primary instance's window.
  - 4 persistent threads + 1 detached worker thread operate strictly on sleep/wakeup primitives (`SDL_CondWait` / `SDL_WaitEventTimeout`), consuming `0% CPU` when idle.

---

## 2. Directory Structure & Module Design Files (MDF)

| Path | Description | MDF Specification |
| :--- | :--- | :--- |
| [main.c](file:///home/fus/Documents/MonitorSelection/main.c) | Application entry point, 3-Layer UI setup, 4-thread orchestration, and event callbacks. | [mdf-app.md](file:///home/fus/Documents/MonitorSelection/mdf-app.md) |
| [AppCommon/](file:///home/fus/Documents/MonitorSelection/AppCommon) | Core foundation: primitive types, `Err_t` error codes, logging (`Log`), `Color_t`, and `CircleQueue_t`. | [mdf-app-common.md](file:///home/fus/Documents/MonitorSelection/AppCommon/mdf-app-common.md) |
| [AppSDL2/](file:///home/fus/Documents/MonitorSelection/AppSDL2) | SDL2 UI engine: `Window` (32-bit ARGB X11 visual), `Layer` (`RectClip`), `Component`, `Draw` (font cache), `Event`. | [mdf-app-sdl2.md](file:///home/fus/Documents/MonitorSelection/AppSDL2/mdf-app-sdl2.md) |
| [AppSysCall/](file:///home/fus/Documents/MonitorSelection/AppSysCall) | OS integration: shell execution (`Exec`), `xrandr` control (`Xrandr`), and YAML parsers (`UserConfig`). | [mdf-app-syscall.md](file:///home/fus/Documents/MonitorSelection/AppSysCall/mdf-app-syscall.md) |
| [UserConfig/](file:///home/fus/Documents/MonitorSelection/UserConfig) | UI configuration [app.yml](file:///home/fus/Documents/MonitorSelection/UserConfig/app.yml) and display preferences [disp.yml](file:///home/fus/Documents/MonitorSelection/UserConfig/disp.yml). | — |

---

## 3. System Architecture (Text Flows)

### 3.1 Thread & Queue Overview

```text
+=========================================================================================+
|                                    PROCESS: ./AppExec                                   |
|                                                                                         |
|  [OS / X11 / SDL2 Input Events]                   [2nd Instance ./AppExec]              |
|     (Mouse, Key, WindowClose)                                |                          |
|                 |                                            | kill(pid, SIGUSR1)       |
|                 v                                            v                          |
|  +------------------------------+              +---------------------------+            |
|  |       1. MAIN THREAD         |<-------------| OnPosixSignalReceived()   |            |
|  |  EventWaitOSOnMainThread()   |              | g_SigTogglePending = 1    |            |
|  |  WindowQueueProcessAll()     |              +---------------------------+            |
|  |  WindowUpdateAll() (OpenGL)  |                                                       |
|  +--------------+---------------+                                                       |
|                 |                                                                       |
|                 | EventPushToListenerQueue()          WindowQueuePush() + SDL_PushEvent |
|                 v (g_EventQueue + CondSignal)                    ^                      |
|  +------------------------------+                                |                      |
|  |   2. EVENT LISTENER THREAD   |--------------------------------+                      |
|  |  EventListenerThreadEntry()  |                                |                      |
|  |  - Hit-test Layer 2 -> 1 -> 0|                                |                      |
|  |  - Drag & Drop tracking      |---> WakeCardMonitorThread()    |                      |
|  |  - Callbacks:                |        |                       |                      |
|  |    + OnCardSelectClick       |        v                       |                      |
|  |    + OnCardDragStart/Drop    |  +--------------------------+  |                      |
|  |    + OnBtnRes/Freq/Power/Pri |  |  3. CARD MONITOR THREAD  |--+                      |
|  |    + OnBtnApply/RollbackClick|  | CardMonitorThreadEntry() |  |                      |
|  +--------------+---------------+  | - Anti-overlap & Snap    |  |                      |
|                 |                  | - Calc POS(x,y) & Texts  |  |                      |
|                 |                  +--------------------------+  |                      |
|                 | Spawn detached                                 |                      |
|                 v (when Apply/Rollback)                          |                      |
|  +------------------------------+  +--------------------------+  |                      |
|  | 5. ASYNC APPLY WORKER THREAD |  | 4. MISC BACKGROUND THREAD|--+                      |
|  |  ApplyCallbackThreadEntry()  |  |MiscBackgroundThreadEntry |                         |
|  |  - Run ApplyCallBack cmd0..N |  |- Pulse StatusCircle 500ms|                         |
|  +------------------------------+  +--------------------------+                         |
+=========================================================================================+
```

---

### 3.2 Flow 1: Startup & Initialization Flow

```text
main() [main.c]
  |
  +--> [Step 0] WindowEnsureSingleInstance(&is_primary) [AppSDL2/Window.c]
  |      |-- Read "/tmp/MonitorSelection_SingleInstance.pid"
  |      |-- If primary PID is alive -> send SIGUSR1 and exit immediately (return 0)
  |      +-- If primary instance     -> write current PID, register signal(SIGUSR1, SIGUSR2)
  |
  +--> [Step 1] InitMonitorDataAndConfig() [main.c]
  |      |-- Load UserConfig/app.yml  (SysCallAppConfigLoad -> g_AppYamlCfg)
  |      |-- Load UserConfig/disp.yml (SysCallDispPreferLoad -> g_DispPrefer)
  |      |-- Query connected displays (SysCallXrandrQuery -> g_MonitorList)
  |      |-- Match display preferences (SysCallDispPreferApplyToMonitors -> g_ActiveCfg)
  |      +-- Apply initial xrandr config (SysCallXrandrApply) & save snapshot g_BackupCfg
  |
  +--> [Step 2] WindowInit(WinTitle, WinW, WinH) [AppSDL2/Window.c]
  |      |-- Query XOpenDisplay for a 32-bit TrueColor ARGB visual -> set SDL_VIDEO_X11_VISUALID
  |      |-- Keep X11 compositor enabled (SDL_VIDEO_X11_NET_WM_BYPASS_COMPOSITOR = "0")
  |      |-- Create SDL_WINDOW_OPENGL window + SDL_Renderer with SDL_BLENDMODE_BLEND
  |      +-- Set window background clear color: g_MainWindow.BgColor = g_AppYamlCfg.WinColor
  |
  +--> [Step 3] Initialize 3 UI Layers & Bind Events [main.c]
  |      |-- InitDemoLayers()     : Setup Layer0 (Full), Layer1 (Canvas 500x380), Layer2 (PROP 210x380)
  |      |-- InitDemoComponents() :
  |      |     |-- Initialize default components in Layer0, Layer1 (3 cards), Layer2 (12 PROP items)
  |      |     |-- Apply g_AppYamlCfg overrides (Position, Color, Font, FontSize, Bold/Italic/Align, TextStrFmt)
  |      |     |-- Call SelectMonitorCard(0) & UpdateRelativePositionsAndTexts()
  |      +-- BindDemoEvents()     : Register mouse/drag/drop callbacks for each interactive component
  |
  +--> [Step 4] WindowUpdateAll() [AppSDL2/Window.c]
  |      +-- Render the initial frame to the screen
  |
  +--> [Step 5..7] Launch 3 background threads:
  |      |-- EventListenerThreadStart() -> EventListenerThreadEntry
  |      |-- SDL_CreateThread()         -> MiscBackgroundThreadEntry
  |      +-- SDL_CreateThread()         -> CardMonitorThreadEntry
  |
  +--> [Step 8] Main Event Loop:
         while (!quit_flag) {
             EventWaitOSOnMainThread(&quit_flag, 0);
             WindowQueueProcessAll();
         }
```

---

### 3.3 Flow 2: Config Loading & Matching (`app.yml`, `disp.yml`, and `xrandr`)

```text
+---------------------------------------------------------------------------------------+
|                          CONFIG LOADING & MERGING PIPELINE                            |
+---------------------------------------------------------------------------------------+

  [1] UserConfig/app.yml
       |
       | SysCallAppConfigLoad("UserConfig/app.yml", &g_AppYamlCfg) [AppSysCall/UserConfig.c]
       v
  +-----------------------------------------------------------------------------------+
  | g_AppYamlCfg:                                                                     |
  |  - PreferDispPath : path to disp.yml                                              |
  |  - SnapThresholdPx: magnetic snap distance in pixels (default 25px)               |
  |  - MainWindow     : Title, Color (0xAARRGGBB), W, H, Mode, Font, Format           |
  |  - Comps[3][16]   : Label, OffsetX/Y, W/H, Color, Radius, Border, Font, FontSize, |
  |                     Format (Bold, Italic, Align, VAlign), TextStr, TextStrFmt     |
  |  - ApplyCmds[32]  : post-Apply shell commands (cmd0 .. cmd31)                     |
  +-----------------------------------------------------------------------------------+
       |
       | Load display preferences from g_AppYamlCfg.PreferDispPath
       v
  [2] UserConfig/disp.yml                [3] X11 Subsystem (xrandr --query)
       |                                      |
       | SysCallDispPreferLoad(&g_DispPrefer) | SysCallXrandrQuery(&g_MonitorList)
       v                                      v
  +-------------------------------+      +--------------------------------------------+
  | g_DispPrefer.Disp[0..7]       |      | g_MonitorList.Monitors[0..2]               |
  |  - Label: "eDP-1" / "HDMI-1"  |      |  - Label ("eDP-1", "DP-2", "HDMI-1")       |
  |  - Enabled: 1/0/-1            |      |  - Supported Modes (W, H) & Freqs (Hz)     |
  |  - Primary: 1/0/-1            |      |  - Current state (CurW, CurH, PosX, PosY)  |
  |  - W, H, Freq (-1 = Auto MAX) |      +---------------------+----------------------+
  |  - PosX, PosY (-1 = Auto)     |                            |
  +---------------+---------------+                            |
                  |                                            |
                  +--------------------+-----------------------+
                                       |
                                       | SysCallDispPreferApplyToMonitors()
                                       v
  +-----------------------------------------------------------------------------------+
  | Two-Pass Preference Matching (SysCallDispPreferFindMatch):                        |
  |  - Pass 1: Exact `Label` match against connected monitor connector name           |
  |  - Pass 2: Fallback to the next unclaimed Disp<N> slot if Label does not match    |
  |  - If W = -1, H = -1, Freq = -1 -> SysCallXrandrFindClosestMode() automatically   |
  |    selects the MAXIMUM resolution (Max W*H) and MAXIMUM refresh rate (Max Hz)!    |
  |  - Converts real display PosX, PosY into initial Canvas card offsets (155x95)     |
  +-----------------------------------------------------------------------------------+
                                       |
                                       v
  +-----------------------------------------------------------------------------------+
  | Layer 1 Card Style Matching (GetLayer1CardYamlCfg in main.c):                     |
  |  - Matches `Layer1 -> Comp0..Comp2` by `Label` ("HDMI-1", "DP-2", "eDP-1")        |
  |  - Applies per-card `Bold`, `Italic`, `Font`, `FontSize`, and `TextStrFmt`!       |
  +-----------------------------------------------------------------------------------+
```

---

### 3.4 Flow 3: User Select / Drag / Drop Interaction Flow

```text
[User presses left mouse button / drags mouse / releases mouse button]
  |
  v
[1. Main Thread] EventWaitOSOnMainThread() [AppSDL2/Event.c]
  |-- Reads SDL_Event from OS (SDL_MOUSEBUTTONDOWN, SDL_MOUSEMOTION, SDL_MOUSEBUTTONUP)
  |-- Translates into EventData_t (EVENT_TYPE_MOUSE_DOWN / MOVE / UP)
  +-- EventPushToListenerQueue(&ev) --> wakes EventListenerThread
        |
        v
[2. Event Listener Thread] EventListenerThreadEntry() [AppSDL2/Event.c]
  |-- Iterates top-down across layers (Layer 2 -> Layer 1 -> Layer 0)
  |-- Performs hit-testing via IsPointInComp(p_comp, layer_ox, layer_oy, mx, my)
  |
  |-- CASE A: Mouse Down on a Monitor Card (EVENT_TYPE_MOUSE_DOWN on Layer 1 Card)
  |     |-- Invokes OnCardSelectClick() [main.c]:
  |     |     |-- SelectMonitorCard(card_idx): highlights selected card border in Cyan (0xFF00E5FF)
  |     |     |-- UpdatePropertyPanelTexts(): refreshes right-side PROP panel (Layer 2)
  |     |     +-- WindowQueuePush(WIN_ACTION_UPDATE_ALL) -> requests Main Thread redraw
  |     +-- If p_comp->IsDraggable == 1:
  |           +-- Initializes g_DragDropCtx (records GrabOffsetX/Y, StartCompX/Y)
  |
  |-- CASE B: Mouse Move while dragging (EVENT_TYPE_MOUSE_MOVE with g_DragDropCtx.IsDragging == 1)
  |     |-- On first motion: dispatches EVENT_TYPE_DRAG_START -> calls OnCardDragStart()
  |     |-- Updates card position live (clamped within Layer 1 Canvas 500x380):
  |     |     new_x = MouseX - Layer1.OffsetX - GrabOffsetX
  |     |     new_y = MouseY - Layer1.OffsetY - GrabOffsetY
  |     |     CompSetPosition(p_DragComp, new_x, new_y)
  |     +-- WindowQueuePush(WIN_ACTION_UPDATE_COMP, p_DragComp) -> smooth live redraw
  |
  +-- CASE C: Mouse Up after dragging (EVENT_TYPE_MOUSE_UP)
        |-- Ends drag state (IsDragging = 0), dispatches EVENT_TYPE_DROP -> calls OnCardDrop() [main.c]:
        |     |-- SelectMonitorCard(idx)
        |     +-- WakeCardMonitorThread() -> wakes CardMonitorThread immediately!
        v
[3. Card Monitor Thread] CardMonitorThreadEntry() [main.c]
  |-- Locks UI Mutex: WindowLockUI()
  |-- Calls MakeSnapMagnetsTogether(p_Cards, 3, SnapThresholdPx) [AppSDL2/Component.c]:
  |     |-- (a) Anti-overlap: pushes overlapping cards apart along the minimum penetration axis
  |     |-- (b) Edge Snap: snaps parallel edges flush together when within 25px
  |     +-- (c) Corner Snap: snaps vertices together when within 25px
  |-- Calls UpdateRelativePositionsAndTexts() [main.c]:
  |     |-- 1. Finds active (ON) card with minimum (OffsetX + OffsetY) -> Origin POS(0, 0)
  |     |-- 2. Propagates real display coordinates across adjacent cards by ACCUMULATING
  |     |      actual monitor width/height (mon_w, mon_h) (e.g. 0,0 -> 1920,0 / 0,1080)
  |     |-- 3. Expands `TextStrFmt` template (<Label>, <Res>, <Freq>, <PosX>, <PosY>)
  |     +-- 4. Synchronizes the right-side PROP panel (UpdatePropertyPanelTexts)
  |-- Unlocks UI Mutex: WindowUnlockUI()
  +-- WindowQueuePush(WIN_ACTION_UPDATE_ALL) --> wakes Main Thread to present updated frame!
```

---

### 3.5 Flow 4: Launching a 2nd Instance (Single-Instance Toggle via `SIGUSR1`)

```text
[Terminal 2: User runs `./AppExec` a 2nd time]          [Primary Instance already running]
  |                                                       |
  v                                                       |
main() -> WindowEnsureSingleInstance(&is_primary)         |
  |                                                       |
  |-- Opens "/tmp/MonitorSelection_SingleInstance.pid"    |
  |-- Reads `existing_pid` of Primary Instance            |
  |-- Verifies `kill(existing_pid, 0) == 0` (alive)       |
  |-- Sends signal: `kill(existing_pid, SIGUSR1)` ------->| OnPosixSignalReceived(SIGUSR1)
  |-- Sets `*p_is_primary = 0`                            |   +-- Sets `g_SigTogglePending = 1`
  +-- Exits cleanly (`return 0` in main)                  |
                                                          v
                                                EventWaitOSOnMainThread() (50ms timeout)
                                                  |-- Calls WindowCheckSingleInstanceSignal()
                                                  |-- Detects `g_SigTogglePending == 1`:
                                                  |     |-- Clears `g_SigTogglePending = 0`
                                                  |     +-- WindowQueuePush(WIN_ACTION_TOGGLE_WINDOW)
                                                  v
                                                WindowQueueProcessAll() -> WindowToggle()
                                                  |-- If visible (IsVisible == 1) -> WindowHide()
                                                  +-- If hidden  (IsVisible == 0) -> WindowShow()
```

---

## 4. Maintenance Guide: "Where Do I Edit What After 1 Month?"

Use this quick-reference cheat sheet whenever you return to the project to customize or extend it:

### 4.1 Customizing UI Layout, Colors, Fonts, or Post-Apply Scripts (No Recompile Needed!)
- **Edit [UserConfig/app.yml](file:///home/fus/Documents/MonitorSelection/UserConfig/app.yml)**:
  - **Outer Window Background & Bottom Bar Color**: Edit `MainWindow -> Color` (line ~80, e.g. `0xFF12161F` for opaque dark, or `0x0012161F` for transparent).
  - **Inner Rounded Container (`MainPanel`)**: Edit `Layer0 -> Comp0`. To enclose the `Roll-back` and `Apply` buttons (at `Y=500`) inside `MainPanel`, increase `H` from `460` to `502`.
  - **Status Bar Position (`Ready | ...`)**: Edit `OffsetX`, `OffsetY` on `Layer0 -> Comp2` (`StatusCircle`) and `Layer0 -> Comp3` (`HeaderText`).
  - **Monitor Card Text Template**: Edit `TextStrFmt` inside `Layer1 -> Comp0..Comp2` (supports `<Label>`, `<Primary>`, `<Res>`, `<W>`, `<H>`, `<Freq>`, `<PosX>`, `<PosY>`, `<State>`).
  - **Post-Apply Shell Commands (polybar, wallpaper, bspwm...)**: Edit `ApplyCallBack:` at the bottom of `app.yml` (`cmd0`, `cmd1`, `cmd2`... up to `cmd31`).
- **Edit [UserConfig/disp.yml](file:///home/fus/Documents/MonitorSelection/UserConfig/disp.yml)**:
  - Change default per-monitor startup preferences (`Label`, `Enabled`, `Primary`, or set `W: -1`, `H: -1`, `Freq: -1` to auto-select maximum resolution and refresh rate).

### 4.2 Modifying C Source Code (File & Function Lookup Table)

| Task / Modification | Target File(s) | Key Function / Struct |
| :--- | :--- | :--- |
| **1. Add a new YAML key to `app.yml` or `disp.yml`** | [AppSysCall/UserConfig.h](file:///home/fus/Documents/MonitorSelection/AppSysCall/UserConfig.h)<br>[AppSysCall/UserConfig.c](file:///home/fus/Documents/MonitorSelection/AppSysCall/UserConfig.c) | - Add field to `struct AppCompYamlCfg_st` or `struct AppYamlConfig_st`<br>- Parse key in `SysCallAppConfigLoad()` (`UserConfig.c`)<br>- Apply override in `InitDemoComponents()` (`main.c`) |
| **2. Add a new placeholder token to `TextStrFmt` (e.g. `<ModeCount>`)** | [main.c](file:///home/fus/Documents/MonitorSelection/main.c) | - [`FormatMonitorTemplateStr()`](file:///home/fus/Documents/MonitorSelection/main.c#L394-L476): add an `else if (0 == strncmp(p_cur, "<Token>", len))` branch |
| **3. Change how `POS(x, y)` coordinates are calculated** | [main.c](file:///home/fus/Documents/MonitorSelection/main.c) | - [`UpdateRelativePositionsAndTexts()`](file:///home/fus/Documents/MonitorSelection/main.c#L571-L785):<br>  + Step 1: Find top-left origin card `origin_idx`<br>  + Step 2: Accumulate `mon_w` / `mon_h` across adjacent cards<br>  + Step 3: Normalize negative coordinates & format card strings |
| **4. Adjust anti-overlap or magnetic edge/corner snapping** | [AppSDL2/Component.c](file:///home/fus/Documents/MonitorSelection/AppSDL2/Component.c) | - [`CompSnapMagnetsTogether()`](file:///home/fus/Documents/MonitorSelection/AppSDL2/Component.c#L420-L600) and [`MakeSnapMagnetsTogether()`](file:///home/fus/Documents/MonitorSelection/AppSDL2/Component.c#L602-L620) |
| **5. Modify the generated `xrandr` command on `Apply`** | [AppSysCall/Xrandr.c](file:///home/fus/Documents/MonitorSelection/AppSysCall/Xrandr.c) | - [`BuildXrandrCmd()`](file:///home/fus/Documents/MonitorSelection/AppSysCall/Xrandr.c#L465-L530) (builds `xrandr --output ...` command string)<br>- [`SysCallXrandrApplySafe()`](file:///home/fus/Documents/MonitorSelection/AppSysCall/Xrandr.c#L600-L660) (applies & auto-rolls back on error) |
| **6. Change button click handlers (`Apply`, `Roll-back`, `< Res`, `Primary`, `ON/OFF`)** | [main.c](file:///home/fus/Documents/MonitorSelection/main.c) | - `OnBtnApplyClick()`, `OnBtnRollbackClick()`<br>- `OnBtnResPrevClick()`, `OnBtnResNextClick()`<br>- `OnBtnFreqPrevClick()`, `OnBtnFreqNextClick()`<br>- `OnBtnPowerToggleClick()`, `OnBtnPrimaryClick()` |
| **7. Change shape rendering or font loading (`fc-match` / `SDL_ttf`)** | [AppSDL2/Draw.c](file:///home/fus/Documents/MonitorSelection/AppSDL2/Draw.c) | - `ResolveFontFileViaFcMatch()` & `GetCachedTtfFont()` (caches by `Name, Size, Bold, Italic`)<br>- `DrawRect()`, `DrawCircle()`, `DrawLine()`, `DrawText()` |
| **8. Modify X11 32-bit ARGB transparency or Single-Instance PID lock** | [AppSDL2/Window.c](file:///home/fus/Documents/MonitorSelection/AppSDL2/Window.c) | - `WindowEnsureSingleInstance()` (checks PID & sends `SIGUSR1`)<br>- `WindowInit()` (queries 32-bit ARGB visual & creates OpenGL renderer)<br>- `WindowUpdateAll()` (clears with `g_MainWindow.BgColor` & renders layers) |

---

## 5. System Requirements & Build Instructions

### Install Dependencies (Ubuntu / Debian)
```bash
sudo apt-get update
sudo apt-get install -y build-essential libsdl2-dev libsdl2-gfx-dev libsdl2-ttf-dev libx11-dev fontconfig x11-xserver-utils doxygen
```

### Build & Run
```bash
# Build the entire project
make clean all

# Run the application (running a second instance toggles the window)
./AppExec

# Generate Doxygen HTML documentation (output in docs/html/index.html)
make doc
```
