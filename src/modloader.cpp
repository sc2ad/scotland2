#ifndef LINUX_TEST
#include "modloader.h"
#include <jni.h>
#include <filesystem>
#include "_config.h"
#include "internal-loader.hpp"
#include "loader.hpp"
#include "log.h"

MODLOADER_EXPORT JavaVM* modloader_jvm;
MODLOADER_EXPORT void* modloader_libil2cpp_handle;
MODLOADER_EXPORT void* modloader_unity_handle;

namespace {

// Private set for libs
std::vector<modloader::LoadResult> loaded_libs;
// Private set for early mods
std::vector<modloader::LoadResult> loaded_early_mods;
// Private set for mods
std::vector<modloader::LoadResult> loaded_mods;
// Private set to avoid dlopening redundantly
std::unordered_set<std::string> skip_load{};

}  // namespace

namespace modloader {

bool copy_all(std::filesystem::path const& filesDir) noexcept {
  auto const base_path = get_modloader_path().parent_path();
  std::error_code error_code;
  for (auto const& [phase, path] : loadPhaseMap.arr) {
    auto dst = filesDir / path;
    auto src = base_path / path;
    std::filesystem::copy(src, dst,
                          std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing,
                          error_code);
    if (error_code) {
      LOG_ERROR("Failed during phase: {} to copy directory: {} to: {}: {}", phase, src.c_str(), dst.c_str(),
                error_code.message().c_str());
      return false;
    }
    std::filesystem::permissions(dst, std::filesystem::perms::all, error_code);
    if (error_code) {
      LOG_ERROR("Failed during phase: {} to set permissions on copied directory: {}: {}", phase, dst.c_str(),
                error_code.message().c_str());
      return false;
    }
  }
  return true;
}

void open_libs(std::filesystem::path const& filesDir) noexcept {
  // Not thread safe: mutates skip_load
  LOG_DEBUG("Opening libs using root: {}", filesDir.c_str());
  auto lib_sos = listAllObjectsInPhase(filesDir, LoadPhase::Libs);
  LOG_DEBUG("Found: {} candidates! Attempting to load them...", lib_sos.size());
  // TODO: Libs are stored as LoadedMod which is redundant
  loaded_libs = loadMods(lib_sos, filesDir, skip_load, LoadPhase::Libs);
  // Report errors
  for (auto& l : loaded_libs) {
    if (auto* fail = std::get_if<FailedMod>(&l)) {
      LOG_WARN("Skipping lib load of library: {} because it failed with: {}", fail->object.path.c_str(),
               fail->failure.c_str());
    }
  }
}

void open_early_mods(std::filesystem::path const& filesDir) noexcept {
  // Construct early mods
  // Not thread safe: mutates skip_load, initializes in sequential order
  auto early_mod_sos = listAllObjectsInPhase(filesDir, LoadPhase::EarlyMods);
  loaded_early_mods = loadMods(early_mod_sos, filesDir, skip_load, LoadPhase::EarlyMods);
  // Call initialize and report errors
  for (auto& m : loaded_early_mods) {
    if (auto* loaded_mod = std::get_if<LoadedMod>(&m)) {
      // Call init, note that it is a mutable call
      if (!loaded_mod->init()) {
        // Setup call does not exist, but the mod was still loaded
        LOG_INFO("No setup on mod: {}", loaded_mod->object.path.c_str());
      }
    } else if (auto* fail = std::get_if<FailedMod>(&m)) {
      LOG_WARN("Skipping setup call on: {} because it failed: {}", fail->object.path.c_str(), fail->failure.c_str());
    }
  }
}

void load_mods() {
  // Construct late mods/libs
  // Call setup() on these newly opened things
  // Also call load() on everything
}

void close_all() noexcept {
  constexpr auto try_close = [](LoadResult& r) {
    if (auto* loaded = std::get_if<LoadedMod>(&r)) {
      if (auto err = loaded->close()) {
        LOG_WARN("Failed to close mod: {}: {}", loaded->object.path.c_str(), err->c_str());
      }
    }
  };
  // Moving out of these collections is fine, because this is teardown
  std::for_each(loaded_mods.begin(), loaded_mods.end(), try_close);
  std::for_each(loaded_early_mods.begin(), loaded_early_mods.end(), try_close);
  std::for_each(loaded_libs.begin(), loaded_libs.end(), try_close);
}
}  // namespace modloader

#endif