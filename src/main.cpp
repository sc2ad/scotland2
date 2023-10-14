#include "_config.h"
#ifndef LINUX_TEST

#include <dlfcn.h>
#include <fmt/format.h>
#include <jni.h>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <string>
#include "internal-loader.hpp"
#include "log.h"
#include "modloader.h"
#include "protect.hpp"

#include "trampoline-allocator.hpp"
#include "trampoline.hpp"
#include "util.hpp"

namespace {
std::string application_id;
std::filesystem::path modloader_path;
std::filesystem::path modloader_root_load_path;
std::filesystem::path modloader_source_path;
std::filesystem::path files_dir;
std::filesystem::path external_dir;
std::filesystem::path libil2cppPath;
bool failed = false;

using namespace std::literals::string_view_literals;
constexpr std::string_view libil2cppName = "libil2cpp.so"sv;

void install_load_hook(uint32_t* target) {
  // Size of the allocation size for the trampoline in bytes
  constexpr static auto trampolineSize = 64;
  // Size of the function we are hooking in instructions
  constexpr static auto hookSize = 8;
  // Size of the allocation page to mark as +rwx
  constexpr static auto kPageSize = 4096ULL;
  // Mostly throw-away reference
  size_t trampoline_size = trampolineSize;
  FLAMINGO_DEBUG("Hello from flamingo!");
  static auto trampoline = flamingo::TrampolineAllocator::Allocate(trampolineSize);
  // We write fixups for the first 4 instructions in the target
  trampoline.WriteHookFixups(target);
  // Then write the jumpback at instruction 5 to continue the code
  trampoline.WriteCallback(&target[4]);
  trampoline.Finish();
  // Ensure target is writable
  auto* page_aligned_target = reinterpret_cast<uint32_t*>(reinterpret_cast<uint64_t>(target) & ~(kPageSize - 1));
  FLAMINGO_DEBUG("Marking target: {} as writable, page aligned: {}", fmt::ptr(target), fmt::ptr(page_aligned_target));
  if (::mprotect(page_aligned_target, kPageSize, PROT_READ | PROT_WRITE | PROT_EXEC) != 0) {
    // Log error on mprotect!
    FLAMINGO_ABORT("Failed to mark: {} (page aligned: {}) as +rwx. err: {}", fmt::ptr(target),
                   fmt::ptr(page_aligned_target), std::strerror(errno));
  }
  static flamingo::Trampoline target_hook(target, hookSize, trampoline_size);
  auto init_hook = [](char const* domain_name) noexcept {
    // Call orig first
    LOG_DEBUG("il2cpp_init called with: {}", domain_name);
    reinterpret_cast<void (*)(char const*)>(trampoline.address.data())(domain_name);
    modloader::load_mods();
  };
  target_hook.WriteCallback(reinterpret_cast<uint32_t*>(+init_hook));
  target_hook.Finish();
  FLAMINGO_DEBUG("Hook installed! Target: {} (il2cpp_init) now will call: {} (hook), with trampoline: {}",
                 fmt::ptr(target), fmt::ptr(+init_hook), fmt::ptr(trampoline.address.data()));
}

}  // namespace
namespace modloader {

MODLOADER_EXPORT std::filesystem::path const& get_modloader_path() noexcept {
  return modloader_path;
}

MODLOADER_EXPORT std::filesystem::path const& get_modloader_root_load_path() noexcept {
  return modloader_root_load_path;
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

MODLOADER_EXPORT std::filesystem::path const& get_modloader_source_path() noexcept {
  return modloader_source_path;
}

}  // namespace modloader

MODLOADER_FUNC bool modloader_get_failed() {
  return failed;
}

MODLOADER_FUNC void modloader_preload(JNIEnv* env, char const* appId, char const* modloaderPath,
                                      char const* modloaderSource, char const* filesDir,
                                      char const* externalDir) noexcept {
  LOG_DEBUG("Hello from " MOD_ID "! {} {} {} {} {} {}", fmt::ptr(env), appId, modloaderPath, modloaderSource, filesDir,
            externalDir);
  // Here, we should copy all of these strings of interest and cache the env and VM.
  application_id = appId;
  modloader_path = modloaderPath;
  modloader_source_path = modloaderSource;
  files_dir = filesDir;
  modloader_root_load_path = std::filesystem::path(modloaderSource).parent_path();
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
  libil2cppPath = libil2cppPath / libil2cppName;
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
        "Could not dlopen libil2cpp.so: {}: {}! Not calling load on early mods or installing unity hooks for late "
        "mods!",
        libil2cppPath.c_str(), dlerror());
    return;
  }
  void* il2cpp_init = dlsym(modloader_libil2cpp_handle, "il2cpp_init");
  LOG_DEBUG("Found il2cpp_init at: {}", fmt::ptr(il2cpp_init));
  modloader_unity_handle = unityHandle;
  // Hook il2cpp_init to redirect to us after loading, and load normal mods
  // TODO: We KNOW it has enough space for us, but we could technically double check this too.
  install_load_hook(reinterpret_cast<uint32_t*>(il2cpp_init));
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