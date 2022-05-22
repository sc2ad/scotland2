#include "modloader-calls.hpp"
#include "modloader.hpp"

std::filesystem::path Modloader::modloader_path;

void Modloader::construct_mods() {
    // Construct early mods/libs
    // Call setup() on these
}

void Modloader::load_mods() {
    // Construct late mods/libs
    // Call setup() on these newly opened things
    // Also call load() on everything
}