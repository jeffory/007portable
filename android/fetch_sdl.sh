#!/usr/bin/env bash
# Fetch the SDL2 source (native build input) and vendor its Java glue
# (org.libsdl.app.*) into the app source tree. Neither is committed:
# android/.gitignore covers both.
set -ue
cd "$(dirname "$0")"

SDL_VER=2.30.9

if [ ! -f sdl/CMakeLists.txt ]; then
    curl -sL "https://github.com/libsdl-org/SDL/releases/download/release-$SDL_VER/SDL2-$SDL_VER.tar.gz" -o sdl.tar.gz
    tar xzf sdl.tar.gz
    rm -rf sdl && mv "SDL2-$SDL_VER" sdl
    rm sdl.tar.gz
fi

mkdir -p app/src/main/java/org/libsdl/app
cp sdl/android-project/app/src/main/java/org/libsdl/app/*.java app/src/main/java/org/libsdl/app/

echo "SDL2 $SDL_VER ready (source: android/sdl, Java glue: app/src/main/java/org/libsdl/app)"
