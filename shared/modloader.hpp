#pragma once

#include "_config.h"
#include <jni.h>
#include <string>
#include <filesystem>

#ifdef __cplusplus

struct Modloader {
    friend struct ModloaderWrapper;
    static JavaVM* jvm;
    static JNIEnv* env;

    static std::string const& get_modloader_path();
    static void construct_mods();
    static void load_mods();
    static inline void set_modloader_path(std::filesystem::path const& p) {
        modloader_path = p;
    }
    private:
    static std::filesystem::path modloader_path;
};

#endif

MODLOADER_FUNC const char* modloader_get_modloader_path();
