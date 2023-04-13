#pragma once

#ifndef MOD_ID
#define MOD_ID "sl2"
#endif

#ifndef MOD_VERSION
#define MOD_VERSION "0.1.0"
#endif

#ifdef ANDROID
#include <android/log.h>

#define LOGA(lvl, ...) __android_log_print(lvl, MOD_ID "|v" MOD_VERSION, __VA_ARGS__)

#define LOG_VERBOSE(...) LOGA(ANDROID_LOG_VERBOSE, __VA_ARGS__)
#define LOG_DEBUG(...) LOGA(ANDROID_LOG_DEBUG, __VA_ARGS__)
#define LOG_INFO(...) LOGA(ANDROID_LOG_INFO, __VA_ARGS__)
#define LOG_WARN(...) LOGA(ANDROID_LOG_WARN, __VA_ARGS__)
#define LOG_ERROR(...) LOGA(ANDROID_LOG_ERROR, __VA_ARGS__)
#define LOG_FATAL(...) LOGA(ANDROID_LOG_FATAL, __VA_ARGS__)
#else

#include <cstdio>

#define LOGA(lvl, ...)                              \
  printf("%s: " MOD_ID "|v" MOD_VERSION ": ", lvl); \
  printf(__VA_ARGS__)

#define LOG_VERBOSE(...) LOGA("VERBOSE", __VA_ARGS__)
#define LOG_DEBUG(...) LOGA("DEBUG", __VA_ARGS__)
#define LOG_INFO(...) LOGA("INFO", __VA_ARGS__)
#define LOG_WARN(...) LOGA("WARN", __VA_ARGS__)
#define LOG_ERROR(...) LOGA("ERROR", __VA_ARGS__)
#define LOG_FATAL(...) LOGA("FATAL", __VA_ARGS__)

#endif
