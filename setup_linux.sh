#!/bin/sh
cmake -G "Ninja" -DBUILD_FOR=LINUX -DCMAKE_BUILD_TYPE="RelWithDebInfo" -B build_linux 
cmake --build ./build_linux