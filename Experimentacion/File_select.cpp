#define SDL_MAIN_USE_CALLBACKS 1  /* use the callbacks instead of main() */
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3_mixer/SDL_mixer.h>
#include <cmath>
static SDL_Window *window = NULL;
static SDL_Renderer *renderer = NULL;
static SDL_Texture *texture = NULL;
static int texture_width = 0;
static int texture_height = 0;
int option = 0;
static bool is_exiting = false;
static Uint64 fade_out_start_ticks = 0;
static const Uint64 FADE_OUT_DURATION = 1500;
#define WINDOW_WIDTH 640
#define WINDOW_HEIGHT 480
MIX_Mixer *mixer = NULL;
MIX_Audio *move = NULL;
MIX_Audio *confirm = NULL;
/* --- NUEVA FUNCIÓN REUTILIZABLE PARA DIBUJAR CUALQUIER CUBO 3D --- */
static void RenderCube(SDL_Renderer *rend, SDL_Texture *tex, float center_x, float center_y, float size, Uint64 now, float pitch_deg)
{
    // 1. Matriz de rotación (animada en función del tiempo)
    const float rad = (((float) ((int) (now % 2000))) / 2000.0f) * SDL_PI_F * 2.0f;
    const float cos_r = SDL_cosf(rad);
    const float sin_r = SDL_sinf(rad);
    const float k[3] = { 0.0f, 1.0f, 0.0f };

    float mat[9] = {
        cos_r      + (1.0f-cos_r)*k[0]*k[0], -sin_r*k[2] + (1.0f-cos_r)*k[0]*k[1],  sin_r*k[1] + (1.0f-cos_r)*k[0]*k[2],
        sin_r*k[2] + (1.0f-cos_r)*k[0]*k[1],  cos_r      + (1.0f-cos_r)*k[1]*k[1], -sin_r*k[0] + (1.0f-cos_r)*k[1]*k[2],
        -sin_r*k[1] + (1.0f-cos_r)*k[0]*k[2],  sin_r*k[0] + (1.0f-cos_r)*k[1]*k[2],  cos_r      + (1.0f-cos_r)*k[2]*k[2],
    };

    // 2. Ángulo de la cámara (Pitch)
    const float pitch_rad = pitch_deg * (SDL_PI_F / 180.0f);
    const float cos_p = SDL_cosf(pitch_rad);
    const float sin_p = SDL_sinf(pitch_rad);

    float corners[16];
    int i;

    // 3. Proyección de los 8 vértices
    for (i = 0; i < 8; i++) {
        const float x = (i & 1) ? -0.5f : 0.5f;
        const float y = (i & 2) ? -0.5f : 0.5f;
        const float z = (i & 4) ? -0.5f : 0.5f;

        float rx = mat[0]*x + mat[1]*y + mat[2]*z;
        float ry = mat[3]*x + mat[4]*y + mat[5]*z;
        float rz = mat[6]*x + mat[7]*y + mat[8]*z;

        corners[0 + 2*i] = rx;
        corners[1 + 2*i] = ry * cos_p - rz * sin_p;
    }

    // 4. Renderizado de las 6 caras visibles con textura afín
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
        origin.x = center_x + size * corners[0 + 2*origin_index];
        origin.y = center_y + size * corners[1 + 2*origin_index];
        right.x  = center_x + size * corners[0 + 2*right_index];
        right.y  = center_y + size * corners[1 + 2*right_index];
        down.x   = center_x + size * corners[0 + 2*down_index];
        down.y   = center_y + size * corners[1 + 2*down_index];

        SDL_RenderTextureAffine(rend, tex, NULL, &origin, &right, &down);
    }
}

/*static void RenderPyramid(SDL_Renderer *rend, SDL_Texture *tex, float center_x, float center_y, float size, Uint64 now, float pitch_deg)
{
    // 1. Matriz de rotación animada en el eje Y
    const float rad = (((float) ((int) (now % 2000))) / 2000.0f) * SDL_PI_F * 2.0f;
    const float cos_r = SDL_cosf(rad);
    const float sin_r = SDL_sinf(rad);

    float mat[9] = {
        cos_r, 0.0f, sin_r,
        0.0f, 1.0f,  0.0f,
        -sin_r, 0.0f, cos_r
    };

    // 2. Inclinación de la cámara (Pitch)
    const float pitch_rad = pitch_deg * (SDL_PI_F / 180.0f);
    const float cos_p = SDL_cosf(pitch_rad);
    const float sin_p = SDL_sinf(pitch_rad);

    // 3. Vértices en 3D
    const float raw_vertices[5][3] = {
        {  0.0f, -0.5f,  0.0f }, // 0: Cúspide
        { -0.5f,  0.5f, -0.5f }, // 1: Frontal-Izquierda
        {  0.5f,  0.5f, -0.5f }, // 2: Frontal-Derecha
        {  0.5f,  0.5f,  0.5f }, // 3: Trasera-Derecha
        { -0.5f,  0.5f,  0.5f }  // 4: Trasera-Izquierda
    };

    // Proyección a coordenadas de pantalla
    SDL_FPoint proj[5];
    for (int i = 0; i < 5; i++) {
        float rx = mat[0]*raw_vertices[i][0] + mat[1]*raw_vertices[i][1] + mat[2]*raw_vertices[i][2];
        float ry = mat[3]*raw_vertices[i][0] + mat[4]*raw_vertices[i][1] + mat[5]*raw_vertices[i][2];
        float rz = mat[6]*raw_vertices[i][0] + mat[7]*raw_vertices[i][1] + mat[8]*raw_vertices[i][2];

        proj[i].x = center_x + size * rx;
        proj[i].y = center_y + size * (ry * cos_p - rz * sin_p);
    }

    // 4. Renderizado de las 4 caras triangulares
    const int faces[4][3] = {
        { 0, 1, 2 },
        { 0, 2, 3 },
        { 0, 3, 4 },
        { 0, 4, 1 }
    };

    for (int f = 0; f < 4; f++) {
        int i0 = faces[f][0];
        int i1 = faces[f][1];
        int i2 = faces[f][2];

        // Back-face culling
        float cross_product = (proj[i1].x - proj[i0].x) * (proj[i2].y - proj[i0].y) -
        (proj[i1].y - proj[i0].y) * (proj[i2].x - proj[i0].x);
        if (cross_product <= 0) continue;

        // Construir los 3 vértices del triángulo con sus coordenadas UV de la textura
        SDL_Vertex vertices[3] = {
            { proj[i0], { 255, 255, 255, 255 }, { 0.5f, 0.0f } }, // Cúspide (Centro superior de la textura)
            { proj[i1], { 255, 255, 255, 255 }, { 0.0f, 1.0f } }, // Esquina inf-izq de la textura
            { proj[i2], { 255, 255, 255, 255 }, { 1.0f, 1.0f } }  // Esquina inf-der de la textura
        };

        SDL_RenderGeometry(rend, tex, vertices, 3, NULL, 0);
    }

    // 5. Renderizado de la Base Cuadrada
    float base_cross = (proj[2].x - proj[1].x) * (proj[4].y - proj[1].y) -
    (proj[2].y - proj[1].y) * (proj[4].x - proj[1].x);
    if (base_cross > 0) {
        SDL_Vertex base_vertices[4] = {
            { proj[1], { 255, 255, 255, 255 }, { 0.0f, 0.0f } },
            { proj[2], { 255, 255, 255, 255 }, { 1.0f, 0.0f } },
            { proj[3], { 255, 255, 255, 255 }, { 1.0f, 1.0f } },
            { proj[4], { 255, 255, 255, 255 }, { 0.0f, 1.0f } }
        };
        int indices[6] = { 0, 1, 2,  0, 2, 3 }; // 2 triángulos para formar la base cuadrada
        SDL_RenderGeometry(rend, tex, base_vertices, 4, indices, 6);
    }
}*/

static void RenderPyramid(SDL_Renderer *rend, SDL_Texture *tex, float center_x, float center_y, float size, Uint64 now, float pitch_deg)
{
    // 1. Matriz de rotación animada en el eje Y
    const float rad = (((float) ((int) (now % 2000))) / 2000.0f) * SDL_PI_F * 2.0f;
    const float cos_r = SDL_cosf(rad);
    const float sin_r = SDL_sinf(rad);

    float mat[9] = {
        cos_r, 0.0f, sin_r,
        0.0f, 1.0f,  0.0f,
        -sin_r, 0.0f, cos_r
    };

    // 2. Ángulo de la cámara (Pitch)
    const float pitch_rad = pitch_deg * (SDL_PI_F / 180.0f);
    const float cos_p = SDL_cosf(pitch_rad);
    const float sin_p = SDL_sinf(pitch_rad);

    // 3. Vértices en 3D
    const float raw_vertices[5][3] = {
        {  0.0f, -0.5f,  0.0f }, // 0: Cúspide
        { -0.5f,  0.5f, -0.5f }, // 1: Frontal-Izquierda
        {  0.5f,  0.5f, -0.5f }, // 2: Frontal-Derecha
        {  0.5f,  0.5f,  0.5f }, // 3: Trasera-Derecha
        { -0.5f,  0.5f,  0.5f }  // 4: Trasera-Izquierda
    };

    // Proyección a 2D
    SDL_FPoint proj[5];
    for (int i = 0; i < 5; i++) {
        float rx = mat[0]*raw_vertices[i][0] + mat[1]*raw_vertices[i][1] + mat[2]*raw_vertices[i][2];
        float ry = mat[3]*raw_vertices[i][0] + mat[4]*raw_vertices[i][1] + mat[5]*raw_vertices[i][2];
        float rz = mat[6]*raw_vertices[i][0] + mat[7]*raw_vertices[i][1] + mat[8]*raw_vertices[i][2];

        proj[i].x = center_x + size * rx;
        proj[i].y = center_y + size * (ry * cos_p - rz * sin_p);
    }

    // Color base blanco para que el PNG conserve sus colores originales
    SDL_FColor color_blanco = { 1.0f, 1.0f, 1.0f, 1.0f };

    // 4. Renderizado de las 4 Caras Triangulares con la Textura PNG
    const int faces[4][3] = {
        { 0, 1, 2 },
        { 0, 2, 3 },
        { 0, 3, 4 },
        { 0, 4, 1 }
    };

    for (int f = 0; f < 4; f++) {
        int i0 = faces[f][0];
        int i1 = faces[f][1];
        int i2 = faces[f][2];

        // Back-face Culling (Descartar caras no visibles)
        float cross_product = (proj[i1].x - proj[i0].x) * (proj[i2].y - proj[i0].y) -
        (proj[i1].y - proj[i0].y) * (proj[i2].x - proj[i0].x);
        if (cross_product <= 0) continue;

        // Mapear los vértices 2D con la textura PNG mediante coordenadas UV
        SDL_Vertex vertices[3] = {
            // { Posición (x,y), Color (r,g,b,a), Textura UV (u,v) }
            { proj[i0], color_blanco, { 0.5f, 0.0f } }, // Punta de la pirámide -> Centro Superior del PNG
            { proj[i1], color_blanco, { 0.0f, 1.0f } }, // Esquina Base 1       -> Esquina Inf-Izq del PNG
            { proj[i2], color_blanco, { 1.0f, 1.0f } }  // Esquina Base 2       -> Esquina Inf-Der del PNG
        };

        // Renderiza el triángulo proyectando el PNG dentro del área
        SDL_RenderGeometry(rend, tex, vertices, 3, NULL, 0);
    }

    // 5. Renderizar la Base Cuadrada con el PNG (Si es visible desde abajo)
    /*float base_cross = (proj[2].x - proj[1].x) * (proj[4].y - proj[1].y) -
    (proj[2].y - proj[1].y) * (proj[4].x - proj[1].x);
    if (base_cross > 0) {
        SDL_Vertex base_vertices[4] = {
            { proj[1], color_blanco, { 0.0f, 0.0f } }, // Sup-Izq
            { proj[2], color_blanco, { 1.0f, 0.0f } }, // Sup-Der
            { proj[3], color_blanco, { 1.0f, 1.0f } }, // Inf-Der
            { proj[4], color_blanco, { 0.0f, 1.0f } }  // Inf-Izq
        };
        int indices[6] = { 0, 1, 2,  0, 2, 3 };
        SDL_RenderGeometry(rend, tex, base_vertices, 4, indices, 6);
    }*/
}

SDL_AppResult SDL_AppInit(void **appstate, int argc, char *argv[])
{
    SDL_Surface *surface = NULL;
    char *png_path = NULL;

    SDL_SetAppMetadata("Example Renderer Affine Textures", "1.0", "com.example.renderer-affine-textures");

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) {
        SDL_Log("Couldn't initialize SDL: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }


    // 2. Inicializar SDL3_mixer
    if (!MIX_Init()) {
        SDL_Log("Error al inicializar SDL_mixer: %s", SDL_GetError());
        SDL_Quit();
        return SDL_APP_FAILURE;
    }

    // 3. Crear el dispositivo de audio del mezclador
    //MIX_Mixer *mixer = MIX_CreateMixerDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, NULL);
    mixer = MIX_CreateMixerDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, NULL);
    if (!mixer) {
        SDL_Log("Error al crear el mixer: %s", SDL_GetError());
        MIX_Quit();
        SDL_Quit();
        return SDL_APP_FAILURE;
    }


    if (!SDL_CreateWindowAndRenderer("examples/renderer/affine-textures", WINDOW_WIDTH, WINDOW_HEIGHT, SDL_WINDOW_RESIZABLE, &window, &renderer)) {
        SDL_Log("Couldn't create window/renderer: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }
    SDL_SetRenderLogicalPresentation(renderer, WINDOW_WIDTH, WINDOW_HEIGHT, SDL_LOGICAL_PRESENTATION_LETTERBOX);

    SDL_asprintf(&png_path, "%striangle.png", SDL_GetBasePath());
    surface = SDL_LoadPNG(png_path);
    if (!surface) {
        SDL_Log("Couldn't load bitmap: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    SDL_free(png_path);

    texture_width = surface->w;
    texture_height = surface->h;

    texture = SDL_CreateTextureFromSurface(renderer, surface);
    if (!texture) {
        SDL_Log("Couldn't create static texture: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    MIX_Audio *audio = MIX_LoadAudio(mixer, "RAG.mp3", false);
    move = MIX_LoadAudio(mixer, "effect.mp3", false);
    confirm = MIX_LoadAudio(mixer, "confirm.mp3", false);
    if (!audio) {
        SDL_Log("Error al cargar el archivo MP3: %s", SDL_GetError());
        MIX_DestroyMixer(mixer);
        MIX_Quit();
        SDL_Quit();
        return SDL_APP_FAILURE;
    }

    MIX_PlayAudio(mixer, audio);

    SDL_DestroySurface(surface);
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event)
{
    if (event->type == SDL_EVENT_QUIT) {
        return SDL_APP_SUCCESS;
    }

    /*if (event->type == SDL_EVENT_KEY_DOWN && !is_exiting) {
        is_exiting = true;
        fade_out_start_ticks = SDL_GetTicks();
    }*/

    if (event->type == SDL_EVENT_KEY_DOWN) {
        switch (event->key.scancode) {
            case SDL_SCANCODE_RIGHT:
                if (option == 2){
                    SDL_Log("Estas en el tope %d", option);
                } else {
                    MIX_PlayAudio(mixer, move);
                    option++;
                }
                break;

            case SDL_SCANCODE_LEFT:
                if (option == 0) {
                    SDL_Log("Estas en el tope %d", option);
                } else {
                    MIX_PlayAudio(mixer, move);
                    option--;
                }
                break;

            case SDL_SCANCODE_SPACE:
            case SDL_SCANCODE_RETURN:
                MIX_PlayAudio(mixer, confirm);
                SDL_Log("Se carga espacio de guardado %d", option);
                is_exiting = true;
                fade_out_start_ticks = SDL_GetTicks();
                break;

            default:
                break;
        }
    }
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void *appstate)
{
    const Uint64 now = SDL_GetTicks();

    SDL_SetRenderDrawColor(renderer, 0, 0, 0, SDL_ALPHA_OPAQUE);
    SDL_RenderClear(renderer);

    // Tamaño base general
    const float base_px = SDL_min(WINDOW_WIDTH, WINDOW_HEIGHT) / SDL_sqrtf(15.0f);

    // --- RENDERIZADO DE MÚLTIPLES CUBOS ---

    // Cubo 1: Izquierda (el original)
    if (option == 0){
        RenderPyramid(renderer, texture, 0.15f * WINDOW_WIDTH, 0.5f * WINDOW_HEIGHT, base_px, now, -30.0f);
        RenderPyramid(renderer, texture, 0.45f * WINDOW_WIDTH, 0.5f * WINDOW_HEIGHT, base_px, 0, -30.0f);
        RenderPyramid(renderer, texture, 0.75f * WINDOW_WIDTH, 0.5f * WINDOW_HEIGHT, base_px, 0, -30.0f);
    }
    else if (option == 1){
        RenderPyramid(renderer, texture, 0.15f * WINDOW_WIDTH, 0.5f * WINDOW_HEIGHT, base_px, 0, -30.0f);
        RenderPyramid(renderer, texture, 0.45f * WINDOW_WIDTH, 0.5f * WINDOW_HEIGHT, base_px, now, -30.0f);
        RenderPyramid(renderer, texture, 0.75f * WINDOW_WIDTH, 0.5f * WINDOW_HEIGHT, base_px, 0, -30.0f);
    }
    else if (option == 2){
        RenderPyramid(renderer, texture, 0.15f * WINDOW_WIDTH, 0.5f * WINDOW_HEIGHT, base_px, 0, -30.0f);
        RenderPyramid(renderer, texture, 0.45f * WINDOW_WIDTH, 0.5f * WINDOW_HEIGHT, base_px, 0, -30.0f);
        RenderPyramid(renderer, texture, 0.75f * WINDOW_WIDTH, 0.5f * WINDOW_HEIGHT, base_px, now, -30.0f);
    }
    //RenderPyramid(renderer, texture, 0.15f * WINDOW_WIDTH, 0.5f * WINDOW_HEIGHT, base_px, now, -30.0f);

    //RenderPyramid(renderer, texture, 0.45f * WINDOW_WIDTH, 0.5f * WINDOW_HEIGHT, base_px, 0, -30.0f);

    //RenderPyramid(renderer, texture, 0.75f * WINDOW_WIDTH, 0.5f * WINDOW_HEIGHT, base_px, now, -30.0f);
    // --- RENDERIZADO DEL TEXTO ---
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, SDL_ALPHA_OPAQUE);
    const float scale = 3.0f;
    SDL_SetRenderScale(renderer, scale, scale);

    const float text_x = (0.5f * WINDOW_WIDTH) / scale;
    const float text_y = (0.5f * WINDOW_HEIGHT) / scale;
    float x; float y;

    switch (option) {
        case 0:
            x = 0.15f * WINDOW_WIDTH / scale;
            y = 0.75f * WINDOW_HEIGHT / scale;
            break;

        case 1:
            x = 0.45f * WINDOW_WIDTH / scale;
            y = 0.75f * WINDOW_HEIGHT / scale;
            break;

        case 2:
            x = 0.75f * WINDOW_WIDTH / scale;
            y = 0.75f * WINDOW_HEIGHT / scale;
            break;

        default:
            break;
    }

    float time = SDL_GetTicks() / 500.0f;
    float factor = (sin(time) + 1.0f) / 2.0f;
    Uint8 color_val = static_cast<Uint8>(150 + (105 * factor));
    SDL_SetRenderDrawColor(renderer, color_val, color_val, color_val, SDL_ALPHA_OPAQUE);
    SDL_RenderDebugText(renderer, x, y, "^");

    SDL_SetRenderScale(renderer, 1.0f, 1.0f);


    // --- FADE IN ---
    const Uint64 fade_duration = 2000;
    if (now < fade_duration) {
        float progress = (float)now / (float)fade_duration;
        Uint8 alpha = (Uint8)((1.0f - progress) * 255.0f);

        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderScale(renderer, 1.0f, 1.0f);
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, alpha);

        SDL_FRect screen_rect = { 0.0f, 0.0f, (float)WINDOW_WIDTH, (float)WINDOW_HEIGHT };
        SDL_RenderFillRect(renderer, &screen_rect);
    }

    // --- FADE OUT ---
    if (is_exiting) {
        Uint64 elapsed_out = now - fade_out_start_ticks;

        if (elapsed_out >= FADE_OUT_DURATION) {
            return SDL_APP_SUCCESS;
        }

        float progress = (float)elapsed_out / (float)FADE_OUT_DURATION;

        float audio_gain = 1.0f - progress;
        MIX_SetMixerGain(mixer, audio_gain);

        Uint8 alpha = (Uint8)(progress * 255.0f);

        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, alpha);

        SDL_FRect screen_rect = { 0.0f, 0.0f, (float)WINDOW_WIDTH, (float)WINDOW_HEIGHT };
        SDL_RenderFillRect(renderer, &screen_rect);
    }

    SDL_RenderPresent(renderer);
    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void *appstate, SDL_AppResult result)
{
    SDL_DestroyTexture(texture);
}
