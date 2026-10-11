Dưới đây là nội dung hoàn chỉnh và chuẩn hóa cho file `GEMINI.md` sau khi cập nhật toàn bộ các góp ý trên:

```markdown
# GEMINI.md - Agent Instructions & Coding Standards

## 1. Operating Rules & Workflow
- **Language**: Respond to the user in Vietnamese, while keeping all code identifiers and code comments strictly in English.
- **Prerequisite Reading**: When requested to generate code, read and inspect all relevant module description files matching `mdf*.md` before implementation.
- **Clarification First**: If requirements, inputs, dependencies, or interfaces are missing or ambiguous, ask concise clarifying questions to confirm before generating complete solutions.
- **Concise Responses**: Keep answers direct, technical, and strictly focused on the user's prompt without unnecessary conversational filler.
- **Handling Incomplete Code / Dependencies**:
  - Use clear placeholders for components, functions, or assets that are not yet available (e.g., `/* TODO: PLACEHOLDER - <Description> */`).
  - Do not invent speculative APIs; explicitly note what needs to be filled in later.

---

## 2. Coding & Documentation Standards
- **Comment Style**:
  - Always write comments in English and keep them concise.
  - Exclusively use C-style comments `/* ... */`. Never use C++ style single-line comments `//`.
  - Reference to `.CodeTemplate/*` to see how we seperate all the sections in the source
- **Indentation**: Tab size = 4 spaces.
- **Naming Conventions**:
  - **Functions**: `PascalCase` (e.g., `Err2Str`, `SystemLog`).
  - **Types**: `PascalCase` with explicit suffixes:
    - Enum: `_e` (e.g., `enum AppStatus_e`).
    - Struct: `_st` (e.g., `struct Node_st`).
    - Union: `_un` (e.g., `union Node_un`).
    - Typedef: `_t` (e.g., `Err_t`, `AppStatus_t`).
  - **Macros & Enum Constants**: `UPPER_SNAKE_CASE` (e.g., `APP_SUCCESS`, `MASK32U`, `SYS_LOG`).
  - **Global Variables / Pointers**: Prefix `g_` or `gp_` + `PascalCase` (e.g., `g_SystemStatus`, `gp_Err2Str`).
  - **Local Variables & Parameters**: `snake_case` or lowercase `camelCase` (e.g., `err`, `pos`, `val`).
- **Control Flow Traceability**:
  - Add explanatory comments immediately before jump statements (`return`, `break`, `continue`, `goto`).
  - Add explanatory comments before steering/control-flow branching (`if`, `switch`, loops).
- **Paramatters check**: Parametters check is required, and shall be wrapped in PARAM_CHECK_EN macro
  ```c
  err_t foo(param_t param){
    #if (1U == PARAM_CHECK_EN)
      if(IsValid(param)){
        APP_ERR("Param is invalid!")
        return stat_err_invalid_param;
      }
    #endif
  }
  ```
- **Comment before branching**: Shall add comment before branching keyword (if it's complicated):
    + function call: Call to do what?
    + if/else: what condition?
    + for: what we repeat to get?
    + continue
    + break
    + return
- **App log**: Shall log the error before exit the app. If the log was added, comment before branching is no need.


---

## 3. Module & File Architecture (MDF Specifications)
- **Header Guards**:
  - Always use `#ifndef __<MODULE>_<FILE>_H__` and `#define __<MODULE>_<FILE>_H__`.
  - Always comment the closing directive: `#endif /* __<MODULE>_<FILE>_H__ */`.
- **Public Interface**: Each module must provide an `All.h` header serving as the public API exposed to other modules.
- **Private Interface**: Each module must provide a `__common__.h` header serving as the internal API shared across the current module only.
- **Build System**:
  - Makefile must automatically include all subdirectories in `./`.
  - Makefile must auto-detect and compile all `*.c` source files located within `./`.

---

## 4. Doxygen Documentation Format
Every public API function must be documented with a concise Doxygen block comment using the `/* ... */` syntax:

```c
/* <What function? is of purpose the this>
 * @param <param_name_0>[<in/out>] <param's purpose>
 * @return <Return description, if non-void value>
 * @note <if any>
 */
Return_t FunctionName(ParamType_t param_name_0) {
    /* Brief rationale before control flow or jump */
    return result;
}

```
