#pragma once
#include <SDL3/SDL.h>

bool Intro_Init(SDL_Renderer* renderer);
void Intro_Event(const SDL_Event* event);
bool Intro_UpdateAndRender(SDL_Renderer* renderer);
void Intro_Quit();
