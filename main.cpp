#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3_mixer/SDL_mixer.h>
#include "Intro.hpp"
#include "Menu.hpp"

enum AppState {
    STATE_INTRO,
    STATE_MENU,
    STATE_GAME
};

static AppState currentState = STATE_INTRO;
static SDL_Window* window = nullptr;
static SDL_Renderer* renderer = nullptr;

SDL_AppResult SDL_AppInit(void** appstate, int argc, char* argv[]) {
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) return SDL_APP_FAILURE;

    if (!MIX_Init()) {
        SDL_Log("Error al inicializar SDL_mixer: %s", SDL_GetError());
        SDL_Quit();
        return SDL_APP_FAILURE;
    }

    if (!SDL_CreateWindowAndRenderer("The Island", 640, 480, SDL_WINDOW_RESIZABLE, &window, &renderer)) {
        return SDL_APP_FAILURE;
    }

    SDL_SetRenderLogicalPresentation(renderer, 640, 480, SDL_LOGICAL_PRESENTATION_LETTERBOX);

    Intro_Init(renderer);
    Menu_Init();


    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void* appstate, SDL_Event* event) {
    if (event->type == SDL_EVENT_QUIT) {
        return SDL_APP_SUCCESS;
    }

    if (currentState == STATE_INTRO) {
        Intro_Event(event);
    } else if (currentState == STATE_MENU) {
        Menu_Event(event);

        if (Menu_IsQuitSelected()) {
            return SDL_APP_SUCCESS; // Cierra la aplicación
        }
        if (Menu_IsStartSelected()) {
            currentState = STATE_GAME; // Pasa a la escena del juego
        }
    }

    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void* appstate) {
    if (currentState == STATE_INTRO) {
        if (!Intro_UpdateAndRender(renderer)) {
            Intro_Quit();
            currentState = STATE_MENU;
        }
    } else if (currentState == STATE_MENU) {
        Menu_Render(renderer);
    } else if (currentState == STATE_GAME) {
        // Render de la escena del juego
        SDL_SetRenderDrawColor(renderer, 20, 80, 30, 255);
        SDL_RenderClear(renderer);
        SDL_RenderDebugText(renderer, 100, 100, "¡Bienvenido a The Island!");
    }

    SDL_RenderPresent(renderer);
    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void* appstate, SDL_AppResult result) {
    if (currentState == STATE_INTRO) Intro_Quit();
    Menu_Quit();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
}
