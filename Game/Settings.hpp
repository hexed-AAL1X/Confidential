#pragma once
#include <SDL3/SDL.h>

enum Action {
    ACTION_UP,
    ACTION_DOWN,
    ACTION_LEFT,
    ACTION_RIGHT,
    ACTION_DASH,
    ACTION_COUNT
};

static const int BINDING_SLOTS = 2;
static const int VOLUME_MAX = 10;
static const int BRIGHTNESS_MAX = 5;

struct Settings {
    int master_volume; /* 0..VOLUME_MAX */
    int music_volume;
    int sfx_volume;
    bool muted;

    int resolution_index;
    bool fullscreen;
    int brightness; /* -BRIGHTNESS_MAX..BRIGHTNESS_MAX */
    bool show_fps;
    /* Pixel art graphics (low-res sprites and island) or the normal high-quality ones. */
    bool pixel_art;
    /* Index into the game's playable characters (Game_PlayerCount). */
    int player_character;

    SDL_Scancode bindings[ACTION_COUNT][BINDING_SLOTS];
};

extern Settings settings;

void Settings_Defaults(Settings *s);
void Settings_ResetControls(Settings *s);
/* resolution_count clamps the stored resolution index. */
void Settings_Load(int resolution_count);
void Settings_Save(void);

const char *Settings_ActionName(Action action);
bool Settings_ActionHeld(const bool *keys, Action action);
bool Settings_ActionPressed(SDL_Scancode scancode, Action action);
/* Swaps with whichever slot already owns the key, so no key drives two actions. */
void Settings_Bind(Action action, int slot, SDL_Scancode scancode);
/* Upper-case ASCII key name; the returned buffer is reused every few calls. */
const char *Settings_KeyName(SDL_Scancode scancode);
