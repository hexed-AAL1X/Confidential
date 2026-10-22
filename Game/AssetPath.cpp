#include "AssetPath.hpp"

#include <SDL3/SDL.h>

#include <string>

static const int MAX_LEVELS_UP = 3;

static bool has_assets(const std::string &dir)
{
    SDL_PathInfo info;
    return SDL_GetPathInfo((dir + "assets").c_str(), &info) && info.type == SDL_PATHTYPE_DIRECTORY;
}

const char *Asset_Root(void)
{
    static std::string root;
    if (!root.empty()) {
        return root.c_str();
    }
    const char *base = SDL_GetBasePath();
    const std::string start = base ? base : "./";
    std::string dir = start;
    for (int up = 0; up <= MAX_LEVELS_UP; ++up) {
        if (has_assets(dir)) {
            root = dir;
            return root.c_str();
        }
        if (dir.size() < 2) {
            break;
        }
        const size_t cut = dir.find_last_of("/\\", dir.size() - 2);
        if (cut == std::string::npos) {
            break;
        }
        dir.resize(cut + 1);
    }
    SDL_Log("assets/ not found near %s", start.c_str());
    root = start;
    return root.c_str();
}
