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
This modloader has several call in points. It performs a topological sort of all depenents that need to be loaded and loads them in turn. `libs` are loaded very early, on `modloader_load`, while `early_mods` are also constructed at this time. `early_mods` have their `load` function called on them after `il2cpp_init`, which is hooked via [Flamingo](https://github.com/sc2ad/Flamingo) to allow us to perform loading at a sufficiently late time. early mods will also have a function named `late_load` called when mods are constructed, but more on that in the next paragraph.

`mods` will be constructed when the libunity.so method `UnityPostLoadApplication` is called. This method is hooked with flamingo through an xref trace through JNI_Onload all the way to that method. After mods are opened and constructed, early mods get their late_load method called, after which mods will get their load and late_load methods called.

Here is a table containing what gets opened and called when.
 - `dlopen` means the .so file will be opened at that time.
 - `setup` means the setup method which fills the mod info is called at that point.
 - `load` means the load method is called at that point
 - `late_load` means that that is when late_load is invoked.

|                          | Lib      | Early Mod        | Mod                                    |
|--------------------------|----------|------------------|----------------------------------------|
| app start                | `dlopen` | `dlopen`, `setup`| -                                      |
| il2cpp_init              | -        | `load`           | -                                      |
| UnityPostLoadApplication | -        | `late_load`      | `dlopen`, `setup`, `load`, `late_load` |
