#pragma once
#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>
#include "../clay/clay.h"
#include "config.h"
#include <stdbool.h>

void ui_init(SDL_Renderer *renderer, const LauncherConfig *cfg);
Clay_RenderCommandArray ui_layout(bool clicked);
void ui_navigate_grid(int dx, int dy);
bool ui_launch_selected(void);
bool ui_pop_launch(void);
bool ui_exit_folder(void); /* returns true if exited a folder, false if already at top level */
void ui_free(void);
