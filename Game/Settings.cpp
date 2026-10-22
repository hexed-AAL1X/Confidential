#include "Settings.hpp"

#include <cstdio>
#include <cstring>
#include <string>

Settings settings;

static const char *const ACTION_NAMES[ACTION_COUNT] = {
    "ARRIBA",
    "ABAJO",
    "IZQUIERDA",
    "DERECHA",
    "DASH"
};

static const char *const ACTION_KEYS[ACTION_COUNT] = {
    "up",
    "down",
    "left",
    "right",
    "dash"
};

static char *settings_path(void)
{
    char *pref = SDL_GetPrefPath("confidential", "CONFIDENTIAL");
    if (!pref) {
        SDL_Log("Couldn't get pref path: %s", SDL_GetError());
        return nullptr;
    }
    char *path = nullptr;
    SDL_asprintf(&path, "%ssettings.ini", pref);
    SDL_free(pref);
    return path;
}

void Settings_ResetControls(Settings *s)
{
    const SDL_Scancode defaults[ACTION_COUNT][BINDING_SLOTS] = {
        {SDL_SCANCODE_W, SDL_SCANCODE_UP},
        {SDL_SCANCODE_S, SDL_SCANCODE_DOWN},
        {SDL_SCANCODE_A, SDL_SCANCODE_LEFT},
        {SDL_SCANCODE_D, SDL_SCANCODE_RIGHT},
        {SDL_SCANCODE_SPACE, SDL_SCANCODE_LSHIFT}
    };
    SDL_memcpy(s->bindings, defaults, sizeof(defaults));
}

void Settings_Defaults(Settings *s)
{
    s->master_volume = VOLUME_MAX;
    s->music_volume = 8;
    s->sfx_volume = 8;
    s->muted = false;

    s->resolution_index = 3;
    s->fullscreen = true;
    s->brightness = 0;
    s->show_fps = false;
    s->pixel_art = true;
    s->player_character = 0;

    Settings_ResetControls(s);
}

static void apply_value(const char *key, int value)
{
    struct IntField {
        const char *name;
        int *field;
        int lo;
        int hi;
    };
    const IntField ints[] = {
        {"master_volume", &settings.master_volume, 0, VOLUME_MAX},
        {"music_volume", &settings.music_volume, 0, VOLUME_MAX},
        {"sfx_volume", &settings.sfx_volume, 0, VOLUME_MAX},
        {"resolution", &settings.resolution_index, 0, 1 << 20},
        {"brightness", &settings.brightness, -BRIGHTNESS_MAX, BRIGHTNESS_MAX},
        {"player_character", &settings.player_character, 0, 1 << 10},
    };
    for (const IntField &f : ints) {
        if (std::strcmp(key, f.name) == 0) {
            *f.field = SDL_clamp(value, f.lo, f.hi);
            return;
        }
    }

    struct BoolField {
        const char *name;
        bool *field;
    };
    const BoolField bools[] = {
        {"muted", &settings.muted},
        {"fullscreen", &settings.fullscreen},
        {"show_fps", &settings.show_fps},
        {"pixel_art", &settings.pixel_art},
    };
    for (const BoolField &f : bools) {
        if (std::strcmp(key, f.name) == 0) {
            *f.field = value != 0;
            return;
        }
    }

    for (int a = 0; a < ACTION_COUNT; ++a) {
        for (int slot = 0; slot < BINDING_SLOTS; ++slot) {
            char name[32];
            std::snprintf(name, sizeof(name), "bind_%s_%d", ACTION_KEYS[a], slot);
            if (std::strcmp(key, name) == 0) {
                if (value >= 0 && value < SDL_SCANCODE_COUNT && value != SDL_SCANCODE_ESCAPE) {
                    settings.bindings[a][slot] = (SDL_Scancode)value;
                }
                return;
            }
        }
    }
}

void Settings_Load(int resolution_count)
{
    Settings_Defaults(&settings);

    char *path = settings_path();
    if (!path) {
        return;
    }
    size_t size = 0;
    char *data = (char *)SDL_LoadFile(path, &size);
    SDL_free(path);
    if (!data) {
        return;
    }

    char *cursor = data;
    while (*cursor) {
        char *end = std::strchr(cursor, '\n');
        if (end) {
            *end = '\0';
        }
        char key[64];
        int value = 0;
        if (std::sscanf(cursor, " %63[^= ] = %d", key, &value) == 2) {
            apply_value(key, value);
        }
        if (!end) {
            break;
        }
        cursor = end + 1;
    }
    SDL_free(data);

    if (settings.resolution_index >= resolution_count) {
        settings.resolution_index = resolution_count - 1;
    }
}

void Settings_Save(void)
{
    char *path = settings_path();
    if (!path) {
        return;
    }

    std::string out;
    char line[96];
    auto put = [&](const char *key, int value) {
        std::snprintf(line, sizeof(line), "%s=%d\n", key, value);
        out += line;
    };
    put("master_volume", settings.master_volume);
    put("music_volume", settings.music_volume);
    put("sfx_volume", settings.sfx_volume);
    put("muted", settings.muted);
    put("resolution", settings.resolution_index);
    put("fullscreen", settings.fullscreen);
    put("brightness", settings.brightness);
    put("show_fps", settings.show_fps);
    put("pixel_art", settings.pixel_art);
    put("player_character", settings.player_character);
    for (int a = 0; a < ACTION_COUNT; ++a) {
        for (int slot = 0; slot < BINDING_SLOTS; ++slot) {
            char key[32];
            std::snprintf(key, sizeof(key), "bind_%s_%d", ACTION_KEYS[a], slot);
            put(key, (int)settings.bindings[a][slot]);
        }
    }

    if (!SDL_SaveFile(path, out.data(), out.size())) {
        SDL_Log("Couldn't save settings to %s: %s", path, SDL_GetError());
    }
    SDL_free(path);
}

const char *Settings_ActionName(Action action)
{
    return (action >= 0 && action < ACTION_COUNT) ? ACTION_NAMES[action] : "?";
}

bool Settings_ActionHeld(const bool *keys, Action action)
{
    for (int slot = 0; slot < BINDING_SLOTS; ++slot) {
        const SDL_Scancode sc = settings.bindings[action][slot];
        if (sc != SDL_SCANCODE_UNKNOWN && keys[sc]) {
            return true;
        }
    }
    return false;
}

bool Settings_ActionPressed(SDL_Scancode scancode, Action action)
{
    if (scancode == SDL_SCANCODE_UNKNOWN) {
        return false;
    }
    for (int slot = 0; slot < BINDING_SLOTS; ++slot) {
        if (settings.bindings[action][slot] == scancode) {
            return true;
        }
    }
    return false;
}

void Settings_Bind(Action action, int slot, SDL_Scancode scancode)
{
    const SDL_Scancode previous = settings.bindings[action][slot];
    if (scancode != SDL_SCANCODE_UNKNOWN) {
        for (int a = 0; a < ACTION_COUNT; ++a) {
            for (int s = 0; s < BINDING_SLOTS; ++s) {
                if ((a != action || s != slot) && settings.bindings[a][s] == scancode) {
                    settings.bindings[a][s] = previous;
                }
            }
        }
    }
    settings.bindings[action][slot] = scancode;
}

const char *Settings_KeyName(SDL_Scancode scancode)
{
    static char buffers[8][24];
    static int next = 0;
    char *buf = buffers[next];
    next = (next + 1) % 8;

    const char *name = (scancode == SDL_SCANCODE_UNKNOWN) ? "" : SDL_GetScancodeName(scancode);
    if (!name || !name[0]) {
        name = "---";
    }
    size_t i = 0;
    for (; name[i] && i < sizeof(buffers[0]) - 1; ++i) {
        const unsigned char c = (unsigned char)name[i];
        buf[i] = (c < 128) ? (char)SDL_toupper(c) : '?';
    }
    buf[i] = '\0';
    return buf;
}
