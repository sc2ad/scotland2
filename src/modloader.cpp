#ifndef LINUX_TEST
#include "modloader.h"
#include <jni.h>
#include <sys/stat.h>
#include <filesystem>
#include <system_error>
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

// Get status type as string
char const* status_type(std::filesystem::file_type const type) {
  switch (type) {
    case std::filesystem::file_type::none:
      return "none";
    case std::filesystem::file_type::not_found:
      return "not found";
    case std::filesystem::file_type::regular:
      return "regular";
    case std::filesystem::file_type::directory:
      return "directory";
    case std::filesystem::file_type::symlink:
      return "symlink";
    case std::filesystem::file_type::block:
      return "block";
    case std::filesystem::file_type::character:
      return "character";
    case std::filesystem::file_type::fifo:
      return "fifo";
    case std::filesystem::file_type::socket:
      return "socket";
    case std::filesystem::file_type::unknown:
    default:
      return "unknown";
  }
}

#if defined(__aarch64__) && !defined(NO_STAT_DUMPS)
void statdump(std::filesystem::path const& path) {
  struct stat64 buffer {};
  if (stat64(path.c_str(), &buffer) != 0) {
    LOG_DEBUG("stat64 of file: {} failed: {}, {}", path.c_str(), errno, strerror(errno));
  } else {
    LOG_DEBUG(
        "File: {}, dev: {}, ino: {}, mode: {}, nlink: {}, uid: {}, gid: {}, rdid: {}, sz: {}, atime: {}, "
        "mtime: {}, ctime: {}",
        path.c_str(), buffer.st_dev, buffer.st_ino, buffer.st_mode, buffer.st_nlink, buffer.st_uid, buffer.st_gid,
        buffer.st_rdev, buffer.st_size, buffer.st_atime, buffer.st_mtime, buffer.st_ctime);
    std::error_code err{};
    auto status = std::filesystem::status(path, err);
    if (err) {
      LOG_ERROR("Failed to get status of path: {}", path.c_str());
      return;
    }
    LOG_DEBUG("File: {}, type: {}, perms: 0x{:x}", path.c_str(), status_type(status.type()),
              static_cast<uint32_t>(status.permissions()));
  }
}
#else
void statdump([[maybe_unused]] fs::path const& path) {}
#endif

void ensure_dir_exists(std::filesystem::path const& dir) {
  // First, statdump the dir
  statdump(dir);
  // Next, attempt to create the directory
  std::error_code err{};
  if (!std::filesystem::create_directories(dir, err)) {
    LOG_WARN("Failed to make directory: {}, err: {}", dir.c_str(), err.message());
    return;
  }
  LOG_DEBUG("Trying to chmod {}", dir.c_str());
  auto flags = S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP | S_IXGRP | S_IWGRP | S_IROTH | S_IXOTH;
  if (chmod(dir.c_str(), flags) < 0) {
    LOG_ERROR("Failed to chmod {} with flags: 0x{:x}! errno: {}, {}", dir.c_str(), flags, errno, std::strerror(errno));
  }
}

bool remove_dir(std::filesystem::path const& dir) {
  // First, statdump
  statdump(dir);
  // Next, try to recursively remove it
  std::error_code err{};
  std::filesystem::remove_all(dir, err);
  if (err) {
    LOG_ERROR("Failed to remove directory: {}", dir.c_str());
    return false;
  }
  return true;
}

}  // namespace

namespace modloader {

bool copy_all(std::filesystem::path const& filesDir) noexcept {
  auto const& base_path = get_modloader_root_load_path();
  std::error_code error_code;
  for (auto const& [phase, path] : loadPhaseMap.arr) {
    auto dst = filesDir / path;
    auto src = base_path / path;
    ensure_dir_exists(src);
    if (!remove_dir(dst)) {
      LOG_ERROR("Failed to remove dst directory, stopping loading process early to avoid grabbing old mods...");
      return false;
    }
    ensure_dir_exists(dst);
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

void load_mods() noexcept {
  // Call load on all early mods
  for (auto& m : loaded_early_mods) {
    if (auto* loaded_mod = std::get_if<LoadedMod>(&m)) {
      if (!loaded_mod->load()) {
        // Load call does not exist, but the mod was still loaded
        LOG_INFO("No load function on mod: {}", loaded_mod->object.path.c_str());
      }
    } else if (auto* fail = std::get_if<FailedMod>(&m)) {
      LOG_WARN("Skipping load call on: {} because it failed to be constructed: {}", fail->object.path.c_str(),
               fail->failure.c_str());
    }
  }
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