#include <SDL3/SDL.h>
#include <vlc/vlc.h>
#include <iostream>

int main(int argc, char* argv[]) {
    // 1. Inicializar SDL3
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::cerr << "No se pudo inicializar SDL: " << SDL_GetError() << std::endl;
        return -1;
    }

    // 2. Crear la ventana de SDL3
    int width = 1280;
    int height = 720;
    SDL_Window* window = SDL_CreateWindow("Reproductor C++ con SDL3 y libVLC", width, height, SDL_WINDOW_RESIZABLE);
    if (!window) {
        std::cerr << "Error al crear la ventana: " << SDL_GetError() << std::endl;
        SDL_Quit();
        return -1;
    }

    // 3. Inicializar la instancia de libVLC
    libvlc_instance_t* vlc_instance = libvlc_new(0, nullptr);
    if (!vlc_instance) {
        std::cerr << "Error al crear la instancia de libVLC" << std::endl;
        SDL_DestroyWindow(window);
        SDL_Quit();
        return -1;
    }

    // 4. Crear el reproductor multimedia
    libvlc_media_player_t* vlc_player = libvlc_media_player_new(vlc_instance);

    // 5. Vincular la ventana de SDL3 con libVLC según el sistema operativo
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

    // 6. Cargar el archivo multimedia
    libvlc_media_t* vlc_media = libvlc_media_new_path(vlc_instance, "rockstar.mkv");
    libvlc_media_player_set_media(vlc_player, vlc_media);
    libvlc_media_release(vlc_media); // libVLC ya tomó la propiedad del objeto media

    // 7. Iniciar la reproducción
    libvlc_media_player_play(vlc_player);

    // 8. Bucle principal de eventos en C++
    bool running = true;
    SDL_Event event;
    while (running) {
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                running = false;
            }
        }
        SDL_Delay(50); // Evitar consumo excesivo de CPU
    }

    // 9. Liberación de recursos en orden inverso
    libvlc_media_player_stop(vlc_player);
    libvlc_media_player_release(vlc_player);
    libvlc_release(vlc_instance);

    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
