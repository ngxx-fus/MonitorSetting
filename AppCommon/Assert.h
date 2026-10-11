/* file-desc **************************************************************************************
 * @filename    Assert.h
 * @brief       Application assertion macros that log failure and trap into DefaultErrorHandler.
 * @note        Provides ASSERT, ASSERT_EQUAL, ASSERT_NOT_EQUAL, ASSERT_LESS_THAN, ASSERT_GREATER_THAN.
 **************************************************************************************************/

#ifndef __APP_COMMON_ASSERT_H__
#define __APP_COMMON_ASSERT_H__

/* headers ****************************************************************************************
 **************************************************************************************************/

#include "./__config__.h"
#include "./Log.h"
#include "./Error.h"
#include <stdbool.h>

/* macros *****************************************************************************************
 **************************************************************************************************/

#ifndef CONF_ASSERT_EN
    #ifdef APP_ASSERT_EN
        #define CONF_ASSERT_EN APP_ASSERT_EN
    #else
        #define CONF_ASSERT_EN (1U)
    #endif
#endif /* CONF_ASSERT_EN */

/* Evaluate boolean expression; if false, log assertion failure and trap into DefaultErrorHandler
 * @param expression[in] Boolean condition expected to be true
 */
#if (1U == CONF_ASSERT_EN)
    #define ASSERT(expression) \
        do { \
            if (!(bool)(expression)) { \
                SystemLog(__FILE__ ":" LOG_STR(__LINE__) ":ASSERT:", \
                          "Assertion failed: (%s)", #expression); \
                DefaultErrorHandler(); \
            } \
        } while (0)
#else
    #define ASSERT(expression) \
        do { \
            (void)(expression); \
        } while (0)
#endif /* (1U == CONF_ASSERT_EN) */

/* Assert that EXP equals VAR ((EXP) == (VAR))
 * @param EXP[in] Expected or left-hand expression
 * @param VAR[in] Actual variable or right-hand expression
 */
#define ASSERT_EQUAL(EXP, VAR) \
    ASSERT((EXP) == (VAR))

/* Assert that EXP does not equal VAR ((EXP) != (VAR))
 * @param EXP[in] Expected or left-hand expression
 * @param VAR[in] Actual variable or right-hand expression
 */
#define ASSERT_NOT_EQUAL(EXP, VAR) \
    ASSERT((EXP) != (VAR))

/* Assert that EXP is strictly less than VAR ((EXP) < (VAR))
 * @param EXP[in] Left-hand expression
 * @param VAR[in] Right-hand variable/expression
 */
#define ASSERT_LESS_THAN(EXP, VAR) \
    ASSERT((EXP) < (VAR))

/* Assert that EXP is strictly greater than VAR ((EXP) > (VAR))
 * @param EXP[in] Left-hand expression
 * @param VAR[in] Right-hand variable/expression
 */
#define ASSERT_GREATER_THAN(EXP, VAR) \
    ASSERT((EXP) > (VAR))

/* definitions ************************************************************************************
 **************************************************************************************************/

/* exported variables *****************************************************************************
 **************************************************************************************************/

/* exported functions *****************************************************************************
 **************************************************************************************************/

/* reserved ***************************************************************************************
 **************************************************************************************************/

#endif /* __APP_COMMON_ASSERT_H__ */
