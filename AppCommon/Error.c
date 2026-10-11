#include "__common__.h"
#include "Error.h"

#define STR_ERR(err)     TO_STRING(STR_##err)

const char * gp_Err2Str [] = {
    STR_ERR(APP_SUCCESS), 
    STR_ERR(APP_ERROR),
    STR_ERR(APP_ERROR_NULL),
    STR_ERR(APP_ERROR_INVALID_PARAM),
    STR_ERR(APP_ERROR_MALOC_FAILED),
    TO_STRING(Unknown error code)
};

const char * Err2Str(Err_t err){
    if(UINT32(err) > UINT32(APP_STAT_NUM) )
    return gp_Err2Str[err];
}