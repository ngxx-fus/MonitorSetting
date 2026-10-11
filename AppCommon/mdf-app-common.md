## DESC

- Provides utilities for all module:
    + log
    + bitwise
    + error types/enums & `DefaultErrorHandler`
    + circle queue (ring buffer)
    + assert macros (`Assert.h`)

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
    + Shall provide "__config__.h" as private config for entire module to call.
    + AppAssert macros (`Assert.h` — log `:ASSERT:` and trap into `DefaultErrorHandler()` on failure):
        * ASSERT(expression)    : bool
        * ASSERT_EQUAL(EXP, VAR)
        * ASSERT_NOT_EQUAL(EXP, VAR)
        * ASSERT_LESS_THAN(EXP, VAR)
        * ASSERT_GREATER_THAN(EXP, VAR)