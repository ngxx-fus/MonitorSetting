#ifndef __APP_COMMON_LOG_H__
#define __APP_COMMON_LOG_H__

#include "stdio.h"

#define SYS_LOG(__fmt__, ...)   SystemLog(__FILE__ ":" __LINE__, __fmt__, ##__VA_ARGS__)

#endif /* __APP_COMMON_LOG_H__ */