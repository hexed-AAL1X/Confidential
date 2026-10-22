#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "AssetPath.hpp"
#include "SpritePack.hpp"

static const int WIN_W = 1600;
static const int WIN_H = 900;

/* Layout: a top bar with the title and the mode tabs, then three columns. */
static const float TOP_BAR_H = 44.0f;
static const float TOP = 60.0f;
static const float BOTTOM = (float)WIN_H - 44.0f;

static const float CANVAS_X = 330.0f;
static const float CANVAS_Y = TOP;
static const float CANVAS_W = 530.0f;
static const float CANVAS_H = BOTTOM - TOP;

static const float VIEW_X = 330.0f;
static const float VIEW_Y = TOP;
static const float VIEW_W = 820.0f;
static const float VIEW_H = BOTTOM - TOP;

/* Opposite direction of each pack row (LEFT <-> RIGHT etc.). */
static const int MIRROR_ROW[SPRITE_DIRS] = {0, 7, 6, 5, 4, 3, 2, 1};

enum Mode {
    MODE_VIEW,
    MODE_EDIT,
    MODE_CREATE
};

enum Tool {
    TOOL_PENCIL,
    TOOL_ERASER,
    TOOL_FILL,
    TOOL_PICKER
};

struct PackEntry {
    std::string dir;
    std::string name;
    std::string folder;
    std::string group;
};

struct UndoStep {
    int action;
    std::vector<Uint32> pixels;
};

static SDL_Window *window = nullptr;
static SDL_Renderer *renderer = nullptr;

static std::vector<std::string> pack_roots;
static std::vector<PackEntry> packs;
static int cur_pack = -1;
static int pending_pack = -1;
static int pack_scroll = 0;
static SpritePack pack;
static std::vector<SDL_Texture *> action_tex;
static std::vector<bool> tex_dirty;
/* Actions with unsaved pixel edits; they stay in memory when switching actions. */
static std::vector<bool> edited;
static std::vector<UndoStep> undo_stack;
static std::vector<Uint32> clipboard;
static bool unsaved = false;
/* VIEW/EDIT show the pack's hd/ variant (normal quality) when it has one. */
static bool view_hd = false;

static Mode mode = MODE_VIEW;
static int cur_action = 0;
static int cur_dir = 0;
static int cur_frame = 0;
static Tool tool = TOOL_PENCIL;
static bool mirror_draw = false;
static bool onion = true;
static bool playing = true;
static bool auto_rotate = false;
static float speed = 1.0f;
static Uint64 anim_start = 0;
static Uint64 rotate_last = 0;
static Uint32 color = 0xff0a0608;
static Uint32 palette[32] = {
    0xff000000, 0xff1d2b53, 0xff7e2553, 0xff008751, 0xffab5236, 0xff5f574f, 0xffc2c3c7, 0xfffff1e8,
    0xffff004d, 0xffffa300, 0xffffec27, 0xff00e436, 0xff29adff, 0xff83769c, 0xffff77a8, 0xffffccaa,
    0xff0a0608, 0xffb81c28, 0xff7a0c16, 0xffe0404a, 0xff1f6b6b, 0xff123f42, 0xff2e9a96, 0xffa8b418,
    0xff6c7410, 0xfff0b888, 0xffc08058, 0xff1c1418, 0xff3c2c34, 0xffe8e8e8, 0xffa8a8a8, 0xff404040
};

static float mouse_x = 0.0f;
static float mouse_y = 0.0f;
static bool left_down = false;
static bool right_down = false;
static bool click_pending = false;
static bool rclick_pending = false;
static int last_px = -1;
static int last_py = -1;
static int slider_drag = -1;
static bool middle_down = false;

/* Opaque area of the idle animation, used to frame the character in the views. */
static SDL_Rect fit_box = {0, 0, 1, 1};
/* Opaque area of each loaded action, for the frame thumbnails. */
static std::vector<SDL_Rect> act_box;
/* Extra zoom steps the user added with the wheel in VIEW / CREATE. */
static int view_zoom_step = 0;
/* EDIT canvas: zoom and screen position of the cell's top-left corner. */
static int canvas_zoom = 1;
static float canvas_ox = CANVAS_X;
static float canvas_oy = CANVAS_Y;

static char status[160] = "";
static Uint64 status_until = 0;

/* Full character creator (CREATE mode), defined further down. */
static void c3_reset(void);
static bool c3_name_key(const SDL_KeyboardEvent &k);
static void c3_text_input(const char *s);
static bool classic_creator_on(void);
static void set_classic_creator(bool on);

static void set_status(const char *msg)
{
    SDL_strlcpy(status, msg, sizeof(status));
    status_until = SDL_GetTicks() + 3000;
}

/* ------------------------------------------------------------------------- */
/* Pack data                                                                  */
/* ------------------------------------------------------------------------- */

static bool has_pack(void)
{
    return cur_pack >= 0 && cur_action >= 0 && cur_action < (int)pack.actions.size() &&
           pack.actions[cur_action].sheet != nullptr;
}

static SpriteAction &action(void)
{
    return pack.actions[cur_action];
}

/* Bounding box of the opaque pixels of a sheet, in cell coordinates (all frames and directions). */
static SDL_Rect sheet_bbox(const SDL_Surface *s, int cw, int ch)
{
    int x0 = cw, y0 = ch, x1 = -1, y1 = -1;
    for (int y = 0; y < s->h; ++y) {
        const Uint32 *row = (const Uint32 *)((const Uint8 *)s->pixels + y * s->pitch);
        const int cy = y % ch;
        for (int x = 0; x < s->w; ++x) {
            if (row[x] >> 24) {
                const int cx = x % cw;
                x0 = SDL_min(x0, cx);
                x1 = SDL_max(x1, cx);
                y0 = SDL_min(y0, cy);
                y1 = SDL_max(y1, cy);
            }
        }
    }
    if (x1 < 0) {
        return {0, 0, cw, ch};
    }
    return {x0, y0, x1 - x0 + 1, y1 - y0 + 1};
}

/* Integer zoom that makes `box` fill about the given fraction of a w x h area. */
static int fit_zoom(float w, float h, const SDL_Rect &box, float fill_part)
{
    return SDL_max(1, (int)SDL_min(w * fill_part / box.w, h * fill_part / box.h));
}

static void canvas_fit(void)
{
    canvas_zoom = SDL_min(32, fit_zoom(CANVAS_W, CANVAS_H, fit_box, 0.85f));
    canvas_ox = SDL_floorf(CANVAS_X + CANVAS_W * 0.5f - (fit_box.x + fit_box.w * 0.5f) * canvas_zoom);
    canvas_oy = SDL_floorf(CANVAS_Y + CANVAS_H * 0.5f - (fit_box.y + fit_box.h * 0.5f) * canvas_zoom);
}

/* Changes the canvas zoom keeping the pixel under (mx, my) in place. */
static void canvas_zoom_at(int zoom, float mx, float my)
{
    zoom = SDL_clamp(zoom, 1, 32);
    const float cx = (mx - canvas_ox) / canvas_zoom;
    const float cy = (my - canvas_oy) / canvas_zoom;
    canvas_zoom = zoom;
    canvas_ox = SDL_floorf(mx - cx * zoom);
    canvas_oy = SDL_floorf(my - cy * zoom);
}

static Uint32 &px_at(int dir, int frame, int x, int y)
{
    SDL_Surface *s = action().sheet;
    Uint8 *row = (Uint8 *)s->pixels + (dir * pack.cell_h + y) * s->pitch;
    return ((Uint32 *)row)[frame * pack.cell_w + x];
}

static void mark_dirty(void)
{
    tex_dirty[cur_action] = true;
    edited[cur_action] = true;
    unsaved = true;
}

static void free_textures(void)
{
    for (SDL_Texture *tex : action_tex) {
        if (tex) {
            SDL_DestroyTexture(tex);
        }
    }
    action_tex.clear();
    tex_dirty.clear();
    edited.clear();
}

/* HD packs are hundreds of MB in total, so only the current action (plus any with
   unsaved edits) is kept in memory. */
static void load_only_action(int index)
{
    for (int i = 0; i < (int)pack.actions.size(); ++i) {
        if (i == index || edited[i]) {
            continue;
        }
        if (action_tex[i]) {
            SDL_DestroyTexture(action_tex[i]);
            action_tex[i] = nullptr;
        }
        SpritePack_FreeSheet(&pack, i);
    }
    if (!pack.actions[index].sheet && SpritePack_LoadSheet(&pack, index)) {
        tex_dirty[index] = true;
        act_box[index] = sheet_bbox(pack.actions[index].sheet, pack.cell_w, pack.cell_h);
    }
}

static void sync_textures(void)
{
    for (size_t i = 0; i < pack.actions.size(); ++i) {
        SDL_Surface *s = pack.actions[i].sheet;
        if (!s) {
            continue;
        }
        if (!action_tex[i]) {
            action_tex[i] = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                                              SDL_TEXTUREACCESS_STREAMING, s->w, s->h);
            SDL_SetTextureScaleMode(action_tex[i], SDL_SCALEMODE_NEAREST);
            SDL_SetTextureBlendMode(action_tex[i], SDL_BLENDMODE_BLEND);
            tex_dirty[i] = true;
        }
        if (tex_dirty[i]) {
            SDL_UpdateTexture(action_tex[i], nullptr, s->pixels, s->pitch);
            tex_dirty[i] = false;
        }
    }
}

static void restart_anim(void)
{
    anim_start = SDL_GetTicks();
}

static void select_action(int index)
{
    if (index < 0 || index >= (int)pack.actions.size()) {
        return;
    }
    load_only_action(index);
    cur_action = index;
    cur_frame = SDL_min(cur_frame, pack.actions[index].frames - 1);
    restart_anim();
}

static bool is_file(const std::string &path)
{
    SDL_PathInfo info;
    return SDL_GetPathInfo(path.c_str(), &info) && info.type == SDL_PATHTYPE_FILE;
}

static void open_pack(int index)
{
    if (index < 0 || index >= (int)packs.size()) {
        return;
    }
    if (unsaved && pending_pack != index) {
        pending_pack = index;
        set_status("Unsaved changes! Ctrl+S to save, or pick it again to discard.");
        return;
    }
    pending_pack = -1;
    free_textures();
    undo_stack.clear();
    std::string dir = packs[index].dir;
    if (view_hd && is_file(dir + "/hd/character.ini")) {
        dir += "/hd";
    }
    if (!SpritePack_Load(&pack, dir.c_str(), false) || pack.actions.empty()) {
        cur_pack = -1;
        set_status("Couldn't load that pack");
        return;
    }
    cur_pack = index;
    action_tex.assign(pack.actions.size(), nullptr);
    tex_dirty.assign(pack.actions.size(), true);
    edited.assign(pack.actions.size(), false);
    act_box.assign(pack.actions.size(), SDL_Rect{0, 0, pack.cell_w, pack.cell_h});
    unsaved = false;
    const int idle = SpritePack_Find(&pack, "idle");
    cur_frame = 0;
    select_action(idle >= 0 ? idle : 0);
    fit_box = act_box[cur_action];
    view_zoom_step = 0;
    canvas_fit();
    char msg[160];
    SDL_snprintf(msg, sizeof(msg), "Loaded %s", dir.c_str());
    set_status(msg);
}

static std::vector<std::string> sorted_entries(const std::string &dir)
{
    int count = 0;
    char **names = SDL_GlobDirectory(dir.c_str(), "*", 0, &count);
    if (!names) {
        return {};
    }
    std::vector<std::string> sorted(names, names + count);
    SDL_free(names);
    std::sort(sorted.begin(), sorted.end());
    return sorted;
}

static std::string ini_name(const std::string &ini, const std::string &fallback)
{
    std::string name = fallback;
    size_t size = 0;
    char *text = (char *)SDL_LoadFile(ini.c_str(), &size);
    if (text) {
        const char *n = std::strstr(text, "name=");
        if (n) {
            n += 5;
            const char *end = std::strpbrk(n, "\r\n");
            name = std::string(n, end ? (size_t)(end - n) : std::strlen(n));
        }
        SDL_free(text);
    }
    return name;
}

static std::string upper(std::string s)
{
    for (char &c : s) {
        c = (char)SDL_toupper((unsigned char)c);
    }
    return s;
}

/* Packs directly inside a root form one group; each subfolder without a character.ini
   (e.g. assets/sprites/postal) is a group of its own. */
static void scan_packs(void)
{
    packs.clear();
    for (size_t r = 0; r < pack_roots.size(); ++r) {
        const std::string &root = pack_roots[r];
        const std::string base = root.substr(root.find_last_of('/') + 1);
        const std::string root_group = r == 0 ? "GAME" : upper(base);
        std::vector<PackEntry> nested;
        for (const std::string &name : sorted_entries(root)) {
            const std::string dir = root + "/" + name;
            if (is_file(dir + "/character.ini")) {
                packs.push_back({dir, ini_name(dir + "/character.ini", name), name, root_group});
                continue;
            }
            for (const std::string &sub : sorted_entries(dir)) {
                const std::string sub_dir = dir + "/" + sub;
                if (is_file(sub_dir + "/character.ini")) {
                    nested.push_back({sub_dir, ini_name(sub_dir + "/character.ini", sub), sub, upper(name)});
                }
            }
        }
        packs.insert(packs.end(), nested.begin(), nested.end());
    }
}

static void save_pack(void)
{
    if (cur_pack < 0) {
        return;
    }
    if (SpritePack_Save(&pack)) {
        unsaved = false;
        edited.assign(pack.actions.size(), false);
        char msg[128];
        SDL_snprintf(msg, sizeof(msg), "Saved %s", pack.dir.c_str());
        set_status(msg);
    } else {
        char msg[128];
        SDL_snprintf(msg, sizeof(msg), "Save failed: %s", SDL_GetError());
        set_status(msg);
    }
}

/* Current frame index for playback; non-looping actions hold the last frame for a
   second and then start again so they can be watched repeatedly. */
static int playback_frame(void)
{
    const SpriteAction &a = action();
    if (!playing) {
        return cur_frame;
    }
    const Uint64 t = (Uint64)((SDL_GetTicks() - anim_start) * speed);
    const int f = (int)(t * (Uint64)a.fps / 1000);
    if (a.loop) {
        return f % a.frames;
    }
    const int hold = a.fps;
    return SDL_min(f % (a.frames + hold), a.frames - 1);
}

/* ------------------------------------------------------------------------- */
/* Editing                                                                    */
/* ------------------------------------------------------------------------- */

static void push_undo(void)
{
    SDL_Surface *s = action().sheet;
    UndoStep step;
    step.action = cur_action;
    step.pixels.resize((size_t)s->w * s->h);
    for (int y = 0; y < s->h; ++y) {
        SDL_memcpy(&step.pixels[(size_t)y * s->w], (Uint8 *)s->pixels + y * s->pitch, (size_t)s->w * 4);
    }
    undo_stack.push_back(std::move(step));
    if (undo_stack.size() > 60) {
        undo_stack.erase(undo_stack.begin());
    }
}

static void undo(void)
{
    if (undo_stack.empty()) {
        set_status("Nothing to undo");
        return;
    }
    UndoStep step = std::move(undo_stack.back());
    undo_stack.pop_back();
    SDL_Surface *s = pack.actions[step.action].sheet;
    for (int y = 0; y < s->h; ++y) {
        SDL_memcpy((Uint8 *)s->pixels + y * s->pitch, &step.pixels[(size_t)y * s->w], (size_t)s->w * 4);
    }
    cur_action = step.action;
    mark_dirty();
}

static void put_pixel(int x, int y, Uint32 c)
{
    if (x < 0 || x >= pack.cell_w || y < 0 || y >= pack.cell_h) {
        return;
    }
    px_at(cur_dir, cur_frame, x, y) = c;
    if (mirror_draw) {
        px_at(cur_dir, cur_frame, pack.cell_w - 1 - x, y) = c;
    }
    mark_dirty();
}

static void draw_line(int x0, int y0, int x1, int y1, Uint32 c)
{
    const int dx = SDL_abs(x1 - x0);
    const int dy = -SDL_abs(y1 - y0);
    const int sx = x0 < x1 ? 1 : -1;
    const int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    while (true) {
        put_pixel(x0, y0, c);
        if (x0 == x1 && y0 == y1) {
            break;
        }
        const int e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

static void flood_fill(int x, int y, Uint32 c)
{
    const Uint32 target = px_at(cur_dir, cur_frame, x, y);
    if (target == c) {
        return;
    }
    std::vector<SDL_Point> stack = {{x, y}};
    while (!stack.empty()) {
        const SDL_Point p = stack.back();
        stack.pop_back();
        if (p.x < 0 || p.x >= pack.cell_w || p.y < 0 || p.y >= pack.cell_h) {
            continue;
        }
        Uint32 &cell = px_at(cur_dir, cur_frame, p.x, p.y);
        if (cell != target) {
            continue;
        }
        cell = c;
        stack.push_back({p.x + 1, p.y});
        stack.push_back({p.x - 1, p.y});
        stack.push_back({p.x, p.y + 1});
        stack.push_back({p.x, p.y - 1});
    }
    mark_dirty();
}

static void flip_frame(void)
{
    push_undo();
    for (int y = 0; y < pack.cell_h; ++y) {
        for (int x = 0; x < pack.cell_w / 2; ++x) {
            Uint32 &a = px_at(cur_dir, cur_frame, x, y);
            Uint32 &b = px_at(cur_dir, cur_frame, pack.cell_w - 1 - x, y);
            const Uint32 t = a;
            a = b;
            b = t;
        }
    }
    mark_dirty();
}

static void clear_frame(void)
{
    push_undo();
    for (int y = 0; y < pack.cell_h; ++y) {
        for (int x = 0; x < pack.cell_w; ++x) {
            px_at(cur_dir, cur_frame, x, y) = 0;
        }
    }
    mark_dirty();
}

static void copy_frame(void)
{
    clipboard.assign((size_t)pack.cell_w * pack.cell_h, 0);
    for (int y = 0; y < pack.cell_h; ++y) {
        for (int x = 0; x < pack.cell_w; ++x) {
            clipboard[(size_t)y * pack.cell_w + x] = px_at(cur_dir, cur_frame, x, y);
        }
    }
    set_status("Frame copied");
}

static void paste_frame(void)
{
    if (clipboard.size() != (size_t)pack.cell_w * pack.cell_h) {
        set_status("Clipboard empty (or from a pack with another cell size)");
        return;
    }
    push_undo();
    for (int y = 0; y < pack.cell_h; ++y) {
        for (int x = 0; x < pack.cell_w; ++x) {
            px_at(cur_dir, cur_frame, x, y) = clipboard[(size_t)y * pack.cell_w + x];
        }
    }
    mark_dirty();
    set_status("Frame pasted");
}

static void mirror_row_from_opposite(void)
{
    const int src = MIRROR_ROW[cur_dir];
    if (src == cur_dir) {
        set_status("DOWN and UP have no opposite to mirror");
        return;
    }
    push_undo();
    for (int f = 0; f < action().frames; ++f) {
        for (int y = 0; y < pack.cell_h; ++y) {
            for (int x = 0; x < pack.cell_w; ++x) {
                px_at(cur_dir, f, x, y) = px_at(src, f, pack.cell_w - 1 - x, y);
            }
        }
    }
    mark_dirty();
    char msg[96];
    SDL_snprintf(msg, sizeof(msg), "Row mirrored from %s", SPRITE_DIR_NAMES[src]);
    set_status(msg);
}

static void auto_outline(void)
{
    push_undo();
    const int w = pack.cell_w;
    const int h = pack.cell_h;
    std::vector<Uint32> src((size_t)w * h);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            src[(size_t)y * w + x] = px_at(cur_dir, cur_frame, x, y);
        }
    }
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            if (src[(size_t)y * w + x] >> 24) {
                continue;
            }
            const bool near =
                (x > 0 && (src[(size_t)y * w + x - 1] >> 24)) ||
                (x < w - 1 && (src[(size_t)y * w + x + 1] >> 24)) ||
                (y > 0 && (src[(size_t)(y - 1) * w + x] >> 24)) ||
                (y < h - 1 && (src[(size_t)(y + 1) * w + x] >> 24));
            if (near) {
                px_at(cur_dir, cur_frame, x, y) = 0xff0a0608;
            }
        }
    }
    mark_dirty();
    set_status("Outline added");
}

static bool canvas_pixel(float mx, float my, int *x, int *y)
{
    if (mode != MODE_EDIT || !has_pack()) {
        return false;
    }
    if (mx < CANVAS_X || mx >= CANVAS_X + CANVAS_W || my < CANVAS_Y || my >= CANVAS_Y + CANVAS_H) {
        return false;
    }
    const int cx = (int)SDL_floorf((mx - canvas_ox) / canvas_zoom);
    const int cy = (int)SDL_floorf((my - canvas_oy) / canvas_zoom);
    *x = cx;
    *y = cy;
    return cx >= 0 && cx < pack.cell_w && cy >= 0 && cy < pack.cell_h;
}

static void apply_tool_at(int x, int y, bool erase, bool start)
{
    if (tool == TOOL_PICKER) {
        const Uint32 c = px_at(cur_dir, cur_frame, x, y);
        if (c >> 24) {
            color = c;
        }
        return;
    }
    if (tool == TOOL_FILL && !erase) {
        if (start) {
            flood_fill(x, y, color);
        }
        return;
    }
    const Uint32 c = (erase || tool == TOOL_ERASER) ? 0u : color;
    if (start || last_px < 0) {
        put_pixel(x, y, c);
    } else {
        draw_line(last_px, last_py, x, y, c);
    }
    last_px = x;
    last_py = y;
}

/* ------------------------------------------------------------------------- */
/* Character creator data                                                     */
/* ------------------------------------------------------------------------- */

/*
 * A creator base (assets/bases/<name>/) has a "nocoat" and optionally a "coat" pack
 * whose pixels are maps instead of colors: R = body-part region, G = lighting
 * (170 = 1.0), B = 255 on the lighter facet of two-tone parts, A = coverage.
 * Region ids must match dude_parts.py.
 */
enum Region {
    R_NONE, R_FACE, R_HAIR, R_BEARD, R_COLLAR, R_CHEST, R_TORSO, R_TAILS, R_FOREARM, R_HANDS,
    R_WATCH_STRAP, R_WATCH_FACE, R_MONOGRAM, R_PANTS, R_SHOES, R_BOOT_TOP,
    R_FLAG_RED, R_FLAG_WHITE, R_FLAG_BLUE, R_COUNT
};

enum Slot {
    S_SKIN, S_HAIR, S_BEARD, S_TOP, S_VEST, S_COLLAR, S_MONOGRAM, S_COAT, S_GLOVES, S_WATCH,
    S_PANTS, S_SHOES, S_COUNT
};
static const char *const SLOT_KEYS[S_COUNT] = {
    "skin", "hair", "beard", "top", "vest", "collar", "monogram", "coat", "gloves", "watch",
    "pants", "shoes"
};

enum Toggle {
    T_BEARD, T_VEST, T_MONOGRAM, T_COAT, T_ROLLED, T_GLOVES, T_WATCH, T_BOOTS, T_FLAG, T_COUNT
};
static const char *const TOGGLE_KEYS[T_COUNT] = {
    "beard", "vest", "monogram", "coat", "rolled_sleeves", "gloves", "watch", "boots", "us_flag"
};

struct Recipe {
    Uint32 color[S_COUNT];
    bool on[T_COUNT];
};

static const Uint32 WATCH_FACE_COLOR = 0xffe1e1eb;
static const Uint32 FLAG_RED_COLOR = 0xffb22234;
static const Uint32 FLAG_WHITE_COLOR = 0xffececec;
static const Uint32 FLAG_BLUE_COLOR = 0xff32346e;

static Recipe preset_classic(void)
{
    Recipe r{};
    r.color[S_SKIN] = 0xffc89678;
    r.color[S_HAIR] = 0xff7c7c7e;
    r.color[S_BEARD] = 0xff6a6a6c;
    r.color[S_TOP] = 0xff1c2448;
    r.color[S_VEST] = 0xff1c2448;
    r.color[S_COLLAR] = 0xff1c2448;
    r.color[S_MONOGRAM] = 0xffc8161c;
    r.color[S_COAT] = 0xff17101c;
    r.color[S_GLOVES] = 0xff471010;
    r.color[S_WATCH] = 0xff121216;
    r.color[S_PANTS] = 0xff52709c;
    r.color[S_SHOES] = 0xff221a18;
    r.on[T_MONOGRAM] = true;
    r.on[T_ROLLED] = true;
    r.on[T_WATCH] = true;
    r.on[T_FLAG] = true;
    return r;
}

static Recipe preset_postal_dude(void)
{
    Recipe r = preset_classic();
    r.color[S_HAIR] = 0xff52301a;
    r.color[S_BEARD] = 0xff40250f;
    r.color[S_TOP] = 0xff17101c;
    r.color[S_VEST] = 0xff6a1212;
    r.color[S_COLLAR] = 0xffd0d6d4;
    r.color[S_PANTS] = 0xff1c2240;
    r.color[S_SHOES] = 0xff17101c;
    r.on[T_BEARD] = true;
    r.on[T_VEST] = true;
    r.on[T_MONOGRAM] = false;
    r.on[T_COAT] = true;
    r.on[T_GLOVES] = true;
    r.on[T_WATCH] = false;
    r.on[T_BOOTS] = true;
    r.on[T_FLAG] = false;
    return r;
}

/* Which color every region gets for a recipe; this is where the options become clothes. */
static void recipe_region_colors(const Recipe &r, Uint32 out[R_COUNT])
{
    const Uint32 upper = r.on[T_COAT] ? r.color[S_COAT] : r.color[S_TOP];
    const Uint32 chest = r.on[T_VEST] ? r.color[S_VEST] : (r.on[T_COAT] ? r.color[S_TOP] : upper);
    const Uint32 forearm = r.on[T_ROLLED] ? r.color[S_SKIN] : upper;
    out[R_NONE] = 0xffff00ff;
    out[R_FACE] = r.color[S_SKIN];
    out[R_HAIR] = r.color[S_HAIR];
    out[R_BEARD] = r.on[T_BEARD] ? r.color[S_BEARD] : r.color[S_SKIN];
    out[R_COLLAR] = r.color[S_COLLAR];
    out[R_CHEST] = chest;
    out[R_TORSO] = upper;
    out[R_TAILS] = r.color[S_COAT];
    out[R_FOREARM] = forearm;
    out[R_HANDS] = r.on[T_GLOVES] ? r.color[S_GLOVES] : r.color[S_SKIN];
    out[R_WATCH_STRAP] = r.on[T_WATCH] ? r.color[S_WATCH] : forearm;
    out[R_WATCH_FACE] = r.on[T_WATCH] ? WATCH_FACE_COLOR : forearm;
    out[R_MONOGRAM] = r.on[T_MONOGRAM] ? r.color[S_MONOGRAM] : chest;
    out[R_PANTS] = r.color[S_PANTS];
    out[R_SHOES] = r.color[S_SHOES];
    out[R_BOOT_TOP] = r.on[T_BOOTS] ? r.color[S_SHOES] : r.color[S_PANTS];
    out[R_FLAG_RED] = r.on[T_FLAG] ? FLAG_RED_COLOR : upper;
    out[R_FLAG_WHITE] = r.on[T_FLAG] ? FLAG_WHITE_COLOR : upper;
    out[R_FLAG_BLUE] = r.on[T_FLAG] ? FLAG_BLUE_COLOR : upper;
}

static void compose_sheet(const SDL_Surface *map, SDL_Surface *dst, const Uint32 colors[R_COUNT])
{
    for (int y = 0; y < map->h; ++y) {
        const Uint32 *src = (const Uint32 *)((const Uint8 *)map->pixels + y * map->pitch);
        Uint32 *out = (Uint32 *)((Uint8 *)dst->pixels + y * dst->pitch);
        for (int x = 0; x < map->w; ++x) {
            const Uint32 m = src[x];
            if (!(m >> 24)) {
                out[x] = 0;
                continue;
            }
            const int id = (int)((m >> 16) & 0xff);
            const Uint32 c = colors[id < R_COUNT ? id : 0];
            const int light = (int)((m >> 8) & 0xff) * ((m & 0xff) > 127 ? 118 : 100);
            const int r = SDL_min(255, (int)((c >> 16) & 0xff) * light / 17000);
            const int g = SDL_min(255, (int)((c >> 8) & 0xff) * light / 17000);
            const int b = SDL_min(255, (int)(c & 0xff) * light / 17000);
            out[x] = 0xff000000u | ((Uint32)r << 16) | ((Uint32)g << 8) | (Uint32)b;
        }
    }
}

struct CreatorBase {
    std::string dir;
    std::string label;
};

static std::vector<CreatorBase> bases;
static int cur_base = -1;
static SpritePack base_coat;
static SpritePack base_nocoat;
static bool base_has_coat = false;
static Recipe recipe = preset_classic();
static int active_slot = S_TOP;
static int creator_action = 0;
static SDL_Texture *creator_tex = nullptr;
static int creator_tex_action = -1;
static bool creator_tex_coat = false;
static bool creator_dirty = true;
/* Like fit_box, for the creator base; w == 0 until the first sheet is loaded. */
static SDL_Rect creator_box = {0, 0, 0, 0};

static SpritePack &creator_variant(void)
{
    return (recipe.on[T_COAT] && base_has_coat) ? base_coat : base_nocoat;
}

static bool creator_ready(void)
{
    return cur_base >= 0 && !creator_variant().actions.empty();
}

static void scan_bases(void)
{
    bases.clear();
    const std::string root = std::string(Asset_Root()) + "assets/bases";
    int count = 0;
    char **names = SDL_GlobDirectory(root.c_str(), "*", 0, &count);
    if (!names) {
        return;
    }
    std::vector<std::string> sorted(names, names + count);
    SDL_free(names);
    std::sort(sorted.begin(), sorted.end());
    for (const std::string &name : sorted) {
        SDL_PathInfo info;
        const std::string dir = root + "/" + name;
        if (SDL_GetPathInfo((dir + "/nocoat/character.ini").c_str(), &info) && info.type == SDL_PATHTYPE_FILE) {
            bases.push_back({dir, name});
        }
    }
}

static void free_creator_texture(void)
{
    if (creator_tex) {
        SDL_DestroyTexture(creator_tex);
        creator_tex = nullptr;
    }
    creator_tex_action = -1;
}

static void open_base(int index)
{
    if (index < 0 || index >= (int)bases.size()) {
        return;
    }
    free_creator_texture();
    SpritePack_Free(&base_coat);
    SpritePack_Free(&base_nocoat);
    cur_base = -1;
    if (!SpritePack_Load(&base_nocoat, (bases[index].dir + "/nocoat").c_str(), false)) {
        set_status("Couldn't load that base");
        return;
    }
    base_has_coat = SpritePack_Load(&base_coat, (bases[index].dir + "/coat").c_str(), false);
    cur_base = index;
    creator_box = {0, 0, 0, 0};
    const int idle = SpritePack_Find(&base_nocoat, "idle");
    creator_action = idle >= 0 ? idle : 0;
    creator_dirty = true;
}

static void creator_select_action(int index)
{
    if (index >= 0 && index < (int)creator_variant().actions.size()) {
        creator_action = index;
        creator_dirty = true;
        restart_anim();
    }
}

/* Recompose the current action when the recipe, action or coat variant changed. */
static void sync_creator_texture(void)
{
    if (!creator_ready()) {
        return;
    }
    SpritePack &variant = creator_variant();
    const bool coat = &variant == &base_coat;
    if (!creator_dirty && creator_tex && creator_tex_action == creator_action && creator_tex_coat == coat) {
        return;
    }
    creator_action = SDL_min(creator_action, (int)variant.actions.size() - 1);
    for (int i = 0; i < (int)base_coat.actions.size(); ++i) {
        if (!(coat && i == creator_action)) {
            SpritePack_FreeSheet(&base_coat, i);
        }
    }
    for (int i = 0; i < (int)base_nocoat.actions.size(); ++i) {
        if (!(!coat && i == creator_action)) {
            SpritePack_FreeSheet(&base_nocoat, i);
        }
    }
    if (!SpritePack_LoadSheet(&variant, creator_action)) {
        return;
    }
    const SDL_Surface *map = variant.actions[creator_action].sheet;
    if (creator_box.w == 0) {
        creator_box = sheet_bbox(map, variant.cell_w, variant.cell_h);
    }
    if (!creator_tex || creator_tex->w != map->w || creator_tex->h != map->h) {
        free_creator_texture();
        creator_tex = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
                                        map->w, map->h);
        SDL_SetTextureScaleMode(creator_tex, SDL_SCALEMODE_NEAREST);
        SDL_SetTextureBlendMode(creator_tex, SDL_BLENDMODE_BLEND);
    }
    SDL_Surface *colored = SDL_CreateSurface(map->w, map->h, SDL_PIXELFORMAT_ARGB8888);
    Uint32 colors[R_COUNT];
    recipe_region_colors(recipe, colors);
    compose_sheet(map, colored, colors);
    SDL_UpdateTexture(creator_tex, nullptr, colored->pixels, colored->pitch);
    SDL_DestroySurface(colored);
    creator_tex_action = creator_action;
    creator_tex_coat = coat;
    creator_dirty = false;
}

static std::string recipe_to_text(const Recipe &r)
{
    std::string out;
    char line[64];
    for (int i = 0; i < S_COUNT; ++i) {
        SDL_snprintf(line, sizeof(line), "color.%s=%06X\n", SLOT_KEYS[i], r.color[i] & 0xffffff);
        out += line;
    }
    for (int i = 0; i < T_COUNT; ++i) {
        SDL_snprintf(line, sizeof(line), "%s=%d\n", TOGGLE_KEYS[i], r.on[i] ? 1 : 0);
        out += line;
    }
    return out;
}

static bool load_recipe(const std::string &path, Recipe *r)
{
    size_t size = 0;
    char *text = (char *)SDL_LoadFile(path.c_str(), &size);
    if (!text) {
        return false;
    }
    for (char *line = text; line && *line;) {
        char *next = std::strchr(line, '\n');
        if (next) {
            *next++ = '\0';
        }
        char *eq = std::strchr(line, '=');
        if (eq) {
            *eq = '\0';
            const char *value = eq + 1;
            for (int i = 0; i < S_COUNT; ++i) {
                if (std::strncmp(line, "color.", 6) == 0 && std::strcmp(line + 6, SLOT_KEYS[i]) == 0) {
                    r->color[i] = 0xff000000u | (Uint32)SDL_strtoul(value, nullptr, 16);
                }
            }
            for (int i = 0; i < T_COUNT; ++i) {
                if (std::strcmp(line, TOGGLE_KEYS[i]) == 0) {
                    r->on[i] = SDL_atoi(value) != 0;
                }
            }
        }
        line = next;
    }
    SDL_free(text);
    return true;
}

/* Bakes every action of a base variant with the current recipe into a normal sprite pack. */
static bool bake_variant(SpritePack &variant, const std::string &dir, const char *name, const char *role)
{
    SDL_CreateDirectory(dir.c_str());
    int count = 0;
    if (char **old = SDL_GlobDirectory(dir.c_str(), "*.png", 0, &count)) {
        for (int i = 0; i < count; ++i) {
            SDL_RemovePath((dir + "/" + old[i]).c_str());
        }
        SDL_free(old);
    }
    Uint32 colors[R_COUNT];
    recipe_region_colors(recipe, colors);
    SpritePack out;
    out.dir = dir;
    out.name = name;
    out.role = role;
    out.source = bases[cur_base].label;
    out.cell_w = variant.cell_w;
    out.cell_h = variant.cell_h;
    out.feet = variant.feet;
    out.scale = variant.scale;
    bool ok = true;
    for (int i = 0; i < (int)variant.actions.size(); ++i) {
        const bool was_loaded = variant.actions[i].sheet != nullptr;
        if (!SpritePack_LoadSheet(&variant, i)) {
            ok = false;
            continue;
        }
        const SDL_Surface *map = variant.actions[i].sheet;
        SDL_Surface *colored = SDL_CreateSurface(map->w, map->h, SDL_PIXELFORMAT_ARGB8888);
        compose_sheet(map, colored, colors);
        ok = SDL_SavePNG(colored, (dir + "/" + variant.actions[i].name + ".png").c_str()) && ok;
        SDL_DestroySurface(colored);
        if (!was_loaded) {
            SpritePack_FreeSheet(&variant, i);
        }
        SpriteAction meta = variant.actions[i];
        meta.sheet = nullptr;
        out.actions.push_back(meta);
    }
    ok = SpritePack_Save(&out) && ok;
    const std::string text = recipe_to_text(recipe);
    ok = SDL_SaveFile((dir + "/recipe.ini").c_str(), text.data(), text.size()) && ok;
    return ok;
}

/* Bakes the character, plus its normal-quality version into dir/hd when the base has one
   (assets/bases/<name>/<coat|nocoat>/hd). */
static bool export_creation(const std::string &dir, const char *name, const char *role)
{
    bool ok = bake_variant(creator_variant(), dir, name, role);
    const bool coat = &creator_variant() == &base_coat;
    SpritePack hd;
    const std::string hd_dir = bases[cur_base].dir + (coat ? "/coat/hd" : "/nocoat/hd");
    if (SpritePack_Load(&hd, hd_dir.c_str(), false)) {
        ok = bake_variant(hd, dir + "/hd", name, role) && ok;
        SpritePack_Free(&hd);
    }
    return ok;
}

/* ------------------------------------------------------------------------- */
/* App                                                                        */
/* ------------------------------------------------------------------------- */

SDL_AppResult SDL_AppInit(void **appstate, int argc, char *argv[])
{
    (void)appstate;
    SDL_SetAppMetadata("CONFIDENTIAL Sprite Editor", "2.0", "com.confidential.editor");
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("Couldn't initialize SDL: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }
    if (!SDL_CreateWindowAndRenderer("CONFIDENTIAL - Sprite Editor", WIN_W, WIN_H,
                                     SDL_WINDOW_RESIZABLE | SDL_WINDOW_MAXIMIZED, &window, &renderer)) {
        SDL_Log("Couldn't create window: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }
    SDL_SetRenderLogicalPresentation(renderer, WIN_W, WIN_H, SDL_LOGICAL_PRESENTATION_LETTERBOX);
    SDL_SetRenderVSync(renderer, 1);

    /* SpriteEditor --export <recipe.ini> <pack folder> [name] [role] bakes a creator recipe
       without UI; the role defaults to "player" for a folder named player, else "npc". */
    if (argc >= 4 && std::strcmp(argv[1], "--export") == 0) {
        scan_bases();
        open_base(0);
        if (!creator_ready() || !load_recipe(argv[2], &recipe)) {
            SDL_Log("Couldn't load the creator base or the recipe %s", argv[2]);
            return SDL_APP_FAILURE;
        }
        std::string dir = argv[3];
        while (dir.size() > 1 && dir.back() == '/') {
            dir.pop_back();
        }
        const bool player = dir.substr(dir.find_last_of('/') + 1) == "player";
        const char *name = argc >= 5 ? argv[4] : "Personaje";
        const char *role = argc >= 6 ? argv[5] : (player ? "player" : "npc");
        const bool ok = export_creation(dir, name, role);
        SDL_Log("Export to %s %s", argv[3], ok ? "finished" : "had errors");
        return ok ? SDL_APP_SUCCESS : SDL_APP_FAILURE;
    }

    /* Packs come from assets/sprites plus any folders given on the command line. */
    pack_roots.push_back(std::string(Asset_Root()) + "assets/sprites");
    for (int i = 1; i < argc; ++i) {
        pack_roots.push_back(argv[i]);
    }
    scan_packs();
    int first = 0;
    for (size_t i = 0; i < packs.size(); ++i) {
        if (packs[i].dir.size() >= 7 && packs[i].dir.compare(packs[i].dir.size() - 7, 7, "/player") == 0) {
            first = (int)i;
        }
    }
    if (packs.empty()) {
        set_status("No sprite packs found (folders with character.ini)");
    } else {
        open_pack(first);
    }
    scan_bases();
    if (!bases.empty()) {
        open_base(0);
    }
    c3_reset();
    return SDL_APP_CONTINUE;
}

/* Switches VIEW/EDIT between the pixel art pack and its hd/ variant. */
static void toggle_view_hd(void)
{
    if (unsaved) {
        set_status("Save (Ctrl+S) or reload before switching quality");
        return;
    }
    view_hd = !view_hd;
    const int keep_action = cur_action;
    const int keep_dir = cur_dir;
    open_pack(cur_pack);
    cur_dir = keep_dir;
    select_action(SDL_min(keep_action, (int)pack.actions.size() - 1));
    if (view_hd && pack.scale <= 1) {
        set_status("This pack has no normal-quality (hd/) version");
    }
}

static void rotate_dir(int step)
{
    cur_dir = (cur_dir + step + SPRITE_DIRS) % SPRITE_DIRS;
}

static void handle_key(const SDL_KeyboardEvent &k)
{
    const bool ctrl = (k.mod & SDL_KMOD_CTRL) != 0;
    if (mode == MODE_CREATE && !classic_creator_on() && c3_name_key(k)) {
        return;
    }
    switch (k.scancode) {
        case SDL_SCANCODE_TAB:
            mode = (Mode)((mode + 1) % 3);
            return;
        case SDL_SCANCODE_SPACE:
            playing = !playing;
            restart_anim();
            return;
        case SDL_SCANCODE_LEFTBRACKET:
            speed = SDL_max(0.25f, speed * 0.5f);
            return;
        case SDL_SCANCODE_RIGHTBRACKET:
            speed = SDL_min(4.0f, speed * 2.0f);
            return;
        case SDL_SCANCODE_PAGEUP:
            open_pack((cur_pack - 1 + (int)packs.size()) % SDL_max(1, (int)packs.size()));
            return;
        case SDL_SCANCODE_PAGEDOWN:
            open_pack((cur_pack + 1) % SDL_max(1, (int)packs.size()));
            return;
        case SDL_SCANCODE_S:
            if (ctrl) {
                save_pack();
                return;
            }
            break;
        default:
            break;
    }
    if (mode == MODE_CREATE && !classic_creator_on()) {
        switch (k.scancode) {
            case SDL_SCANCODE_LEFT: rotate_dir(-1); break;
            case SDL_SCANCODE_RIGHT: rotate_dir(1); break;
            case SDL_SCANCODE_R: auto_rotate = !auto_rotate; break;
            default: break;
        }
        return;
    }
    if (mode == MODE_CREATE) {
        if (!creator_ready()) {
            return;
        }
        const int n = (int)creator_variant().actions.size();
        switch (k.scancode) {
            case SDL_SCANCODE_LEFT: rotate_dir(-1); break;
            case SDL_SCANCODE_RIGHT: rotate_dir(1); break;
            case SDL_SCANCODE_UP: creator_select_action((creator_action - 1 + n) % n); break;
            case SDL_SCANCODE_DOWN: creator_select_action((creator_action + 1) % n); break;
            case SDL_SCANCODE_R: auto_rotate = !auto_rotate; break;
            case SDL_SCANCODE_RETURN: restart_anim(); break;
            default: break;
        }
        return;
    }
    if (!has_pack()) {
        return;
    }
    const int n_actions = (int)pack.actions.size();

    if (mode == MODE_VIEW) {
        switch (k.scancode) {
            case SDL_SCANCODE_LEFT: rotate_dir(-1); break;
            case SDL_SCANCODE_RIGHT: rotate_dir(1); break;
            case SDL_SCANCODE_UP: select_action((cur_action - 1 + n_actions) % n_actions); break;
            case SDL_SCANCODE_DOWN: select_action((cur_action + 1) % n_actions); break;
            case SDL_SCANCODE_R: auto_rotate = !auto_rotate; break;
            case SDL_SCANCODE_RETURN: restart_anim(); break;
            case SDL_SCANCODE_H: toggle_view_hd(); break;
            default: break;
        }
        return;
    }

    switch (k.scancode) {
        case SDL_SCANCODE_Z: if (ctrl) undo(); break;
        case SDL_SCANCODE_C: if (ctrl) copy_frame(); break;
        case SDL_SCANCODE_V: if (ctrl) paste_frame(); break;
        case SDL_SCANCODE_B: tool = TOOL_PENCIL; break;
        case SDL_SCANCODE_E: tool = TOOL_ERASER; break;
        case SDL_SCANCODE_G: tool = TOOL_FILL; break;
        case SDL_SCANCODE_I: tool = TOOL_PICKER; break;
        case SDL_SCANCODE_M: mirror_draw = !mirror_draw; break;
        case SDL_SCANCODE_O: onion = !onion; break;
        case SDL_SCANCODE_H: flip_frame(); break;
        case SDL_SCANCODE_K: auto_outline(); break;
        case SDL_SCANCODE_DELETE: clear_frame(); break;
        case SDL_SCANCODE_F: canvas_fit(); break;
        case SDL_SCANCODE_EQUALS: canvas_zoom_at(canvas_zoom + 1, CANVAS_X + CANVAS_W * 0.5f, CANVAS_Y + CANVAS_H * 0.5f); break;
        case SDL_SCANCODE_MINUS: canvas_zoom_at(canvas_zoom - 1, CANVAS_X + CANVAS_W * 0.5f, CANVAS_Y + CANVAS_H * 0.5f); break;
        case SDL_SCANCODE_LEFT: cur_frame = (cur_frame + action().frames - 1) % action().frames; break;
        case SDL_SCANCODE_RIGHT: cur_frame = (cur_frame + 1) % action().frames; break;
        case SDL_SCANCODE_UP: rotate_dir(-1); break;
        case SDL_SCANCODE_DOWN: rotate_dir(1); break;
        case SDL_SCANCODE_A: select_action((cur_action - 1 + n_actions) % n_actions); break;
        case SDL_SCANCODE_D: select_action((cur_action + 1) % n_actions); break;
        default: break;
    }
}

SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event)
{
    (void)appstate;
    if (event->type == SDL_EVENT_QUIT) {
        return SDL_APP_SUCCESS;
    }
    if (event->type == SDL_EVENT_MOUSE_MOTION || event->type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
        event->type == SDL_EVENT_MOUSE_BUTTON_UP || event->type == SDL_EVENT_MOUSE_WHEEL) {
        SDL_ConvertEventToRenderCoordinates(renderer, event);
    }

    switch (event->type) {
        case SDL_EVENT_KEY_DOWN:
            handle_key(event->key);
            break;

        case SDL_EVENT_TEXT_INPUT:
            c3_text_input(event->text.text);
            break;

        case SDL_EVENT_MOUSE_WHEEL: {
            const int step = event->wheel.y > 0 ? 1 : (event->wheel.y < 0 ? -1 : 0);
            const float mx = event->wheel.mouse_x;
            const float my = event->wheel.mouse_y;
            const bool in_view = mx >= VIEW_X && mx < VIEW_X + VIEW_W && my >= VIEW_Y && my < VIEW_Y + VIEW_H;
            if (mode == MODE_VIEW && mx < 310.0f) {
                pack_scroll = SDL_max(0, pack_scroll - step);
            } else if (mode == MODE_EDIT && in_view && mx < CANVAS_X + CANVAS_W && has_pack()) {
                canvas_zoom_at(canvas_zoom + step * SDL_max(1, canvas_zoom / 4), mx, my);
            } else if (mode != MODE_EDIT && in_view) {
                view_zoom_step = SDL_clamp(view_zoom_step + step, -8, 8);
            }
            break;
        }

        case SDL_EVENT_MOUSE_BUTTON_DOWN: {
            mouse_x = event->button.x;
            mouse_y = event->button.y;
            int x;
            int y;
            const bool on_canvas = canvas_pixel(mouse_x, mouse_y, &x, &y);
            if (event->button.button == SDL_BUTTON_LEFT) {
                left_down = true;
                click_pending = !on_canvas;
            } else if (event->button.button == SDL_BUTTON_RIGHT) {
                right_down = true;
                rclick_pending = !on_canvas;
            } else if (event->button.button == SDL_BUTTON_MIDDLE) {
                middle_down = true;
            }
            if (on_canvas && (event->button.button == SDL_BUTTON_LEFT || event->button.button == SDL_BUTTON_RIGHT)) {
                const bool alt = (SDL_GetModState() & SDL_KMOD_ALT) != 0;
                if (alt) {
                    const Uint32 c = px_at(cur_dir, cur_frame, x, y);
                    if (c >> 24) {
                        color = c;
                    }
                } else {
                    if (tool != TOOL_PICKER) {
                        push_undo();
                    }
                    apply_tool_at(x, y, event->button.button == SDL_BUTTON_RIGHT, true);
                }
            }
            break;
        }

        case SDL_EVENT_MOUSE_BUTTON_UP:
            if (event->button.button == SDL_BUTTON_LEFT) {
                left_down = false;
            } else if (event->button.button == SDL_BUTTON_RIGHT) {
                right_down = false;
            } else if (event->button.button == SDL_BUTTON_MIDDLE) {
                middle_down = false;
            }
            last_px = -1;
            last_py = -1;
            slider_drag = -1;
            break;

        case SDL_EVENT_MOUSE_MOTION: {
            if (middle_down && mode == MODE_EDIT) {
                canvas_ox += event->motion.x - mouse_x;
                canvas_oy += event->motion.y - mouse_y;
            }
            mouse_x = event->motion.x;
            mouse_y = event->motion.y;
            int x;
            int y;
            if ((left_down || right_down) && last_px >= 0 && canvas_pixel(mouse_x, mouse_y, &x, &y) &&
                (tool == TOOL_PENCIL || tool == TOOL_ERASER)) {
                apply_tool_at(x, y, right_down && !left_down, false);
            }
            break;
        }

        default:
            break;
    }
    return SDL_APP_CONTINUE;
}

/* ------------------------------------------------------------------------- */
/* UI helpers                                                                 */
/* ------------------------------------------------------------------------- */

static void text(float x, float y, const char *s, Uint8 r = 230, Uint8 g = 230, Uint8 b = 230, float scale = 1.0f)
{
    SDL_SetRenderScale(renderer, scale, scale);
    SDL_SetRenderDrawColor(renderer, r, g, b, 255);
    SDL_RenderDebugText(renderer, x / scale, y / scale, s);
    SDL_SetRenderScale(renderer, 1.0f, 1.0f);
}

static void header(float x, float y, const char *s)
{
    text(x, y, s, 220, 28, 28, 2.0f);
}

static void fill(float x, float y, float w, float h, Uint32 c)
{
    SDL_SetRenderDrawColor(renderer, (c >> 16) & 0xff, (c >> 8) & 0xff, c & 0xff, (c >> 24) & 0xff);
    SDL_FRect r = {x, y, w, h};
    SDL_RenderFillRect(renderer, &r);
}

static void frame_rect(float x, float y, float w, float h, Uint32 c)
{
    SDL_SetRenderDrawColor(renderer, (c >> 16) & 0xff, (c >> 8) & 0xff, c & 0xff, 255);
    SDL_FRect r = {x, y, w, h};
    SDL_RenderRect(renderer, &r);
}

static bool hovered(float x, float y, float w, float h)
{
    return mouse_x >= x && mouse_x < x + w && mouse_y >= y && mouse_y < y + h;
}

static bool button(float x, float y, float w, float h, const char *label, bool active = false)
{
    const bool hot = hovered(x, y, w, h);
    fill(x, y, w, h, active ? 0xffb81c28 : (hot ? 0xff3a3440 : 0xff26222a));
    frame_rect(x, y, w, h, active ? 0xffe0404a : 0xff4a4450);
    const int max_chars = SDL_max(1, (int)((w - 8.0f) / 8.0f));
    char clipped[96];
    SDL_snprintf(clipped, sizeof(clipped), "%.*s", max_chars, label);
    text(x + (w - (float)SDL_strlen(clipped) * 8.0f) * 0.5f, y + (h - 8.0f) * 0.5f, clipped);
    if (hot && click_pending) {
        click_pending = false;
        return true;
    }
    return false;
}

/* List entry: name on the left (clipped with ".."), dim tag on the right. */
static bool list_button(float x, float y, float w, float h, const char *name, const char *tag, bool active)
{
    const bool hot = hovered(x, y, w, h);
    fill(x, y, w, h, active ? 0xffb81c28 : (hot ? 0xff3a3440 : 0xff26222a));
    frame_rect(x, y, w, h, active ? 0xffe0404a : 0xff4a4450);
    const float tag_w = (float)SDL_strlen(tag) * 8.0f;
    const int max_chars = SDL_max(3, (int)((w - 24.0f - tag_w) / 8.0f));
    char clipped[96];
    if ((int)SDL_strlen(name) > max_chars) {
        SDL_snprintf(clipped, sizeof(clipped), "%.*s..", max_chars - 2, name);
    } else {
        SDL_strlcpy(clipped, name, sizeof(clipped));
    }
    const float ty = y + (h - 8.0f) * 0.5f;
    text(x + 8, ty, clipped);
    text(x + w - 8 - tag_w, ty, tag, active ? 255 : 120, active ? 190 : 120, active ? 190 : 130);
    if (hot && click_pending) {
        click_pending = false;
        return true;
    }
    return false;
}

/* R, G or B slider of a color, with its value; `id` identifies the drag. Returns true on change. */
static bool color_slider(float x, float y, float w, int channel, Uint32 *c, int id)
{
    static const char *const names[3] = {"R", "G", "B"};
    static const Uint32 bars[3] = {0xffc03030, 0xff30a040, 0xff3060c0};
    const int shift = 16 - channel * 8;
    const float sx = x + 18.0f;
    const int v = (int)((*c >> shift) & 0xff);
    text(x, y + 6, names[channel]);
    fill(sx, y, w, 20, 0xff1a181c);
    fill(sx, y, w * v / 255.0f, 20, bars[channel]);
    frame_rect(sx, y, w, 20, 0xff4a4450);
    char value[8];
    SDL_snprintf(value, sizeof(value), "%3d", v);
    text(sx + w + 8, y + 6, value, 170, 170, 170);
    if (hovered(sx, y, w, 20) && click_pending) {
        slider_drag = id;
        click_pending = false;
    }
    if (slider_drag == id && left_down) {
        const int nv = SDL_clamp((int)SDL_roundf((mouse_x - sx) * 255.0f / w), 0, 255);
        if (nv != v) {
            *c = (*c & ~(0xffu << shift)) | ((Uint32)nv << shift) | 0xff000000u;
            return true;
        }
    }
    return false;
}

static void clip(float x, float y, float w, float h)
{
    const SDL_Rect r = {(int)x, (int)y, (int)w, (int)h};
    SDL_SetRenderClipRect(renderer, &r);
}

static void unclip(void)
{
    SDL_SetRenderClipRect(renderer, nullptr);
}

/* Dark strip with a line of text, at the bottom of a view box. */
static void view_caption(float x, float y, float w, const char *line)
{
    fill(x, y, w, 24, 0xd0141218);
    text(x + 12, y + 8, line, 170, 170, 170);
}

static void checker(float x, float y, float w, float h, float size)
{
    for (float yy = 0; yy < h; yy += size) {
        for (float xx = 0; xx < w; xx += size) {
            const bool dark = (((int)(xx / size) + (int)(yy / size)) & 1) != 0;
            fill(x + xx, y + yy, SDL_min(size, w - xx), SDL_min(size, h - yy), dark ? 0xff2a2a30 : 0xff34343c);
        }
    }
}

/* Draws `part` (cell coordinates) of one cell with its top-left at (x, y). */
static void draw_cell_part(int act, int dir, int frame, const SDL_Rect &part, float x, float y, float zoom,
                           Uint8 alpha = 255)
{
    SDL_Texture *tex = action_tex[act];
    if (!tex) {
        return;
    }
    const SDL_FRect src = {(float)(frame * pack.cell_w + part.x), (float)(dir * pack.cell_h + part.y),
                           (float)part.w, (float)part.h};
    const SDL_FRect dst = {x, y, part.w * zoom, part.h * zoom};
    /* Smooth when shrinking (thumbnails), sharp pixels when enlarging. */
    SDL_SetTextureScaleMode(tex, zoom < 1.0f ? SDL_SCALEMODE_LINEAR : SDL_SCALEMODE_NEAREST);
    SDL_SetTextureAlphaMod(tex, alpha);
    SDL_RenderTexture(renderer, tex, &src, &dst);
    SDL_SetTextureAlphaMod(tex, 255);
    SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_NEAREST);
}

static void draw_cell(int act, int dir, int frame, float x, float y, float zoom, Uint8 alpha = 255)
{
    SDL_Texture *tex = action_tex[act];
    if (!tex) {
        return;
    }
    const float cw = (float)pack.cell_w;
    const float ch = (float)pack.cell_h;
    const SDL_FRect src = {frame * cw, dir * ch, cw, ch};
    const SDL_FRect dst = {x, y, cw * zoom, ch * zoom};
    SDL_SetTextureAlphaMod(tex, alpha);
    SDL_RenderTexture(renderer, tex, &src, &dst);
    SDL_SetTextureAlphaMod(tex, 255);
}

/* 3x3 compass of direction buttons around an empty center. */
static void ui_direction_pad(float px, float py, float bw)
{
    static const int layout[3][3] = {{5, 4, 3}, {6, -1, 2}, {7, 0, 1}};
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) {
            const int d = layout[r][c];
            if (d < 0) {
                continue;
            }
            if (button(px + c * (bw + 6.0f), py + r * 36.0f, bw, 30, SPRITE_DIR_NAMES[d], d == cur_dir)) {
                cur_dir = d;
            }
        }
    }
}

static void ui_action_buttons(float px, float py, float bw, int per_row)
{
    for (int i = 0; i < (int)pack.actions.size(); ++i) {
        const float x = px + (i % per_row) * (bw + 6.0f);
        const float y = py + (i / per_row) * 32.0f;
        if (button(x, y, bw, 26, pack.actions[i].name.c_str(), i == cur_action)) {
            select_action(i);
        }
    }
}

/* ------------------------------------------------------------------------- */
/* VIEW mode                                                                  */
/* ------------------------------------------------------------------------- */

/* Soft ground shadow under the feet; `unit` is the size of one art pixel on screen. */
static void draw_feet_shadow(float feet_x, float feet_y, float unit)
{
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 110);
    for (int i = -3; i <= 3; ++i) {
        const float half = (10.0f - SDL_abs(i) * 2.0f) * unit;
        SDL_FRect r = {feet_x - half, feet_y + i * unit * 0.7f, half * 2.0f, unit * 0.7f};
        SDL_RenderFillRect(renderer, &r);
    }
}

/* Zoom and cell position that center `box` in the view box (plus the user's wheel zoom). */
static int view_layout(const SDL_Rect &box, float *x, float *y)
{
    const int zoom = SDL_max(1, fit_zoom(VIEW_W, VIEW_H - 24.0f, box, 0.6f) + view_zoom_step);
    *x = SDL_floorf(VIEW_X + VIEW_W * 0.5f - (box.x + box.w * 0.5f) * zoom);
    *y = SDL_floorf(VIEW_Y + (VIEW_H - 24.0f) * 0.5f - (box.y + box.h * 0.5f) * zoom);
    return zoom;
}

static void ui_pack_list(void)
{
    header(20, TOP, "CHARACTERS");
    text(20, TOP + 24, "Wheel to scroll, PgUp/PgDn to switch", 140, 140, 140);

    /* Rows are either a group title (pack = -1) or a pack. */
    struct Row {
        int pack;
        const char *group;
    };
    std::vector<Row> rows;
    for (int i = 0; i < (int)packs.size(); ++i) {
        if (i == 0 || packs[i].group != packs[i - 1].group) {
            rows.push_back({-1, packs[i].group.c_str()});
        }
        rows.push_back({i, nullptr});
    }
    const float row_h = 28.0f;
    const float list_y = TOP + 44.0f;
    const float list_bottom = BOTTOM - 34.0f;
    const int visible = (int)((list_bottom - list_y) / row_h);
    const int max_scroll = SDL_max(0, (int)rows.size() - visible);
    pack_scroll = SDL_min(pack_scroll, max_scroll);
    for (int i = 0; i < visible && pack_scroll + i < (int)rows.size(); ++i) {
        const Row &row = rows[pack_scroll + i];
        const float y = list_y + i * row_h;
        if (row.pack < 0) {
            text(20, y + 12, row.group, 220, 28, 28);
            fill(20 + (SDL_strlen(row.group) + 1) * 8.0f, y + 15, 280 - SDL_strlen(row.group) * 8.0f, 1, 0xff4a4450);
            continue;
        }
        const PackEntry &e = packs[row.pack];
        if (list_button(20, y, 284, 24, e.name.c_str(), e.folder.c_str(), row.pack == cur_pack)) {
            open_pack(row.pack);
        }
    }
    if (max_scroll > 0) {
        const float track = visible * row_h - 4.0f;
        const float thumb = SDL_max(20.0f, track * visible / rows.size());
        fill(307, list_y, 3, track, 0xff26222a);
        fill(307, list_y + (track - thumb) * pack_scroll / max_scroll, 3, thumb, 0xff6a6470);
    }
    if (packs.empty()) {
        text(20, list_y + 8, "No folders with character.ini", 170, 170, 170);
    }
    if (button(20, BOTTOM - 26, 290, 26, "RESCAN FOLDERS")) {
        const std::string keep = cur_pack >= 0 ? packs[cur_pack].dir : "";
        scan_packs();
        cur_pack = -1;
        for (size_t i = 0; i < packs.size(); ++i) {
            if (packs[i].dir == keep) {
                cur_pack = (int)i;
            }
        }
    }
}

static void ui_viewer(void)
{
    fill(VIEW_X, VIEW_Y, VIEW_W, VIEW_H, 0xff1c1a22);
    frame_rect(VIEW_X - 1, VIEW_Y - 1, VIEW_W + 2, VIEW_H + 2, 0xff4a4450);
    if (!has_pack()) {
        return;
    }
    float x;
    float y;
    const int zoom = view_layout(fit_box, &x, &y);
    clip(VIEW_X, VIEW_Y, VIEW_W, VIEW_H);
    draw_feet_shadow(x + pack.cell_w * 0.5f * zoom, y + pack.feet * (float)zoom, (float)zoom * pack.scale);
    draw_cell(cur_action, cur_dir, playback_frame(), x, y, (float)zoom);

    const SpriteAction &a = action();
    char line[160];
    SDL_snprintf(line, sizeof(line), "%s  /  %s  /  frame %d of %d  /  %d fps x%.2g  /  %s  /  zoom %dx",
                 a.name.c_str(), SPRITE_DIR_NAMES[cur_dir], playback_frame() + 1, a.frames, a.fps,
                 speed, a.loop ? "loop" : "once", zoom);
    view_caption(VIEW_X, VIEW_Y + VIEW_H - 24, VIEW_W, line);
    unclip();
}

static void help_lines(float x, float y, const char *const *lines, int count)
{
    for (int i = 0; i < count; ++i, y += 16.0f) {
        if (i == 0) {
            text(x, y, lines[i], 220, 28, 28);
        } else {
            text(x, y, lines[i], 170, 170, 170);
        }
    }
}

static void ui_view_panel(void)
{
    const float px = 1180.0f;
    header(px, TOP, "DIRECTION");
    ui_direction_pad(px, TOP + 30, 124);
    if (button(px, TOP + 142, 190, 28, auto_rotate ? "AUTO-ROTATE ON" : "AUTO-ROTATE (R)", auto_rotate)) {
        auto_rotate = !auto_rotate;
    }
    if (button(px + 196, TOP + 142, 190, 28, playing ? "PAUSE (SPACE)" : "PLAY (SPACE)", !playing)) {
        playing = !playing;
        restart_anim();
    }

    header(px, TOP + 192, "ACTIONS");
    ui_action_buttons(px, TOP + 220, 124, 3);
    const float info_y = TOP + 220.0f + ((pack.actions.size() + 2) / 3) * 32.0f + 14.0f;

    char info[160];
    SDL_snprintf(info, sizeof(info), "%s  (%s)", pack.name.c_str(), pack.role.c_str());
    text(px, info_y, info);
    SDL_snprintf(info, sizeof(info), "cell %dx%d  feet row %d  %d actions%s",
                 pack.cell_w, pack.cell_h, pack.feet, (int)pack.actions.size(), pack.scale > 1 ? "  HD" : "");
    text(px, info_y + 16, info, 150, 150, 150);
    if (button(px, info_y + 34, 190, 26, "PIXEL ART", !view_hd)) {
        if (view_hd) toggle_view_hd();
    }
    if (button(px + 196, info_y + 34, 190, 26, "NORMAL (HD)", view_hd)) {
        if (!view_hd) toggle_view_hd();
    }

    static const char *const help[] = {
        "VIEW MODE",
        "",
        "Left/Right  rotate",
        "Up/Down     change action",
        "Space       play / pause",
        "[ ]         slower / faster",
        "R           auto-rotate",
        "Enter       restart action",
        "Wheel       zoom",
        "PgUp/PgDn   previous / next pack",
        "H           pixel art / normal (HD)",
    };
    help_lines(px, info_y + 76.0f, help, SDL_arraysize(help));
}

/* ------------------------------------------------------------------------- */
/* EDIT mode                                                                  */
/* ------------------------------------------------------------------------- */

static void ui_tools_panel(void)
{
    const float t = TOP;
    header(20, t, "TOOLS");
    if (button(20, t + 30, 130, 30, "PENCIL (B)", tool == TOOL_PENCIL)) tool = TOOL_PENCIL;
    if (button(160, t + 30, 130, 30, "ERASER (E)", tool == TOOL_ERASER)) tool = TOOL_ERASER;
    if (button(20, t + 66, 130, 30, "FILL (G)", tool == TOOL_FILL)) tool = TOOL_FILL;
    if (button(160, t + 66, 130, 30, "PICKER (I)", tool == TOOL_PICKER)) tool = TOOL_PICKER;
    if (button(20, t + 102, 130, 30, "MIRROR (M)", mirror_draw)) mirror_draw = !mirror_draw;
    if (button(160, t + 102, 130, 30, "ONION (O)", onion)) onion = !onion;
    if (button(20, t + 138, 130, 30, "FLIP (H)")) flip_frame();
    if (button(160, t + 138, 130, 30, "OUTLINE (K)")) auto_outline();
    if (button(20, t + 174, 130, 30, "COPY ^C")) copy_frame();
    if (button(160, t + 174, 130, 30, "PASTE ^V")) paste_frame();
    if (button(20, t + 210, 130, 30, "UNDO ^Z")) undo();
    if (button(160, t + 210, 130, 30, "CLEAR (DEL)")) clear_frame();

    header(20, t + 264, "PALETTE");
    text(20, t + 284, "L-click: use   R-click: store", 150, 150, 150);
    for (int i = 0; i < 32; ++i) {
        const float x = 20.0f + (i % 8) * 34.0f;
        const float y = t + 302.0f + (i / 8) * 34.0f;
        fill(x, y, 30, 30, palette[i]);
        frame_rect(x, y, 30, 30, palette[i] == color ? 0xffffffff : 0xff4a4450);
        if (hovered(x, y, 30, 30)) {
            if (click_pending) {
                color = palette[i];
                click_pending = false;
            }
            if (rclick_pending) {
                palette[i] = color;
                rclick_pending = false;
            }
        }
    }

    header(20, t + 450, "COLOR");
    fill(20, t + 474, 60, 60, color);
    frame_rect(20, t + 474, 60, 60, 0xffffffff);
    char hex[16];
    SDL_snprintf(hex, sizeof(hex), "#%06X", color & 0xffffff);
    text(92, t + 500, hex);
    for (int ch = 0; ch < 3; ++ch) {
        color_slider(20, t + 550.0f + ch * 30.0f, 230, ch, &color, ch);
    }

    if (button(20, t + 660, 290, 40, unsaved ? "SAVE ^S  *" : "SAVE ^S", unsaved)) save_pack();
    if (button(20, t + 708, 290, 30, "RELOAD FROM DISK")) {
        unsaved = false;
        open_pack(cur_pack);
    }
}

static void ui_canvas(void)
{
    const float z = (float)canvas_zoom;
    const float w = pack.cell_w * z;
    const float h = pack.cell_h * z;
    fill(CANVAS_X, CANVAS_Y, CANVAS_W, CANVAS_H, 0xff1c1a22);
    clip(CANVAS_X, CANVAS_Y, CANVAS_W, CANVAS_H);

    /* Checkerboard only over the visible part of the cell, in squares of whole pixels. */
    const float size = z * SDL_ceilf(8.0f / z);
    const int sx0 = SDL_max(0, (int)((CANVAS_X - canvas_ox) / size));
    const int sy0 = SDL_max(0, (int)((CANVAS_Y - canvas_oy) / size));
    const int sx1 = SDL_min((int)SDL_ceilf(w / size), (int)((CANVAS_X + CANVAS_W - canvas_ox) / size) + 1);
    const int sy1 = SDL_min((int)SDL_ceilf(h / size), (int)((CANVAS_Y + CANVAS_H - canvas_oy) / size) + 1);
    for (int sy = sy0; sy < sy1; ++sy) {
        for (int sx = sx0; sx < sx1; ++sx) {
            const float x = sx * size;
            const float y = sy * size;
            fill(canvas_ox + x, canvas_oy + y, SDL_min(size, w - x), SDL_min(size, h - y),
                 ((sx + sy) & 1) ? 0xff2a2a30 : 0xff34343c);
        }
    }
    if (onion && cur_frame > 0) {
        draw_cell(cur_action, cur_dir, cur_frame - 1, canvas_ox, canvas_oy, z, 70);
    }
    draw_cell(cur_action, cur_dir, cur_frame, canvas_ox, canvas_oy, z);

    if (canvas_zoom >= 6) {
        SDL_SetRenderDrawColor(renderer, 255, 255, 255, 18);
        const int x0 = SDL_max(0, (int)((CANVAS_X - canvas_ox) / z));
        const int x1 = SDL_min(pack.cell_w, (int)((CANVAS_X + CANVAS_W - canvas_ox) / z) + 1);
        const int y0 = SDL_max(0, (int)((CANVAS_Y - canvas_oy) / z));
        const int y1 = SDL_min(pack.cell_h, (int)((CANVAS_Y + CANVAS_H - canvas_oy) / z) + 1);
        for (int x = x0; x <= x1; ++x) {
            SDL_RenderLine(renderer, canvas_ox + x * z, canvas_oy + y0 * z, canvas_ox + x * z, canvas_oy + y1 * z);
        }
        for (int y = y0; y <= y1; ++y) {
            SDL_RenderLine(renderer, canvas_ox + x0 * z, canvas_oy + y * z, canvas_ox + x1 * z, canvas_oy + y * z);
        }
    }
    SDL_SetRenderDrawColor(renderer, 220, 28, 28, 120);
    SDL_RenderLine(renderer, canvas_ox + w * 0.5f, canvas_oy, canvas_ox + w * 0.5f, canvas_oy + h);
    SDL_RenderLine(renderer, canvas_ox, canvas_oy + pack.feet * z, canvas_ox + w, canvas_oy + pack.feet * z);
    frame_rect(canvas_ox - 1, canvas_oy - 1, w + 2, h + 2, 0xff6a6470);

    char line[128];
    int x;
    int y;
    if (canvas_pixel(mouse_x, mouse_y, &x, &y)) {
        frame_rect(canvas_ox + x * z, canvas_oy + y * z, z, z, 0xffffffff);
        SDL_snprintf(line, sizeof(line), "x %d  y %d  /  zoom %dx  /  wheel zoom, middle-drag pan, F fit", x, y, canvas_zoom);
    } else {
        SDL_snprintf(line, sizeof(line), "zoom %dx  /  wheel zoom, middle-drag pan, F fit", canvas_zoom);
    }
    view_caption(CANVAS_X, CANVAS_Y + CANVAS_H - 24, CANVAS_W, line);
    unclip();
    frame_rect(CANVAS_X - 1, CANVAS_Y - 1, CANVAS_W + 2, CANVAS_H + 2, 0xff4a4450);
}

static void ui_edit_panel(void)
{
    const float px = 880.0f;
    header(px, TOP, "DIRECTION");
    ui_direction_pad(px, TOP + 30, 86);
    if (button(px + 290, TOP + 30, 300, 30, "MIRROR ROW FROM OPPOSITE")) mirror_row_from_opposite();
    text(px + 290, TOP + 72, "Copies the opposite direction", 140, 140, 140);
    text(px + 290, TOP + 84, "flipped (LEFT <-> RIGHT), all frames.", 140, 140, 140);

    header(px, TOP + 142, "ACTIONS");
    text(px + 140, TOP + 148, "A / D", 140, 140, 140);
    ui_action_buttons(px, TOP + 168, 108, 6);
    const float frames_y = TOP + 168.0f + ((pack.actions.size() + 5) / 6) * 32.0f + 10.0f;

    header(px, frames_y, "FRAMES");
    const SpriteAction &a = action();
    const SDL_Rect &part = act_box[cur_action];
    const int per_row = SDL_min(a.frames, 12);
    const int rows = (a.frames + per_row - 1) / per_row;
    const float slot = 700.0f / per_row;
    const float tz = SDL_min(SDL_min(2.0f, (slot - 10.0f) / part.w), (220.0f / rows - 18.0f) / part.h);
    const float tw = part.w * tz;
    const float th = part.h * tz;
    for (int f = 0; f < a.frames; ++f) {
        const float fx = px + (f % per_row) * slot;
        const float fy = frames_y + 28.0f + (f / per_row) * (th + 18.0f);
        checker(fx, fy, tw, th, 6);
        draw_cell_part(cur_action, cur_dir, f, part, fx, fy, tz);
        frame_rect(fx - 2, fy - 2, tw + 4, th + 4, f == cur_frame ? 0xffe0404a : 0xff4a4450);
        char label[8];
        SDL_snprintf(label, sizeof(label), "%d", f + 1);
        text(fx + 2, fy + th + 5, label, f == cur_frame ? 255 : 150, f == cur_frame ? 80 : 150, f == cur_frame ? 80 : 150);
        if (hovered(fx, fy, tw, th) && click_pending) {
            cur_frame = f;
            click_pending = false;
        }
    }

    const float py = frames_y + 28.0f + rows * (th + 18.0f) + 12.0f;
    header(px, py, "PREVIEW");
    const float box_w = 220.0f;
    const float box_h = SDL_min(260.0f, BOTTOM - (py + 28.0f) - 20.0f);
    const float pz = SDL_min(box_w / part.w, box_h / part.h);
    fill(px, py + 28, box_w, box_h, 0xff1a2828);
    draw_cell_part(cur_action, cur_dir, playback_frame(), part, px + (box_w - part.w * pz) * 0.5f,
                   py + 28 + (box_h - part.h * pz) * 0.5f, pz);
    frame_rect(px - 1, py + 27, box_w + 2, box_h + 2, 0xff4a4450);
    char info[96];
    SDL_snprintf(info, sizeof(info), "%s %d fps x%.2g  ([ ] speed)", playing ? "PLAYING" : "PAUSED", a.fps, speed);
    text(px, py + 36 + box_h, info, 170, 170, 170);

    static const char *const help[] = {
        "EDIT MODE",
        "L-click    paint",
        "R-click    erase",
        "Alt+click  pick color",
        "Wheel, -/= zoom",
        "Middle     drag to pan",
        "F          fit to sprite",
        "Left/Right frame",
        "Up/Down    direction",
        "A / D      action",
        "Ctrl+S     save pack",
        "Feet: on the red lines",
    };
    help_lines(px + box_w + 40.0f, py + 28.0f, help, SDL_arraysize(help));
}

/* ------------------------------------------------------------------------- */

/* ------------------------------------------------------------------------- */
/* CREATE mode                                                                */
/* ------------------------------------------------------------------------- */

static const Uint32 CREATOR_PALETTE[36] = {
    0xfff2d2b6, 0xffe0b090, 0xffc89678, 0xffa87050, 0xff7a4a30, 0xff4e2e1c,
    0xff0e0c0c, 0xff3a2416, 0xff6a4422, 0xffb08850, 0xff7c7c7e, 0xffd8d8d4,
    0xff1c2448, 0xff17101c, 0xffd0d6d4, 0xff6a1212, 0xffc8161c, 0xff52709c,
    0xff8aa6c8, 0xff1c2240, 0xff2e5a2e, 0xff3c4a24, 0xffa08c60, 0xff5a3a22,
    0xff404048, 0xff8a8a90, 0xffe0c040, 0xff6a3a8a, 0xffd07a20, 0xff2a7a8a,
    0xffe88aa8, 0xff221a18, 0xff7a5030, 0xff121216, 0xffb0b0b8, 0xffc8a040
};

static int creator_frame(void)
{
    const SpriteAction &a = creator_variant().actions[creator_action];
    if (!playing) {
        return 0;
    }
    const Uint64 t = (Uint64)((SDL_GetTicks() - anim_start) * speed);
    const int f = (int)(t * (Uint64)a.fps / 1000);
    if (a.loop) {
        return f % a.frames;
    }
    return SDL_min(f % (a.frames + a.fps), a.frames - 1);
}

static void creator_changed(void)
{
    creator_dirty = true;
}

static std::string free_npc_dir(void)
{
    const std::string root = std::string(Asset_Root()) + "assets/sprites/";
    for (int n = 1;; ++n) {
        char name[32];
        SDL_snprintf(name, sizeof(name), "npc_%d", n);
        SDL_PathInfo info;
        if (!SDL_GetPathInfo((root + name).c_str(), &info)) {
            return root + name;
        }
    }
}

static void save_creation(const std::string &dir, const char *name, const char *role)
{
    if (!creator_ready()) {
        return;
    }
    /* Baking every action takes a few seconds; say so before the window stops updating. */
    SDL_SetRenderDrawColor(renderer, 16, 14, 18, 230);
    SDL_FRect veil = {0, 0, (float)WIN_W, (float)WIN_H};
    SDL_RenderFillRect(renderer, &veil);
    header(WIN_W * 0.5f - 160.0f, WIN_H * 0.5f, "EXPORTING...");
    SDL_RenderPresent(renderer);
    SDL_Log("Exporting character to %s", dir.c_str());
    const Uint64 t0 = SDL_GetTicks();
    const bool ok = export_creation(dir, name, role);
    SDL_Log("Export %s in %.1f s", ok ? "finished" : "had errors", (SDL_GetTicks() - t0) / 1000.0);
    scan_packs();
    char msg[160];
    SDL_snprintf(msg, sizeof(msg), ok ? "Saved %s" : "Save had errors: %s", dir.c_str());
    set_status(msg);
}

static void ui_creator_left(void)
{
    if (button(20, BOTTOM - 26, 290, 26, "< CREADOR COMPLETO")) {
        set_classic_creator(false);
    }
    header(20, TOP, "BASE");
    float y = TOP + 30.0f;
    for (int i = 0; i < (int)bases.size(); ++i, y += 30.0f) {
        if (button(20, y, 290, 26, bases[i].label.c_str(), i == cur_base)) {
            open_base(i);
        }
    }
    if (bases.empty()) {
        text(20, y, "No bases in assets/bases", 170, 170, 170);
        y += 20.0f;
    }

    header(20, y + 14, "PRESETS");
    y += 44.0f;
    if (button(20, y, 142, 26, "CLASSIC SUIT")) {
        recipe = preset_classic();
        creator_changed();
    }
    if (button(168, y, 142, 26, "POSTAL DUDE")) {
        recipe = preset_postal_dude();
        creator_changed();
    }

    header(20, y + 44, "SAVE");
    y += 74.0f;
    if (button(20, y, 290, 26, "SAVE AS PLAYER")) {
        const std::string dir = std::string(Asset_Root()) + "assets/sprites/player";
        const std::string name = ini_name(dir + "/character.ini", "Jugador");
        save_creation(dir, name.c_str(), "player");
    }
    if (button(20, y + 30, 290, 26, "SAVE AS NEW NPC")) {
        const std::string dir = free_npc_dir();
        save_creation(dir, dir.substr(dir.find_last_of('/') + 1).c_str(), "npc");
    }
    if (button(20, y + 60, 290, 26, "LOAD RECIPE FROM VIEW PACK")) {
        if (cur_pack >= 0 && load_recipe(packs[cur_pack].dir + "/recipe.ini", &recipe)) {
            creator_changed();
            set_status("Recipe loaded");
        } else {
            set_status("The pack selected in VIEW has no recipe.ini");
        }
    }

    header(20, y + 104, "ACTION");
    y += 134.0f;
    if (creator_ready()) {
        const SpritePack &v = creator_variant();
        for (int i = 0; i < (int)v.actions.size(); ++i) {
            const float bx = 20.0f + (i % 2) * 148.0f;
            const float by = y + (i / 2) * 28.0f;
            if (button(bx, by, 142, 24, v.actions[i].name.c_str(), i == creator_action)) {
                creator_select_action(i);
            }
        }
    }
}

static void ui_creator_view(void)
{
    fill(VIEW_X, VIEW_Y, VIEW_W, VIEW_H, 0xff1c1a22);
    frame_rect(VIEW_X - 1, VIEW_Y - 1, VIEW_W + 2, VIEW_H + 2, 0xff4a4450);
    if (!creator_ready() || !creator_tex) {
        return;
    }
    const SpritePack &v = creator_variant();
    float x;
    float y;
    const int zoom = view_layout(creator_box, &x, &y);
    clip(VIEW_X, VIEW_Y, VIEW_W, VIEW_H);
    draw_feet_shadow(x + v.cell_w * 0.5f * zoom, y + v.feet * (float)zoom, (float)zoom * v.scale);
    const int frame = creator_frame();
    const SDL_FRect src = {frame * (float)v.cell_w, cur_dir * (float)v.cell_h, (float)v.cell_w, (float)v.cell_h};
    const SDL_FRect dst = {x, y, (float)v.cell_w * zoom, (float)v.cell_h * zoom};
    SDL_RenderTexture(renderer, creator_tex, &src, &dst);

    const SpriteAction &a = v.actions[creator_action];
    char line[160];
    SDL_snprintf(line, sizeof(line), "%s  /  %s  /  frame %d of %d  /  zoom %dx   (arrows rotate / action, R auto-rotate, wheel zoom)",
                 a.name.c_str(), SPRITE_DIR_NAMES[cur_dir], frame + 1, a.frames, zoom);
    view_caption(VIEW_X, VIEW_Y + VIEW_H - 24, VIEW_W, line);
    unclip();
}

/* One option row: label, optional ON/OFF toggle, optional color swatch (click to edit). */
static void creator_row(float px, float y, const char *label, int toggle, int slot, bool enabled = true)
{
    text(px, y + 8, label, enabled ? 230 : 110, enabled ? 230 : 110, enabled ? 230 : 110);
    if (toggle >= 0 && enabled) {
        if (button(px + 170, y, 70, 24, recipe.on[toggle] ? "ON" : "OFF", recipe.on[toggle])) {
            recipe.on[toggle] = !recipe.on[toggle];
            creator_changed();
        }
    }
    if (slot >= 0 && enabled) {
        const float sx = px + 250.0f;
        fill(sx, y, 70, 24, recipe.color[slot]);
        frame_rect(sx, y, 70, 24, slot == active_slot ? 0xffffffff : 0xff4a4450);
        if (slot == active_slot) {
            frame_rect(sx - 2, y - 2, 74, 28, 0xffe0404a);
        }
        if (hovered(sx, y, 70, 24) && click_pending) {
            active_slot = slot;
            click_pending = false;
        }
    }
}

static void ui_creator_panel(void)
{
    const float px = 1180.0f;
    float y = TOP;
    const float row = 28.0f;
    header(px, y, "HEAD");
    y += 26.0f;
    creator_row(px, y, "Skin", -1, S_SKIN); y += row;
    creator_row(px, y, "Hair", -1, S_HAIR); y += row;
    creator_row(px, y, "Beard", T_BEARD, S_BEARD); y += row + 4;

    header(px, y, "TORSO");
    y += 26.0f;
    creator_row(px, y, "Top / sweater", -1, S_TOP); y += row;
    creator_row(px, y, "Vest", T_VEST, S_VEST); y += row;
    creator_row(px, y, "Collar", -1, S_COLLAR); y += row;
    creator_row(px, y, "Monogram", T_MONOGRAM, S_MONOGRAM); y += row + 4;

    header(px, y, "ARMS");
    y += 26.0f;
    creator_row(px, y, "Rolled sleeves", T_ROLLED, -1); y += row;
    creator_row(px, y, "Gloves", T_GLOVES, S_GLOVES); y += row;
    creator_row(px, y, "Watch", T_WATCH, S_WATCH); y += row;
    creator_row(px, y, "US flag (left arm)", T_FLAG, -1); y += row + 4;

    header(px, y, "LEGS");
    y += 26.0f;
    creator_row(px, y, "Pants", -1, S_PANTS); y += row;
    creator_row(px, y, "Shoes", -1, S_SHOES); y += row;
    creator_row(px, y, "Boots", T_BOOTS, -1); y += row + 4;

    header(px, y, "COAT");
    y += 26.0f;
    creator_row(px, y, base_has_coat ? "Trench coat" : "Trench coat (n/a)", T_COAT, S_COAT, base_has_coat);
    y += row + 10;

    char title[64];
    SDL_snprintf(title, sizeof(title), "COLOR: %s", SLOT_KEYS[active_slot]);
    text(px, y, title, 220, 28, 28);
    y += 16.0f;
    for (int i = 0; i < 36; ++i) {
        const float x = px + (i % 12) * 33.0f;
        const float cy = y + (i / 12) * 30.0f;
        fill(x, cy, 29, 26, CREATOR_PALETTE[i]);
        frame_rect(x, cy, 29, 26, CREATOR_PALETTE[i] == recipe.color[active_slot] ? 0xffffffff : 0xff4a4450);
        if (hovered(x, cy, 29, 26) && click_pending) {
            recipe.color[active_slot] = CREATOR_PALETTE[i];
            click_pending = false;
            creator_changed();
        }
    }
    y += 3 * 30.0f + 8.0f;
    for (int ch = 0; ch < 3; ++ch) {
        if (color_slider(px, y + ch * 26.0f, 330, ch, &recipe.color[active_slot], 10 + ch)) {
            creator_changed();
        }
    }
}

/* ------------------------------------------------------------------------- */
/* CREATE mode: full character creator                                        */
/* ------------------------------------------------------------------------- */
/*
 * Builds a character from every piece the hand-made looks use (hair styles, beards, hats,
 * hoods, capes, robes, sashes, flags...). The recipe is rendered by the Python tools
 * (custom_look.py next to make_pack.py): a creator_preview.py server for the live preview and
 * make_pack.py for the packs. Saved characters go to assets/sprites/custom/<id>/ (NPC) and
 * custom/<id>/jugador/ (playable), each with its hd/ version; the game finds them there.
 *
 * Tools folder: $CONFIDENTIAL_TOOLS, else ~/postal_extract. Python: $CONFIDENTIAL_PYTHON,
 * else python3 (python on Windows).
 */

struct OptValue {
    const char *id;
    const char *label;
};

struct OptDef {
    const char *key;   /* recipe key; nullptr for a color-only row */
    const char *label;
    std::vector<OptValue> values;
    int color;         /* COLOR3D index edited from this row, or -1 */
};

struct Color3D {
    const char *key;
    const char *label;
    Uint32 initial;
};

enum {
    C3_SKIN, C3_HAIR, C3_BEARD, C3_HEAD, C3_TOP, C3_SHIRT, C3_TIE, C3_SASH, C3_GLOVES, C3_PANTS,
    C3_SHOES, C3_OUTER, C3_LINING, C3_GEAR, C3_COUNT
};
static const Color3D COLOR3D[C3_COUNT] = {
    {"piel", "Piel", 0xffd8b496},       {"pelo", "Pelo", 0xff3a2416},     {"barba", "Barba", 0xff3a2416},
    {"cabeza", "Gorro", 0xff2a2a30},    {"superior", "Ropa", 0xff1c2448}, {"camisa", "Camisa", 0xffececec},
    {"corbata", "Corbata", 0xffa01820}, {"banda", "Banda", 0xffb81c28},   {"guantes", "Guantes", 0xff202024},
    {"pantalon", "Pantalon", 0xff30364a}, {"calzado", "Calzado", 0xff161416}, {"prenda", "Capa/tunica", 0xff1c1820},
    {"forro", "Forro", 0xff8a1c24},       {"equipo", "Equipo", 0xff464c34},
};

static const char *const OPT_GROUPS[] = {"CUERPO", "CABEZA", "TORSO", "PIERNAS", "ENCIMA", "EQUIPO"};

struct OptRow {
    int group;
    OptDef def;
};

static const std::vector<OptRow> OPTS = {
    {0, {"complexion", "Complexion", {{"normal", "Normal"}, {"robusto", "Robusto"}, {"musculoso", "Musculoso"}}, -1}},
    {0, {"altura", "Altura", {{"1.0", "Normal"}, {"1.04", "Alto"}, {"1.08", "Muy alto"}, {"0.92", "Muy bajo"},
                              {"0.96", "Bajo"}, {"bajito", "Bajito (cabeza grande)"}}, -1}},
    {0, {nullptr, "Piel", {}, C3_SKIN}},
    {1, {"pelo", "Pelo", {{"corto", "Corto"}, {"calvo", "Calvo"}, {"tupe", "Tupe"}, {"rizado", "Rizado"},
                          {"largo", "Largo"}, {"largo_rizado", "Largo rizado"}}, C3_HAIR}},
    {1, {"barba", "Barba", {{"ninguna", "Ninguna"}, {"corta", "Corta"}, {"larga", "Larga"}, {"bigote", "Bigote"},
                            {"perilla", "Perilla"}, {"bigote_cepillo", "Bigote cepillo"}}, C3_BEARD}},
    {1, {"cabeza", "Cabeza", {{"nada", "Nada"}, {"capucha", "Capucha"}, {"capucha_monje", "Capucha monje"},
                              {"sombrero", "Sombrero"}, {"gorra_militar", "Gorra militar"}, {"turbante", "Turbante"},
                              {"gorro_aluminio", "Gorro aluminio"}, {"cuernos", "Cuernos"}, {"panuelo", "Panuelo"},
                              {"mitra", "Mitra"}, {"bombin", "Bombin"}, {"sombrero_copa", "Sombrero de copa"}}, C3_HEAD}},
    {1, {"gafas", "Ojos", {{"ninguna", "Nada"}, {"gafas", "Gafas"}, {"gafas_sol", "Gafas de sol"},
                           {"pestanas", "Pestanas (ojo der.)"}}, -1}},
    {1, {"aureola", "Aureola", {{"0", "No"}, {"1", "Si"}}, -1}},
    {2, {"superior", "Ropa", {{"camiseta", "Camiseta"}, {"polo", "Polo"}, {"traje", "Traje"}, {"tirantes", "Sin mangas"},
                              {"sin_camisa", "Sin camisa"}, {"uniforme", "Uniforme"}, {"franela", "Franela"},
                              {"chaleco_tactico", "Chaleco tactico"}}, C3_TOP}},
    {2, {"mangas", "Mangas", {{"cortas", "Cortas"}, {"largas", "Largas"}}, -1}},
    {2, {nullptr, "Camisa (traje)", {}, C3_SHIRT}},
    {2, {"corbata", "Corbata", {{"0", "No"}, {"1", "Si"}}, C3_TIE}},
    {2, {"banda", "Banda", {{"ninguna", "Ninguna"}, {"banda", "Banda"}, {"presidencial", "Presidencial"}}, C3_SASH}},
    {2, {"bandera", "Bandera EEUU", {{"ninguna", "Ninguna"}, {"camiseta", "En el pecho"}, {"calzon", "Calzon"}}, -1}},
    {2, {"medallas", "Medallas", {{"0", "No"}, {"1", "Si"}}, -1}},
    {2, {"guantes", "Guantes", {{"0", "No"}, {"1", "Si"}}, C3_GLOVES}},
    {3, {"pantalon", "Pantalon", {{"largo", "Largo"}, {"corto", "Corto"}, {"calzon", "Calzon"}}, C3_PANTS}},
    {3, {"calzado", "Calzado", {{"zapatos", "Zapatos"}, {"botas", "Botas"}, {"descalzo", "Descalzo"}}, C3_SHOES}},
    {4, {"prenda", "Prenda", {{"nada", "Nada"}, {"capa", "Capa"}, {"capa_bandera", "Capa bandera"},
                              {"tunica", "Tunica / bata"}}, C3_OUTER}},
    {4, {nullptr, "Forro de capa", {}, C3_LINING}},
    {5, {"mano", "En la mano", {{"nada", "Nada"}, {"baston", "Baston"}, {"baculo", "Baculo"}}, -1}},
    {5, {"mochila", "Mochila", {{"0", "No"}, {"1", "Si"}}, C3_GEAR}},
    {5, {"tirantes_", "Tirantes", {{"0", "No"}, {"1", "Si"}}, -1}},
    {5, {"coquilla", "Coquilla", {{"0", "No"}, {"1", "Si"}}, -1}},
};

static std::vector<int> c3_value(OPTS.size(), 0);
static Uint32 c3_color[C3_COUNT];
static int c3_active_color = C3_TOP;
static bool classic_creator = false;
static char c3_name[48] = "Nuevo personaje";
static bool c3_name_editing = false;

/* creator_preview.py --serve: stays running so each preview only costs the render. */
static SDL_Process *c3_server = nullptr;
static int c3_seq_sent = 0;
static int c3_seq_shown = 0;
static Uint64 c3_last_poll = 0;
static bool c3_preview_pending = true;
static Uint64 c3_changed_at = 0;
static SDL_Texture *c3_preview_tex = nullptr;
static SDL_Rect c3_preview_box = {0, 0, 1, 1};
static int c3_cell_w = 0;
static int c3_cell_h = 0;
static std::string c3_preview_error;

static std::vector<SDL_Process *> c3_export_procs;
static int c3_export_failed = 0;
static Uint64 c3_export_started = 0;
static std::string c3_export_dir;

static void c3_reset(void)
{
    std::fill(c3_value.begin(), c3_value.end(), 0);
    for (int i = 0; i < C3_COUNT; ++i) {
        c3_color[i] = COLOR3D[i].initial;
    }
    c3_preview_pending = true;
    c3_changed_at = 0;
}

static void c3_changed(void)
{
    c3_preview_pending = true;
    c3_changed_at = SDL_GetTicks();
}

static std::string c3_env_or(const char *name, const std::string &fallback)
{
    const char *v = SDL_getenv(name);
    return v && *v ? std::string(v) : fallback;
}

static std::string c3_tools_dir(void)
{
    const char *home = SDL_getenv("HOME");
    if (!home) {
        home = SDL_getenv("USERPROFILE");
    }
    return c3_env_or("CONFIDENTIAL_TOOLS", std::string(home ? home : ".") + "/postal_extract");
}

static std::string c3_python(void)
{
#ifdef _WIN32
    return c3_env_or("CONFIDENTIAL_PYTHON", "python");
#else
    return c3_env_or("CONFIDENTIAL_PYTHON", "python3");
#endif
}

static std::string c3_work_dir(void)
{
    char *pref = SDL_GetPrefPath("confidential", "editor");
    std::string dir = pref ? pref : "./";
    SDL_free(pref);
    return dir;
}

static std::string c3_recipe_text(void)
{
    std::string out = std::string("name=") + c3_name + "\n";
    char line[96];
    for (size_t i = 0; i < OPTS.size(); ++i) {
        const OptDef &d = OPTS[i].def;
        if (d.key) {
            SDL_snprintf(line, sizeof(line), "%s=%s\n", d.key, d.values[(size_t)c3_value[i]].id);
            out += line;
        }
    }
    for (int i = 0; i < C3_COUNT; ++i) {
        SDL_snprintf(line, sizeof(line), "color.%s=%06X\n", COLOR3D[i].key, c3_color[i] & 0xffffff);
        out += line;
    }
    return out;
}

static bool c3_write_recipe(const std::string &path)
{
    const std::string text = c3_recipe_text();
    return SDL_SaveFile(path.c_str(), text.data(), text.size());
}

static bool c3_load_recipe(const std::string &path)
{
    size_t size = 0;
    char *text = (char *)SDL_LoadFile(path.c_str(), &size);
    if (!text) {
        return false;
    }
    c3_reset();
    for (char *line = text; line && *line;) {
        char *next = std::strpbrk(line, "\r\n");
        if (next) {
            *next++ = '\0';
            while (*next == '\n' || *next == '\r') {
                ++next;
            }
        }
        char *eq = std::strchr(line, '=');
        if (eq) {
            *eq = '\0';
            const char *value = eq + 1;
            if (std::strcmp(line, "name") == 0) {
                SDL_strlcpy(c3_name, value, sizeof(c3_name));
            } else if (std::strncmp(line, "color.", 6) == 0) {
                for (int i = 0; i < C3_COUNT; ++i) {
                    if (std::strcmp(line + 6, COLOR3D[i].key) == 0) {
                        c3_color[i] = 0xff000000u | (Uint32)SDL_strtoul(value, nullptr, 16);
                    }
                }
            } else {
                for (size_t i = 0; i < OPTS.size(); ++i) {
                    const OptDef &d = OPTS[i].def;
                    if (!d.key || std::strcmp(line, d.key) != 0) {
                        continue;
                    }
                    for (size_t v = 0; v < d.values.size(); ++v) {
                        if (std::strcmp(d.values[v].id, value) == 0) {
                            c3_value[i] = (int)v;
                        }
                    }
                }
            }
        }
        line = next;
    }
    SDL_free(text);
    c3_changed();
    return true;
}

static void c3_randomize(void)
{
    for (size_t i = 0; i < OPTS.size(); ++i) {
        const OptDef &d = OPTS[i].def;
        if (d.key) {
            c3_value[i] = (int)SDL_rand((Sint32)d.values.size());
        }
    }
    static const Uint32 skins[] = {0xfff2d2b6, 0xffe0b090, 0xffc89678, 0xffa87050, 0xff7a4a30, 0xff4e2e1c};
    for (int i = 0; i < C3_COUNT; ++i) {
        c3_color[i] = CREATOR_PALETTE[SDL_rand(36)];
    }
    c3_color[C3_SKIN] = skins[SDL_rand(6)];
    c3_changed();
}

/* Runs a command line in the background; its output goes to the editor's terminal. */
static SDL_Process *c3_spawn(const std::vector<std::string> &args)
{
    std::vector<const char *> argv;
    for (const std::string &a : args) {
        argv.push_back(a.c_str());
    }
    argv.push_back(nullptr);
    SDL_Process *p = SDL_CreateProcess(argv.data(), false);
    if (!p) {
        SDL_Log("Couldn't start %s: %s", args[0].c_str(), SDL_GetError());
    }
    return p;
}

static void c3_load_preview(const std::string &png)
{
    SDL_Surface *s = SDL_LoadPNG(png.c_str());
    if (!s) {
        c3_preview_error = "No se pudo leer la vista previa";
        return;
    }
    SDL_Surface *argb = SDL_ConvertSurface(s, SDL_PIXELFORMAT_ARGB8888);
    SDL_DestroySurface(s);
    if (!argb) {
        return;
    }
    c3_cell_w = argb->w / SPRITE_DIRS;
    c3_cell_h = argb->h;
    c3_preview_box = sheet_bbox(argb, c3_cell_w, c3_cell_h);
    if (c3_preview_tex) {
        SDL_DestroyTexture(c3_preview_tex);
    }
    c3_preview_tex = SDL_CreateTextureFromSurface(renderer, argb);
    SDL_SetTextureScaleMode(c3_preview_tex, SDL_SCALEMODE_NEAREST);
    SDL_DestroySurface(argb);
    c3_preview_error.clear();
}

static bool c3_busy(void)
{
    return c3_preview_pending || c3_seq_shown < c3_seq_sent;
}

static bool c3_start_server(const std::string &work)
{
    SDL_RemovePath((work + "preview.done").c_str());
    SDL_RemovePath((work + "request.ini").c_str());
    c3_server = c3_spawn({c3_python(), c3_tools_dir() + "/creator_preview.py", "--serve", work, "3"});
    if (!c3_server) {
        c3_preview_error = "No se encontro Python o las herramientas en " + c3_tools_dir();
        return false;
    }
    return true;
}

/* Sends the recipe to the preview server once it has been still for a moment, and picks up
   each finished render. */
static void c3_update_preview(void)
{
    const std::string work = c3_work_dir();
    const Uint64 now = SDL_GetTicks();
    if (c3_server) {
        int code = 0;
        if (SDL_WaitProcess(c3_server, false, &code)) {
            SDL_DestroyProcess(c3_server);
            c3_server = nullptr;
            c3_preview_error = "El renderizador se cerro (mira la terminal)";
            c3_seq_shown = c3_seq_sent;
        }
    }
    if (c3_seq_shown < c3_seq_sent && now - c3_last_poll >= 30) {
        c3_last_poll = now;
        size_t size = 0;
        char *done = (char *)SDL_LoadFile((work + "preview.done").c_str(), &size);
        if (done) {
            const int seq = std::strncmp(done, "seq=", 4) == 0 ? SDL_atoi(done + 4) : 0;
            const bool failed = std::strstr(done, "error") != nullptr;
            SDL_free(done);
            if (seq > c3_seq_shown) {
                c3_seq_shown = seq;
                if (failed) {
                    c3_preview_error = "La vista previa fallo (mira la terminal)";
                } else {
                    c3_load_preview(work + "preview.png");
                }
            }
        }
    }
    if (!c3_preview_pending || now - c3_changed_at < 120) {
        return;
    }
    c3_preview_pending = false;
    if (!c3_server && !c3_start_server(work)) {
        return;
    }
    const int seq = c3_seq_sent + 1;
    char head[32];
    SDL_snprintf(head, sizeof(head), "seq=%d\n", seq);
    const std::string text = head + c3_recipe_text();
    const std::string tmp = work + "request.tmp";
    if (!SDL_SaveFile(tmp.c_str(), text.data(), text.size()) || !SDL_RenamePath(tmp.c_str(), (work + "request.ini").c_str())) {
        c3_preview_error = "No se pudo escribir la receta";
        return;
    }
    c3_seq_sent = seq;
}

static void c3_stop_server(void)
{
    if (c3_server) {
        SDL_KillProcess(c3_server, false);
        SDL_WaitProcess(c3_server, true, nullptr);
        SDL_DestroyProcess(c3_server);
        c3_server = nullptr;
    }
}

static std::string c3_slug(void)
{
    std::string out;
    for (const char *p = c3_name; *p; ++p) {
        const char ch = (char)SDL_tolower((unsigned char)*p);
        if ((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9')) {
            out += ch;
        } else if (!out.empty() && out.back() != '_') {
            out += '_';
        }
    }
    while (!out.empty() && out.back() == '_') {
        out.pop_back();
    }
    return out.empty() ? "personaje" : out;
}

/* Renders the four packs (NPC and playable, pixel art and HD) in parallel. */
static void c3_export(void)
{
    if (!c3_export_procs.empty()) {
        set_status("Ya se esta exportando un personaje");
        return;
    }
    const std::string sprites = std::string(Asset_Root()) + "assets/sprites";
    const std::string custom = sprites + "/custom";
    const std::string slug = c3_slug();
    const std::string dir = custom + "/" + slug;
    SDL_CreateDirectory(dir.c_str());
    const std::string recipe = dir + "/receta.ini";
    if (!c3_write_recipe(recipe)) {
        set_status("No se pudo escribir la receta en assets/sprites/custom");
        return;
    }
    const std::string make_pack = c3_tools_dir() + "/make_pack.py";
    const std::string py = c3_python();
    const std::vector<std::string> common = {py, make_pack, "man", "--look", "custom", "--recipe", recipe,
                                             "--name", c3_name};
    auto job = [&](std::vector<std::string> extra) {
        std::vector<std::string> args = common;
        args.insert(args.end(), extra.begin(), extra.end());
        if (SDL_Process *p = c3_spawn(args)) {
            c3_export_procs.push_back(p);
        } else {
            ++c3_export_failed;
        }
    };
    c3_export_failed = 0;
    job({"--role", "npc", "--id", slug, "--out", custom});
    job({"--role", "npc", "--id", "hd", "--pixel-scale", "3", "--out", dir});
    job({"--role", "player", "--anims-of", "main", "--id", "jugador", "--out", dir});
    job({"--role", "player", "--anims-of", "main", "--id", "hd", "--pixel-scale", "3", "--out", dir + "/jugador"});
    c3_export_started = SDL_GetTicks();
    c3_export_dir = dir;
    SDL_Log("Exporting %s to %s", c3_name, dir.c_str());
}

static void c3_update_export(void)
{
    if (c3_export_procs.empty()) {
        return;
    }
    for (size_t i = 0; i < c3_export_procs.size();) {
        int code = 0;
        if (SDL_WaitProcess(c3_export_procs[i], false, &code)) {
            SDL_DestroyProcess(c3_export_procs[i]);
            c3_export_procs.erase(c3_export_procs.begin() + (long)i);
            if (code != 0) {
                ++c3_export_failed;
            }
            if (c3_export_procs.empty()) {
                char msg[160];
                if (c3_export_failed) {
                    SDL_snprintf(msg, sizeof(msg), "Exportacion con %d errores (mira la terminal)", c3_export_failed);
                } else {
                    SDL_snprintf(msg, sizeof(msg), "Guardado en %s - ya esta en el juego", c3_export_dir.c_str());
                }
                set_status(msg);
                status_until = SDL_GetTicks() + 8000;
                scan_packs();
            }
            continue;
        }
        ++i;
    }
}

static void c3_text_input(const char *s)
{
    const size_t len = SDL_strlen(c3_name);
    for (; *s && len + 1 < sizeof(c3_name); ++s) {
        if ((unsigned char)*s >= 32 && (unsigned char)*s < 127) {
            const size_t n = SDL_strlen(c3_name);
            if (n + 1 < sizeof(c3_name)) {
                c3_name[n] = *s;
                c3_name[n + 1] = '\0';
            }
        }
    }
}

static void c3_stop_name_edit(void)
{
    if (c3_name_editing) {
        c3_name_editing = false;
        SDL_StopTextInput(window);
    }
}

/* Returns true when the key went to the name field. */
static bool c3_name_key(const SDL_KeyboardEvent &k)
{
    if (!c3_name_editing) {
        return false;
    }
    if (k.scancode == SDL_SCANCODE_BACKSPACE) {
        const size_t n = SDL_strlen(c3_name);
        if (n) {
            c3_name[n - 1] = '\0';
        }
    } else if (k.scancode == SDL_SCANCODE_RETURN || k.scancode == SDL_SCANCODE_ESCAPE ||
               k.scancode == SDL_SCANCODE_TAB) {
        c3_stop_name_edit();
    }
    return true;
}

static void ui_c3_left(void)
{
    header(20, TOP, "PERSONAJE");
    float y = TOP + 30.0f;
    text(20, y, "Nombre (clic para escribir)", 150, 150, 150);
    y += 14.0f;
    const bool hot = hovered(20, y, 290, 28);
    fill(20, y, 290, 28, c3_name_editing ? 0xff3a3440 : (hot ? 0xff2e2a34 : 0xff26222a));
    frame_rect(20, y, 290, 28, c3_name_editing ? 0xffe0404a : 0xff4a4450);
    char shown[64];
    const bool blink = c3_name_editing && (SDL_GetTicks() / 500) % 2 == 0;
    SDL_snprintf(shown, sizeof(shown), "%.*s%s", 34, c3_name, blink ? "_" : "");
    text(28, y + 10, shown);
    if (hot && click_pending) {
        click_pending = false;
        if (!c3_name_editing) {
            c3_name_editing = true;
            SDL_StartTextInput(window);
        }
    }
    y += 36.0f;
    char id[96];
    SDL_snprintf(id, sizeof(id), "custom/%s", c3_slug().c_str());
    text(20, y, id, 120, 120, 130);
    y += 22.0f;

    const bool exporting = !c3_export_procs.empty();
    if (button(20, y, 290, 34, exporting ? "EXPORTANDO..." : "GUARDAR PERSONAJE", exporting)) {
        c3_stop_name_edit();
        c3_export();
    }
    y += 40.0f;
    if (exporting) {
        char line[96];
        const Uint64 secs = (SDL_GetTicks() - c3_export_started) / 1000;
        SDL_snprintf(line, sizeof(line), "%d de 4 paquetes listos  %d:%02d", 4 - (int)c3_export_procs.size(),
                     (int)(secs / 60), (int)(secs % 60));
        text(20, y, line, 255, 200, 80);
        text(20, y + 14, "NPC + jugador, pixel y HD (unos minutos)", 150, 150, 150);
    } else {
        text(20, y, "Crea el NPC y la version jugable", 150, 150, 150);
        text(20, y + 14, "con todas las animaciones.", 150, 150, 150);
    }
    y += 40.0f;
    if (button(20, y, 142, 26, "ALEATORIO")) {
        c3_randomize();
    }
    if (button(168, y, 142, 26, "REINICIAR")) {
        c3_reset();
        c3_changed();
    }
    y += 32.0f;
    if (button(20, y, 290, 26, "CARGAR RECETA DE VIEW")) {
        if (cur_pack >= 0 && (c3_load_recipe(packs[cur_pack].dir + "/receta.ini") ||
                              c3_load_recipe(packs[cur_pack].dir + "/../receta.ini"))) {
            set_status("Receta cargada");
        } else {
            set_status("El personaje elegido en VIEW no tiene receta.ini (solo los creados aqui)");
        }
    }
    y += 32.0f;
    if (button(20, y, 290, 26, "CREADOR CLASICO (POSTAL DUDE)")) {
        classic_creator = true;
    }
    y += 44.0f;

    char title[64];
    SDL_snprintf(title, sizeof(title), "COLOR: %s", COLOR3D[c3_active_color].label);
    text(20, y, title, 220, 28, 28);
    y += 16.0f;
    for (int i = 0; i < 36; ++i) {
        const float x = 20.0f + (i % 9) * 32.0f;
        const float cy = y + (i / 9) * 28.0f;
        fill(x, cy, 28, 24, CREATOR_PALETTE[i]);
        frame_rect(x, cy, 28, 24, CREATOR_PALETTE[i] == c3_color[c3_active_color] ? 0xffffffff : 0xff4a4450);
        if (hovered(x, cy, 28, 24) && click_pending) {
            c3_color[c3_active_color] = CREATOR_PALETTE[i];
            click_pending = false;
            c3_changed();
        }
    }
    y += 4 * 28.0f + 6.0f;
    for (int ch = 0; ch < 3; ++ch) {
        if (color_slider(20, y + ch * 26.0f, 240, ch, &c3_color[c3_active_color], 30 + ch)) {
            c3_changed();
        }
    }
    y += 3 * 26.0f + 14.0f;
    static const char *const help[] = {
        "CREADOR",
        "Clic en < >    siguiente opcion",
        "Clic derecho   opcion anterior",
        "Clic en color  editar ese color",
        "Izq/Der        girar  (R auto)",
        "Rueda          zoom",
    };
    help_lines(20, y, help, SDL_arraysize(help));
}

static void ui_c3_view(void)
{
    fill(VIEW_X, VIEW_Y, VIEW_W, VIEW_H, 0xff1c1a22);
    frame_rect(VIEW_X - 1, VIEW_Y - 1, VIEW_W + 2, VIEW_H + 2, 0xff4a4450);
    clip(VIEW_X, VIEW_Y, VIEW_W, VIEW_H);
    if (c3_preview_tex && c3_cell_w > 0) {
        const float thumbs_h = 96.0f;
        const float area_h = VIEW_H - 24.0f - thumbs_h;
        const int zoom = SDL_max(1, fit_zoom(VIEW_W, area_h, c3_preview_box, 0.8f) + view_zoom_step);
        const float x = SDL_floorf(VIEW_X + VIEW_W * 0.5f - (c3_preview_box.x + c3_preview_box.w * 0.5f) * zoom);
        const float y = SDL_floorf(VIEW_Y + area_h * 0.5f - (c3_preview_box.y + c3_preview_box.h * 0.5f) * zoom);
        draw_feet_shadow(x + c3_cell_w * 0.5f * zoom, y + (c3_preview_box.y + c3_preview_box.h - 2) * (float)zoom,
                         (float)zoom * 3.0f);
        const SDL_FRect src = {(float)(cur_dir * c3_cell_w), 0, (float)c3_cell_w, (float)c3_cell_h};
        const SDL_FRect dst = {x, y, (float)c3_cell_w * zoom, (float)c3_cell_h * zoom};
        SDL_RenderTexture(renderer, c3_preview_tex, &src, &dst);

        /* The eight directions; click one to face it. */
        const float tz = SDL_min(1.0f, (thumbs_h - 12.0f) / c3_preview_box.h);
        const float slot = VIEW_W / SPRITE_DIRS;
        const float ty = VIEW_Y + VIEW_H - 24.0f - thumbs_h + 4.0f;
        fill(VIEW_X, ty - 4.0f, VIEW_W, thumbs_h, 0xff17151b);
        for (int d = 0; d < SPRITE_DIRS; ++d) {
            const float tx = VIEW_X + d * slot + (slot - c3_preview_box.w * tz) * 0.5f;
            const SDL_FRect s = {(float)(d * c3_cell_w + c3_preview_box.x), (float)c3_preview_box.y,
                                 (float)c3_preview_box.w, (float)c3_preview_box.h};
            const SDL_FRect t = {tx, ty, c3_preview_box.w * tz, c3_preview_box.h * tz};
            SDL_SetTextureScaleMode(c3_preview_tex, SDL_SCALEMODE_LINEAR);
            SDL_RenderTexture(renderer, c3_preview_tex, &s, &t);
            SDL_SetTextureScaleMode(c3_preview_tex, SDL_SCALEMODE_NEAREST);
            if (d == cur_dir) {
                frame_rect(VIEW_X + d * slot + 4, ty - 2, slot - 8, thumbs_h - 4, 0xffe0404a);
            }
            if (hovered(VIEW_X + d * slot, ty - 4, slot, thumbs_h) && click_pending) {
                cur_dir = d;
                click_pending = false;
            }
        }
    }
    const bool busy = c3_busy();
    char line[200];
    if (!c3_preview_error.empty()) {
        SDL_snprintf(line, sizeof(line), "%s", c3_preview_error.c_str());
    } else {
        SDL_snprintf(line, sizeof(line), "%s  /  %s  /  vista previa de pie; las animaciones se generan al guardar",
                     busy ? "RENDERIZANDO..." : "LISTO", SPRITE_DIR_NAMES[cur_dir]);
    }
    view_caption(VIEW_X, VIEW_Y + VIEW_H - 24, VIEW_W, line);
    if (busy) {
        fill(VIEW_X + VIEW_W - 150, VIEW_Y + 10, 140, 24, 0xd0141218);
        text(VIEW_X + VIEW_W - 142, VIEW_Y + 18, "RENDERIZANDO", 255, 200, 80);
    }
    unclip();
}

static void ui_c3_panel(void)
{
    const float px = 1180.0f;
    float y = TOP - 4.0f;
    int group = -1;
    for (size_t i = 0; i < OPTS.size(); ++i) {
        if (OPTS[i].group != group) {
            group = OPTS[i].group;
            text(px, y + 8, OPT_GROUPS[group], 220, 28, 28);
            fill(px + (SDL_strlen(OPT_GROUPS[group]) + 1) * 8.0f, y + 11, 390 - SDL_strlen(OPT_GROUPS[group]) * 8.0f, 1,
                 0xff4a4450);
            y += 22.0f;
        }
        const OptDef &d = OPTS[i].def;
        text(px, y + 8, d.label);
        if (d.key) {
            char label[64];
            SDL_snprintf(label, sizeof(label), "< %s >", d.values[(size_t)c3_value[i]].label);
            const float bx = px + 124.0f;
            const bool non_default = c3_value[i] != 0;
            if (button(bx, y, 196, 24, label, non_default)) {
                c3_value[i] = (c3_value[i] + 1) % (int)d.values.size();
                c3_changed();
            }
            if (hovered(bx, y, 196, 24) && rclick_pending) {
                c3_value[i] = (c3_value[i] + (int)d.values.size() - 1) % (int)d.values.size();
                rclick_pending = false;
                c3_changed();
            }
        }
        if (d.color >= 0) {
            const float sx = px + 328.0f;
            fill(sx, y, 62, 24, c3_color[d.color]);
            frame_rect(sx, y, 62, 24, d.color == c3_active_color ? 0xffffffff : 0xff4a4450);
            if (d.color == c3_active_color) {
                frame_rect(sx - 2, y - 2, 66, 28, 0xffe0404a);
            }
            if (hovered(sx, y, 62, 24) && click_pending) {
                c3_active_color = d.color;
                click_pending = false;
            }
        }
        y += 27.0f;
    }
}

static bool classic_creator_on(void)
{
    return classic_creator;
}

static void set_classic_creator(bool on)
{
    classic_creator = on;
}

SDL_AppResult SDL_AppIterate(void *appstate)
{
    (void)appstate;
    sync_textures();
    if (auto_rotate && SDL_GetTicks() - rotate_last > 700) {
        rotate_dir(1);
        rotate_last = SDL_GetTicks();
    }

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 16, 14, 18, 255);
    SDL_RenderClear(renderer);

    fill(0, 0, (float)WIN_W, TOP_BAR_H, 0xff17151b);
    fill(0, TOP_BAR_H, (float)WIN_W, 1, 0xff3a3440);
    const float tabs_x = (float)WIN_W - 20.0f - 3 * 110.0f - 2 * 8.0f;
    if (button(tabs_x, 9, 110, 26, "VIEW", mode == MODE_VIEW)) mode = MODE_VIEW;
    if (button(tabs_x + 118, 9, 110, 26, "EDIT", mode == MODE_EDIT)) mode = MODE_EDIT;
    if (button(tabs_x + 236, 9, 110, 26, "CREATE", mode == MODE_CREATE)) mode = MODE_CREATE;
    text(tabs_x - 100, 18, "Tab: switch", 110, 110, 110);
    c3_update_export();
    if (mode == MODE_CREATE) {
        if (classic_creator) {
            sync_creator_texture();
        } else {
            c3_update_preview();
        }
    }

    char title[160];
    if (mode == MODE_CREATE && !classic_creator) {
        SDL_snprintf(title, sizeof(title), "CREADOR DE PERSONAJES  /  %s", c3_name);
    } else if (mode == MODE_CREATE) {
        SDL_snprintf(title, sizeof(title), "CHARACTER CREATOR%s%s", cur_base >= 0 ? "  /  " : "",
                     cur_base >= 0 ? bases[cur_base].label.c_str() : "");
    } else if (has_pack()) {
        SDL_snprintf(title, sizeof(title), "%s%s  /  %s  /  %s", pack.name.c_str(), unsaved ? " *" : "",
                     action().name.c_str(), SPRITE_DIR_NAMES[cur_dir]);
    } else {
        SDL_snprintf(title, sizeof(title), "No sprite pack loaded");
    }
    /* Headers are 16 px per char; stop before the tabs. */
    const int max_chars = (int)((tabs_x - 120.0f - 20.0f) / 16.0f);
    if ((int)SDL_strlen(title) > max_chars) {
        title[SDL_max(0, max_chars - 2)] = '\0';
        SDL_strlcat(title, "..", sizeof(title));
    }
    header(20, 14, title);

    if (mode == MODE_CREATE && !classic_creator) {
        ui_c3_left();
        ui_c3_view();
        ui_c3_panel();
    } else if (mode == MODE_CREATE) {
        ui_creator_left();
        ui_creator_view();
        ui_creator_panel();
    } else if (mode == MODE_VIEW) {
        ui_pack_list();
        ui_viewer();
        if (has_pack()) {
            ui_view_panel();
        }
    } else if (has_pack()) {
        ui_tools_panel();
        ui_canvas();
        ui_edit_panel();
    } else {
        text(CANVAS_X, CANVAS_Y, "Load a pack in VIEW mode first.", 170, 170, 170);
    }

    fill(0, BOTTOM + 12, (float)WIN_W, 1, 0xff2a2630);
    if (SDL_GetTicks() < status_until) {
        text(20, BOTTOM + 22, status, 255, 200, 80);
    }

    click_pending = false;
    rclick_pending = false;
    SDL_RenderPresent(renderer);
    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void *appstate, SDL_AppResult result)
{
    (void)appstate;
    (void)result;
    free_textures();
    SpritePack_Free(&pack);
    free_creator_texture();
    if (c3_preview_tex) {
        SDL_DestroyTexture(c3_preview_tex);
    }
    c3_stop_server();
    SpritePack_Free(&base_coat);
    SpritePack_Free(&base_nocoat);
}
