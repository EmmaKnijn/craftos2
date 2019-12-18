/*
 * TerminalWindow.cpp
 * CraftOS-PC 2
 * 
 * This file implements the TerminalWindow class.
 * 
 * This code is licensed under the MIT license.
 * Copyright (c) 2019 JackMacWindows.
 */

#include "TerminalWindow.hpp"
#ifndef NO_PNG
#include <png++/png.hpp>
#endif
#include <sstream>
#include <assert.h>
#include "favicon.h"
#include "config.hpp"
#include "gif.hpp"
#include "os.hpp"
#define rgb(color) ((color.r << 16) | (color.g << 8) | color.b)

extern "C" {
    struct font_image {
        unsigned int 	 width;
        unsigned int 	 height;
        unsigned int 	 bytes_per_pixel; /* 2:RGB16, 3:RGB, 4:RGBA */
        unsigned char	 pixel_data[128 * 175 * 2 + 1];
    };
    extern struct font_image font_image;
}

Color defaultPalette[16] = {
    {0xf0, 0xf0, 0xf0},
    {0xf2, 0xb2, 0x33},
    {0xe5, 0x7f, 0xd8},
    {0x99, 0xb2, 0xf2},
    {0xde, 0xde, 0x6c},
    {0x7f, 0xcc, 0x19},
    {0xf2, 0xb2, 0xcc},
    {0x4c, 0x4c, 0x4c},
    {0x99, 0x99, 0x99},
    {0x4c, 0x99, 0xb2},
    {0xb2, 0x66, 0xe5},
    {0x33, 0x66, 0xcc},
    {0x7f, 0x66, 0x4c},
    {0x57, 0xa6, 0x4e},
    {0xcc, 0x4c, 0x4c},
    {0x11, 0x11, 0x11}
};

void MySDL_GetDisplayDPI(int displayIndex, float* dpi, float* defaultDpi)
{
    const float kSysDefaultDpi =
#ifdef __APPLE__
        72.0f;
#elif defined(_WIN32)
        96.0f;
#else
        96.0f;
#endif
 
    if (SDL_GetDisplayDPI(displayIndex, NULL, dpi, NULL) != 0)
    {
        // Failed to get DPI, so just return the default value.
        if (dpi) *dpi = kSysDefaultDpi;
    }
 
    if (defaultDpi) *defaultDpi = kSysDefaultDpi;
}

int TerminalWindow::fontScale = 2;
std::list<TerminalWindow*> TerminalWindow::renderTargets;
std::mutex TerminalWindow::renderTargetsLock;

TerminalWindow::TerminalWindow(int w, int h): width(w), height(h) {
    memcpy(palette, defaultPalette, sizeof(defaultPalette));
    screen = std::vector<std::vector<unsigned char> >(h, std::vector<unsigned char>(w, ' '));
    colors = std::vector<std::vector<unsigned char> >(h, std::vector<unsigned char>(w, 0xF0));
    pixels = std::vector<std::vector<unsigned char> >(h*fontHeight, std::vector<unsigned char>(w*fontWidth, 0x0F));
}

TerminalWindow::TerminalWindow(std::string title): TerminalWindow(51, 19) {
    //locked.unlock();
    float dpi, defaultDpi;
    MySDL_GetDisplayDPI(0, &dpi, &defaultDpi);
    dpiScale = (dpi / defaultDpi) - floor(dpi / defaultDpi) > 0.5 ? ceil(dpi / defaultDpi) : floor(dpi / defaultDpi);
    if (config.customFontPath == "hdfont") {
        fontScale = 1;
        charScale = 1;
        charWidth = fontWidth * 2/fontScale * charScale;
        charHeight = fontHeight * 2/fontScale * charScale;
    } else if (!config.customFontPath.empty()) {
        fontScale = config.customFontScale;
        charScale = 2 / fontScale;
        charWidth = fontWidth * 2/fontScale * charScale;
        charHeight = fontHeight * 2/fontScale * charScale;
    }
    if (config.customCharScale > 0) {
        charScale = config.customCharScale;
        charWidth = fontWidth * 2/fontScale * charScale;
        charHeight = fontHeight * 2/fontScale * charScale;
    }
    win = SDL_CreateWindow(title.c_str(), SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, width*charWidth+(4 * charScale), height*charHeight+(4 * charScale), SDL_WINDOW_SHOWN | SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_RESIZABLE | SDL_WINDOW_INPUT_FOCUS);
    if (win == nullptr || win == NULL || win == (SDL_Window*)0) {
        overridden = true;
        throw window_exception("Failed to create window");
    }
    id = SDL_GetWindowID(win);
#ifndef __APPLE__
    char * icon_pixels = new char[favicon_width * favicon_height * 4];
    memset(icon_pixels, 0xFF, favicon_width * favicon_height * 4);
    const char * icon_data = header_data;
    for (unsigned i = 0; i < favicon_width * favicon_height; i++) HEADER_PIXEL(icon_data, (&icon_pixels[i*4]));
    SDL_Surface* icon = SDL_CreateRGBSurfaceFrom(icon_pixels, favicon_width, favicon_height, 32, favicon_width * 4, 0x000000ff, 0x0000ff00, 0x00ff0000, 0xff000000);
    SDL_SetWindowIcon(win, icon);
    SDL_FreeSurface(icon);
    delete[] icon_pixels;
#endif
    dpiScale = 1;
    SDL_Surface* old_bmp;
    if (config.customFontPath.empty()) 
        old_bmp = SDL_CreateRGBSurfaceWithFormatFrom((void*)font_image.pixel_data, font_image.width, font_image.height, font_image.bytes_per_pixel * 8, font_image.bytes_per_pixel * font_image.width, SDL_PIXELFORMAT_RGB565);
    else if (config.customFontPath == "hdfont") old_bmp = SDL_LoadBMP((getROMPath() + "/hdfont.bmp").c_str());
    else old_bmp = SDL_LoadBMP(config.customFontPath.c_str());
    if (old_bmp == nullptr || old_bmp == NULL || old_bmp == (SDL_Surface*)0) {
        SDL_DestroyWindow(win);
        overridden = true;
        throw window_exception("Failed to load font");
    }
    bmp = SDL_ConvertSurfaceFormat(old_bmp, SDL_PIXELFORMAT_RGBA32, 0);
    if (bmp == nullptr || bmp == NULL || bmp == (SDL_Surface*)0) {
        SDL_DestroyWindow(win);
        overridden = true;
        throw window_exception("Failed to convert font");
    }
    SDL_FreeSurface(old_bmp);
    SDL_SetColorKey(bmp, SDL_TRUE, SDL_MapRGB(bmp->format, 0, 0, 0));
    renderTargets.push_back(this);
}

TerminalWindow::~TerminalWindow() {
    TerminalWindow::renderTargetsLock.lock();
    std::lock_guard<std::mutex> locked_g(locked);
    for (auto it = renderTargets.begin(); it != renderTargets.end(); it++) {
        if (*it == this)
            it = renderTargets.erase(it);
        if (it == renderTargets.end()) break;
    }
    TerminalWindow::renderTargetsLock.unlock();
    if (!overridden) {
        if (surf != NULL) SDL_FreeSurface(surf);
        SDL_FreeSurface(bmp);
        SDL_DestroyWindow(win);
    }
}

void TerminalWindow::setPalette(Color * p) {
    for (int i = 0; i < 16; i++) palette[i] = p[i];
}

void TerminalWindow::setCharScale(int scale) {
    if (scale < 1) scale = 1;
    charScale = scale;
    charWidth = fontWidth * (2/fontScale) * charScale;
    charHeight = fontHeight * (2/fontScale) * charScale;
    SDL_SetWindowSize(win, width*charWidth+(4 * charScale), height*charHeight+(4 * charScale));
}

bool operator!=(Color lhs, Color rhs) {
    return lhs.r != rhs.r || lhs.g != rhs.g || lhs.b != rhs.b;
}

bool TerminalWindow::drawChar(unsigned char c, int x, int y, Color fg, Color bg, bool transparent) {
    SDL_Rect srcrect = getCharacterRect(c);
    SDL_Rect destrect = {
        x * charWidth * dpiScale + 2 * charScale * 2/fontScale * dpiScale, 
        y * charHeight * dpiScale + 2 * charScale * 2/fontScale * dpiScale, 
        fontWidth * 2/fontScale * charScale * dpiScale, 
        fontHeight * 2/fontScale * charScale * dpiScale
    };
    if (!transparent && bg != palette[15]) {
        if (gotResizeEvent) return false;
        if (SDL_FillRect(surf, &destrect, rgb(bg)) != 0) return false;
    }
    if (c != ' ' && c != '\0') {
        if (gotResizeEvent) return false;
        if (SDL_SetSurfaceColorMod(bmp, fg.r, fg.g, fg.b) != 0) return false;
        if (gotResizeEvent) return false;
        if (SDL_BlitScaled(bmp, &srcrect, surf, &destrect) != 0) return false;
    }
    return true;
}

SDL_Rect * setRect(SDL_Rect * rect, int x, int y, int w, int h) {
    rect->x = x;
    rect->y = y;
    rect->w = w;
    rect->h = h;
    return rect;
}

static unsigned char circlePix[] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 0, 0,
    0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255,
    0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255,
    0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255,
    0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255,
    0, 0, 0, 0, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};

void TerminalWindow::render() {
    std::lock_guard<std::mutex> locked_g(locked);
    if (gotResizeEvent) {
        gotResizeEvent = false;
        this->screen.resize(newHeight);
        if (newHeight > height) std::fill(screen.begin() + height, screen.end(), std::vector<unsigned char>(newWidth, ' '));
        for (unsigned i = 0; i < screen.size(); i++) {
            screen[i].resize(newWidth);
            if (newWidth > width) std::fill(screen[i].begin() + width, screen[i].end(), ' ');
        }
        this->colors.resize(newHeight);
        if (newHeight > height) std::fill(colors.begin() + height, colors.end(), std::vector<unsigned char>(newWidth, 0xF0));
        for (unsigned i = 0; i < colors.size(); i++) {
            colors[i].resize(newWidth);
            if (newWidth > width) std::fill(colors[i].begin() + width, colors[i].end(), 0xF0);
        }
        this->pixels.resize(newHeight * fontHeight);
        if (newHeight > height) std::fill(pixels.begin() + (height * fontHeight), pixels.end(), std::vector<unsigned char>(newWidth * fontWidth, 0x0F));
        for (unsigned i = 0; i < pixels.size(); i++) {
            pixels[i].resize(newWidth * fontWidth);
            if (newWidth > width) std::fill(pixels[i].begin() + (width * fontWidth), pixels[i].end(), 0x0F);
        }
        this->width = newWidth;
        this->height = newHeight;
        changed = true;
    }
    if (!changed && !shouldScreenshot && !shouldRecord) return;
    changed = false;
    int ww = 0, wh = 0;
    SDL_GetWindowSize(win, &ww, &wh);
    if (surf != NULL) SDL_FreeSurface(surf);
    surf = SDL_CreateRGBSurfaceWithFormat(0, ww, wh, 24, SDL_PIXELFORMAT_RGB888);
    if (surf == NULL) {
        printf("Could not allocate renderer: %s\n", SDL_GetError());
        return;
    }
    SDL_Rect rect;
    if (gotResizeEvent || SDL_FillRect(surf, NULL, rgb(defaultPalette[15])) != 0) return;
    if (mode != 0) {
        for (int y = 0; y < height * charHeight; y+=(2/fontScale)*charScale) {
            for (int x = 0; x < width * charWidth; x+=(2/fontScale)*charScale) {
                unsigned char c = pixels[y / (2/fontScale) / charScale][x / (2/fontScale) / charScale];
                if (gotResizeEvent) return;
                if (SDL_FillRect(surf, setRect(&rect, x + (2 * (2/fontScale) * charScale), y + (2 * (2/fontScale) * charScale), (2/fontScale) * charScale, (2/fontScale) * charScale), rgb(palette[(int)c])) != 0) return;
            }
        }
    } else {
        for (int y = 0; y < height; y++) for (int x = 0; x < width; x++) 
            if (gotResizeEvent || !drawChar(screen[y][x], x, y, palette[colors[y][x] & 0x0F], palette[colors[y][x] >> 4])) return;
		if (blinkX >= width) blinkX = width - 1;
		if (blinkY >= height) blinkY = height - 1;
		if (blinkX < 0) blinkX = 0;
		if (blinkY < 0) blinkY = 0;
        if (gotResizeEvent) return;
        if (blink) if (!drawChar('_', blinkX, blinkY, palette[0], palette[colors[blinkY][blinkX] >> 4], true)) return;
    }
    currentFPS++;
    if (lastSecond != time(0)) {
        lastSecond = time(0);
        lastFPS = currentFPS;
        currentFPS = 0;
    }
    if (/*showFPS*/ false) {
        // later
    }
    if (shouldScreenshot) {
        shouldScreenshot = false;
        if (gotResizeEvent) return;
#ifdef PNGPP_PNG_HPP_INCLUDED
        SDL_Surface * temp = SDL_ConvertSurfaceFormat(surf, SDL_PIXELFORMAT_RGB24, 0);
        if (screenshotPath == "clipboard") {
            copyImage(temp);
        } else {
            png::solid_pixel_buffer<png::rgb_pixel> pixbuf(temp->w, temp->h);
            memcpy((void*)&pixbuf.get_bytes()[0], temp->pixels, temp->h * temp->pitch);
            png::image<png::rgb_pixel, png::solid_pixel_buffer<png::rgb_pixel> > img(temp->w, temp->h);
            img.set_pixbuf(pixbuf);
            img.write(screenshotPath);
        }
        SDL_FreeSurface(temp);
#else
        SDL_Surface *conv = SDL_ConvertSurfaceFormat(surf, SDL_PIXELFORMAT_RGB888, 0);
        SDL_SaveBMP(conv, screenshotPath.c_str());
        SDL_FreeSurface(conv);
#endif
    }
    if (shouldRecord) {
        if (recordedFrames >= 150) stopRecording();
        else if (--frameWait < 1) {
            recorderMutex.lock();
            uint32_t uw = static_cast<uint32_t>(surf->w), uh = static_cast<uint32_t>(surf->h);
            std::string rle = std::string((char*)&uw, 4) + std::string((char*)&uh, 4);
            SDL_Surface * temp = SDL_ConvertSurfaceFormat(surf, SDL_PIXELFORMAT_ABGR8888, 0);
            uint32_t * px = ((uint32_t*)temp->pixels);
            uint32_t data = px[0] & 0xFFFFFF;
            for (int y = 0; y < surf->h; y++) {
                for (int x = 0; x < surf->w; x++) {
                    uint32_t p = px[y*surf->w+x];
                    if ((p & 0xFFFFFF) != (data & 0xFFFFFF) || (data & 0xFF000000) == 0xFF000000) {
                        rle += std::string((char*)&data, 4);
                        data = p & 0xFFFFFF;
                    } else data += 0x1000000;
                }
            }
            rle += std::string((char*)&data, 4);
            SDL_FreeSurface(temp);
            recording.push_back(rle);
            recordedFrames++;
            frameWait = config.clockSpeed / 10;
            recorderMutex.unlock();if (gotResizeEvent) return;
        }
        SDL_Surface* circle = SDL_CreateRGBSurfaceWithFormatFrom(circlePix, 10, 10, 32, 40, SDL_PIXELFORMAT_BGRA32);
        if (circle == NULL) { printf("Error: %s\n", SDL_GetError()); assert(false); }
        if (gotResizeEvent) return;
        if (SDL_BlitSurface(circle, NULL, surf, setRect(&rect, (width * charWidth * dpiScale + 2 * charScale * (2/fontScale) * dpiScale) - 10, 2 * charScale * (2/fontScale) * dpiScale, 10, 10)) != 0) return;
        SDL_FreeSurface(circle);
    }
    /*if (gotResizeEvent) return;
#ifdef __linux__
    queueTask([ ](void* arg)->void*{SDL_UpdateWindowSurface((SDL_Window*)arg); return NULL;}, win);
#else
    if (SDL_UpdateWindowSurface(win) != 0) {
        printf("Error rendering: %s\n", SDL_GetError());
        surf = SDL_GetWindowSurface(win);
    }
#endif*/
}

void convert_to_renderer_coordinates(SDL_Renderer *renderer, int *x, int *y) {
    SDL_Rect viewport;
    float scale_x, scale_y;
    SDL_RenderGetViewport(renderer, &viewport);
    SDL_RenderGetScale(renderer, &scale_x, &scale_y);
    *x = (int) (*x / scale_x) - viewport.x;
    *y = (int) (*y / scale_y) - viewport.y;
}

void TerminalWindow::getMouse(int *x, int *y) {
    SDL_GetMouseState(x, y);
    //convert_to_renderer_coordinates(ren, x, y);
}

SDL_Rect TerminalWindow::getCharacterRect(unsigned char c) {
    SDL_Rect retval;
    retval.w = fontWidth * 2/fontScale;
    retval.h = fontHeight * 2/fontScale;
    retval.x = ((fontWidth + 2) * 2/fontScale)*(c & 0x0F)+2/fontScale;
    retval.y = ((fontHeight + 2) * 2/fontScale)*(c >> 4)+2/fontScale;
    return retval;
}

bool TerminalWindow::resize(int w, int h) {
    newWidth = (w - 4*(2/fontScale)*charScale) / charWidth;
    newHeight = (h - 4*(2/fontScale)*charScale) / charHeight;
    gotResizeEvent = (newWidth != width || newHeight != height);
    if (!gotResizeEvent) return false;
    while (gotResizeEvent) std::this_thread::yield();
    return true;
}

void TerminalWindow::screenshot(std::string path) {
    shouldScreenshot = true;
    if (path != "") screenshotPath = path;
    else {
        time_t now = time(0);
        struct tm * nowt = localtime(&now);
        screenshotPath = getBasePath();
#ifdef WIN32
        screenshotPath += "\\screenshots\\";
#else
        screenshotPath += "/screenshots/";
#endif
        createDirectory(screenshotPath.c_str());
        char * tstr = new char[24];
        strftime(tstr, 24, "%F_%H.%M.%S", nowt);
#ifdef NO_PNG
        screenshotPath += std::string(tstr) + ".bmp";
#else
        screenshotPath += std::string(tstr) + ".png";
#endif
        delete[] tstr;
    }
}

void TerminalWindow::record(std::string path) {
    shouldRecord = true;
    recordedFrames = 0;
    frameWait = 0;
    if (path != "") recordingPath = path;
    else {
        time_t now = time(0);
        struct tm * nowt = localtime(&now);
        recordingPath = getBasePath();
#ifdef WIN32
        recordingPath += "\\screenshots\\";
#else
        recordingPath += "/screenshots/";
#endif
        createDirectory(recordingPath.c_str());
        char * tstr = new char[20];
        strftime(tstr, 24, "%F_%H.%M.%S", nowt);
        recordingPath += std::string(tstr) + ".gif";
        delete[] tstr;
    }
}

uint32_t *memset_int(uint32_t *ptr, uint32_t value, size_t num) {
    for (size_t i = 0; i < num; i++) memcpy(&ptr[i], &value, 4);
    return &ptr[num];
}

void TerminalWindow::stopRecording() {
    shouldRecord = false;
    recorderMutex.lock();
    if (recording.size() < 1) { recorderMutex.unlock(); return; }
    GifWriter g;
    GifBegin(&g, recordingPath.c_str(), ((uint32_t*)(&recording[0][0]))[0], ((uint32_t*)(&recording[0][0]))[1], 10);
    for (std::string s : recording) {
        uint32_t w = ((uint32_t*)&s[0])[0], h = ((uint32_t*)&s[0])[1];
        uint32_t* ipixels = new uint32_t[w * h];
        uint32_t* lp = ipixels;
        for (unsigned i = 2; i*4 < s.size(); i++) {
            uint32_t c = ((uint32_t*)&s[0])[i];
            lp = memset_int(lp, c & 0xFFFFFF, ((c & 0xFF000000) >> 24) + 1);
        }
        GifWriteFrame(&g, (uint8_t*)ipixels, w, h, 10);
        delete[] ipixels;
    }
    GifEnd(&g);
    recording.clear();
    recorderMutex.unlock();
}

void TerminalWindow::showMessage(Uint32 flags, const char * title, const char * message) {SDL_ShowSimpleMessageBox(flags, title, message, win);}

void TerminalWindow::toggleFullscreen() {
    fullscreen = !fullscreen;
    if (fullscreen) queueTask([ ](void* param)->void*{SDL_SetWindowFullscreen((SDL_Window*)param, SDL_WINDOW_FULLSCREEN_DESKTOP); return NULL;}, win);
    else queueTask([ ](void* param)->void*{SDL_SetWindowFullscreen((SDL_Window*)param, 0); return NULL;}, win);
}