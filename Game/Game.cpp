#include "Game.hpp"
#include "AssetPath.hpp"
#include "Map.hpp"
#include "Settings.hpp"
#include "SpritePack.hpp"
#include "Video.hpp"
#include <algorithm>
#include <numeric>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <deque>
#include <iterator>
#include <string>
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
    /* Time in the current sprite action; restarts when switching between idle and moving. */
    float anim_t;
    bool anim_moving;
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

/* Character that wanders around the arena on its own. */
struct Npc {
    int kind;   /* index into NPC_KINDS */
    int leader; /* index into npcs of the NPC this one follows, LEADER_PLAYER, or -1 */
    int slot;   /* place in the leader's formation */
    float x;
    float y;
    Facing facing;
    bool moving;
    float anim_t;
    float target_x;
    float target_y;
    float wait;
};

static const int ARENA_N = 14;
static const float PLAYER_RADIUS = 0.3f;
static const float WALK_SPEED = 3.2f;
static const float DASH_SPEED = 13.0f;
static const float DASH_TIME = 0.14f;
static const float DASH_COOLDOWN = 0.45f;
static const float GHOST_LIFE = 0.25f;
static const float PLAYER_MAX_HP = 100.0f;
struct NpcKind {
    const char *pack; /* folder in assets/sprites */
    float radius;
    float speed;
    int count;   /* how many spawn */
    int follows; /* index into NPC_KINDS of the leader they walk behind, or -1 */
};
static const NpcKind BUILTIN_KINDS[] = {
    {"hawking", 0.45f, 1.1f, 1, -1},
    {"trump", 0.32f, 1.4f, 1, -1},
    {"michael", 0.3f, 1.6f, 1, -1},
    {"pope", 0.32f, 0.9f, 1, -1},
    {"anonymous", 0.3f, 1.5f, 1, -1},
    {"gates", 0.3f, 1.3f, 1, -1},
    {"diddy", 0.3f, 1.4f, 1, -1},
    {"cult_priest", 0.34f, 0.8f, 1, -1},
    {"cult_acolyte", 0.3f, 0.8f, 4, 7},
    {"clinton", 0.32f, 1.3f, 1, -1},
    {"ostrich", 0.4f, 2.2f, 3, -1},
    {"kirk", 0.3f, 1.4f, 1, -1},
    {"maxwell", 0.3f, 1.3f, 1, -1},
    {"andrew", 0.3f, 1.2f, 1, -1},
    {"manson", 0.3f, 1.1f, 1, -1},
    {"manson_girl", 0.3f, 1.1f, 3, 14},
    {"lavey", 0.3f, 0.9f, 1, -1},
    {"escobar", 0.34f, 1.2f, 1, -1},
    {"cia", 0.3f, 1.6f, 1, -1},
    {"mossad", 0.3f, 1.6f, 1, -1},
    {"illuminati", 0.32f, 0.8f, 3, -1},
    {"billionaire", 0.3f, 1.0f, 1, -1},
    {"politico", 0.3f, 1.2f, 1, -1},
    {"mib", 0.3f, 1.4f, 2, -1},
    {"dictador", 0.3f, 1.0f, 1, -1},
    {"conspiranoico", 0.3f, 1.5f, 1, -1},
    {"maduro", 0.34f, 1.1f, 1, -1},
    {"kim", 0.34f, 1.0f, 1, -1},
    {"putin", 0.3f, 1.2f, 1, -1},
    {"musk", 0.3f, 1.4f, 1, -1},
    {"zuckerberg", 0.3f, 1.4f, 1, -1},
    {"fujimori", 0.3f, 1.1f, 1, -1},
    {"jesus", 0.32f, 0.9f, 1, -1},
    {"homelander", 0.32f, 1.5f, 1, -1},
    {"walter_white", 0.3f, 1.2f, 1, -1},
    {"bateman", 0.3f, 1.4f, 1, -1},
    {"hitler", 0.3f, 1.2f, 1, -1},
    {"bin_laden", 0.32f, 1.0f, 1, -1},
    {"ricardo", 0.3f, 1.8f, 1, -1},
    {"alex", 0.3f, 1.4f, 1, -1},
    {"dim", 0.34f, 1.4f, 1, 39},
    {"georgie", 0.3f, 1.4f, 1, 39},
    {"pete", 0.3f, 1.4f, 1, 39},
    {"springtrap", 0.34f, 1.0f, 1, -1},
    {"guardia", 0.32f, 1.6f, 2, -1},
    {"epstein", 0.32f, 1.1f, 1, -1},
    {"purple_guy", 0.3f, 1.5f, 1, -1},
    {"gary_coleman", 0.26f, 1.5f, 1, -1},
};
/* Formation around a leader, as (behind, to the side) in tiles relative to where it faces. */
static const float FOLLOW_SLOTS[][2] = {{1.0f, -0.8f}, {1.0f, 0.8f}, {1.9f, -0.8f}, {1.9f, 0.8f}, {2.8f, 0.0f}};
static const int FOLLOW_SLOT_COUNT = (int)(sizeof(FOLLOW_SLOTS) / sizeof(FOLLOW_SLOTS[0]));
/* The built-in cast, plus the characters made with the Sprite Editor's creator. */
static std::vector<NpcKind> NPC_KINDS(std::begin(BUILTIN_KINDS), std::end(BUILTIN_KINDS));
static const int LEADER_PLAYER = -2;

struct PlayableCharacter {
    const char *pack; /* folder in assets/sprites */
    const char *name;
    const char *npc;  /* NPC_KINDS pack left out while playing as them, or nullptr */
};
static const PlayableCharacter BUILTIN_PLAYABLE[] = {
    {"player", "Moon", nullptr},
    /*{"players/trump", "Donald Trump", "trump"},
    {"players/clinton", "Bill Clinton", "clinton"},
    {"players/michael", "Michael Jackson", "michael"},
    {"players/pope", "El Papa", "pope"},
    {"players/anonymous", "Anonymous", "anonymous"},
    {"players/gates", "Bill Gates", "gates"},
    {"players/diddy", "P. Diddy", "diddy"},
    {"players/cult_priest", "Sumo Sacerdote", "cult_priest"},
    {"players/cult_acolyte", "Acolito", nullptr},
    {"players/kirk", "Charlie Kirk", "kirk"},
    {"players/maxwell", "Ghislaine Maxwell", "maxwell"},
    {"players/andrew", "Principe Andres", "andrew"},
    {"players/manson", "Charles Manson", "manson"},
    {"players/manson_girl", "Chica de la Familia", nullptr},
    {"players/lavey", "Anton LaVey", "lavey"},
    {"players/escobar", "Pablo Escobar", "escobar"},
    {"players/cia", "Agente de la CIA", nullptr},
    {"players/mossad", "Agente del Mossad", "mossad"},
    {"players/illuminati", "Illuminati", nullptr},
    {"players/billionaire", "Multimillonario anonimo", "billionaire"},
    {"players/politico", "Politico corrupto", "politico"},
    {"players/mib", "Hombre de negro", nullptr},
    {"players/dictador", "Dictador", "dictador"},
    {"players/conspiranoico", "Conspiranoico", "conspiranoico"},
    {"players/maduro", "Nicolas Maduro", "maduro"},
    {"players/kim", "Kim Jong Un", "kim"},
    {"players/putin", "Vladimir Putin", "putin"},
    {"players/musk", "Elon Musk", "musk"},
    {"players/zuckerberg", "Mark Zuckerberg", "zuckerberg"},
    {"players/fujimori", "Alberto Fujimori", "fujimori"},
    {"players/jesus", "Jesucristo", "jesus"},
    {"players/homelander", "Homelander", "homelander"},
    {"players/walter_white", "Walter White", "walter_white"},
    {"players/bateman", "Patrick Bateman", "bateman"},
    {"players/hitler", "Adolf Hitler", "hitler"},
    {"players/bin_laden", "Osama bin Laden", "bin_laden"},
    {"players/ricardo", "Ricardo Milos", "ricardo"},
    {"players/alex", "Alex DeLarge", "alex"},
    {"players/dim", "Dim", "dim"},
    {"players/georgie", "Georgie", "georgie"},
    {"players/pete", "Pete", "pete"},
    {"players/springtrap", "Springtrap", "springtrap"},
    {"players/purple_guy", "Purple Guy", "purple_guy"},
    {"players/gary_coleman", "Gary Coleman", "gary_coleman"},*/
};
static std::vector<PlayableCharacter> PLAYABLE(std::begin(BUILTIN_PLAYABLE), std::end(BUILTIN_PLAYABLE));

/* Characters saved by the Sprite Editor's creator: assets/sprites/custom/<id>/ is the NPC
   pack and custom/<id>/jugador/ the playable one. Found once, the first time they're needed. */
static void find_custom_characters(void)
{
    static bool done = false;
    if (done) {
        return;
    }
    done = true;
    static std::deque<std::string> strings; /* NpcKind and PlayableCharacter point into these */
    const std::string root = std::string(Asset_Root()) + "assets/sprites/";
    int count = 0;
    char **names = SDL_GlobDirectory((root + "custom").c_str(), "*", 0, &count);
    if (!names) {
        return;
    }
    std::vector<std::string> sorted(names, names + count);
    SDL_free(names);
    std::sort(sorted.begin(), sorted.end());
    for (const std::string &id : sorted) {
        const std::string pack = "custom/" + id;
        SpritePack info;
        if (!SpritePack_Load(&info, (root + pack).c_str(), false)) {
            continue;
        }
        strings.push_back(pack);
        const char *npc = strings.back().c_str();
        strings.push_back(info.name.empty() ? id : info.name);
        const char *name = strings.back().c_str();
        SpritePack_Free(&info);
        NPC_KINDS.push_back(NpcKind{npc, 0.3f, 1.3f, 1, -1});
        SDL_PathInfo player_ini;
        if (SDL_GetPathInfo((root + pack + "/jugador/character.ini").c_str(), &player_ini)) {
            strings.push_back(pack + "/jugador");
            PLAYABLE.push_back(PlayableCharacter{strings.back().c_str(), name, npc});
        }
        SDL_Log("Custom character %s (%s)", name, pack.c_str());
    }
}

static const PlayableCharacter &playable(void)
{
    return PLAYABLE[SDL_clamp(settings.player_character, 0, (int)PLAYABLE.size() - 1)];
}

static bool kind_is_player(int k)
{
    const char *npc = playable().npc;
    return npc && SDL_strcmp(NPC_KINDS[k].pack, npc) == 0;
}
static const Pillar PILLARS[] = {{3, 3}, {3, 10}, {10, 3}, {10, 10}};
static const int PILLAR_COUNT = (int)(sizeof(PILLARS) / sizeof(PILLARS[0]));

/* The Tiled map when one loaded; otherwise the built-in arena with its pillars. */
static float world_w(void)
{
    return Map_Loaded() ? (float)Map_Width() : (float)ARENA_N;
}

static float world_h(void)
{
    return Map_Loaded() ? (float)Map_Height() : (float)ARENA_N;
}

/* Whether a circle of radius r fits at (x, y): on walkable ground, out of solid objects. */
static bool world_free(float x, float y, float r)
{
    if (!Map_Loaded()) {
        if (x < 1.0f || y < 1.0f || x > ARENA_N - 1.0f || y > ARENA_N - 1.0f) {
            return false;
        }
        for (int k = 0; k < PILLAR_COUNT; ++k) {
            if (SDL_fabsf(x - (PILLARS[k].i + 0.5f)) < 0.5f + r && SDL_fabsf(y - (PILLARS[k].j + 0.5f)) < 0.5f + r) {
                return false;
            }
        }
        return true;
    }
    for (int j = (int)SDL_floorf(y - r); j <= (int)SDL_floorf(y + r); ++j) {
        for (int i = (int)SDL_floorf(x - r); i <= (int)SDL_floorf(x + r); ++i) {
            if (!Map_Walkable(i, j)) {
                return false;
            }
        }
    }
    for (const MapBox &b : Map_SolidBoxes()) {
        if (x > b.x0 - r && x < b.x1 + r && y > b.y0 - r && y < b.y1 + r) {
            return false;
        }
    }
    return true;
}

/* A straight walk from a to b stays clear. */
static bool world_path_free(float ax, float ay, float bx, float by, float r)
{
    const float len = SDL_sqrtf((bx - ax) * (bx - ax) + (by - ay) * (by - ay));
    const int steps = SDL_max(1, (int)(len / 0.25f));
    for (int k = 1; k <= steps; ++k) {
        const float t = (float)k / (float)steps;
        if (!world_free(ax + (bx - ax) * t, ay + (by - ay) * t, r)) {
            return false;
        }
    }
    return true;
}

static Player player;
static std::vector<Ghost> ghosts;
static std::vector<Npc> npcs;
static const char *difficulty = "";
static float player_hp = PLAYER_MAX_HP;

static SDL_Renderer *rend = nullptr;
static float P = 6.0f;
/* Mouse-wheel zoom, in steps of one sprite pixel size (2 world P), so pixel art stays crisp
   once a step settles. Limited to half and double the default view. */
static int zoom_step = 0;
static float zoom_p = 0.0f; /* P shown on screen, easing toward the zoom step's P */
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

/* World move direction -> the nearest of the 8 screen facings. */
static Facing world_to_facing(float wx, float wy)
{
    const float sx = wx - wy;
    const float sy = (wx + wy) * 0.5f;
    const float sector = SDL_roundf(SDL_atan2f(sy, sx) / (SDL_PI_F * 0.25f));
    const float a = sector * SDL_PI_F * 0.25f;
    return screen_to_facing(SDL_roundf(SDL_cosf(a)), SDL_roundf(SDL_sinf(a)));
}

static void npc_pick_target(Npc &n)
{
    /* Short walks (2-5 tiles) keep the crowd spread out: far targets all cross the middle
       and jam there. */
    for (int tries = 0; tries < 20; ++tries) {
        const float a = SDL_randf() * 2.0f * SDL_PI_F;
        const float d = 2.0f + SDL_randf() * 3.0f;
        const float x = n.x + SDL_cosf(a) * d;
        const float y = n.y + SDL_sinf(a) * d;
        if (world_path_free(n.x, n.y, x, y, NPC_KINDS[n.kind].radius)) {
            n.target_x = x;
            n.target_y = y;
            return;
        }
    }
    n.target_x = n.x;
    n.target_y = n.y;
}

/* Maps: the climb starts in the cells under the main building; stairs lead up, floor by
   floor, to the island's exterior. */
static const char *const START_MAP = "celdas.tmj";
static SDL_Renderer *map_renderer = nullptr;
static std::string maps_dir;
static std::string cur_map;
/* Door transition: fade to black, switch maps, fade back in. */
static int fade_state = 0; /* 0 none, 1 going out, 2 coming in */
static float fade = 0.0f;
static MapDoor pending_door;
/* Set on arrival while standing in the door just used, so it doesn't send you back. */
static bool door_lock = false;
static float banner_t = 0.0f;

static void place_player(const char *point)
{
    player.x = world_w() * 0.5f;
    player.y = world_h() * 0.5f;
    for (const MapPoint &p : Map_Points()) {
        if (p.name == point) {
            player.x = p.x;
            player.y = p.y;
        }
    }
}

static int kind_of_pack(const std::string &pack)
{
    for (int k = 0; k < (int)NPC_KINDS.size(); ++k) {
        if (pack == NPC_KINDS[k].pack) {
            return k;
        }
    }
    return -1;
}

/* NPCs of the current map: one at each "npc" point, plus (on maps marked "poblar") everyone
   the map doesn't exclude, on a lattice around the player. */
static void populate(void)
{
    ghosts.clear();
    npcs.clear();

    for (const MapPoint &mp : Map_Points()) {
        if (mp.type != "npc") {
            continue;
        }
        const int k = kind_of_pack(mp.name);
        if (k < 0 || kind_is_player(k)) {
            continue;
        }
        Npc n{};
        n.kind = k;
        n.leader = -1;
        n.x = mp.x;
        n.y = mp.y;
        n.facing = FACE_DOWN_LEFT;
        n.wait = 0.5f + (float)(npcs.size() % 5) * 0.6f;
        npcs.push_back(n);
    }

    if (!Map_Loaded() || Map_Populates()) {
        /* Spawn points: a lattice over the open ground near the player's start (but not on
           it), visited in a scrambled (but fixed) order so neighbours in the list spread out. */
        std::vector<SDL_FPoint> spawn;
        const float spawn_reach = Map_Loaded() ? 14.0f : 1e9f;
        for (float sx = 1.0f; sx < world_w() - 0.5f; sx += 1.25f) {
            for (float sy = 1.0f; sy < world_h() - 0.5f; sy += 1.25f) {
                const float dx = sx - player.x;
                const float dy = sy - player.y;
                if (SDL_fabsf(dx) + SDL_fabsf(dy) > 2.0f && dx * dx + dy * dy < spawn_reach * spawn_reach &&
                    world_free(sx, sy, 0.6f)) {
                    spawn.push_back(SDL_FPoint{sx, sy});
                }
            }
        }
        if (spawn.empty()) {
            spawn.push_back(SDL_FPoint{player.x + 1.5f, player.y});
        }
        const int spawn_count = (int)spawn.size();
        /* A stride coprime with the count visits every point before repeating one. */
        int stride = 23;
        while (std::gcd(stride, spawn_count) != 1) {
            ++stride;
        }
        int place = 0;
        for (int k = 0; k < (int)NPC_KINDS.size(); ++k) {
            if (NPC_KINDS[k].follows >= 0 || kind_is_player(k) || (Map_Loaded() && Map_Excludes(NPC_KINDS[k].pack))) {
                continue;
            }
            for (int i = 0; i < NPC_KINDS[k].count; ++i) {
                Npc n{};
                n.kind = k;
                n.leader = -1;
                const SDL_FPoint at = spawn[(place * stride + 5) % spawn_count];
                n.x = at.x;
                n.y = at.y;
                n.facing = FACE_DOWN_LEFT;
                n.wait = 0.5f + (float)(place % 5) * 0.6f;
                npcs.push_back(n);
                ++place;
            }
        }
    }
    /* Followers start in formation around the first NPC of their leader's kind, or around
       the player when that's who they are playing as. */
    for (int k = 0; k < (int)NPC_KINDS.size(); ++k) {
        const int lead_kind = NPC_KINDS[k].follows;
        if (lead_kind < 0 || kind_is_player(k)) {
            continue;
        }
        int leader = kind_is_player(lead_kind) ? LEADER_PLAYER : -1;
        for (int i = 0; leader == -1 && i < (int)npcs.size(); ++i) {
            if (npcs[i].kind == lead_kind) {
                leader = i;
            }
        }
        if (leader == -1) {
            continue;
        }
        const float lx = leader == LEADER_PLAYER ? player.x : npcs[leader].x;
        const float ly = leader == LEADER_PLAYER ? player.y : npcs[leader].y;
        for (int i = 0; i < NPC_KINDS[k].count; ++i) {
            Npc n{};
            n.kind = k;
            n.leader = leader;
            n.slot = i % FOLLOW_SLOT_COUNT;
            n.x = lx + FOLLOW_SLOTS[n.slot][0] * 0.7f;
            n.y = ly + FOLLOW_SLOTS[n.slot][1] * 0.7f - 0.6f;
            n.facing = leader == LEADER_PLAYER ? player.facing : npcs[leader].facing;
            npcs.push_back(n);
        }
    }
}

static bool player_in_door(const MapDoor **door)
{
    for (const MapDoor &d : Map_Doors()) {
        if (player.x >= d.area.x0 && player.x <= d.area.x1 && player.y >= d.area.y0 && player.y <= d.area.y1) {
            if (door) {
                *door = &d;
            }
            return true;
        }
    }
    return false;
}

/* Loads a map next to the current one and puts the player at the named point. */
static bool enter_map(const std::string &file, const char *arrival)
{
    if (!map_renderer || !Map_Load(map_renderer, (maps_dir + file).c_str(), settings.pixel_art)) {
        return false;
    }
    cur_map = file;
    place_player(arrival);
    player.dash_t = 0.0f;
    populate();
    door_lock = player_in_door(nullptr);
    banner_t = 2.5f;
    return true;
}

const char *Game_Music(void)
{
    return Map_Loaded() ? Map_Music().c_str() : "";
}

void Game_Start(const char *difficulty_label)
{
    difficulty = difficulty_label ? difficulty_label : "";
    player = Player{};
    player.x = ARENA_N * 0.5f;
    player.y = ARENA_N * 0.5f - 2.0f;
    player.facing = FACE_DOWN;
    player_hp = PLAYER_MAX_HP;
    fade_state = 0;
    fade = 0.0f;
    if (Map_Loaded() && cur_map != START_MAP) {
        enter_map(START_MAP, "jugador");
        return;
    }
    if (Map_Loaded()) {
        place_player("jugador");
        banner_t = 2.5f;
    }
    door_lock = false;
    populate();
}

bool Game_Event(const SDL_Event *event)
{
    if (event->type == SDL_EVENT_MOUSE_WHEEL) {
        if (event->wheel.y > 0.0f) {
            ++zoom_step;
        } else if (event->wheel.y < 0.0f) {
            --zoom_step;
        }
        return false;
    }
    if (event->type != SDL_EVENT_KEY_DOWN || event->key.repeat) {
        return false;
    }
    if (event->key.scancode == SDL_SCANCODE_ESCAPE) {
        return true;
    }
    if (Settings_ActionPressed(event->key.scancode, ACTION_DASH) && player.dash_cd <= 0.0f) {
        const bool *keys = SDL_GetKeyboardState(nullptr);
        float sx = (float)(Settings_ActionHeld(keys, ACTION_RIGHT) - Settings_ActionHeld(keys, ACTION_LEFT));
        float sy = (float)(Settings_ActionHeld(keys, ACTION_DOWN) - Settings_ActionHeld(keys, ACTION_UP));
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

/* Pushes a circle of radius r out of the box [x0,x1] x [y0,y1]. */
static void push_out_of_box(float *x, float *y, float r, float x0, float y0, float x1, float y1)
{
    const float cx = SDL_clamp(*x, x0, x1);
    const float cy = SDL_clamp(*y, y0, y1);
    const float dx = *x - cx;
    const float dy = *y - cy;
    const float d2 = dx * dx + dy * dy;
    if (d2 >= r * r) {
        return;
    }
    if (d2 > 0.000001f) {
        const float d = SDL_sqrtf(d2);
        *x = cx + dx / d * r;
        *y = cy + dy / d * r;
        return;
    }
    /* Centre inside the box: out through the nearest side. */
    const float left = *x - x0, right = x1 - *x, up = *y - y0, down = y1 - *y;
    const float m = SDL_min(SDL_min(left, right), SDL_min(up, down));
    if (m == left) {
        *x = x0 - r;
    } else if (m == right) {
        *x = x1 + r;
    } else if (m == up) {
        *y = y0 - r;
    } else {
        *y = y1 + r;
    }
}

/* Keeps a circle of radius r inside the world and out of its obstacles: the pillars, or the
   map's unwalkable cells and solid objects. */
static void collide_world(float *x, float *y, float r)
{
    *x = SDL_clamp(*x, r, world_w() - r);
    *y = SDL_clamp(*y, r, world_h() - r);

    if (!Map_Loaded()) {
        for (int k = 0; k < PILLAR_COUNT; ++k) {
            const float x0 = (float)PILLARS[k].i;
            const float y0 = (float)PILLARS[k].j;
            push_out_of_box(x, y, r, x0, y0, x0 + 1.0f, y0 + 1.0f);
        }
        return;
    }
    for (const MapBox &b : Map_SolidBoxes()) {
        if (*x > b.x0 - r - 0.5f && *x < b.x1 + r + 0.5f && *y > b.y0 - r - 0.5f && *y < b.y1 + r + 0.5f) {
            push_out_of_box(x, y, r, b.x0, b.y0, b.x1, b.y1);
        }
    }
    for (int j = (int)SDL_floorf(*y - r); j <= (int)SDL_floorf(*y + r); ++j) {
        for (int i = (int)SDL_floorf(*x - r); i <= (int)SDL_floorf(*x + r); ++i) {
            if (!Map_Walkable(i, j)) {
                push_out_of_box(x, y, r, (float)i, (float)j, (float)i + 1.0f, (float)j + 1.0f);
            }
        }
    }
}

static void resolve_collisions(void)
{
    for (const Npc &n : npcs) {
        if (n.leader == LEADER_PLAYER) {
            continue; /* the player's own followers make way */
        }
        const float dx = player.x - n.x;
        const float dy = player.y - n.y;
        const float r = PLAYER_RADIUS + NPC_KINDS[n.kind].radius;
        const float d2 = dx * dx + dy * dy;
        if (d2 < r * r && d2 > 0.000001f) {
            const float d = SDL_sqrtf(d2);
            player.x = n.x + dx / d * r;
            player.y = n.y + dy / d * r;
        }
    }
    collide_world(&player.x, &player.y, PLAYER_RADIUS);
}

/* Walks to its place in the leader's formation; when there, faces where the leader faces. */
static void update_follower(Npc &n, float dt)
{
    const NpcKind &kind = NPC_KINDS[n.kind];
    const bool by_player = n.leader == LEADER_PLAYER;
    const float lead_x = by_player ? player.x : npcs[n.leader].x;
    const float lead_y = by_player ? player.y : npcs[n.leader].y;
    const Facing lead_facing = by_player ? player.facing : npcs[n.leader].facing;
    float fx;
    float fy;
    float wx = 1.0f;
    float wy = 0.0f;
    facing_to_screen(lead_facing, &fx, &fy);
    screen_to_world_dir(fx, fy, &wx, &wy);
    const float back = FOLLOW_SLOTS[n.slot][0];
    const float side = FOLLOW_SLOTS[n.slot][1];
    float tx = lead_x - wx * back - wy * side;
    float ty = lead_y - wy * back + wx * side;
    collide_world(&tx, &ty, kind.radius);
    const float dx = tx - n.x;
    const float dy = ty - n.y;
    const float dist = SDL_sqrtf(dx * dx + dy * dy);
    n.moving = dist > 0.2f;
    if (n.moving) {
        /* Hurry a bit when falling behind. */
        const float speed = kind.speed * (dist > 1.0f ? 1.6f : 1.0f);
        const float step = SDL_min(dist, speed * dt);
        n.x += dx / dist * step;
        n.y += dy / dist * step;
        collide_world(&n.x, &n.y, kind.radius);
        n.facing = world_to_facing(dx, dy);
    } else {
        n.facing = lead_facing;
    }
}

/* Rolls to a random spot, waits a few seconds, picks another. Stops when the player is in the way. */
static void update_npc(Npc &n, float dt)
{
    const NpcKind &kind = NPC_KINDS[n.kind];
    const bool was_moving = n.moving;
    n.moving = false;
    if (n.leader != -1) {
        update_follower(n, dt);
    } else if (n.wait > 0.0f) {
        n.wait -= dt;
        if (n.wait <= 0.0f) {
            npc_pick_target(n);
        }
    } else {
        const float dx = n.target_x - n.x;
        const float dy = n.target_y - n.y;
        const float dist = SDL_sqrtf(dx * dx + dy * dy);
        const float px = player.x - n.x;
        const float py = player.y - n.y;
        const float r = PLAYER_RADIUS + kind.radius + 0.1f;
        bool blocked = dist > 0.0f && px * px + py * py < r * r && (px * dx + py * dy) > 0.0f;
        for (const Npc &o : npcs) {
            const float ox = o.x - n.x;
            const float oy = o.y - n.y;
            const float orr = kind.radius + NPC_KINDS[o.kind].radius + 0.1f;
            const bool own_follower = o.leader >= 0 && &npcs[o.leader] == &n;
            if (&o != &n && !own_follower && ox * ox + oy * oy < orr * orr && (ox * dx + oy * dy) > 0.0f) {
                blocked = true;
            }
        }
        if (dist < 0.1f) {
            n.wait = 2.0f + SDL_randf() * 3.0f;
        } else if (blocked) {
            /* Someone is in the way: wait a moment, then try somewhere else. */
            n.wait = 0.8f;
        } else {
            const float step = SDL_min(dist, kind.speed * dt);
            const float ox = n.x;
            const float oy = n.y;
            n.x += dx / dist * step;
            n.y += dy / dist * step;
            collide_world(&n.x, &n.y, kind.radius);
            n.facing = world_to_facing(dx, dy);
            n.moving = true;
            /* Stuck against a pillar: give up on this spot. */
            if (SDL_fabsf(n.x - ox) + SDL_fabsf(n.y - oy) < step * 0.2f) {
                n.wait = 0.5f;
            }
        }
    }
    if (n.moving != was_moving) {
        n.anim_t = 0.0f;
    }
    n.anim_t += dt;
}

void Game_Update(float dt)
{
    dt = SDL_min(dt, 0.05f);
    banner_t = SDL_max(0.0f, banner_t - dt);
    if (fade_state == 1) {
        fade = SDL_min(1.0f, fade + dt * 4.0f);
        if (fade >= 1.0f) {
            const MapDoor door = pending_door;
            if (!enter_map(door.map, door.dest.c_str())) {
                SDL_Log("Door to %s (%s) failed", door.map.c_str(), door.dest.c_str());
            }
            fade_state = 2;
        }
    } else if (fade_state == 2) {
        fade = SDL_max(0.0f, fade - dt * 4.0f);
        if (fade <= 0.0f) {
            fade_state = 0;
        }
    }
    const bool *keys = SDL_GetKeyboardState(nullptr);
    const bool frozen = fade_state != 0;
    const float sx = frozen ? 0.0f : (float)(Settings_ActionHeld(keys, ACTION_RIGHT) - Settings_ActionHeld(keys, ACTION_LEFT));
    const float sy = frozen ? 0.0f : (float)(Settings_ActionHeld(keys, ACTION_DOWN) - Settings_ActionHeld(keys, ACTION_UP));

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
    if (player.moving != player.anim_moving) {
        player.anim_moving = player.moving;
        player.anim_t = 0.0f;
    }
    player.anim_t += dt;
    player.dash_cd = SDL_max(0.0f, player.dash_cd - dt);
    for (Npc &n : npcs) {
        update_npc(n, dt);
    }
    resolve_collisions();

    if (Map_Loaded() && fade_state == 0) {
        const MapDoor *door = nullptr;
        const bool inside = player_in_door(&door);
        if (inside && !door_lock) {
            pending_door = *door;
            fade_state = 1;
        }
        door_lock = door_lock && inside;
    }

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
    /* Whole sprite pixels when settled; in between zoom steps it scales smoothly. */
    const float s = P * 0.5f;
    return SDL_max(1.0f, SDL_fabsf(s - SDL_roundf(s)) < 0.01f ? SDL_roundf(s) : s);
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

/* Pack rows: DOWN, DOWN-R, RIGHT, UP-R, UP, UP-L, LEFT, DOWN-L (SPRITE_DIR_NAMES). */
static const int ROW_OF_FACING[8] = {0, 4, 6, 2, 7, 1, 5, 3};

/* A sprite pack as the game uses it: only the idle and moving sheets, as textures. */
struct Sprites {
    SpritePack pack;
    std::vector<SDL_Texture *> tex; /* one per pack action */
    int act_idle = -1;
    int act_move = -1;
};

static Sprites player_sprites;
static std::vector<Sprites> npc_sprites;

static void sprites_free(Sprites &sp)
{
    for (SDL_Texture *tex : sp.tex) {
        if (tex) {
            SDL_DestroyTexture(tex);
        }
    }
    sp.tex.clear();
    sp.act_idle = sp.act_move = -1;
}

/* The moving action is "run" (or "walk"); NPCs prefer "walk". */
static void sprites_load(Sprites &sp, SDL_Renderer *renderer, const char *dir, bool prefer_walk = false)
{
    sprites_free(sp);
    /* Packs can be large (HD packs are hundreds of MB in total); load only what the game draws. */
    if (!SpritePack_Load(&sp.pack, dir, false)) {
        return;
    }
    sp.act_idle = SpritePack_Find(&sp.pack, "idle");
    sp.act_move = SpritePack_Find(&sp.pack, prefer_walk ? "walk" : "run");
    if (sp.act_move < 0) {
        sp.act_move = SpritePack_Find(&sp.pack, prefer_walk ? "run" : "walk");
    }
    if (sp.act_idle < 0) {
        sp.act_idle = sp.act_move;
    }
    if (sp.act_move < 0) {
        sp.act_move = sp.act_idle;
    }
    sp.tex.assign(sp.pack.actions.size(), nullptr);
    for (int act : {sp.act_idle, sp.act_move}) {
        if (act < 0 || sp.tex[act] || !SpritePack_LoadSheet(&sp.pack, act)) {
            continue;
        }
        const SpriteAction &action = sp.pack.actions[act];
        SDL_Texture *tex = SDL_CreateTextureFromSurface(renderer, action.sheet);
        if (tex) {
            /* High-res packs get resampled at other resolutions; keep them smooth. */
            SDL_SetTextureScaleMode(tex, sp.pack.scale > 1 ? SDL_SCALEMODE_LINEAR : SDL_SCALEMODE_NEAREST);
            SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
        } else {
            SDL_Log("Couldn't create texture for action %s (%dx%d): %s",
                    action.name.c_str(), action.sheet->w, action.sheet->h, SDL_GetError());
        }
        sp.tex[act] = tex;
        SpritePack_FreeSheet(&sp.pack, act);
    }
}

/* A pack folder, or its hd/ variant when the normal-quality graphics are on and it has one. */
static std::string pack_dir(const std::string &dir)
{
    if (!settings.pixel_art) {
        SDL_PathInfo info;
        const std::string hd = dir + "/hd";
        if (SDL_GetPathInfo((hd + "/character.ini").c_str(), &info) && info.type == SDL_PATHTYPE_FILE) {
            return hd;
        }
    }
    return dir;
}

void Game_LoadSprites(SDL_Renderer *renderer, const char *sprites_dir)
{
    find_custom_characters();
    npc_sprites.resize(NPC_KINDS.size());
    const std::string root = sprites_dir;
    map_renderer = renderer;
    maps_dir = root + "/../maps/";
    cur_map = START_MAP;
    Map_Load(renderer, (maps_dir + cur_map).c_str(), settings.pixel_art);
    sprites_load(player_sprites, renderer, pack_dir(root + "/" + playable().pack).c_str());
    for (int k = 0; k < (int)NPC_KINDS.size(); ++k) {
        sprites_load(npc_sprites[k], renderer, pack_dir(root + "/" + NPC_KINDS[k].pack).c_str(), true);
    }
}

int Game_PlayerCount(void)
{
    find_custom_characters();
    return (int)PLAYABLE.size();
}

const char *Game_PlayerName(int index)
{
    find_custom_characters();
    return PLAYABLE[SDL_clamp(index, 0, (int)PLAYABLE.size() - 1)].name;
}

static Sprites preview_sprites;
static std::string preview_dir;

void Game_DrawPlayerPreview(SDL_Renderer *renderer, const char *sprites_dir, int index, float cx, float feet_y,
                            float height, float t)
{
    find_custom_characters();
    const std::string dir = pack_dir(std::string(sprites_dir) + "/" + PLAYABLE[SDL_clamp(index, 0, (int)PLAYABLE.size() - 1)].pack);
    if (dir != preview_dir) {
        sprites_load(preview_sprites, renderer, dir.c_str());
        preview_dir = dir;
    }
    const int act = preview_sprites.act_idle;
    if (act < 0 || !preview_sprites.tex[act]) {
        return;
    }
    const SpriteAction &action = preview_sprites.pack.actions[act];
    const int step = (int)(t * (float)action.fps);
    const int col = action.loop ? step % action.frames : SDL_min(step, action.frames - 1);
    /* Turns slowly through the eight directions, starting facing the camera. */
    const int row = (int)(t * 0.8f) % SPRITE_DIRS;
    const float cw = (float)preview_sprites.pack.cell_w;
    const float ch = (float)preview_sprites.pack.cell_h;
    const float s = height / ch;
    const SDL_FRect src = {col * cw, row * ch, cw, ch};
    const SDL_FRect dst = {cx - cw * 0.5f * s, feet_y - preview_sprites.pack.feet * s, cw * s, ch * s};
    SDL_RenderTexture(renderer, preview_sprites.tex[act], &src, &dst);
}

static bool draw_sheet_sprite(const Sprites &sp, float fx, float fy, Facing facing, float anim_t, bool moving,
                              bool ghost, Uint8 ghost_alpha)
{
    const int act = moving ? sp.act_move : sp.act_idle;
    if (act < 0 || !sp.tex[act]) {
        return false;
    }
    SDL_Texture *tex = sp.tex[act];
    const SpriteAction &action = sp.pack.actions[act];
    const int step = (int)(anim_t * (float)action.fps);
    const int col = action.loop ? step % action.frames : SDL_min(step, action.frames - 1);
    const int row = ROW_OF_FACING[facing];
    const float cw = (float)sp.pack.cell_w;
    const float ch = (float)sp.pack.cell_h;

    const float s = char_scale() / (float)sp.pack.scale;
    const SDL_FRect src = {col * cw, row * ch, cw, ch};
    const SDL_FRect dst = {fx - cw * 0.5f * s, fy - sp.pack.feet * s, cw * s, ch * s};
    if (ghost) {
        SDL_SetTextureColorMod(tex, 255, 40, 40);
        SDL_SetTextureAlphaMod(tex, ghost_alpha);
    }
    SDL_RenderTexture(rend, tex, &src, &dst);
    if (ghost) {
        SDL_SetTextureColorMod(tex, 255, 255, 255);
        SDL_SetTextureAlphaMod(tex, 255);
    }
    return true;
}

static void draw_npc(const Npc &n)
{
    const float s = char_scale();
    const SDL_FPoint f = project(n.x, n.y);
    const float fx = SDL_roundf(f.x / s) * s;
    const float fy = SDL_roundf(f.y / s) * s;
    draw_shadow(fx, fy);
    draw_sheet_sprite(npc_sprites[n.kind], fx, fy, n.facing, n.anim_t, n.moving, false, 0);
}

static void draw_player(void)
{
    const float s = char_scale();
    const SDL_FPoint f = project(player.x, player.y);
    const float fx = SDL_roundf(f.x / s) * s;
    const float fy = SDL_roundf(f.y / s) * s;
    const int frame = player.moving ? ((int)(player.walk_t * 12.0f)) % 8 : -1;
    draw_shadow(fx, fy);
    if (!draw_sheet_sprite(player_sprites, fx, fy, player.facing, player.anim_t, player.moving, false, 0)) {
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
        if (!draw_sheet_sprite(player_sprites, fx, fy, g.facing, 0.0f, false, true, a)) {
            draw_character(fx, fy, g.facing, 2, true, a);
        }
    }
}

static void draw_hud(int w, int h)
{
    const float scale = SDL_max(1.0f, (float)h / 360.0f);
    SDL_SetRenderScale(rend, scale, scale);

    char line[96];
    SDL_SetRenderDrawColor(rend, 220, 28, 28, SDL_ALPHA_OPAQUE);
    std::snprintf(line, sizeof(line), "DIFICULTAD: %s", difficulty ? difficulty : "-");
    SDL_RenderDebugText(rend, 8, 8, line);

    /* HP bar (pixel HUD, sizes in px at logical UI scale). */
    const float hp_w = 120.0f;
    const float hp_h = 8.0f;
    const float hp_x = 8.0f;
    const float hp_y = 24.0f;
    SDL_SetRenderDrawColor(rend, 40, 40, 40, SDL_ALPHA_OPAQUE);
    SDL_FRect hp_bg = {hp_x, hp_y, hp_w, hp_h};
    SDL_RenderFillRect(rend, &hp_bg);
    SDL_SetRenderDrawColor(rend, 200, 40, 50, SDL_ALPHA_OPAQUE);
    SDL_FRect hp_fg = {hp_x, hp_y, hp_w * (player_hp / PLAYER_MAX_HP), hp_h};
    SDL_RenderFillRect(rend, &hp_fg);
    SDL_SetRenderDrawColor(rend, 255, 255, 255, SDL_ALPHA_OPAQUE);
    std::snprintf(line, sizeof(line), "HP %d/%d", (int)player_hp, (int)PLAYER_MAX_HP);
    SDL_RenderDebugText(rend, hp_x, hp_y + 10.0f, line);

    /* Dash cooldown bar */
    const float dash_ready = 1.0f - (player.dash_cd / DASH_COOLDOWN);
    SDL_SetRenderDrawColor(rend, 40, 40, 40, SDL_ALPHA_OPAQUE);
    SDL_FRect dash_bg = {hp_x, hp_y + 24.0f, hp_w, 5.0f};
    SDL_RenderFillRect(rend, &dash_bg);
    SDL_SetRenderDrawColor(rend, 80, 180, 220, SDL_ALPHA_OPAQUE);
    SDL_FRect dash_fg = {hp_x, hp_y + 24.0f, hp_w * SDL_clamp(dash_ready, 0.0f, 1.0f), 5.0f};
    SDL_RenderFillRect(rend, &dash_fg);
    SDL_SetRenderDrawColor(rend, 200, 200, 200, SDL_ALPHA_OPAQUE);
    SDL_RenderDebugText(rend, hp_x, hp_y + 32.0f, "DASH");

    SDL_SetRenderScale(rend, 1.0f, 1.0f);

    char hint[128];
    std::snprintf(
        hint,
        sizeof(hint),
        "%s/%s/%s/%s=Mover  %s=Dash  ESC=Menu",
        Settings_KeyName(settings.bindings[ACTION_UP][0]),
        Settings_KeyName(settings.bindings[ACTION_LEFT][0]),
        Settings_KeyName(settings.bindings[ACTION_DOWN][0]),
        Settings_KeyName(settings.bindings[ACTION_RIGHT][0]),
        Settings_KeyName(settings.bindings[ACTION_DASH][0]));
    SDL_SetRenderDrawColor(rend, 170, 170, 170, SDL_ALPHA_OPAQUE);
    SDL_RenderDebugText(rend, (w - (float)SDL_strlen(hint) * 8.0f) * 0.5f, (float)h - 28.0f, hint);
}

void Game_Render(SDL_Renderer *renderer, int w, int h)
{
    rend = renderer;
    const float base_p = SDL_max(1.0f, SDL_floorf((float)h / 180.0f));
    const int min_step = -(int)SDL_floorf(base_p * 0.25f);
    const int max_step = (int)SDL_floorf(base_p * 0.5f);
    zoom_step = SDL_clamp(zoom_step, min_step, SDL_max(1, max_step));
    const float target_p = base_p + 2.0f * (float)zoom_step;
    const Uint64 now = SDL_GetTicks();
    static Uint64 last_zoom_ticks = 0;
    const float dt = last_zoom_ticks ? SDL_min((float)(now - last_zoom_ticks) / 1000.0f, 0.1f) : 1.0f;
    last_zoom_ticks = now;
    if (zoom_p <= 0.0f) {
        zoom_p = target_p;
    }
    zoom_p += (target_p - zoom_p) * SDL_min(1.0f, dt * 14.0f);
    if (SDL_fabsf(target_p - zoom_p) < 0.02f) {
        zoom_p = target_p;
    }
    P = zoom_p;
    TW = 28.0f * P;
    TH = TW * 0.5f;

    origin_x = 0.0f;
    origin_y = 0.0f;
    const SDL_FPoint cam = project(player.x, player.y);
    origin_x = SDL_roundf((float)w * 0.5f - cam.x);
    origin_y = SDL_roundf((float)h * 0.58f - cam.y);

    const MapView view = {origin_x, origin_y, TW, TH, char_scale(), w, h};
    if (Map_Loaded()) {
        const SDL_Color bg = Map_Background();
        SDL_SetRenderDrawColor(rend, bg.r, bg.g, bg.b, SDL_ALPHA_OPAQUE);
    } else {
        SDL_SetRenderDrawColor(rend, 10, 6, 12, SDL_ALPHA_OPAQUE);
    }
    SDL_RenderClear(rend);
    SDL_SetRenderDrawBlendMode(rend, SDL_BLENDMODE_BLEND);

    if (Map_Loaded()) {
        Map_DrawGround(rend, view);
    } else {
        draw_floor();
    }

    /* Depth sort by x + y of each thing's centre; map objects have square footprints, for
       which the centre's key orders them right against anyone walking around them. */
    struct SortItem {
        float key;
        int type; /* 0 pillar, 1 player, 2 npc, 3 map object */
        int index;
    };
    std::vector<SortItem> items;
    if (Map_Loaded()) {
        const std::vector<MapObject> &objects = Map_Objects();
        for (int k = 0; k < (int)objects.size(); ++k) {
            if (!objects[k].flat && !objects[k].wall) {
                const MapObject &o = objects[k];
                items.push_back(SortItem{o.x - o.w * 0.5f + o.y - o.d * 0.5f, 3, k});
            }
        }
    } else {
        for (int k = 0; k < PILLAR_COUNT; ++k) {
            items.push_back(SortItem{(float)(PILLARS[k].i + PILLARS[k].j) + 1.0f, 0, k});
        }
    }
    items.push_back(SortItem{player.x + player.y, 1, 0});
    for (int k = 0; k < (int)npcs.size(); ++k) {
        if (npc_sprites[npcs[k].kind].act_idle >= 0) {
            items.push_back(SortItem{npcs[k].x + npcs[k].y, 2, k});
        }
    }
    std::sort(items.begin(), items.end(), [](const SortItem &a, const SortItem &b) {
        return a.key < b.key;
    });

    /* X-ray oval around the player's body, as in POSTAL: objects drawn after the player
       (in front of them) show the player through it. */
    const float unit = char_scale();
    const SDL_FPoint feet = project(player.x, player.y);
    const float xray_x = SDL_roundf(feet.x / unit) * unit;
    const float xray_y = SDL_roundf(feet.y / unit) * unit - 24.0f * unit;
    bool player_drawn = false;
    bool ghosts_drawn = false;
    for (const SortItem &it : items) {
        if (it.type == 0) {
            draw_pillar(PILLARS[it.index]);
        } else if (it.type == 3) {
            const MapObject &o = Map_Objects()[(size_t)it.index];
            if (player_drawn) {
                Map_DrawObjectXRay(rend, view, o, xray_x, xray_y, 20.0f * unit, 32.0f * unit);
            } else {
                Map_DrawObject(rend, view, o);
            }
        } else if (it.type == 2) {
            draw_npc(npcs[it.index]);
        } else {
            if (!ghosts_drawn) {
                draw_ghosts();
                ghosts_drawn = true;
            }
            draw_player();
            player_drawn = true;
        }
    }
    if (!ghosts_drawn) {
        draw_ghosts();
    }

    draw_hud(w, h);
    if (Map_Loaded() && banner_t > 0.0f) {
        /* Name of the place on arrival. */
        const float scale = SDL_max(2.0f, SDL_floorf((float)h / 240.0f));
        const char *name = Map_Name().c_str();
        const float tw = (float)SDL_strlen(name) * 8.0f * scale;
        const Uint8 a = (Uint8)(255.0f * SDL_min(1.0f, banner_t / 0.6f));
        SDL_SetRenderScale(rend, scale, scale);
        SDL_SetRenderDrawColor(rend, 0, 0, 0, (Uint8)(a * 0.6f));
        SDL_FRect bar = {((float)w - tw) * 0.5f / scale - 8.0f, (float)h * 0.16f / scale - 6.0f, tw / scale + 16.0f, 20.0f};
        SDL_RenderFillRect(rend, &bar);
        SDL_SetRenderDrawColor(rend, 230, 200, 120, a);
        SDL_RenderDebugText(rend, ((float)w - tw) * 0.5f / scale, (float)h * 0.16f / scale, name);
        SDL_SetRenderScale(rend, 1.0f, 1.0f);
    }
    if (fade > 0.0f) {
        SDL_SetRenderDrawColor(rend, 0, 0, 0, (Uint8)(255.0f * fade));
        SDL_FRect all = {0, 0, (float)w, (float)h};
        SDL_RenderFillRect(rend, &all);
    }
    SDL_SetRenderDrawBlendMode(rend, SDL_BLENDMODE_NONE);
}
