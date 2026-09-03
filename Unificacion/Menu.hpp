#pragma once
#include <SDL3/SDL.h>

// Inicializa el estado del menú
void Menu_Init();

// Maneja los eventos de navegación por teclado y selección
void Menu_Event(const SDL_Event* event);

// Renderiza los elementos del menú en pantalla
void Menu_Render(SDL_Renderer* renderer);

// Devuelve 'true' si el usuario confirmó la opción "Start"
bool Menu_IsStartSelected();

// Devuelve 'true' si el usuario confirmó la opción "Quit"
bool Menu_IsQuitSelected();

// Libera recursos del menú si los hubiera
void Menu_Quit();
