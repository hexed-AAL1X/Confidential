#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <cstdio>
#include <cstring>
#include <vector>

static const int CELL_W = 32;
static const int CELL_H = 48;
static const int COLS = 9;
static const int ROWS = 8;
static const int SHEET_W = CELL_W * COLS;
static const int SHEET_H = CELL_H * ROWS;
static const int WIN_W = 1600;
static const int WIN_H = 900;

static const int CANVAS_X = 330;
static const int CANVAS_Y = 70;
static const int ZOOM = 16;

static const char *const DIR_NAMES[ROWS] = {
    "DOWN", "DOWN-R", "RIGHT", "UP-R", "UP", "UP-L", "LEFT", "DOWN-L"
};
static const int MIRROR_ROW[ROWS] = {0, 7, 6, 5, 4, 3, 2, 1};
static const char *const FRAME_NAMES[COLS] = {"IDLE", "W1", "W2", "W3", "W4", "W5", "W6", "W7", "W8"};
static const char *const FRAME_TITLES[COLS] = {
    "IDLE", "WALK 1", "WALK 2", "WALK 3", "WALK 4", "WALK 5", "WALK 6", "WALK 7", "WALK 8"
};

enum Tool {
    TOOL_PENCIL,
    TOOL_ERASER,
    TOOL_FILL,
    TOOL_PICKER
};

static SDL_Window *window = nullptr;
static SDL_Renderer *renderer = nullptr;
static SDL_Texture *sheet_tex = nullptr;
static char *save_path = nullptr;

static Uint32 sheet[SHEET_H][SHEET_W];
static std::vector<std::vector<Uint32>> undo_stack;
static std::vector<Uint32> clipboard;
static bool dirty_tex = true;
static bool unsaved = false;

static int cur_row = 0;
static int cur_col = 0;
static Tool tool = TOOL_PENCIL;
static bool mirror_draw = false;
static bool onion = true;
static bool playing = true;
static int preview_fps = 10;
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
static char status[96] = "";
static Uint64 status_until = 0;

static void set_status(const char *msg)
{
    SDL_strlcpy(status, msg, sizeof(status));
    status_until = SDL_GetTicks() + 2500;
}

static Uint32 &px_at(int row, int col, int x, int y)
{
    return sheet[row * CELL_H + y][col * CELL_W + x];
}

static void push_undo(void)
{
    std::vector<Uint32> snap(&sheet[0][0], &sheet[0][0] + SHEET_W * SHEET_H);
    undo_stack.push_back(std::move(snap));
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
    SDL_memcpy(sheet, undo_stack.back().data(), sizeof(sheet));
    undo_stack.pop_back();
    dirty_tex = true;
    unsaved = true;
}

static bool frame_used(int row, int col)
{
    for (int y = 0; y < CELL_H; ++y) {
        for (int x = 0; x < CELL_W; ++x) {
            if (px_at(row, col, x, y) >> 24) {
                return true;
            }
        }
    }
    return false;
}

static void load_sheet(void)
{
    SDL_memset(sheet, 0, sizeof(sheet));
    SDL_Surface *loaded = SDL_LoadPNG(save_path);
    if (!loaded) {
        set_status("New sheet (no player.png yet)");
        return;
    }
    SDL_Surface *conv = SDL_ConvertSurface(loaded, SDL_PIXELFORMAT_ARGB8888);
    SDL_DestroySurface(loaded);
    if (!conv) {
        set_status("Couldn't read player.png");
        return;
    }
    const int w = SDL_min(conv->w, SHEET_W);
    const int h = SDL_min(conv->h, SHEET_H);
    for (int y = 0; y < h; ++y) {
        const Uint32 *src = (const Uint32 *)((const Uint8 *)conv->pixels + y * conv->pitch);
        SDL_memcpy(sheet[y], src, (size_t)w * sizeof(Uint32));
    }
    SDL_DestroySurface(conv);
    set_status("Loaded player.png");
}

static void save_sheet(void)
{
    SDL_Surface *surf = SDL_CreateSurface(SHEET_W, SHEET_H, SDL_PIXELFORMAT_ARGB8888);
    if (!surf) {
        set_status("Save failed");
        return;
    }
    for (int y = 0; y < SHEET_H; ++y) {
        SDL_memcpy((Uint8 *)surf->pixels + y * surf->pitch, sheet[y], SHEET_W * sizeof(Uint32));
    }
    if (SDL_SavePNG(surf, save_path)) {
        set_status("Saved assets/sprites/player.png");
        unsaved = false;
    } else {
        char msg[96];
        SDL_snprintf(msg, sizeof(msg), "Save failed: %s", SDL_GetError());
        set_status(msg);
    }
    SDL_DestroySurface(surf);
}

static void put_pixel(int x, int y, Uint32 c)
{
    if (x < 0 || x >= CELL_W || y < 0 || y >= CELL_H) {
        return;
    }
    px_at(cur_row, cur_col, x, y) = c;
    if (mirror_draw) {
        px_at(cur_row, cur_col, CELL_W - 1 - x, y) = c;
    }
    dirty_tex = true;
    unsaved = true;
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
    const Uint32 target = px_at(cur_row, cur_col, x, y);
    if (target == c) {
        return;
    }
    std::vector<SDL_Point> stack = {{x, y}};
    while (!stack.empty()) {
        const SDL_Point p = stack.back();
        stack.pop_back();
        if (p.x < 0 || p.x >= CELL_W || p.y < 0 || p.y >= CELL_H) {
            continue;
        }
        Uint32 &cell = px_at(cur_row, cur_col, p.x, p.y);
        if (cell != target) {
            continue;
        }
        cell = c;
        stack.push_back({p.x + 1, p.y});
        stack.push_back({p.x - 1, p.y});
        stack.push_back({p.x, p.y + 1});
        stack.push_back({p.x, p.y - 1});
    }
    dirty_tex = true;
    unsaved = true;
}

static void flip_frame(void)
{
    push_undo();
    for (int y = 0; y < CELL_H; ++y) {
        for (int x = 0; x < CELL_W / 2; ++x) {
            Uint32 &a = px_at(cur_row, cur_col, x, y);
            Uint32 &b = px_at(cur_row, cur_col, CELL_W - 1 - x, y);
            const Uint32 t = a;
            a = b;
            b = t;
        }
    }
    dirty_tex = true;
    unsaved = true;
}

static void clear_frame(void)
{
    push_undo();
    for (int y = 0; y < CELL_H; ++y) {
        for (int x = 0; x < CELL_W; ++x) {
            px_at(cur_row, cur_col, x, y) = 0;
        }
    }
    dirty_tex = true;
    unsaved = true;
}

static void copy_frame(void)
{
    clipboard.assign(CELL_W * CELL_H, 0);
    for (int y = 0; y < CELL_H; ++y) {
        for (int x = 0; x < CELL_W; ++x) {
            clipboard[y * CELL_W + x] = px_at(cur_row, cur_col, x, y);
        }
    }
    set_status("Frame copied");
}

static void paste_frame(void)
{
    if (clipboard.empty()) {
        set_status("Clipboard empty");
        return;
    }
    push_undo();
    for (int y = 0; y < CELL_H; ++y) {
        for (int x = 0; x < CELL_W; ++x) {
            px_at(cur_row, cur_col, x, y) = clipboard[y * CELL_W + x];
        }
    }
    dirty_tex = true;
    unsaved = true;
    set_status("Frame pasted");
}

static void mirror_row_from_opposite(void)
{
    const int src = MIRROR_ROW[cur_row];
    if (src == cur_row) {
        set_status("DOWN and UP have no opposite to mirror");
        return;
    }
    push_undo();
    for (int col = 0; col < COLS; ++col) {
        for (int y = 0; y < CELL_H; ++y) {
            for (int x = 0; x < CELL_W; ++x) {
                px_at(cur_row, col, x, y) = px_at(src, col, CELL_W - 1 - x, y);
            }
        }
    }
    dirty_tex = true;
    unsaved = true;
    char msg[96];
    SDL_snprintf(msg, sizeof(msg), "Row mirrored from %s", DIR_NAMES[src]);
    set_status(msg);
}

static void auto_outline(void)
{
    push_undo();
    std::vector<Uint32> src(CELL_W * CELL_H);
    for (int y = 0; y < CELL_H; ++y) {
        for (int x = 0; x < CELL_W; ++x) {
            src[y * CELL_W + x] = px_at(cur_row, cur_col, x, y);
        }
    }
    for (int y = 0; y < CELL_H; ++y) {
        for (int x = 0; x < CELL_W; ++x) {
            if (src[y * CELL_W + x] >> 24) {
                continue;
            }
            const bool near =
                (x > 0 && (src[y * CELL_W + x - 1] >> 24)) ||
                (x < CELL_W - 1 && (src[y * CELL_W + x + 1] >> 24)) ||
                (y > 0 && (src[(y - 1) * CELL_W + x] >> 24)) ||
                (y < CELL_H - 1 && (src[(y + 1) * CELL_W + x] >> 24));
            if (near) {
                px_at(cur_row, cur_col, x, y) = 0xff0a0608;
            }
        }
    }
    dirty_tex = true;
    unsaved = true;
    set_status("Outline added");
}

static bool canvas_pixel(float mx, float my, int *x, int *y)
{
    const int cx = (int)SDL_floorf((mx - CANVAS_X) / ZOOM);
    const int cy = (int)SDL_floorf((my - CANVAS_Y) / ZOOM);
    *x = cx;
    *y = cy;
    return cx >= 0 && cx < CELL_W && cy >= 0 && cy < CELL_H;
}

static void apply_tool_at(int x, int y, bool erase, bool start)
{
    if (tool == TOOL_PICKER) {
        const Uint32 c = px_at(cur_row, cur_col, x, y);
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

SDL_AppResult SDL_AppInit(void **appstate, int argc, char *argv[])
{
    (void)appstate;
    (void)argc;
    (void)argv;
    SDL_SetAppMetadata("CONFIDENTIAL Sprite Editor", "1.0", "com.confidential.editor");
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

    const char *base = SDL_GetBasePath();
    char *dir = nullptr;
    SDL_asprintf(&dir, "%sassets/sprites", base ? base : "./");
    SDL_CreateDirectory(dir);
    SDL_asprintf(&save_path, "%s/player.png", dir);
    SDL_free(dir);

    sheet_tex = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, SHEET_W, SHEET_H);
    SDL_SetTextureScaleMode(sheet_tex, SDL_SCALEMODE_NEAREST);
    SDL_SetTextureBlendMode(sheet_tex, SDL_BLENDMODE_BLEND);

    load_sheet();
    return SDL_APP_CONTINUE;
}

static void handle_key(const SDL_KeyboardEvent &k)
{
    const bool ctrl = (k.mod & SDL_KMOD_CTRL) != 0;
    switch (k.scancode) {
        case SDL_SCANCODE_S:
            if (ctrl) {
                save_sheet();
            }
            break;
        case SDL_SCANCODE_Z:
            if (ctrl) {
                undo();
            }
            break;
        case SDL_SCANCODE_C:
            if (ctrl) {
                copy_frame();
            }
            break;
        case SDL_SCANCODE_V:
            if (ctrl) {
                paste_frame();
            }
            break;
        case SDL_SCANCODE_B: tool = TOOL_PENCIL; break;
        case SDL_SCANCODE_E: tool = TOOL_ERASER; break;
        case SDL_SCANCODE_G: tool = TOOL_FILL; break;
        case SDL_SCANCODE_I: tool = TOOL_PICKER; break;
        case SDL_SCANCODE_M: mirror_draw = !mirror_draw; break;
        case SDL_SCANCODE_O: onion = !onion; break;
        case SDL_SCANCODE_H: flip_frame(); break;
        case SDL_SCANCODE_K: auto_outline(); break;
        case SDL_SCANCODE_DELETE: clear_frame(); break;
        case SDL_SCANCODE_SPACE: playing = !playing; break;
        case SDL_SCANCODE_LEFT: cur_col = (cur_col + COLS - 1) % COLS; break;
        case SDL_SCANCODE_RIGHT: cur_col = (cur_col + 1) % COLS; break;
        case SDL_SCANCODE_UP: cur_row = (cur_row + ROWS - 1) % ROWS; break;
        case SDL_SCANCODE_DOWN: cur_row = (cur_row + 1) % ROWS; break;
        case SDL_SCANCODE_LEFTBRACKET: preview_fps = SDL_max(2, preview_fps - 1); break;
        case SDL_SCANCODE_RIGHTBRACKET: preview_fps = SDL_min(30, preview_fps + 1); break;
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
        event->type == SDL_EVENT_MOUSE_BUTTON_UP) {
        SDL_ConvertEventToRenderCoordinates(renderer, event);
    }

    switch (event->type) {
        case SDL_EVENT_KEY_DOWN:
            handle_key(event->key);
            break;

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
            }
            if (on_canvas && (event->button.button == SDL_BUTTON_LEFT || event->button.button == SDL_BUTTON_RIGHT)) {
                const bool alt = (SDL_GetModState() & SDL_KMOD_ALT) != 0;
                if (alt) {
                    const Uint32 c = px_at(cur_row, cur_col, x, y);
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
            }
            last_px = -1;
            last_py = -1;
            slider_drag = -1;
            break;

        case SDL_EVENT_MOUSE_MOTION: {
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

static void text(float x, float y, const char *s, Uint8 r = 230, Uint8 g = 230, Uint8 b = 230, float scale = 1.0f)
{
    SDL_SetRenderScale(renderer, scale, scale);
    SDL_SetRenderDrawColor(renderer, r, g, b, 255);
    SDL_RenderDebugText(renderer, x / scale, y / scale, s);
    SDL_SetRenderScale(renderer, 1.0f, 1.0f);
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
    text(x + (w - (float)SDL_strlen(label) * 8.0f) * 0.5f, y + (h - 8.0f) * 0.5f, label);
    if (hot && click_pending) {
        click_pending = false;
        return true;
    }
    return false;
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

static void draw_cell(int row, int col, float x, float y, float zoom, Uint8 alpha = 255)
{
    const SDL_FRect src = {(float)(col * CELL_W), (float)(row * CELL_H), (float)CELL_W, (float)CELL_H};
    const SDL_FRect dst = {x, y, CELL_W * zoom, CELL_H * zoom};
    SDL_SetTextureAlphaMod(sheet_tex, alpha);
    SDL_RenderTexture(renderer, sheet_tex, &src, &dst);
    SDL_SetTextureAlphaMod(sheet_tex, 255);
}

static void ui_left_panel(void)
{
    text(20, 20, "TOOLS", 220, 28, 28, 2.0f);
    if (button(20, 50, 130, 30, "PENCIL (B)", tool == TOOL_PENCIL)) tool = TOOL_PENCIL;
    if (button(160, 50, 130, 30, "ERASER (E)", tool == TOOL_ERASER)) tool = TOOL_ERASER;
    if (button(20, 86, 130, 30, "FILL (G)", tool == TOOL_FILL)) tool = TOOL_FILL;
    if (button(160, 86, 130, 30, "PICKER (I)", tool == TOOL_PICKER)) tool = TOOL_PICKER;
    if (button(20, 122, 130, 30, "MIRROR (M)", mirror_draw)) mirror_draw = !mirror_draw;
    if (button(160, 122, 130, 30, "ONION (O)", onion)) onion = !onion;
    if (button(20, 158, 130, 30, "FLIP (H)")) flip_frame();
    if (button(160, 158, 130, 30, "OUTLINE (K)")) auto_outline();
    if (button(20, 194, 130, 30, "COPY ^C")) copy_frame();
    if (button(160, 194, 130, 30, "PASTE ^V")) paste_frame();
    if (button(20, 230, 130, 30, "UNDO ^Z")) undo();
    if (button(160, 230, 130, 30, "CLEAR (DEL)")) clear_frame();

    text(20, 284, "PALETTE", 220, 28, 28, 2.0f);
    text(20, 304, "L-click: use   R-click: store", 150, 150, 150);
    for (int i = 0; i < 32; ++i) {
        const float x = 20.0f + (i % 8) * 34.0f;
        const float y = 322.0f + (i / 8) * 34.0f;
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

    text(20, 470, "COLOR", 220, 28, 28, 2.0f);
    fill(20, 494, 60, 60, color);
    frame_rect(20, 494, 60, 60, 0xffffffff);
    char hex[16];
    SDL_snprintf(hex, sizeof(hex), "#%06X", color & 0xffffff);
    text(92, 520, hex);

    const char *names[3] = {"R", "G", "B"};
    for (int ch = 0; ch < 3; ++ch) {
        const int shift = 16 - ch * 8;
        const float y = 570.0f + ch * 30.0f;
        const float x = 40.0f;
        const float w = 256.0f;
        text(20, y + 6, names[ch]);
        fill(x, y, w, 20, 0xff1a181c);
        const int v = (int)((color >> shift) & 0xff);
        fill(x, y, (float)v, 20, ch == 0 ? 0xffc03030 : ch == 1 ? 0xff30a040 : 0xff3060c0);
        frame_rect(x, y, w, 20, 0xff4a4450);
        if (hovered(x, y, w, 20) && click_pending) {
            slider_drag = ch;
            click_pending = false;
        }
        if (slider_drag == ch && left_down) {
            const int nv = SDL_clamp((int)(mouse_x - x), 0, 255);
            color = (color & ~(0xffu << shift)) | ((Uint32)nv << shift) | 0xff000000u;
        }
    }

    if (button(20, 680, 270, 40, unsaved ? "SAVE ^S  *" : "SAVE ^S", unsaved)) save_sheet();
    if (button(20, 728, 270, 30, "RELOAD FROM DISK")) {
        push_undo();
        load_sheet();
        dirty_tex = true;
    }
}

static void ui_canvas(void)
{
    const float w = CELL_W * ZOOM;
    const float h = CELL_H * ZOOM;
    char title[96];
    SDL_snprintf(title, sizeof(title), "%s  /  %s", DIR_NAMES[cur_row], FRAME_TITLES[cur_col]);
    text(CANVAS_X, 30, title, 220, 28, 28, 2.0f);

    checker(CANVAS_X, CANVAS_Y, w, h, ZOOM);
    if (onion) {
        const int prev = (cur_col == 0) ? -1 : (cur_col == 1 ? 0 : cur_col - 1);
        if (prev >= 0) {
            draw_cell(cur_row, prev, CANVAS_X, CANVAS_Y, ZOOM, 70);
        }
    }
    draw_cell(cur_row, cur_col, CANVAS_X, CANVAS_Y, ZOOM);

    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 22);
    for (int x = 0; x <= CELL_W; ++x) {
        SDL_RenderLine(renderer, CANVAS_X + x * ZOOM, CANVAS_Y, CANVAS_X + x * ZOOM, CANVAS_Y + h);
    }
    for (int y = 0; y <= CELL_H; ++y) {
        SDL_RenderLine(renderer, CANVAS_X, CANVAS_Y + y * ZOOM, CANVAS_X + w, CANVAS_Y + y * ZOOM);
    }
    SDL_SetRenderDrawColor(renderer, 220, 28, 28, 120);
    SDL_RenderLine(renderer, CANVAS_X + w * 0.5f, CANVAS_Y, CANVAS_X + w * 0.5f, CANVAS_Y + h);
    SDL_RenderLine(renderer, CANVAS_X, CANVAS_Y + (CELL_H - 1) * ZOOM, CANVAS_X + w, CANVAS_Y + (CELL_H - 1) * ZOOM);
    frame_rect(CANVAS_X - 1, CANVAS_Y - 1, w + 2, h + 2, 0xff6a6470);

    int x;
    int y;
    if (canvas_pixel(mouse_x, mouse_y, &x, &y)) {
        frame_rect(CANVAS_X + x * ZOOM, CANVAS_Y + y * ZOOM, ZOOM, ZOOM, 0xffffffff);
        char pos[32];
        SDL_snprintf(pos, sizeof(pos), "x %d  y %d", x, y);
        text(CANVAS_X, CANVAS_Y + h + 10, pos, 150, 150, 150);
    }
    text(CANVAS_X + 140, CANVAS_Y + h + 10, "red lines: center / feet row", 120, 120, 120);
}

static void ui_right_panel(void)
{
    const float px = 880.0f;
    text(px, 20, "DIRECTION", 220, 28, 28, 2.0f);
    static const int layout[3][3] = {{5, 4, 3}, {6, -1, 2}, {7, 0, 1}};
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) {
            const int d = layout[r][c];
            const float bx = px + c * 92.0f;
            const float by = 50.0f + r * 36.0f;
            if (d < 0) {
                continue;
            }
            char label[24];
            SDL_snprintf(label, sizeof(label), "%s%s", DIR_NAMES[d], frame_used(d, 0) ? "" : " .");
            if (button(bx, by, 86, 30, label, d == cur_row)) {
                cur_row = d;
            }
        }
    }
    if (button(px + 290, 50, 300, 30, "MIRROR ROW FROM OPPOSITE")) mirror_row_from_opposite();
    text(px + 290, 92, "Empty directions use the mirrored", 140, 140, 140);
    text(px + 290, 104, "opposite in game (LEFT <-> RIGHT).", 140, 140, 140);
    text(px + 290, 122, "DOWN is used if nothing else exists.", 140, 140, 140);

    text(px, 172, "FRAMES", 220, 28, 28, 2.0f);
    for (int col = 0; col < COLS; ++col) {
        const float fx = px + col * 76.0f;
        const float fy = 200.0f;
        checker(fx, fy, 64, 96, 8);
        draw_cell(cur_row, col, fx, fy, 2.0f);
        frame_rect(fx - 2, fy - 2, 68, 100, col == cur_col ? 0xffe0404a : 0xff4a4450);
        text(fx + 4, fy + 104, FRAME_NAMES[col],
             col == cur_col ? 255 : 170, col == cur_col ? 80 : 170, col == cur_col ? 80 : 170);
        if (hovered(fx, fy, 64, 96) && click_pending) {
            cur_col = col;
            click_pending = false;
        }
    }

    text(px, 340, "PREVIEW", 220, 28, 28, 2.0f);
    std::vector<int> walk;
    for (int col = 1; col < COLS; ++col) {
        if (frame_used(cur_row, col)) {
            walk.push_back(col);
        }
    }
    int show = 0;
    if (playing && !walk.empty()) {
        show = walk[(SDL_GetTicks() * (Uint64)preview_fps / 1000) % walk.size()];
    }
    fill(px, 370, 6 * CELL_W, 6 * CELL_H, 0xff1a2828);
    draw_cell(cur_row, show, px, 370, 6.0f);
    frame_rect(px - 1, 369, 6 * CELL_W + 2, 6 * CELL_H + 2, 0xff4a4450);

    char info[64];
    SDL_snprintf(info, sizeof(info), "%s  %d fps  ([ ] to change)", playing ? "PLAYING" : "PAUSED", preview_fps);
    text(px, 370 + 6 * CELL_H + 10, info, 170, 170, 170);
    if (button(px, 370 + 6 * CELL_H + 26, 192, 30, playing ? "PAUSE (SPACE)" : "PLAY (SPACE)", playing)) {
        playing = !playing;
    }

    const float hx = px + 230.0f;
    float hy = 370.0f;
    const char *help[] = {
        "HOW TO USE",
        "",
        "L-click    paint",
        "R-click    erase",
        "Alt+click  pick color",
        "Arrows     frame / direction",
        "Ctrl+S     save",
        "",
        "IDLE = standing still.",
        "W1..W8 = walk cycle, plays in",
        "order, skipping empty frames.",
        "",
        "Feet go on the bottom red line,",
        "centered on the vertical one.",
        "",
        "Saved to assets/sprites/player.png",
        "Enter the game again to see it.",
    };
    for (const char *line : help) {
        text(hx, hy, line, line == help[0] ? 220 : 170, line == help[0] ? 28 : 170, line == help[0] ? 28 : 170);
        hy += 16.0f;
    }
}

SDL_AppResult SDL_AppIterate(void *appstate)
{
    (void)appstate;
    if (dirty_tex) {
        SDL_UpdateTexture(sheet_tex, nullptr, sheet, SHEET_W * sizeof(Uint32));
        dirty_tex = false;
    }

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 16, 14, 18, 255);
    SDL_RenderClear(renderer);

    ui_left_panel();
    ui_canvas();
    ui_right_panel();

    if (SDL_GetTicks() < status_until) {
        text(20, WIN_H - 24, status, 255, 200, 80);
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
    if (sheet_tex) {
        SDL_DestroyTexture(sheet_tex);
    }
    SDL_free(save_path);
}
