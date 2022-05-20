#pragma once

#include "_config.h"
#include <jni.h>
#include <string_view>

namespace modloader {
    // preload_t is called in JNIOnLoad
    using preload_t = void() noexcept;
    // first, main is called. the interface it returns is given to LibUnity, but only after calling accept_unity_handle.
    // if you redirect GetJavaVM, it will be redirected to keep the patched chain alive.
    using main_t = JNINativeInterface*(JavaVM* vm, JNIEnv* env, std::string_view loadSrc, JNINativeInterface* passthroughIface) noexcept;
    // this is called *before* calling LibUnity's JNI_OnLoad.
    using accept_unity_handle_t = void(void* unityModuleHandle) noexcept;
}

#define MODLOADER_CHECK_FUNCTION(name) static_assert(::std::is_same_v<::std::remove_reference_t<decltype(modloader_##name)>, ::modloader::name##_t>, \
        "modloader_" #name " has the wrong signature!")

#define MODLOADER_CHECK MODLOADER_CHECK_FUNCTION(main); MODLOADER_CHECK_FUNCTION(preload); MODLOADER_CHECK_FUNCTION(accept_unity_handle);