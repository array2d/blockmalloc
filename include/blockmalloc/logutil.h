#ifndef LOGUTIL_H
#define LOGUTIL_H

// 不再直接依赖 <stdio.h>；用户可在裸机实现 PLATFORM_LOG(fmt, ...)
#ifdef ENABLE_LOG
    #ifdef PLATFORM_LOG
        #define LOG(fmt, ...) PLATFORM_LOG("[blockmalloc] " fmt, ##__VA_ARGS__)
    #else
        // 默认不做任何输出；在裸机平台实现 PLATFORM_LOG 来启用输出（例如通过 UART）
        #define LOG(fmt, ...) ((void)0)
    #endif
#else
    #define LOG(fmt, ...) ((void)0)
#endif

#endif // LOGUTIL_H