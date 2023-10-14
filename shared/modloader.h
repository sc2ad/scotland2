#pragma once

#include <jni.h>
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

#endif

MODLOADER_FUNC bool modloader_get_failed();
MODLOADER_EXPORT extern JavaVM* modloader_jvm;
MODLOADER_EXPORT extern void* modloader_libil2cpp_handle;
MODLOADER_EXPORT extern void* modloader_unity_handle;
