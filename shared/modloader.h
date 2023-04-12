#pragma once

#include <jni.h>
#include "_config.h"

#ifdef __cplusplus
#include <filesystem>
#include <string>

namespace modloader {

MODLOADER_EXPORT std::filesystem::path const& get_modloader_path() noexcept;
MODLOADER_EXPORT std::filesystem::path const& get_files_dir() noexcept;
MODLOADER_EXPORT std::filesystem::path const& get_external_dir() noexcept;
MODLOADER_EXPORT std::string const& get_application_id() noexcept;

}  // namespace modloader

#endif

MODLOADER_EXPORT extern JavaVM* modloader_jvm;
MODLOADER_EXPORT extern void* modloader_libil2cpp_handle;
MODLOADER_EXPORT extern void* modloader_unity_handle;
