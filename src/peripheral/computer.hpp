/*
 * peripheral/computer.hpp
 * CraftOS-PC 2
 * 
 * This file defines the class for the computer peripheral.
 * 
 * This code is licensed under the MIT License.
 * Copyright (c) 2019 JackMacWindows. 
 */

#ifndef PERIPHERAL_COMPUTER_HPP
#define PERIPHERAL_COMPUTER_HPP
#include "peripheral.hpp"
#include "../Computer.hpp"

class computer: public peripheral {
private:
    friend class Computer;
    Computer * comp;
    Computer * thiscomp;
    int turnOn(lua_State *L);
    int shutdown(lua_State *L);
    int reboot(lua_State *L);
    int getID(lua_State *L);
    int isOn(lua_State *L);
public:
    static library_t methods;
    static peripheral * init(lua_State *L, const char * side) {return new computer(L, side);}
    static void deinit(peripheral * p) {delete (computer*)p;}
    destructor getDestructor() {return deinit;}
    library_t getMethods() {return methods;}
    computer(lua_State *L, const char * side);
    ~computer();
    int call(lua_State *L, const char * method);
    void update() {}
};

#endif