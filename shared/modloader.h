#pragma once

// Exposed API functions
#include <jni.h>
#include <stdint.h>
#include "_config.h"

#ifdef __cplusplus
#include <filesystem>
#include <string>

namespace modloader {

MODLOADER_EXPORT std::filesystem::path const& get_modloader_path() noexcept;
MODLOADER_EXPORT std::filesystem::path const& get_modloader_root_load_path() noexcept;
MODLOADER_EXPORT std::filesystem::path const& get_files_dir() noexcept;
MODLOADER_EXPORT std::filesystem::path const& get_external_dir() noexcept;
MODLOADER_EXPORT std::string const& get_application_id() noexcept;
MODLOADER_EXPORT std::filesystem::path const& get_modloader_source_path() noexcept;

}  // namespace modloader
extern "C" {
#endif
typedef struct {
  char const* id;
  char const* version;
  uint64_t version_long;
} CModInfo;

typedef enum {
  MatchType_Strict,
  MatchType_IdOnly,
  MatchType_IdVersion,
  MatchType_IdVersionLong,
} CMatchType;

#ifdef __cplusplus
}
#endif

MODLOADER_FUNC bool modloader_get_failed();
MODLOADER_EXPORT extern JavaVM* modloader_jvm;
MODLOADER_EXPORT extern void* modloader_libil2cpp_handle;
MODLOADER_EXPORT extern void* modloader_unity_handle;
MODLOADER_FUNC char const* modloader_get_path();
MODLOADER_FUNC char const* modloader_get_root_load_path();
MODLOADER_FUNC char const* modloader_get_files_dir();
MODLOADER_FUNC char const* modloader_get_external_dir();
MODLOADER_FUNC char const* modloader_get_application_id();
MODLOADER_FUNC char const* modloader_get_source_path();
/// @brief Triggers an unload of the specified mod, which will in turn call the unload() method of it.
/// It will also be removed from any collections. It is UB if the mod to be unloaded is the currently executing mod.
/// @return False if the mod failed to be unloaded in any way, true if it either did not exist or was successfully
/// unloaded.
MODLOADER_FUNC bool modloader_force_unload(CModInfo info, CMatchType match_type);
// TODO: Add requireMod
// TODO: More docs on existing
// TODO: Improve version_long to be more descriptive, potentially 3 or more fields?
// TODO: Add way of fetching all loaded libs, early_mods, mods
// - CAPI will need more effort here, we need to copy over
// TODO: Add void** param to setup call, store as userdata in a given mod structure
// TODO: Add a find mod call akin to force_unload
