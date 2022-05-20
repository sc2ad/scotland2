#pragma once

#define MODLOADER_FUNC extern "C" __attribute__((visibility("default")))
#define MODLOADER_EXPORT __attribute__((visibility("default")))
