/*
 * peripheral/speaker.cpp
 * CraftOS-PC 2
 * 
 * This file implements the methods for the speaker peripheral.
 * 
 * This code is licensed under the MIT license.
 * Copyright (c) 2019-2021 JackMacWindows.
 */

#ifndef NO_MIXER
#include <cmath>
#include <algorithm>
#include <fstream>
#include <random>
#include <configuration.hpp>
#include <dirent.h>
#include <SDL2/SDL_mixer.h>
#include <sys/stat.h>
#include "../platform.hpp"
#include "../runtime.hpp"
#include "speaker.hpp"

#ifndef MIX_INIT_MID
#define MIX_INIT_MID 0
#endif

#ifdef __INTELLISENSE__
#pragma region
#endif
/*
 * sound_pitching_example.cpp
 * Original source available at https://gist.github.com/hydren/f60d107f144fcb41dd6f898b126e17b2
 *
 *  Created on: 27 de dez de 2017
 *      Author: Carlos Faruolo
 */

namespace AudioSpec
{
    // read-only!
    Uint16 format = AUDIO_S16;  // current audio format constant
    int frequency = 44100;  // frequency rate of the current audio format
    int channelCount = 2;  // number of channels of the current audio format
}

// this could be a macro as well
static inline Uint16 formatSampleSize(Uint16 format)
{
    return (format & 0xFF) / 8;
}

// Get chunk time length (in ms) given its size and current audio format
static int computeChunkLengthMillisec(int chunkSize)
{
    /* bytes / samplesize == sample points */
    const Uint32 points = chunkSize / formatSampleSize(AudioSpec::format);

    /* sample points / channels == sample frames */
    const Uint32 frames = (points / AudioSpec::channelCount);

    /* (sample frames * 1000) / frequency == play length, in ms */
    return ((frames * 1000) / AudioSpec::frequency);
}

// Custom handler object to control which part of the Mix_Chunk's audio data will be played, with which pitch-related modifications.
// This needed to be a template because the actual Mix_Chunk's data format may vary (AUDIO_U8, AUDIO_S16, etc) and the data type varies with it (Uint8, Sint16, etc)
// The AudioFormatType should be the data type that is compatible with the current SDL_mixer-initialized audio format.
template<typename AudioFormatType>
struct CustomSdlMixerPlaybackSpeedEffectHandler
{
    float speed;
    Mix_Chunk* chunk;
    int position;  // current position of the sound, in ms
    bool touched;  // false if this playback has never been pitched.

    // read-only!
    bool loop;
    int duration;  // the duration of the sound, in ms
    int chunkSize;  // the size of the sound, as a number of indexes (or sample points). thinks of this as a array size when using the proper array type (instead of just Uint8*).

    CustomSdlMixerPlaybackSpeedEffectHandler(float& speed, Mix_Chunk* chunk, bool loop)
    : speed(speed), chunk(chunk),
      position(0), touched(false), loop(loop), duration(0), chunkSize(0)
    {
        duration = computeChunkLengthMillisec(chunk->alen);
        chunkSize = chunk->alen / formatSampleSize(AudioSpec::format);
    }

    ~CustomSdlMixerPlaybackSpeedEffectHandler() {
        Mix_FreeChunk(chunk);
    }

    // processing function to be able to change chunk speed/pitch.
    void modifyStreamPlaybackSpeed(int mixChannel, void* stream, int length)
    {
        const float speedFactor = speed;
        const int channelCount = AudioSpec::channelCount, frequency = AudioSpec::frequency;

        const AudioFormatType* chunkData = reinterpret_cast<AudioFormatType*>(chunk->abuf);

        AudioFormatType* buffer = static_cast<AudioFormatType*>(stream);
        const int bufferSize = length / sizeof(AudioFormatType);  // buffer size (as array)
        const int bufferDuration = computeChunkLengthMillisec(length);  // buffer time duration

        if(!touched)  // if playback is still untouched
        {
            // if playback is still untouched and no pitch is requested this time, skip pitch routine and leave stream untouched.
            if(speedFactor == 1.0f)
            {
                // if there is still sound to be played
                if(position < duration || loop)
                {
                    // just update position
                    position += bufferDuration;

                    // reset position if looping
                    if(loop) while(position > duration)
                        position -= duration;
                }
                else  // if we already played the whole sound, halt channel
                {
                    // set silence on the buffer since Mix_HaltChannel() poops out some of it for a few ms.
                    for(int i = 0; i < bufferSize; i++)
                        buffer[i] = 0;

                    Mix_HaltChannel(mixChannel);
                }

                return;  // skipping pitch routine
            }
            // if pitch is required for the first time
            else
                touched = true;  // mark as touched and proceed to the pitch routine.
        }

        // if there is still sound to be played
        if(position < duration || loop)
        {
            const float delta = 1000.0/frequency,   // normal duration of each sample
                        delta2 = delta*speedFactor; // virtual stretched duration, scaled by 'speedFactor'

            for(int i = 0; i < bufferSize; i += channelCount)
            {
                const int j = i/channelCount; // j goes from 0 to size/channelCount, incremented 1 by 1
                const float x = position + j*delta2;  // get "virtual" index. its corresponding value will be interpolated.
                const int k = floor(x / delta);  // get left index to interpolate from original chunk data (right index will be this plus 1)
                const float proportion = (x / delta) - k;  // get the proportion of the right value (left will be 1.0 minus this)

                // usually just 2 channels: 0 (left) and 1 (right), but who knows...
                for(int c = 0; c < channelCount; c++)
                {
                    // check if k will be within bounds
                    if(k*channelCount + channelCount - 1 < chunkSize || loop)
                    {
                        AudioFormatType  leftValue =  chunkData[(  k   * channelCount + c) % chunkSize],
                                         rightValue = chunkData[((k+1) * channelCount + c) % chunkSize];

                        // put interpolated value on 'data' (linear interpolation)
                        buffer[i + c] = (1-proportion)*leftValue + proportion*rightValue;
                    }
                    else  // if k will be out of bounds (chunk bounds), it means we already finished; thus, we'll pass silence
                    {
                        buffer[i + c] = 0;
                    }
                }
            }

            // update position
            position += bufferDuration * speedFactor; // this is not exact since a frame may play less than its duration when finished playing, but its simpler

            // reset position if looping
            if(loop) while(position > duration)
                position -= duration;
        }

        else  // if we already played the whole sound but finished earlier than expected by SDL_mixer (due to faster playback speed)
        {
            // set silence on the buffer since Mix_HaltChannel() poops out some of it for a few ms.
            for(int i = 0; i < bufferSize; i++)
                buffer[i] = 0;

            Mix_HaltChannel(mixChannel);
        }
    }

    // a shorter name to improve readability
    typedef CustomSdlMixerPlaybackSpeedEffectHandler Handler;

    // Mix_EffectFunc_t callback that redirects to handler method (handler passed via userData)
    static void mixEffectFuncCallback(int channel, void* stream, int length, void* userData)
    {
        static_cast<Handler*>(userData)->modifyStreamPlaybackSpeed(channel, stream, length);
    }

    // Mix_EffectDone_t callback that deletes the handler at the end of the effect usage  (handler passed via userData)
    static void mixEffectDoneCallback(int channel, void *userData)
    {
        delete static_cast<Handler*>(userData);
    }

    // function to register a handler to this channel for the next playback.
    static void registerOnChannel(int channel, float& speed, Mix_Chunk* chunk, bool loop)
    {
        if (!Mix_RegisterEffect(channel, Handler::mixEffectFuncCallback, Handler::mixEffectDoneCallback, new Handler(speed, chunk, loop)))
            fprintf(stderr, "Could not register effect: %s\n", Mix_GetError());
    }
};

// register proper effect handler according to the current audio format. effect valid for the next playback only.
void setupForNextPlayback(float& speed, Mix_Chunk* chunk, int channel, bool loop)
{
    // select the register function for the current audio format and register the effect using the compatible handlers
    // xxx is it correct to behave the same way to all S16 and U16 formats? Should we create case statements for AUDIO_S16SYS, AUDIO_S16LSB, AUDIO_S16MSB, etc, individually?
    switch(AudioSpec::format)
    {
        case AUDIO_U8:  CustomSdlMixerPlaybackSpeedEffectHandler<Uint8 >::registerOnChannel(channel, speed, chunk, loop); break;
        case AUDIO_S8:  CustomSdlMixerPlaybackSpeedEffectHandler<Sint8 >::registerOnChannel(channel, speed, chunk, loop); break;
        case AUDIO_U16: CustomSdlMixerPlaybackSpeedEffectHandler<Uint16>::registerOnChannel(channel, speed, chunk, loop); break;
        default:
        case AUDIO_S16: CustomSdlMixerPlaybackSpeedEffectHandler<Sint16>::registerOnChannel(channel, speed, chunk, loop); break;
        case AUDIO_S32: CustomSdlMixerPlaybackSpeedEffectHandler<Sint32>::registerOnChannel(channel, speed, chunk, loop); break;
        case AUDIO_F32: CustomSdlMixerPlaybackSpeedEffectHandler<float >::registerOnChannel(channel, speed, chunk, loop); break;
    }
}

#ifdef __INTELLISENSE__
#pragma endregion
#endif

struct sound_file_t {
    std::string name;
    float volume = 1.0;
    float pitch = 1.0;
    int weight = 1;
    bool isEvent = false;
    bool isMusic = false;
};

extern std::unordered_map<std::string, std::pair<unsigned char *, unsigned int> > speaker_sounds;
static std::unordered_map<std::string, std::vector<sound_file_t> > soundEvents;
static std::mt19937 RNG;

/* Adding custom sounds:
 * Custom sounds can be added with this folder structure:
 * <ROM root dir>
 * - rom/, bios.lua, etc.
 * - sounds/
 *   - <namespace, e.g. minecraft>/
 *     - sounds.json
 *     - sounds/
 *       - <sound files/folders as described in sounds.json>
 * 
 * sounds.json uses the same format as Minecraft, meaning that MC assets can be
 * copied directly into the namespace folder. See https://minecraft.gamepedia.com/Sounds.json
 * for more info.
 */

static Mix_Music * currentlyPlayingMusic = NULL;
static speaker * musicSpeaker = NULL;

static void musicFinished() { if (currentlyPlayingMusic != NULL) { Mix_FreeMusic(currentlyPlayingMusic); currentlyPlayingMusic = NULL; musicSpeaker = NULL; } }
static void channelFinished(int c) { Mix_FreeChunk(Mix_GetChunk(c)); }

static bool playSoundEvent(std::string name, float volume, float speed, unsigned int channel) {
    if (name.find(':') == std::string::npos) name = "minecraft:" + name;
    if (soundEvents.find(name) == soundEvents.end()) return false;
    unsigned randMax = 0;
    for (const sound_file_t& f : soundEvents[name]) randMax += f.weight;
    const unsigned num = std::uniform_int_distribution<unsigned>(0, randMax-1)(RNG);
    unsigned i = 0;
    for (const sound_file_t& f : soundEvents[name]) {
        if ((i += f.pitch) > num) {
            // play this event
            if (f.isEvent) return playSoundEvent(f.name, min(volume * f.volume, 3.0f), min(speed * f.pitch, 2.0f), channel);
#ifdef WIN32
            std::string path(astr(getROMPath() + WS("\\sounds\\") + wstr(f.name.find(':') == std::string::npos ? name.substr(0, name.find(':')) : f.name.substr(0, f.name.find(':'))) + WS("\\sounds\\") + wstr(f.name.find(':') == std::string::npos ? f.name : f.name.substr(f.name.find(':') + 1))));
            for (char& c : path) if (c == '/') c = '\\';
#else
            std::string path(astr(getROMPath() + WS("/sounds/") + wstr(f.name.find(":") == std::string::npos ? name.substr(0, name.find(":")) : f.name.substr(0, f.name.find(":"))) + WS("/sounds/") + wstr(f.name.find(":") == std::string::npos ? f.name : f.name.substr(f.name.find(":") + 1))));
#endif
            if (f.isMusic) {
                Mix_Music * chunk = Mix_LoadMUS((path + ".ogg").c_str());
                if (chunk == NULL) {
                    chunk = Mix_LoadMUS((path + ".mp3").c_str());
                    if (chunk == NULL) {
                        chunk = Mix_LoadMUS((path + ".flac").c_str());
                        if (chunk == NULL) {
                            chunk = Mix_LoadMUS((path + ".wav").c_str());
                            if (chunk == NULL) {
                                chunk = Mix_LoadMUS((path + ".mid").c_str());
                                if (chunk == NULL) return false;
                            }
                        }
                    }
                }
                if (Mix_PlayingMusic()) Mix_HaltMusic();
                Mix_VolumeMusic((int)(min(volume * f.volume, 3.0f) * (MIX_MAX_VOLUME / 3.0f)));
                currentlyPlayingMusic = chunk;
                Mix_PlayMusic(chunk, 0);
                Mix_HookMusicFinished(musicFinished);
                return true;
            } else {
                Mix_Chunk * chunk = Mix_LoadWAV((path + ".ogg").c_str());
                if (chunk == NULL) {
                    chunk = Mix_LoadWAV((path + ".mp3").c_str());
                    if (chunk == NULL) {
                        chunk = Mix_LoadWAV((path + ".flac").c_str());
                        if (chunk == NULL) {
                            chunk = Mix_LoadWAV((path + ".wav").c_str());
                            if (chunk == NULL) {
                                chunk = Mix_LoadWAV((path + ".mid").c_str());
                                if (chunk == NULL) return false;
                            }
                        }
                    }
                }
                CustomSdlMixerPlaybackSpeedEffectHandler<Sint16> handler(speed, chunk, false); // automatically frees chunk on scope exit
                void * data = SDL_malloc(max(chunk->alen, (Uint32)ceil((double)chunk->alen / (double)speed)));
                memcpy(data, chunk->abuf, chunk->alen);
                handler.modifyStreamPlaybackSpeed(0, data, max(chunk->alen, (Uint32)ceil((double)chunk->alen / (double)speed)));
                Mix_Chunk * newchunk = (Mix_Chunk*)SDL_malloc(sizeof(Mix_Chunk));
                newchunk->abuf = (Uint8*)data;
                newchunk->alen = (Uint32)((float)chunk->alen / speed);
                newchunk->allocated = true;
                newchunk->volume = (int)(min(volume * f.volume, 3.0f) * (MIX_MAX_VOLUME / 3.0f));
                if (Mix_PlayChannel(channel, newchunk, 0) == -1) return false;
                Mix_ChannelFinished(channelFinished);
                return true;
            }
        }
    }
    return false;
}

int speaker::playNote(lua_State *L) {
    lastCFunction = __func__;
    const std::string inst = luaL_checkstring(L, 1);
    const float volume = (float)luaL_optnumber(L, 2, 1.0);
    const int pitch = (int)luaL_optnumber(L, 3, 1.0);
    if (volume < 0.0f || volume > 3.0f) luaL_error(L, "invalid volume %f", volume);
    if (pitch < 0 || pitch > 24) luaL_error(L, "invalid pitch %d", pitch);
    if (speaker_sounds.find(inst) == speaker_sounds.end()) luaL_error(L, "invalid instrument %s", inst.c_str());
    if (std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now() - lastTickReset).count() >= 50) {
        lastTickReset = std::chrono::system_clock::now();
        noteCount = 0;
    }
    if (noteCount >= (unsigned)config.maxNotesPerTick) {
        lua_pushboolean(L, false);
        return 1;
    }
    noteCount++;
    int channel;
    for (channel = Mix_GroupAvailable(channelGroup); channel == -1; channel = Mix_GroupAvailable(channelGroup)) {
        int next = Mix_GroupAvailable(0);
        if (next == -1) next = Mix_AllocateChannels(Mix_AllocateChannels(-1) + 1) - 1;
        Mix_GroupChannel(next, channelGroup);
    }
    if (soundEvents.find("minecraft:block.note_block." + inst) != soundEvents.end()) {
        lua_pushboolean(L, playSoundEvent("minecraft:block.note_block." + inst, volume, (float)pow(2.0, (pitch - 12.0) / 12.0), channel));
    } else if (soundEvents.find("minecraft:block.note." + inst) != soundEvents.end()) {
        lua_pushboolean(L, playSoundEvent("minecraft:block.note." + inst, volume, (float)pow(2.0, (pitch - 12.0) / 12.0), channel));
    } else {
        Mix_Chunk * chunk = Mix_LoadWAV_RW(SDL_RWFromConstMem(speaker_sounds[inst].first, speaker_sounds[inst].second), true);
        if (chunk == NULL) luaL_error(L, "Fatal error while reading instrument sample");
        float speed = (float)pow(2.0, (pitch - 12.0) / 12.0);
        CustomSdlMixerPlaybackSpeedEffectHandler<Sint16> handler(speed, chunk, false);
        void * data = SDL_malloc(max(chunk->alen, (Uint32)ceil((float)chunk->alen / speed)));
        memset(data, 0, max(chunk->alen, (Uint32)ceil((float)chunk->alen / speed)));
        memcpy(data, chunk->abuf, chunk->alen);
        handler.modifyStreamPlaybackSpeed(0, data, chunk->alen);
        Mix_Chunk * newchunk = (Mix_Chunk*)SDL_malloc(sizeof(Mix_Chunk));
        newchunk->abuf = (Uint8*)data;
        newchunk->alen = (Uint32)((float)chunk->alen / speed);
        newchunk->allocated = true;
        newchunk->volume = (Uint8)(volume * (MIX_MAX_VOLUME / 3.0f));
        if (Mix_PlayChannel(channel, newchunk, 0) == -1) {
            lua_pushboolean(L, false);
            return 1;
        }
        Mix_ChannelFinished(channelFinished);
        lua_pushboolean(L, true);
    }
    if (lua_toboolean(L, -1)) { lua_pushinteger(L, channel); return 2; }
    return 1;
}

int speaker::playSound(lua_State *L) {
    lastCFunction = __func__;
#ifdef STANDALONE_ROM
    luaL_error(L, "Sounds are not available on standalone builds");
    return 0;
#else
    const std::string inst = luaL_checkstring(L, 1);
    const float volume = (float)luaL_optnumber(L, 2, 1.0);
    const float speed = (float)luaL_optnumber(L, 3, 1.0);
    if (volume < 0.0f || volume > 3.0f) luaL_error(L, "invalid volume %f", volume);
    if (speed < 0.0f || speed > 2.0f) luaL_error(L, "invalid speed %f", speed);
    if (std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now() - lastTickReset).count() >= 50) {
        lastTickReset = std::chrono::system_clock::now();
        noteCount = 0;
    }
    if (noteCount != 0) {
        lua_pushboolean(L, false);
        return 1;
    }
    noteCount = UINT_MAX;
    int channel;
    for (channel = Mix_GroupAvailable(channelGroup); channel == -1; channel = Mix_GroupAvailable(channelGroup)) {
        int next = Mix_GroupAvailable(0);
        if (next == -1) next = Mix_AllocateChannels(Mix_AllocateChannels(-1) + 1) - 1;
        Mix_GroupChannel(next, channelGroup);
    }
    lua_pushboolean(L, playSoundEvent(inst, volume, speed, channel));
    lua_pushinteger(L, channel);
    return 2;
#endif
}

int speaker::listSounds(lua_State *L) {
    lastCFunction = __func__;
    lua_newtable(L);
    for (const auto& ev : soundEvents) {
        std::vector<std::string> parts = split(ev.first.substr(ev.first.find(':') + 1), '.');
        std::string back = parts.back();
        parts.pop_back();
        lua_pushstring(L, ev.first.substr(0, ev.first.find(':')).c_str());
        lua_gettable(L, -2);
        if (!lua_istable(L, -1)) {
            lua_pop(L, 1);
            lua_newtable(L);
            lua_pushstring(L, ev.first.substr(0, ev.first.find(':')).c_str());
            lua_pushvalue(L, -2);
            lua_settable(L, -4);
        }
        for (const std::string& p : parts) {
            lua_pushstring(L, p.c_str());
            lua_gettable(L, -2);
            if (!lua_istable(L, -1)) {
                lua_pop(L, 1);
                lua_newtable(L);
                lua_pushstring(L, p.c_str());
                lua_pushvalue(L, -2);
                lua_settable(L, -4);
            }
        }
        lua_pushstring(L, back.c_str());
        lua_pushstring(L, ev.first.c_str());
        lua_settable(L, -3);
        lua_pop(L, parts.size() + 1);
    }
    return 1;
}

int speaker::playLocalMusic(lua_State *L) {
    lastCFunction = __func__;
    const path_t path = fixpath(get_comp(L), luaL_checkstring(L, 1), true);
    const float volume = (float)luaL_optnumber(L, 2, 1.0);
    if (path.empty()) luaL_error(L, "%s: File does not exist", lua_tostring(L, 1));
    if (volume < 0.0f || volume > 3.0f) luaL_error(L, "invalid volume %f", volume);
    Mix_Music * mus = Mix_LoadMUS(astr(path).c_str());
    if (mus == NULL) luaL_error(L, "%s: Could not load music file: %s", lua_tostring(L, 1), Mix_GetError());
    if (Mix_PlayingMusic()) Mix_HaltMusic();
    Mix_VolumeMusic((Uint8)(volume * (MIX_MAX_VOLUME / 3.0f)));
    currentlyPlayingMusic = mus;
    musicSpeaker = this;
    Mix_PlayMusic(mus, 0);
    Mix_HookMusicFinished(musicFinished);
    return 0;
}

int speaker::setSoundFont(lua_State *L) {
    lastCFunction = __func__;
    Mix_SetSoundFonts(astr(fixpath(get_comp(L), luaL_checkstring(L, 1), true)).c_str());
    return 0;
}

int speaker::stopSounds(lua_State *L) {
    lastCFunction = __func__;
    if (lua_isnumber(L, 1)) Mix_HaltChannel((int)lua_tointeger(L, 1));
    else {
        if (musicSpeaker == this) { Mix_HaltMusic(); musicSpeaker = NULL; }
        Mix_HaltGroup(channelGroup);
    }
    return 0;
}

speaker::speaker(lua_State *L, const char * side) {
    RNG.seed((unsigned)time(0)); // doing this here so the seed can be refreshed
    channelGroup = nextChannelGroup++;
}

speaker::~speaker() {
    if (musicSpeaker == this) { Mix_HaltMusic(); musicSpeaker = NULL; }
    Mix_HaltGroup(channelGroup);
    for (int channel = Mix_GroupAvailable(channelGroup); channel != -1; channel = Mix_GroupAvailable(channelGroup))
        Mix_GroupChannel(channel, 0);
}

int speaker::call(lua_State *L, const char * method) {
    const std::string m(method);
    if (m == "playNote") return playNote(L);
    else if (m == "playSound") return playSound(L);
    else if (m == "listSounds") return listSounds(L);
    else if (m == "playLocalMusic") return playLocalMusic(L);
    else if (m == "setSoundFont") return setSoundFont(L);
    else return 0;
}

#define MIXER_FORMATS (MIX_INIT_FLAC | MIX_INIT_MP3 | MIX_INIT_OGG | MIX_INIT_MID)
void speakerInit() {
    int loadedFormats = Mix_Init(MIXER_FORMATS);
    if ((loadedFormats & MIXER_FORMATS) != MIXER_FORMATS) {
        fprintf(stderr, "Missing audio support for: ");
        if (!(loadedFormats & MIX_INIT_FLAC)) fprintf(stderr, "flac ");
        if (!(loadedFormats & MIX_INIT_MP3)) fprintf(stderr, "mp3 ");
        if (!(loadedFormats & MIX_INIT_OGG)) fprintf(stderr, "ogg ");
        if (!(loadedFormats & MIX_INIT_MID)) fprintf(stderr, "mid ");
        fprintf(stderr, "\n");
    }
    Mix_GroupChannels(0, Mix_AllocateChannels(config.maxNotesPerTick)-1, 0);
#ifndef STANDALONE_ROM
    platform_DIR * d = platform_opendir((getROMPath() + WS("/sounds")).c_str());
    if (d) {
        struct_dirent *dir;
        for (int i = 0; (dir = platform_readdir(d)) != NULL; i++) {
            if (path_t(dir->d_name) == WS(".") || path_t(dir->d_name) == WS("..")) continue;
            struct_stat st;
            if (platform_stat((getROMPath() + WS("/sounds/") + dir->d_name).c_str(), &st) != 0 ||
                !S_ISDIR(st.st_mode) || 
                platform_stat((getROMPath() + WS("/sounds/") + dir->d_name + WS("/sounds.json")).c_str(), &st) != 0)
                continue;
            std::ifstream in(getROMPath() + WS("/sounds/") + dir->d_name + WS("/sounds.json"));
            if (!in.is_open()) continue;
            Value root;
            Poco::JSON::Object::Ptr p = root.parse(in);
            in.close();
            try {
                for (const auto& p1 : root) {
                    std::string eventName = astr(path_t(dir->d_name)) + ":" + p1.first;
                    std::vector<sound_file_t> items;
                    for (const auto& pp : *p1.second.extract<Poco::JSON::Object::Ptr>()->get("sounds").extract<Poco::JSON::Array::Ptr>()) {
                        Value obj2(pp);
                        sound_file_t item;
                        if (obj2.isString()) {
                            item.name = obj2.asString();
                        } else {
                            obj2 = Value(*pp.extract<Poco::JSON::Object::Ptr>());
                            item.name = obj2["name"].asString();
                            if (obj2.isMember("volume")) item.volume = obj2["volume"].asFloat();
                            if (obj2.isMember("pitch")) item.pitch = obj2["pitch"].asFloat();
                            if (obj2.isMember("weight")) item.weight = obj2["weight"].asInt();
                            if (obj2.isMember("type")) item.isEvent = obj2["type"].asString() == "event";
                            if (obj2.isMember("stream")) item.isMusic = obj2["stream"].asBool();
                        }
                        items.push_back(item);
                    }
                    Value obj(*p1.second.extract<Poco::JSON::Object::Ptr>());
                    if (soundEvents.find(eventName) == soundEvents.end() || (obj.isMember("replace") && obj["replace"].isBoolean() && obj["replace"].asBool())) 
                        soundEvents[eventName] = items;
                    else for (const sound_file_t& f : items) soundEvents[eventName].push_back(f);
                }
            } catch (Poco::BadCastException &e) {
                fprintf(stderr, "An error occurred while parsing the sounds.json file: %s\n", e.displayText().c_str());
            }
        }
        platform_closedir(d);
    }
#endif
}

void speakerQuit() {
    Mix_HaltChannel(-1); // automatically frees chunks
}

static luaL_Reg speaker_reg[] = {
    {"playNote", NULL},
    {"playSound", NULL},
    {"listSounds", NULL},
    {"playLocalMusic", NULL},
    {"setSoundFont", NULL},
    {"stopSounds", NULL},
    {NULL, NULL}
};

library_t speaker::methods = {"speaker", speaker_reg, nullptr, nullptr};
int speaker::nextChannelGroup = 1;

#endif