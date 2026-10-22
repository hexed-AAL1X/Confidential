#pragma once
#include <SDL3/SDL.h>

/* Loads the sprite packs (see SpritePack.hpp) from assets/sprites: player/ and the NPCs.
   A missing player pack falls back to the built-in character. */
void Game_LoadSprites(SDL_Renderer *renderer, const char *sprites_dir);
void Game_Start(const char *difficulty_label);
/* Playable characters; settings.player_character picks one (applied by Game_LoadSprites). */
int Game_PlayerCount(void);
const char *Game_PlayerName(int index);
/* Draws that character's idle animation, turning, with its feet at (cx, feet_y) and the
   sprite cell `height` pixels tall. Loads the pack the first time it's asked for. */
void Game_DrawPlayerPreview(SDL_Renderer *renderer, const char *sprites_dir, int index, float cx, float feet_y,
                            float height, float t);
/* Returns true when the player wants to go back to the menu. */
bool Game_Event(const SDL_Event *event);
void Game_Update(float dt);
void Game_Render(SDL_Renderer *renderer, int w, int h);
/* Music file (in assets/music) for where the player is now, or "" for silence. */
const char *Game_Music(void);
