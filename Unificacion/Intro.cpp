#include "Intro.hpp"

namespace {
    // Definición de constantes del Intro
    constexpr float WINDOW_WIDTH = 640.0f;
    constexpr float WINDOW_HEIGHT = 480.0f;
    constexpr Uint64 FADE_OUT_DURATION = 1500; // 1.5s
    constexpr Uint64 FADE_IN_DURATION = 2000;  // 2s

    // Recurso local
    SDL_Texture* texture = nullptr;

    // Control de Fade Out y finalización
    bool is_exiting = false;
    bool intro_finished = false;
    Uint64 fade_out_start_ticks = 0;
}

bool Intro_Init(SDL_Renderer* renderer) {
    char* png_path = nullptr;
    SDL_asprintf(&png_path, "%sresources/cirno.png", SDL_GetBasePath());

    SDL_Surface* surface = SDL_LoadPNG(png_path);
    SDL_free(png_path);

    if (!surface) {
        SDL_Log("Couldn't load bitmap for intro: %s", SDL_GetError());
        return false;
    }

    texture = SDL_CreateTextureFromSurface(renderer, surface);
    SDL_DestroySurface(surface);

    if (!texture) {
        SDL_Log("Couldn't create static texture for intro: %s", SDL_GetError());
        return false;
    }

    // Reiniciar variables de estado al iniciar
    is_exiting = false;
    intro_finished = false;
    fade_out_start_ticks = 0;

    return true;
}

void Intro_Event(const SDL_Event* event) {
    // Si se presiona cualquier tecla durante la intro, inicia el fade out
    if (event->type == SDL_EVENT_KEY_DOWN && !is_exiting) {
        is_exiting = true;
        fade_out_start_ticks = SDL_GetTicks();
    }
}

bool Intro_UpdateAndRender(SDL_Renderer* renderer) {
    if (intro_finished) {
        return false; // El intro ya concluyó
    }

    const Uint64 now = SDL_GetTicks();

    // --- LÓGICA DEL CUBO EN 3D (RENDERIZADO AFÍN) ---
    const float x0 = 0.21f * WINDOW_WIDTH;
    const float y0 = 0.5f * WINDOW_HEIGHT;
    const float px = SDL_min(WINDOW_WIDTH, WINDOW_HEIGHT) / SDL_sqrtf(15.0f);

    const float rad = (((float)((int)(now % 2000))) / 2000.0f) * SDL_PI_F * 2.0f;
    const float cos = SDL_cosf(rad);
    const float sin = SDL_sinf(rad);

    const float k[3] = { 0.0f, 1.0f, 0.0f };
    float mat[9] = {
        cos      + (1.0f-cos)*k[0]*k[0], -sin*k[2] + (1.0f-cos)*k[0]*k[1],  sin*k[1] + (1.0f-cos)*k[0]*k[2],
        sin*k[2] + (1.0f-cos)*k[0]*k[1],  cos      + (1.0f-cos)*k[1]*k[1], -sin*k[0] + (1.0f-cos)*k[1]*k[2],
        -sin*k[1] + (1.0f-cos)*k[0]*k[2],  sin*k[0] + (1.0f-cos)*k[1]*k[2],  cos      + (1.0f-cos)*k[2]*k[2],
    };

    const float pitch_deg = -30.0f;
    const float pitch_rad = pitch_deg * (SDL_PI_F / 180.0f);
    const float cos_p = SDL_cosf(pitch_rad);
    const float sin_p = SDL_sinf(pitch_rad);

    float corners[16];
    for (int i = 0; i < 8; i++) {
        const float x = (i & 1) ? -0.5f : 0.5f;
        const float y = (i & 2) ? -0.5f : 0.5f;
        const float z = (i & 4) ? -0.5f : 0.5f;

        float rx = mat[0]*x + mat[1]*y + mat[2]*z;
        float ry = mat[3]*x + mat[4]*y + mat[5]*z;
        float rz = mat[6]*x + mat[7]*y + mat[8]*z;

        corners[0 + 2*i] = rx;
        corners[1 + 2*i] = ry * cos_p - rz * sin_p;
    }

    // Fondo negro
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, SDL_ALPHA_OPAQUE);
    SDL_RenderClear(renderer);

    // Dibujar caras del cubo
    for (int i = 1; i < 7; i++) {
        const int dir = 3 & ((i & 4) ? ~i : i);
        const int odd = (i & 1) ^ ((i & 2) >> 1) ^ ((i & 4) >> 2);
        if (0 < (odd ? 1.0f : -1.0f) * mat[5 + dir]) continue;

        int origin_index = (1 << ((dir - 1) % 3));
        int right_index = (1 << ((dir + odd) % 3)) | origin_index;
        int down_index = (1 << ((dir + (odd^1)) % 3)) | origin_index;

        if (!odd) {
            origin_index ^= 7;
            right_index ^= 7;
            down_index ^= 7;
        }

        SDL_FPoint origin, right, down;
        origin.x = x0 + px * corners[0 + 2 * origin_index];
        origin.y = y0 + px * corners[1 + 2 * origin_index];
        right.x  = x0 + px * corners[0 + 2 * right_index];
        right.y  = y0 + px * corners[1 + 2 * right_index];
        down.x   = x0 + px * corners[0 + 2 * down_index];
        down.y   = y0 + px * corners[1 + 2 * down_index];

        SDL_RenderTextureAffine(renderer, texture, NULL, &origin, &right, &down);
    }

    // --- TEXTO NINTENDO 64 ---
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, SDL_ALPHA_OPAQUE);
    const float scale = 3.0f;
    SDL_SetRenderScale(renderer, scale, scale);

    const float text_x = (0.5f * WINDOW_WIDTH) / scale;
    const float text_y = (0.5f * WINDOW_HEIGHT) / scale;
    SDL_RenderDebugText(renderer, text_x, text_y, "Nintendo 64");

    SDL_SetRenderScale(renderer, 1.0f, 1.0f);

    // --- EFECTO FADE IN ---
    if (now < FADE_IN_DURATION) {
        float progress = (float)now / (float)FADE_IN_DURATION;
        Uint8 alpha = (Uint8)((1.0f - progress) * 255.0f);

        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, alpha);

        SDL_FRect screen_rect = { 0.0f, 0.0f, WINDOW_WIDTH, WINDOW_HEIGHT };
        SDL_RenderFillRect(renderer, &screen_rect);
    }

    // --- EFECTO FADE OUT ---
    if (is_exiting) {
        Uint64 elapsed_out = now - fade_out_start_ticks;

        if (elapsed_out >= FADE_OUT_DURATION) {
            intro_finished = true;
            return false; // Terminar ejecución del intro
        }

        float progress = (float)elapsed_out / (float)FADE_OUT_DURATION;
        Uint8 alpha = (Uint8)(progress * 255.0f);

        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, alpha);

        SDL_FRect screen_rect = { 0.0f, 0.0f, WINDOW_WIDTH, WINDOW_HEIGHT };
        SDL_RenderFillRect(renderer, &screen_rect);
    }

    return true; // Continuar mostrando el intro
}

void Intro_Quit() {
    if (texture) {
        SDL_DestroyTexture(texture);
        texture = nullptr;
    }
}
