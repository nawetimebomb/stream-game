#define SDL_MAIN_HANDLED
#include "win32_ftw.h"

#include "ftw.h"
#include "ftw_config.h"

#include <stdio.h>
#include <windows.h>

#include "SDL.h"
#include "SDL_image.h"
#include "SDL_mixer.h"
#include "SDL_ttf.h"

// External dependencies
#include "../external/ini.h"
#include "../external/ini.c"

global_var bool game_running = false;
global_var bool global_pause = false;

static TTF_Font *press_start_font;
static SDL_Color text_color = {255, 255, 255};
static SDL_Game_Config game_config = {};
static SDL_Game_Code game_code = {};
static Memory memory;
Game_Data game;

void SDL_platform_unload_game_code() {
    if (game_code.game_code_dll) {
        SDL_UnloadObject(game_code.game_code_dll);
        game_code.game_code_dll = NULL;
    }

    game_code.is_valid = false;
    game_code.game_update_and_render = game_update_and_render_stub;
    game_code.dll_file_time = {};
}

bool SDL_platform_copy_file(const char *source_file,
                            const char *temp_file) {
    SDL_RWops *source = SDL_RWFromFile(source_file, "r");
    SDL_RWops *target = SDL_RWFromFile(temp_file, "w");

    if (!source || !target) {
        return false;
    }

    // Read source into buffer
    Sint64 size = SDL_RWsize(source);

    void *buffer = SDL_calloc(1, size);
    SDL_RWread(source, buffer, size, 1);

    // Write buffer to target
    SDL_RWwrite(target, buffer, size, 1);

    printf("%s --> File copied successfully.\n", source_file);

    SDL_RWclose(source);
    SDL_RWclose(target);
    SDL_free(buffer);

    return true;
}

SDL_Game_Code SDL_platform_load_game_code() {
    SDL_Game_Code result = {};
    WIN32_FILE_ATTRIBUTE_DATA file_data;

    // TODO: DLL file should be loaded from the same folder
    result.source_dll_name = SOURCE_DLL_NAME;
    result.temp_dll_name = TEMP_DLL_NAME;

#if FTW_INTERNAL
    assert(SDL_platform_copy_file(result.source_dll_name, result.temp_dll_name));
#endif

    result.game_code_dll = SDL_LoadObject(result.temp_dll_name);

    if (result.game_code_dll == NULL) {
        printf("SDL_LoadObject failed: %s\n", SDL_GetError());
        exit(1);
    }

    result.game_update_and_render =
        (Game_Update_And_Render *) SDL_LoadFunction(result.game_code_dll,
                                                    "game_update_and_render");

    assert(result.game_update_and_render);

    result.is_valid = true;

    if (GetFileAttributesExA(result.source_dll_name,
                             GetFileExInfoStandard,
                             &file_data)) {
        result.dll_file_time = file_data.ftLastWriteTime;
    }

    return result;
}

bool SDL_platform_code_changed() {
    WIN32_FILE_ATTRIBUTE_DATA new_file_data;
    FILETIME new_file_time;

    if (GetFileAttributesExA(game_code.source_dll_name,
                             GetFileExInfoStandard,
                             &new_file_data)) {
        new_file_time = new_file_data.ftLastWriteTime;

        if (CompareFileTime(&new_file_time, &game_code.dll_file_time) != 0) {
            // Sleep lets Windows to close the file before opening it again
            // to do the copy once the change happens.
            Sleep(500);
            return true;
        }
    }

    return false;
}

bool SDL_platform_config_changed() {
    WIN32_FILE_ATTRIBUTE_DATA file_data;

    if (GetFileAttributesExA(CONFIG_FILE_NAME,
                             GetFileExInfoStandard,
                             &file_data)) {
        FILETIME new_file_time = file_data.ftLastWriteTime;

        if (CompareFileTime(&new_file_time, &game_config.last_update) != 0) {
            // Sleep lets Windows to close the file before opening it again
            // to do the copy once the change happens.
            Sleep(500);
            return true;
        }
    }

    return false;
}

void SDL_platform_unload_config() {

}

int ini_parser_callback(void* user,
                        const char* section,
                        const char* name,
                        const char* value) {
    Game_Options *p_options = (Game_Options *)user;

#define MATCH(s, n) strcmp(section, s) == 0 && strcmp(name, n) == 0
    if (MATCH("Options", "music_volume")) {
        p_options->music_volume = atof(value);
    } else {
        return 0;
    }

    return 1;
}

Game_Options SDL_platform_load_config() {
    Game_Options result = {};
    WIN32_FILE_ATTRIBUTE_DATA file_data;
    char *ini_file = CONFIG_FILE_NAME;

#if FTW_INTERNAL
    ini_file = CONFIG_TEMP_FILE_NAME;
#endif

    assert(SDL_platform_copy_file(CONFIG_FILE_NAME, CONFIG_TEMP_FILE_NAME));

    // Parse .ini file and set new variables
    if (ini_parse(ini_file, ini_parser_callback, &result) < 0) {
        printf("[sdl_platform] Error loading configuration file: %s\n", ini_file);
        exit(1);
    }

    game_config.is_valid = true;

    if (GetFileAttributesExA(CONFIG_FILE_NAME,
                             GetFileExInfoStandard,
                             &file_data)) {
        game_config.last_update = file_data.ftLastWriteTime;
    }

    return result;
}

void on_init(SDL_Window *window, SDL_Renderer *renderer) {
    game_code = SDL_platform_load_game_code();

    assert(game_code.is_valid);
    game.options = SDL_platform_load_config();
    game.screen_width = ORIGINAL_GRAPHIC_WIDTH;
    game.screen_height = ORIGINAL_GRAPHIC_HEIGHT;
    game.window_width = WINDOW_WIDTH;
    game.window_height = WINDOW_HEIGHT;
    game.window = window;
    game.renderer = renderer;
}

void on_refresh() {
    assert(game_code.is_valid);
}

int main() {
    u64 START, END;
    u32 fps_value = 0;
    r32 delta_time_value = 0;
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_Event event;
    u8 *keyboard_state;

    if (SDL_Init(SDL_INIT_VIDEO|SDL_INIT_AUDIO) != 0) {
        fprintf(stderr, "[SDL] SDL_Init Error: %s\n", SDL_GetError());
        return 1;
    }

    if (Mix_Init(MIX_INIT_MP3) != MIX_INIT_MP3) {
        fprintf(stderr, "[SDL_mixer] Mix_Init Error: %s\n", Mix_GetError());
        return 1;
    }

    if (Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 1024) != 0) {
        printf("ERROR: Couldn't open audio.\n Message: %s\n", Mix_GetError());
        return 1;
    }

    if (IMG_Init(IMG_INIT_PNG) != IMG_INIT_PNG) {
        fprintf(stderr, "[SDL_image] IMG_Init Error: %s\n", IMG_GetError());
        return 1;
    }

    if (TTF_Init() == -1) {
        printf("[SDL_ttf] TTF_Init Error: %s\n", TTF_GetError());
        return 1;
    }

    window = SDL_CreateWindow(WINDOW_TITLE,
                              SDL_WINDOWPOS_UNDEFINED,
                              SDL_WINDOWPOS_UNDEFINED,
                              WINDOW_WIDTH,
                              WINDOW_HEIGHT,
                              SDL_WINDOW_OPENGL|
                              SDL_WINDOW_SHOWN|
                              SDL_WINDOW_RESIZABLE);

    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);

    // Recalculate renderer scale
    SDL_RenderSetScale(renderer,
                       (WINDOW_WIDTH  / ORIGINAL_GRAPHIC_WIDTH),
                       (WINDOW_HEIGHT / ORIGINAL_GRAPHIC_HEIGHT));

    on_init(window, renderer);

    // FONT
    press_start_font = TTF_OpenFont("assets/PressStart2P-Regular.ttf", 12);

    if (window) {
        game_running = true;

        Game_Input input = {};

#if FTW_INTERNAL
        LPVOID base_address = (LPVOID)Terabytes(2);
#else
        LPVOID base_address = 0;
#endif

        Game_Memory game_memory = {};
        game_memory.permanent_storage_size = Megabytes(64);
        game_memory.transient_storage_size = Gigabytes(4);

        u64 total_size = game_memory.permanent_storage_size +
            game_memory.transient_storage_size;

        game_memory.permanent_storage = VirtualAlloc(base_address,
                                                     total_size,
                                                     MEM_RESERVE|MEM_COMMIT,
                                                     PAGE_READWRITE);
        game_memory.transient_storage =
            ((u8 *)game_memory.permanent_storage + game_memory.permanent_storage_size);

        if (game_memory.permanent_storage && game_memory.transient_storage) {
            while (game_running) {
                if (game_code.is_valid && !global_pause) {
                    START = SDL_GetPerformanceCounter();
                    keyboard_state = (u8 *)SDL_GetKeyboardState(NULL);

                    if (keyboard_state) {
                        input.kb.move_up.is_down = (keyboard_state[SDL_SCANCODE_W] == 1);
                        input.kb.move_down.is_down = (keyboard_state[SDL_SCANCODE_S] == 1);
                        input.kb.move_left.is_down = (keyboard_state[SDL_SCANCODE_A] == 1);
                        input.kb.move_right.is_down = (keyboard_state[SDL_SCANCODE_D] == 1);
                    }

                    while (SDL_PollEvent(&event)) {
                        switch(event.type) {
                            case SDL_QUIT: {
                                game_running = false;
                            } break;
                            case SDL_WINDOWEVENT: {
                                if (event.window.event == SDL_WINDOWEVENT_RESIZED) {
                                    r32 new_width = event.window.data1;
                                    r32 new_height = event.window.data2;
                                    r32 aspect_ratio_result;

                                    // Keep aspect ratio defined in
                                    // ASPECT_RATIO_WIDTH & ASPECT_RATIO_HEIGHT
                                    if (new_width != game.window_width) {
                                        aspect_ratio_result = new_width / ASPECT_RATIO_WIDTH;
                                        new_height = aspect_ratio_result * ASPECT_RATIO_HEIGHT;
                                    } else if (new_height != game.window_height) {
                                        aspect_ratio_result = new_height / ASPECT_RATIO_HEIGHT;
                                        new_width = aspect_ratio_result * ASPECT_RATIO_WIDTH;
                                    }

                                    // Resizing window and game scale
                                    SDL_SetWindowSize(game.window,
                                                      new_width,
                                                      new_height);

                                    SDL_RenderSetScale(game.renderer,
                                                       (new_width  / ORIGINAL_GRAPHIC_WIDTH),
                                                       (new_height / ORIGINAL_GRAPHIC_HEIGHT));

                                    game.window_width = new_width;
                                    game.window_height = new_height;
                                }
                            } break;
                            case SDL_KEYDOWN: {
                                if (event.key.keysym.sym == SDLK_ESCAPE) {
                                    game_running = false;
                                }

                                if (event.key.keysym.sym == SDLK_p) {
                                    global_pause = !global_pause;
                                }
                            } break;
                        }
                    }

                    SDL_SetRenderDrawColor(game.renderer, 0, 0, 0, 255);
                    SDL_RenderClear(game.renderer);

                    game_code.game_update_and_render(&game_memory, &input, &game);

#if FTW_INTERNAL
                    // TODO: Temp solution to draw FPS/Frametime
                    // This needs to be moved to UI rendering.
                    SDL_Rect ft_rect, fps_rect;

                    // Show FRAMETIME
                    char ft_amount[12];

                    sprintf(ft_amount, "FRAMETIME %f ms", (delta_time_value * 1000));
                    SDL_Surface *ft_surface = TTF_RenderText_Solid(press_start_font,
                                                                   ft_amount,
                                                                   text_color);
                    SDL_Texture *ft_texture = SDL_CreateTextureFromSurface(game.renderer,
                                                                           ft_surface);

                    ft_rect.x = 0;
                    ft_rect.y = 0;
                    ft_rect.w = ft_surface->w;
                    ft_rect.h = ft_surface->h;

                    // Show FPS
                    char fps_amount[12];

                    sprintf(fps_amount, "FPS %d", fps_value);
                    SDL_Surface *fps_surface = TTF_RenderText_Solid(press_start_font,
                                                                    fps_amount,
                                                                    text_color);
                    SDL_Texture *fps_texture = SDL_CreateTextureFromSurface(game.renderer,
                                                                            fps_surface);


                    fps_rect.x = ft_rect.x;
                    fps_rect.y = ft_rect.y + ft_rect.h + 5;
                    fps_rect.w = fps_surface->w;
                    fps_rect.h = fps_surface->h;

                    SDL_RenderCopy(game.renderer, ft_texture, NULL, &ft_rect);
                    SDL_RenderCopy(game.renderer, fps_texture, NULL, &fps_rect);

                    SDL_FreeSurface(ft_surface);
                    SDL_FreeSurface(fps_surface);
                    SDL_DestroyTexture(ft_texture);
                    SDL_DestroyTexture(fps_texture);
#endif

                    SDL_RenderPresent(game.renderer);

                    r32 FREQ = (r32)SDL_GetPerformanceFrequency();
                    END = SDL_GetPerformanceCounter();
                    delta_time_value = (END - START) / FREQ;
                    fps_value = (u32)(1.0f / delta_time_value);
                    game.delta_time = delta_time_value;
                }
#if FTW_INTERNAL
                if (SDL_platform_code_changed()) {
                    SDL_platform_unload_game_code();

                    game_code = SDL_platform_load_game_code();
                    on_refresh();
                }

                if (SDL_platform_config_changed()) {
                    SDL_platform_unload_config();

                    game.options = SDL_platform_load_config();
                }
#endif
            }
        }
    }

    SDL_platform_unload_game_code();

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);

    SDL_Quit();

    return 0;
}
