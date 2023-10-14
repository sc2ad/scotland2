#pragma once

#ifndef MOD_ID
#define MOD_ID "scotland2"
#endif

#ifndef MOD_VERSION
#define MOD_VERSION "0.1.0"
#endif

#ifdef ANDROID
#include <android/log.h>
#include <fmt/compile.h>
#include <fmt/core.h>

#define SL2_LOG(lvl, str, ...)                                                     \
  do {                                                                          \
    std::string __ss = fmt::format(FMT_COMPILE(str) __VA_OPT__(, __VA_ARGS__)); \
    __android_log_write(lvl, MOD_ID "|v" MOD_VERSION, __ss.c_str());            \
  } while (0)

#define LOG_VERBOSE(str, ...) SL2_LOG(ANDROID_LOG_VERBOSE, str __VA_OPT__(, __VA_ARGS__))
#define LOG_DEBUG(str, ...) SL2_LOG(ANDROID_LOG_DEBUG, str __VA_OPT__(, __VA_ARGS__))
#define LOG_INFO(str, ...) SL2_LOG(ANDROID_LOG_INFO, str __VA_OPT__(, __VA_ARGS__))
#define LOG_WARN(str, ...) SL2_LOG(ANDROID_LOG_WARN, str __VA_OPT__(, __VA_ARGS__))
#define LOG_ERROR(str, ...) SL2_LOG(ANDROID_LOG_ERROR, str __VA_OPT__(, __VA_ARGS__))
#define LOG_FATAL(str, ...) SL2_LOG(ANDROID_LOG_FATAL, str __VA_OPT__(, __VA_ARGS__))
#else

#include <fmt/compile.h>
#include <fmt/core.h>

#define SL2_LOG(lvl, str, ...) fmt::print(FMT_COMPILE(MOD_ID "|v" MOD_VERSION str ": {}: "), lvl __VA_OPT__(, __VA_ARGS__))

#define LOG_VERBOSE(...) SL2_LOG("VERBOSE", __VA_ARGS__)
#define LOG_DEBUG(...) SL2_LOG("DEBUG", __VA_ARGS__)
#define LOG_INFO(...) SL2_LOG("INFO", __VA_ARGS__)
#define LOG_WARN(...) SL2_LOG("WARN", __VA_ARGS__)
#define LOG_ERROR(...) SL2_LOG("ERROR", __VA_ARGS__)
#define LOG_FATAL(...) SL2_LOG("FATAL", __VA_ARGS__)

#endif
