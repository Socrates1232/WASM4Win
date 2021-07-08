# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION 3.21.0-rc2)

# We name the project and the target for the ExternalProject_Add() call
# to something that will highlight to the user what we are working on if
# something goes wrong and an error message is produced.

project(libuv-populate NONE)

# Pass through things we've already detected in the main project to avoid
# paying the cost of redetecting them again in ExternalProject_Add()
set(GIT_EXECUTABLE [==[C:/Program Files/Git/mingw64/bin/git.exe]==])
set(GIT_VERSION_STRING [==[2.32.0.windows.2]==])
set_property(GLOBAL PROPERTY _CMAKE_FindGit_GIT_EXECUTABLE_VERSION
  [==[C:/Program Files/Git/mingw64/bin/git.exe;2.32.0.windows.2]==]
)


include(ExternalProject)
ExternalProject_Add(libuv-populate
                     "UPDATE_DISCONNECTED" "False" "GIT_REPOSITORY" "https://github.com/libuv/libuv.git" "GIT_TAG" "v1.39.0"
                    SOURCE_DIR          "D:/wasm/wasm-micro-runtime/product-mini/platforms/windows/build/_deps/libuv-src"
                    BINARY_DIR          "D:/wasm/wasm-micro-runtime/product-mini/platforms/windows/build/_deps/libuv-build"
                    CONFIGURE_COMMAND   ""
                    BUILD_COMMAND       ""
                    INSTALL_COMMAND     ""
                    TEST_COMMAND        ""
                    USES_TERMINAL_DOWNLOAD  YES
                    USES_TERMINAL_UPDATE    YES
)


