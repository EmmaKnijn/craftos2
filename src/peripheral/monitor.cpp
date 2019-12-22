/*
 * peripheral/monitor.cpp
 * CraftOS-PC 2
 * 
 * This file implements the methods for the monitor peripheral.
 * 
 * This code is licensed under the MIT license.
 * Copyright (c) 2019 JackMacWindows.
 */

#include "monitor.hpp"
#include "../os.hpp"
#include "../CLITerminalWindow.hpp"

extern int log2i(int);
extern unsigned char htoi(char c);
extern bool cli;

monitor::monitor(lua_State *L, const char * side) {
#ifndef NO_CLI
    if (cli) {
        term = new CLITerminalWindow("CraftOS Terminal: Monitor " + std::string(side));
    } else 
#endif
    {
        term = (TerminalWindow*)queueTask([ ](void* side)->void* {
            return new TerminalWindow("CraftOS Terminal: Monitor " + std::string((const char*)side));
        }, (void*)side);
    }
    term->canBlink = false;
}

monitor::~monitor() {delete term;}

int monitor::write(lua_State *L) {
    if (!lua_isstring(L, 1)) bad_argument(L, "string", 1);
    size_t str_sz;
    const char * str = lua_tolstring(L, 1, &str_sz);
    std::lock_guard<std::mutex> lock(term->locked);
    for (unsigned i = 0; i < str_sz && term->blinkX < term->width; i++, term->blinkX++) {
        term->screen[term->blinkY][term->blinkX] = str[i];
        term->colors[term->blinkY][term->blinkX] = colors;
    }
    term->changed = true;
    return 0;
}

int monitor::scroll(lua_State *L) {
    if (!lua_isnumber(L, 1)) bad_argument(L, "number", 1);
    int lines = lua_tointeger(L, 1);
    std::lock_guard<std::mutex> lock(term->locked);
    for (int i = lines; i < term->height; i++) {
        term->screen[i-lines] = term->screen[i];
        term->colors[i-lines] = term->colors[i];
    }
    for (int i = term->height; i < term->height + lines; i++) {
        term->screen[i-lines] = std::vector<unsigned char>(term->width, ' ');
        term->colors[i-lines] = std::vector<unsigned char>(term->width, colors);
    }
    term->changed = true;
    return 0;
}

int monitor::setCursorPos(lua_State *L) {
    if (!lua_isnumber(L, 1)) bad_argument(L, "number", 1);
    if (!lua_isnumber(L, 2)) bad_argument(L, "number", 2);
    std::lock_guard<std::mutex> lock(term->locked);
    term->blinkX = lua_tointeger(L, 1) - 1;
    term->blinkY = lua_tointeger(L, 2) - 1;
    if (term->blinkX >= term->width) term->blinkX = term->width - 1;
    if (term->blinkY >= term->height) term->blinkY = term->height - 1;
    if (term->blinkX < 0) term->blinkX = 0;
    if (term->blinkY < 0) term->blinkY = 0;
    return 0;
}

int monitor::setCursorBlink(lua_State *L) {
    if (!lua_isboolean(L, 1)) bad_argument(L, "boolean", 1);
    std::lock_guard<std::mutex> lock(term->locked);
    term->canBlink = lua_toboolean(L, 1);
    return 0;
}

int monitor::getCursorPos(lua_State *L) {
    lua_pushinteger(L, term->blinkX + 1);
    lua_pushinteger(L, term->blinkY + 1);
    return 2;
}

int monitor::getCursorBlink(lua_State *L) {
    lua_pushboolean(L, term->canBlink);
    return 1;
}

int monitor::getSize(lua_State *L) {
    lua_pushinteger(L, term->width);
    lua_pushinteger(L, term->height);
    return 2;
}

int monitor::clear(lua_State *L) {
    std::lock_guard<std::mutex> lock(term->locked);
    if (term->mode != 0) {
        term->pixels = vector2d<unsigned char>(term->width * TerminalWindow::fontWidth, term->height * TerminalWindow::fontHeight, 0x0F);
    } else {
        term->screen = vector2d<unsigned char>(term->width, term->height, ' ');
        term->colors = vector2d<unsigned char>(term->width, term->height, 0xF0);
    }
    term->changed = true;
    return 0;
}

int monitor::clearLine(lua_State *L) {
    std::lock_guard<std::mutex> lock(term->locked);
    term->screen[term->blinkY] = std::vector<unsigned char>(term->width, ' ');
    term->colors[term->blinkY] = std::vector<unsigned char>(term->width, colors);
    term->changed = true;
    return 0;
}

int monitor::setTextColor(lua_State *L) {
    if (!lua_isnumber(L, 1)) bad_argument(L, "number", 1);
    int c = log2i(lua_tointeger(L, 1));
    colors = (colors & 0xf0) | c;
    return 0;
}

int monitor::setBackgroundColor(lua_State *L) {
    if (!lua_isnumber(L, 1)) bad_argument(L, "number", 1);
    int c = log2i(lua_tointeger(L, 1));
    colors = (colors & 0x0f) | (c << 4);
    return 0;
}

int monitor::isColor(lua_State *L) {
    lua_pushboolean(L, true);
    return 1;
}

int monitor::getTextColor(lua_State *L) {
    lua_pushinteger(L, 1 << ((int)colors & 0x0f));
    return 1;
}

int monitor::getBackgroundColor(lua_State *L) {
    lua_pushinteger(L, 1 << ((int)colors >> 4));
    return 1;
}

int monitor::blit(lua_State *L) {
    if (!lua_isstring(L, 1)) bad_argument(L, "string", 1);
    if (!lua_isstring(L, 2)) bad_argument(L, "string", 2);
    if (!lua_isstring(L, 3)) bad_argument(L, "string", 3);
    size_t str_sz, fg_sz, bg_sz;
    const char * str = lua_tolstring(L, 1, &str_sz);
    const char * fg = lua_tolstring(L, 2, &fg_sz);
    const char * bg = lua_tolstring(L, 3, &bg_sz);
    if (str_sz != fg_sz || fg_sz != bg_sz) {
        lua_pushstring(L, "Arguments must be the same length");
        lua_error(L);
    }
    std::lock_guard<std::mutex> lock(term->locked);
    for (unsigned i = 0; i < str_sz && term->blinkX < term->width; i++, term->blinkX++) {
        colors = htoi(bg[i]) << 4 | htoi(fg[i]);
        term->screen[term->blinkY][term->blinkX] = str[i];
        term->colors[term->blinkY][term->blinkX] = colors;
    }
    term->changed = true;
    return 0;
}

int monitor::getPaletteColor(lua_State *L) {
    int color = log2i(lua_tointeger(L, 1));
    lua_pushnumber(L, term->palette[color].r/255.0);
    lua_pushnumber(L, term->palette[color].g/255.0);
    lua_pushnumber(L, term->palette[color].b/255.0);
    return 3;
}

int monitor::setPaletteColor(lua_State *L) {
    if (!lua_isnumber(L, 1)) bad_argument(L, "number", 1);
    if (!lua_isnumber(L, 2)) bad_argument(L, "number", 2);
    if (!lua_isnumber(L, 3)) bad_argument(L, "number", 3);
    if (!lua_isnumber(L, 4)) bad_argument(L, "number", 4);
    int color = log2i(lua_tointeger(L, 1));
    std::lock_guard<std::mutex> lock(term->locked);
    term->palette[color].r = (int)(lua_tonumber(L, 2) * 255);
    term->palette[color].g = (int)(lua_tonumber(L, 3) * 255);
    term->palette[color].b = (int)(lua_tonumber(L, 4) * 255);
    term->changed = true;
    return 0;
}

int monitor::setGraphicsMode(lua_State *L) {
    if (!lua_isboolean(L, 1)) bad_argument(L, "boolean", 1);
    std::lock_guard<std::mutex> lock(term->locked);
    term->mode = lua_toboolean(L, 1);
    term->changed = true;
    return 0;
}

int monitor::getGraphicsMode(lua_State *L) {
    lua_pushboolean(L, term->mode);
    return 1;
}

int monitor::setPixel(lua_State *L) {
    if (!lua_isnumber(L, 1)) bad_argument(L, "number", 1);
    if (!lua_isnumber(L, 2)) bad_argument(L, "number", 2);
    if (!lua_isnumber(L, 3)) bad_argument(L, "number", 3);
    int x = lua_tointeger(L, 1);
    int y = lua_tointeger(L, 2);
    std::lock_guard<std::mutex> lock(term->locked);
    if (x >= term->width * term->fontWidth || y >= term->height * term->fontHeight || x < 0 || y < 0) return 0;
    term->pixels[y][x] = log2i(lua_tointeger(L, 3));
    term->changed = true;
    return 0;
}

int monitor::getPixel(lua_State *L) {
    if (!lua_isnumber(L, 1)) bad_argument(L, "number", 1);
    if (!lua_isnumber(L, 2)) bad_argument(L, "number", 2);
    int x = lua_tointeger(L, 1);
    int y = lua_tointeger(L, 2);
    if (x >= term->width * term->fontWidth || y >= term->height * term->fontHeight || x < 0 || y < 0) return 0;
    lua_pushinteger(L, 2^term->pixels[lua_tointeger(L, 2)][lua_tointeger(L, 1)]);
    return 1;
}

int monitor::setTextScale(lua_State *L) {
    if (!lua_isnumber(L, 1)) bad_argument(L, "number", 1);
    std::lock_guard<std::mutex> lock(term->locked);
    term->charScale = lua_tonumber(L, -1) * 2;
    queueTask([ ](void* term)->void*{((TerminalWindow*)term)->setCharScale(((TerminalWindow*)term)->charScale); return NULL;}, term);
    term->changed = true;
    return 0;
}

int monitor::getTextScale(lua_State *L) {
    lua_pushnumber(L, term->charScale / 2.0);
    return 1;
}

int monitor::drawPixels(lua_State *L) {
    if (!lua_isnumber(L, 1)) bad_argument(L, "number", 1);
    if (!lua_isnumber(L, 2)) bad_argument(L, "number", 2);
    if (!lua_istable(L, 3)) bad_argument(L, "table", 3);
    std::lock_guard<std::mutex> lock(term->locked);
    unsigned int init_x = lua_tointeger(L, 1), init_y = lua_tointeger(L, 2);
    for (unsigned int y = 1; y < lua_objlen(L, 3) && init_y + y - 1 < term->height * TerminalWindow::fontHeight; y++) {
        lua_pushinteger(L, y);
        lua_gettable(L, 3); 
        if (lua_isstring(L, -1)) {
            size_t str_sz;
            const char * str = lua_tolstring(L, -1, &str_sz);
            if (init_x + str_sz - 1 < term->width * TerminalWindow::fontWidth)
                memcpy(&term->pixels[init_y+y-1][init_x], str, str_sz);
        } else if (lua_istable(L, -1)) {
            for (unsigned int x = 1; x < lua_objlen(L, -1) && init_x + x - 1 < term->width * TerminalWindow::fontWidth; x++) {
                lua_pushinteger(L, x);
                lua_gettable(L, -2);
                term->pixels[init_y+y-1][init_x+x-1] = (unsigned char)(lua_tointeger(L, -1) % 256);
                lua_pop(L, 1);
            }
        }
        lua_pop(L, 1);
    }
    term->changed = true;
    return 0;
}

int monitor::call(lua_State *L, const char * method) {
    std::string m(method);
    if (m == "write") return write(L);
    else if (m == "scroll") return scroll(L);
    else if (m == "setCursorPos") return setCursorPos(L);
    else if (m == "setCursorBlink") return setCursorBlink(L);
    else if (m == "getCursorPos") return getCursorPos(L);
    else if (m == "getSize") return getSize(L);
    else if (m == "clear") return clear(L);
    else if (m == "clearLine") return clearLine(L);
    else if (m == "setTextColour") return setTextColor(L);
    else if (m == "setTextColor") return setTextColor(L);
    else if (m == "setBackgroundColour") return setBackgroundColor(L);
    else if (m == "setBackgroundColor") return setBackgroundColor(L);
    else if (m == "isColour") return isColor(L);
    else if (m == "isColor") return isColor(L);
    else if (m == "getTextColour") return getTextColor(L);
    else if (m == "getTextColor") return getTextColor(L);
    else if (m == "getBackgroundColour") return getBackgroundColor(L);
    else if (m == "getBackgroundColor") return getBackgroundColor(L);
    else if (m == "blit") return blit(L);
    else if (m == "getPaletteColor") return getPaletteColor(L);
    else if (m == "getPaletteColour") return getPaletteColor(L);
    else if (m == "setPaletteColor") return setPaletteColor(L);
    else if (m == "setPaletteColour") return setPaletteColor(L);
    else if (m == "setGraphicsMode") return setGraphicsMode(L);
    else if (m == "getGraphicsMode") return getGraphicsMode(L);
    else if (m == "setPixel") return setPixel(L);
    else if (m == "getPixel") return getPixel(L);
    else if (m == "setTextScale") return setTextScale(L);
    else if (m == "getTextScale") return getTextScale(L);
    else if (m == "drawPixels") return drawPixels(L);
    else return 0;
}

void monitor::update() {}

const char * monitor_keys[31] = {
    "write",
    "scroll",
    "setCursorPos",
    "setCursorBlink",
    "getCursorPos",
    "getCursorBlink",
    "getSize",
    "clear",
    "clearLine",
    "setTextColour",
    "setTextColor",
    "setBackgroundColour",
    "setBackgroundColor",
    "isColour",
    "isColor",
    "getTextColour",
    "getTextColor",
    "getBackgroundColour",
    "getBackgroundColor",
    "blit",
    "getPaletteColor",
    "getPaletteColour",
    "setPaletteColor",
    "setPaletteColour",
    "setGraphicsMode",
    "getGraphicsMode",
    "setPixel",
    "getPixel",
    "setTextScale",
    "getTextScale",
    "drawPixels"
};

library_t monitor::methods = {"monitor", 31, monitor_keys, NULL, nullptr, nullptr};