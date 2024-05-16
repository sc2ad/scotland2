#include <algorithm>
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
#include "elf-utils.hpp"
#include "runtime-restriction.hpp"
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

void print_decode_loop(uint32_t* val, int n) {
  auto handle = flamingo::getHandle();
  for (int i = 0; i < n; i++) {
    cs_insn* insns = nullptr;
    auto count = cs_disasm(handle, reinterpret_cast<uint8_t const*>(val), sizeof(uint32_t),
                           static_cast<uint64_t>(reinterpret_cast<uint64_t>(val)), 1, &insns);
    if (count == 1) {
      FLAMINGO_DEBUG("Addr: {} Value: 0x{:x}, {} {}", fmt::ptr(val), *val, insns->mnemonic, insns->op_str);
    } else {
      FLAMINGO_DEBUG("Addr: {} Value: 0x{:x}", fmt::ptr(val), *val);
    }
    val++;
  }
}

/// should flush instruction cache
#define __flush_cache(c, n) __builtin___clear_cache(reinterpret_cast<char*>(c), reinterpret_cast<char*>(c) + n)

/// @brief undoes a hook at target with the original instructions from trampoline
void undo_hook(flamingo::Trampoline const& trampoline, uint32_t* target) {
  constexpr static auto kPageSize = 4096ULL;
  auto* page_aligned_target = reinterpret_cast<uint32_t*>(reinterpret_cast<uint64_t>(target) & ~(kPageSize - 1));

  FLAMINGO_DEBUG("Marking target: {} as writable, page aligned: {}", fmt::ptr(target), fmt::ptr(page_aligned_target));
  if (::mprotect(page_aligned_target, kPageSize, PROT_READ | PROT_WRITE | PROT_EXEC) != 0) {
    // Log error on mprotect!
    FLAMINGO_ABORT("Failed to mark: {} (page aligned: {}) as +rwx. err: {}", fmt::ptr(target),
                   fmt::ptr(page_aligned_target), std::strerror(errno));
  }
  FLAMINGO_DEBUG("Undoing hook of {} with {} original instructions", fmt::ptr(target),
                 trampoline.original_instructions.size());
  std::copy_n(trampoline.original_instructions.begin(), trampoline.original_instructions.size(), target);

  FLAMINGO_DEBUG("Target decoded after uninstall: {}", fmt::ptr(target));
  print_decode_loop(target, 5);
  __flush_cache(target, sizeof(uint32_t) * 4);
}

/// @brief attempts to setup the unity late load hook
void setup_unity_hook();

void install_load_hook(uint32_t* target) {
  FLAMINGO_DEBUG("Installing hook at: {}, initial dump:", fmt::ptr(target));
  print_decode_loop(target, 5);
  // Size of the trampoline allocation we desire in number of instructions
  constexpr static auto trampolineInstCount = 16;
  // Size of the allocation page to mark as +rwx
  constexpr static auto kPageSize = 4096ULL;
  // Trampoline size in bytes as a reference. Support for only one hook
  size_t trampoline_size = trampolineInstCount * sizeof(uint32_t);
  FLAMINGO_DEBUG("Hello from flamingo!");
  static auto trampoline = flamingo::TrampolineAllocator::Allocate(trampoline_size);
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
  static flamingo::Trampoline target_hook(target, trampolineInstCount, trampoline_size);
  static auto target_hook_point = target;
  auto init_hook = [](char const* domain_name) noexcept -> int {
    // Call orig first
    LOG_DEBUG("il2cpp_init called with: {}", domain_name);
    auto ret = reinterpret_cast<int (*)(char const*)>(trampoline.address.data())(domain_name);

    undo_hook(trampoline, target_hook_point);
    modloader::load_early_mods();

    // we do the unity hook after il2cpp init so the icalls are registered
    setup_unity_hook();
    return ret;
  };
  // TODO: mprotect memory again after we are done writing
  target_hook.WriteCallback(reinterpret_cast<uint32_t*>(+init_hook));
  target_hook.Finish();
  __flush_cache(target, sizeof(uint32_t) * 4);

  FLAMINGO_DEBUG("Hook installed! Target: {} (il2cpp_init) now will call: {} (hook), with trampoline: {}",
                 fmt::ptr(target), fmt::ptr(+init_hook), fmt::ptr(trampoline.address.data()));
  FLAMINGO_DEBUG("Target decoded: {}", fmt::ptr(target));
  print_decode_loop(target, 5);
  FLAMINGO_DEBUG("Trampoline decoded: {}", fmt::ptr(trampoline.address.data()));
  print_decode_loop(trampoline.address.data(), 16);
}

void install_unity_hook(uint32_t* target) {
  // Size of the trampoline allocation we desire in number of instructions
  constexpr static auto trampolineInstCount = 16;
  // Size of the allocation page to mark as +rwx
  constexpr static auto kPageSize = 4096ULL;
  // Trampoline size in bytes as a reference. Support for only one hook
  size_t trampoline_size = trampolineInstCount * sizeof(uint32_t);
  static auto trampoline_target = target;
  static auto trampoline = flamingo::TrampolineAllocator::Allocate(trampoline_size);
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
  static flamingo::Trampoline target_hook(target, trampolineInstCount, trampoline_size);
  // we hook ClearRoots which is a method called at the start of StartFirstScene.
  // if we did anything with gameobjects before this, it would clear them here, so we load our late mods *after*
  // that way, mods can create GameObjects and other unity objects at dlopen time if they want to.
  auto unity_hook = [](void* param_1, bool param_2) noexcept {
    // First param is a unity object, second param is some kind of bool

    LOG_DEBUG("DestroyImmediate called with params {} {}", fmt::ptr(param_1), param_2);

    // call orig
    reinterpret_cast<void (*)(void*, bool)>(trampoline.address.data())(param_1, param_2);
    undo_hook(trampoline, trampoline_target);

    // open mods and call load / late_load on things that require it
    LOG_DEBUG("Opening mods");
    modloader::open_mods(modloader::get_files_dir());
    LOG_DEBUG("Loading mods");
    modloader::load_mods();
  };
  // STOPGAP: Always write ldr + br + data because fixups are done incorrectly when we have a near branch
  target_hook.WriteLdrBrData(reinterpret_cast<uint32_t*>(+unity_hook));
  target_hook.Finish();
  __flush_cache(target, sizeof(uint32_t) * 4);

  // TODO: mprotect memory again after we are done writing
  FLAMINGO_DEBUG("Hook installed! Target: {} (DestroyImmediate) now will call: {} (hook), with trampoline: {}",
                 fmt::ptr(target), fmt::ptr(+unity_hook), fmt::ptr(trampoline.address.data()));
  FLAMINGO_DEBUG("Target decoded: {}", fmt::ptr(target));
  print_decode_loop(target, 5);
  FLAMINGO_DEBUG("Trampoline decoded: {}", fmt::ptr(trampoline.address.data()));
  print_decode_loop(trampoline.address.data(), 16);
}

#define RET_NULL_LOG_UNLESS(v)       \
  if (!v) {                          \
    LOG_ERROR("Could not find " #v); \
    return nullptr;                  \
  }

#define LOG_OFFSET(name, val) LOG_DEBUG(name " found @ offset: {}", fmt::ptr((void*)((uintptr_t)val - unity_base)))

/// @brief attempts to find the ClearRoots method within libunity.so
uint32_t* find_unity_hook_loc([[maybe_unused]] void* unity_handle, void* il2cpp_handle) noexcept {
  // xref trace
  // dlsym il2cpp_resolve_icall
  // resolve icall for UnityEngine.Object::DestroyImmediate
  // 2nd bl to Scripting::DestroyObjectFromScriptingImmediate
  // 1st b to DestroyObjectHighLevel

  // for logging purposes
  auto unity_base = elf_utils::baseAddr((modloader::get_libil2cpp_path().parent_path() / "libunity.so").c_str());
  auto resolve_icall = reinterpret_cast<void* (*)(char const*)>(dlsym(il2cpp_handle, "il2cpp_resolve_icall"));
  if (!resolve_icall) {
    LOG_ERROR("Could not dlsym 'resolve_icall': {}", dlerror());
    return nullptr;
  }

  auto destroy_immediate = static_cast<uint32_t*>(resolve_icall("UnityEngine.Object::DestroyImmediate"));
  RET_NULL_LOG_UNLESS(destroy_immediate);
  LOG_OFFSET("UnityEngine.Object::DestroyImmediate", destroy_immediate);

  // find first ret to find end of method
  auto end = destroy_immediate + 100;
  static constexpr auto RET_OP = 0xd65f03c0;
  uint32_t* destroy_immediate_ret = nullptr;
  for (auto addr = destroy_immediate; addr != end; addr++) {
    if (*addr == RET_OP) {
      destroy_immediate_ret = addr;
      break;
    }
  }
  RET_NULL_LOG_UNLESS(destroy_immediate_ret);
  LOG_OFFSET("destroy_immediate_ret", destroy_immediate_ret);

  auto rend = destroy_immediate;
  auto rbegin = destroy_immediate_ret;

  static constexpr int32_t BL_OP = 0b100101;
  static constexpr int32_t BL_MASK = 0b111111;
  struct BL {
    int32_t offset : 26;
    int32_t op : 6;
  };

  // then walk backwards from there to find the last bl in the method
  uint32_t* scripting_destroy_immediate = nullptr;
  for (auto addr = rbegin; addr != rend; addr--) {
    auto bl = reinterpret_cast<BL*>(addr);

    if ((bl->op & BL_MASK) == BL_OP) {  // it was a bl
      // bl->offset should be multiplied by 4 but because addr is a uint32_t* it does that for us
      scripting_destroy_immediate = addr + bl->offset;
      break;
    }
  }
  RET_NULL_LOG_UNLESS(scripting_destroy_immediate);
  LOG_OFFSET("Scripting::DestroyObjectFromScriptingImmediate", scripting_destroy_immediate);

  auto destroy_object_high_level = cs::findNthB<1, false>(scripting_destroy_immediate);
  RET_NULL_LOG_UNLESS(destroy_object_high_level);
  LOG_OFFSET("DestroyObjectHighLevel", *destroy_object_high_level);
  return *destroy_object_high_level;
}

#undef LOG_OFFSET
#undef RET_NULL_LOG_UNLESS

/// @brief attempts to setup the late load hook for unity
void setup_unity_hook() {
  // Hook DestroyObjectHighLevel (in libunity) so we can load in mods (aka late mods)
  // This method is first ran within the load of the first scene, so we are before anything in the scene activated
  // TODO: should have enough space, but we can double check this too like for il2cpp init

  auto unity_hook_loc = find_unity_hook_loc(modloader_unity_handle, modloader_libil2cpp_handle);
  if (unity_hook_loc == nullptr) {
    LOG_ERROR(
        "Could not find unity hook location, "
        "late_load will not be called for early mods, and mods will not be opened!");
  } else {
    LOG_DEBUG("Found DestroyImmediate @ {}", fmt::ptr(unity_hook_loc));
    install_unity_hook(static_cast<uint32_t*>(unity_hook_loc));
  }
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
  if (runtime_restriction::init(std::filesystem::path(modloaderSource).filename().string())) {
    std::vector<std::string> ld_paths = { "/vendor/lib64", "/system/lib64", "/system/product/lib64" };
    for (auto const& entry : std::filesystem::recursive_directory_iterator(files_dir)) {
      if (entry.is_directory()) {
        ld_paths.push_back(entry.path());
      }
    }
    if (runtime_restriction::add_ld_library_paths(std::move(ld_paths))) {
      LOG_DEBUG("Added ld_library_paths!");
    } else {
      LOG_WARN("Failed to add ld_library_paths!");
    }
  } else {
    LOG_WARN("Failed to add ld_library_paths!");
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
}

MODLOADER_FUNC void modloader_unload([[maybe_unused]] JavaVM* vm) noexcept {
  if (failed) {
    LOG_FATAL("Not unloading mods because we failed!");
    return;
  }
  // dlclose all opened mods, uninstall all hooks
  modloader::close_all();
}

MODLOADER_FUNC bool modloader_add_ld_library_path(char const* path) {
  return runtime_restriction::add_ld_library_paths({ path });
}
#endif
