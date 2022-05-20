#include <filesystem>
#include "modloader-calls.hpp"
#include "modloader.hpp"

MODLOADER_FUNC void modloader_preload() noexcept {
}

MODLOADER_FUNC JNINativeInterface* modloader_main(JavaVM* v, JNIEnv* env, std::string_view loadSrc, JNINativeInterface* passthroughIface) noexcept {
    logpf(ANDROID_LOG_VERBOSE, "modloader_main called with vm: 0x%p, env: 0x%p, loadSrc: %s", v, env, loadSrc.data());

    jobject activity = getActivityFromUnityPlayer(env);
    if (activity) ensurePerms(env, activity);
    std::filesystem::path fpath(loadSrc);
    Modloader::set_modloader_path(fpath.parent_path());

    // Create libil2cpp path string. Should be in the same path as loadSrc (since libmodloader.so needs to be in the same path)
    char *dirPath = dirname(loadSrc.data());
    if (dirPath == NULL) {
        logpf(ANDROID_LOG_FATAL, "loadSrc cannot be converted to a valid directory!");
        return passthroughIface;
    }
    // TODO: Check if path exists before setting it and assuming it is valid
    ModloaderInfo info;
    info.name = "MainModloader";
    info.tag = "main-modloader";
    Modloader::setInfo(info);
    Modloader::modloaderPath = dirPath;
    Modloader::construct_mods();

    return passthroughIface;
}

MODLOADER_FUNC void modloader_accept_unity_handle(void* uhandle) noexcept {
    logpf(ANDROID_LOG_VERBOSE, "modloader_accept_unity_handle called with uhandle: 0x%p", uhandle);

    init_all_mods();
}

MODLOADER_CHECK