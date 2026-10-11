/* file-desc **************************************************************************************
 * @filename    Exec.h
 * @brief       Shell script execution, system variable access, and file/folder helpers.
 * @note        Thin, error-checked wrappers around popen/system/getenv/stat.
 **************************************************************************************************/

#ifndef __APP_SYSCALL_EXEC_H__
#define __APP_SYSCALL_EXEC_H__

/* headers ****************************************************************************************
 **************************************************************************************************/

#include "../AppCommon/All.h"
#include "./__config__.h"
#include <stdint.h>

/* macros *****************************************************************************************
 **************************************************************************************************/

/* definitions ************************************************************************************
 **************************************************************************************************/

/* exported variables *****************************************************************************
 **************************************************************************************************/

/* exported functions *****************************************************************************
 **************************************************************************************************/

Err_t SysCallExecScript(const_str_t cmd, int32_t *p_exit_code);
Err_t SysCallExecCapture(const_str_t cmd, char *p_out, uint32_t out_size, int32_t *p_exit_code);
Err_t SysCallGetEnv(const_str_t name, char *p_out, uint32_t out_size);
Err_t SysCallPathExists(const_str_t path, uint8_t *p_exists, uint8_t *p_is_dir);
Err_t SysCallMakeDir(const_str_t path);
Err_t SysCallReadFile(const_str_t path, char *p_out, uint32_t out_size, uint32_t *p_read_len);
Err_t SysCallWriteFile(const_str_t path, const_str_t content);
Err_t SysCallCopyFile(const_str_t src_path, const_str_t dst_path);

/* reserved ***************************************************************************************
 **************************************************************************************************/

#endif /* __APP_SYSCALL_EXEC_H__ */

