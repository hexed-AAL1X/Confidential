#include "Map.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <memory>

/* ------------------------------------------------------------------ minimal JSON reader */

namespace {

struct Json {
    enum Type { NUL, BOOL, NUM, STR, ARR, OBJ } type = NUL;
    bool b = false;
    double num = 0.0;
    std::string str;
    std::vector<Json> arr;
    std::vector<std::pair<std::string, Json>> obj;

    const Json &operator[](const char *key) const
    {
        static const Json none;
        for (const auto &kv : obj) {
            if (kv.first == key) {
                return kv.second;
            }
        }
        return none;
    }
    double number(double fallback = 0.0) const { return type == NUM ? num : (type == BOOL ? (b ? 1.0 : 0.0) : fallback); }
    int integer(int fallback = 0) const { return type == NUM ? (int)num : fallback; }
    const std::string &string(void) const { return str; }
    bool truthy(bool fallback = false) const { return type == BOOL ? b : (type == NUM ? num != 0.0 : fallback); }
};

struct JsonParser {
    const char *p;
    const char *end;
    bool ok = true;

    void ws(void)
    {
        while (p < end && (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t')) {
            ++p;
        }
    }

    bool lit(const char *s)
    {
        const size_t n = std::strlen(s);
        if ((size_t)(end - p) >= n && std::memcmp(p, s, n) == 0) {
            p += n;
            return true;
        }
        return false;
    }

    void str(std::string &out)
    {
        ++p; /* opening quote */
        while (p < end && *p != '"') {
            if (*p == '\\' && p + 1 < end) {
                ++p;
                switch (*p) {
                    case 'n': out += '\n'; break;
                    case 't': out += '\t'; break;
                    case 'r': out += '\r'; break;
                    case 'b': out += '\b'; break;
                    case 'f': out += '\f'; break;
                    case 'u': {
                        unsigned code = 0;
                        for (int i = 0; i < 4 && p + 1 < end; ++i) {
                            const char c = *++p;
                            code = code * 16 + (unsigned)(c <= '9' ? c - '0' : (c | 0x20) - 'a' + 10);
                        }
                        if (code < 0x80) {
                            out += (char)code;
                        } else if (code < 0x800) {
                            out += (char)(0xC0 | (code >> 6));
                            out += (char)(0x80 | (code & 0x3F));
                        } else {
                            out += (char)(0xE0 | (code >> 12));
                            out += (char)(0x80 | ((code >> 6) & 0x3F));
                            out += (char)(0x80 | (code & 0x3F));
                        }
                        break;
                    }
                    default: out += *p; break;
                }
                ++p;
            } else {
                out += *p++;
            }
        }
        if (p < end) {
            ++p;
        } else {
            ok = false;
        }
    }

    Json value(void)
    {
        Json v;
        ws();
        if (p >= end) {
            ok = false;
            return v;
        }
        if (*p == '{') {
            v.type = Json::OBJ;
            ++p;
            ws();
            if (p < end && *p == '}') {
                ++p;
                return v;
            }
            while (ok) {
                ws();
                if (p >= end || *p != '"') {
                    ok = false;
                    break;
                }
                std::string key;
                str(key);
                ws();
                if (p >= end || *p != ':') {
                    ok = false;
                    break;
                }
                ++p;
                v.obj.emplace_back(std::move(key), value());
                ws();
                if (p < end && *p == ',') {
                    ++p;
                } else if (p < end && *p == '}') {
                    ++p;
                    break;
                } else {
                    ok = false;
                }
            }
        } else if (*p == '[') {
            v.type = Json::ARR;
            ++p;
            ws();
            if (p < end && *p == ']') {
                ++p;
                return v;
            }
            while (ok) {
                v.arr.push_back(value());
                ws();
                if (p < end && *p == ',') {
                    ++p;
                } else if (p < end && *p == ']') {
                    ++p;
                    break;
                } else {
                    ok = false;
                }
            }
        } else if (*p == '"') {
            v.type = Json::STR;
            str(v.str);
        } else if (lit("true")) {
            v.type = Json::BOOL;
            v.b = true;
        } else if (lit("false")) {
            v.type = Json::BOOL;
        } else if (lit("null")) {
            v.type = Json::NUL;
        } else {
            char *after = nullptr;
            v.type = Json::NUM;
            v.num = std::strtod(p, &after);
            if (after == p) {
                ok = false;
            } else {
                p = after;
            }
        }
        return v;
    }
};

bool load_json(const std::string &path, Json &out)
{
    size_t size = 0;
    void *data = SDL_LoadFile(path.c_str(), &size);
    if (!data) {
        SDL_Log("Map: couldn't read %s: %s", path.c_str(), SDL_GetError());
        return false;
    }
    JsonParser parser{(const char *)data, (const char *)data + size};
    out = parser.value();
    SDL_free(data);
    if (!parser.ok) {
        SDL_Log("Map: %s is not valid JSON", path.c_str());
    }
    return parser.ok;
}

std::string dir_of(const std::string &path)
{
    const size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? std::string() : path.substr(0, slash + 1);
}

/* Tiled "properties": [{"name", "type", "value"}]. */
const Json &property(const Json &holder, const char *name)
{
    static const Json none;
    for (const Json &p : holder["properties"].arr) {
        if (p["name"].string() == name) {
            return p["value"];
        }
    }
    return none;
}

/* ------------------------------------------------------------------ map state */

struct Image {
    SDL_Texture *tex = nullptr;
    float w = 0.0f;     /* texture size in its own pixels */
    float h = 0.0f;
    float scale = 3.0f; /* pixel scale of the art: 3 HD, 1 pixelated */
};

struct TileInfo {
    bool walkable = true;
    float w = 1.0f;
    float d = 1.0f;
    bool solid = false;
    bool flat = false;
    bool wall = false;
    int image = -1;                            /* image-collection tilesets */
    std::vector<std::pair<int, int>> frames;   /* (local tile id, ms) */
    int frames_ms = 0;
};

struct Tileset {
    int firstgid = 1;
    int count = 0;
    int columns = 0;
    float tile_w = 0.0f; /* HD pixels */
    float tile_h = 0.0f;
    float offset_x = 0.0f;
    float offset_y = 0.0f;
    int atlas = -1; /* image index, -1 for an image collection */
    std::vector<TileInfo> tiles;
};

struct TileLayer {
    std::vector<int> gids;
};

struct State {
    bool loaded = false;
    int w = 0;
    int h = 0;
    float tile_h = 84.0f;
    SDL_Color background = {26, 84, 140, 255};
    std::vector<Image> images;
    std::vector<Tileset> tilesets;
    std::vector<TileLayer> layers;
    std::vector<unsigned char> walkable;
    std::vector<MapObject> objects;
    std::vector<MapPoint> points;
    std::vector<MapDoor> doors;
    std::vector<MapBox> solid;
    std::string name;
    std::string exclude; /* ",pack,pack," */
    bool extend_border = false;
    bool populate = false;
    std::string music;
};

State map;

const unsigned FLIP_MASK = 0x1FFFFFFFu; /* strips Tiled's flip and rotation bits */

int load_image(SDL_Renderer *renderer, const std::string &hd_path, bool pixel_art)
{
    std::string path = hd_path;
    float scale = 3.0f;
    if (pixel_art) {
        const size_t at = hd_path.rfind("hd/");
        if (at != std::string::npos) {
            std::string px = hd_path;
            px.replace(at, 3, "px/");
            if (SDL_GetPathInfo(px.c_str(), nullptr)) {
                path = px;
                scale = 1.0f;
            }
        }
    }
    SDL_Surface *surface = SDL_LoadPNG(path.c_str());
    if (!surface) {
        SDL_Log("Map: couldn't load %s: %s", path.c_str(), SDL_GetError());
        return -1;
    }
    Image img;
    img.tex = SDL_CreateTextureFromSurface(renderer, surface);
    img.w = (float)surface->w;
    img.h = (float)surface->h;
    img.scale = scale;
    SDL_DestroySurface(surface);
    if (!img.tex) {
        return -1;
    }
    SDL_SetTextureScaleMode(img.tex, SDL_SCALEMODE_NEAREST);
    map.images.push_back(img);
    return (int)map.images.size() - 1;
}

bool load_tileset(SDL_Renderer *renderer, const std::string &path, int firstgid, bool pixel_art)
{
    Json ts;
    if (!load_json(path, ts)) {
        return false;
    }
    const std::string dir = dir_of(path);
    Tileset set;
    set.firstgid = firstgid;
    set.count = ts["tilecount"].integer();
    set.columns = ts["columns"].integer();
    set.tile_w = (float)ts["tilewidth"].number();
    set.tile_h = (float)ts["tileheight"].number();
    set.offset_x = (float)ts["tileoffset"]["x"].number();
    set.offset_y = (float)ts["tileoffset"]["y"].number();
    set.tiles.resize((size_t)SDL_max(0, set.count));
    if (ts["image"].type == Json::STR) {
        set.atlas = load_image(renderer, dir + ts["image"].string(), pixel_art);
        if (set.atlas < 0) {
            return false;
        }
    }
    for (const Json &t : ts["tiles"].arr) {
        const int id = t["id"].integer(-1);
        if (id < 0) {
            continue;
        }
        if (id >= (int)set.tiles.size()) {
            set.tiles.resize((size_t)id + 1);
        }
        TileInfo &info = set.tiles[(size_t)id];
        info.walkable = property(t, "transitable").truthy(true);
        const float pie = (float)property(t, "pie").number(1.0);
        info.w = (float)property(t, "ancho").number(pie);
        info.d = (float)property(t, "fondo").number(pie);
        info.solid = property(t, "solido").truthy(false);
        info.flat = property(t, "plano").truthy(false);
        info.wall = property(t, "pared").truthy(false);
        if (t["image"].type == Json::STR) {
            info.image = load_image(renderer, dir + t["image"].string(), pixel_art);
        }
        for (const Json &f : t["animation"].arr) {
            info.frames.emplace_back(f["tileid"].integer(), SDL_max(1, f["duration"].integer(100)));
            info.frames_ms += info.frames.back().second;
        }
    }
    map.tilesets.push_back(std::move(set));
    return true;
}

/* Tileset and local id of a gid, or nullptr. */
const Tileset *find_tile(int gid, int *local)
{
    gid = (int)((unsigned)gid & FLIP_MASK);
    if (gid <= 0) {
        return nullptr;
    }
    const Tileset *found = nullptr;
    for (const Tileset &t : map.tilesets) {
        if (gid >= t.firstgid) {
            found = &t;
        }
    }
    if (!found) {
        return nullptr;
    }
    *local = gid - found->firstgid;
    return *local < (int)found->tiles.size() || found->atlas >= 0 ? found : nullptr;
}

const TileInfo *tile_info(const Tileset *t, int local)
{
    static const TileInfo plain;
    return local >= 0 && local < (int)t->tiles.size() ? &t->tiles[(size_t)local] : &plain;
}

SDL_Color parse_color(const std::string &s, SDL_Color fallback)
{
    /* "#rrggbb" or "#aarrggbb" */
    if (s.size() != 7 && s.size() != 9) {
        return fallback;
    }
    const unsigned v = (unsigned)std::strtoul(s.c_str() + 1, nullptr, 16);
    return SDL_Color{(Uint8)((v >> 16) & 255), (Uint8)((v >> 8) & 255), (Uint8)(v & 255), 255};
}

} /* namespace */

void Map_Unload(void)
{
    for (Image &img : map.images) {
        SDL_DestroyTexture(img.tex);
    }
    map = State{};
}

bool Map_Load(SDL_Renderer *renderer, const char *tmj_path, bool pixel_art)
{
    Map_Unload();
    Json tmj;
    if (!load_json(tmj_path, tmj)) {
        return false;
    }
    if (tmj["orientation"].string() != "isometric") {
        SDL_Log("Map: %s is not an isometric map", tmj_path);
        return false;
    }
    const std::string dir = dir_of(tmj_path);
    map.w = tmj["width"].integer();
    map.h = tmj["height"].integer();
    map.tile_h = (float)tmj["tileheight"].number(84.0);
    map.background = parse_color(tmj["backgroundcolor"].string(), map.background);
    map.name = property(tmj, "nombre").string();
    if (map.name.empty()) {
        const std::string file = tmj_path;
        map.name = file.substr(file.find_last_of("/\\") + 1);
    }
    map.extend_border = property(tmj, "extender_borde").truthy(false);
    map.populate = property(tmj, "poblar").truthy(false);
    map.music = property(tmj, "musica").string();
    map.exclude = "," + property(tmj, "excluir").string() + ",";
    for (const Json &ts : tmj["tilesets"].arr) {
        if (ts["source"].type != Json::STR) {
            SDL_Log("Map: embedded tilesets aren't supported; export them as .tsj");
            Map_Unload();
            return false;
        }
        if (!load_tileset(renderer, dir + ts["source"].string(), ts["firstgid"].integer(1), pixel_art)) {
            Map_Unload();
            return false;
        }
    }

    const size_t cells = (size_t)map.w * (size_t)map.h;
    for (const Json &layer : tmj["layers"].arr) {
        if (!layer["visible"].truthy(true)) {
            continue;
        }
        if (layer["type"].string() == "tilelayer") {
            TileLayer tl;
            tl.gids.assign(cells, 0);
            const auto &data = layer["data"].arr;
            for (size_t k = 0; k < cells && k < data.size(); ++k) {
                tl.gids[k] = (int)((unsigned)data[k].number() & FLIP_MASK);
            }
            map.layers.push_back(std::move(tl));
        } else if (layer["type"].string() == "objectgroup") {
            for (const Json &o : layer["objects"].arr) {
                const float x = (float)o["x"].number() / map.tile_h;
                const float y = (float)o["y"].number() / map.tile_h;
                const int gid = o["gid"].integer();
                if (gid > 0) {
                    int local = 0;
                    const Tileset *t = find_tile(gid, &local);
                    if (!t) {
                        continue;
                    }
                    const TileInfo *info = tile_info(t, local);
                    if (info->image < 0) {
                        continue;
                    }
                    MapObject mo{x, y, info->w, info->d, info->solid, info->flat, info->wall, info->image};
                    map.objects.push_back(mo);
                    if (mo.solid) {
                        map.solid.push_back(MapBox{x - mo.w, y - mo.d, x, y});
                    }
                } else if ((o["type"].string() == "puerta" || o["class"].string() == "puerta") && !o["point"].truthy()) {
                    const float w = (float)o["width"].number() / map.tile_h;
                    const float h = (float)o["height"].number() / map.tile_h;
                    map.doors.push_back(MapDoor{MapBox{x, y, x + w, y + h}, property(o, "mapa").string(),
                                                property(o, "destino").string()});
                } else if (o["point"].truthy()) {
                    const std::string &type = o["type"].type == Json::STR ? o["type"].string() : o["class"].string();
                    map.points.push_back(MapPoint{o["name"].string(), type, x, y});
                }
            }
        }
    }

    map.walkable.assign(cells, 0);
    for (size_t k = 0; k < cells; ++k) {
        for (auto it = map.layers.rbegin(); it != map.layers.rend(); ++it) {
            int local = 0;
            const Tileset *t = find_tile(it->gids[k], &local);
            if (t) {
                map.walkable[k] = tile_info(t, local)->walkable ? 1 : 0;
                break;
            }
        }
    }
    map.loaded = true;
    SDL_Log("Map: %s, %dx%d, %d layers, %d objects", tmj_path, map.w, map.h, (int)map.layers.size(),
            (int)map.objects.size());
    return true;
}

bool Map_Loaded(void) { return map.loaded; }
int Map_Width(void) { return map.w; }
int Map_Height(void) { return map.h; }
const std::vector<MapObject> &Map_Objects(void) { return map.objects; }
const std::vector<MapPoint> &Map_Points(void) { return map.points; }
const std::vector<MapBox> &Map_SolidBoxes(void) { return map.solid; }
const std::vector<MapDoor> &Map_Doors(void) { return map.doors; }
const std::string &Map_Name(void) { return map.name; }

bool Map_Populates(void) { return map.populate; }
const std::string &Map_Music(void) { return map.music; }

bool Map_Excludes(const char *pack)
{
    return map.exclude.find(std::string(",") + pack + ",") != std::string::npos;
}
SDL_Color Map_Background(void) { return map.background; }

bool Map_Walkable(int i, int j)
{
    if (!map.loaded || i < 0 || j < 0 || i >= map.w || j >= map.h) {
        return false;
    }
    return map.walkable[(size_t)j * (size_t)map.w + (size_t)i] != 0;
}

static SDL_FPoint view_project(const MapView &v, float x, float y)
{
    return SDL_FPoint{v.origin_x + (x - y) * v.tw * 0.5f, v.origin_y + (x + y) * v.th * 0.5f};
}

static void draw_cell(SDL_Renderer *renderer, const MapView &view, int gid, int i, int j, Uint64 ms)
{
    int local = 0;
    const Tileset *t = find_tile(gid, &local);
    if (!t || t->atlas < 0 || t->columns <= 0) {
        return;
    }
    const TileInfo *info = tile_info(t, local);
    if (!info->frames.empty()) {
        int at = (int)(ms % (Uint64)info->frames_ms);
        for (const auto &f : info->frames) {
            if (at < f.second) {
                local = f.first;
                break;
            }
            at -= f.second;
        }
    }
    const Image &img = map.images[(size_t)t->atlas];
    const float k = img.scale / 3.0f;           /* atlas pixels per HD pixel */
    const float cw = t->tile_w * k;
    const float ch = t->tile_h * k;
    const SDL_FRect src = {(float)(local % t->columns) * cw, (float)(local / t->columns) * ch, cw, ch};
    const float hd = view.unit / 3.0f;          /* screen pixels per HD pixel */
    const float left = view_project(view, (float)i, (float)(j + 1)).x;
    const float bottom = view_project(view, (float)(i + 1), (float)(j + 1)).y;
    const SDL_FRect dst = {left + t->offset_x * hd, bottom + t->offset_y * hd - t->tile_h * hd, t->tile_w * hd,
                           t->tile_h * hd};
    SDL_RenderTexture(renderer, img.tex, &src, &dst);
}

void Map_DrawGround(SDL_Renderer *renderer, const MapView &view)
{
    if (!map.loaded || map.layers.empty()) {
        return;
    }
    /* World rectangle under the screen: invert the projection at the corners. */
    float i0 = 1e9f, i1 = -1e9f, j0 = 1e9f, j1 = -1e9f;
    const float corners[4][2] = {{0, 0}, {(float)view.screen_w, 0}, {0, (float)view.screen_h},
                                 {(float)view.screen_w, (float)view.screen_h}};
    for (const auto &c : corners) {
        const float d = (c[0] - view.origin_x) / (view.tw * 0.5f);
        const float s = (c[1] - view.origin_y) / (view.th * 0.5f);
        const float x = (s + d) * 0.5f;
        const float y = (s - d) * 0.5f;
        i0 = SDL_min(i0, x);
        i1 = SDL_max(i1, x);
        j0 = SDL_min(j0, y);
        j1 = SDL_max(j1, y);
    }
    const int ci0 = (int)SDL_floorf(i0) - 2, ci1 = (int)SDL_ceilf(i1) + 2;
    const int cj0 = (int)SDL_floorf(j0) - 2, cj1 = (int)SDL_ceilf(j1) + 2;
    const Uint64 ms = SDL_GetTicks();
    const float pad_y = view.th * 2.0f;
    for (size_t l = 0; l < map.layers.size(); ++l) {
        const std::vector<int> &gids = map.layers[l].gids;
        for (int j = cj0; j <= cj1; ++j) {
            for (int i = ci0; i <= ci1; ++i) {
                const SDL_FPoint p = view_project(view, (float)i + 0.5f, (float)j + 0.5f);
                if (p.x < -view.tw || p.x > view.screen_w + view.tw || p.y < -pad_y || p.y > view.screen_h + pad_y) {
                    continue;
                }
                const bool inside = i >= 0 && j >= 0 && i < map.w && j < map.h;
                if (!inside && (l != 0 || !map.extend_border)) {
                    continue;
                }
                const int ci = SDL_clamp(i, 0, map.w - 1);
                const int cj = SDL_clamp(j, 0, map.h - 1);
                const int gid = gids[(size_t)cj * (size_t)map.w + (size_t)ci];
                if (gid) {
                    draw_cell(renderer, view, gid, i, j, ms);
                }
            }
        }
    }
    for (const MapObject &o : map.objects) {
        if (o.flat) {
            Map_DrawObject(renderer, view, o);
        }
    }
    for (const MapObject &o : map.objects) {
        if (o.wall) {
            Map_DrawObject(renderer, view, o);
        }
    }
}

/* Where an object lands on screen; false when it's off screen. */
static bool object_rect(const MapView &view, const MapObject &object, SDL_FRect *dst)
{
    const Image &img = map.images[(size_t)object.image];
    const float s = view.unit / img.scale; /* screen pixels per image pixel */
    const SDL_FPoint p = view_project(view, object.x, object.y);
    const float fx = SDL_roundf(p.x / view.unit) * view.unit;
    const float fy = SDL_roundf(p.y / view.unit) * view.unit;
    const float w = img.w * s;
    const float h = img.h * s;
    *dst = SDL_FRect{fx - w * 0.5f, fy - h, w, h};
    return !(fx + w * 0.5f < 0 || fx - w * 0.5f > view.screen_w || fy < 0 || fy - h > view.screen_h);
}

void Map_DrawObject(SDL_Renderer *renderer, const MapView &view, const MapObject &object)
{
    SDL_FRect dst;
    if (object_rect(view, object, &dst)) {
        SDL_RenderTexture(renderer, map.images[(size_t)object.image].tex, nullptr, &dst);
    }
}

void Map_DrawObjectXRay(SDL_Renderer *renderer, const MapView &view, const MapObject &object, float cx, float cy,
                        float rx, float ry)
{
    SDL_FRect dst;
    if (!object_rect(view, object, &dst)) {
        return;
    }
    const Image &img = map.images[(size_t)object.image];
    if (cx + rx <= dst.x || cx - rx >= dst.x + dst.w || cy + ry <= dst.y || cy - ry >= dst.y + dst.h) {
        SDL_RenderTexture(renderer, img.tex, nullptr, &dst);
        return;
    }
    const float k = img.w / dst.w; /* image pixels per screen pixel */
    auto part = [&](float x0, float x1, float y0, float y1, Uint8 alpha) {
        x0 = SDL_max(x0, dst.x);
        x1 = SDL_min(x1, dst.x + dst.w);
        y0 = SDL_max(y0, dst.y);
        y1 = SDL_min(y1, dst.y + dst.h);
        if (x1 <= x0 || y1 <= y0) {
            return;
        }
        const SDL_FRect src = {(x0 - dst.x) * k, (y0 - dst.y) * k, (x1 - x0) * k, (y1 - y0) * k};
        const SDL_FRect out = {x0, y0, x1 - x0, y1 - y0};
        SDL_SetTextureAlphaMod(img.tex, alpha);
        SDL_RenderTexture(renderer, img.tex, &src, &out);
    };
    const float left = dst.x;
    const float right = dst.x + dst.w;
    /* Above and below the oval as usual, then the rows through it in strips one sprite pixel
       tall: solid, a half-transparent rim, the clear middle, the rim, solid. Edges snap to the
       sprite pixel grid so the hole stays as crisp as the art. */
    const float step = SDL_max(1.0f, view.unit);
    const float top = cy - ry;
    const float bottom = cy + ry;
    part(left, right, dst.y, top, 255);
    for (float y = top; y < bottom; y += step) {
        const float dy = (y + step * 0.5f - cy) / ry;
        const float outer = dy * dy < 1.0f ? rx * SDL_sqrtf(1.0f - dy * dy) : 0.0f;
        const float inner = outer * 0.78f;
        auto snap = [&](float x) { return left + SDL_roundf((x - left) / step) * step; };
        const float o0 = snap(cx - outer), i0 = snap(cx - inner), i1 = snap(cx + inner), o1 = snap(cx + outer);
        part(left, o0, y, y + step, 255);
        part(o0, i0, y, y + step, 150);
        part(i0, i1, y, y + step, 70);
        part(i1, o1, y, y + step, 150);
        part(o1, right, y, y + step, 255);
    }
    part(left, right, bottom, dst.y + dst.h, 255);
    SDL_SetTextureAlphaMod(img.tex, 255);
}
