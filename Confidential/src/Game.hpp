#pragma once
#include <SDL3/SDL.h>

/* Loads the sprite sheet drawn with the editor; falls back to the built-in character if missing. */
void Game_LoadSprites(SDL_Renderer *renderer, const char *path);
void Game_Start(const char *difficulty_label);
/* Returns true when the player wants to go back to the menu. */
bool Game_Event(const SDL_Event *event);
void Game_Update(float dt);
void Game_Render(SDL_Renderer *renderer, int w, int h);
