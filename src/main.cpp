#ifndef LINUX_TEST

#include <filesystem>
#include "modloader-calls.hpp"
#include "modloader.hpp"

MODLOADER_FUNC void modloader_preload() noexcept {
}

MODLOADER_FUNC JNINativeInterface* modloader_main(JavaVM* v, JNIEnv* env, std::string_view loadSrc, JNINativeInterface* passthroughIface) noexcept {
    // logpf(ANDROID_LOG_VERBOSE, "modloader_main called with vm: 0x%p, env: 0x%p, loadSrc: %s", v, env, loadSrc.data());

    // jobject activity = getActivityFromUnityPlayer(env);
    // if (activity) ensurePerms(env, activity);
    std::filesystem::path fpath(loadSrc);
    Modloader::set_modloader_path(fpath.parent_path());
    Modloader::construct_mods();

    return passthroughIface;
}

MODLOADER_FUNC void modloader_accept_unity_handle(void* uhandle) noexcept {

}

MODLOADER_CHECK

#endif