cmake_minimum_required(VERSION 3.22)

if (NOT DEFINED BUILD_FOR)
    set(BUILD_FOR QUEST)
endif()

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/qpm2.json" QPM_MANIFEST)
string(JSON MOD_ID GET "${QPM_MANIFEST}" id)
string(JSON MOD_VERSION GET "${QPM_MANIFEST}" version)
set(COMPILE_ID sl2)
set(EXTERN_DIR "${CMAKE_CURRENT_SOURCE_DIR}/extern")
set(SHARED_DIR "${CMAKE_CURRENT_SOURCE_DIR}/shared")

if (${BUILD_FOR} STREQUAL LINUX)
    message("Building for Linux...")
    set(CMAKE_TOOLCHAIN_FILE "")
    set(CMAKE_SYSTEM_NAME ${CMAKE_HOST_SYSTEM_NAME})

    add_compile_definitions(LINUX_TEST)
else()
    file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/ndkpath.txt" NDK_PATH LIMIT_COUNT 1)
    file(TO_CMAKE_PATH "${NDK_PATH}" NDK_PATH)
    set(CMAKE_TOOLCHAIN_FILE "${NDK_PATH}/build/cmake/android.toolchain.cmake")
    set(ANDROID_PLATFORM 24)
    set(ANDROID_ABI arm64-v8a)
    set(ANDROID_STL c++_static)
    set(ANDROID_USE_LEGACY_TOOLCHAIN_FILE OFF)
endif()

project(${COMPILE_ID})

# c++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED 20)
if (${CMAKE_BUILD_TYPE} STREQUAL "RELEASE" OR ${CMAKE_BUILD_TYPE} STREQUAL "RelWithDebInfo")
    # LTO
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()

# define that stores the actual source directory
set(SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src)
set(INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/include)

# compile options used
add_compile_options(-fPIE -flto -fPIC -fno-rtti -fvisibility=hidden -Os -O2)
add_compile_definitions(MOD_VERSION=\"${MOD_VERSION}\")
add_compile_definitions(MOD_ID=\"${MOD_ID}\")
# Ensure we don't create any references through flamingo
add_compile_definitions(FLAMINGO_HEADER_ONLY FMT_HEADER_ONLY)
add_compile_options(-Wall -Wextra -Werror -Wpedantic -Wno-gnu-zero-variadic-macro-arguments)
# add_link_options(-fuse-ld=lld)
# compile definitions used

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if (DEFINED TEST_BUILD)
    MESSAGE(STATUS "Compiling with test defines")
endif()

file(GLOB_RECURSE cpp_file_list ${SOURCE_DIR}/*.cpp)

set(FLAMINGO_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/flamingo")

list(APPEND cpp_file_list
    ${FLAMINGO_DIR}/src/fixups.cpp
    ${FLAMINGO_DIR}/src/installer.cpp
    ${FLAMINGO_DIR}/src/page-allocator.cpp
)

add_library(
    ${COMPILE_ID} 
    SHARED
    ${cpp_file_list}
)

# add src dir as include dir
target_include_directories(${COMPILE_ID} PRIVATE ${SOURCE_DIR})
# add include dir as include dir
target_include_directories(${COMPILE_ID} PRIVATE ${INCLUDE_DIR})
# add shared dir as include dir
target_include_directories(${COMPILE_ID} PUBLIC ${SHARED_DIR})

target_include_directories(${COMPILE_ID} PRIVATE
    ${FLAMINGO_DIR}/shared
    ${EXTERN_DIR}/includes/fmt
    ${EXTERN_DIR}/includes/capstone
)
target_link_libraries(${COMPILE_ID} PRIVATE "${EXTERN_DIR}/libs/libcapstone.a")


if (${BUILD_FOR} STREQUAL LINUX)
    message("Building tests")
    file(GLOB_RECURSE cpp_test_file_list ./test/*.cpp)

    set(testname BootstrapTest)

    add_executable(${testname} ${cpp_test_file_list})

    # add include dir as include dir
    target_include_directories(${testname} PRIVATE ${INCLUDE_DIR})
    target_include_directories(${testname} PRIVATE ./test/)
    # add shared dir as include dir
    target_include_directories(${testname} PUBLIC ${SHARED_DIR})

    target_link_libraries(${testname} PRIVATE ${COMPILE_ID} -ldl)

else()
    target_link_libraries(${COMPILE_ID} PRIVATE -llog -ldl)

add_custom_command(TARGET ${COMPILE_ID} POST_BUILD
        COMMAND ${CMAKE_STRIP} -g -S -d --strip-all
        "lib${COMPILE_ID}.so" -o "stripped_lib${COMPILE_ID}.so"
        COMMENT "Strip debug symbols done on final binary.")

add_custom_command(TARGET ${COMPILE_ID} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E rename lib${COMPILE_ID}.so debug_lib${COMPILE_ID}.so
        COMMENT "Rename the lib to debug_ since it has debug symbols"
        )

add_custom_command(TARGET ${COMPILE_ID} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E rename stripped_lib${COMPILE_ID}.so lib${COMPILE_ID}.so
        COMMENT "Rename the stripped lib to regular"
        )

endif()
