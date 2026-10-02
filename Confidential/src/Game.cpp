#include "Game.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

enum Facing {
    FACE_DOWN,
    FACE_UP,
    FACE_LEFT,
    FACE_RIGHT,
    FACE_DOWN_LEFT,
    FACE_DOWN_RIGHT,
    FACE_UP_LEFT,
    FACE_UP_RIGHT
};

struct Player {
    float x;
    float y;
    Facing facing;
    bool moving;
    float walk_t;
    float dash_t;
    float dash_cd;
    float dash_wx;
    float dash_wy;
    float trail_t;
};

struct Ghost {
    float x;
    float y;
    Facing facing;
    float life;
};

struct Pillar {
    int i;
    int j;
};

static const int ARENA_N = 14;
static const float PLAYER_RADIUS = 0.3f;
static const float WALK_SPEED = 3.2f;
static const float DASH_SPEED = 13.0f;
static const float DASH_TIME = 0.14f;
static const float DASH_COOLDOWN = 0.45f;
static const float GHOST_LIFE = 0.25f;
static const Pillar PILLARS[] = {{3, 3}, {3, 10}, {10, 3}, {10, 10}};
static const int PILLAR_COUNT = (int)(sizeof(PILLARS) / sizeof(PILLARS[0]));

static Player player;
static std::vector<Ghost> ghosts;
static const char *difficulty = "";

static SDL_Renderer *rend = nullptr;
static float P = 6.0f;
static float TW = 168.0f;
static float TH = 84.0f;
static float origin_x = 0.0f;
static float origin_y = 0.0f;

static SDL_FColor fcolor(Uint8 r, Uint8 g, Uint8 b, Uint8 a = 255)
{
    return SDL_FColor{r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f};
}

static SDL_FPoint project(float x, float y)
{
    return SDL_FPoint{
        origin_x + (x - y) * TW * 0.5f,
        origin_y + (x + y) * TH * 0.5f
    };
}

static void fill_quad(SDL_FPoint a, SDL_FPoint b, SDL_FPoint c, SDL_FPoint d, SDL_FColor col)
{
    const SDL_Vertex v[4] = {
        {a, col, {0, 0}},
        {b, col, {0, 0}},
        {c, col, {0, 0}},
        {d, col, {0, 0}}
    };
    static const int idx[6] = {0, 1, 2, 0, 2, 3};
    SDL_RenderGeometry(rend, nullptr, v, 4, idx, 6);
}

static SDL_FPoint lift(SDL_FPoint p, float h)
{
    return SDL_FPoint{p.x, p.y - h};
}

/* Screen-space input direction -> normalized world direction (true isometric feel). */
static bool screen_to_world_dir(float sx, float sy, float *wx, float *wy)
{
    const float x = sx + 2.0f * sy;
    const float y = 2.0f * sy - sx;
    const float len = SDL_sqrtf(x * x + y * y);
    if (len < 0.0001f) {
        return false;
    }
    *wx = x / len;
    *wy = y / len;
    return true;
}

static void facing_to_screen(Facing f, float *sx, float *sy)
{
    switch (f) {
        case FACE_DOWN:       *sx = 0.0f;  *sy = 1.0f;  break;
        case FACE_UP:         *sx = 0.0f;  *sy = -1.0f; break;
        case FACE_LEFT:       *sx = -1.0f; *sy = 0.0f;  break;
        case FACE_RIGHT:      *sx = 1.0f;  *sy = 0.0f;  break;
        case FACE_DOWN_LEFT:  *sx = -1.0f; *sy = 1.0f;  break;
        case FACE_DOWN_RIGHT: *sx = 1.0f;  *sy = 1.0f;  break;
        case FACE_UP_LEFT:    *sx = -1.0f; *sy = -1.0f; break;
        case FACE_UP_RIGHT:   *sx = 1.0f;  *sy = -1.0f; break;
    }
}

static Facing screen_to_facing(float sx, float sy)
{
    if (sx != 0.0f && sy != 0.0f) {
        if (sy > 0.0f) {
            return (sx < 0.0f) ? FACE_DOWN_LEFT : FACE_DOWN_RIGHT;
        }
        return (sx < 0.0f) ? FACE_UP_LEFT : FACE_UP_RIGHT;
    }
    if (sx != 0.0f) {
        return (sx < 0.0f) ? FACE_LEFT : FACE_RIGHT;
    }
    return (sy < 0.0f) ? FACE_UP : FACE_DOWN;
}

void Game_Start(const char *difficulty_label)
{
    difficulty = difficulty_label ? difficulty_label : "";
    player = Player{};
    player.x = ARENA_N * 0.5f;
    player.y = ARENA_N * 0.5f - 2.0f;
    player.facing = FACE_DOWN;
    ghosts.clear();
}

bool Game_Event(const SDL_Event *event)
{
    if (event->type != SDL_EVENT_KEY_DOWN || event->key.repeat) {
        return false;
    }
    if (event->key.scancode == SDL_SCANCODE_ESCAPE) {
        return true;
    }
    if ((event->key.scancode == SDL_SCANCODE_SPACE || event->key.scancode == SDL_SCANCODE_LSHIFT) &&
        player.dash_cd <= 0.0f) {
        const bool *keys = SDL_GetKeyboardState(nullptr);
        float sx = (float)((keys[SDL_SCANCODE_D] || keys[SDL_SCANCODE_RIGHT]) - (keys[SDL_SCANCODE_A] || keys[SDL_SCANCODE_LEFT]));
        float sy = (float)((keys[SDL_SCANCODE_S] || keys[SDL_SCANCODE_DOWN]) - (keys[SDL_SCANCODE_W] || keys[SDL_SCANCODE_UP]));
        if (sx == 0.0f && sy == 0.0f) {
            facing_to_screen(player.facing, &sx, &sy);
        }
        screen_to_world_dir(sx, sy, &player.dash_wx, &player.dash_wy);
        player.dash_t = DASH_TIME;
        player.dash_cd = DASH_COOLDOWN;
        player.trail_t = 0.0f;
    }
    return false;
}

static void resolve_collisions(void)
{
    const float r = PLAYER_RADIUS;
    player.x = SDL_clamp(player.x, r, ARENA_N - r);
    player.y = SDL_clamp(player.y, r, ARENA_N - r);

    for (int k = 0; k < PILLAR_COUNT; ++k) {
        const float x0 = (float)PILLARS[k].i;
        const float y0 = (float)PILLARS[k].j;
        const float cx = SDL_clamp(player.x, x0, x0 + 1.0f);
        const float cy = SDL_clamp(player.y, y0, y0 + 1.0f);
        const float dx = player.x - cx;
        const float dy = player.y - cy;
        const float d2 = dx * dx + dy * dy;
        if (d2 >= r * r) {
            continue;
        }
        if (d2 > 0.000001f) {
            const float d = SDL_sqrtf(d2);
            player.x = cx + dx / d * r;
            player.y = cy + dy / d * r;
        } else {
            player.y = y0 - r;
        }
    }
}

void Game_Update(float dt)
{
    dt = SDL_min(dt, 0.05f);

    const bool *keys = SDL_GetKeyboardState(nullptr);
    const float sx = (float)((keys[SDL_SCANCODE_D] || keys[SDL_SCANCODE_RIGHT]) - (keys[SDL_SCANCODE_A] || keys[SDL_SCANCODE_LEFT]));
    const float sy = (float)((keys[SDL_SCANCODE_S] || keys[SDL_SCANCODE_DOWN]) - (keys[SDL_SCANCODE_W] || keys[SDL_SCANCODE_UP]));

    float wx = 0.0f;
    float wy = 0.0f;
    player.moving = screen_to_world_dir(sx, sy, &wx, &wy);
    if (player.moving) {
        player.facing = screen_to_facing(sx, sy);
    }

    if (player.dash_t > 0.0f) {
        player.x += player.dash_wx * DASH_SPEED * dt;
        player.y += player.dash_wy * DASH_SPEED * dt;
        player.dash_t -= dt;
        player.trail_t -= dt;
        if (player.trail_t <= 0.0f) {
            ghosts.push_back(Ghost{player.x, player.y, player.facing, GHOST_LIFE});
            player.trail_t = 0.025f;
        }
    } else if (player.moving) {
        player.x += wx * WALK_SPEED * dt;
        player.y += wy * WALK_SPEED * dt;
    }

    player.walk_t = player.moving ? player.walk_t + dt : 0.0f;
    player.dash_cd = SDL_max(0.0f, player.dash_cd - dt);
    resolve_collisions();

    for (Ghost &g : ghosts) {
        g.life -= dt;
    }
    ghosts.erase(
        std::remove_if(ghosts.begin(), ghosts.end(), [](const Ghost &g) { return g.life <= 0.0f; }),
        ghosts.end());
}

static void draw_floor(void)
{
    const float cliff = TH * 1.5f;
    const SDL_FPoint a = project(0, (float)ARENA_N);
    const SDL_FPoint b = project((float)ARENA_N, (float)ARENA_N);
    const SDL_FPoint c = project((float)ARENA_N, 0);
    fill_quad(a, b, lift(b, -cliff), lift(a, -cliff), fcolor(30, 24, 34));
    fill_quad(b, c, lift(c, -cliff), lift(b, -cliff), fcolor(22, 18, 26));

    const float inset = 0.06f;
    for (int i = 0; i < ARENA_N; ++i) {
        for (int j = 0; j < ARENA_N; ++j) {
            const int noise = (int)(((unsigned)(i * 73856093) ^ (unsigned)(j * 19349663)) % 3) * 5;
            const bool dark = ((i + j) & 1) != 0;
            const Uint8 base = (Uint8)((dark ? 52 : 62) + noise);
            fill_quad(project(i, j), project(i + 1, j), project(i + 1, j + 1), project(i, j + 1),
                      fcolor(34, 27, 40));
            fill_quad(project(i + inset, j + inset), project(i + 1 - inset, j + inset),
                      project(i + 1 - inset, j + 1 - inset), project(i + inset, j + 1 - inset),
                      fcolor(base, (Uint8)(base - 10), (Uint8)(base + 6)));
        }
    }
}

static void draw_pillar(const Pillar &p)
{
    const float h = TH * 3.2f;
    const SDL_FPoint n = project(p.i, p.j);
    const SDL_FPoint e = project(p.i + 1, p.j);
    const SDL_FPoint s = project(p.i + 1, p.j + 1);
    const SDL_FPoint w = project(p.i, p.j + 1);

    fill_quad(w, s, lift(s, h), lift(w, h), fcolor(62, 52, 68));
    fill_quad(s, e, lift(e, h), lift(s, h), fcolor(44, 36, 50));

    const float band = h * 0.82f;
    const float band_h = P * 2.0f;
    fill_quad(lift(w, band), lift(s, band), lift(s, band + band_h), lift(w, band + band_h), fcolor(170, 22, 28));
    fill_quad(lift(s, band), lift(e, band), lift(e, band + band_h), lift(s, band + band_h), fcolor(120, 14, 20));

    fill_quad(lift(n, h), lift(e, h), lift(s, h), lift(w, h), fcolor(96, 84, 102));

    const float t = SDL_max(1.0f, SDL_floorf(P * 0.5f));
    const SDL_FPoint edges[][2] = {
        {lift(n, h), lift(e, h)}, {lift(e, h), lift(s, h)}, {lift(s, h), lift(w, h)}, {lift(w, h), lift(n, h)},
        {w, lift(w, h)}, {s, lift(s, h)}, {e, lift(e, h)}, {w, s}, {s, e}
    };
    SDL_SetRenderDrawColor(rend, 18, 12, 20, 255);
    for (const auto &edge : edges) {
        for (float o = 0.0f; o < t; o += 1.0f) {
            SDL_RenderLine(rend, edge[0].x + o, edge[0].y, edge[1].x + o, edge[1].y);
            SDL_RenderLine(rend, edge[0].x, edge[0].y + o, edge[1].x, edge[1].y + o);
        }
    }
}

static const int CV_W = 40;
static const int CV_H = 58;
static const float CV_CX = 20.0f;
static const float CV_FEET = 54.0f;
static const float VIEW_TILT = 0.25f;

struct Tone {
    Uint32 light;
    Uint32 base;
    Uint32 dark;
};

struct Cell {
    Uint32 c;
    int part;
};

struct Canvas {
    Cell px[CV_H][CV_W];
};

struct V2 {
    float x;
    float y;
};

static const Tone T_HAIR = {0xff3c2c34, 0xff1c1418, 0xff0c080a};
static const Tone T_SKIN = {0xffffd4a8, 0xfff0b888, 0xffc08058};
static const Tone T_JACKET = {0xffe0404a, 0xffb81c28, 0xff7a0c16};
static const Tone T_PANTS = {0xff2e9a96, 0xff1f6b6b, 0xff123f42};
static const Tone T_SHOE = {0xffffffff, 0xffe8e8e8, 0xffa8a8a8};
static const Uint32 C_SOLE = 0xff404040;
static const Uint32 C_EYE = 0xff100c10;
static const Uint32 C_MOUTH = 0xff8a3a32;
static const Uint32 C_OUTLINE = 0xff0a0608;

static Uint32 scale_color(Uint32 c, float f)
{
    const int r = SDL_min(255, (int)(((c >> 16) & 0xff) * f));
    const int g = SDL_min(255, (int)(((c >> 8) & 0xff) * f));
    const int b = SDL_min(255, (int)((c & 0xff) * f));
    return 0xff000000u | ((Uint32)r << 16) | ((Uint32)g << 8) | (Uint32)b;
}

static Tone far_tone(const Tone &t)
{
    return Tone{scale_color(t.light, 0.72f), scale_color(t.base, 0.72f), scale_color(t.dark, 0.72f)};
}

static Uint32 shade(const Tone &t, float nx, float ny)
{
    const float lit = -0.6f * nx - 0.8f * ny;
    if (lit > 0.45f) {
        return t.light;
    }
    if (lit < -0.3f) {
        return t.dark;
    }
    return t.base;
}

static void plot(Canvas &cv, int col, int row, Uint32 c, int part)
{
    if (row >= 0 && row < CV_H && col >= 0 && col < CV_W) {
        cv.px[row][col] = Cell{c, part};
    }
}

static void capsule(Canvas &cv, V2 a, V2 b, float r, const Tone &t, int part)
{
    const int x0 = (int)SDL_floorf(SDL_min(a.x, b.x) - r - 1);
    const int x1 = (int)SDL_ceilf(SDL_max(a.x, b.x) + r + 1);
    const int y0 = (int)SDL_floorf(SDL_min(a.y, b.y) - r - 1);
    const int y1 = (int)SDL_ceilf(SDL_max(a.y, b.y) + r + 1);
    const float dx = b.x - a.x;
    const float dy = b.y - a.y;
    const float len2 = SDL_max(0.0001f, dx * dx + dy * dy);
    for (int row = y0; row <= y1; ++row) {
        for (int col = x0; col <= x1; ++col) {
            const float px = col + 0.5f;
            const float py = row + 0.5f;
            const float u = SDL_clamp(((px - a.x) * dx + (py - a.y) * dy) / len2, 0.0f, 1.0f);
            const float qx = px - (a.x + u * dx);
            const float qy = py - (a.y + u * dy);
            const float d = SDL_sqrtf(qx * qx + qy * qy);
            if (d <= r) {
                plot(cv, col, row, shade(t, qx / r, qy / r), part);
            }
        }
    }
}

struct Body {
    float fx;
    float fz;
    float rx;
    float rz;
    float lift;
};

static V2 proj(const Body &b, float lat, float up, float fwd, float *depth = nullptr)
{
    const float x = lat * b.rx + fwd * b.fx;
    const float z = lat * b.rz + fwd * b.fz;
    if (depth) {
        *depth = z;
    }
    return V2{CV_CX + x, CV_FEET - (up + b.lift) + z * VIEW_TILT};
}

struct LegPose {
    float thigh;
    float shin;
};

static LegPose leg_pose(float phase, bool moving)
{
    if (!moving) {
        return LegPose{0.0f, 0.05f};
    }
    const float thigh = 0.6f * SDL_sinf(phase);
    const float bend = 0.15f + 1.1f * SDL_max(0.0f, SDL_cosf(phase));
    return LegPose{thigh, thigh - bend};
}

static const float HIP_Y = 23.0f;
static const float THIGH_LEN = 11.0f;
static const float SHIN_LEN = 10.5f;

static float ankle_height(const LegPose &p)
{
    return HIP_Y - THIGH_LEN * SDL_cosf(p.thigh) - SHIN_LEN * SDL_cosf(p.shin);
}

static void draw_leg(Canvas &cv, const Body &b, float side, const LegPose &p, bool far, int part)
{
    const Tone pants = far ? far_tone(T_PANTS) : T_PANTS;
    const Tone shoe = far ? far_tone(T_SHOE) : T_SHOE;
    const float lat = side * 2.8f;

    const float kf = THIGH_LEN * SDL_sinf(p.thigh);
    const float ku = HIP_Y - THIGH_LEN * SDL_cosf(p.thigh);
    const float af = kf + SHIN_LEN * SDL_sinf(p.shin);
    const float au = ku - SHIN_LEN * SDL_cosf(p.shin);

    const V2 hip = proj(b, lat, HIP_Y, 0.0f);
    const V2 knee = proj(b, lat, ku, kf);
    const V2 ankle = proj(b, lat, au, af);
    const V2 toe = proj(b, lat, au - 0.6f, af + 3.6f);

    capsule(cv, hip, knee, 2.7f, pants, part);
    capsule(cv, knee, ankle, 2.3f, pants, part);
    capsule(cv, ankle, toe, 1.9f, shoe, part);

    const int sole_row = (int)SDL_floorf(SDL_max(ankle.y, toe.y) + 1.4f);
    for (int col = 0; col < CV_W; ++col) {
        if (sole_row >= 0 && sole_row < CV_H && cv.px[sole_row][col].part == part &&
            (cv.px[sole_row][col].c == shoe.light || cv.px[sole_row][col].c == shoe.base ||
             cv.px[sole_row][col].c == shoe.dark)) {
            cv.px[sole_row][col].c = far ? scale_color(C_SOLE, 0.72f) : C_SOLE;
        }
    }
}

static void draw_arm(Canvas &cv, const Body &b, float side, float swing, bool far, int part)
{
    const Tone sleeve = far ? far_tone(T_JACKET) : T_JACKET;
    const Tone skin = far ? far_tone(T_SKIN) : T_SKIN;
    const float upper = swing;
    const float lower = swing + 0.45f + 0.35f * SDL_max(0.0f, swing);

    const float sl = side * 5.6f;
    const float su = 38.0f;
    const float ef = 8.0f * SDL_sinf(upper);
    const float eu = su - 8.0f * SDL_cosf(upper);
    const float hf = ef + 7.5f * SDL_sinf(lower);
    const float hu = eu - 7.5f * SDL_cosf(lower);
    const float hl = sl + side * 0.8f;

    const V2 shoulder = proj(b, sl, su, 0.0f);
    const V2 elbow = proj(b, sl + side * 0.6f, eu, ef);
    const V2 hand = proj(b, hl, hu, hf);

    capsule(cv, shoulder, elbow, 2.5f, sleeve, part);
    capsule(cv, elbow, hand, 2.2f, sleeve, part);
    capsule(cv, hand, hand, 1.8f, skin, part);
}

static void draw_torso(Canvas &cv, const Body &b, float lean, int part)
{
    const V2 hip = proj(b, 0.0f, 25.0f, 0.0f);
    const V2 chest = proj(b, 0.0f, 35.5f, lean);
    capsule(cv, hip, chest, 5.4f, T_JACKET, part);
    capsule(cv, proj(b, -4.6f, 37.5f, lean), proj(b, 4.6f, 37.5f, lean), 3.0f, T_JACKET, part);

    if (b.fz > 0.5f) {
        const V2 top = proj(b, 0.0f, 37.0f, lean + 5.0f);
        const V2 bottom = proj(b, 0.0f, 23.5f, 5.0f);
        for (float t = 0.0f; t <= 1.0f; t += 0.05f) {
            plot(cv, (int)(top.x + (bottom.x - top.x) * t), (int)(top.y + (bottom.y - top.y) * t),
                 T_JACKET.dark, part);
        }
        const V2 collar = proj(b, 0.0f, 39.0f, lean + 4.0f);
        capsule(cv, collar, collar, 1.6f, Tone{0xff2c2c34, 0xff2c2c34, 0xff1c1c22}, part);
    }
}

static void draw_head(Canvas &cv, const Body &b, float lean, int part)
{
    capsule(cv, proj(b, 0.0f, 39.0f, lean), proj(b, 0.0f, 40.8f, lean), 1.6f, T_SKIN, part);

    const V2 c = proj(b, 0.0f, 45.6f, lean + 0.5f);
    const float rx = 4.3f;
    const float ry = 5.0f;
    for (int row = (int)(c.y - ry - 1); row <= (int)(c.y + ry + 1); ++row) {
        for (int col = (int)(c.x - rx - 1); col <= (int)(c.x + rx + 1); ++col) {
            const float nx = (col + 0.5f - c.x) / rx;
            const float ny = (row + 0.5f - c.y) / ry;
            const float rr = nx * nx + ny * ny;
            if (rr > 1.0f) {
                continue;
            }
            const float nz = SDL_sqrtf(SDL_max(0.0f, 1.0f - rr));
            const float facing = nx * b.fx + nz * b.fz;
            const float up = -ny;
            const bool hair = up > 0.3f || facing < -0.15f || (up > -0.2f && facing < 0.2f);
            plot(cv, col, row, shade(hair ? T_HAIR : T_SKIN, nx, ny), part);
        }
    }

    for (int k = -1; k <= 1; k += 2) {
        const float ex = b.fx * 0.8f + b.rx * 0.5f * k;
        const float ez = b.fz * 0.8f + b.rz * 0.5f * k;
        if (ez > 0.15f) {
            plot(cv, (int)(c.x + ex * rx * 0.85f), (int)(c.y + 0.2f), C_EYE, part);
        }
    }
    if (SDL_fabsf(b.fx) > 0.5f) {
        plot(cv, (int)(c.x + b.fx * (rx + 0.4f)), (int)(c.y + 1.4f), T_SKIN.base, part);
    }
    if (b.fz > 0.3f) {
        const int mx = (int)(c.x + b.fx * rx * 0.6f);
        plot(cv, mx, (int)(c.y + 3.0f), C_MOUTH, part);
        if (b.fz > 0.8f) {
            plot(cv, mx - 1, (int)(c.y + 3.0f), C_MOUTH, part);
        }
    }
}

static void add_outlines(Canvas &cv)
{
    const Canvas src = cv;
    static const int DX[4] = {1, -1, 0, 0};
    static const int DY[4] = {0, 0, 1, -1};
    for (int row = 0; row < CV_H; ++row) {
        for (int col = 0; col < CV_W; ++col) {
            const Cell &cell = src.px[row][col];
            for (int k = 0; k < 4; ++k) {
                const int r = row + DY[k];
                const int cc = col + DX[k];
                if (r < 0 || r >= CV_H || cc < 0 || cc >= CV_W) {
                    continue;
                }
                const Cell &n = src.px[r][cc];
                if (!cell.c && n.c) {
                    cv.px[row][col] = Cell{C_OUTLINE, -1};
                    break;
                }
                if (cell.c && n.c && n.part > cell.part) {
                    cv.px[row][col] = Cell{C_OUTLINE, cell.part};
                    break;
                }
            }
        }
    }
}

static void facing_vector(Facing f, float *fx, float *fz)
{
    const float d = 0.7071f;
    switch (f) {
        case FACE_DOWN:       *fx = 0.0f; *fz = 1.0f;  break;
        case FACE_UP:         *fx = 0.0f; *fz = -1.0f; break;
        case FACE_LEFT:       *fx = -1.0f; *fz = 0.0f; break;
        case FACE_RIGHT:      *fx = 1.0f; *fz = 0.0f;  break;
        case FACE_DOWN_LEFT:  *fx = -d; *fz = d;  break;
        case FACE_DOWN_RIGHT: *fx = d;  *fz = d;  break;
        case FACE_UP_LEFT:    *fx = -d; *fz = -d; break;
        case FACE_UP_RIGHT:   *fx = d;  *fz = -d; break;
    }
}

static void build_character(Canvas &cv, Facing facing, int frame)
{
    SDL_memset(&cv, 0, sizeof(cv));
    const bool moving = frame >= 0;
    const float phase = moving ? frame * (SDL_PI_F / 4.0f) : 0.0f;

    Body b{};
    facing_vector(facing, &b.fx, &b.fz);
    b.rx = -b.fz;
    b.rz = b.fx;

    const LegPose left = leg_pose(phase, moving);
    const LegPose right = leg_pose(phase + SDL_PI_F, moving);
    b.lift = 2.2f - SDL_min(ankle_height(left), ankle_height(right));
    const float lean = moving ? 1.5f : 0.0f;

    const float near_left = -b.rz;
    const float near_right = b.rz;
    const float arm_swing_left = moving ? -0.55f * SDL_sinf(phase) : 0.0f;
    const float arm_swing_right = moving ? -0.55f * SDL_sinf(phase + SDL_PI_F) : 0.0f;

    int part = 1;
    if (near_left < -0.2f) {
        draw_arm(cv, b, -1.0f, arm_swing_left, true, part++);
    }
    if (near_right < -0.2f) {
        draw_arm(cv, b, 1.0f, arm_swing_right, true, part++);
    }
    if (near_left <= near_right) {
        draw_leg(cv, b, -1.0f, left, near_left < -0.2f, part++);
        draw_leg(cv, b, 1.0f, right, near_right < -0.2f, part++);
    } else {
        draw_leg(cv, b, 1.0f, right, near_right < -0.2f, part++);
        draw_leg(cv, b, -1.0f, left, near_left < -0.2f, part++);
    }
    draw_torso(cv, b, lean, part++);
    if (near_left >= -0.2f) {
        draw_arm(cv, b, -1.0f, arm_swing_left, false, part++);
    }
    if (near_right >= -0.2f) {
        draw_arm(cv, b, 1.0f, arm_swing_right, false, part++);
    }
    draw_head(cv, b, lean, part++);
    add_outlines(cv);
}

static float char_scale(void)
{
    return SDL_max(1.0f, SDL_floorf(P * 0.5f));
}

static void draw_shadow(float fx, float fy)
{
    const float s = char_scale();
    SDL_SetRenderDrawColor(rend, 0, 0, 0, 90);
    const SDL_FRect rows[3] = {
        {fx - 7 * s, fy - 2 * s, 14 * s, 1 * s},
        {fx - 10 * s, fy - 1 * s, 20 * s, 2 * s},
        {fx - 7 * s, fy + 1 * s, 14 * s, 1 * s}
    };
    SDL_RenderFillRects(rend, rows, 3);
}

static void draw_character(float fx, float fy, Facing facing, int frame, bool ghost, Uint8 ghost_alpha)
{
    static Canvas cv;
    build_character(cv, facing, frame);

    const float s = char_scale();
    const float left = fx - CV_CX * s;
    const float top = fy - CV_FEET * s;

    if (ghost) {
        SDL_SetRenderDrawColor(rend, 220, 28, 28, ghost_alpha);
    }
    for (int row = 0; row < CV_H; ++row) {
        int col = 0;
        while (col < CV_W) {
            const Uint32 c = cv.px[row][col].c;
            int run = 1;
            while (col + run < CV_W) {
                const Uint32 n = cv.px[row][col + run].c;
                if (ghost ? ((n != 0) != (c != 0)) : (n != c)) {
                    break;
                }
                ++run;
            }
            if (c) {
                if (!ghost) {
                    SDL_SetRenderDrawColor(rend, (c >> 16) & 0xff, (c >> 8) & 0xff, c & 0xff, 255);
                }
                SDL_FRect dst = {left + col * s, top + row * s, run * s, s};
                SDL_RenderFillRect(rend, &dst);
            }
            col += run;
        }
    }
}

static const int SHEET_CW = 32;
static const int SHEET_CH = 48;
static const int SHEET_COLS = 9;
static const int SHEET_ROWS = 8;
/* Sheet rows: DOWN, DOWN-R, RIGHT, UP-R, UP, UP-L, LEFT, DOWN-L (same order as the editor). */
static const int ROW_OF_FACING[8] = {0, 4, 6, 2, 7, 1, 5, 3};
static const int MIRROR_ROW[SHEET_ROWS] = {0, 7, 6, 5, 4, 3, 2, 1};
static SDL_Texture *sheet_tex = nullptr;
static bool sheet_used[SHEET_ROWS][SHEET_COLS];

void Game_LoadSprites(SDL_Renderer *renderer, const char *path)
{
    if (sheet_tex) {
        SDL_DestroyTexture(sheet_tex);
        sheet_tex = nullptr;
    }
    SDL_memset(sheet_used, 0, sizeof(sheet_used));

    SDL_Surface *loaded = SDL_LoadPNG(path);
    if (!loaded) {
        return;
    }
    SDL_Surface *surf = SDL_ConvertSurface(loaded, SDL_PIXELFORMAT_ARGB8888);
    SDL_DestroySurface(loaded);
    if (!surf) {
        return;
    }
    bool any = false;
    for (int row = 0; row < SHEET_ROWS; ++row) {
        for (int col = 0; col < SHEET_COLS; ++col) {
            for (int y = row * SHEET_CH; y < (row + 1) * SHEET_CH && y < surf->h && !sheet_used[row][col]; ++y) {
                const Uint32 *line = (const Uint32 *)((const Uint8 *)surf->pixels + y * surf->pitch);
                for (int x = col * SHEET_CW; x < (col + 1) * SHEET_CW && x < surf->w; ++x) {
                    if (line[x] >> 24) {
                        sheet_used[row][col] = true;
                        any = true;
                        break;
                    }
                }
            }
        }
    }
    if (any) {
        sheet_tex = SDL_CreateTextureFromSurface(renderer, surf);
        if (sheet_tex) {
            SDL_SetTextureScaleMode(sheet_tex, SDL_SCALEMODE_NEAREST);
            SDL_SetTextureBlendMode(sheet_tex, SDL_BLENDMODE_BLEND);
        }
    }
    SDL_DestroySurface(surf);
}

static bool row_has_frames(int row)
{
    for (int col = 0; col < SHEET_COLS; ++col) {
        if (sheet_used[row][col]) {
            return true;
        }
    }
    return false;
}

static bool draw_sheet_sprite(float fx, float fy, Facing facing, float walk_t, bool moving, bool ghost, Uint8 ghost_alpha)
{
    if (!sheet_tex) {
        return false;
    }
    int row = ROW_OF_FACING[facing];
    bool flip = false;
    if (!row_has_frames(row)) {
        if (row_has_frames(MIRROR_ROW[row])) {
            row = MIRROR_ROW[row];
            flip = true;
        } else if (row_has_frames(0)) {
            row = 0;
        } else {
            return false;
        }
    }

    int walk[SHEET_COLS];
    int walk_count = 0;
    for (int col = 1; col < SHEET_COLS; ++col) {
        if (sheet_used[row][col]) {
            walk[walk_count++] = col;
        }
    }
    int col = 0;
    if (moving && walk_count > 0) {
        col = walk[((int)(walk_t * 10.0f)) % walk_count];
    } else if (!sheet_used[row][0] && walk_count > 0) {
        col = walk[0];
    }

    const float s = char_scale();
    const SDL_FRect src = {(float)(col * SHEET_CW), (float)(row * SHEET_CH), (float)SHEET_CW, (float)SHEET_CH};
    const SDL_FRect dst = {fx - (SHEET_CW / 2) * s, fy - (SHEET_CH - 1) * s, SHEET_CW * s, SHEET_CH * s};
    if (ghost) {
        SDL_SetTextureColorMod(sheet_tex, 255, 40, 40);
        SDL_SetTextureAlphaMod(sheet_tex, ghost_alpha);
    }
    SDL_RenderTextureRotated(rend, sheet_tex, &src, &dst, 0.0, nullptr, flip ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE);
    if (ghost) {
        SDL_SetTextureColorMod(sheet_tex, 255, 255, 255);
        SDL_SetTextureAlphaMod(sheet_tex, 255);
    }
    return true;
}

static void draw_player(void)
{
    const float s = char_scale();
    const SDL_FPoint f = project(player.x, player.y);
    const float fx = SDL_roundf(f.x / s) * s;
    const float fy = SDL_roundf(f.y / s) * s;
    const int frame = player.moving ? ((int)(player.walk_t * 12.0f)) % 8 : -1;
    draw_shadow(fx, fy);
    if (!draw_sheet_sprite(fx, fy, player.facing, player.walk_t, player.moving, false, 0)) {
        draw_character(fx, fy, player.facing, frame, false, 0);
    }
}

static void draw_ghosts(void)
{
    const float s = char_scale();
    for (const Ghost &g : ghosts) {
        const SDL_FPoint f = project(g.x, g.y);
        const float fx = SDL_roundf(f.x / s) * s;
        const float fy = SDL_roundf(f.y / s) * s;
        const Uint8 a = (Uint8)(SDL_clamp(g.life / GHOST_LIFE, 0.0f, 1.0f) * 140.0f);
        if (!draw_sheet_sprite(fx, fy, g.facing, 0.0f, false, true, a)) {
            draw_character(fx, fy, g.facing, 2, true, a);
        }
    }
}

static void draw_hud(int w, int h)
{
    const float scale = SDL_max(1.0f, (float)h / 360.0f);
    SDL_SetRenderScale(rend, scale, scale);
    SDL_SetRenderDrawColor(rend, 220, 28, 28, SDL_ALPHA_OPAQUE);
    SDL_RenderDebugText(rend, 8, 8, difficulty);
    SDL_SetRenderScale(rend, 1.0f, 1.0f);

    const char *hint = "WASD/ARROWS=Move  SPACE=Dash  ESC=Menu";
    SDL_SetRenderDrawColor(rend, 170, 170, 170, SDL_ALPHA_OPAQUE);
    SDL_RenderDebugText(rend, (w - (float)SDL_strlen(hint) * 8.0f) * 0.5f, (float)h - 28.0f, hint);
}

void Game_Render(SDL_Renderer *renderer, int w, int h)
{
    rend = renderer;
    P = SDL_max(1.0f, SDL_floorf((float)h / 180.0f));
    TW = 28.0f * P;
    TH = TW * 0.5f;

    origin_x = 0.0f;
    origin_y = 0.0f;
    const SDL_FPoint cam = project(player.x, player.y);
    origin_x = SDL_roundf((float)w * 0.5f - cam.x);
    origin_y = SDL_roundf((float)h * 0.58f - cam.y);

    SDL_SetRenderDrawColor(rend, 10, 6, 12, SDL_ALPHA_OPAQUE);
    SDL_RenderClear(rend);
    SDL_SetRenderDrawBlendMode(rend, SDL_BLENDMODE_BLEND);

    draw_floor();

    const float player_key = player.x + player.y;
    bool player_drawn = false;
    for (int k = 0; k < PILLAR_COUNT; ++k) {
        const float key = (float)(PILLARS[k].i + PILLARS[k].j) + 1.0f;
        if (!player_drawn && player_key < key) {
            draw_ghosts();
            draw_player();
            player_drawn = true;
        }
        draw_pillar(PILLARS[k]);
    }
    if (!player_drawn) {
        draw_ghosts();
        draw_player();
    }

    draw_hud(w, h);
    SDL_SetRenderDrawBlendMode(rend, SDL_BLENDMODE_NONE);
}
