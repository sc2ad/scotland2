#ifndef LINUX_TEST
#include "modloader.h"
#include <jni.h>
#include "_config.h"

MODLOADER_EXPORT JavaVM* modloader_jvm;
MODLOADER_EXPORT void* modloader_libil2cpp_handle;
MODLOADER_EXPORT void* modloader_unity_handle;

namespace modloader {

void construct_mods() {
  // Construct early mods/libs
  // Call setup() on these
}

void load_mods() {
  // Construct late mods/libs
  // Call setup() on these newly opened things
  // Also call load() on everything
}
}  // namespace modloader

#endif