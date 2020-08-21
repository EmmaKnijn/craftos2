/*
 * TRoRTerminal.hpp
 * CraftOS-PC 2
 * 
 * This file defines the TRoRTerminal class.
 * 
 * This code is licensed under the MIT license.
 * Copyright (c) 2019-2020 JackMacWindows.
 */

#ifndef TERMINAL_TRORTERMINAL_HPP
#define TERMINAL_TRORTERMINAL_HPP
#include "Terminal.hpp"
#include <set>

class TRoRTerminal: public Terminal {
    static std::set<unsigned> currentIDs;
public:
    static void init();
    static void quit();
    static void showGlobalMessage(Uint32 flags, const char * title, const char * message);
    TRoRTerminal(std::string title);
    ~TRoRTerminal() override;
    void render() override {}
    void showMessage(Uint32 flags, const char * title, const char * message) override;
    void setLabel(std::string label) override;
    bool resize(int w, int h) override {return false;}
};

#endif