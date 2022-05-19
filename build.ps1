
function Clean-Build-Folder {
    if (Test-Path -Path "build")
    {
        remove-item build -R
        new-item -Path build -ItemType Directory
    } else {
        new-item -Path build -ItemType Directory
    }
}

$NDKPath = Get-Content $PSScriptRoot/ndkpath.txt

# Clean-Build-Folder
# build tests

& cmake -G "Ninja" -DCMAKE_BUILD_TYPE="RelWithDebInfo" -B build
& cmake --build ./build

$ExitCode = $LastExitCode

# Post build, we actually want to transform the compile_commands.json file such that it has no \\ characters and ONLY has / characters
(Get-Content -Path build/compile_commands.json) |
    ForEach-Object {$_ -Replace '\\\\', '/'} | Set-Content -Path build/compile_commands.json


exit $ExitCode