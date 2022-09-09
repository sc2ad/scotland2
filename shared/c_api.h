#pragma once

// allocated with malloc()
// free with free()
#define MODLOADER_C_STRING const char*
#define MODLOADER_C_SIZE_TYPE unsigned long long
#define MODLOADER_C_API_FUNC(ret, n, ...) extern "C" __attribute__((visibility("default"))) ret modloader_##n(__VA_ARGS__)
#define MODLOADER_C_STRUCT(n) extern "C" struct __attribute__((visibility("default"))) modloader_##n
#define MODLOADER_C_UNION(n) extern "C" union __attribute__((visibility("default"))) modloader_##n
#define MODLOADER_C_ENUM(n, ...) struct modloader_##n { enum e { __VA_ARGS__ }; }
#define MODLOADER_C_ENUM_VALUE(n) MODLOADER_##n
#define MODLOADER_C_TYPEDEF(n, og) typedef og modloader_##n
#define MODLOADER_C_TYPEDEF_FN(n, ret, ...) typedef ret modloader_##n(__VA_ARGS__)

#define MODLOADER_C_CONTAINER(name, type) \
MODLOADER_C_STRUCT(name) { \
    type* items; \
    MODLOADER_C_SIZE_TYPE len; \
}

/// TYPE DECLARATIONS
MODLOADER_C_STRUCT(SharedObject);
MODLOADER_C_STRUCT(Dependency);
MODLOADER_C_TYPEDEF(MissingDependency, const char*);


MODLOADER_C_STRUCT(ModInfo);

MODLOADER_C_STRUCT(FailedMod);
MODLOADER_C_STRUCT(LoadedMod);

MODLOADER_C_TYPEDEF_FN(SetupFunc, void, modloader_ModInfo* modInfo);
MODLOADER_C_TYPEDEF_FN(LoadFunc, void);

/// TYPE DEFINITIONS

/// START DEPENDENCY_RESULT
MODLOADER_C_UNION(DepedencyResultUnion) {
    modloader_Dependency* dependency;
    modloader_MissingDependency missingDependency;
};

MODLOADER_C_ENUM(DepedencyResultEnum,
    MODLOADER_C_ENUM_VALUE(DEPENDENCY),
    MODLOADER_C_ENUM_VALUE(MISSING_DEPENDENCY)
);

MODLOADER_C_ENUM(LoadPhase,
    MODLOADER_C_ENUM_VALUE(Mods = 0),
    MODLOADER_C_ENUM_VALUE(EarlyMods = 1),
    MODLOADER_C_ENUM_VALUE(Libs = 2)
);

MODLOADER_C_STRUCT(DependencyResult) {
    modloader_DepedencyResultEnum::e type;
    modloader_DepedencyResultUnion d;
};

MODLOADER_C_CONTAINER(DependencyResults, modloader_DependencyResult);
/// END DEPENDENCY_RESULT

/// FUNCTION DECLARATIONS

/// START SHARED_OBJECT
MODLOADER_C_API_FUNC(MODLOADER_C_STRING, sharedobject_get_path, modloader_SharedObject* self);
MODLOADER_C_API_FUNC(modloader_DependencyResults, sharedobject_get_to_load, modloader_SharedObject* self, MODLOADER_C_STRING path, modloader_LoadPhase::e loadPhase);
/// END SHARED_OBJECT

/// START DEPENDENCY
MODLOADER_C_API_FUNC(modloader_SharedObject*, dependency_get_object, modloader_Dependency* self);
MODLOADER_C_API_FUNC(modloader_DependencyResults, dependency_get_dependencies, modloader_Dependency* self);
/// END DEPENDENCY

/// START MOD_INFO
MODLOADER_C_API_FUNC(MODLOADER_C_STRING, modinfo_get_version, modloader_ModInfo* self);
MODLOADER_C_API_FUNC(MODLOADER_C_SIZE_TYPE, modinfo_get_versionLong, modloader_ModInfo* self);
MODLOADER_C_API_FUNC(MODLOADER_C_STRING, modinfo_get_name, modloader_ModInfo* self);
/// END MOD_INFO

/// START FAILED_MOD
MODLOADER_C_API_FUNC(modloader_SharedObject*, failed_mod_get_object, modloader_FailedMod* self);
MODLOADER_C_API_FUNC(MODLOADER_C_STRING, failed_mod_get_failure, modloader_FailedMod* self);
MODLOADER_C_API_FUNC(modloader_DependencyResults, failed_mod_get_dependencies, modloader_FailedMod* self);
/// END FAILED_MOD

/// START LOADED_MOD
MODLOADER_C_API_FUNC(modloader_SharedObject*, loaded_mod_get_object, modloader_LoadedMod* self);
MODLOADER_C_API_FUNC(modloader_ModInfo*, loaded_mod_get_modinfo, modloader_LoadedMod* self);
// nullable return
MODLOADER_C_API_FUNC(modloader_SetupFunc*, loaded_mod_get_setupFn, modloader_LoadedMod* self);
// nullable return
MODLOADER_C_API_FUNC(modloader_LoadFunc*, loaded_mod_get_loadFn, modloader_LoadedMod* self);
MODLOADER_C_API_FUNC(void const*, loaded_mod_get_handle, modloader_LoadedMod* self);

/// END LOADED_MOD

// TODO: Topological sort?