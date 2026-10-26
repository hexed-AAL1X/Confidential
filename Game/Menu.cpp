#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3_mixer/SDL_mixer.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include "AssetPath.hpp"
#include "Game.hpp"
#include "Settings.hpp"
#include "Video.hpp"

static SDL_Window *window = NULL;
static SDL_Renderer *renderer = NULL;
static MIX_Mixer *mixer = NULL;
static MIX_Audio *typewriter_sfx = NULL;
static MIX_Audio *menu_music = NULL;
static MIX_Track *menu_music_track = NULL;
static MIX_Audio *move_sfx = NULL;
/* Music of the map being played (its "musica" property), streamed from assets/music. */
static MIX_Audio *game_music = NULL;
static MIX_Track *game_music_track = NULL;
static std::string game_music_file;
static MIX_Audio *confirm_sfx = NULL;
static bool menu_music_started = false;
static int typewriter_last_char = 0;
/* Own tracks (instead of MIX_PlayAudio) so the SFX volume slider can reach them. */
static const int SFX_TRACK_COUNT = 16;
static MIX_Track *sfx_tracks[SFX_TRACK_COUNT] = {};
static int sfx_next = 0;
static Uint64 fps_window_start = 0;
static int fps_frames = 0;
static int fps_value = 0;
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

// Orbit: approach flyover then idle loop. Frames stay PNG-compressed; only the visible one is decoded.
static const int ORBIT_MAX_FRAMES = 720;
static const int ORBIT_APPROACH_FPS = 12;
static const int ORBIT_IDLE_FPS = 8; /* gentler than approach, not sluggish */
static const Uint8 ORBIT_DARKEN = 28;
static int orbit_count = 0;
static int orbit_idle_start = 0;
static Uint64 orbit_play_start = 0;
static void *orbit_png[ORBIT_MAX_FRAMES] = {};
static size_t orbit_png_size[ORBIT_MAX_FRAMES] = {};
static SDL_Texture *orbit_tex = NULL;
static int orbit_shown = -1;

enum Screen {
    SCREEN_WARNING,
    SCREEN_MENU,
    SCREEN_LOAD,
    SCREEN_OPTIONS,
    SCREEN_CHARACTER,
    SCREEN_NEW,
    SCREEN_GAME
};

static Screen screen = SCREEN_WARNING;
static int option = 0;
static int load_option = 0;
static int options_option = 0;
static int options_root_option = 0;
static int binding_slot = 0;
static bool rebinding = false;
static bool reset_armed = false;
static Uint64 options_enter_ticks = 0;
static int difficulty_option = 2;
static Uint64 new_enter_ticks = 0;
static Uint64 character_enter_ticks = 0;
static Uint64 game_last_ns = 0;
static bool menu_visible = false;
static bool is_exiting = false;
static Uint64 fade_out_start_ticks = 0;
static Uint64 warning_start_ticks = 0;
static Uint64 menu_enter_ticks = 0;
static Uint64 load_enter_ticks = 0;
static const Uint64 FADE_OUT_DURATION = 1500;
static const Uint64 WARNING_TYPE_MS = 11000;
static const Uint64 WARNING_PAUSE_MS = 6500;
static const Uint64 WARNING_FADE_OUT_MS = 900;
static const Uint64 MENU_FADE_IN_MS = 1200;

#define DEBUG_FONT_W 8
/* Titulos de pantalla claramente debajo del logo + tag CENSURADO. */
static const float MENU_SCREEN_TITLE_Y = 0.40f;

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
static int logical_w = 1920;
static int logical_h = 1080;

static const char *const MENU_OPTIONS[] = {
    "NUEVA PARTIDA",
    "MULTIJUGADOR",
    "OPCIONES",
    "CARGAR PARTIDA",
    "SALIR"
};
static const int MENU_COUNT = 5;

/* Warning styles: STRONG = rojo oscuro + glow rojo claro; IMPORTANT = amarillo + glow naranja. */
enum WarnStyle {
    WARN_NORMAL,
    WARN_STRONG,
    WARN_IMPORTANT
};

struct WarnSeg {
    const char *text;
    WarnStyle style;
};

struct WarnLine {
    const WarnSeg *segs;
    int count;
};

/* SDL_RenderDebugText solo ASCII: sin tildes. */
static const WarnSeg WARN_L0[] = {{"ADVERTENCIA", WARN_STRONG}};
static const WarnSeg WARN_L1[] = {{"", WARN_NORMAL}};
static const WarnSeg WARN_L2[] = {
    {"Este videojuego contiene material, situaciones y representaciones", WARN_NORMAL}
};
static const WarnSeg WARN_L3[] = {
    {"completamente inapropiados", WARN_STRONG},
    {", de ", WARN_NORMAL},
    {"dudoso gusto", WARN_STRONG},
    {" y ", WARN_NORMAL},
    {"potencialmente", WARN_STRONG}
};
static const WarnSeg WARN_L4[] = {
    {"ofensivos", WARN_STRONG},
    {". ", WARN_NORMAL},
    {"No se recomienda", WARN_IMPORTANT},
    {" para ", WARN_NORMAL},
    {"menores de edad", WARN_STRONG},
    {", personas que", WARN_NORMAL}
};
static const WarnSeg WARN_L5[] = {
    {"aspiren a desarrollar una carrera profesional respetable ni para", WARN_NORMAL}
};
static const WarnSeg WARN_L6[] = {
    {"quienes deseen conservar una ", WARN_NORMAL},
    {"salud mental", WARN_IMPORTANT},
    {" acorde con las", WARN_NORMAL}
};
static const WarnSeg WARN_L7[] = {{"convenciones sociales.", WARN_NORMAL}};
static const WarnSeg WARN_L8[] = {{"", WARN_NORMAL}};
static const WarnSeg WARN_L9[] = {
    {"Asimismo, se desaconseja su consumo a toda persona comprometida", WARN_NORMAL}
};
static const WarnSeg WARN_L10[] = {
    {"con el cumplimiento estricto de las ", WARN_NORMAL},
    {"normas del Estado", WARN_IMPORTANT},
    {", las ", WARN_NORMAL},
    {"leyes", WARN_IMPORTANT}
};
static const WarnSeg WARN_L11[] = {
    {"de la ", WARN_NORMAL},
    {"Republica del Peru", WARN_IMPORTANT},
    {" y las ", WARN_NORMAL},
    {"buenas costumbres", WARN_IMPORTANT},
    {" promovidas por", WARN_NORMAL}
};
static const WarnSeg WARN_L12[] = {{"la sociedad.", WARN_NORMAL}};
static const WarnSeg WARN_L13[] = {{"", WARN_NORMAL}};
static const WarnSeg WARN_L14[] = {
    {"Los desarrolladores", WARN_IMPORTANT},
    {" no se hacen responsables de ", WARN_NORMAL},
    {"crisis morales", WARN_STRONG},
    {",", WARN_NORMAL}
};
static const WarnSeg WARN_L15[] = {
    {"cuestionamientos existenciales ni del ", WARN_NORMAL},
    {"deterioro de su imagen", WARN_STRONG}
};
static const WarnSeg WARN_L16[] = {
    {"publica", WARN_STRONG},
    {" derivados de jugar este titulo.", WARN_NORMAL}
};

static const WarnLine WARNING_LINES[] = {
    {WARN_L0, (int)(sizeof(WARN_L0) / sizeof(WARN_L0[0]))},
    {WARN_L1, (int)(sizeof(WARN_L1) / sizeof(WARN_L1[0]))},
    {WARN_L2, (int)(sizeof(WARN_L2) / sizeof(WARN_L2[0]))},
    {WARN_L3, (int)(sizeof(WARN_L3) / sizeof(WARN_L3[0]))},
    {WARN_L4, (int)(sizeof(WARN_L4) / sizeof(WARN_L4[0]))},
    {WARN_L5, (int)(sizeof(WARN_L5) / sizeof(WARN_L5[0]))},
    {WARN_L6, (int)(sizeof(WARN_L6) / sizeof(WARN_L6[0]))},
    {WARN_L7, (int)(sizeof(WARN_L7) / sizeof(WARN_L7[0]))},
    {WARN_L8, (int)(sizeof(WARN_L8) / sizeof(WARN_L8[0]))},
    {WARN_L9, (int)(sizeof(WARN_L9) / sizeof(WARN_L9[0]))},
    {WARN_L10, (int)(sizeof(WARN_L10) / sizeof(WARN_L10[0]))},
    {WARN_L11, (int)(sizeof(WARN_L11) / sizeof(WARN_L11[0]))},
    {WARN_L12, (int)(sizeof(WARN_L12) / sizeof(WARN_L12[0]))},
    {WARN_L13, (int)(sizeof(WARN_L13) / sizeof(WARN_L13[0]))},
    {WARN_L14, (int)(sizeof(WARN_L14) / sizeof(WARN_L14[0]))},
    {WARN_L15, (int)(sizeof(WARN_L15) / sizeof(WARN_L15[0]))},
    {WARN_L16, (int)(sizeof(WARN_L16) / sizeof(WARN_L16[0]))},
};
static const int WARNING_LINE_COUNT = (int)(sizeof(WARNING_LINES) / sizeof(WARNING_LINES[0]));

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
    {"RECLUTA", "MUY FACIL"},
    {"AGENTE", "FACIL"},
    {"OPERATIVO", "NORMAL"},
    {"VETERANO", "DIFICIL"},
    {"CLASIFICADO", "MUY DIFICIL"},
};
static const int DIFFICULTY_COUNT = (int)(sizeof(DIFFICULTIES) / sizeof(DIFFICULTIES[0]));

enum OptionsPage {
    PAGE_ROOT,
    PAGE_SOUND,
    PAGE_DISPLAY,
    PAGE_CONTROLS,
    PAGE_COUNT
};

enum OptionId {
    OPT_PAGE_SOUND,
    OPT_PAGE_DISPLAY,
    OPT_PAGE_CONTROLS,
    OPT_RESET_ALL,
    OPT_MASTER,
    OPT_MUSIC,
    OPT_SFX,
    OPT_MUTE,
    OPT_RESOLUTION,
    OPT_FULLSCREEN,
    OPT_BRIGHTNESS,
    OPT_SHOW_FPS,
    OPT_GRAPHICS,
    OPT_BIND_UP, /* OPT_BIND_* follow the Action order. */
    OPT_BIND_DOWN,
    OPT_BIND_LEFT,
    OPT_BIND_RIGHT,
    OPT_BIND_DASH,
    OPT_RESET_CONTROLS,
    OPT_BACK
};

struct OptionRow {
    OptionId id;
    const char *label;
    const char *help;
};

struct OptionsPageDef {
    const char *title;
    const OptionRow *rows;
    int count;
};

static const OptionRow ROOT_ROWS[] = {
    {OPT_PAGE_SOUND, "SONIDO", "Volumen general, musica y efectos"},
    {OPT_PAGE_DISPLAY, "PANTALLA", "Resolucion, modo, brillo, FPS y graficos"},
    {OPT_PAGE_CONTROLS, "CONTROLES", "Reasigna las teclas del juego"},
    {OPT_RESET_ALL, "RESTABLECER TODO", "Vuelve a la configuracion por defecto"},
    {OPT_BACK, "VOLVER", "Guarda y vuelve al menu principal"},
};

static const OptionRow SOUND_ROWS[] = {
    {OPT_MASTER, "VOLUMEN GENERAL", "Volumen de todo el juego"},
    {OPT_MUSIC, "MUSICA", "Volumen de la musica"},
    {OPT_SFX, "EFECTOS", "Volumen de los efectos de sonido"},
    {OPT_MUTE, "SILENCIAR", "Apaga todo el sonido"},
    {OPT_BACK, "VOLVER", "Vuelve a opciones"},
};

static const OptionRow DISPLAY_ROWS[] = {
    {OPT_RESOLUTION, "RESOLUCION", "Tamano de la imagen del juego"},
    {OPT_FULLSCREEN, "MODO", "Pantalla completa o ventana"},
    {OPT_BRIGHTNESS, "BRILLO", "Oscurece o aclara la imagen"},
    {OPT_SHOW_FPS, "MOSTRAR FPS", "Contador de cuadros por segundo"},
    {OPT_GRAPHICS, "GRAFICOS", "Pixel art o calidad normal (personajes e isla)"},
    {OPT_BACK, "VOLVER", "Vuelve a opciones"},
};

static const OptionRow CONTROLS_ROWS[] = {
    {OPT_BIND_UP, "ARRIBA", "ENTER=Cambiar tecla  SUPR=Borrar"},
    {OPT_BIND_DOWN, "ABAJO", "ENTER=Cambiar tecla  SUPR=Borrar"},
    {OPT_BIND_LEFT, "IZQUIERDA", "ENTER=Cambiar tecla  SUPR=Borrar"},
    {OPT_BIND_RIGHT, "DERECHA", "ENTER=Cambiar tecla  SUPR=Borrar"},
    {OPT_BIND_DASH, "DASH", "ENTER=Cambiar tecla  SUPR=Borrar"},
    {OPT_RESET_CONTROLS, "RESTABLECER CONTROLES", "WASD/flechas y ESPACIO/SHIFT (ESC es fijo)"},
    {OPT_BACK, "VOLVER", "Vuelve a opciones"},
};

#define OPTION_ROWS(rows) rows, (int)(sizeof(rows) / sizeof(rows[0]))
static const OptionsPageDef OPTIONS_PAGES[PAGE_COUNT] = {
    {"OPCIONES", OPTION_ROWS(ROOT_ROWS)},
    {"SONIDO", OPTION_ROWS(SOUND_ROWS)},
    {"PANTALLA", OPTION_ROWS(DISPLAY_ROWS)},
    {"CONTROLES", OPTION_ROWS(CONTROLS_ROWS)},
};
#undef OPTION_ROWS

static OptionsPage options_page = PAGE_ROOT;

static float text_width(const char *text);
static float centered_x(float scaled_w, const char *text);
static void draw_black_overlay(Uint8 alpha);
static void load_orbit(const char *base);
static void render_logo(void);
static void apply_resolution(int index);
static void draw_debug_glow_text(
    float x,
    float y,
    const char *text,
    Uint8 fr,
    Uint8 fg,
    Uint8 fb,
    Uint8 gr,
    Uint8 gg,
    Uint8 gb);
static void draw_menu_text(float x, float y, const char *text, bool selected);
static void draw_menu_title(float x, float y, const char *text);
static void draw_menu_dim(float x, float y, const char *text);

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
    settings.resolution_index = index;
    logical_w = RESOLUTIONS[index].w;
    logical_h = RESOLUTIONS[index].h;

    if (!window || !renderer) {
        return;
    }

    /* Salir de fullscreen para poder cambiar tamano / modo de verdad. */
    SDL_SetWindowFullscreen(window, false);
    SDL_SetWindowFullscreenMode(window, nullptr);

    SDL_DisplayID display = SDL_GetDisplayForWindow(window);
    if (!display) {
        display = SDL_GetPrimaryDisplay();
    }

    SDL_DisplayMode closest;
    const bool have_mode = settings.fullscreen && display &&
        SDL_GetClosestFullscreenDisplayMode(
            display, logical_w, logical_h, 0.0f, false, &closest);

    bool exclusive = false;
    if (have_mode && closest.w > 0 && closest.h > 0) {
        /*
         * Fullscreen exclusivo a la resolucion elegida (o la mas cercana).
         * Asi el framebuffer, el UI y las imagenes escalan de verdad.
         */
        if (SDL_SetWindowFullscreenMode(window, &closest) &&
            SDL_SetWindowSize(window, closest.w, closest.h) &&
            SDL_SetWindowFullscreen(window, true)) {
            exclusive = true;
            /* Mantener el layout del juego en la resolucion pedida. */
            SDL_SetRenderLogicalPresentation(
                renderer,
                logical_w,
                logical_h,
                SDL_LOGICAL_PRESENTATION_LETTERBOX);
            SDL_Log(
                "Resolution %s via exclusive fullscreen %dx%d",
                RESOLUTIONS[index].label,
                closest.w,
                closest.h);
        }
    }

    if (!exclusive) {
        /* Fallback: ventana real al tamano elegido (se ve el cambio al instante). */
        SDL_SetWindowFullscreenMode(window, nullptr);
        SDL_SetWindowSize(window, logical_w, logical_h);
        SDL_SetWindowPosition(
            window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
        SDL_SetRenderLogicalPresentation(
            renderer,
            logical_w,
            logical_h,
            SDL_LOGICAL_PRESENTATION_STRETCH);
        SDL_Log(
            "Resolution %s via windowed %dx%d",
            RESOLUTIONS[index].label,
            logical_w,
            logical_h);
    }

    SDL_SyncWindow(window);
}

static float volume_gain(int volume)
{
    return (float)volume / (float)VOLUME_MAX;
}

static void apply_audio(void)
{
    if (!mixer) {
        return;
    }
    MIX_SetMixerGain(mixer, settings.muted ? 0.0f : volume_gain(settings.master_volume));
    if (menu_music_track) {
        MIX_SetTrackGain(menu_music_track, volume_gain(settings.music_volume));
    }
    if (game_music_track) {
        MIX_SetTrackGain(game_music_track, volume_gain(settings.music_volume));
    }
    for (int i = 0; i < SFX_TRACK_COUNT; ++i) {
        if (sfx_tracks[i]) {
            MIX_SetTrackGain(sfx_tracks[i], volume_gain(settings.sfx_volume));
        }
    }
}

static void play_sfx(MIX_Audio *audio)
{
    if (!audio) {
        return;
    }
    /* Prefer an idle track; if all are busy, cut the oldest one. */
    int pick = sfx_next;
    for (int i = 0; i < SFX_TRACK_COUNT; ++i) {
        const int idx = (sfx_next + i) % SFX_TRACK_COUNT;
        if (sfx_tracks[idx] && !MIX_TrackPlaying(sfx_tracks[idx])) {
            pick = idx;
            break;
        }
    }
    sfx_next = (pick + 1) % SFX_TRACK_COUNT;
    MIX_Track *track = sfx_tracks[pick];
    if (track && MIX_SetTrackAudio(track, audio)) {
        MIX_PlayTrack(track, 0);
    }
}

SDL_AppResult SDL_AppInit(void **appstate, int argc, char *argv[])
{
    (void)appstate;
    (void)argc;
    (void)argv;

    SDL_SetAppMetadata("CONFIDENTIAL", "1.0", "com.confidential.menu");
    SDL_SetHint(SDL_HINT_MAIN_CALLBACK_RATE, "60");

    play_intro_video(std::string(Asset_Root()) + "assets/video/rockstar.mkv");
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
    for (int i = 0; i < SFX_TRACK_COUNT; ++i) {
        sfx_tracks[i] = MIX_CreateTrack(mixer);
    }

    Settings_Load(RESOLUTION_COUNT);
    logical_w = RESOLUTIONS[settings.resolution_index].w;
    logical_h = RESOLUTIONS[settings.resolution_index].h;

    /* Crear en ventana; apply_resolution pone fullscreen exclusivo o windowed real. */
    if (!SDL_CreateWindowAndRenderer(
            "CONFIDENCIAL",
            logical_w,
            logical_h,
            0,
            &window,
            &renderer)) {
        SDL_Log("Couldn't create window/renderer: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }
    SDL_SetRenderVSync(renderer, 1);

    SDL_HideCursor();

    const char *base = Asset_Root();

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

    apply_resolution(settings.resolution_index);

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
        SDL_asprintf(&music_path, "%sassets/sfx/menu_bg.mp3", base);
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

    apply_audio();

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
    SDL_SetNumberProperty(opts, MIX_PROP_PLAY_FADE_IN_MILLISECONDS_NUMBER, 1500);

    if (!MIX_PlayTrack(menu_music_track, opts)) {
        SDL_Log("Couldn't play menu music: %s", SDL_GetError());
    } else {
        MIX_SetTrackLoops(menu_music_track, -1);
        menu_music_started = true;
    }

    SDL_DestroyProperties(opts);
}

static void stop_menu_music(void)
{
    if (menu_music_track && menu_music_started) {
        MIX_StopTrack(menu_music_track, 0);
        menu_music_started = false;
    }
}

static void stop_game_music(void)
{
    if (game_music_track) {
        MIX_StopTrack(game_music_track, 0);
    }
    if (game_music) {
        MIX_DestroyAudio(game_music);
        game_music = NULL;
    }
    game_music_file.clear();
}

/* Follows the map's music: when it changes (or the player moves to a map with other music),
   the new track fades in looping; a missing file just means silence. */
static void update_game_music(void)
{
    const char *want = Game_Music();
    if (!mixer || game_music_file == want) {
        return;
    }
    stop_game_music();
    game_music_file = want;
    if (!*want) {
        return;
    }
    char *path = NULL;
    SDL_asprintf(&path, "%sassets/music/%s", Asset_Root(), want);
    game_music = path ? MIX_LoadAudio(mixer, path, false) : NULL;
    if (!game_music) {
        SDL_Log("Level music %s not found: %s", path ? path : want, SDL_GetError());
        SDL_free(path);
        return;
    }
    SDL_free(path);
    if (!game_music_track) {
        game_music_track = MIX_CreateTrack(mixer);
    }
    if (!game_music_track || !MIX_SetTrackAudio(game_music_track, game_music)) {
        return;
    }
    MIX_SetTrackGain(game_music_track, volume_gain(settings.music_volume));
    SDL_PropertiesID opts = SDL_CreateProperties();
    SDL_SetNumberProperty(opts, MIX_PROP_PLAY_LOOPS_NUMBER, -1);
    SDL_SetNumberProperty(opts, MIX_PROP_PLAY_FADE_IN_MILLISECONDS_NUMBER, 1200);
    if (!MIX_PlayTrack(game_music_track, opts)) {
        SDL_Log("Couldn't play level music: %s", SDL_GetError());
    }
    SDL_DestroyProperties(opts);
}

static void go_to_menu(void)
{
    screen = SCREEN_MENU;
    menu_visible = false;
    menu_enter_ticks = SDL_GetTicks();
    /* Restart approach so the island settles before the menu can open. */
    orbit_play_start = SDL_GetTicks();
    orbit_shown = -1;
    start_menu_music();
}

static bool orbit_island_ready(void)
{
    if (orbit_count <= 0) {
        return true;
    }
    if (orbit_play_start == 0) {
        return false;
    }
    if (orbit_idle_start <= 0 || orbit_idle_start >= orbit_count) {
        return true;
    }
    const Uint64 approach_ms =
        ((Uint64)orbit_idle_start * 1000) / (Uint64)ORBIT_APPROACH_FPS;
    return (SDL_GetTicks() - orbit_play_start) >= approach_ms;
}

static void go_to_load(void)
{
    screen = SCREEN_LOAD;
    load_option = 0;
    load_enter_ticks = SDL_GetTicks();
    menu_visible = false;
}

static void go_to_character(void)
{
    screen = SCREEN_CHARACTER;
    settings.player_character = SDL_clamp(settings.player_character, 0, Game_PlayerCount() - 1);
    character_enter_ticks = SDL_GetTicks();
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
    options_page = PAGE_ROOT;
    options_option = 0;
    options_root_option = 0;
    rebinding = false;
    reset_armed = false;
    options_enter_ticks = SDL_GetTicks();
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
    /*
     * Piramide cuadrada regular centrada en el origen (giro estable).
     * Las 4 caras laterales son isosceles congruentes.
     */
    const float half = 0.5f;
    const float height = 0.90f;
    const float y_apex = height * 0.75f;   /* centroide ~ 0 */
    const float y_base = -height * 0.25f;

    /* UVs al bbox real de triangle.png (64,72)-(959,887) en 1024. */
    const float u0 = 64.0f / 1024.0f;
    const float u1 = 959.0f / 1024.0f;
    const float v0 = 72.0f / 1024.0f;
    const float v1 = 887.0f / 1024.0f;
    const float u_mid = 0.5f * (u0 + u1);

    const float yaw = (((float)((int)(now % 4000))) / 4000.0f) * SDL_PI_F * 2.0f;
    const float cy = SDL_cosf(yaw);
    const float sy = SDL_sinf(yaw);
    const float pitch = pitch_deg * (SDL_PI_F / 180.0f);
    const float cp = SDL_cosf(pitch);
    const float sp = SDL_sinf(pitch);

    const float raw[5][3] = {
        { 0.0f, y_apex, 0.0f },
        { -half, y_base, -half },
        {  half, y_base, -half },
        {  half, y_base,  half },
        { -half, y_base,  half }
    };

    /* Vista: yaw (Y) luego pitch (X). Guardamos xyz para normales / profundidad. */
    float view[5][3];
    SDL_FPoint proj[5];
    for (int i = 0; i < 5; ++i) {
        const float x0 = raw[i][0];
        const float y0 = raw[i][1];
        const float z0 = raw[i][2];
        /* yaw around Y */
        const float x1 = x0 * cy + z0 * sy;
        const float y1 = y0;
        const float z1 = -x0 * sy + z0 * cy;
        /* pitch around X */
        const float x2 = x1;
        const float y2 = y1 * cp - z1 * sp;
        const float z2 = y1 * sp + z1 * cp;
        view[i][0] = x2;
        view[i][1] = y2;
        view[i][2] = z2;
        proj[i].x = center_x + size * x2;
        /* SDL: Y crece hacia abajo; negamos para que +Y del modelo sea arriba. */
        proj[i].y = center_y - size * y2;
    }

    /* Caras laterales: winding CCW visto desde fuera. */
    const int faces[4][3] = {
        {0, 1, 2},
        {0, 2, 3},
        {0, 3, 4},
        {0, 4, 1}
    };

    int order[4] = {0, 1, 2, 3};
    float face_z[4];
    for (int f = 0; f < 4; ++f) {
        face_z[f] =
            (view[faces[f][0]][2] + view[faces[f][1]][2] + view[faces[f][2]][2]) / 3.0f;
    }
    /* Painter: lejos -> cerca */
    for (int a = 0; a < 3; ++a) {
        for (int b = a + 1; b < 4; ++b) {
            if (face_z[order[a]] < face_z[order[b]]) {
                const int tmp = order[a];
                order[a] = order[b];
                order[b] = tmp;
            }
        }
    }

    /* Luz fija (arriba-izquierda-frente) para que las 4 caras se lean parejas. */
    const float lx = -0.35f, ly = 0.80f, lz = -0.50f;
    const float llen = SDL_sqrtf(lx * lx + ly * ly + lz * lz);

    for (int oi = 0; oi < 4; ++oi) {
        const int f = order[oi];
        const int i0 = faces[f][0];
        const int i1 = faces[f][1];
        const int i2 = faces[f][2];

        const float ax = view[i1][0] - view[i0][0];
        const float ay = view[i1][1] - view[i0][1];
        const float az = view[i1][2] - view[i0][2];
        const float bx = view[i2][0] - view[i0][0];
        const float by = view[i2][1] - view[i0][1];
        const float bz = view[i2][2] - view[i0][2];
        float nx = ay * bz - az * by;
        float ny = az * bx - ax * bz;
        float nz = ax * by - ay * bx;
        const float nlen = SDL_sqrtf(nx * nx + ny * ny + nz * nz);
        if (nlen < 1e-6f) {
            continue;
        }
        nx /= nlen;
        ny /= nlen;
        nz /= nlen;

        /* Back-face: normal hacia la camara (+Z en este espacio de vista). */
        if (nz <= 0.02f) {
            continue;
        }

        float ndotl = (nx * lx + ny * ly + nz * lz) / llen;
        if (ndotl < 0.0f) {
            ndotl = 0.0f;
        }
        /* Ambiente alto para que ninguna cara se vea "rota" o demasiado distinta. */
        const float shade = 0.55f + 0.45f * ndotl;
        const SDL_FColor col = {shade, shade, shade, 1.0f};

        SDL_Vertex vertices[3] = {
            {proj[i0], col, {u_mid, v0}},
            {proj[i1], col, {u0, v1}},
            {proj[i2], col, {u1, v1}}
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

    const float max_w = (float)logical_w * 0.55f;
    const float max_h = (float)logical_h * 0.18f;
    float fit = max_w / tw;
    if (th * fit > max_h) {
        fit = max_h / th;
    }
    const float lw = tw * fit;
    const float lh = th * fit;
    const float lx = ((float)logical_w - lw) * 0.5f;
    const float ly = (float)logical_h * 0.10f;
    SDL_FRect dst = {lx, ly, lw, lh};
    SDL_RenderTexture(renderer, logo_tex, nullptr, &dst);

    const float tag_scale = ui_text_scale();
    SDL_SetRenderScale(renderer, tag_scale, tag_scale);
    const char *tag = "CENSURADO";
    const float bx = (lx + lw) / tag_scale - text_width(tag) + 1.0f;
    const float by = (ly + lh) / tag_scale + 1.0f;
    draw_menu_text(bx, by, tag, true);
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

/* The island frames stay PNG-compressed in RAM. Worker threads decode the next frames of the
   animation into a small cache ahead of time: a normal-quality (1080p) frame takes longer to
   decode than it stays on screen, so one decoder alone can't keep up with the approach. */
static const int ORBIT_WORKERS = 4;
static const int ORBIT_CACHE = 12;           /* decoded frames kept (~8 MB each at 1080p) */
static const int ORBIT_AHEAD = ORBIT_CACHE - ORBIT_WORKERS;

struct OrbitSlot {
    int frame = -1;
    SDL_Surface *surface = nullptr;
    bool busy = false; /* a worker is decoding into it */
};

static SDL_Thread *orbit_threads[ORBIT_WORKERS] = {};
static SDL_Mutex *orbit_mutex = nullptr;
static SDL_Condition *orbit_cond = nullptr;
static bool orbit_quit = false;
static OrbitSlot orbit_slots[ORBIT_CACHE];
static int orbit_window[ORBIT_AHEAD];        /* frames needed next, soonest first */
static int orbit_window_count = 0;
static bool orbit_hd = false;                /* the loaded frames are the normal-quality set */

static SDL_Surface *decode_orbit_frame(int frame)
{
    SDL_Surface *png = SDL_LoadPNG_IO(SDL_IOFromConstMem(orbit_png[frame], orbit_png_size[frame]), true);
    if (!png) {
        return nullptr;
    }
    SDL_Surface *rgba = SDL_ConvertSurface(png, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(png);
    return rgba;
}

static bool orbit_in_window(int frame)
{
    for (int i = 0; i < orbit_window_count; ++i) {
        if (orbit_window[i] == frame) {
            return true;
        }
    }
    return false;
}

static OrbitSlot *orbit_slot_of(int frame)
{
    for (OrbitSlot &slot : orbit_slots) {
        if (slot.frame == frame) {
            return &slot;
        }
    }
    return nullptr;
}

/* With the mutex held: the soonest window frame nobody has, and a slot to decode it into
   (one whose frame is no longer needed). */
static OrbitSlot *orbit_claim(int *frame)
{
    for (int i = 0; i < orbit_window_count; ++i) {
        if (orbit_slot_of(orbit_window[i])) {
            continue;
        }
        for (OrbitSlot &slot : orbit_slots) {
            if (!slot.busy && (slot.frame < 0 || !orbit_in_window(slot.frame))) {
                SDL_DestroySurface(slot.surface);
                slot.surface = nullptr;
                slot.frame = orbit_window[i];
                slot.busy = true;
                *frame = orbit_window[i];
                return &slot;
            }
        }
        return nullptr;
    }
    return nullptr;
}

static int SDLCALL orbit_worker(void *)
{
    SDL_LockMutex(orbit_mutex);
    while (!orbit_quit) {
        int frame = -1;
        OrbitSlot *slot = orbit_claim(&frame);
        if (!slot) {
            SDL_WaitCondition(orbit_cond, orbit_mutex);
            continue;
        }
        SDL_UnlockMutex(orbit_mutex);
        SDL_Surface *rgba = decode_orbit_frame(frame);
        SDL_LockMutex(orbit_mutex);
        slot->surface = rgba;
        slot->busy = false;
        if (!rgba) {
            slot->frame = -1;
        }
    }
    SDL_UnlockMutex(orbit_mutex);
    return 0;
}

static void unload_orbit(void)
{
    if (orbit_mutex) {
        SDL_LockMutex(orbit_mutex);
        orbit_quit = true;
        SDL_BroadcastCondition(orbit_cond);
        SDL_UnlockMutex(orbit_mutex);
    }
    for (SDL_Thread *&thread : orbit_threads) {
        if (thread) {
            SDL_WaitThread(thread, nullptr);
            thread = nullptr;
        }
    }
    for (OrbitSlot &slot : orbit_slots) {
        SDL_DestroySurface(slot.surface);
        slot = OrbitSlot{};
    }
    orbit_window_count = 0;
    if (orbit_tex) {
        SDL_DestroyTexture(orbit_tex);
        orbit_tex = nullptr;
    }
    orbit_shown = -1;
    for (int i = 0; i < orbit_count; ++i) {
        SDL_free(orbit_png[i]);
        orbit_png[i] = nullptr;
    }
    orbit_count = 0;
}

static int load_orbit_frames(const char *base, const char *dir)
{
    int count = 0;
    for (; count < ORBIT_MAX_FRAMES; ++count) {
        char *path = nullptr;
        SDL_asprintf(&path, "%sassets/img/%s/orbit_%03d.png", base, dir, count);
        orbit_png[count] = path ? SDL_LoadFile(path, &orbit_png_size[count]) : nullptr;
        SDL_free(path);
        if (!orbit_png[count]) {
            break;
        }
    }
    return count;
}

/* Loads the island frames for the current graphics setting (orbit/ pixel art, orbit_hd/ normal). */
static void load_orbit(const char *base)
{
    unload_orbit();
    orbit_idle_start = 0;
    orbit_play_start = 0;
    const char *dir = settings.pixel_art ? "orbit" : "orbit_hd";
    orbit_count = load_orbit_frames(base, dir);
    if (orbit_count == 0 && !settings.pixel_art) {
        dir = "orbit";
        orbit_count = load_orbit_frames(base, dir);
    }
    orbit_hd = SDL_strcmp(dir, "orbit_hd") == 0;
    {
        char *meta_path = nullptr;
        SDL_asprintf(&meta_path, "%sassets/img/%s/meta.txt", base, dir);
        if (meta_path) {
            size_t meta_size = 0;
            char *meta = (char *)SDL_LoadFile(meta_path, &meta_size);
            SDL_free(meta_path);
            if (meta && meta_size > 0) {
                int idle = 0;
                if (std::sscanf(meta, "idle_start=%d", &idle) == 1 && idle > 0) {
                    orbit_idle_start = idle;
                }
                SDL_free(meta);
            }
        }
    }
    if (orbit_idle_start <= 0 || orbit_idle_start >= orbit_count) {
        orbit_idle_start = (orbit_count > 40) ? (int)(orbit_count * 0.72f) : 0;
    }
    if (orbit_count > 0) {
        SDL_Log("Loaded %d %s orbit frames (idle from %d)", orbit_count, orbit_hd ? "normal" : "pixel art",
                orbit_idle_start);
        if (!orbit_mutex) {
            orbit_mutex = SDL_CreateMutex();
            orbit_cond = SDL_CreateCondition();
        }
        orbit_quit = false;
        for (SDL_Thread *&thread : orbit_threads) {
            thread = SDL_CreateThread(orbit_worker, "orbit", nullptr);
        }
    }
}

static void upload_orbit_frame(SDL_Surface *rgba, int frame)
{
    float tw = 0.0f;
    float th = 0.0f;
    if (orbit_tex && (!SDL_GetTextureSize(orbit_tex, &tw, &th) || (int)tw != rgba->w || (int)th != rgba->h)) {
        SDL_DestroyTexture(orbit_tex);
        orbit_tex = nullptr;
    }
    if (!orbit_tex) {
        orbit_tex = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING, rgba->w, rgba->h);
        if (orbit_tex) {
            SDL_SetTextureScaleMode(orbit_tex, orbit_hd ? SDL_SCALEMODE_LINEAR : SDL_SCALEMODE_NEAREST);
            SDL_SetTextureBlendMode(orbit_tex, SDL_BLENDMODE_BLEND);
        }
    }
    if (orbit_tex) {
        SDL_UpdateTexture(orbit_tex, nullptr, rgba->pixels, rgba->pitch);
        orbit_shown = frame;
    }
}

/* Frame of the island animation after `elapsed_ms`: the approach once, then the idle loop. */
static int orbit_frame_at(Uint64 elapsed_ms)
{
    if (orbit_idle_start > 0 && orbit_idle_start < orbit_count) {
        const Uint64 approach_ms = ((Uint64)orbit_idle_start * 1000) / (Uint64)ORBIT_APPROACH_FPS;
        if (elapsed_ms < approach_ms) {
            return SDL_min((int)((elapsed_ms * (Uint64)ORBIT_APPROACH_FPS) / 1000), orbit_idle_start - 1);
        }
        const int idle_len = orbit_count - orbit_idle_start;
        const Uint64 idle_f = ((elapsed_ms - approach_ms) * (Uint64)ORBIT_IDLE_FPS) / 1000;
        return orbit_idle_start + (int)(idle_f % (Uint64)idle_len);
    }
    return (int)((elapsed_ms * (Uint64)ORBIT_IDLE_FPS / 1000) % (Uint64)orbit_count);
}

/* Asks the workers for the frames shown from `elapsed_ms` on. */
static void orbit_prefetch(Uint64 elapsed_ms)
{
    if (!orbit_mutex || orbit_count <= 0) {
        return;
    }
    int window[ORBIT_AHEAD];
    int count = 0;
    for (Uint64 ahead = 0; ahead <= 4000 && count < ORBIT_AHEAD; ahead += 20) {
        const int frame = orbit_frame_at(elapsed_ms + ahead);
        if (count == 0 || window[count - 1] != frame) {
            window[count++] = frame;
        }
    }
    SDL_LockMutex(orbit_mutex);
    SDL_memcpy(orbit_window, window, sizeof(window[0]) * (size_t)count);
    orbit_window_count = count;
    SDL_BroadcastCondition(orbit_cond);
    SDL_UnlockMutex(orbit_mutex);
}

static void update_orbit(void)
{
    if (orbit_play_start == 0) {
        orbit_play_start = SDL_GetTicks();
    }
    const Uint64 elapsed_ms = SDL_GetTicks() - orbit_play_start;
    const int frame = orbit_frame_at(elapsed_ms);
    orbit_prefetch(elapsed_ms);
    if (frame == orbit_shown) {
        return;
    }
    SDL_Surface *ready = nullptr;
    SDL_LockMutex(orbit_mutex);
    if (OrbitSlot *slot = orbit_slot_of(frame)) {
        ready = slot->busy ? nullptr : slot->surface;
    }
    SDL_UnlockMutex(orbit_mutex);
    if (ready) {
        /* The frame is in the window, so no worker reuses its slot while it's uploaded. */
        upload_orbit_frame(ready, frame);
    } else if (!orbit_tex) {
        /* Nothing on screen yet: decode this one right away instead of flashing the fallback. */
        if (SDL_Surface *rgba = decode_orbit_frame(frame)) {
            upload_orbit_frame(rgba, frame);
            SDL_DestroySurface(rgba);
        }
    }
}

/* Switches the island between pixel art and normal quality without restarting its animation. */
static void apply_graphics(void)
{
    const Uint64 play_start = orbit_play_start;
    load_orbit(Asset_Root());
    orbit_play_start = play_start;
}

static void render_background(void)
{
    if (orbit_count > 0) {
        update_orbit();
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
    /* Un poco mas pequenos que el tamano anterior (~75%). */
    const float base_px = SDL_min((float)logical_w, (float)logical_h) / SDL_sqrtf(8.0f) * 0.75f;
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
                0.68f * logical_h,
                base_px,
                spin,
                -30.0f);
        }
    }

    const float scale = ui_text_scale();
    SDL_SetRenderScale(renderer, scale, scale);

    const char *title = "CARGAR PARTIDA";
    draw_menu_title(
        centered_x((float)logical_w / scale, title),
        (MENU_SCREEN_TITLE_Y * (float)logical_h) / scale,
        title);

    for (int i = 0; i < FILE_COUNT; ++i) {
        const bool sel = (i == load_option);
        const float label_x =
            (positions[i] / scale) - (text_width(FILE_LABELS[i]) * 0.5f);
        /* File 1/2/3 debajo del logo (no solapar CONFIDENCIAL / CENSURADO). */
        const float label_y = (0.48f * logical_h) / scale;
        draw_menu_text(label_x, label_y, FILE_LABELS[i], sel);

        const float empty_x =
            (positions[i] / scale) - (text_width(FILE_EMPTY_LABEL) * 0.5f);
        const float empty_y = (0.84f * logical_h) / scale;
        draw_menu_dim(empty_x, empty_y, FILE_EMPTY_LABEL);
    }

    if (load_option >= 0 && load_option < FILE_COUNT) {
        const float marker_x =
            (positions[load_option] / scale) - (DEBUG_FONT_W * 0.5f);
        const float marker_y = (0.89f * logical_h) / scale;
        draw_menu_text(marker_x, marker_y, "^", true);
    }

    SDL_SetRenderScale(renderer, 1.0f, 1.0f);

    const char *hint = "LEFT/RIGHT  ENTER=Load  ESC=Back";
    draw_menu_dim(
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

    const char *title = "SELECT DIFFICULTY";
    draw_menu_title(
        centered_x(sw, title),
        (MENU_SCREEN_TITLE_Y * (float)logical_h) / scale,
        title);

    float max_w = 0.0f;
    for (int i = 0; i < DIFFICULTY_COUNT; ++i) {
        max_w = SDL_max(max_w, text_width(DIFFICULTIES[i].name));
    }
    const float left_x = (sw - max_w) * 0.5f;
    const float first_y = sh * 0.48f;
    const float line_gap = sh * 0.065f;

    for (int i = 0; i < DIFFICULTY_COUNT; ++i) {
        const float y = first_y + i * line_gap;
        const bool sel = (i == difficulty_option);
        if (sel) {
            draw_menu_text(left_x - (DEBUG_FONT_W * 2), y, ">", true);
        }
        draw_menu_text(left_x, y, DIFFICULTIES[i].name, sel);
    }

    char level_line[48];
    std::snprintf(level_line, sizeof(level_line), "(%s)", DIFFICULTIES[difficulty_option].level);
    draw_menu_dim(
        centered_x(sw, level_line),
        first_y + DIFFICULTY_COUNT * line_gap + line_gap * 0.5f,
        level_line);

    SDL_SetRenderScale(renderer, 1.0f, 1.0f);

    const char *hint = "UP/DOWN  ENTER=Start  ESC=Back";
    draw_menu_dim(
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

static void render_character_select(void)
{
    render_logo();

    const float w = (float)logical_w;
    const float h = (float)logical_h;
    const SDL_FRect panel = {w * 0.33f, h * 0.45f, w * 0.34f, h * 0.49f};
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 22, 20, 28, 228);
    SDL_RenderFillRect(renderer, &panel);
    SDL_SetRenderDrawColor(renderer, 120, 20, 24, 255);
    SDL_RenderRect(renderer, &panel);

    char *sprites = nullptr;
    SDL_asprintf(&sprites, "%sassets/sprites", Asset_Root());
    if (sprites) {
        const float t = (float)(SDL_GetTicks() - character_enter_ticks) / 1000.0f;
        Game_DrawPlayerPreview(renderer, sprites, settings.player_character, w * 0.5f, h * 0.80f, h * 0.42f, t);
        SDL_free(sprites);
    }

    const float scale = ui_text_scale();
    SDL_SetRenderScale(renderer, scale, scale);
    const float sw = (float)logical_w / scale;
    const float sh = (float)logical_h / scale;

    const char *title = "SELECT CHARACTER";
    draw_menu_title(centered_x(sw, title), (MENU_SCREEN_TITLE_Y * (float)logical_h) / scale, title);

    char name[64];
    std::snprintf(name, sizeof(name), "< %s >", Game_PlayerName(settings.player_character));
    draw_menu_text(centered_x(sw, name), sh * 0.85f, name, true);

    char count[16];
    std::snprintf(count, sizeof(count), "%d/%d", settings.player_character + 1, Game_PlayerCount());
    draw_menu_dim(centered_x(sw, count), sh * 0.85f + DEBUG_FONT_W * 2.5f, count);

    SDL_SetRenderScale(renderer, 1.0f, 1.0f);

    const char *hint = "LEFT/RIGHT  ENTER=Select  ESC=Back";
    draw_menu_dim(centered_x((float)logical_w, hint), (float)logical_h - 28.0f, hint);

    const Uint64 fade_duration = 900;
    const Uint64 since = SDL_GetTicks() - character_enter_ticks;
    if (since < fade_duration) {
        const float progress = (float)since / (float)fade_duration;
        draw_black_overlay((Uint8)((1.0f - progress) * 180.0f));
    }
}

static bool option_is_link(OptionId id)
{
    return id == OPT_PAGE_SOUND || id == OPT_PAGE_DISPLAY || id == OPT_PAGE_CONTROLS ||
           id == OPT_RESET_ALL || id == OPT_RESET_CONTROLS || id == OPT_BACK;
}

static bool option_is_slider(OptionId id)
{
    return id == OPT_MASTER || id == OPT_MUSIC || id == OPT_SFX || id == OPT_BRIGHTNESS;
}

static bool *option_toggle(OptionId id)
{
    switch (id) {
        case OPT_MUTE: return &settings.muted;
        case OPT_FULLSCREEN: return &settings.fullscreen;
        case OPT_SHOW_FPS: return &settings.show_fps;
        case OPT_GRAPHICS: return &settings.pixel_art;
        default: return nullptr;
    }
}

static int *option_volume(OptionId id)
{
    switch (id) {
        case OPT_MASTER: return &settings.master_volume;
        case OPT_MUSIC: return &settings.music_volume;
        case OPT_SFX: return &settings.sfx_volume;
        default: return nullptr;
    }
}

static void format_bar(char *buf, size_t size, int filled, int total, const char *suffix)
{
    char bar[32];
    int n = 0;
    bar[n++] = '[';
    for (int i = 0; i < total && n < (int)sizeof(bar) - 2; ++i) {
        bar[n++] = (i < filled) ? '#' : '-';
    }
    bar[n++] = ']';
    bar[n] = '\0';
    std::snprintf(buf, size, "%s %s", bar, suffix);
}

static void option_value(OptionId id, char *buf, size_t size)
{
    buf[0] = '\0';
    if (const int *volume = option_volume(id)) {
        char num[8];
        std::snprintf(num, sizeof(num), "%d", *volume);
        format_bar(buf, size, *volume, VOLUME_MAX, num);
        return;
    }
    if (id == OPT_BRIGHTNESS) {
        char num[8];
        std::snprintf(num, sizeof(num), "%+d", settings.brightness);
        format_bar(buf, size, settings.brightness + BRIGHTNESS_MAX, BRIGHTNESS_MAX * 2, num);
        return;
    }
    if (id == OPT_RESOLUTION) {
        std::snprintf(buf, size, "< %s >", RESOLUTIONS[settings.resolution_index].label);
        return;
    }
    if (id == OPT_FULLSCREEN) {
        std::snprintf(buf, size, "< %s >", settings.fullscreen ? "PANTALLA COMPLETA" : "VENTANA");
        return;
    }
    if (id == OPT_GRAPHICS) {
        std::snprintf(buf, size, "< %s >", settings.pixel_art ? "PIXELADO" : "NORMAL");
        return;
    }
    if (const bool *toggle = option_toggle(id)) {
        std::snprintf(buf, size, "< %s >", *toggle ? "SI" : "NO");
    }
}

static void options_open_page(OptionsPage page)
{
    if (options_page == PAGE_ROOT) {
        options_root_option = options_option;
    }
    options_page = page;
    options_option = (page == PAGE_ROOT) ? options_root_option : 0;
    binding_slot = 0;
    rebinding = false;
    reset_armed = false;
    options_enter_ticks = SDL_GetTicks();
}

static void options_back(void)
{
    if (options_page != PAGE_ROOT) {
        options_open_page(PAGE_ROOT);
        return;
    }
    Settings_Save();
    screen = SCREEN_MENU;
    menu_visible = true;
}

/* Returns true when the setting actually changed. */
static bool options_adjust(OptionId id, int dir)
{
    if (int *volume = option_volume(id)) {
        const int next = SDL_clamp(*volume + dir, 0, VOLUME_MAX);
        if (next == *volume) {
            return false;
        }
        *volume = next;
        apply_audio();
        return true;
    }
    if (id == OPT_BRIGHTNESS) {
        const int next = SDL_clamp(settings.brightness + dir, -BRIGHTNESS_MAX, BRIGHTNESS_MAX);
        if (next == settings.brightness) {
            return false;
        }
        settings.brightness = next;
        return true;
    }
    if (id == OPT_RESOLUTION) {
        apply_resolution((settings.resolution_index + dir + RESOLUTION_COUNT) % RESOLUTION_COUNT);
        return true;
    }
    if (id >= OPT_BIND_UP && id <= OPT_BIND_DASH) {
        const int next = SDL_clamp(binding_slot + dir, 0, BINDING_SLOTS - 1);
        if (next == binding_slot) {
            return false;
        }
        binding_slot = next;
        return true;
    }
    if (bool *toggle = option_toggle(id)) {
        *toggle = !*toggle;
        if (id == OPT_MUTE) {
            apply_audio();
        } else if (id == OPT_FULLSCREEN) {
            apply_resolution(settings.resolution_index);
        } else if (id == OPT_GRAPHICS) {
            apply_graphics();
        }
        return true;
    }
    return false;
}

static void options_activate(OptionId id)
{
    switch (id) {
        case OPT_PAGE_SOUND: options_open_page(PAGE_SOUND); break;
        case OPT_PAGE_DISPLAY: options_open_page(PAGE_DISPLAY); break;
        case OPT_PAGE_CONTROLS: options_open_page(PAGE_CONTROLS); break;
        case OPT_BACK: options_back(); break;

        case OPT_RESET_ALL:
            if (!reset_armed) {
                reset_armed = true;
                return;
            }
            {
                const bool pixel_art = settings.pixel_art;
                Settings_Defaults(&settings);
                apply_resolution(settings.resolution_index);
                apply_audio();
                if (settings.pixel_art != pixel_art) {
                    apply_graphics();
                }
            }
            reset_armed = false;
            break;

        case OPT_RESET_CONTROLS:
            Settings_ResetControls(&settings);
            break;

        case OPT_BIND_UP:
        case OPT_BIND_DOWN:
        case OPT_BIND_LEFT:
        case OPT_BIND_RIGHT:
        case OPT_BIND_DASH:
            rebinding = true;
            break;

        default:
            if (option_toggle(id)) {
                options_adjust(id, 1);
            }
            break;
    }
}

static void play_move(void);
static void play_confirm(void);

static void options_key(const SDL_KeyboardEvent *key)
{
    const OptionsPageDef &page = OPTIONS_PAGES[options_page];
    const OptionId id = page.rows[options_option].id;

    if (rebinding) {
        if (key->repeat) {
            return;
        }
        const Action action = (Action)(id - OPT_BIND_UP);
        if (key->scancode == SDL_SCANCODE_DELETE || key->scancode == SDL_SCANCODE_BACKSPACE) {
            Settings_Bind(action, binding_slot, SDL_SCANCODE_UNKNOWN);
        } else if (key->scancode != SDL_SCANCODE_ESCAPE) {
            Settings_Bind(action, binding_slot, key->scancode);
        }
        rebinding = false;
        play_confirm();
        return;
    }

    switch (key->scancode) {
        case SDL_SCANCODE_ESCAPE:
            if (!key->repeat) {
                reset_armed = false;
                options_back();
            }
            break;

        case SDL_SCANCODE_UP:
        case SDL_SCANCODE_W:
            options_option = (options_option - 1 + page.count) % page.count;
            reset_armed = false;
            play_move();
            break;

        case SDL_SCANCODE_DOWN:
        case SDL_SCANCODE_S:
            options_option = (options_option + 1) % page.count;
            reset_armed = false;
            play_move();
            break;

        case SDL_SCANCODE_LEFT:
        case SDL_SCANCODE_A:
        case SDL_SCANCODE_RIGHT:
        case SDL_SCANCODE_D: {
            if (key->repeat && !option_is_slider(id)) {
                break;
            }
            const int dir =
                (key->scancode == SDL_SCANCODE_LEFT || key->scancode == SDL_SCANCODE_A) ? -1 : 1;
            if (options_adjust(id, dir)) {
                play_move();
            }
            break;
        }

        case SDL_SCANCODE_DELETE:
        case SDL_SCANCODE_BACKSPACE:
            if (!key->repeat && id >= OPT_BIND_UP && id <= OPT_BIND_DASH) {
                Settings_Bind((Action)(id - OPT_BIND_UP), binding_slot, SDL_SCANCODE_UNKNOWN);
                play_move();
            }
            break;

        case SDL_SCANCODE_SPACE:
        case SDL_SCANCODE_RETURN:
            if (!key->repeat) {
                play_confirm();
                options_activate(id);
            }
            break;

        default:
            break;
    }
}

static void render_options(void)
{
    render_logo();

    const float scale = ui_text_scale();
    SDL_SetRenderScale(renderer, scale, scale);
    const float sw = (float)logical_w / scale;
    const float sh = (float)logical_h / scale;

    const OptionsPageDef &page = OPTIONS_PAGES[options_page];
    draw_menu_title(
        centered_x(sw, page.title),
        (MENU_SCREEN_TITLE_Y * (float)logical_h) / scale,
        page.title);

    const bool dense = page.count > 6;
    const float first_y = sh * (dense ? 0.50f : 0.48f);
    const float line_gap = sh * (dense ? 0.058f : 0.065f);
    const float label_w = 20.0f * DEBUG_FONT_W;
    const float value_w = 22.0f * DEBUG_FONT_W;
    const float slot_w = 13.0f * DEBUG_FONT_W;
    const float left_x = (sw - (label_w + value_w)) * 0.5f;

    if (options_page == PAGE_CONTROLS) {
        const float header_y = first_y - line_gap * 0.8f;
        draw_menu_dim(left_x + label_w, header_y, "PRINCIPAL");
        draw_menu_dim(left_x + label_w + slot_w, header_y, "ALTERNATIVA");
    }

    for (int i = 0; i < page.count; ++i) {
        const OptionRow &row = page.rows[i];
        const float y = first_y + i * line_gap;
        const bool sel = (i == options_option);

        if (option_is_link(row.id)) {
            const float x = centered_x(sw, row.label);
            if (sel) {
                draw_menu_text(x - (DEBUG_FONT_W * 2), y, ">", true);
            }
            draw_menu_text(x, y, row.label, sel);
            continue;
        }

        if (sel) {
            draw_menu_text(left_x - (DEBUG_FONT_W * 2), y, ">", true);
        }
        draw_menu_text(left_x, y, row.label, sel);

        if (row.id >= OPT_BIND_UP && row.id <= OPT_BIND_DASH) {
            const Action action = (Action)(row.id - OPT_BIND_UP);
            for (int slot = 0; slot < BINDING_SLOTS; ++slot) {
                const bool slot_sel = sel && slot == binding_slot;
                char key[24];
                if (rebinding && slot_sel) {
                    const bool blink = ((SDL_GetTicks() / 300) % 2) == 0;
                    std::snprintf(key, sizeof(key), "[%s]", blink ? "..." : "   ");
                } else {
                    std::snprintf(
                        key, sizeof(key), "[%.10s]", Settings_KeyName(settings.bindings[action][slot]));
                }
                draw_menu_text(left_x + label_w + slot * slot_w, y, key, slot_sel);
            }
            continue;
        }

        char value[48];
        option_value(row.id, value, sizeof(value));
        draw_menu_text(left_x + label_w, y, value, sel);
    }

    const float help_y = sh * 0.88f;
    if (rebinding) {
        const char *help = "PULSA UNA TECLA  (ESC=Cancelar  SUPR=Borrar)";
        draw_menu_text(centered_x(sw, help), help_y, help, true);
    } else if (reset_armed) {
        const char *help = "Pulsa ENTER otra vez para confirmar";
        draw_menu_text(centered_x(sw, help), help_y, help, true);
    } else {
        const char *help = page.rows[options_option].help;
        draw_menu_dim(centered_x(sw, help), help_y, help);
    }

    SDL_SetRenderScale(renderer, 1.0f, 1.0f);

    const char *hint = (options_page == PAGE_CONTROLS)
        ? "UP/DOWN=Accion  LEFT/RIGHT=Columna  ENTER=Cambiar  ESC=Atras"
        : "UP/DOWN=Elegir  LEFT/RIGHT=Ajustar  ENTER=Aceptar  ESC=Atras";
    draw_menu_dim(
        centered_x((float)logical_w, hint),
        (float)logical_h - 28.0f,
        hint);

    const Uint64 fade_duration = 900;
    const Uint64 since = SDL_GetTicks() - options_enter_ticks;
    if (since < fade_duration) {
        const float progress = (float)since / (float)fade_duration;
        draw_black_overlay((Uint8)((1.0f - progress) * 180.0f));
    }
}

static float warn_seg_width_n(const WarnSeg *seg, int nchars)
{
    const int len = (int)std::strlen(seg->text);
    const int n = (nchars < 0) ? len : ((nchars > len) ? len : nchars);
    return (float)(n * DEBUG_FONT_W);
}

static float warn_seg_width(const WarnSeg *seg)
{
    return warn_seg_width_n(seg, -1);
}

static float warn_line_width(const WarnLine *line)
{
    float w = 0.0f;
    for (int i = 0; i < line->count; ++i) {
        w += warn_seg_width(&line->segs[i]);
    }
    return w;
}

static int warning_total_chars(void)
{
    int total = 0;
    for (int i = 0; i < WARNING_LINE_COUNT; ++i) {
        for (int s = 0; s < WARNING_LINES[i].count; ++s) {
            total += (int)std::strlen(WARNING_LINES[i].segs[s].text);
        }
    }
    return total;
}

static char warning_char_at(int index)
{
    for (int i = 0; i < WARNING_LINE_COUNT; ++i) {
        for (int s = 0; s < WARNING_LINES[i].count; ++s) {
            const int len = (int)std::strlen(WARNING_LINES[i].segs[s].text);
            if (index < len) {
                return WARNING_LINES[i].segs[s].text[index];
            }
            index -= len;
        }
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
            play_sfx(typewriter_sfx);
        }
        ++typewriter_last_char;
    }
}

/* Any key or click: the text appears whole (without a burst of key sounds) and the
   warning fades out into the menu. */
static void skip_warning(void)
{
    const Uint64 pause_end = WARNING_TYPE_MS + WARNING_PAUSE_MS;
    const Uint64 elapsed = SDL_GetTicks() - warning_start_ticks;
    if (elapsed < pause_end) {
        typewriter_last_char = warning_total_chars();
        warning_start_ticks = SDL_GetTicks() - pause_end;
    }
}

/* Halo suave (no neon): poca opacidad y un solo anillo. */
static void draw_debug_glow_text(
    float x,
    float y,
    const char *text,
    Uint8 fr,
    Uint8 fg,
    Uint8 fb,
    Uint8 gr,
    Uint8 gg,
    Uint8 gb)
{
    if (!text || text[0] == '\0') {
        return;
    }

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, gr, gg, gb, 42);
    const float d = 1.0f;
    SDL_RenderDebugText(renderer, x - d, y, text);
    SDL_RenderDebugText(renderer, x + d, y, text);
    SDL_RenderDebugText(renderer, x, y - d, text);
    SDL_RenderDebugText(renderer, x, y + d, text);

    SDL_SetRenderDrawColor(renderer, fr, fg, fb, SDL_ALPHA_OPAQUE);
    SDL_RenderDebugText(renderer, x, y, text);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
}

/* Menú: rojo oscuro + glow rojo claro (seleccion / titulos). */
static void draw_menu_text(float x, float y, const char *text, bool selected)
{
    if (selected) {
        draw_debug_glow_text(x, y, text, 170, 30, 30, 220, 70, 70);
    } else {
        draw_debug_glow_text(x, y, text, 230, 230, 230, 120, 120, 120);
    }
}

static void draw_menu_title(float x, float y, const char *text)
{
    draw_debug_glow_text(x, y, text, 170, 30, 30, 220, 70, 70);
}

static void draw_menu_dim(float x, float y, const char *text)
{
    draw_debug_glow_text(x, y, text, 170, 170, 170, 90, 90, 90);
}

static void draw_warn_text(float x, float y, const char *text, WarnStyle style)
{
    switch (style) {
        case WARN_STRONG:
            draw_debug_glow_text(x, y, text, 130, 28, 28, 170, 55, 55);
            break;
        case WARN_IMPORTANT:
            draw_debug_glow_text(x, y, text, 220, 190, 55, 190, 110, 35);
            break;
        default:
            SDL_SetRenderDrawColor(renderer, 230, 230, 230, SDL_ALPHA_OPAQUE);
            SDL_RenderDebugText(renderer, x, y, text);
            break;
    }
}

static void draw_warn_seg_partial(float x, float y, const WarnSeg *seg, int show_chars)
{
    const int len = (int)std::strlen(seg->text);
    int show = show_chars;
    if (show < 0 || show > len) {
        show = len;
    }
    if (show <= 0) {
        return;
    }

    char buffer[128];
    if (show >= (int)sizeof(buffer)) {
        show = (int)sizeof(buffer) - 1;
    }
    SDL_memcpy(buffer, seg->text, (size_t)show);
    buffer[show] = '\0';
    draw_warn_text(x, y, buffer, seg->style);
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
    play_sfx(move_sfx);
}

static void play_confirm(void)
{
    play_sfx(confirm_sfx);
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

    if (screen == SCREEN_OPTIONS) {
        if (event->type == SDL_EVENT_KEY_DOWN) {
            options_key(&event->key);
        }
        return SDL_APP_CONTINUE;
    }

    if (screen == SCREEN_GAME && event->type == SDL_EVENT_MOUSE_WHEEL) {
        Game_Event(event);
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
        } else {
            skip_warning();
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
            stop_game_music();
            start_menu_music();
        }
        return SDL_APP_CONTINUE;
    }

    if (screen == SCREEN_CHARACTER) {
        if (!key_down) {
            return SDL_APP_CONTINUE;
        }
        const int count = Game_PlayerCount();
        switch (event->key.scancode) {
            case SDL_SCANCODE_ESCAPE:
                screen = SCREEN_MENU;
                menu_visible = true;
                break;

            case SDL_SCANCODE_LEFT:
            case SDL_SCANCODE_A:
                settings.player_character = (settings.player_character - 1 + count) % count;
                character_enter_ticks = SDL_GetTicks() - 900; /* restart the turn, no fade */
                play_move();
                break;

            case SDL_SCANCODE_RIGHT:
            case SDL_SCANCODE_D:
                settings.player_character = (settings.player_character + 1) % count;
                character_enter_ticks = SDL_GetTicks() - 900;
                play_move();
                break;

            case SDL_SCANCODE_SPACE:
            case SDL_SCANCODE_RETURN:
                play_confirm();
                Settings_Save();
                go_to_new();
                break;

            default:
                break;
        }
        return SDL_APP_CONTINUE;
    }

    if (screen == SCREEN_NEW) {
        if (!key_down) {
            return SDL_APP_CONTINUE;
        }

        switch (event->key.scancode) {
            case SDL_SCANCODE_ESCAPE:
                go_to_character();
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
                    SDL_asprintf(&sprite_path, "%sassets/sprites", Asset_Root());
                    if (sprite_path) {
                        Game_LoadSprites(renderer, sprite_path);
                        SDL_free(sprite_path);
                    }
                }
                Game_Start(DIFFICULTIES[difficulty_option].name);
                game_last_ns = SDL_GetTicksNS();
                stop_menu_music();
                screen = SCREEN_GAME;
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
        if (!orbit_island_ready()) {
            return SDL_APP_CONTINUE;
        }
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
                go_to_character();
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

static void render_warning(float body_scale, int visible_chars)
{
    const float scaled_w = (float)logical_w / body_scale;
    const float scaled_h = (float)logical_h / body_scale;
    const float line_gap = scaled_h * 0.045f;
    /* Same debug-text scale as the menu; start a bit lower on screen. */
    float y = scaled_h * 0.16f;
    int remaining = visible_chars;
    bool cursor_drawn = false;
    const bool show_cursor =
        (visible_chars < warning_total_chars()) &&
        (((SDL_GetTicks() - warning_start_ticks) / 250) % 2 == 0);

    SDL_SetRenderScale(renderer, body_scale, body_scale);

    for (int i = 0; i < WARNING_LINE_COUNT; ++i) {
        const WarnLine *line = &WARNING_LINES[i];
        const bool blank =
            line->count == 1 && line->segs[0].text[0] == '\0';
        if (blank) {
            y += line_gap * 0.65f;
            continue;
        }

        const bool is_title = (i == 0);
        float x = (scaled_w - warn_line_width(line)) * 0.5f;

        for (int s = 0; s < line->count; ++s) {
            const WarnSeg *seg = &line->segs[s];
            const int len = (int)std::strlen(seg->text);
            int show = 0;
            bool cursor_here = false;

            if (remaining > 0) {
                if (remaining < len) {
                    show = remaining;
                    cursor_here = true;
                    remaining = 0;
                } else {
                    show = len;
                    remaining -= len;
                }
            } else if (visible_chars == 0 && i == 0 && s == 0) {
                cursor_here = true;
            }

            if (show > 0) {
                draw_warn_seg_partial(x, y, seg, show);
            }

            if (show_cursor && cursor_here && !cursor_drawn) {
                draw_warn_text(
                    x + warn_seg_width_n(seg, show),
                    y,
                    "_",
                    seg->style == WARN_NORMAL ? WARN_IMPORTANT : seg->style);
                cursor_drawn = true;
            }

            x += warn_seg_width_n(seg, show);
        }

        y += is_title ? (line_gap * 1.35f) : line_gap;
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
        const bool sel = (i == option);
        if (sel) {
            draw_menu_text(left_x - (DEBUG_FONT_W * 2), y, ">", true);
        }
        draw_menu_text(left_x, y, MENU_OPTIONS[i], sel);
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

static void draw_brightness(void)
{
    if (settings.brightness < 0) {
        draw_black_overlay((Uint8)(-settings.brightness * 28));
    } else if (settings.brightness > 0) {
        const Uint8 lift = (Uint8)(settings.brightness * 10);
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_ADD);
        SDL_SetRenderDrawColor(renderer, lift, lift, lift, SDL_ALPHA_OPAQUE);
        SDL_FRect full = {0, 0, (float)logical_w, (float)logical_h};
        SDL_RenderFillRect(renderer, &full);
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
    }
}

static void draw_fps(void)
{
    const Uint64 now = SDL_GetTicks();
    ++fps_frames;
    if (fps_window_start == 0) {
        fps_window_start = now;
    } else if (now - fps_window_start >= 1000) {
        fps_value = (int)((Uint64)fps_frames * 1000 / (now - fps_window_start));
        fps_frames = 0;
        fps_window_start = now;
    }
    if (!settings.show_fps) {
        return;
    }
    char label[16];
    std::snprintf(label, sizeof(label), "FPS %d", fps_value);
    SDL_SetRenderDrawColor(renderer, 120, 220, 120, SDL_ALPHA_OPAQUE);
    SDL_RenderDebugText(renderer, centered_x((float)logical_w, label), 8.0f, label);
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
        const int visible_chars = warning_visible_chars(elapsed);
        update_typewriter_sfx(visible_chars);
        orbit_prefetch(0); /* the approach starts with the menu; have its first frames ready */

        const float scale = ui_text_scale();
        SDL_SetRenderScale(renderer, scale, scale);
        render_warning(scale, visible_chars);
        SDL_SetRenderScale(renderer, 1.0f, 1.0f);

        const char *hint = "Pulsa cualquier tecla para saltar";
        draw_menu_dim(centered_x((float)logical_w, hint), (float)logical_h - 28.0f, hint);

        const Uint64 pause_end = WARNING_TYPE_MS + WARNING_PAUSE_MS;
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
    } else if (screen == SCREEN_CHARACTER) {
        render_background();
        render_character_select();
    } else if (screen == SCREEN_NEW) {
        render_background();
        render_new_file();
    } else if (screen == SCREEN_GAME) {
        const Uint64 now_ns = SDL_GetTicksNS();
        Game_Update((float)(now_ns - game_last_ns) / 1e9f);
        game_last_ns = now_ns;
        update_game_music();
        Game_Render(renderer, logical_w, logical_h);
    } else {
        render_background();

        const Uint64 since_menu = SDL_GetTicks() - menu_enter_ticks;
        if (since_menu < MENU_FADE_IN_MS) {
            const float t = 1.0f - ((float)since_menu / (float)MENU_FADE_IN_MS);
            draw_black_overlay((Uint8)(t * 255.0f));
        }

        if (menu_visible) {
            render_logo();
            const float scale = ui_text_scale();
            render_menu_options((float)logical_w / scale, (float)logical_h / scale);
        }
    }

    if (screen != SCREEN_WARNING && screen != SCREEN_GAME) {
        draw_version_label();
    }

    SDL_SetRenderScale(renderer, 1.0f, 1.0f);
    draw_brightness();
    draw_fps();

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
    unload_orbit();
    if (orbit_mutex) {
        SDL_DestroyCondition(orbit_cond);
        SDL_DestroyMutex(orbit_mutex);
        orbit_cond = nullptr;
        orbit_mutex = nullptr;
    }

    stop_menu_music();
    stop_game_music();
    if (game_music_track) {
        MIX_DestroyTrack(game_music_track);
        game_music_track = nullptr;
    }
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
    for (int i = 0; i < SFX_TRACK_COUNT; ++i) {
        if (sfx_tracks[i]) {
            MIX_DestroyTrack(sfx_tracks[i]);
            sfx_tracks[i] = nullptr;
        }
    }
    if (mixer) {
        MIX_DestroyMixer(mixer);
        mixer = nullptr;
    }
    MIX_Quit();
}
