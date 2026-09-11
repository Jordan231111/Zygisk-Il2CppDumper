#pragma once
#if defined(__ANDROID__)
#include <android/log.h>
#define LOG_TAG "Il2CppDumper"
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#else
#include <cstdio>
#define LOGI(...)                                                                                  \
    do {                                                                                           \
        std::fprintf(stderr, __VA_ARGS__);                                                         \
        std::fputc('\n', stderr);                                                                  \
    } while (false)
#define LOGD(...) LOGI(__VA_ARGS__)
#define LOGW(...) LOGI(__VA_ARGS__)
#define LOGE(...) LOGI(__VA_ARGS__)
#endif
