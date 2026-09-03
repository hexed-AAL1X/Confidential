#include <SDL3/SDL.h>
#include <SDL3_mixer/SDL_mixer.h>

int main(int argc, char *argv[]) {
    // 1. Inicializar SDL
    if (!SDL_Init(SDL_INIT_AUDIO)) {
        SDL_Log("Error al inicializar SDL: %s", SDL_GetError());
        return 1;
    }

    // 2. Inicializar SDL3_mixer
    if (!MIX_Init()) {
        SDL_Log("Error al inicializar SDL_mixer: %s", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    // 3. Crear el dispositivo de audio del mezclador
    MIX_Mixer *mixer = MIX_CreateMixerDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, NULL);
    if (!mixer) {
        SDL_Log("Error al crear el mixer: %s", SDL_GetError());
        MIX_Quit();
        SDL_Quit();
        return 1;
    }

    // 4. Cargar el archivo MP3 (en SDL3 todo sonido es un MIX_Audio)
    // El último parámetro (false) indica que se transmitirá desde el disco (streaming)
    MIX_Audio *audio = MIX_LoadAudio(mixer, "musica.mp3", false);
    if (!audio) {
        SDL_Log("Error al cargar el archivo MP3: %s", SDL_GetError());
        MIX_DestroyMixer(mixer);
        MIX_Quit();
        SDL_Quit();
        return 1;
    }

    // 5. Reproducir el audio en el mixer
    if (!MIX_PlayAudio(mixer, audio)) {
        SDL_Log("Error al reproducir audio: %s", SDL_GetError());
    } else {
        SDL_Log("Reproduciendo MP3 con SDL3_mixer...");
        SDL_Delay(500000); // Esperar 5 segundos escuchando
    }

    // 6. Limpieza de recursos
    MIX_DestroyAudio(audio);
    MIX_DestroyMixer(mixer);
    MIX_Quit();
    SDL_Quit();

    return 0;
}
