#pragma once
#include <SDL3/SDL.h>

#include <string>
#include <vector>

/*
 * A sprite pack is a folder with a character.ini manifest and one PNG per action.
 * Each action PNG has SPRITE_DIRS rows (directions) and `frames` columns, all
 * cells cell_w x cell_h, with the ground point at (cell_w / 2, feet).
 */
static const int SPRITE_DIRS = 8;
/* Row order inside every action PNG. */
static const char *const SPRITE_DIR_NAMES[SPRITE_DIRS] = {
    "DOWN", "DOWN-R", "RIGHT", "UP-R", "UP", "UP-L", "LEFT", "DOWN-L"
};

struct SpriteAction {
    std::string name;
    int frames = 1;
    int fps = 10;
    bool loop = true;
    SDL_Surface *sheet = nullptr; /* ARGB8888, (frames * cell_w) x (SPRITE_DIRS * cell_h) */
};

struct SpritePack {
    std::string dir;
    std::string name;
    std::string role;
    std::string source;
    int cell_w = 0;
    int cell_h = 0;
    int feet = 0;
    /* Sprite pixels per game pixel: 1 = classic pixel art, 3 = drawn 1:1 at 1080p. */
    int scale = 1;
    std::vector<SpriteAction> actions;
};

/* With load_sheets = false only character.ini is read; load actions with SpritePack_LoadSheet. */
bool SpritePack_Load(SpritePack *pack, const char *dir, bool load_sheets = true);
bool SpritePack_LoadSheet(SpritePack *pack, int action);
void SpritePack_FreeSheet(SpritePack *pack, int action);
/* Writes character.ini and the PNG of every action whose sheet is loaded. */
bool SpritePack_Save(const SpritePack *pack);
void SpritePack_Free(SpritePack *pack);
int SpritePack_Find(const SpritePack *pack, const char *action);
