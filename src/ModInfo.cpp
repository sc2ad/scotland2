#include "ModInfo.h"

#define ModInfo_setter(identifier) const char* ModInfo_get_##identifier(ModInfo* modInfo) { return modInfo->identifier.c_str(); }
#define ModInfo_getter(identifier) void ModInfo_set_##identifier(ModInfo* modInfo, const char* identifier) { modInfo->identifier = identifier; }

ModInfo_setter(id);
ModInfo_setter(version);
ModInfo_setter(author);
ModInfo_setter(description);
void ModInfo_set_user(ModInfo* modInfo, void* user) { modInfo->user = user; }

ModInfo_getter(id);
ModInfo_getter(version);
ModInfo_getter(author);
ModInfo_getter(description);
void* ModInfo_set_user(ModInfo* modInfo) { return modInfo->user; }
