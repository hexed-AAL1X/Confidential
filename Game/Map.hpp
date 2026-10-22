#pragma once
#include <SDL3/SDL.h>

#include <string>
#include <vector>

/*
 * Isometric maps made in Tiled (https://www.mapeditor.org), saved as JSON: a .tmj map with
 * external .tsj tilesets. World coordinates match the game's: tile (i, j) covers
 * [i, i+1] x [j, j+1], and Tiled's isometric object positions are converted from pixels by
 * dividing by the map's tile height.
 *
 * What the map can hold:
 *  - Tile layers, drawn bottom to top. A ground tileset (one atlas image) may have bigger
 *    tiles than the grid, placed with Tiled's rule (bottom-left on the cell, plus the
 *    tileset's tileoffset), animations, and the bool property "transitable" (default true).
 *    A cell is walkable when its topmost tile is.
 *    A cell with no tile in any layer can't be walked on either (the void around a room).
 *  - Object layers with tile objects from an image-collection tileset, anchored at their
 *    bottom-centre (the south corner of their footprint). Tile properties: "ancho" and
 *    "fondo" (footprint along x and y, in tiles; or "pie" for a square), "solido" (blocks
 *    walking), "plano" (lies on the ground, drawn under everything else), "pared" (a back
 *    wall of a room, drawn behind everyone).
 *  - Point objects, by name and type ("jugador", "llegada", "npc"...).
 *  - Doors: rectangles of type "puerta" with the properties "mapa" (another .tmj next to
 *    this one) and "destino" (the name of the point to arrive at there).
 *  - Map properties: "nombre" (shown on arrival), "extender_borde" (repeat the bottom layer
 *    past the edges, e.g. the sea), "poblar" (scatter the cast around) and "excluir" (comma
 *    list of NPC packs that live in other maps, left out of the scattering).
 *
 * Images are referenced at HD scale (pixel scale 3). With pixelated graphics the loader uses
 * the same path with hd/ replaced by px/ when that file exists, at pixel scale 1.
 */

struct MapView {
    float origin_x;
    float origin_y;
    float tw;        /* screen width of a tile */
    float th;        /* screen height of a tile */
    float unit;      /* screen pixels per sprite pixel (char_scale) */
    int screen_w;
    int screen_h;
};

struct MapObject {
    float x; /* south corner of the footprint, world units */
    float y;
    float w; /* footprint along x and y */
    float d;
    bool solid;
    bool flat;
    bool wall;
    int image; /* index into the map's object images */
};

struct MapPoint {
    std::string name;
    std::string type;
    float x;
    float y;
};

struct MapBox {
    float x0, y0, x1, y1;
};

struct MapDoor {
    MapBox area;
    std::string map;  /* file next to the current map */
    std::string dest; /* point name in that map */
};

bool Map_Load(SDL_Renderer *renderer, const char *tmj_path, bool pixel_art);
void Map_Unload(void);
bool Map_Loaded(void);
int Map_Width(void);
int Map_Height(void);
bool Map_Walkable(int i, int j);
const std::vector<MapObject> &Map_Objects(void);
const std::vector<MapPoint> &Map_Points(void);
const std::vector<MapDoor> &Map_Doors(void);
/* The map's "nombre" property, or the file name. */
const std::string &Map_Name(void);
/* Whether the NPC pack is listed in the map's "excluir" property. */
bool Map_Excludes(const char *pack);
/* The map's "poblar" property: fill it with the cast at random spots (besides its "npc"
   points), like the island's exterior. */
bool Map_Populates(void);
/* The map's "musica" property: a file in assets/music to loop while on this map. */
const std::string &Map_Music(void);
/* Footprints of the solid objects. */
const std::vector<MapBox> &Map_SolidBoxes(void);
/* Fill colour for the screen (the map's background colour). */
SDL_Color Map_Background(void);

/* Every tile layer, then the flat objects. Cells outside the map repeat the bottom layer's
   nearest edge cell, so the sea goes on past the map. */
void Map_DrawGround(SDL_Renderer *renderer, const MapView &view);
void Map_DrawObject(SDL_Renderer *renderer, const MapView &view, const MapObject &object);
/* POSTAL's x-ray: the object drawn with a see-through oval centred at (cx, cy) with radii
   (rx, ry) in screen pixels, so whoever stands behind it shows through. */
void Map_DrawObjectXRay(SDL_Renderer *renderer, const MapView &view, const MapObject &object, float cx, float cy,
                        float rx, float ry);
