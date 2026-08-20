# Android native build: SDL2 from source (android/sdl, via fetch_sdl.sh)
# plus the whole port from the repo root as a shared library named
# libmain.so (SDLActivity loads it and calls SDL_main).
cmake_minimum_required(VERSION 3.22)
project(ge007-android C CXX)

set(SDL_SRC "${CMAKE_CURRENT_SOURCE_DIR}/../../sdl")
if(NOT EXISTS "${SDL_SRC}/CMakeLists.txt")
    message(FATAL_ERROR "SDL2 source not found at ${SDL_SRC} - run android/fetch_sdl.sh first")
endif()

set(SDL_STATIC OFF CACHE BOOL "" FORCE)
set(SDL_SHARED ON CACHE BOOL "" FORCE)
set(SDL_TEST OFF CACHE BOOL "" FORCE)
add_subdirectory("${SDL_SRC}" SDL2-build)

set(GE_ANDROID ON)
set(GE_HAVE_SDL2 ON)
set(GE_SDL2_TARGET SDL2::SDL2)
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../.." ge007-build)
