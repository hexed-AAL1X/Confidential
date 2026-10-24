#include "SpritePack.hpp"

#include <cstdio>
#include <cstring>

static std::string join_path(const std::string &dir, const char *file)
{
    if (!dir.empty() && dir.back() != '/' && dir.back() != '\\') {
        return dir + "/" + file;
    }
    return dir + file;
}

static void parse_action(const char *value, SpriteAction *action)
{
    char name[64] = "";
    int frames = 1;
    int fps = 10;
    int loop = 1;
    std::sscanf(value, "%63s frames=%d fps=%d loop=%d", name, &frames, &fps, &loop);
    action->name = name;
    action->frames = SDL_max(1, frames);
    action->fps = SDL_clamp(fps, 1, 60);
    action->loop = loop != 0;
}

bool SpritePack_LoadSheet(SpritePack *pack, int index)
{
    if (index < 0 || index >= (int)pack->actions.size()) {
        return false;
    }
    SpriteAction &action = pack->actions[index];
    if (action.sheet) {
        return true;
    }
    const std::string png = join_path(pack->dir, (action.name + ".png").c_str());
    SDL_Surface *loaded = SDL_LoadPNG(png.c_str());
    if (!loaded) {
        SDL_Log("Sprite pack %s: couldn't load %s", pack->dir.c_str(), png.c_str());
        return false;
    }
    action.sheet = SDL_CreateSurface(action.frames * pack->cell_w, SPRITE_DIRS * pack->cell_h,
                                     SDL_PIXELFORMAT_ARGB8888);
    if (!action.sheet) {
        SDL_DestroySurface(loaded);
        return false;
    }
    SDL_ClearSurface(action.sheet, 0, 0, 0, 0);
    SDL_SetSurfaceBlendMode(loaded, SDL_BLENDMODE_NONE);
    SDL_BlitSurface(loaded, nullptr, action.sheet, nullptr);
    SDL_DestroySurface(loaded);
    return true;
}

void SpritePack_FreeSheet(SpritePack *pack, int index)
{
    if (index >= 0 && index < (int)pack->actions.size() && pack->actions[index].sheet) {
        SDL_DestroySurface(pack->actions[index].sheet);
        pack->actions[index].sheet = nullptr;
    }
}

bool SpritePack_Load(SpritePack *pack, const char *dir, bool load_sheets)
{
    SpritePack_Free(pack);
    pack->dir = dir;

    size_t size = 0;
    char *ini = (char *)SDL_LoadFile(join_path(pack->dir, "character.ini").c_str(), &size);
    if (!ini) {
        return false;
    }
    for (char *line = ini; line && *line;) {
        char *next = std::strchr(line, '\n');
        if (next) {
            *next++ = '\0';
        }
        char *eq = std::strchr(line, '=');
        if (eq) {
            *eq = '\0';
            const char *key = line;
            const char *value = eq + 1;
            if (std::strcmp(key, "name") == 0) {
                pack->name = value;
            } else if (std::strcmp(key, "role") == 0) {
                pack->role = value;
            } else if (std::strcmp(key, "source") == 0) {
                pack->source = value;
            } else if (std::strcmp(key, "cell") == 0) {
                std::sscanf(value, "%dx%d", &pack->cell_w, &pack->cell_h);
            } else if (std::strcmp(key, "feet") == 0) {
                pack->feet = SDL_atoi(value);
            } else if (std::strcmp(key, "scale") == 0) {
                pack->scale = SDL_max(1, SDL_atoi(value));
            } else if (std::strcmp(key, "action") == 0) {
                SpriteAction action;
                parse_action(value, &action);
                if (!action.name.empty()) {
                    pack->actions.push_back(action);
                }
            }
        }
        line = next;
    }
    SDL_free(ini);

    if (pack->cell_w <= 0 || pack->cell_h <= 0) {
        SDL_Log("Sprite pack %s: missing cell size", dir);
        SpritePack_Free(pack);
        return false;
    }

    if (load_sheets) {
        for (int i = 0; i < (int)pack->actions.size(); ++i) {
            SpritePack_LoadSheet(pack, i);
        }
    }
    return true;
}

bool SpritePack_Save(const SpritePack *pack)
{
    std::string ini;
    char line[160];
    ini += "name=" + pack->name + "\n";
    ini += "role=" + pack->role + "\n";
    if (!pack->source.empty()) {
        ini += "source=" + pack->source + "\n";
    }
    std::snprintf(line, sizeof(line), "cell=%dx%d\nfeet=%d\nscale=%d\n",
                  pack->cell_w, pack->cell_h, pack->feet, pack->scale);
    ini += line;
    bool ok = true;
    for (const SpriteAction &action : pack->actions) {
        std::snprintf(line, sizeof(line), "action=%s frames=%d fps=%d loop=%d\n",
                      action.name.c_str(), action.frames, action.fps, action.loop ? 1 : 0);
        ini += line;
        if (action.sheet) {
            const std::string png = join_path(pack->dir, (action.name + ".png").c_str());
            ok = SDL_SavePNG(action.sheet, png.c_str()) && ok;
        }
    }
    const std::string ini_path = join_path(pack->dir, "character.ini");
    return SDL_SaveFile(ini_path.c_str(), ini.data(), ini.size()) && ok;
}

void SpritePack_Free(SpritePack *pack)
{
    for (SpriteAction &action : pack->actions) {
        if (action.sheet) {
            SDL_DestroySurface(action.sheet);
        }
    }
    *pack = SpritePack();
}

int SpritePack_Find(const SpritePack *pack, const char *action)
{
    for (size_t i = 0; i < pack->actions.size(); ++i) {
        if (pack->actions[i].name == action) {
            return (int)i;
        }
    }
    return -1;
}
