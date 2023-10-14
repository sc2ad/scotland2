# scotland2

A Quest/Android specific modloader for Beat Saber.

## Features

- "something regarding scene changes similar to how bsipa has on enable and whatnot"
- plain check for other mods being installed
- unload method except maybe too crashy
- on enable / on unity init method
- maybe do something with registration instead of extern c stuff?
- [thing](https://discord.com/channels/629851957439365131/629851957439365135/975450045585440788)

## Installation/Usage

This is a [LibProxyMain](https://github.com/sc2ad/LibMainLoader) compatible modloader. Thus, it should be placed at: `/sdcard/ModData/<APP ID>/Modloader/libsl2.so`.
This modloader has several call in points. It performs a topological sort of all depenents that need to be loaded and loads them in turn. `libs` are loaded very early, on `modloader_load`, while `early_mods` are also constructed at this time. `early_mods` have their `load` function called on them after `il2cpp_init`, which is hooked via [Flamingo](https://github.com/sc2ad/Flamingo) to allow us to perform loading at a sufficiently late time.

`mods` WILL be loaded on some unity initialization function, currently TBD.
