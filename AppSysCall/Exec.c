/* file-desc **************************************************************************************
 * @filename    Exec.c
 * @brief       Implementation of shell execution, environment, and file/folder helpers.
 * @note        Exit codes are decoded with WEXITSTATUS; -1 means abnormal termination.
 **************************************************************************************************/

/* headers ****************************************************************************************
 **************************************************************************************************/

#include "./__common__.h"
#include "./Exec.h"
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>

/* macros *****************************************************************************************
 **************************************************************************************************/

/* definitions ************************************************************************************
 **************************************************************************************************/

/* exported variables *****************************************************************************
 **************************************************************************************************/

/* public variables *******************************************************************************
 **************************************************************************************************/

/* private variables ******************************************************************************
 **************************************************************************************************/

/* exported functions *****************************************************************************
 **************************************************************************************************/

/* public functions *******************************************************************************
 **************************************************************************************************/

Err_t SysCallExecScript(const_str_t cmd, int32_t *p_exit_code);
Err_t SysCallExecCapture(const_str_t cmd, char *p_out, uint32_t out_size, int32_t *p_exit_code);
Err_t SysCallGetEnv(const_str_t name, char *p_out, uint32_t out_size);
Err_t SysCallPathExists(const_str_t path, uint8_t *p_exists, uint8_t *p_is_dir);
Err_t SysCallMakeDir(const_str_t path);
Err_t SysCallReadFile(const_str_t path, char *p_out, uint32_t out_size, uint32_t *p_read_len);
Err_t SysCallWriteFile(const_str_t path, const_str_t content);
Err_t SysCallCopyFile(const_str_t src_path, const_str_t dst_path);

/* private functions ******************************************************************************
 **************************************************************************************************/

static int32_t DecodeExitStatus(int status);

/* function implementations ***********************************************************************
 **************************************************************************************************/

/* Convert a raw wait status into a process exit code
 * @param status[in] Raw status returned by system()/pclose()
 * @return Exit code (0-255), or -1 if the child terminated abnormally
 */
static int32_t DecodeExitStatus(int status) {
    /* Abnormal termination (signal, fork failure) is reported as -1 */
    if ((-1 == status) || (0 == WIFEXITED(status))) {
        return -1;
    }
    /* Return normal exit code */
    return (int32_t)WEXITSTATUS(status);
}

/* Execute a shell command/script and wait for completion
 * @param cmd[in] Null-terminated shell command line
 * @param p_exit_code[out] Process exit code (0 = success, -1 = abnormal termination)
 * @return APP_SUCCESS if the command ran and exited with 0, APP_ERROR otherwise
 */
Err_t SysCallExecScript(const_str_t cmd, int32_t *p_exit_code) {
    int32_t exit_code;

    #if (1U == PARAM_CHECK_EN)
    if ((NULL == cmd) || (NULL == p_exit_code)) {
        APP_ERR("Null pointer in SysCallExecScript!");
        return APP_ERROR_NULL;
    }
    #endif /* (1U == PARAM_CHECK_EN) */

    exit_code    = DecodeExitStatus(system(cmd));
    *p_exit_code = exit_code;

    if (0 != exit_code) {
        APP_ERR("Command failed (exit=%d): %s", exit_code, cmd);
        return APP_ERROR;
    }

    /* Return success after command exits with code 0 */
    return APP_SUCCESS;
}

/* Execute a shell command and capture its standard output into a buffer
 * @param cmd[in] Null-terminated shell command line
 * @param p_out[out] Destination buffer (always null-terminated, truncated if too small)
 * @param out_size[in] Size of p_out in bytes
 * @param p_exit_code[out] Process exit code (0 = success, -1 = abnormal termination)
 * @return APP_SUCCESS if the command exited with 0, APP_ERROR otherwise
 */
Err_t SysCallExecCapture(const_str_t cmd, char *p_out, uint32_t out_size, int32_t *p_exit_code) {
    FILE    *p_pipe;
    uint32_t used = 0U;
    size_t   n;

    #if (1U == PARAM_CHECK_EN)
    if ((NULL == cmd) || (NULL == p_out) || (NULL == p_exit_code)) {
        APP_ERR("Null pointer in SysCallExecCapture!");
        return APP_ERROR_NULL;
    }
    if (0U == out_size) {
        APP_ERR("out_size is zero!");
        return APP_ERROR_INVALID_PARAM;
    }
    #endif /* (1U == PARAM_CHECK_EN) */

    p_out[0]     = '\0';
    *p_exit_code = -1;

    p_pipe = popen(cmd, "r");
    if (NULL == p_pipe) {
        APP_ERR("popen failed: %s", cmd);
        return APP_ERROR;
    }

    /* Read stdout until EOF or until the output buffer is full */
    while ((used + 1U) < out_size) {
        n = fread(&p_out[used], 1U, (size_t)(out_size - 1U - used), p_pipe);
        /* Stop reading on EOF or read error */
        if (0U == n) {
            break;
        }
        used += (uint32_t)n;
    }
    p_out[used] = '\0';

    *p_exit_code = DecodeExitStatus(pclose(p_pipe));
    if (0 != *p_exit_code) {
        APP_ERR("Command failed (exit=%d): %s", *p_exit_code, cmd);
        return APP_ERROR;
    }

    /* Return success after capturing output */
    return APP_SUCCESS;
}

/* Read a system/environment variable
 * @param name[in] Variable name (e.g. "DISPLAY", "HOME")
 * @param p_out[out] Destination buffer for the value (null-terminated)
 * @param out_size[in] Size of p_out in bytes
 * @return APP_SUCCESS if found, APP_ERROR if the variable is not set
 */
Err_t SysCallGetEnv(const_str_t name, char *p_out, uint32_t out_size) {
    const char *p_val;

    #if (1U == PARAM_CHECK_EN)
    if ((NULL == name) || (NULL == p_out)) {
        APP_ERR("Null pointer in SysCallGetEnv!");
        return APP_ERROR_NULL;
    }
    if (0U == out_size) {
        APP_ERR("out_size is zero!");
        return APP_ERROR_INVALID_PARAM;
    }
    #endif /* (1U == PARAM_CHECK_EN) */

    p_out[0] = '\0';
    p_val    = getenv(name);
    /* Report missing variable without logging (caller decides severity) */
    if (NULL == p_val) {
        return APP_ERROR;
    }

    (void)snprintf(p_out, out_size, "%s", p_val);

    /* Return success after copying value */
    return APP_SUCCESS;
}

/* Check whether a file or folder exists
 * @param path[in] Path to check
 * @param p_exists[out] 1U if the path exists, 0U otherwise
 * @param p_is_dir[out] 1U if the path is a directory (may be NULL if not needed)
 * @return APP_SUCCESS on success, or APP_ERROR_NULL
 */
Err_t SysCallPathExists(const_str_t path, uint8_t *p_exists, uint8_t *p_is_dir) {
    struct stat st;

    #if (1U == PARAM_CHECK_EN)
    if ((NULL == path) || (NULL == p_exists)) {
        APP_ERR("Null pointer in SysCallPathExists!");
        return APP_ERROR_NULL;
    }
    #endif /* (1U == PARAM_CHECK_EN) */

    *p_exists = (0 == stat(path, &st)) ? 1U : 0U;
    if (NULL != p_is_dir) {
        *p_is_dir = ((0U != *p_exists) && (0 != S_ISDIR(st.st_mode))) ? 1U : 0U;
    }

    /* Return success after stat check */
    return APP_SUCCESS;
}

/* Create a folder (single level) if it does not already exist
 * @param path[in] Folder path
 * @return APP_SUCCESS if created or already existing, APP_ERROR otherwise
 */
Err_t SysCallMakeDir(const_str_t path) {
    #if (1U == PARAM_CHECK_EN)
    if (NULL == path) {
        APP_ERR("path is NULL!");
        return APP_ERROR_NULL;
    }
    #endif /* (1U == PARAM_CHECK_EN) */

    if ((0 != mkdir(path, 0755)) && (EEXIST != errno)) {
        APP_ERR("mkdir failed: %s", path);
        return APP_ERROR;
    }

    /* Return success after folder is available */
    return APP_SUCCESS;
}

/* Read a whole text file into a buffer
 * @param path[in] File path
 * @param p_out[out] Destination buffer (always null-terminated, truncated if too small)
 * @param out_size[in] Size of p_out in bytes
 * @param p_read_len[out] Number of bytes read (may be NULL)
 * @return APP_SUCCESS on success, APP_ERROR if the file cannot be opened
 */
Err_t SysCallReadFile(const_str_t path, char *p_out, uint32_t out_size, uint32_t *p_read_len) {
    FILE  *p_file;
    size_t n;

    #if (1U == PARAM_CHECK_EN)
    if ((NULL == path) || (NULL == p_out)) {
        APP_ERR("Null pointer in SysCallReadFile!");
        return APP_ERROR_NULL;
    }
    if (0U == out_size) {
        APP_ERR("out_size is zero!");
        return APP_ERROR_INVALID_PARAM;
    }
    #endif /* (1U == PARAM_CHECK_EN) */

    p_out[0] = '\0';
    p_file   = fopen(path, "r");
    if (NULL == p_file) {
        APP_ERR("Cannot open file: %s", path);
        return APP_ERROR;
    }

    n        = fread(p_out, 1U, (size_t)(out_size - 1U), p_file);
    p_out[n] = '\0';
    (void)fclose(p_file);

    if (NULL != p_read_len) {
        *p_read_len = (uint32_t)n;
    }

    /* Return success after reading file */
    return APP_SUCCESS;
}

/* Write (overwrite) a text file atomically via a temporary file + rename
 * @param path[in] Destination file path
 * @param content[in] Null-terminated content to write
 * @return APP_SUCCESS on success, APP_ERROR on I/O failure
 * @note The original file is kept intact if writing the temporary file fails
 */
Err_t SysCallWriteFile(const_str_t path, const_str_t content) {
    char   tmp_path[SYSCALL_PATH_LEN + 8U];
    FILE  *p_file;
    size_t len;

    #if (1U == PARAM_CHECK_EN)
    if ((NULL == path) || (NULL == content)) {
        APP_ERR("Null pointer in SysCallWriteFile!");
        return APP_ERROR_NULL;
    }
    if (strlen(path) >= SYSCALL_PATH_LEN) {
        APP_ERR("Path too long: %s", path);
        return APP_ERROR_INVALID_PARAM;
    }
    #endif /* (1U == PARAM_CHECK_EN) */

    (void)snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", path);
    p_file = fopen(tmp_path, "w");
    if (NULL == p_file) {
        APP_ERR("Cannot open file for writing: %s", tmp_path);
        return APP_ERROR;
    }

    len = strlen(content);
    if ((len != fwrite(content, 1U, len, p_file)) || (0 != fclose(p_file))) {
        APP_ERR("Failed to write file: %s", tmp_path);
        (void)remove(tmp_path);
        return APP_ERROR;
    }

    if (0 != rename(tmp_path, path)) {
        APP_ERR("Failed to replace file: %s", path);
        (void)remove(tmp_path);
        return APP_ERROR;
    }

    /* Return success after atomic replace */
    return APP_SUCCESS;
}

/* Copy a file (binary-safe) from src_path to dst_path
 * @param src_path[in] Source file path
 * @param dst_path[in] Destination file path (overwritten)
 * @return APP_SUCCESS on success, APP_ERROR on I/O failure
 */
Err_t SysCallCopyFile(const_str_t src_path, const_str_t dst_path) {
    FILE  *p_src;
    FILE  *p_dst;
    char   buf[1024];
    size_t n;
    Err_t  err = APP_SUCCESS;

    #if (1U == PARAM_CHECK_EN)
    if ((NULL == src_path) || (NULL == dst_path)) {
        APP_ERR("Null pointer in SysCallCopyFile!");
        return APP_ERROR_NULL;
    }
    #endif /* (1U == PARAM_CHECK_EN) */

    p_src = fopen(src_path, "rb");
    if (NULL == p_src) {
        APP_ERR("Cannot open source file: %s", src_path);
        return APP_ERROR;
    }
    p_dst = fopen(dst_path, "wb");
    if (NULL == p_dst) {
        APP_ERR("Cannot open destination file: %s", dst_path);
        (void)fclose(p_src);
        return APP_ERROR;
    }

    /* Copy blocks until EOF or a short write occurs */
    while (0U != (n = fread(buf, 1U, sizeof(buf), p_src))) {
        if (n != fwrite(buf, 1U, n, p_dst)) {
            err = APP_ERROR;
            /* Stop copying on write failure */
            break;
        }
    }

    (void)fclose(p_src);
    if (0 != fclose(p_dst)) {
        err = APP_ERROR;
    }
    if (APP_SUCCESS != err) {
        APP_ERR("Failed to copy %s -> %s", src_path, dst_path);
    }

    /* Return copy status */
    return err;
}

/* reserved ***************************************************************************************
 **************************************************************************************************/

