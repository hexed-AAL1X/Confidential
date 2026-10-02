#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3_mixer/SDL_mixer.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include "Game.hpp"

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
static SDL_Texture *logo_tex = NULL;
static SDL_TimerID wake_timer = 0;

static const char *const BG_FILES[] = {
    "isla_pano1.png",
    "isla_pano2.png"
};
static const int BG_COUNT = 2;
static SDL_Texture *bg_tex[BG_COUNT] = {NULL, NULL};
static const Uint64 BG_SHOW_MS = 7000;
static const Uint64 BG_FADE_MS = 1500;
static const Uint8 BG_DARKEN = 150;

// 360 orbit around the island: frames stay PNG-compressed in RAM and only the visible one is decoded.
static const int ORBIT_MAX_FRAMES = 720;
static const int ORBIT_FPS = 10;
static const Uint8 ORBIT_DARKEN = 60;
static int orbit_count = 0;
static void *orbit_png[ORBIT_MAX_FRAMES] = {};
static size_t orbit_png_size[ORBIT_MAX_FRAMES] = {};
static SDL_Texture *orbit_tex = NULL;
static int orbit_shown = -1;

enum Screen {
    SCREEN_WARNING,
    SCREEN_MENU,
    SCREEN_LOAD,
    SCREEN_OPTIONS,
    SCREEN_NEW,
    SCREEN_GAME
};

static Screen screen = SCREEN_WARNING;
static int option = 0;
static int load_option = 0;
static int options_option = 0;
static int difficulty_option = 2;
static Uint64 new_enter_ticks = 0;
static Uint64 game_last_ns = 0;
static bool menu_visible = false;
static bool is_exiting = false;
static Uint64 fade_out_start_ticks = 0;
static Uint64 warning_start_ticks = 0;
static Uint64 menu_enter_ticks = 0;
static Uint64 load_enter_ticks = 0;
static const Uint64 FADE_OUT_DURATION = 1500;
static const Uint64 WARNING_TYPE_MS = 2200;
static const Uint64 WARNING_PAUSE_MS = 3000;
static const Uint64 WARNING_FADE_OUT_MS = 900;
static const Uint64 MENU_FADE_IN_MS = 1200;

#define DEBUG_FONT_W 8

struct Resolution {
    int w;
    int h;
    const char *label;
};

static const Resolution RESOLUTIONS[] = {
    {640, 480, "640x480"},
    {960, 540, "960x540"},
    {1280, 720, "1280x720"},
    {1920, 1080, "1920x1080"},
};
static const int RESOLUTION_COUNT = (int)(sizeof(RESOLUTIONS) / sizeof(RESOLUTIONS[0]));
static int resolution_index = 3;
static int logical_w = 1920;
static int logical_h = 1080;

static const char *const MENU_OPTIONS[] = {
    "JUEGA POR TU CUENTA",
    "JUEGA CON AMIGOS",
    "COSAS DE MIERDA",
    "RECOMENZAR",
    "SAL DE AQUI"
};
static const int MENU_COUNT = 5;

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

struct Difficulty {
    const char *name;
    const char *level;
};

/* SDL_RenderDebugText only draws ASCII, so no accents. */
static const Difficulty DIFFICULTIES[] = {
    {"ERES UN TONTO", "MUY FACIL"},
    {"ERES UN TARADO", "FACIL"},
    {"COME MIERDA", "NORMAL"},
    {"TREMENDO PERDEDOR", "DIFICIL"},
    {"ERES EL MAS TONTO", "MUY DIFICIL"},
};
static const int DIFFICULTY_COUNT = (int)(sizeof(DIFFICULTIES) / sizeof(DIFFICULTIES[0]));

static float text_width(const char *text);
static float centered_x(float scaled_w, const char *text);
static void draw_black_overlay(Uint8 alpha);
static void load_orbit(const char *base);
static void render_logo(void);
static void apply_resolution(int index);

static float ui_text_scale(void)
{
    const float s = (float)logical_h / 360.0f;
    if (s < 1.0f) {
        return 1.0f;
    }
    if (s > 5.0f) {
        return 5.0f;
    }
    return s;
}

static Uint32 SDLCALL menu_wake_timer(void *userdata, SDL_TimerID timer_id, Uint32 interval)
{
    (void)userdata;
    (void)timer_id;
    SDL_Event event;
    SDL_zero(event);
    event.type = SDL_EVENT_USER;
    SDL_PushEvent(&event);
    return interval;
}

static void apply_resolution(int index)
{
    if (index < 0 || index >= RESOLUTION_COUNT) {
        return;
    }
    resolution_index = index;
    logical_w = RESOLUTIONS[index].w;
    logical_h = RESOLUTIONS[index].h;

    if (renderer) {
        SDL_SetRenderLogicalPresentation(
            renderer,
            logical_w,
            logical_h,
            SDL_LOGICAL_PRESENTATION_STRETCH);
    }
    if (window) {
        SDL_SetWindowFullscreen(window, true);
    }
    SDL_Log("Resolution set to %s", RESOLUTIONS[index].label);
}

SDL_AppResult SDL_AppInit(void **appstate, int argc, char *argv[])
{
    (void)appstate;
    (void)argc;
    (void)argv;

    SDL_SetAppMetadata("CONFIDENTIAL", "1.0", "com.confidential.menu");
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

    logical_w = RESOLUTIONS[resolution_index].w;
    logical_h = RESOLUTIONS[resolution_index].h;

    if (!SDL_CreateWindowAndRenderer(
            "CONFIDENTIAL",
            logical_w,
            logical_h,
            SDL_WINDOW_FULLSCREEN,
            &window,
            &renderer)) {
        SDL_Log("Couldn't create window/renderer: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    SDL_HideCursor();

    const char *base = SDL_GetBasePath();
    if (!base) {
        SDL_Log("Couldn't get base path: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    {
        char *logo_path = nullptr;
        SDL_asprintf(&logo_path, "%sassets/ui/Logo.png", base);
        if (logo_path) {
            SDL_Surface *surface = SDL_LoadPNG(logo_path);
            if (!surface) {
                SDL_Log("Couldn't load logo: %s", SDL_GetError());
            } else {
                logo_tex = SDL_CreateTextureFromSurface(renderer, surface);
                SDL_DestroySurface(surface);
                if (logo_tex) {
                    SDL_SetTextureScaleMode(logo_tex, SDL_SCALEMODE_NEAREST);
                    SDL_SetTextureBlendMode(logo_tex, SDL_BLENDMODE_BLEND);
                }
            }
            SDL_free(logo_path);
        }
    }

    for (int i = 0; i < BG_COUNT; ++i) {
        char *bg_path = nullptr;
        SDL_asprintf(&bg_path, "%sassets/img/%s", base, BG_FILES[i]);
        if (!bg_path) {
            continue;
        }
        SDL_Surface *surface = SDL_LoadPNG(bg_path);
        SDL_free(bg_path);
        if (!surface) {
            SDL_Log("Couldn't load background %s: %s", BG_FILES[i], SDL_GetError());
            continue;
        }
        bg_tex[i] = SDL_CreateTextureFromSurface(renderer, surface);
        SDL_DestroySurface(surface);
        if (bg_tex[i]) {
            SDL_SetTextureScaleMode(bg_tex[i], SDL_SCALEMODE_NEAREST);
            SDL_SetTextureBlendMode(bg_tex[i], SDL_BLENDMODE_BLEND);
        }
    }

    load_orbit(base);

    apply_resolution(resolution_index);

    {
        char *sfx_path = nullptr;
        SDL_asprintf(&sfx_path, "%sassets/sfx/typewriter.wav", base);
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
            "%sassets/sfx/Spazzmatica-Polka(chosic.com).mp3",
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
        SDL_asprintf(&tri_path, "%sassets/ui/triangle.png", base);
        if (tri_path) {
            SDL_Surface *surface = SDL_LoadPNG(tri_path);
            if (!surface) {
                SDL_Log("Couldn't load triangle: %s", SDL_GetError());
            } else {
                triangle_tex = SDL_CreateTextureFromSurface(renderer, surface);
                SDL_DestroySurface(surface);
                if (triangle_tex) {
                    SDL_SetTextureScaleMode(triangle_tex, SDL_SCALEMODE_NEAREST);
                }
            }
            SDL_free(tri_path);
        }
    }

    {
        char *path = nullptr;
        SDL_asprintf(&path, "%sassets/sfx/effect.mp3", base);
        if (path) {
            move_sfx = MIX_LoadAudio(mixer, path, true);
            SDL_free(path);
        }
        SDL_asprintf(&path, "%sassets/sfx/confirm.mp3", base);
        if (path) {
            confirm_sfx = MIX_LoadAudio(mixer, path, true);
            SDL_free(path);
        }
    }

    wake_timer = SDL_AddTimer(16, menu_wake_timer, nullptr);
    warning_start_ticks = SDL_GetTicks();
    return SDL_APP_CONTINUE;
}

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
    menu_enter_ticks = SDL_GetTicks();
    start_menu_music();
}

static void go_to_load(void)
{
    screen = SCREEN_LOAD;
    load_option = 0;
    load_enter_ticks = SDL_GetTicks();
    menu_visible = false;
}

static void go_to_new(void)
{
    screen = SCREEN_NEW;
    difficulty_option = 2;
    new_enter_ticks = SDL_GetTicks();
    menu_visible = false;
}

static void go_to_options(void)
{
    screen = SCREEN_OPTIONS;
    options_option = 0;
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
    /* Match triangle.png content bbox (896x816 on 1024): height/base ~= 0.9107 */
    const float half_base = 0.5f;
    const float face_h = (816.0f / 896.0f) * (2.0f * half_base);
    const float apex_rise = SDL_sqrtf((face_h * face_h) - (half_base * half_base));
    const float ay = -0.5f * apex_rise;
    const float by = 0.5f * apex_rise;

    /* UVs clipped to the drawn triangle (not the full square canvas). */
    const float u0 = 64.0f / 1024.0f;
    const float u1 = 960.0f / 1024.0f;
    const float v0 = 72.0f / 1024.0f;
    const float v1 = 888.0f / 1024.0f;
    const float u_mid = 0.5f * (u0 + u1);

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
        { 0.0f, ay, 0.0f },
        { -half_base, by, -half_base },
        {  half_base, by, -half_base },
        {  half_base, by,  half_base },
        { -half_base, by,  half_base }
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
            {proj[i0], color_blanco, {u_mid, v0}},
            {proj[i1], color_blanco, {u0, v1}},
            {proj[i2], color_blanco, {u1, v1}}
        };
        SDL_RenderGeometry(rend, tex, vertices, 3, NULL, 0);
    }
}

static void render_logo(void)
{
    if (!logo_tex) {
        return;
    }

    float tw = 0.0f;
    float th = 0.0f;
    SDL_GetTextureSize(logo_tex, &tw, &th);
    if (tw <= 0.0f || th <= 0.0f) {
        return;
    }

    const float max_w = (float)logical_w * 0.38f;
    const float max_h = (float)logical_h * 0.11f;
    float fit = max_w / tw;
    if (th * fit > max_h) {
        fit = max_h / th;
    }
    const float lw = tw * fit;
    const float lh = th * fit;
    const float lx = ((float)logical_w - lw) * 0.5f;
    const float ly = (float)logical_h * 0.12f;
    SDL_FRect dst = {lx, ly, lw, lh};
    SDL_RenderTexture(renderer, logo_tex, nullptr, &dst);

    const float beta_scale = ui_text_scale();
    SDL_SetRenderScale(renderer, beta_scale, beta_scale);
    SDL_SetRenderDrawColor(renderer, 220, 28, 28, SDL_ALPHA_OPAQUE);
    const char *beta = "BETA";
    const float bx = (lx + lw) / beta_scale - text_width(beta) + 1.0f;
    const float by = (ly + lh) / beta_scale + 1.0f;
    SDL_RenderDebugText(renderer, bx, by, beta);
    SDL_SetRenderScale(renderer, 1.0f, 1.0f);
}

static void draw_bg_texture(SDL_Texture *tex, Uint8 alpha)
{
    float tw = 0.0f;
    float th = 0.0f;
    if (!tex || !SDL_GetTextureSize(tex, &tw, &th) || tw <= 0.0f || th <= 0.0f) {
        return;
    }
    const float cover = SDL_max((float)logical_w / tw, (float)logical_h / th);
    const float dw = tw * cover;
    const float dh = th * cover;
    SDL_FRect dst = {((float)logical_w - dw) * 0.5f, ((float)logical_h - dh) * 0.5f, dw, dh};
    SDL_SetTextureAlphaMod(tex, alpha);
    SDL_RenderTexture(renderer, tex, nullptr, &dst);
}

static void load_orbit(const char *base)
{
    for (orbit_count = 0; orbit_count < ORBIT_MAX_FRAMES; ++orbit_count) {
        char *path = nullptr;
        SDL_asprintf(&path, "%sassets/img/orbit/orbit_%03d.png", base, orbit_count);
        orbit_png[orbit_count] = path ? SDL_LoadFile(path, &orbit_png_size[orbit_count]) : nullptr;
        SDL_free(path);
        if (!orbit_png[orbit_count]) {
            break;
        }
    }
    if (orbit_count > 0) {
        SDL_Log("Loaded %d orbit background frames", orbit_count);
    }
}

static void show_orbit_frame(int frame)
{
    SDL_Surface *png = SDL_LoadPNG_IO(SDL_IOFromConstMem(orbit_png[frame], orbit_png_size[frame]), true);
    if (!png) {
        return;
    }
    SDL_Surface *rgba = SDL_ConvertSurface(png, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(png);
    if (!rgba) {
        return;
    }
    if (!orbit_tex) {
        orbit_tex = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING, rgba->w, rgba->h);
        if (orbit_tex) {
            SDL_SetTextureScaleMode(orbit_tex, SDL_SCALEMODE_NEAREST);
            SDL_SetTextureBlendMode(orbit_tex, SDL_BLENDMODE_BLEND);
        }
    }
    if (orbit_tex) {
        SDL_UpdateTexture(orbit_tex, nullptr, rgba->pixels, rgba->pitch);
        orbit_shown = frame;
    }
    SDL_DestroySurface(rgba);
}

static void render_background(void)
{
    if (orbit_count > 0) {
        const int frame = (int)((SDL_GetTicks() * ORBIT_FPS / 1000) % (Uint64)orbit_count);
        if (frame != orbit_shown) {
            show_orbit_frame(frame);
        }
        if (orbit_tex) {
            draw_bg_texture(orbit_tex, 255);
            draw_black_overlay(ORBIT_DARKEN);
            return;
        }
    }

    const Uint64 cycle = BG_SHOW_MS + BG_FADE_MS;
    const Uint64 t = SDL_GetTicks();
    const int current = (int)((t / cycle) % BG_COUNT);
    const int next = (current + 1) % BG_COUNT;
    const Uint64 phase = t % cycle;

    draw_bg_texture(bg_tex[current], 255);
    if (phase > BG_SHOW_MS) {
        const float f = (float)(phase - BG_SHOW_MS) / (float)BG_FADE_MS;
        draw_bg_texture(bg_tex[next], (Uint8)(f * 255.0f));
    }
    draw_black_overlay(BG_DARKEN);
}

static void draw_black_overlay(Uint8 alpha)
{
    if (alpha == 0) {
        return;
    }
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, alpha);
    SDL_FRect full = {0, 0, (float)logical_w, (float)logical_h};
    SDL_RenderFillRect(renderer, &full);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
}

static float text_width(const char *text)
{
    return (float)(std::strlen(text) * DEBUG_FONT_W);
}

static float centered_x(float scaled_w, const char *text)
{
    return (scaled_w - text_width(text)) * 0.5f;
}

static void render_load_game(void)
{
    const Uint64 now = SDL_GetTicks();
    /* Bigger on-screen pyramids; texture uses NEAREST for crisp HD pixels. */
    const float base_px = SDL_min((float)logical_w, (float)logical_h) / SDL_sqrtf(8.0f);
    const float positions[3] = {
        0.20f * logical_w,
        0.50f * logical_w,
        0.80f * logical_w
    };

    render_logo();

    if (triangle_tex) {
        for (int i = 0; i < FILE_COUNT; ++i) {
            const Uint64 spin = (i == load_option) ? now : 0;
            RenderPyramid(
                renderer,
                triangle_tex,
                positions[i],
                0.48f * logical_h,
                base_px,
                spin,
                -30.0f);
        }
    }

    const float scale = ui_text_scale();
    SDL_SetRenderScale(renderer, scale, scale);
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, SDL_ALPHA_OPAQUE);

    const char *title = "RECOMENZAR";
    SDL_RenderDebugText(
        renderer,
        centered_x((float)logical_w / scale, title),
        (0.12f * logical_h) / scale,
        title);

    for (int i = 0; i < FILE_COUNT; ++i) {
        const float label_x =
            (positions[i] / scale) - (text_width(FILE_LABELS[i]) * 0.5f);
        const float label_y = (0.28f * logical_h) / scale;
        SDL_RenderDebugText(renderer, label_x, label_y, FILE_LABELS[i]);

        const float empty_x =
            (positions[i] / scale) - (text_width(FILE_EMPTY_LABEL) * 0.5f);
        const float empty_y = (0.78f * logical_h) / scale;
        SDL_SetRenderDrawColor(renderer, 160, 160, 160, SDL_ALPHA_OPAQUE);
        SDL_RenderDebugText(renderer, empty_x, empty_y, FILE_EMPTY_LABEL);
        SDL_SetRenderDrawColor(renderer, 255, 255, 255, SDL_ALPHA_OPAQUE);
    }

    if (load_option >= 0 && load_option < FILE_COUNT) {
        const float marker_x =
            (positions[load_option] / scale) - (DEBUG_FONT_W * 0.5f);
        const float marker_y = (0.88f * logical_h) / scale;
        const float time = SDL_GetTicks() / 500.0f;
        const float factor = (sin(time) + 1.0f) / 2.0f;
        const Uint8 color_val = static_cast<Uint8>(150 + (105 * factor));
        SDL_SetRenderDrawColor(renderer, color_val, color_val, color_val, SDL_ALPHA_OPAQUE);
        SDL_RenderDebugText(renderer, marker_x, marker_y, "^");
    }

    SDL_SetRenderScale(renderer, 1.0f, 1.0f);

    const char *hint = "LEFT/RIGHT  ENTER=Load  ESC=Back";
    SDL_SetRenderDrawColor(renderer, 180, 180, 180, SDL_ALPHA_OPAQUE);
    SDL_RenderDebugText(
        renderer,
        centered_x((float)logical_w, hint),
        (float)logical_h - 28.0f,
        hint);

    const Uint64 fade_duration = 900;
    const Uint64 since = now - load_enter_ticks;
    if (since < fade_duration) {
        const float progress = (float)since / (float)fade_duration;
        draw_black_overlay((Uint8)((1.0f - progress) * 180.0f));
    }
}

static void render_new_file(void)
{
    render_logo();

    const float scale = ui_text_scale();
    SDL_SetRenderScale(renderer, scale, scale);
    const float sw = (float)logical_w / scale;
    const float sh = (float)logical_h / scale;

    SDL_SetRenderDrawColor(renderer, 220, 28, 28, SDL_ALPHA_OPAQUE);
    const char *title = "SELECT DIFFICULTY";
    SDL_RenderDebugText(renderer, centered_x(sw, title), sh * 0.40f, title);

    float max_w = 0.0f;
    for (int i = 0; i < DIFFICULTY_COUNT; ++i) {
        max_w = SDL_max(max_w, text_width(DIFFICULTIES[i].name));
    }
    const float left_x = (sw - max_w) * 0.5f;
    const float first_y = sh * 0.50f;
    const float line_gap = sh * 0.065f;

    for (int i = 0; i < DIFFICULTY_COUNT; ++i) {
        const float y = first_y + i * line_gap;
        if (i == difficulty_option) {
            SDL_SetRenderDrawColor(renderer, 220, 28, 28, SDL_ALPHA_OPAQUE);
            SDL_RenderDebugText(renderer, left_x - (DEBUG_FONT_W * 2), y, ">");
        } else {
            SDL_SetRenderDrawColor(renderer, 255, 255, 255, SDL_ALPHA_OPAQUE);
        }
        SDL_RenderDebugText(renderer, left_x, y, DIFFICULTIES[i].name);
    }

    char level_line[48];
    std::snprintf(level_line, sizeof(level_line), "(%s)", DIFFICULTIES[difficulty_option].level);
    SDL_SetRenderDrawColor(renderer, 160, 160, 160, SDL_ALPHA_OPAQUE);
    SDL_RenderDebugText(
        renderer,
        centered_x(sw, level_line),
        first_y + DIFFICULTY_COUNT * line_gap + line_gap * 0.5f,
        level_line);

    SDL_SetRenderScale(renderer, 1.0f, 1.0f);

    const char *hint = "UP/DOWN  ENTER=Start  ESC=Back";
    SDL_SetRenderDrawColor(renderer, 140, 140, 140, SDL_ALPHA_OPAQUE);
    SDL_RenderDebugText(
        renderer,
        centered_x((float)logical_w, hint),
        (float)logical_h - 28.0f,
        hint);

    const Uint64 fade_duration = 900;
    const Uint64 since = SDL_GetTicks() - new_enter_ticks;
    if (since < fade_duration) {
        const float progress = (float)since / (float)fade_duration;
        draw_black_overlay((Uint8)((1.0f - progress) * 180.0f));
    }
}

static void render_options(void)
{
    render_logo();

    const float scale = ui_text_scale();
    SDL_SetRenderScale(renderer, scale, scale);
    const float sw = (float)logical_w / scale;
    const float sh = (float)logical_h / scale;

    SDL_SetRenderDrawColor(renderer, 220, 28, 28, SDL_ALPHA_OPAQUE);
    const char *title = "COSAS DE MIERDA";
    SDL_RenderDebugText(renderer, centered_x(sw, title), sh * 0.38f, title);

    char res_line[64];
    std::snprintf(
        res_line,
        sizeof(res_line),
        "RESOLUTION  < %s >",
        RESOLUTIONS[resolution_index].label);

    SDL_SetRenderDrawColor(renderer, 255, 255, 255, SDL_ALPHA_OPAQUE);
    const float y0 = sh * 0.52f;
    const float y1 = sh * 0.62f;

    SDL_RenderDebugText(renderer, centered_x(sw, res_line), y0, res_line);
    SDL_RenderDebugText(renderer, centered_x(sw, "BACK"), y1, "BACK");

    const float marker_y = (options_option == 0) ? y0 : y1;
    const char *row = (options_option == 0) ? res_line : "BACK";
    SDL_SetRenderDrawColor(renderer, 220, 28, 28, SDL_ALPHA_OPAQUE);
    SDL_RenderDebugText(
        renderer,
        centered_x(sw, row) - (DEBUG_FONT_W * 2),
        marker_y,
        ">");

    SDL_SetRenderScale(renderer, 1.0f, 1.0f);

    const char *hint = "LEFT/RIGHT=Resolution  ENTER  ESC=Back";
    SDL_SetRenderDrawColor(renderer, 140, 140, 140, SDL_ALPHA_OPAQUE);
    SDL_RenderDebugText(
        renderer,
        centered_x((float)logical_w, hint),
        (float)logical_h - 28.0f,
        hint);
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

static void play_move(void)
{
    if (move_sfx) {
        MIX_PlayAudio(mixer, move_sfx);
    }
}

static void play_confirm(void)
{
    if (confirm_sfx) {
        MIX_PlayAudio(mixer, confirm_sfx);
    }
}

SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event)
{
    (void)appstate;

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
                    play_move();
                }
                break;

            case SDL_SCANCODE_RIGHT:
            case SDL_SCANCODE_D:
                if (load_option < FILE_COUNT - 1) {
                    ++load_option;
                    play_move();
                }
                break;

            case SDL_SCANCODE_SPACE:
            case SDL_SCANCODE_RETURN:
                play_confirm();
                SDL_Log("Loading %s", FILE_LABELS[load_option]);
                break;

            default:
                break;
        }
        return SDL_APP_CONTINUE;
    }

    if (screen == SCREEN_GAME) {
        if (key_down && Game_Event(event)) {
            screen = SCREEN_MENU;
            menu_visible = true;
        }
        return SDL_APP_CONTINUE;
    }

    if (screen == SCREEN_NEW) {
        if (!key_down) {
            return SDL_APP_CONTINUE;
        }

        switch (event->key.scancode) {
            case SDL_SCANCODE_ESCAPE:
                screen = SCREEN_MENU;
                menu_visible = true;
                break;

            case SDL_SCANCODE_UP:
            case SDL_SCANCODE_W:
                difficulty_option = (difficulty_option - 1 + DIFFICULTY_COUNT) % DIFFICULTY_COUNT;
                play_move();
                break;

            case SDL_SCANCODE_DOWN:
            case SDL_SCANCODE_S:
                difficulty_option = (difficulty_option + 1) % DIFFICULTY_COUNT;
                play_move();
                break;

            case SDL_SCANCODE_SPACE:
            case SDL_SCANCODE_RETURN:
                play_confirm();
                SDL_Log(
                    "New file with difficulty: %s (%s)",
                    DIFFICULTIES[difficulty_option].name,
                    DIFFICULTIES[difficulty_option].level);
                {
                    char *sprite_path = nullptr;
                    SDL_asprintf(&sprite_path, "%sassets/sprites/player.png", SDL_GetBasePath());
                    if (sprite_path) {
                        Game_LoadSprites(renderer, sprite_path);
                        SDL_free(sprite_path);
                    }
                }
                Game_Start(DIFFICULTIES[difficulty_option].name);
                game_last_ns = SDL_GetTicksNS();
                screen = SCREEN_GAME;
                break;

            default:
                break;
        }
        return SDL_APP_CONTINUE;
    }

    if (screen == SCREEN_OPTIONS) {
        if (!key_down) {
            return SDL_APP_CONTINUE;
        }

        switch (event->key.scancode) {
            case SDL_SCANCODE_ESCAPE:
                screen = SCREEN_MENU;
                menu_visible = true;
                break;

            case SDL_SCANCODE_UP:
            case SDL_SCANCODE_W:
            case SDL_SCANCODE_DOWN:
            case SDL_SCANCODE_S:
                options_option = 1 - options_option;
                play_move();
                break;

            case SDL_SCANCODE_LEFT:
            case SDL_SCANCODE_A:
                if (options_option == 0) {
                    apply_resolution((resolution_index - 1 + RESOLUTION_COUNT) % RESOLUTION_COUNT);
                    play_move();
                }
                break;

            case SDL_SCANCODE_RIGHT:
            case SDL_SCANCODE_D:
                if (options_option == 0) {
                    apply_resolution((resolution_index + 1) % RESOLUTION_COUNT);
                    play_move();
                }
                break;

            case SDL_SCANCODE_SPACE:
            case SDL_SCANCODE_RETURN:
                play_confirm();
                if (options_option == 1) {
                    screen = SCREEN_MENU;
                    menu_visible = true;
                }
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
            play_move();
            break;

        case SDL_SCANCODE_UP:
        case SDL_SCANCODE_W:
            option = (option - 1 + MENU_COUNT) % MENU_COUNT;
            play_move();
            break;

        case SDL_SCANCODE_SPACE:
        case SDL_SCANCODE_RETURN:
            play_confirm();
            SDL_Log("Selected: %s", MENU_OPTIONS[option]);
            if (option == 0) {
                go_to_new();
            } else if (option == 2) {
                go_to_options();
            } else if (option == 3) {
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
    (void)scaled_w;
    (void)scaled_h;

    const float scale = ui_text_scale();
    SDL_SetRenderScale(renderer, scale, scale);

    const float sw = (float)logical_w / scale;
    const float sh = (float)logical_h / scale;

    float max_w = 0.0f;
    for (int i = 0; i < MENU_COUNT; ++i) {
        const float w = text_width(MENU_OPTIONS[i]);
        if (w > max_w) {
            max_w = w;
        }
    }
    const float left_x = (sw - max_w) * 0.5f;
    const float first_y = sh * 0.42f;
    const float line_gap = sh * 0.08f;

    for (int i = 0; i < MENU_COUNT; ++i) {
        const float y = first_y + i * line_gap;
        if (i == option) {
            SDL_SetRenderDrawColor(renderer, 220, 28, 28, SDL_ALPHA_OPAQUE);
            SDL_RenderDebugText(renderer, left_x - (DEBUG_FONT_W * 2), y, ">");
            SDL_RenderDebugText(renderer, left_x, y, MENU_OPTIONS[i]);
        } else {
            SDL_SetRenderDrawColor(renderer, 255, 255, 255, SDL_ALPHA_OPAQUE);
            SDL_RenderDebugText(renderer, left_x, y, MENU_OPTIONS[i]);
        }
    }

    SDL_SetRenderScale(renderer, 1.0f, 1.0f);
}

static void draw_version_label(void)
{
    const char *label = "v0.666";
    const float pad = 16.0f;
    const float x = (float)logical_w - text_width(label) - pad;
    const float y = (float)logical_h - DEBUG_FONT_W - pad;
    SDL_SetRenderScale(renderer, 1.0f, 1.0f);
    SDL_SetRenderDrawColor(renderer, 140, 140, 140, SDL_ALPHA_OPAQUE);
    SDL_RenderDebugText(renderer, x, y, label);
}

SDL_AppResult SDL_AppIterate(void *appstate)
{
    (void)appstate;

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

        const float scale = ui_text_scale();
        const float scaled_w = (float)logical_w / scale;
        const float scaled_h = (float)logical_h / scale;
        SDL_SetRenderScale(renderer, scale, scale);
        SDL_SetRenderDrawColor(renderer, 255, 255, 255, SDL_ALPHA_OPAQUE);
        render_warning(scaled_w, scaled_h);
        SDL_SetRenderScale(renderer, 1.0f, 1.0f);

        if (elapsed >= pause_end) {
            const float t = (float)(elapsed - pause_end) / (float)WARNING_FADE_OUT_MS;
            draw_black_overlay((Uint8)(SDL_min(t, 1.0f) * 255.0f));
        }
    } else if (screen == SCREEN_LOAD) {
        render_background();
        render_load_game();
    } else if (screen == SCREEN_OPTIONS) {
        render_background();
        render_options();
    } else if (screen == SCREEN_NEW) {
        render_background();
        render_new_file();
    } else if (screen == SCREEN_GAME) {
        const Uint64 now_ns = SDL_GetTicksNS();
        Game_Update((float)(now_ns - game_last_ns) / 1e9f);
        game_last_ns = now_ns;
        Game_Render(renderer, logical_w, logical_h);
    } else {
        render_background();
        render_logo();

        const Uint64 since_menu = SDL_GetTicks() - menu_enter_ticks;
        if (since_menu < MENU_FADE_IN_MS) {
            const float t = 1.0f - ((float)since_menu / (float)MENU_FADE_IN_MS);
            draw_black_overlay((Uint8)(t * 255.0f));
        }

        if (menu_visible) {
            const float scale = ui_text_scale();
            render_menu_options((float)logical_w / scale, (float)logical_h / scale);
        }
    }

    if (screen != SCREEN_WARNING && screen != SCREEN_GAME) {
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
        SDL_FRect full = {0, 0, (float)logical_w, (float)logical_h};
        SDL_RenderFillRect(renderer, &full);
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
    }

    SDL_RenderPresent(renderer);
    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void *appstate, SDL_AppResult result)
{
    (void)appstate;
    (void)result;

    if (wake_timer) {
        SDL_RemoveTimer(wake_timer);
        wake_timer = 0;
    }

    if (logo_tex) {
        SDL_DestroyTexture(logo_tex);
        logo_tex = nullptr;
    }
    if (triangle_tex) {
        SDL_DestroyTexture(triangle_tex);
        triangle_tex = nullptr;
    }
    for (int i = 0; i < BG_COUNT; ++i) {
        if (bg_tex[i]) {
            SDL_DestroyTexture(bg_tex[i]);
            bg_tex[i] = nullptr;
        }
    }
    if (orbit_tex) {
        SDL_DestroyTexture(orbit_tex);
        orbit_tex = nullptr;
    }
    for (int i = 0; i < orbit_count; ++i) {
        SDL_free(orbit_png[i]);
        orbit_png[i] = nullptr;
    }
    orbit_count = 0;

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
