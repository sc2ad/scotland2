#ifndef LINUX_TEST

#include <dlfcn.h>
#include <jni.h>
#include <filesystem>
#include <string>
#include "internal-loader.hpp"
#include "log.h"
#include "modloader.h"
#include "protect.hpp"

namespace {
std::string application_id;
std::filesystem::path modloader_path;
std::filesystem::path files_dir;
std::filesystem::path external_dir;
std::filesystem::path libil2cppPath;
bool failed = false;

using namespace std::literals::string_view_literals;
constexpr std::string_view libil2cppName = "libil2cpp.so"sv;
}  // namespace
namespace modloader {

MODLOADER_EXPORT std::filesystem::path const& get_modloader_path() noexcept {
  return modloader_path;
}

MODLOADER_EXPORT std::filesystem::path const& get_files_dir() noexcept {
  return files_dir;
}

MODLOADER_EXPORT std::filesystem::path const& get_external_dir() noexcept {
  return external_dir;
}

MODLOADER_EXPORT std::string const& get_application_id() noexcept {
  return application_id;
}

}  // namespace modloader

MODLOADER_FUNC bool modloader_get_failed() {
  return failed;
}

MODLOADER_FUNC void modloader_preload(JNIEnv* env, char const* appId, char const* modloaderPath, char const* filesDir,
                                      char const* externalDir) noexcept {
  // Here, we should copy all of these strings of interest and cache the env and VM.
  application_id = appId;
  modloader_path = modloaderPath;
  files_dir = filesDir;
  external_dir = externalDir;
  if (env->GetJavaVM(&modloader_jvm) != 0) {
    LOG_WARN("Failed to get JavaVM! Be careful when using it!");
  }
  if (!modloader::copy_all(files_dir)) {
    LOG_FATAL("Failed to copy over files! Modloading cannot continue!");
    failed = true;
  }
}

MODLOADER_FUNC void modloader_load([[maybe_unused]] JNIEnv* env, char const* soDir) noexcept {
  // Copy over soDir
  libil2cppPath = soDir;
  libil2cppPath = libil2cppPath.parent_path() / libil2cppName;
  if (failed) {
    LOG_FATAL("Not loading mods because we failed!");
    return;
  }
  // dlopen all libs and dlopen early mods, call setup
  modloader::open_libs(files_dir);
  modloader::open_early_mods(files_dir);
}

MODLOADER_FUNC void modloader_accept_unity_handle([[maybe_unused]] JNIEnv* env, void* unityHandle) noexcept {
  // Call init on early mods, install il2cpp_init, unity hook installed after il2cpp_init
  // il2cpp_init hook to call load on early mods
  if (failed) {
    LOG_FATAL("Not loading mods because we failed!");
    return;
  }
  modloader_libil2cpp_handle = dlopen(libil2cppPath.c_str(), RTLD_LOCAL | RTLD_LAZY);
  // On startup, we also want to protect everything, and ensure we have read/write
  modloader::protect_all();
  if (modloader_libil2cpp_handle == nullptr) {
    LOG_ERROR(
        "Could not dlopen libil2cpp.so: %s: %s! Not calling load on early mods or installing unity hooks for late "
        "mods!",
        libil2cppPath.c_str(), dlerror());
    return;
  }
  void* il2cpp_init = dlsym(modloader_libil2cpp_handle, "il2cpp_init");
  modloader_unity_handle = unityHandle;
  // TODO: Add hooks here
  (void)il2cpp_init;
}

MODLOADER_FUNC void modloader_unload([[maybe_unused]] JavaVM* vm) noexcept {
  if (failed) {
    LOG_FATAL("Not unloading mods because we failed!");
    return;
  }
  // dlclose all opened mods, uninstall all hooks
  modloader::close_all();
}

#endif