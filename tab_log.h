#ifndef TAB_LOG_H
#define TAB_LOG_H

typedef enum _TabLogLevel {
	TAB_LOG_LEVEL_SYSTEM = 0,
	TAB_LOG_LEVEL_ERROR = 1,
	TAB_LOG_LEVEL_WARNING = 2,
	TAB_LOG_LEVEL_INFO = 3,
	TAB_LOG_LEVEL_DEBUG = 4,
} TabLogLevel;

void		tab_log(TabLogLevel level, const char* fmt, ...);

#define tab_log_debug(...)		tab_log(TAB_LOG_LEVEL_DEBUG, __VA_ARGS__)
#define tab_log_info(...)		tab_log(TAB_LOG_LEVEL_INFO, __VA_ARGS__)
#define tab_log_warn(...)		tab_log(TAB_LOG_LEVEL_WARNING, __VA_ARGS__)
#endif