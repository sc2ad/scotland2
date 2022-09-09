// A C++ file to offer C the methods for C bindings

// This is so bad and yet so awesome

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunknown-pragmas"
#pragma ide diagnostic ignored "cppcoreguidelines-pro-bounds-pointer-arithmetic"
#pragma ide diagnostic ignored "cppcoreguidelines-owning-memory"
#pragma ide diagnostic ignored "cppcoreguidelines-pro-type-reinterpret-cast"
#pragma ide diagnostic ignored "cppcoreguidelines-no-malloc"

#include <cstring>
#include <variant>

#include "c_api.h"
#include "loader.hpp"

#define cast(type, ptr) (reinterpret_cast<type*>(ptr)) // NOLINT(bugprone-macro-parentheses)

template <typename C, typename U>
requires(!std::is_pointer_v<C> && !std::is_pointer_v<U>)
C* mallocCopy(U const& og) {
    auto* n = static_cast<C*>(malloc(sizeof(U)));
    auto casted_n = reinterpret_cast<U*>(n);
    std::copy(&og, &og + sizeof(U), casted_n); // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)

    return n;
}

const char* mallocStringCopy(std::string_view const og) {
    char* n = static_cast<char*>(malloc(og.size() + 1));
    strcpy(n, og.data());
    n[og.size() + 1] = '\0';

    return n;
}

modloader_DependencyResults convert_dependency_results_c(std::span<modloader::DependencyResult> dependencies) {
    auto* c_dependencies = static_cast<modloader_DependencyResult*>(malloc(dependencies.size() * sizeof(modloader_DependencyResult)));
    std::span<modloader_DependencyResult> c_dependencies_span = {c_dependencies, dependencies.size()};

    for (size_t i = 0; i < dependencies.size(); i++) {
        auto const& dep = dependencies[i];
        if (const auto *successDep = get_if<modloader::Dependency>(&dep)) {
            c_dependencies_span[i] = modloader_DependencyResult{
                .type = modloader_DepedencyResultEnum::MODLOADER_DEPENDENCY,
                .d = {
                    .dependency = mallocCopy<modloader_Dependency>(*successDep)
                }
            };

        } else if (const auto *errorDep = get_if<modloader::MissingDependency>(&dep)) {
            c_dependencies_span[i] = modloader_DependencyResult{
                .type = modloader_DepedencyResultEnum::MODLOADER_MISSING_DEPENDENCY,
                .d = {
                    .missingDependency = mallocStringCopy(*errorDep)
                }
            };
        }
    }

    return {
        .items = c_dependencies,
        .len = dependencies.size()
    };
}

#pragma region sharedobject
char const* modloader_sharedobject_get_path(modloader_SharedObject* self) {
    auto *so = cast(modloader::SharedObject, self);
    std::string const& path = so->path.native();

    return mallocStringCopy(path);
}

modloader_DependencyResults modloader_sharedobject_get_to_load(modloader_SharedObject* self, char const* path, modloader_LoadPhase::e loadPhase) {
    auto *so = cast(modloader::SharedObject, self);
    auto dependencies = so->getToLoad(path, static_cast<modloader::LoadPhase>(loadPhase));

    return convert_dependency_results_c(dependencies);
}
#pragma endregion

#pragma region dependency
modloader_SharedObject* modloader_dependency_get_object(modloader_Dependency* self) {
    auto * dep = cast(modloader::Dependency, self);
    return mallocCopy<modloader_SharedObject>(dep->object);
}
modloader_DependencyResults modloader_dependency_get_dependencies(modloader_Dependency* self) {
    auto * dep = cast(modloader::Dependency, self);
    return convert_dependency_results_c(dep->dependencies);
}
#pragma endregion

#pragma region ModInfo
char const* modloader_modinfo_get_version(modloader_ModInfo* self) {
    auto * modInfo = cast(modloader::ModInfo, self);

    return mallocStringCopy(modInfo->version);
}
MODLOADER_C_SIZE_TYPE modloader_modinfo_get_versionLong(modloader_ModInfo* self) {
    auto * modInfo = cast(modloader::ModInfo, self);

    return modInfo->versionLong;
}
char const* modloader_modinfo_get_name(modloader_ModInfo* self) {
    auto * modInfo = cast(modloader::ModInfo, self);

    return mallocStringCopy(modInfo->name);
}
#pragma endregion

modloader_SharedObject* modloader_failed_mod_get_object(modloader_FailedMod* self) {
    auto * failedMod = cast(modloader::FailedMod, self);

    return mallocCopy<modloader_SharedObject>(failedMod->object);
}
char const* modloader_failed_mod_get_failure(modloader_FailedMod* self) {
    auto * failedMod = cast(modloader::FailedMod, self);

    return mallocStringCopy(failedMod->failure);
}
modloader_DependencyResults modloader_failed_mod_get_dependencies(modloader_FailedMod* self) {
    auto * failedMod = cast(modloader::FailedMod, self);

    return convert_dependency_results_c(failedMod->dependencies);
}
modloader_SharedObject* modloader_loaded_mod_get_object(modloader_LoadedMod* self) {
    auto * loadedMod = cast(modloader::LoadedMod, self);

    return mallocCopy<modloader_SharedObject>(loadedMod->object);
}
modloader_ModInfo* modloader_loaded_mod_get_modinfo(modloader_LoadedMod* self) {
    auto * loadedMod = cast(modloader::LoadedMod, self);

    return mallocCopy<modloader_ModInfo>(loadedMod->modInfo);
}
modloader_SetupFunc* modloader_loaded_mod_get_setupFn(modloader_LoadedMod* self) {
    auto * loadedMod = cast(modloader::LoadedMod, self);

    return reinterpret_cast<modloader_SetupFunc*>(loadedMod->setupFn.value_or(nullptr));
}
modloader_LoadFunc* modloader_loaded_mod_get_loadFn(modloader_LoadedMod* self) {
    auto * loadedMod = cast(modloader::LoadedMod, self);

    return reinterpret_cast<modloader_LoadFunc*>(loadedMod->loadFn.value_or(nullptr));
}
void const* modloader_loaded_mod_get_handle(modloader_LoadedMod* self) {
    auto * loadedMod = cast(modloader::LoadedMod, self);

    return loadedMod->handle;
}

#pragma clang diagnostic pop