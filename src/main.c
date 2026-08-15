#define SDL_MAIN_USE_CALLBACKS
#include <SDL3/SDL_main.h>
#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <stdio.h>

#define CLAY_IMPLEMENTATION
#include "../clay/clay.h"
#include "../clay/SDL3/clay_renderer_SDL3.c"

#include "ui.h"
#include "config.h"

typedef struct {
    SDL_Window *window;
    Clay_SDL3RendererData rendererData;
    bool clicked;
} AppState;

static Clay_Dimensions SDL_MeasureText(Clay_StringSlice text, Clay_TextElementConfig *config, void *userData) {
    TTF_Font **fonts = userData;
    TTF_Font *font = fonts[config->fontId];
    int w = 0, h = 0;
    TTF_SetFontSize(font, config->fontSize);
    TTF_GetStringSize(font, text.chars, text.length, &w, &h);
    return (Clay_Dimensions){ (float)w, (float)h };
}

static void HandleClayErrors(Clay_ErrorData errorData) {
    SDL_Log("%s", errorData.errorText.chars);
}

static TTF_Font *find_font(const char *name, int size) {
    /* If it looks like an absolute path, try it directly */
    if (name[0] == '/') {
        TTF_Font *f = TTF_OpenFont(name, size);
        if (f) return f;
    }

    /* Search common font directories for <name>.ttf / <name>.otf */
    const char *dirs[] = {
        "/usr/share/fonts/TTF",
        "/usr/share/fonts/OTF",
        "/usr/share/fonts/truetype",
        "/usr/share/fonts/opentype",
        "/usr/local/share/fonts",
        NULL
    };
    const char *exts[] = { ".ttf", ".otf", NULL };
    char path[1024];
    for (int d = 0; dirs[d]; d++) {
        for (int e = 0; exts[e]; e++) {
            snprintf(path, sizeof(path), "%s/%s%s", dirs[d], name, exts[e]);
            TTF_Font *f = TTF_OpenFont(path, size);
            if (f) return f;
        }
    }

    /* Fallback fonts */
    const char *fallbacks[] = {
        "/usr/share/fonts/TTF/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/TTF/LiberationSans-Regular.ttf",
        NULL
    };
    for (int i = 0; fallbacks[i]; i++) {
        TTF_Font *f = TTF_OpenFont(fallbacks[i], size);
        if (f) return f;
    }
    return NULL;
}

SDL_AppResult SDL_AppInit(void **appstate, int argc, char *argv[]) {
    (void)argc; (void)argv;
    if (!TTF_Init()) return SDL_APP_FAILURE;

    AppState *state = SDL_calloc(1, sizeof(AppState));
    if (!state) return SDL_APP_FAILURE;
    *appstate = state;

    if (!SDL_CreateWindowAndRenderer("Launcher", 0, 0,
            SDL_WINDOW_FULLSCREEN, &state->window, &state->rendererData.renderer)) {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "%s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    state->rendererData.textEngine = TTF_CreateRendererTextEngine(state->rendererData.renderer);
    if (!state->rendererData.textEngine) return SDL_APP_FAILURE;

    state->rendererData.fonts = SDL_calloc(1, sizeof(TTF_Font *));
    if (!state->rendererData.fonts) return SDL_APP_FAILURE;

    LauncherConfig cfg;
    loadConfig(&cfg);

    TTF_Font *font = find_font(cfg.fontName, cfg.fontSize);
    if (!font) {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "No usable font found: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }
    state->rendererData.fonts[0] = font;

    int w, h;
    SDL_GetWindowSize(state->window, &w, &h);
    uint64_t memSize = Clay_MinMemorySize();
    Clay_Arena arena = { .memory = SDL_malloc(memSize), .capacity = memSize };
    Clay_Initialize(arena, (Clay_Dimensions){ (float)w, (float)h }, (Clay_ErrorHandler){ HandleClayErrors });
    Clay_SetMeasureTextFunction(SDL_MeasureText, state->rendererData.fonts);

    ui_init(state->rendererData.renderer, &cfg);

    for (int i = 0; i < cfg.orderSlotCount; i++) free(cfg.orderSlots[i]);
    free(cfg.orderSlots);
    free(cfg.pageDefs);
    for (int i = 0; i < cfg.folderDefCount; i++) {
        for (int j = 0; j < cfg.folderDefs[i].orderSlotCount; j++)
            free(cfg.folderDefs[i].orderSlots[j]);
        free(cfg.folderDefs[i].orderSlots);
    }
    free(cfg.folderDefs);

    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event) {
    AppState *state = appstate;
    switch (event->type) {
        case SDL_EVENT_QUIT:
            return SDL_APP_SUCCESS;
        case SDL_EVENT_KEY_DOWN:
            if (event->key.scancode == SDL_SCANCODE_ESCAPE) {
                if (!ui_exit_folder()) return SDL_APP_SUCCESS;
                break;
            }
            if (event->key.scancode == SDL_SCANCODE_LEFT)   ui_navigate_grid(-1,  0);
            if (event->key.scancode == SDL_SCANCODE_RIGHT)  ui_navigate_grid( 1,  0);
            if (event->key.scancode == SDL_SCANCODE_UP)     ui_navigate_grid( 0, -1);
            if (event->key.scancode == SDL_SCANCODE_DOWN)   ui_navigate_grid( 0,  1);
            if (event->key.scancode == SDL_SCANCODE_RETURN ||
                event->key.scancode == SDL_SCANCODE_KP_ENTER) {
                if (ui_launch_selected()) return SDL_APP_SUCCESS;
            }
            break;
        case SDL_EVENT_WINDOW_RESIZED:
            Clay_SetLayoutDimensions((Clay_Dimensions){
                (float)event->window.data1, (float)event->window.data2 });
            break;
        case SDL_EVENT_MOUSE_MOTION:
            Clay_SetPointerState(
                (Clay_Vector2){ event->motion.x, event->motion.y },
                event->motion.state & SDL_BUTTON_LMASK);
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            Clay_SetPointerState(
                (Clay_Vector2){ event->button.x, event->button.y },
                event->button.button == SDL_BUTTON_LEFT);
            if (event->button.button == SDL_BUTTON_LEFT)
                state->clicked = true;
            break;
        case SDL_EVENT_MOUSE_WHEEL:
            Clay_UpdateScrollContainers(true,
                (Clay_Vector2){ event->wheel.x * 30, event->wheel.y * 30 }, 0.016f);
            break;
        default: break;
    }
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void *appstate) {
    AppState *state = appstate;
    Clay_RenderCommandArray cmds = ui_layout(state->clicked);
    state->clicked = false;
    if (ui_pop_launch() && ui_launch_selected()) return SDL_APP_SUCCESS;
    SDL_SetRenderDrawColor(state->rendererData.renderer, 24, 24, 28, 255);
    SDL_RenderClear(state->rendererData.renderer);
    SDL_Clay_RenderClayCommands(&state->rendererData, &cmds);
    SDL_RenderPresent(state->rendererData.renderer);
    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void *appstate, SDL_AppResult result) {
    (void)result;
    AppState *state = appstate;
    if (!state) return;
    ui_free();
    if (state->rendererData.fonts) {
        TTF_CloseFont(state->rendererData.fonts[0]);
        SDL_free(state->rendererData.fonts);
    }
    if (state->rendererData.textEngine)
        TTF_DestroyRendererTextEngine(state->rendererData.textEngine);
    if (state->rendererData.renderer)
        SDL_DestroyRenderer(state->rendererData.renderer);
    if (state->window)
        SDL_DestroyWindow(state->window);
    SDL_free(state);
    TTF_Quit();
}
