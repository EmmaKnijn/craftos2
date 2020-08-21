/*
 * mounter.hpp
 * CraftOS-PC 2
 * 
 * This file defines the methods for the mounter API.
 * 
 * This code is licensed under the MIT license.
 * Copyright (c) 2019-2020 JackMacWindows.
 */

#ifndef MOUNTER_HPP
#define MOUNTER_HPP
#include "lib.hpp"
#include "Computer.hpp"
extern library_t mounter_lib;
extern std::string fixpath(Computer *comp, const char * path, bool exists, bool addExt = true, std::string * mountPath = NULL, bool getAllResults = false, bool * isRoot = NULL);
extern bool addMount(Computer *comp, const char * real_path, const char * comp_path, bool read_only);
extern bool fixpath_ro(Computer *comp, const char * path);
#endif
