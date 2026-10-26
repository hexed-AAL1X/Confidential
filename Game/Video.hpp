#ifndef VIDEO_HPP
#define VIDEO_HPP

#include <SDL3/SDL.h>
#include <vlc/vlc.h>
#include <iostream>
#include <string>

#if defined(_WIN32)
#include <windows.h>
#endif

inline void play_intro_video(const std::string& video_path) {
    // Inicializar SDL exclusivamente para el video de introducción
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::cerr << "No se pudo inicializar SDL para el video: " << SDL_GetError() << std::endl;
        return;
    }

    int width = 1280;
    int height = 720;
    SDL_Window* window = SDL_CreateWindow("Intro - Rockstar", width, height, SDL_WINDOW_RESIZABLE);
    if (!window) {
        std::cerr << "Error al crear la ventana de video: " << SDL_GetError() << std::endl;
        SDL_Quit();
        return;
    }

    // Inicializar libVLC
    libvlc_instance_t* vlc_instance = libvlc_new(0, nullptr);
    if (!vlc_instance) {
        std::cerr << "Error al crear la instancia de libVLC" << std::endl;
        SDL_DestroyWindow(window);
        SDL_Quit();
        return;
    }

    libvlc_media_player_t* vlc_player = libvlc_media_player_new(vlc_instance);

    // Vincular ventana según el Sistema Operativo
#if defined(_WIN32)
    HWND hwnd = static_cast<HWND>(SDL_GetPointerProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
    libvlc_media_player_set_hwnd(vlc_player, hwnd);
#elif defined(__APPLE__)
    void* nswindow = SDL_GetPointerProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, nullptr);
    libvlc_media_player_set_nsobject(vlc_player, nswindow);
#elif defined(__linux__)
    uint32_t x11_screen = static_cast<uint32_t>(SDL_GetNumberProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0));
    if (x11_screen != 0) {
        libvlc_media_player_set_xwindow(vlc_player, x11_screen);
    }
#endif

    // Cargar y reproducir el archivo multimedia
    libvlc_media_t* vlc_media = libvlc_media_new_path(vlc_instance, video_path.c_str());
    libvlc_media_player_set_media(vlc_player, vlc_media);
    libvlc_media_release(vlc_media);

    libvlc_media_player_play(vlc_player);

    // Bucle de reproducción del video
    bool playing = true;
    SDL_Event event;
    while (playing) {
        while (SDL_PollEvent(&event)) {
            // Permitir saltar el video con una tecla o cerrando la ventana de la intro
            if (event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_KEY_DOWN) {
                playing = false;
            }
        }

        // Comprobar si el video terminó por sí solo
        libvlc_state_t state = libvlc_media_player_get_state(vlc_player);
        if (state == libvlc_Ended || state == libvlc_Error) {
            playing = false;
        }

        SDL_Delay(50);
    }

    // Liberar recursos de VLC y cerrar la ventana de la intro
    libvlc_media_player_stop(vlc_player);
    libvlc_media_player_release(vlc_player);
    libvlc_release(vlc_instance);

    SDL_DestroyWindow(window);
    SDL_Quit(); // Cerramos esta instancia de SDL para que el juego pueda inicializar la suya limpiamente
}

#endif