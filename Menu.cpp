#define SDL_MAIN_USE_CALLBACKS 1  /* use the callbacks instead of main() */
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

/* We will use this renderer to draw into this window every frame. */
static SDL_Window *window = NULL;
static SDL_Renderer *renderer = NULL;

int option = 1;
//fade out
static bool is_exiting = false;
static Uint64 fade_out_start_ticks = 0;
static const Uint64 FADE_OUT_DURATION = 1500; // Duración del desvanecimiento al salir (1.5s)
#define WINDOW_WIDTH 640
#define WINDOW_HEIGHT 480

/* This function runs once at startup. */
SDL_AppResult SDL_AppInit(void **appstate, int argc, char *argv[])
{


    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("Couldn't initialize SDL: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    if (!SDL_CreateWindowAndRenderer("examples/renderer/affine-textures", WINDOW_WIDTH, WINDOW_HEIGHT, SDL_WINDOW_RESIZABLE, &window, &renderer)) {
        SDL_Log("Couldn't create window/renderer: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }
    SDL_SetRenderLogicalPresentation(renderer, WINDOW_WIDTH, WINDOW_HEIGHT, SDL_LOGICAL_PRESENTATION_LETTERBOX);

    return SDL_APP_CONTINUE;
}

/* This function runs when a new event (mouse input, keypresses, etc) occurs. */
SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event)
{
    const bool* keystate = SDL_GetKeyboardState(nullptr);
    if (event->type == SDL_EVENT_QUIT) {
        return SDL_APP_SUCCESS;  /* end the program, reporting success to the OS. */
    }

    if (event->type == SDL_EVENT_KEY_DOWN && !is_exiting) {
        is_exiting = true;
        fade_out_start_ticks = SDL_GetTicks();
    }

    else if(event->type == SDL_EVENT_KEY_DOWN){
        if(keystate[SDL_SCANCODE_DOWN] && option == 1){
            option = 0;
            SDL_Log("%d",option);
        }
        else if(keystate[SDL_SCANCODE_UP] && option == 0){
            option = 1;
            SDL_Log("%d",option);
        }
        else if(keystate[SDL_SCANCODE_SPACE]){
            if(option == 1) {SDL_Log("Resident Evil 4");}
            if(option == 0) {return SDL_APP_SUCCESS;}
        }

    }
    return SDL_APP_CONTINUE;  /* carry on with the program! */
}

/* This function runs once per frame, and is the heart of the program. */
SDL_AppResult SDL_AppIterate(void *appstate)
{
    //Fondo
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, SDL_ALPHA_OPAQUE);
    SDL_RenderClear(renderer);
    //Letras en blanco
    float scale = 3.0f;
    SDL_SetRenderScale(renderer, scale, scale);
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, SDL_ALPHA_OPAQUE);

    float x = (0.5f * WINDOW_WIDTH) / scale;
    float y = (0.25f * WINDOW_HEIGHT) / scale;

    SDL_RenderDebugText(renderer, x, y, "The island");
    y = (0.75f * WINDOW_HEIGHT) / scale;
    SDL_RenderDebugText(renderer, x, y, "Start");
    y = (0.90f * WINDOW_HEIGHT) / scale;
    SDL_RenderDebugText(renderer, x, y, "Quit");

    if (option == 1){
        x = (0.25f * WINDOW_WIDTH) / scale;
        y = (0.75f * WINDOW_HEIGHT) / scale;
    } else if (option == 0){
        x = (0.25f * WINDOW_WIDTH) / scale;
        y = (0.90f * WINDOW_HEIGHT) / scale;
    }

    SDL_RenderDebugText(renderer, x, y, ">");
    SDL_SetRenderScale(renderer, 1.0f, 1.0f);



    // Presentar el resultado en pantalla
    SDL_RenderPresent(renderer);

    return SDL_APP_CONTINUE;
}

/* This function runs once at shutdown. */
void SDL_AppQuit(void *appstate, SDL_AppResult result){

}
