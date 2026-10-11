## DESC

- Provides APIs for SDL2:
    + shall provide main windows with some layers
    + shall provide APIs to update these layer
        * Color mode: ARGB
        * Components architexture:
            ```plaintext
                Layer0
                    |---Comp0:
                    |           |---Class: (Class of this component)
                    |           |---Label: (name of this component)
                    |           |---Type: rect (Base on type, it will have other config)
                    |           |           |---Color:  AARRGGBB
                    |           |           |---Dim:
                    |           |           |   |---W
                    |           |           |   |---H
                    |           |           |   |---OffsetX (row)
                    |           |           |   |---OffsetY (col)
                    |           |           |   |---Rotation (0-360)
                    |           |           |---boder:
                    |           |               |----Size
                    |           |               |----Color: AARRGGBB
                    |           |---Listeners:
                    |           |           |---Event0:
                    |           |           |   |---Type:               (Type of listen event, will be defined in a enum for mouse/keyboard/system/acpi/monitor/usb...)
                    |           |           |   |---p_CallBack:         ()
                    |           |           |---Event1:
                    |           |           ...
                    |           |
                    |           |---p_ExtComps: (for child of this component, like text...)
                    |                       |---Count:          (The number of the ext comp)
                    |                       |---ExtComp0:
                    |                           |---CastType:   (we will have a enum to identify the type of a component)
                    |                           |---p_Comp:     (pointer to the ext component)
                    |---Comp1:
                    ...
                Layer1:
                    |---Comp0:
                    ...
                Layer2:
                    |---Comp0:
                    ...
            ```
        * Draw text/line/circle/rect(w-radius) | With filled or not:
            + Text supports multi-line newline (`\n`), word wrapping (`IsWrap` within `Dim.W`), horizontal alignment (`TEXT_ALIGN_LEFT`, `TEXT_ALIGN_CENTER`, `TEXT_ALIGN_RIGHT`), and vertical alignment (`TEXT_VALIGN_TOP`, `TEXT_VALIGN_MIDDLE`, `TEXT_VALIGN_BOTTOM`).
            + Supports `Dim.Rotation` (`0-360` degrees clockwise around center) for components and layer `RectClip` viewports via offscreen target textures (`SDL_RenderCopyEx`) and inverse-rotated mouse hit-testing.
        * Able to make scrollable layer (a layer can attach a component like a table):
            + Components inside a layer are positioned relative to `(0, 0)` of that layer.
            + Layer `RectClip` (viewport) is positioned relative to `(0, 0)` of `MainWindow` and supports `Rotation` (`0-360` degrees).
        * Able to update all components
        * Able to hide/show/adjust-szie of the main window (mode: maximize, floating)
        * Able to receive user custome signal
        * Shall support only instance, if App ís called, just toggle the 1st windows/instance
        * Able to add button with event (button is a component)
        * Able to add quick diag for yes/no
        * Provide timer (if SDL2 has it, just skip)
        * Event Listeners: A thread received the event from the OS via a circular queue (`CircleQueue_t`), then sequently exec all the registered callback Event (E.g: Event0).
        * MainWindow shall be updated from sub-thread via an `g_MainWindowUpdateQueue` (backed by `CircleQueue_t`).
        ```plaintext
            g_MainWindowUpdateQueue
                |---Action0:
                |       |---Type: (Type of action, may be an union)
                |       |---p_Comp: (Pointer to the dest component)
                |---Action1
                ...
        ```
        * Support drag/drop component (currently i have no idea, AI will help me add it!)
        * Layer*.Comp*: Shall a struct
        * Layer*.Comp*.Type: Shall a union
        * Layer*.Comp*.Listeners.Event*: Shall a union
        * Layer*.Comp*.Listeners.p_ExtComps->ExtComp*: Shall a struct
        * Layer*.Comp*.Listeners.p_ExtComps->ExtComp*: Shall a union
    + error types/enums

- Safety:
    + Add mutext/semaphore is need to prevent the UI lock
    + Allow a thread goes to sleep, then be wake up with a message/event (e.g: evetn-thread/ main-thread)

- Inheritance:
    + Shall be inherent from `AppCommon`

- Makefile:
    + Actions: all, clean, build
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
    + Shall provide "__config__.h" as private config for entire module to call.