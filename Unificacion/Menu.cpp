#include "Menu.hpp"
#include <cmath>
#include <SDL3_mixer/SDL_mixer.h>
using namespace std;
namespace {
    constexpr float WINDOW_WIDTH = 640.0f;
    constexpr float WINDOW_HEIGHT = 480.0f;

    int option = 1;
    bool start_selected = false;
    bool quit_selected = false;
}

void Menu_Init() {
    option = 1;
    start_selected = false;
    quit_selected = false;

    MIX_Mixer *mixer = MIX_CreateMixerDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, NULL);
    if (!mixer) {
        SDL_Log("Error al crear el mixer: %s", SDL_GetError());
        MIX_Quit();
        SDL_Quit();
    }

    MIX_Audio *audio = MIX_LoadAudio(mixer, "resources/musica.mp3", false);
    if (!audio) {
        SDL_Log("Error al cargar el archivo MP3: %s", SDL_GetError());
        MIX_DestroyMixer(mixer);
        MIX_Quit();
        SDL_Quit();
    }

    MIX_PlayAudio(mixer, audio);
}

void Menu_Event(const SDL_Event* event) {
    if (event->type == SDL_EVENT_KEY_DOWN && !event->key.repeat) {
        switch (event->key.scancode) {
            case SDL_SCANCODE_DOWN:
            case SDL_SCANCODE_S:
                if (option == 1) {
                    option = 0;
                    SDL_Log("Opcion cambiada a: %d (Quit)", option);
                }
                break;

            case SDL_SCANCODE_UP:
            case SDL_SCANCODE_W:
                if (option == 0) {
                    option = 1;
                    SDL_Log("Opcion cambiada a: %d (Start)", option);
                }
                break;

            case SDL_SCANCODE_SPACE:
            case SDL_SCANCODE_RETURN:
                if (option == 1) {
                    SDL_Log("Opcion elegida: Start / Resident Evil 4");
                    //start_selected = true;
                } else if (option == 0) {
                    SDL_Log("Opcion elegida: Quit");
                    quit_selected = true;
                }
                break;

            default:
                break;
        }
    }
}

void Menu_Render(SDL_Renderer* renderer) {
    // Fondo negro y limpieza
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, SDL_ALPHA_OPAQUE);
    SDL_RenderClear(renderer);

    // Ajuste de escala para el texto
    const float scale = 3.0f;
    SDL_SetRenderScale(renderer, scale, scale);
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, SDL_ALPHA_OPAQUE);

    float x = (0.35f * WINDOW_WIDTH) / scale;
    float y = (0.15f * WINDOW_HEIGHT) / scale;

    const float posicion1 = 0.75f; const float posicion2 = 0.90f;
    y = (posicion1 * WINDOW_HEIGHT) / scale;
    x = (0.45f * WINDOW_WIDTH) / scale;
    SDL_RenderDebugText(renderer, x, y, "Start");

    y = (posicion2 * WINDOW_HEIGHT) / scale;
    SDL_RenderDebugText(renderer, x, y, "Quit");

    // Dibujar el selector según la opción actual
    x = (0.35f * WINDOW_WIDTH) / scale;
    y = (option == 1) ? (posicion1 * WINDOW_HEIGHT) / scale
    : (posicion2 * WINDOW_HEIGHT) / scale;

    float time = SDL_GetTicks() / 500.0f;

    float factor = (sin(time) + 1.0f) / 2.0f;
    Uint8 color_val = static_cast<Uint8>(150 + (105 * factor));
    SDL_SetRenderDrawColor(renderer, color_val, color_val, color_val, SDL_ALPHA_OPAQUE);
    SDL_RenderDebugText(renderer, x, y, ">");
    // Restaurar la escala
    SDL_SetRenderScale(renderer, 1.0f, 1.0f);
}

bool Menu_IsStartSelected() {
    return start_selected;
}

bool Menu_IsQuitSelected() {
    return quit_selected;
}

void Menu_Quit() {
    // Limpieza de recursos locales si agregas imágenes o sonidos en el futuro
}
