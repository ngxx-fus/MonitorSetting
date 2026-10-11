## DESC

- Perform system call:
    + Execute script
    + Get system variable
    + Perform actions related to file/folder
    + Get the monitor info
        ```plaintext
            ConnectedMonitor:
                |----Label: "HDMI-1" (exact name of the monitor!)
                |----Displays:
                |       |----Display0:
                |       |       |----- W        : UINT16
                |       |       |----- H        : UINT16
                |       |       |----- FreqNum  : UINT16
                |       |       |----- Freq[] = {UINT16, UINT16, ...} 
                |       |----Display2:
                |       |       |----- W        : UINT16
                |       |       |----- H        : UINT16
                |       |       |----- FreqNum  : UINT16
                |       |       |----- Freq[] = {UINT16, UINT16, ...} 
        ```
    + UserPreferConfig: (Users r able to setting the these config in a yml-file)
        ```yml
        DisplayPreferSettings:
            - Disp0:
                - Label:   "eDP-1" # exact monitor connector name to auto-match (leave ""/empty for slot index)
                - Enabled: 1       # 1 = ON (active), 0 = OFF (--off), -1/empty = auto ON
                - Primary: 1       # 1 = Primary (--primary), 0 = Non-primary, -1/empty = auto primary
                - W:       1920    # leave -1/empty for auto select max available value. (otherwise, choose the closest available value)
                - H:       1080    # leave -1/empty for auto select max available value. (otherwise, choose the closest available value)
                - Freq:    60      # leave -1/empty for auto select max available value (otherwise, choose the closest available value)
                - PosX:    0       # leave -1/empty, this field will be skipped; otherwise, will be load as default in UI.
                - PosY:    0       # leave -1, this field will be skipped; otherwise, will be load as default in UI.
            - Disp1: # leave empty meaning that all childs have the default value is NEG-ONE (-1).
            - Disp2: # leave empty meaning that all childs have the default value is NEG-ONE (-1).
        ```
    + Any changes will be save back to the config.
    + App can be config via app.yml 
        ```yml
        App:
            - PreferDispPath: "/home/fus/Documents/MonitorSelection/UserConfig/disp.yml"
            - SnapThresholdPx: 25
            - MainWindow:
                - Title: "MonitorSelection - AppSDL2 Demo (ESC to Quit)"
                - W: 800
                - H: 520
                - IsVisible: 1
                - Mode: "FLOATING"               # FLOATING | MAXIMIZED
                - Layers:
                    - Layer0:
                ...
            - ApplyCallBack:  # Run after the Apply / Roll-back action done (up to cmd0..cmd31)
                - CmdNum: 4
                - cmd0: "/home/fus/.config/bspwm/scripts/background.sh"
                - cmd1: ""
        ```
    + Provide APIs to get xrandr (can be via xrandr.h)
    + Provide APIs to set xrandr!
    + Provide check if error while setting new config, and roll-back if failed!
- Makefile:
    + Include all dir entire this folder (./)
    + Detect and compiles all *.c files entire this folder (./)

- Coding rules:
    + Naming: PascalStyle for all
    + Simple Doxygen comment in English:
        
        ```c
        /* <What's the purpose of this function?>
         * @param <param's name0>[<in/out>] <param's purpose> 
         * @note <if any> */
        void Foo(param0) {

        }
        ```
- API:
    + Shall provide "All.h" as public API for other modules to call.
    + Shall provide "__common__.h" as private API for entire module to call.