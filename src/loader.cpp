#include "loader.hpp"

#include <string>
#include <utility>
#include <vector>
#include <span>
#include <filesystem>
#include <fstream>
#include <optional>
#include <stack>
#include <unordered_set>
#include <unordered_map>

#include <elf.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <cstdlib>

// I wrote all of this while being tempted by Stack to use rust
// temptation is very strong

using namespace modloader;

template <typename T>
using StackDoubleFlow = std::stack<T>;


template<typename T>
T& readAtOffset(std::span<uint8_t> f, ptrdiff_t offset) {
    return *reinterpret_cast<T*>(&f[offset]);
}

std::string_view readAtOffset(std::span<uint8_t> f, ptrdiff_t offset, size_t size) {
    return {reinterpret_cast<char const*>(&f[offset]), size};
}

template<typename T>
std::span<T> readManyAtOffset(std::span<uint8_t> f, ptrdiff_t offset, size_t amount, size_t size) {
    T* begin = reinterpret_cast<T*>(f.data() + offset);
    T* end = begin + (amount * size);
    return std::span<T>(begin, end);
}


std::optional<std::pair<SharedObject, LoadPhase>> getSharedObject(LoadPhase phase, std::filesystem::path const& name) {
    std::unordered_map<LoadPhase, std::filesystem::path> pathsMap = {
        {LoadPhase::Libs, "libs"},
        {LoadPhase::EarlyMods, "early_mods"},
        {LoadPhase::Mods, "mods"}
    };

    StackDoubleFlow<std::string> paths;

    // i = 2
    // mods last
    // early mods before
    // libs first
    for (LoadPhase i = phase; i <= phase; i = (static_cast<LoadPhase>(static_cast<int>(i) + 1))) {
        paths.emplace(pathsMap[i]);
    }

    auto dir = paths.top();
    paths.pop();

    auto openedPhase = static_cast<LoadPhase>(phase);

    while (!std::filesystem::exists(dir / name)) {
        if (paths.empty()) {
            return std::nullopt;
        }

        dir = paths.top();
        paths.pop();
        openedPhase = static_cast<LoadPhase>(static_cast<int>(openedPhase) + 1);
    }

    return {{SharedObject(dir / name), openedPhase}};
}

//TODO: Use a map?
std::vector<modloader::Dependency> modloader::SharedObject::getToLoad(LoadPhase phase) const {
    int fd = open(this->path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd == -1) {
//        MLogger::GetLogger().error("Error reading file at %s: %s", path.c_str(),
//                                   strerror(errno));
//        SAFE_ABORT();
        throw std::runtime_error("Unable to open file descriptor");
    }

    struct stat st;
    fstat(fd, &st);
    size_t size = st.st_size;

    void *mapped = mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);

    if (mapped == MAP_FAILED) {
        throw std::runtime_error("Unable to memory map");
    }

    std::span<uint8_t> f(static_cast<uint8_t*>(mapped), static_cast<uint8_t*>(mapped) + size);

    auto elf = readAtOffset<Elf64_Ehdr>(f, 0);
    auto sectionHeaders = readManyAtOffset<Elf64_Shdr>(f, elf.e_shoff, elf.e_shnum, elf.e_shentsize);

    std::vector<Dependency> dependencies;

    for (auto it = sectionHeaders.begin(); it != sectionHeaders.end(); it++) {
        auto const& sectionHeader = *it;
        if (sectionHeader.sh_type != SHT_DYNAMIC) { continue; }

        for (size_t i = 0; i < sectionHeader.sh_size / sectionHeader.sh_entsize; i++) {
            auto dyn = readAtOffset<Elf64_Dyn>(f, sectionHeader.sh_offset);

            if (dyn.d_tag != DT_NEEDED) {
                continue;
            }

            std::string_view name = readAtOffset(f, sectionHeaders[sectionHeader.sh_link].sh_offset, dyn.d_un.d_val);

            if (name.data() == nullptr) {
                continue;
            }

            auto optObj = getSharedObject(phase, name);

                std::ofstream fff(std::filesystem::current_path() / std::string("test") / ".txt");
            fff << name << std::endl;
                fff.flush();

            if (optObj) {
                auto [obj, openedPhase] = *optObj;
                dependencies.emplace_back(obj, obj.getToLoad(openedPhase));
            }
        }
    }

    if(munmap(mapped, size) == -1) {
        // TODO: Error check
        // this todo will never be done, I'm betting on it
    }

    return dependencies;
}

void sortDependencies(std::span<Dependency> deps) {
    std::stable_sort(deps.begin(), deps.end(), [](Dependency const& a, Dependency const& b) {
        return a.object.path > b.object.path;
    });
}

// https://www.geeksforgeeks.org/cpp-program-for-topological-sorting/
// Use mutable ref to avoid making a new vector that is sorted
// TODO: Should we even bother?
void topologicalSortRecurse(Dependency& main, StackDoubleFlow<Dependency>& stack, std::unordered_set<std::string_view>& visited) {
    if (visited.contains(main.object.path.c_str())) { return; }

    visited.emplace(main.object.path.c_str());
    sortDependencies(main.dependencies);

    for (auto& dep : main.dependencies) {
        if (visited.contains(dep.object.path.c_str())) { continue; }

        visited.emplace(dep.object.path.c_str());

        sortDependencies(dep.dependencies);
        for (auto& innerDep : dep.dependencies) {
            if (!visited.contains(innerDep.object.path.c_str())) {
                topologicalSortRecurse(innerDep, stack, visited);
            }
        }
    }

    stack.emplace(main);
}

StackDoubleFlow<Dependency> modloader::topologicalSort(std::span<Dependency const> const list) {
    StackDoubleFlow<Dependency> dependencies;
    std::unordered_set<std::string_view> visited;

    std::vector<Dependency> deps(list.begin(), list.end());
    sortDependencies(deps);

    for (Dependency& dep : deps) {
        topologicalSortRecurse(dep, dependencies, visited);
    }

    return dependencies;
}