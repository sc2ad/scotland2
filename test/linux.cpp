#ifdef LINUX_TEST

#include <iostream>
#include "loader.hpp"

void logDependencies(std::span<modloader::Dependency const> dependencies, size_t indent = 1) {
    for (auto const& dep : dependencies) {
        std::cout << std::string(indent * 3, ' ') << '-' << dep.object.path.filename() << std::endl;
        logDependencies(dep.dependencies, indent + 1);
    }
}

int main() {

    auto path = std::filesystem::current_path();
    auto mod = modloader::SharedObject(path/"test"/"libchroma.so");


    std::cout << "Loading " << mod.path.c_str() << std::endl;

    auto dependencies = mod.getToLoad(modloader::LoadPhase::Mods);

    for (auto const& dep : dependencies) {
        std::cout << "-" << dep.object.path.filename() << std::endl;
        logDependencies(dep.dependencies);
    }

    std::cout << std::string(3, '\n') << "Sorted:" << std::endl;

    auto sorted = modloader::topologicalSort(dependencies);
    while (!sorted.empty()) {
        auto const& dep = sorted.top();
        std::cout << "-" << dep.object.path.filename() << std::endl;
        sorted.pop();
    }
}

#endif