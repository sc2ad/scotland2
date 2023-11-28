& cmake -G "Ninja" -DCMAKE_BUILD_TYPE="Debug" -DTEST_BUILD=1 -B build
& cmake --build ./build

& adb push ./build/libsl2.so /sdcard/ModData/com.beatgames.beatsaber/Modloader

& adb shell am force-stop com.beatgames.beatsaber
& adb shell am start com.beatgames.beatsaber/com.unity3d.player.UnityPlayerActivity