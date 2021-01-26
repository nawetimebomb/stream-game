#include "ftw_config.h"
#include "ftw_intrinsics.h"
#include "ftw_math.h"
#include "ftw_types.h"
#include "ftw_world.h"

#include "ftw_world.cpp"

/* TODO: (read below)
 *     All these below might need clean up or shouldn't be here.
 *     Make sure all internal dependency starts with ftw_ this point forward,
 *     also, make the platform layer debug the code, not the game itself.
 */
#include <stdio.h>
#include <stdlib.h>
#include "entity.h"
#include "item.h"
#include "SDL_mixer.h"
#include "SDL_ttf.h"
#include "texture_manager.h"

// Compile only these...
#include "entity.cpp"
#include "item.cpp"
#include "texture_manager.cpp"

global_var Game_Data *game;
global_var Uint64 animation_timer;
global_var Uint32 current;
global_var Uint32 last_update;

internal void initialize_arena(Memory_Arena *arena, mem_size size, u8 *base) {
    arena->size = size;
    arena->base = base;
    arena->used = 0;
}

#define push_struct(arena, type) (type *)push_size_(arena, sizeof(type))
#define push_array(arena, count, type) (type *)push_size_(arena, (count)*sizeof(type))
void * push_size_(Memory_Arena *arena, mem_size size) {
    assert(arena->used + size <= arena->size);
    void *result = arena->base + arena->used;
    arena->used += size;

    return result;
}

extern "C" GAME_UPDATE_AND_RENDER(game_update_and_render) {
    assert(sizeof(Game_State) <= memory->permanent_storage_size);

    Game_State *game_state = (Game_State *)memory->permanent_storage;

    if (!memory->is_initialized) {
        // TODO: Randomness
        srand(time(NULL));

        // TODO: Legacy code to be removed once we start using
        //       the permanent/transient storage.
        // TODO: remove this with new load_texture/create_entity combo
        game = game;

        initialize_arena(&game_state->world_arena,
                         memory->permanent_storage_size - sizeof(Game_State),
                         (u8 *)memory->permanent_storage + sizeof(Game_State));

        game_state->world = push_struct(&game_state->world_arena, World);
        World *world = game_state->world;
        world->tilemap = push_struct(&game_state->world_arena, Tilemap);
        Tilemap *tilemap = world->tilemap;

        tilemap->tile_side_in_meters = 1.4f;
        tilemap->tile_side_in_pixels = 30.0f;
        tilemap->tilemap_count = 100;
        tilemap->count_x = 17;
        tilemap->count_y = 9;
        tilemap->rooms = push_array(&game_state->world_arena,
                                    (tilemap->tilemap_count*tilemap->tilemap_count),
                                    Room);

        // TODO: World generator!
        u32 TOTAL_ROOMS = random_range(8, 20);
        u32 GEN_X = tilemap->tilemap_count / 2;
        u32 GEN_Y = tilemap->tilemap_count / 2;
        u32 FOUND = 0;
        Room *room = get_room(tilemap, GEN_X, GEN_Y);
        assert(room);

        { // Start Position
            room->id = 1;
            room->colorR = random_range(20, 200);
            room->colorG = random_range(20, 200);
            room->colorB = random_range(20, 200);
            room->tiles = (u32 *)room_nesw00;
        }

        while (FOUND < TOTAL_ROOMS) {
            u32 DIRECTION = random(3);

            switch (DIRECTION) {
                case WorldGen_North: {
                    GEN_Y += 1;
                } break;
                case WorldGen_East: {
                    GEN_X += 1;
                } break;
                case WorldGen_South: {
                    GEN_Y -= 1;
                } break;
                case WorldGen_West: {
                    GEN_X -= 1;
                } break;
            }

            assert(GEN_X >= 0 &&
                   GEN_X <= tilemap->tilemap_count &&
                   GEN_Y >= 0 &&
                   GEN_Y <= tilemap->tilemap_count);

            Room *room = get_room(tilemap, GEN_X, GEN_Y);
            assert(room);

            if (room->id == 0) {
                u32 random_room = random(4);
                room->id = 1;
                room->colorR = random_range(20, 200);
                room->colorG = random_range(20, 200);
                room->colorB = random_range(20, 200);

                switch (random_room) {
                    case 0: {
                        room->tiles = (u32 *)room_nesw00;
                    } break;
                    case 1: {
                        room->tiles = (u32 *)room_nesw01;
                    } break;
                    case 2: {
                        room->tiles = (u32 *)room_nesw02;
                    } break;
                    case 3: {
                        room->tiles = (u32 *)room_nesw03;
                    } break;
                    case 4: {
                        room->tiles = (u32 *)room_nesw04;
                    } break;
                }

                FOUND++;
            }
        }

        {// Player
            game_state->player_p.tilemap_x = tilemap->tilemap_count / 2;
            game_state->player_p.tilemap_y = tilemap->tilemap_count / 2;
            game_state->player_p.tile_x = 3;
            game_state->player_p.tile_y = 3;
            game_state->player_p.tile_relative_x = 5.0f;
            game_state->player_p.tile_relative_y = 5.0f;
        }

        memory->is_initialized = true;
    }

    World *world = game_state->world;
    Tilemap *tilemap = world->tilemap;

    u32 center_x = (game->window_width - tilemap->tile_side_in_pixels) / 2;
    u32 center_y = game->window_height / 2;
    u32 meters_to_pixels = tilemap->tile_side_in_pixels / tilemap->tile_side_in_meters;

    r32 player_height = 1.2f;
    r32 player_width =  0.6f*player_height;

    Vector2 movement = {};

    Game_Input_Controller *keyboard = &input->controllers[0];
    Game_Input_Controller *joystick = &input->controllers[1];


    if (keyboard->move_left.is_down) {
        movement.x = -1.0f;
    }
    if (keyboard->move_right.is_down) {
        movement.x = 1.0f;
    }
    if (keyboard->move_up.is_down) {
        movement.y = -1.0f;
    }
    if (keyboard->move_down.is_down) {
        movement.y = 1.0f;
    }

    if (joystick->is_analog) {
        movement.x += joystick->left_axis.x;
        movement.y += joystick->left_axis.y;
    }

    // NOTE: this is meters per second.
    f32 velocity = 8.0f * game->delta_time;
    Vector2 d_player_position = movement * velocity;

    World_Position new_player_position = game_state->player_p;
    new_player_position.tile_relative_x += d_player_position.x;
    new_player_position.tile_relative_y -= d_player_position.y;
    new_player_position = recanonicalize_position(tilemap, new_player_position);

    World_Position new_player_left = new_player_position;
    new_player_left.tile_relative_x -= 0.5f*player_width;
    new_player_left = recanonicalize_position(tilemap, new_player_left);

    World_Position new_player_right = new_player_position;
    new_player_right.tile_relative_x += 0.5f*player_width;
    new_player_right = recanonicalize_position(tilemap, new_player_right);

    if (is_world_point_empty(tilemap, new_player_position) &&
        is_world_point_empty(tilemap, new_player_left) &&
        is_world_point_empty(tilemap, new_player_right)) {
        game_state->player_p = new_player_position;
    }

    // 30
    // -13 - 13 = 780
    // -22 - 22 = 1320

    // X = 1280
    // Y = 720

    // 40
    // Y = -10 - 10
    // X = -16 - 16
    for (int rel_row = -13; rel_row <= 13; ++rel_row) {
        for (int rel_column = -22; rel_column <= 22; ++rel_column) {
            SDL_Rect dst_rect;

            s32 column = game_state->player_p.tile_x + rel_column;
            s32 row = game_state->player_p.tile_y + rel_row;

            u32 tile_value = get_tile_value(tilemap,
                                            game_state->player_p.tilemap_x,
                                            game_state->player_p.tilemap_y,
                                            column,
                                            row);
            if (tile_value == 9) {
                continue;
            }


            dst_rect.x = 0.5f*tilemap->tile_side_in_pixels + center_x -
                tilemap->tile_side_in_pixels*game_state->player_p.tile_x -
                meters_to_pixels*game_state->player_p.tile_relative_x +
                ((r32)column)*tilemap->tile_side_in_pixels;
            dst_rect.y = center_y +
                tilemap->tile_side_in_pixels*game_state->player_p.tile_y +
                meters_to_pixels*game_state->player_p.tile_relative_y -
                ((r32)row)*tilemap->tile_side_in_pixels;
            dst_rect.w = tilemap->tile_side_in_pixels;
            dst_rect.h = tilemap->tile_side_in_pixels;

            // TODO: This is only demo testing
            Room *room = get_room(tilemap,
                                  game_state->player_p.tilemap_x,
                                  game_state->player_p.tilemap_y,
                                  column,
                                  row);
            SDL_SetRenderDrawColor(game->renderer,
                                   room->colorR,
                                   room->colorG,
                                   room->colorB,
                                   255);

            if (tile_value == 1) {
                SDL_SetRenderDrawColor(game->renderer, 50, 50, 50, 255);
            }

            if (column == game_state->player_p.tile_x &&
                row == game_state->player_p.tile_y) {
                SDL_SetRenderDrawColor(game->renderer, 0, 0, 0, 255);
            }

            SDL_RenderFillRect(game->renderer, &dst_rect);
        }
    }

    SDL_FRect player_rect;
    player_rect.x = center_x + 0.5f*(player_width*meters_to_pixels);
    // TODO: Adding world->tile_side_pixels because it's missing
    // from the original math. Check later.
    player_rect.y = tilemap->tile_side_in_pixels + center_y -
        (player_height*meters_to_pixels);
    player_rect.w = player_width*meters_to_pixels;
    player_rect.h = player_height*meters_to_pixels;

    SDL_SetRenderDrawColor(game->renderer, 255, 255, 255, 255);
    SDL_RenderFillRectF(game->renderer, &player_rect);
}
