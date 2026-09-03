#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3_mixer/SDL_mixer.h>
#include <webp/decode.h>
#include <cmath>
#include <cstring>
#include <dirent.h>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_THREAD_LOCALS
#include "stb_image.h"

static SDL_Window *window = NULL;
static SDL_Renderer *renderer = NULL;
static MIX_Mixer *mixer = NULL;
static MIX_Audio *typewriter_sfx = NULL;
static MIX_Audio *menu_music = NULL;
static MIX_Track *menu_music_track = NULL;
static MIX_Audio *move_sfx = NULL;
static MIX_Audio *confirm_sfx = NULL;
static bool menu_music_started = false;
static int typewriter_last_char = 0;
static SDL_Texture *triangle_tex = NULL;

enum Screen {
    SCREEN_WARNING,
    SCREEN_MENU,
    SCREEN_LOAD
};

static Screen screen = SCREEN_WARNING;
static int option = 0;
static int load_option = 0;
static bool menu_visible = false;
static bool is_exiting = false;
static Uint64 fade_out_start_ticks = 0;
static Uint64 slideshow_start_ticks = 0;
static Uint64 warning_start_ticks = 0;
static Uint64 menu_enter_ticks = 0;
static Uint64 load_enter_ticks = 0;
static const Uint64 FADE_OUT_DURATION = 1500;
static const Uint64 SLIDE_DURATION_MS = 5000;
static const Uint64 SLIDE_FADE_MS = 1000;
static const Uint64 WARNING_TYPE_MS = 2200;
static const Uint64 WARNING_PAUSE_MS = 3000;
static const Uint64 WARNING_FADE_OUT_MS = 900;
static const Uint64 MENU_FADE_IN_MS = 1200;

#define WINDOW_WIDTH 640
#define WINDOW_HEIGHT 480
#define DEBUG_FONT_W 8
#define MAX_SLIDES 64

static const char *const MENU_OPTIONS[] = {
    "Play Alone",
    "Play with Drugies",
    "Load Game",
    "Get out"
};
static const int MENU_COUNT = 4;

static const char *const WARNING_TITLE = "WARNING";
static const char *const WARNING_DESC[] = {
    "This game has explicit content +18.",
    "Play under your own responsibility."
};
static const int WARNING_DESC_COUNT = 2;

static const char *const FILE_LABELS[] = {
    "File 1",
    "File 2",
    "File 3"
};
static const char *const FILE_EMPTY_LABEL = "[Empty]";
static const int FILE_COUNT = 3;

static SDL_Texture *slides[MAX_SLIDES] = {nullptr};
static int slide_count = 0;
static int slideshow_index = 0;
static int slideshow_next_index = 0;
static Uint64 slideshow_slide_start = 0;

static bool ends_with_ci(const char *text, const char *suffix)
{
    const size_t text_len = std::strlen(text);
    const size_t suffix_len = std::strlen(suffix);
    if (text_len < suffix_len) {
        return false;
    }
    return SDL_strcasecmp(text + text_len - suffix_len, suffix) == 0;
}

static bool is_image_filename(const char *name)
{
    return ends_with_ci(name, ".png")
        || ends_with_ci(name, ".jpg")
        || ends_with_ci(name, ".jpeg")
        || ends_with_ci(name, ".bmp")
        || ends_with_ci(name, ".gif")
        || ends_with_ci(name, ".tga")
        || ends_with_ci(name, ".webp");
}

static int image_ext_priority(const char *name)
{
    if (ends_with_ci(name, ".png")) return 0;
    if (ends_with_ci(name, ".jpg") || ends_with_ci(name, ".jpeg")) return 1;
    if (ends_with_ci(name, ".webp")) return 2;
    if (ends_with_ci(name, ".bmp")) return 3;
    if (ends_with_ci(name, ".gif")) return 4;
    if (ends_with_ci(name, ".tga")) return 5;
    return 99;
}

static void basename_no_ext(const char *filename, char *out, size_t out_size)
{
    const char *dot = SDL_strrchr(filename, '.');
    size_t len = std::strlen(filename);
    if (dot && dot != filename) {
        len = (size_t)(dot - filename);
    }
    if (len >= out_size) {
        len = out_size - 1;
    }
    SDL_memcpy(out, filename, len);
    out[len] = '\0';
}

static SDL_Surface *surface_from_rgba(unsigned char *pixels, int w, int h)
{
    if (!pixels || w <= 0 || h <= 0) {
        return nullptr;
    }

    SDL_Surface *surface = SDL_CreateSurface(w, h, SDL_PIXELFORMAT_RGBA32);
    if (!surface) {
        return nullptr;
    }

    const size_t bytes = (size_t)w * (size_t)h * 4;
    if ((size_t)surface->pitch == (size_t)w * 4) {
        SDL_memcpy(surface->pixels, pixels, bytes);
    } else {
        for (int y = 0; y < h; ++y) {
            SDL_memcpy(
                (Uint8 *)surface->pixels + y * surface->pitch,
                pixels + y * w * 4,
                (size_t)w * 4);
        }
    }
    return surface;
}

static SDL_Surface *load_surface_webp(const char *path)
{
    size_t data_size = 0;
    void *data = SDL_LoadFile(path, &data_size);
    if (!data) {
        return nullptr;
    }

    int w = 0;
    int h = 0;
    uint8_t *rgba = WebPDecodeRGBA((const uint8_t *)data, data_size, &w, &h);
    SDL_free(data);

    if (!rgba) {
        return nullptr;
    }

    SDL_Surface *surface = surface_from_rgba(rgba, w, h);
    WebPFree(rgba);
    return surface;
}

static SDL_Surface *load_surface_any(const char *path)
{
    if (ends_with_ci(path, ".webp")) {
        return load_surface_webp(path);
    }

    if (ends_with_ci(path, ".png")) {
        SDL_Surface *png = SDL_LoadPNG(path);
        if (png) {
            return png;
        }
    }

    if (ends_with_ci(path, ".bmp")) {
        SDL_Surface *bmp = SDL_LoadBMP(path);
        if (bmp) {
            return bmp;
        }
    }

    int w = 0;
    int h = 0;
    int channels = 0;
    unsigned char *pixels = stbi_load(path, &w, &h, &channels, 4);
    if (!pixels) {
        SDL_Log("stbi_load failed for %s: %s", path, stbi_failure_reason());
        return nullptr;
    }

    SDL_Surface *surface = surface_from_rgba(pixels, w, h);
    stbi_image_free(pixels);
    return surface;
}

static int pick_random_slide(int avoid)
{
    if (slide_count <= 0) {
        return 0;
    }
    if (slide_count == 1) {
        return 0;
    }

    int idx = 0;
    do {
        idx = (int)SDL_rand(slide_count);
    } while (idx == avoid);
    return idx;
}

static void load_slides_from_dir(const char *img_dir)
{
    DIR *dir = opendir(img_dir);
    if (!dir) {
        SDL_Log("Couldn't open image dir %s", img_dir);
        return;
    }

    char *names[MAX_SLIDES * 2] = {nullptr};
    int name_count = 0;

    struct dirent *entry = nullptr;
    while ((entry = readdir(dir)) != nullptr && name_count < (MAX_SLIDES * 2)) {
        if (entry->d_name[0] == '.' || !is_image_filename(entry->d_name)) {
            continue;
        }
        char base_check[512];
        basename_no_ext(entry->d_name, base_check, sizeof(base_check));
        if (SDL_strcasecmp(base_check, "triangle") == 0) {
            continue;
        }
        names[name_count] = SDL_strdup(entry->d_name);
        if (names[name_count]) {
            ++name_count;
        }
    }
    closedir(dir);

    bool used[MAX_SLIDES * 2] = {false};

    for (int i = 0; i < name_count && slide_count < MAX_SLIDES; ++i) {
        if (used[i] || !names[i]) {
            continue;
        }

        char base_i[512];
        basename_no_ext(names[i], base_i, sizeof(base_i));

        int best = i;
        int best_prio = image_ext_priority(names[i]);

        for (int j = i + 1; j < name_count; ++j) {
            if (used[j] || !names[j]) {
                continue;
            }
            char base_j[512];
            basename_no_ext(names[j], base_j, sizeof(base_j));
            if (SDL_strcasecmp(base_i, base_j) != 0) {
                continue;
            }
            used[j] = true;
            const int prio = image_ext_priority(names[j]);
            if (prio < best_prio) {
                best = j;
                best_prio = prio;
            }
        }
        used[best] = true;

        char *path = nullptr;
        SDL_asprintf(&path, "%s/%s", img_dir, names[best]);
        if (!path) {
            continue;
        }

        SDL_Surface *surface = load_surface_any(path);
        if (!surface) {
            SDL_Log("Couldn't load %s: %s", path, SDL_GetError());
            SDL_free(path);
            continue;
        }

        SDL_Texture *tex = SDL_CreateTextureFromSurface(renderer, surface);
        SDL_DestroySurface(surface);
        SDL_free(path);

        if (!tex) {
            SDL_Log("Couldn't create texture: %s", SDL_GetError());
            continue;
        }

        SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_LINEAR);
        SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
        slides[slide_count++] = tex;
        SDL_Log("Loaded slide: %s", names[best]);
    }

    for (int i = 0; i < name_count; ++i) {
        SDL_free(names[i]);
    }
}

static void reset_slideshow_order(void)
{
    if (slide_count <= 0) {
        slideshow_index = 0;
        slideshow_next_index = 0;
        return;
    }

    slideshow_index = (int)SDL_rand(slide_count);
    slideshow_next_index = pick_random_slide(slideshow_index);
}

static Uint32 SDLCALL slideshow_wake_timer(void *userdata, SDL_TimerID timer_id, Uint32 interval)
{
    (void)userdata;
    (void)timer_id;
    SDL_Event event;
    SDL_zero(event);
    event.type = SDL_EVENT_USER;
    SDL_PushEvent(&event);
    return interval;
}

SDL_AppResult SDL_AppInit(void **appstate, int argc, char *argv[])
{
    SDL_SetAppMetadata("The Island", "1.0", "com.theisland.menu");

    SDL_SetHint(SDL_HINT_MAIN_CALLBACK_RATE, "60");

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) {
        SDL_Log("Couldn't initialize SDL: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    if (!MIX_Init()) {
        SDL_Log("Couldn't initialize SDL_mixer: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    mixer = MIX_CreateMixerDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, NULL);
    if (!mixer) {
        SDL_Log("Couldn't create mixer: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    if (!SDL_CreateWindowAndRenderer(
            "The Island",
            WINDOW_WIDTH,
            WINDOW_HEIGHT,
            SDL_WINDOW_FULLSCREEN,
            &window,
            &renderer)) {
        SDL_Log("Couldn't create window/renderer: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    SDL_SetRenderLogicalPresentation(
        renderer,
        WINDOW_WIDTH,
        WINDOW_HEIGHT,
        SDL_LOGICAL_PRESENTATION_LETTERBOX);

    SDL_HideCursor();

    const char *base = SDL_GetBasePath();
    if (!base) {
        SDL_Log("Couldn't get base path: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    {
        char *img_dir = nullptr;
        SDL_asprintf(&img_dir, "%s../assets/img", base);
        if (img_dir) {
            load_slides_from_dir(img_dir);
            SDL_free(img_dir);
        }
    }

    {
        char *sfx_path = nullptr;
        SDL_asprintf(&sfx_path, "%s../assets/sfx/typewriter.wav", base);
        if (sfx_path) {
            typewriter_sfx = MIX_LoadAudio(mixer, sfx_path, true);
            if (!typewriter_sfx) {
                SDL_Log("Couldn't load typewriter sfx: %s", SDL_GetError());
            }
            SDL_free(sfx_path);
        }
    }

    {
        char *music_path = nullptr;
        SDL_asprintf(
            &music_path,
            "%s../assets/sfx/Spazzmatica-Polka(chosic.com).mp3",
            base);
        if (music_path) {
            menu_music = MIX_LoadAudio(mixer, music_path, true);
            if (!menu_music) {
                SDL_Log("Couldn't load menu music: %s", SDL_GetError());
            } else {
                menu_music_track = MIX_CreateTrack(mixer);
                if (!menu_music_track || !MIX_SetTrackAudio(menu_music_track, menu_music)) {
                    SDL_Log("Couldn't prepare menu music track: %s", SDL_GetError());
                }
            }
            SDL_free(music_path);
        }
    }

    {
        char *tri_path = nullptr;
        SDL_asprintf(&tri_path, "%s../assets/ui/triangle.png", base);
        if (tri_path) {
            SDL_Surface *surface = SDL_LoadPNG(tri_path);
            if (!surface) {
                SDL_Log("Couldn't load triangle: %s", SDL_GetError());
            } else {
                triangle_tex = SDL_CreateTextureFromSurface(renderer, surface);
                SDL_DestroySurface(surface);
                if (triangle_tex) {
                    SDL_SetTextureScaleMode(triangle_tex, SDL_SCALEMODE_LINEAR);
                }
            }
            SDL_free(tri_path);
        }
    }

    {
        char *path = nullptr;
        SDL_asprintf(&path, "%s../assets/sfx/effect.mp3", base);
        if (path) {
            move_sfx = MIX_LoadAudio(mixer, path, true);
            SDL_free(path);
        }
        SDL_asprintf(&path, "%s../assets/sfx/confirm.mp3", base);
        if (path) {
            confirm_sfx = MIX_LoadAudio(mixer, path, true);
            SDL_free(path);
        }
    }

    SDL_free((void *)base);

    if (slide_count == 0) {
        SDL_Log("No slideshow images loaded from assets/img");
    } else {
        SDL_Log("Slideshow ready with %d image(s)", slide_count);
    }

    SDL_AddTimer(16, slideshow_wake_timer, nullptr);

    warning_start_ticks = SDL_GetTicks();
    return SDL_APP_CONTINUE;
}

static void start_menu_music(void);
static void draw_black_overlay(Uint8 alpha);
static void draw_blood_overlay(Uint8 alpha);
static float text_width(const char *text);
static float centered_x(float scaled_w, const char *text);
static void render_slideshow(void);

static void start_menu_music(void)
{
    if (menu_music_started || !menu_music_track) {
        return;
    }

    SDL_PropertiesID opts = SDL_CreateProperties();
    if (!opts) {
        SDL_Log("Couldn't create music play properties: %s", SDL_GetError());
        return;
    }

    SDL_SetNumberProperty(opts, MIX_PROP_PLAY_LOOPS_NUMBER, -1);
    SDL_SetNumberProperty(opts, MIX_PROP_PLAY_FADE_IN_MILLISECONDS_NUMBER, 1200);

    if (!MIX_PlayTrack(menu_music_track, opts)) {
        SDL_Log("Couldn't play menu music: %s", SDL_GetError());
    } else {
        MIX_SetTrackLoops(menu_music_track, -1);
        menu_music_started = true;
    }

    SDL_DestroyProperties(opts);
}

static void go_to_menu(void)
{
    screen = SCREEN_MENU;
    menu_visible = false;
    slideshow_start_ticks = SDL_GetTicks();
    menu_enter_ticks = slideshow_start_ticks;
    slideshow_slide_start = slideshow_start_ticks;
    reset_slideshow_order();
    start_menu_music();
}

static void go_to_load(void)
{
    screen = SCREEN_LOAD;
    load_option = 0;
    load_enter_ticks = SDL_GetTicks();
    menu_visible = false;
}

static void RenderPyramid(
    SDL_Renderer *rend,
    SDL_Texture *tex,
    float center_x,
    float center_y,
    float size,
    Uint64 now,
    float pitch_deg)
{
    const float rad = (((float)((int)(now % 2000))) / 2000.0f) * SDL_PI_F * 2.0f;
    const float cos_r = SDL_cosf(rad);
    const float sin_r = SDL_sinf(rad);

    float mat[9] = {
        cos_r, 0.0f, sin_r,
        0.0f, 1.0f, 0.0f,
        -sin_r, 0.0f, cos_r
    };

    const float pitch_rad = pitch_deg * (SDL_PI_F / 180.0f);
    const float cos_p = SDL_cosf(pitch_rad);
    const float sin_p = SDL_sinf(pitch_rad);

    const float raw_vertices[5][3] = {
        { 0.0f, -0.5f, 0.0f },
        { -0.5f, 0.5f, -0.5f },
        { 0.5f, 0.5f, -0.5f },
        { 0.5f, 0.5f, 0.5f },
        { -0.5f, 0.5f, 0.5f }
    };

    SDL_FPoint proj[5];
    for (int i = 0; i < 5; i++) {
        float rx = mat[0] * raw_vertices[i][0] + mat[1] * raw_vertices[i][1] + mat[2] * raw_vertices[i][2];
        float ry = mat[3] * raw_vertices[i][0] + mat[4] * raw_vertices[i][1] + mat[5] * raw_vertices[i][2];
        float rz = mat[6] * raw_vertices[i][0] + mat[7] * raw_vertices[i][1] + mat[8] * raw_vertices[i][2];
        proj[i].x = center_x + size * rx;
        proj[i].y = center_y + size * (ry * cos_p - rz * sin_p);
    }

    SDL_FColor color_blanco = {1.0f, 1.0f, 1.0f, 1.0f};
    const int faces[4][3] = {
        {0, 1, 2},
        {0, 2, 3},
        {0, 3, 4},
        {0, 4, 1}
    };

    for (int f = 0; f < 4; f++) {
        const int i0 = faces[f][0];
        const int i1 = faces[f][1];
        const int i2 = faces[f][2];
        const float cross_product =
            (proj[i1].x - proj[i0].x) * (proj[i2].y - proj[i0].y) -
            (proj[i1].y - proj[i0].y) * (proj[i2].x - proj[i0].x);
        if (cross_product <= 0) {
            continue;
        }

        SDL_Vertex vertices[3] = {
            {proj[i0], color_blanco, {0.5f, 0.0f}},
            {proj[i1], color_blanco, {0.0f, 1.0f}},
            {proj[i2], color_blanco, {1.0f, 1.0f}}
        };
        SDL_RenderGeometry(rend, tex, vertices, 3, NULL, 0);
    }
}

static void render_load_game(void)
{
    const Uint64 now = SDL_GetTicks();
    const float base_px = SDL_min(WINDOW_WIDTH, WINDOW_HEIGHT) / SDL_sqrtf(15.0f);
    const float positions[3] = {
        0.20f * WINDOW_WIDTH,
        0.50f * WINDOW_WIDTH,
        0.80f * WINDOW_WIDTH
    };

    render_slideshow();
    draw_blood_overlay(150);

    if (triangle_tex) {
        for (int i = 0; i < FILE_COUNT; ++i) {
            const Uint64 spin = (i == load_option) ? now : 0;
            RenderPyramid(
                renderer,
                triangle_tex,
                positions[i],
                0.48f * WINDOW_HEIGHT,
                base_px,
                spin,
                -30.0f);
        }
    }

    const float scale = 2.5f;
    SDL_SetRenderScale(renderer, scale, scale);
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, SDL_ALPHA_OPAQUE);

    const char *title = "Load Game";
    SDL_RenderDebugText(
        renderer,
        centered_x(WINDOW_WIDTH / scale, title),
        (0.12f * WINDOW_HEIGHT) / scale,
        title);

    for (int i = 0; i < FILE_COUNT; ++i) {
        const float label_x =
            (positions[i] / scale) - (text_width(FILE_LABELS[i]) * 0.5f);
        const float label_y = (0.28f * WINDOW_HEIGHT) / scale;
        SDL_RenderDebugText(renderer, label_x, label_y, FILE_LABELS[i]);

        const float empty_x =
            (positions[i] / scale) - (text_width(FILE_EMPTY_LABEL) * 0.5f);
        const float empty_y = (0.68f * WINDOW_HEIGHT) / scale;
        SDL_SetRenderDrawColor(renderer, 180, 180, 180, SDL_ALPHA_OPAQUE);
        SDL_RenderDebugText(renderer, empty_x, empty_y, FILE_EMPTY_LABEL);
        SDL_SetRenderDrawColor(renderer, 255, 255, 255, SDL_ALPHA_OPAQUE);
    }

    if (load_option >= 0 && load_option < FILE_COUNT) {
        const float marker_x =
            (positions[load_option] / scale) - (DEBUG_FONT_W * 0.5f);
        const float marker_y = (0.82f * WINDOW_HEIGHT) / scale;
        const float time = SDL_GetTicks() / 500.0f;
        const float factor = (sin(time) + 1.0f) / 2.0f;
        const Uint8 color_val = static_cast<Uint8>(150 + (105 * factor));
        SDL_SetRenderDrawColor(renderer, color_val, color_val, color_val, SDL_ALPHA_OPAQUE);
        SDL_RenderDebugText(renderer, marker_x, marker_y, "^");
    }

    SDL_SetRenderScale(renderer, 1.0f, 1.0f);

    const char *hint = "LEFT/RIGHT  ENTER=Load  ESC=Back";
    SDL_SetRenderDrawColor(renderer, 255, 220, 220, SDL_ALPHA_OPAQUE);
    SDL_RenderDebugText(
        renderer,
        centered_x((float)WINDOW_WIDTH, hint),
        WINDOW_HEIGHT - 24.0f,
        hint);

    const Uint64 fade_duration = 900;
    const Uint64 since = now - load_enter_ticks;
    if (since < fade_duration) {
        const float progress = (float)since / (float)fade_duration;
        draw_blood_overlay((Uint8)((1.0f - progress) * 180.0f));
    }
}

static void draw_black_overlay(Uint8 alpha)
{
    if (alpha == 0) {
        return;
    }
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, alpha);
    SDL_FRect full = {0, 0, (float)WINDOW_WIDTH, (float)WINDOW_HEIGHT};
    SDL_RenderFillRect(renderer, &full);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
}

static void draw_blood_overlay(Uint8 alpha)
{
    if (alpha == 0) {
        return;
    }
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 120, 0, 10, alpha);
    SDL_FRect full = {0, 0, (float)WINDOW_WIDTH, (float)WINDOW_HEIGHT};
    SDL_RenderFillRect(renderer, &full);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
}

static int warning_total_chars(void)
{
    int total = 0;
    for (int i = 0; i < WARNING_DESC_COUNT; ++i) {
        total += (int)std::strlen(WARNING_DESC[i]);
    }
    return total;
}

static char warning_char_at(int index)
{
    for (int i = 0; i < WARNING_DESC_COUNT; ++i) {
        const int len = (int)std::strlen(WARNING_DESC[i]);
        if (index < len) {
            return WARNING_DESC[i][index];
        }
        index -= len;
    }
    return '\0';
}

static int warning_visible_chars(Uint64 elapsed)
{
    const int total_chars = warning_total_chars();
    if (elapsed >= WARNING_TYPE_MS || total_chars <= 0) {
        return total_chars;
    }
    int visible = (int)((elapsed * (Uint64)total_chars) / WARNING_TYPE_MS);
    if (visible > total_chars) {
        visible = total_chars;
    }
    return visible;
}

static void update_typewriter_sfx(int visible_chars)
{
    if (!mixer || !typewriter_sfx) {
        typewriter_last_char = visible_chars;
        return;
    }

    while (typewriter_last_char < visible_chars) {
        const char c = warning_char_at(typewriter_last_char);
        if (c != ' ' && c != '\0') {
            MIX_PlayAudio(mixer, typewriter_sfx);
        }
        ++typewriter_last_char;
    }
}

static void start_fade_out(void)
{
    if (!is_exiting) {
        is_exiting = true;
        fade_out_start_ticks = SDL_GetTicks();
    }
}

static float text_width(const char *text)
{
    return (float)(std::strlen(text) * DEBUG_FONT_W);
}

static float centered_x(float scaled_w, const char *text)
{
    return (scaled_w - text_width(text)) * 0.5f;
}

static float menu_block_left(float scaled_w)
{
    float max_w = 0.0f;
    for (int i = 0; i < MENU_COUNT; ++i) {
        const float w = text_width(MENU_OPTIONS[i]);
        if (w > max_w) {
            max_w = w;
        }
    }
    return (scaled_w - max_w) * 0.5f;
}

static SDL_FRect cover_dst(SDL_Texture *tex)
{
    float tw = 0.0f;
    float th = 0.0f;
    SDL_GetTextureSize(tex, &tw, &th);
    if (tw <= 0.0f || th <= 0.0f) {
        return SDL_FRect{0, 0, (float)WINDOW_WIDTH, (float)WINDOW_HEIGHT};
    }

    const float scale = SDL_max(WINDOW_WIDTH / tw, WINDOW_HEIGHT / th);
    const float w = tw * scale;
    const float h = th * scale;
    return SDL_FRect{
        (WINDOW_WIDTH - w) * 0.5f,
        (WINDOW_HEIGHT - h) * 0.5f,
        w,
        h
    };
}

static void update_slideshow(void)
{
    if (slide_count <= 1) {
        return;
    }

    const Uint64 now = SDL_GetTicks();
    while (now - slideshow_slide_start >= SLIDE_DURATION_MS) {
        slideshow_slide_start += SLIDE_DURATION_MS;
        slideshow_index = slideshow_next_index;
        slideshow_next_index = pick_random_slide(slideshow_index);
    }
}

static void render_slideshow(void)
{
    if (slide_count <= 0) {
        return;
    }

    update_slideshow();

    const Uint64 local = SDL_GetTicks() - slideshow_slide_start;
    const int current = slideshow_index;
    const int next = slideshow_next_index;

    float alpha_current = 1.0f;
    float alpha_next = 0.0f;

    if (slide_count > 1 && local >= SLIDE_DURATION_MS - SLIDE_FADE_MS) {
        const float t = (float)(local - (SLIDE_DURATION_MS - SLIDE_FADE_MS)) / (float)SLIDE_FADE_MS;
        alpha_current = 1.0f - t;
        alpha_next = t;
    }

    for (int i = 0; i < slide_count; ++i) {
        SDL_SetTextureAlphaMod(slides[i], SDL_ALPHA_OPAQUE);
    }

    SDL_Texture *cur = slides[current];
    SDL_FRect dst = cover_dst(cur);
    SDL_SetTextureAlphaMod(cur, (Uint8)(alpha_current * 255.0f));
    SDL_RenderTexture(renderer, cur, nullptr, &dst);

    if (alpha_next > 0.0f) {
        SDL_Texture *nxt = slides[next];
        SDL_FRect ndst = cover_dst(nxt);
        SDL_SetTextureAlphaMod(nxt, (Uint8)(alpha_next * 255.0f));
        SDL_RenderTexture(renderer, nxt, nullptr, &ndst);
    }
}

SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event)
{
    if (event->type == SDL_EVENT_QUIT) {
        return SDL_APP_SUCCESS;
    }

    if (is_exiting) {
        return SDL_APP_CONTINUE;
    }

    const bool key_down = event->type == SDL_EVENT_KEY_DOWN && !event->key.repeat;
    const bool click =
        event->type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
        event->button.button == SDL_BUTTON_LEFT;

    if (!key_down && !click) {
        return SDL_APP_CONTINUE;
    }

    if (screen == SCREEN_WARNING) {
        if (key_down && event->key.scancode == SDL_SCANCODE_ESCAPE) {
            start_fade_out();
        }
        return SDL_APP_CONTINUE;
    }

    if (screen == SCREEN_LOAD) {
        if (!key_down) {
            return SDL_APP_CONTINUE;
        }

        switch (event->key.scancode) {
            case SDL_SCANCODE_ESCAPE:
                screen = SCREEN_MENU;
                menu_visible = true;
                break;

            case SDL_SCANCODE_LEFT:
            case SDL_SCANCODE_A:
                if (load_option > 0) {
                    --load_option;
                    if (move_sfx) {
                        MIX_PlayAudio(mixer, move_sfx);
                    }
                }
                break;

            case SDL_SCANCODE_RIGHT:
            case SDL_SCANCODE_D:
                if (load_option < FILE_COUNT - 1) {
                    ++load_option;
                    if (move_sfx) {
                        MIX_PlayAudio(mixer, move_sfx);
                    }
                }
                break;

            case SDL_SCANCODE_SPACE:
            case SDL_SCANCODE_RETURN:
                if (confirm_sfx) {
                    MIX_PlayAudio(mixer, confirm_sfx);
                }
                SDL_Log("Loading %s", FILE_LABELS[load_option]);
                break;

            default:
                break;
        }
        return SDL_APP_CONTINUE;
    }

    if (key_down && event->key.scancode == SDL_SCANCODE_ESCAPE) {
        if (menu_visible) {
            menu_visible = false;
        } else {
            start_fade_out();
        }
        return SDL_APP_CONTINUE;
    }

    if (!menu_visible) {
        menu_visible = true;
        return SDL_APP_CONTINUE;
    }

    if (!key_down) {
        return SDL_APP_CONTINUE;
    }

    switch (event->key.scancode) {
        case SDL_SCANCODE_DOWN:
        case SDL_SCANCODE_S:
            option = (option + 1) % MENU_COUNT;
            break;

        case SDL_SCANCODE_UP:
        case SDL_SCANCODE_W:
            option = (option - 1 + MENU_COUNT) % MENU_COUNT;
            break;

        case SDL_SCANCODE_SPACE:
        case SDL_SCANCODE_RETURN:
            SDL_Log("Selected: %s", MENU_OPTIONS[option]);
            if (option == 2) {
                go_to_load();
            } else if (option == MENU_COUNT - 1) {
                start_fade_out();
            }
            break;

        default:
            break;
    }

    return SDL_APP_CONTINUE;
}

static void render_warning(float scaled_w, float scaled_h)
{
    const Uint64 elapsed = SDL_GetTicks() - warning_start_ticks;
    const int total_chars = warning_total_chars();
    const int visible_chars = warning_visible_chars(elapsed);
    update_typewriter_sfx(visible_chars);

    const float line_gap = scaled_h * 0.10f;
    const float title_y = scaled_h * 0.32f;
    float y = title_y + line_gap * 1.4f;

    SDL_RenderDebugText(
        renderer,
        centered_x(scaled_w, WARNING_TITLE),
        title_y,
        WARNING_TITLE);

    int remaining = visible_chars;
    char buffer[128];
    const bool show_cursor = (visible_chars < total_chars) && ((elapsed / 250) % 2 == 0);

    for (int i = 0; i < WARNING_DESC_COUNT; ++i) {
        const int line_len = (int)std::strlen(WARNING_DESC[i]);
        int show = 0;
        bool cursor_here = false;

        if (remaining > 0) {
            if (remaining < line_len) {
                show = remaining;
                cursor_here = true;
                remaining = 0;
            } else {
                show = line_len;
                remaining -= line_len;
            }
        } else if (visible_chars == 0 && i == 0) {
            cursor_here = true;
        }

        if (show > 0) {
            SDL_memcpy(buffer, WARNING_DESC[i], (size_t)show);
            buffer[show] = '\0';
            SDL_RenderDebugText(
                renderer,
                centered_x(scaled_w, WARNING_DESC[i]),
                y,
                buffer);
        }

        if (show_cursor && cursor_here) {
            const float cx = centered_x(scaled_w, WARNING_DESC[i]) + (float)(show * DEBUG_FONT_W);
            SDL_RenderDebugText(renderer, cx, y, "_");
        }

        y += line_gap;
    }
}

static void render_menu_options(float scaled_w, float scaled_h)
{
    draw_blood_overlay(150);

    const float scale = 3.0f;
    SDL_SetRenderScale(renderer, scale, scale);
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, SDL_ALPHA_OPAQUE);

    const char *title = "The island";
    SDL_RenderDebugText(renderer, centered_x(scaled_w, title), scaled_h * 0.18f, title);

    const float left_x = menu_block_left(scaled_w);
    const float first_y = scaled_h * 0.42f;
    const float line_gap = scaled_h * 0.10f;

    for (int i = 0; i < MENU_COUNT; ++i) {
        const float y = first_y + i * line_gap;
        SDL_RenderDebugText(renderer, left_x, y, MENU_OPTIONS[i]);
        if (i == option) {
            SDL_RenderDebugText(renderer, left_x - (DEBUG_FONT_W * 2), y, ">");
        }
    }

    SDL_SetRenderScale(renderer, 1.0f, 1.0f);
}

static void draw_version_label(void)
{
    const char *version = "version-beta";
    const float pad = 8.0f;
    const float x = (float)WINDOW_WIDTH - text_width(version) - pad;
    const float y = (float)WINDOW_HEIGHT - DEBUG_FONT_W - pad;
    SDL_SetRenderScale(renderer, 1.0f, 1.0f);
    SDL_SetRenderDrawColor(renderer, 220, 220, 220, SDL_ALPHA_OPAQUE);
    SDL_RenderDebugText(renderer, x, y, version);
}

SDL_AppResult SDL_AppIterate(void *appstate)
{
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, SDL_ALPHA_OPAQUE);
    SDL_RenderClear(renderer);

    if (screen == SCREEN_WARNING) {
        const Uint64 elapsed = SDL_GetTicks() - warning_start_ticks;
        const Uint64 pause_end = WARNING_TYPE_MS + WARNING_PAUSE_MS;
        const Uint64 fade_end = pause_end + WARNING_FADE_OUT_MS;
        if (elapsed >= fade_end) {
            go_to_menu();
        }
    }

    if (screen == SCREEN_WARNING) {
        const Uint64 elapsed = SDL_GetTicks() - warning_start_ticks;
        const Uint64 pause_end = WARNING_TYPE_MS + WARNING_PAUSE_MS;

        const float scale = 2.0f;
        const float scaled_w = WINDOW_WIDTH / scale;
        const float scaled_h = WINDOW_HEIGHT / scale;
        SDL_SetRenderScale(renderer, scale, scale);
        SDL_SetRenderDrawColor(renderer, 255, 255, 255, SDL_ALPHA_OPAQUE);
        render_warning(scaled_w, scaled_h);
        SDL_SetRenderScale(renderer, 1.0f, 1.0f);

        if (elapsed >= pause_end) {
            const float t = (float)(elapsed - pause_end) / (float)WARNING_FADE_OUT_MS;
            draw_black_overlay((Uint8)(SDL_min(t, 1.0f) * 255.0f));
        }
    } else if (screen == SCREEN_LOAD) {
        render_load_game();
    } else {
        render_slideshow();

        const Uint64 since_menu = SDL_GetTicks() - menu_enter_ticks;
        if (since_menu < MENU_FADE_IN_MS) {
            const float t = 1.0f - ((float)since_menu / (float)MENU_FADE_IN_MS);
            draw_black_overlay((Uint8)(t * 255.0f));
        }

        if (menu_visible) {
            const float scale = 3.0f;
            render_menu_options(WINDOW_WIDTH / scale, WINDOW_HEIGHT / scale);
        }
    }

    if (screen != SCREEN_WARNING) {
        draw_version_label();
    }

    if (is_exiting) {
        const Uint64 elapsed = SDL_GetTicks() - fade_out_start_ticks;
        if (elapsed >= FADE_OUT_DURATION) {
            return SDL_APP_SUCCESS;
        }
        const float t = (float)elapsed / (float)FADE_OUT_DURATION;
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, (Uint8)(t * 255.0f));
        SDL_FRect full = {0, 0, (float)WINDOW_WIDTH, (float)WINDOW_HEIGHT};
        SDL_RenderFillRect(renderer, &full);
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
    }

    SDL_RenderPresent(renderer);
    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void *appstate, SDL_AppResult result)
{
    if (triangle_tex) {
        SDL_DestroyTexture(triangle_tex);
        triangle_tex = nullptr;
    }
    for (int i = 0; i < slide_count; ++i) {
        SDL_DestroyTexture(slides[i]);
        slides[i] = nullptr;
    }
    slide_count = 0;

    if (menu_music_track) {
        MIX_DestroyTrack(menu_music_track);
        menu_music_track = nullptr;
    }
    if (menu_music) {
        MIX_DestroyAudio(menu_music);
        menu_music = nullptr;
    }
    if (typewriter_sfx) {
        MIX_DestroyAudio(typewriter_sfx);
        typewriter_sfx = nullptr;
    }
    if (move_sfx) {
        MIX_DestroyAudio(move_sfx);
        move_sfx = nullptr;
    }
    if (confirm_sfx) {
        MIX_DestroyAudio(confirm_sfx);
        confirm_sfx = nullptr;
    }
    if (mixer) {
        MIX_DestroyMixer(mixer);
        mixer = nullptr;
    }
    MIX_Quit();
}
