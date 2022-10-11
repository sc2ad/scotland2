#include "loader.hpp"
#include "internal-loader.hpp"
#include "utils.hpp"

#include <dlfcn.h>
#include <elf.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <fstream>
#include <optional>
#include <span>
#include <stack>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

// I wrote all of this while being tempted by Stack to use rust
// temptation is very strong

using namespace modloader;

template <typename T>
using StackDoubleFlow = std::stack<T>;

template <typename T>
T& readAtOffset(std::span<uint8_t> f, ptrdiff_t offset) {
    return *reinterpret_cast<T*>(&f[offset]);
}

std::string_view readAtOffset(std::span<uint8_t> f, ptrdiff_t offset) {
    return { reinterpret_cast<char const*>(&f[offset]) };
}

template <typename T>
std::span<T> readManyAtOffset(std::span<uint8_t> f, ptrdiff_t offset, size_t amount, size_t size) {
    T* begin = reinterpret_cast<T*>(f.data() + offset);
    T* end = begin + (amount * size);
    return std::span<T>(begin, end);
}

inline std::unordered_map<LoadPhase, std::string> getLoadPhaseDirectories() {
    return { { LoadPhase::Libs, "libs" }, { LoadPhase::EarlyMods, "early_mods" }, { LoadPhase::Mods, "mods" } };
}

std::optional<std::pair<SharedObject, LoadPhase>> findSharedObject(std::filesystem::path const& dependencyDir, LoadPhase phase, std::filesystem::path const& name) {
    std::unordered_map<LoadPhase, std::string> const pathsMap = getLoadPhaseDirectories();

    StackDoubleFlow<std::string> paths;

    // i = 2
    // mods last
    // early mods before
    // libs first
    for (int i = static_cast<int>(phase); i <= static_cast<int>(LoadPhase::Libs); i++) {
        paths.emplace(pathsMap.at(static_cast<LoadPhase>(i)));
    }

    std::filesystem::path dir = paths.top();
    paths.pop();

    auto openedPhase = static_cast<LoadPhase>(phase);

    auto check = dependencyDir / dir / name;

    while (!std::filesystem::exists(check)) {
        if (paths.empty()) {
            return std::nullopt;
        }

        dir = paths.top();
        check = dependencyDir / dir / name;
        paths.pop();

        openedPhase = static_cast<LoadPhase>(std::max(static_cast<int>(openedPhase) - 1, 0));
    }

    return { { SharedObject(check), openedPhase } };
}

std::vector<modloader::DependencyResult> modloader::SharedObject::getToLoad(std::filesystem::path const& dependencyDir, LoadPhase phase,
                                                                            std::unordered_map<std::string_view, std::vector<DependencyResult>>& loadedDependencies) const {
    auto depIt = loadedDependencies.find(this->path.c_str());

    if (depIt != loadedDependencies.end()) {
        return depIt->second;
    }
    
    int fd = open64(this->path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd == -1) {
        //        MLogger::GetLogger().error("Error reading file at %s: %s", path.c_str(),
        //                                   strerror(errno));
        //        SAFE_ABORT();
        throw std::runtime_error("Unable to open file descriptor");
    }

    struct stat64 st {};
    fstat64(fd, &st);
    size_t size = st.st_size;

    void* mapped = mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);

    if (mapped == MAP_FAILED) {
        throw std::runtime_error("Unable to memory map");
    }

    std::span<uint8_t> f(static_cast<uint8_t*>(mapped), static_cast<uint8_t*>(mapped) + size);

    auto elf = readAtOffset<Elf64_Ehdr>(f, 0);

    std::vector<DependencyResult>& dependencies = loadedDependencies[this->path.c_str()];

    auto getSectionHeader = [&](size_t s) { return *reinterpret_cast<Elf64_Shdr*>((uint8_t*)f.data() + elf.e_shoff + (elf.e_shentsize * s)); };

    for (size_t s = 0; s < elf.e_shnum; s++) {
        auto const& sectionHeader = getSectionHeader(s);
        if (sectionHeader.sh_type != SHT_DYNAMIC) {
            continue;
        }

        // divide by zero
        size_t amount = sectionHeader.sh_entsize > 0 ? sectionHeader.sh_size / sectionHeader.sh_entsize : 0;

        for (size_t d = 0; d < amount; d++) {
            auto const& dyn = *reinterpret_cast<Elf64_Dyn*>((uint8_t*)f.data() + sectionHeader.sh_offset + (sectionHeader.sh_entsize * d));
            if (dyn.d_tag != DT_NEEDED) {
                continue;
            }

            std::string_view name = readAtOffset(f, getSectionHeader(sectionHeader.sh_link).sh_offset + dyn.d_un.d_val);

            if (name.data() == nullptr || name.empty()) {
                continue;
            }

            auto optObj = findSharedObject(dependencyDir, phase, name);

            // TODO: Add to a list of "failed" dependencies to locate
            if (optObj) {
                auto [obj, openedPhase] = *optObj;
                dependencies.emplace_back(std::in_place_type_t<Dependency>{}, obj, obj.getToLoad(dependencyDir, openedPhase));
            } else {
                dependencies.emplace_back(std::in_place_type_t<MissingDependency>{}, copyStrC(name));
            }
        }
    }

    munmap(mapped, size);
    close(fd);

    return dependencies;
}

void sortDependencies(std::span<Dependency> deps) {
    std::stable_sort(deps.begin(), deps.end(), [](Dependency const& a, Dependency const& b) { return a.object.path > b.object.path; });
}

void sortDependencies(std::span<DependencyResult> deps) {
    std::stable_sort(deps.begin(), deps.end(), [](DependencyResult const& a, DependencyResult const& b) {
        std::string_view aPath;
        std::string_view bPath;
        if (holds_alternative<Dependency>(a)) {
            aPath = get<Dependency>(a).object.path.c_str();
        } else {
            aPath = get<MissingDependency>(a);
        }

        if (holds_alternative<Dependency>(b)) {
            bPath = get<Dependency>(b).object.path.c_str();
        } else {
            bPath = get<MissingDependency>(b);
        }

        return aPath > bPath;
    });
}

// https://www.geeksforgeeks.org/cpp-program-for-topological-sorting/
// Use mutable ref to avoid making a new vector that is sorted
// TODO: Should we even bother?
void topologicalSortRecurse(Dependency& main, std::deque<Dependency>& stack, std::unordered_set<std::string_view>& visited) {
    if (visited.contains(main.object.path.c_str())) {
        return;
    }

    visited.emplace(main.object.path.c_str());
    sortDependencies(main.dependencies);

    for (auto& depResult : main.dependencies) {
        auto* dep = get_if<Dependency>(&depResult);
        if (dep == nullptr) {
            continue;
        }
        topologicalSortRecurse(*dep, stack, visited);
    }

    stack.emplace_back(main);
}

std::deque<Dependency> modloader::topologicalSort(std::span<DependencyResult const> const list) {
    std::vector<Dependency> deps;
    deps.reserve(list.size());

    for (auto const& result : list) {
        auto const* dep = get_if<Dependency>(&result);
        if (dep == nullptr) {
            continue;
        }

        deps.emplace_back(*dep);
    }

    return topologicalSort(deps);
}

std::deque<Dependency> modloader::topologicalSort(std::span<Dependency const> const list) {
    std::deque<Dependency> dependencies;
    std::unordered_set<std::string_view> visited;

    std::vector<Dependency> deps(list.begin(), list.end());
    sortDependencies(deps);

    for (Dependency& dep : deps) {
        topologicalSortRecurse(dep, dependencies, visited);
    }

    return dependencies;
}

std::vector<SharedObject> modloader::listModsInPhase(std::filesystem::path const& dependencyDir, LoadPhase phase) {
    if (phase == LoadPhase::Libs) {
        return {};
    }

    auto const loadDirs = getLoadPhaseDirectories();
    std::filesystem::path const& loadDir = loadDirs.at(phase);

    std::vector<SharedObject> objects;

    for (auto const& file : std::filesystem::directory_iterator(dependencyDir / loadDir)) {
        if (file.is_directory()) {
            continue;
        }

        if (file.path().extension() != ".so") {
            continue;
        }
        if (!file.path().filename().string().starts_with("lib")) {
            continue;
        }

        objects.emplace_back(file.path());
    }

    return objects;
}

// handle or failure message
using OpenLibraryResult = std::variant<void*, std::string>;

OpenLibraryResult openLibrary(std::filesystem::path const& path) {
    if (!std::filesystem::exists(path)) {
        throw std::runtime_error("Path does not exist on file system!");
    }
    auto* handle = dlopen(path.c_str(), RTLD_LOCAL | RTLD_NOW);
    //    protect();
    if (handle == nullptr) {
        // Error logging (for if symbols cannot be resolved)
        return dlerror();
    }

    return { handle };
}

template <typename T>
std::optional<T> getFunction(void* handle, std::string_view name) {
    auto ptr = reinterpret_cast<T>(dlsym(handle, name.data()));
    return ptr ? ptr : static_cast<std::optional<T>>(std::nullopt);
}

// This will throw if the mod path is in skipLoad
// TODO: Return LoadResult vector since libraries are also loaded here?
LoadResult modloader::loadMod(SharedObject const& mod, std::filesystem::path const& dependencyDir, std::unordered_set<std::string>& skipLoad, LoadPhase phase) {
    if (skipLoad.contains(mod.path)) {
        // TODO: Log
        throw std::runtime_error("Mod is already in skipLoad!");
    }

    auto handleResult = [&](OpenLibraryResult const& result, SharedObject const& obj, std::vector<DependencyResult> const& dependencies) -> LoadResult {
        if (auto const* error = get_if<std::string>(&result)) {
            return FailedMod(obj, copyStrC(*error), dependencies);
        }

        auto* handle = get<void*>(result);

        // TODO: unsafe
        ModInfo modInfo(nullptr, nullptr);

        auto setupFn = getFunction<SetupFunc>(handle, "setup");
        auto loadFn = getFunction<LoadFunc>(handle, "load");

        // TRY/CATCH HERE?
        if (setupFn) {
            setupFn.value()(modInfo);
        }

        return LoadedMod(modInfo, obj, setupFn, loadFn, handle);
    };

    auto deps = mod.getToLoad(dependencyDir, phase);
    auto sorted = modloader::topologicalSort(deps);

    for (auto const& dep : sorted) {
        if (skipLoad.contains(dep.object.path)) {
            continue;
        }

        auto result = openLibrary(dep.object.path);
        skipLoad.emplace(dep.object.path);
        // ignore result
        // this is however a problem since library load errors
        // are ignored
        handleResult(result, dep.object, dep.dependencies);
    }

    auto result = openLibrary(mod.path);
    skipLoad.emplace(mod.path);

    return handleResult(result, mod, deps);
}

std::vector<LoadResult> modloader::loadMods(std::span<SharedObject const> const mods, std::filesystem::path const& dependencyDir, std::unordered_set<std::string>& skipLoad, LoadPhase phase) {
    std::vector<LoadResult> results;

    for (auto const& mod : mods) {
        if (skipLoad.contains(mod.path)) {
            continue;
        }

        results.emplace_back(modloader::loadMod(mod, dependencyDir, skipLoad, phase));
    }

    return results;
}
