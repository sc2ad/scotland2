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

#include "capstone-utils.hpp"
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

/// should flush instruction cache
#define __flush_cache(c, n) __builtin___clear_cache(reinterpret_cast<char*>(c), reinterpret_cast<char*>(c) + n)

/// @brief undoes a hook at target with the original instructions from trampoline
void undo_hook(flamingo::Trampoline const& trampoline, uint32_t* target) {
  constexpr static auto trampolineSize = 64;
  constexpr static auto hookSize = 8;
  constexpr static auto kPageSize = 4096ULL;
  size_t trampoline_size = trampolineSize;
  auto* page_aligned_target = reinterpret_cast<uint32_t*>(reinterpret_cast<uint64_t>(target) & ~(kPageSize - 1));

  FLAMINGO_DEBUG("Marking target: {} as writable, page aligned: {}", fmt::ptr(target), fmt::ptr(page_aligned_target));
  if (::mprotect(page_aligned_target, kPageSize, PROT_READ | PROT_WRITE | PROT_EXEC) != 0) {
    // Log error on mprotect!
    FLAMINGO_ABORT("Failed to mark: {} (page aligned: {}) as +rwx. err: {}", fmt::ptr(target),
                   fmt::ptr(page_aligned_target), std::strerror(errno));
  }

  // local target hook to make writing and mprotecting things easier
  flamingo::Trampoline target_hook(target, hookSize, trampoline_size);
  FLAMINGO_DEBUG("Undoing hook of {} with {} original instructions", fmt::ptr(target),
                 trampoline.original_instructions.size());

  for (auto ins : trampoline.original_instructions) {
    target_hook.Write(ins);
  }
  target_hook.Finish();
  __flush_cache(target, sizeof(uint32_t) * 4);
}

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
  static auto trampoline_target = target;
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
    // TODO: uninstall hook after first call
    // Call orig first
    LOG_DEBUG("il2cpp_init called with: {}", domain_name);
    reinterpret_cast<void (*)(char const*)>(trampoline.address.data())(domain_name);

    undo_hook(trampoline, trampoline_target);
    modloader::load_early_mods();
  };
  // TODO: mprotect memory again after we are done writing
  target_hook.WriteCallback(reinterpret_cast<uint32_t*>(+init_hook));
  target_hook.Finish();
  __flush_cache(target, sizeof(uint32_t) * 4);

  FLAMINGO_DEBUG("Hook installed! Target: {} (il2cpp_init) now will call: {} (hook), with trampoline: {}",
                 fmt::ptr(target), fmt::ptr(+init_hook), fmt::ptr(trampoline.address.data()));
}

// credits to https://github.com/ikoz/AndroidSubstrate_hookingC_examples/blob/master/nativeHook3/jni/nativeHook3.cy.cpp
uintptr_t baseAddr(char const* soname, void* imagehandle) {
  if (soname == NULL) return (uintptr_t)NULL;
  if (imagehandle == NULL) return (uintptr_t)NULL;

  FILE* f = NULL;
  char line[200] = { 0 };
  char* state = NULL;
  char* tok = NULL;
  char* baseAddr = NULL;
  if ((f = fopen("/proc/self/maps", "r")) == NULL) return (uintptr_t)NULL;
  while (fgets(line, 199, f) != NULL) {
    tok = strtok_r(line, "-", &state);
    baseAddr = tok;
    strtok_r(NULL, "\t ", &state);
    strtok_r(NULL, "\t ", &state);        // "r-xp" field
    strtok_r(NULL, "\t ", &state);        // "0000000" field
    strtok_r(NULL, "\t ", &state);        // "01:02" field
    strtok_r(NULL, "\t ", &state);        // "133224" field
    tok = strtok_r(NULL, "\t ", &state);  // path field

    if (tok != NULL) {
      int i;
      for (i = (int)strlen(tok) - 1; i >= 0; --i) {
        if (!(tok[i] == ' ' || tok[i] == '\r' || tok[i] == '\n' || tok[i] == '\t')) break;
        tok[i] = 0;
      }
      {
        size_t toklen = strlen(tok);
        size_t solen = strlen(soname);
        if (toklen > 0) {
          if (toklen >= solen && strcmp(tok + (toklen - solen), soname) == 0) {
            fclose(f);
            return (uintptr_t)strtoll(baseAddr, NULL, 16);
          }
        }
      }
    }
  }
  fclose(f);
  return (uintptr_t)NULL;
}

#define RET_NULL_LOG_UNLESS(v)       \
  if (!v) {                          \
    LOG_ERROR("Could not find " #v); \
    return nullptr;                  \
  }

#define LOG_OFFSET(name, val) LOG_DEBUG(name " found @ offset: {}", fmt::ptr((void*)((uintptr_t)val - unity_base)))

/// @brief attempts to find the ClearRoots method within libunity.so
uint32_t* find_unity_hook_loc([[maybe_unused]] JNIEnv* env, [[maybe_unused]] void* unityHandle) noexcept {
  // xref trace
  // dlsym JNI_OnLoad
  // 2nd bl to UnityPlayer::RegisterNatives
  // 2nd pcAddr for the method array -> find nativeRender JNINativeMethod*
  // 6th bl to UnityPlayerLoop
  // 2nd tbz to pendingWindow check target
  // 1st tbz to levelLoaded check target
  // 1st b.ne to initialized check target
  // 1st tbz to splashScreen check target
  // 1st bl to LoadFirstScene
  // 9th bl to StartFirstScene
  // 2nd bl to ClearRoots

  // for logging purposes
  auto unity_base = baseAddr((modloader::get_libil2cpp_path().parent_path() / "libunity.so").c_str(), unityHandle);

  // exported symbol of libunity.so
  auto jni_onload = static_cast<uint32_t*>(dlsym(unityHandle, "JNI_OnLoad"));
  RET_NULL_LOG_UNLESS(jni_onload);
  LOG_OFFSET("JNI_OnLoad", jni_onload);

  // registerNatives is something used when interacting with jni from a native binary
  auto registerNatives = cs::findNthBl<2>(jni_onload);
  RET_NULL_LOG_UNLESS(registerNatives);
  LOG_OFFSET("UnityPlayer::RegisterNatives", *registerNatives);

  // an array of native methods registered for the unityplayer
  auto s_UnityPlayerMethods = cs::getpcaddr<2, 1>(*registerNatives);
  RET_NULL_LOG_UNLESS(s_UnityPlayerMethods);
  LOG_OFFSET("s_UnityPlayerMethods", std::get<2>(*s_UnityPlayerMethods));

  // method array ptr
  JNINativeMethod* meths = reinterpret_cast<JNINativeMethod*>(std::get<2>(*s_UnityPlayerMethods));

  // get the size of the method array
  auto movSz = cs::getMovzValue<1>(*registerNatives);
  RET_NULL_LOG_UNLESS(movSz);
  auto methsz = std::get<1>(*movSz);

  // 0x19 was the size of this array in the unstripped libunity we inject into beat saber.
  // this is not guaranteed to be the size across every libunty ever,
  // therefore it not matching is not cause for erroring out of the xref trace
  if (methsz != 0x19) {
    LOG_WARN("Method size found was {} (0x{:x}), while 25 (0x19) was expected", methsz, methsz);
    LOG_DEBUG("Despite wrong method size, lookup will still be attempted, this could crash!");
  } else {
    LOG_DEBUG("Found native method array size {} (0x{:x})", methsz, methsz);
  }

  // walk the native array until we find nativeRender or until we run out of methods
  JNINativeMethod* nativeRenderMethod = nullptr;
  auto mend = meths + methsz;
  for (auto method = meths; method != mend; method++) {
    if (strcmp(method->name, "nativeRender") == 0) {
      nativeRenderMethod = method;
      break;
    }
  }

  RET_NULL_LOG_UNLESS(nativeRenderMethod);
  // get the method pointer for nativeRender
  auto nativeRender = static_cast<uint32_t*>(nativeRenderMethod->fnPtr);
  LOG_OFFSET("nativeRender", nativeRender);

  // main player loop
  auto unityplayerloop = cs::findNthBl<6>(nativeRender);
  RET_NULL_LOG_UNLESS(unityplayerloop);
  LOG_OFFSET("UnityPlayerLoop", *unityplayerloop);

  // we want to find LoadFirstScene, we can do this either by finding the 20th or so bl, but this breaks
  // across different unity versions. A different strategy is to follow a few jumps across labels, and end up at the
  // right label instead to get the first bl there to get to LoadFirstScene.

  auto pendingWindow = cs::getTbzAddr<2>(*unityplayerloop);
  RET_NULL_LOG_UNLESS(pendingWindow);
  LOG_OFFSET("pendingWindow check", std::get<2>(*pendingWindow));

  auto levelLoaded = cs::getTbzAddr<1>(std::get<2>(*pendingWindow));
  RET_NULL_LOG_UNLESS(levelLoaded);
  LOG_OFFSET("levelLoaded check", std::get<2>(*levelLoaded));

  auto initialized = cs::getBCondAddr<1, ARM64_CC_NE>(std::get<2>(*levelLoaded));
  RET_NULL_LOG_UNLESS(initialized);
  LOG_OFFSET("initialized check", std::get<2>(*initialized));

  auto splashScreen = cs::getTbzAddr<1>(std::get<2>(*initialized));
  RET_NULL_LOG_UNLESS(splashScreen);
  LOG_OFFSET("splashScreen check", std::get<2>(*splashScreen));

  auto loadfirstscene = cs::findNthBl<1>(std::get<2>(*splashScreen));
  RET_NULL_LOG_UNLESS(loadfirstscene);
  LOG_OFFSET("LoadFirstScene", *loadfirstscene);

  auto startFirstScene = cs::findNthBl<9>(*loadfirstscene);
  RET_NULL_LOG_UNLESS(startFirstScene);
  LOG_OFFSET("StartFirstScene", *startFirstScene);

  auto clearRoots = cs::findNthBl<2>(*startFirstScene);
  RET_NULL_LOG_UNLESS(clearRoots);
  LOG_OFFSET("ClearRoots", *clearRoots);

  return *clearRoots;
}

#undef LOG_OFFSET
#undef RET_NULL_LOG_UNLESS

void install_unity_hook(uint32_t* target) {
  // Size of the allocation size for the trampoline in bytes
  constexpr static auto trampolineSize = 64;
  // Size of the function we are hooking in instructions
  constexpr static auto hookSize = 33;
  // Size of the allocation page to mark as +rwx
  constexpr static auto kPageSize = 4096ULL;
  // Mostly throw-away reference
  size_t trampoline_size = trampolineSize;
  static auto trampoline_target = target;
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
  // we hook ClearRoots which is a method called at the start of StartFirstScene.
  // if we did anything with gameobjects before this, it would clear them here, so we load our late mods *after*
  // that way, mods can create GameObjects and other unity objects at dlopen time if they want to.
  auto unity_hook = [](void* param_1) noexcept {
    // TODO: uninstall hook after first call
    // First param is assumed to be a linked list, stored on the SceneManager.
    // this linked list is iterated over by ClearRoots and all unity objects are destroyed.
    LOG_DEBUG("ClearRoots called with param {}", fmt::ptr(param_1));

    // call orig
    reinterpret_cast<void (*)(void*)>(trampoline.address.data())(param_1);
    undo_hook(trampoline, trampoline_target);

    // open mods and call load / late_load on things that require it
    LOG_DEBUG("Opening mods");
    modloader::open_mods(modloader::get_files_dir());
    LOG_DEBUG("Loading mods");
    modloader::load_mods();
  };
  target_hook.WriteCallback(reinterpret_cast<uint32_t*>(+unity_hook));
  target_hook.Finish();
  __flush_cache(target, sizeof(uint32_t) * 4);

  // TODO: mprotect memory again after we are done writing
  FLAMINGO_DEBUG("Hook installed! Target: {} (ClearRoots) now will call: {} (hook), with trampoline: {}",
                 fmt::ptr(target), fmt::ptr(+unity_hook), fmt::ptr(trampoline.address.data()));
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

MODLOADER_EXPORT std::filesystem::path const& get_libil2cpp_path() noexcept {
  return libil2cppPath;
}

}  // namespace modloader

// Exposed C API
MODLOADER_FUNC bool modloader_get_failed() {
  return failed;
}
MODLOADER_FUNC char const* modloader_get_path() {
  return modloader_path.c_str();
}
MODLOADER_FUNC char const* modloader_get_root_load_path() {
  return modloader_root_load_path.c_str();
}
MODLOADER_FUNC char const* modloader_get_files_dir() {
  return files_dir.c_str();
}
MODLOADER_FUNC char const* modloader_get_external_dir() {
  return external_dir.c_str();
}
MODLOADER_FUNC char const* modloader_get_application_id() {
  return application_id.c_str();
}
MODLOADER_FUNC char const* modloader_get_source_path() {
  return modloader_source_path.c_str();
}
MODLOADER_FUNC char const* modloader_get_libil2cpp_path() {
  return libil2cppPath.c_str();
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
  modloader_unity_handle = unityHandle;
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
  // Hook il2cpp_init to redirect to us after loading, and load normal mods
  // TODO: We KNOW it has enough space for us, but we could technically double check this too.
  install_load_hook(reinterpret_cast<uint32_t*>(il2cpp_init));

  // Hook ClearRoots (in StartFirstScene) so we can load in mods (aka late mods)
  // this method is ran to clear all unity objects, after which the first scene is loaded
  // TODO: should have enough space, but we can double check this too like for il2cpp init
  auto unity_hook_loc = find_unity_hook_loc(env, unityHandle);
  if (!unity_hook_loc) {
    LOG_ERROR(
        "Could not find unity hook location, "
        "late_load will not be called for early mods, and mods will not be opened!");
  } else {
    LOG_DEBUG("Found ClearRoots @ {}", fmt::ptr(unity_hook_loc));
    install_unity_hook(static_cast<uint32_t*>(unity_hook_loc));
  }
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
