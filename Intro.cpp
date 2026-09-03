#define SDL_MAIN_USE_CALLBACKS 1  /* use the callbacks instead of main() */
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

/* We will use this renderer to draw into this window every frame. */
static SDL_Window *window = NULL;
static SDL_Renderer *renderer = NULL;
static SDL_Texture *texture = NULL;
static int texture_width = 0;
static int texture_height = 0;

//fade out

static bool is_exiting = false;
static Uint64 fade_out_start_ticks = 0;
static const Uint64 FADE_OUT_DURATION = 1500; // Duración del desvanecimiento al salir (1.5s)
#define WINDOW_WIDTH 640
#define WINDOW_HEIGHT 480

/* This function runs once at startup. */
SDL_AppResult SDL_AppInit(void **appstate, int argc, char *argv[])
{
    SDL_Surface *surface = NULL;
    char *png_path = NULL;

    SDL_SetAppMetadata("Example Renderer Affine Textures", "1.0", "com.example.renderer-affine-textures");

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("Couldn't initialize SDL: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    if (!SDL_CreateWindowAndRenderer("examples/renderer/affine-textures", WINDOW_WIDTH, WINDOW_HEIGHT, SDL_WINDOW_RESIZABLE, &window, &renderer)) {
        SDL_Log("Couldn't create window/renderer: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }
    SDL_SetRenderLogicalPresentation(renderer, WINDOW_WIDTH, WINDOW_HEIGHT, SDL_LOGICAL_PRESENTATION_LETTERBOX);

    /* Textures are pixel data that we upload to the video hardware for fast drawing. Lots of 2D
     *      engines refer to these as "sprites." We'll do a static texture (upload once, draw many
     *      times) with data from a bitmap file. */

    /* SDL_Surface is pixel data the CPU can access. SDL_Texture is pixel data the GPU can access.
     *      Load a .png into a surface, move it to a texture from there. */
    SDL_asprintf(&png_path, "%scirno.png", SDL_GetBasePath());  /* allocate a string of the full file path */
    surface = SDL_LoadPNG(png_path);
    if (!surface) {
        SDL_Log("Couldn't load bitmap: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    SDL_free(png_path);  /* done with this, the file is loaded. */

    texture_width = surface->w;
    texture_height = surface->h;

    texture = SDL_CreateTextureFromSurface(renderer, surface);
    if (!texture) {
        SDL_Log("Couldn't create static texture: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    SDL_DestroySurface(surface);  /* done with this, the texture has a copy of the pixels now. */

    return SDL_APP_CONTINUE;  /* carry on with the program! */
}

/* This function runs when a new event (mouse input, keypresses, etc) occurs. */
SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event)
{
    if (event->type == SDL_EVENT_QUIT) {
        return SDL_APP_SUCCESS;  /* end the program, reporting success to the OS. */
    }

    if (event->type == SDL_EVENT_KEY_DOWN && !is_exiting) {
        is_exiting = true;
        fade_out_start_ticks = SDL_GetTicks();
    }
    return SDL_APP_CONTINUE;  /* carry on with the program! */
}

/* This function runs once per frame, and is the heart of the program. */
SDL_AppResult SDL_AppIterate(void *appstate)
{
    //Inicializador para filtro

    const float x0 = 0.21f * WINDOW_WIDTH;
    const float y0 = 0.5f * WINDOW_HEIGHT;
    //Tamano del cubo
    const float px = SDL_min(WINDOW_WIDTH, WINDOW_HEIGHT) / SDL_sqrtf(15.0f);

    const Uint64 now = SDL_GetTicks();

    //Velocidad Cambiar los dos 2000 al mismo valor
    const float rad = (((float) ((int) (now % 2000))) / 2000.0f) * SDL_PI_F * 2;
    const float cos = SDL_cosf(rad);
    const float sin = SDL_sinf(rad);
    //Movimiento del cubo
    //const float k[3] = { 3.0f / SDL_sqrtf(50.0f), 4.0f / SDL_sqrtf(50.0f), 5.0f / SDL_sqrtf(50.0f)};
    const float k[3] = { 0.0f, 1.0f, 0.0f };
    float mat[9] = {
        cos      + (1.0f-cos)*k[0]*k[0], -sin*k[2] + (1.0f-cos)*k[0]*k[1],  sin*k[1] + (1.0f-cos)*k[0]*k[2],
        sin*k[2] + (1.0f-cos)*k[0]*k[1],  cos      + (1.0f-cos)*k[1]*k[1], -sin*k[0] + (1.0f-cos)*k[1]*k[2],
        -sin*k[1] + (1.0f-cos)*k[0]*k[2],  sin*k[0] + (1.0f-cos)*k[1]*k[2],  cos      + (1.0f-cos)*k[2]*k[2],
    };

    // 2. Ángulo de la cámara para ver desde arriba (Pitch/Tilt)
    // Cambia el valor (p. ej. 30, 45, 60 grados) para mayor o menor inclinación superior
    const float pitch_deg = -30.0f;
    const float pitch_rad = pitch_deg * (SDL_PI_F / 180.0f);
    const float cos_p = SDL_cosf(pitch_rad);
    const float sin_p = SDL_sinf(pitch_rad);

    float corners[16];
    int i;

    // 3. Aplicar rotación + inclinación a cada vértice
    for (i = 0; i < 8; i++) {
        const float x = (i & 1) ? -0.5f : 0.5f;
        const float y = (i & 2) ? -0.5f : 0.5f;
        const float z = (i & 4) ? -0.5f : 0.5f;

        // Posición rotada por la matriz horizontal
        float rx = mat[0]*x + mat[1]*y + mat[2]*z;
        float ry = mat[3]*x + mat[4]*y + mat[5]*z;
        float rz = mat[6]*x + mat[7]*y + mat[8]*z;

        // Inclinación de la cámara (Pitch alrededor del eje X)
        // Esto inclina el cubo hacia abajo para simular una vista desde arriba
        corners[0 + 2*i] = rx;
        corners[1 + 2*i] = ry * cos_p - rz * sin_p;
    }
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, SDL_ALPHA_OPAQUE);  // light blue background.
    SDL_RenderClear(renderer);

    for (i = 1; i < 7; i++) {
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
        origin.x = x0 + px*corners[0 + 2*origin_index];
        origin.y = y0 + px*corners[1 + 2*origin_index];
        right.x  = x0 + px*corners[0 + 2*right_index];
        right.y  = y0 + px*corners[1 + 2*right_index];
        down.x   = x0 + px*corners[0 + 2*down_index];
        down.y   = y0 + px*corners[1 + 2*down_index];
        SDL_RenderTextureAffine(renderer, texture, NULL, &origin, &right, &down);
    }

    // --- RENDERIZADO DEL TEXTO CON ESCALA ---
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, SDL_ALPHA_OPAQUE);

    // 1. Escala deseada (ej. 3.0f hace el texto 3 veces más grande)
    const float scale = 3.0f;
    SDL_SetRenderScale(renderer, scale, scale);

    // 2. Ajustamos las coordenadas dividiéndolas entre la escala
    // (de lo contrario, la posición del texto también se multiplicará por 3)
    const float text_x = (0.5f * WINDOW_WIDTH) / scale;
    const float text_y = (0.5f * WINDOW_HEIGHT) / scale;

    SDL_RenderDebugText(renderer, text_x, text_y, "Nintendo 64");

    // 3. Restauramos la escala a 1.0f para los siguientes fotogramas
    SDL_SetRenderScale(renderer, 1.0f, 1.0f);


    // Configuración del efecto
    const Uint64 fade_duration = 2000; // Duración en milisegundos (2 segundos)

    //fade in
    if (now < fade_duration) {
        // Calculamos cuánto ha avanzado el tiempo de 0.0 a 1.0
        float progress = (float)now / (float)fade_duration;

        // Calculamos el nivel de opacidad (de 255 a 0)
        Uint8 alpha = (Uint8)((1.0f - progress) * 255.0f);

        //Blend
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        // Ajustamos la escala a 1.0f por si el texto la modificó

        SDL_SetRenderScale(renderer, 1.0f, 1.0f);

        // Establecemos el color de la capa superpuesta: Blanco puro (255, 255, 255) con alpha variable
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, alpha);

        // Dibujamos un rectángulo lleno que cubra toda la pantalla
        SDL_FRect screen_rect = { 0.0f, 0.0f, (float)WINDOW_WIDTH, (float)WINDOW_HEIGHT };

        SDL_RenderFillRect(renderer, &screen_rect);
    }

    //fade out

    if (is_exiting) {
        Uint64 elapsed_out = now - fade_out_start_ticks;

        if (elapsed_out >= FADE_OUT_DURATION) {
            // El fade out terminó: cerramos la aplicación
            return SDL_APP_SUCCESS;
        }

        float progress = (float)elapsed_out / (float)FADE_OUT_DURATION;
        Uint8 alpha = (Uint8)(progress * 255.0f); // De 0 (transparente) a 255 (negro sólido)

        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, alpha);

        SDL_FRect screen_rect = { 0.0f, 0.0f, (float)WINDOW_WIDTH, (float)WINDOW_HEIGHT };
        SDL_RenderFillRect(renderer, &screen_rect);
    }


    // Presentar el resultado en pantalla
    SDL_RenderPresent(renderer);

    return SDL_APP_CONTINUE;
}

/* This function runs once at shutdown. */
void SDL_AppQuit(void *appstate, SDL_AppResult result)
{
    SDL_DestroyTexture(texture);
    /* SDL will clean up the window/renderer for us. */
}
