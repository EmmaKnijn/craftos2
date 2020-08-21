/*
 * platform.cpp
 * CraftOS-PC 2
 * 
 * This file controls which platform implementation will be compiled.
 * 
 * This code is licensed under the MIT license.
 * Copyright (c) 2019-2020 JackMacWindows.
 */

#define CRAFTOSPC_INTERNAL
#include "platform.hpp"
#ifdef WIN32
#include "platform_win.cpp"
#else
#ifdef __APPLE__
#include "platform_darwin.cpp"
#else
#ifdef __linux__
#include "platform_linux.cpp"
#else
#ifdef __EMSCRIPTEN__
#include "platform_emscripten.cpp"
#else
#error Unknown platform
#endif
#endif
#endif
#endif