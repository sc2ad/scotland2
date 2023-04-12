#pragma once

namespace modloader {

// Three types of "mods":
// Library - not really a mod, no special functions to be associated with it, just dlopen it and move on
// EarlyMod - dlopened on application open + launch, have various helper methods. Analogous to mods as of 2022
// Mod - dlopened after unity is initialized-- all game methods will be safe to use at this point

}
