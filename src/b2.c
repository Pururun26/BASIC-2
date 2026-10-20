#include "b2_conf.h"
#include "b2_api.h"
#include "b2_api_bs.h"
#include "raylib.h"
#include <stdio.h>

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s <cartridge.b2>\n", argv[0]);
        return 2;
    }
    const char* cart_path = argv[1];

    /* --- 1. Сначала bs и компиляция, без окна --- */
    bs_State* S = bs_new();
    b2_register_bs_api(S);
    bs_set_budget(S, 10000000);

    if (bs_run_file(S, cart_path) < 0) {
        fprintf(stderr, "compile error: %s (line %d)\n",
                bs_error(S), bs_error_line(S));
        bs_free(S);
        return 1;
    }
    fprintf(stderr, "[compile] %s OK\n", cart_path);

    /* --- 2. Окно --- */
    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    InitWindow(B2_WINDOW_WIDTH, B2_WINDOW_HEIGHT, B2_TITLE);
    InitAudioDevice();
    SetTargetFPS(B2_FPS);
    SetWindowMinSize(B2_WIDTH * 2, B2_HEIGHT * 2);

    b2_init();

    /* --- 3. Цикл --- */
    while (!WindowShouldClose()) {
        if (IsKeyPressed(KEY_F11)) ToggleFullscreen();

        b2_poll_buttons();       /* опрос до bs_call */

        BeginDrawing();
            ClearBackground(BLACK);

            if (bs_call(S, "UPDATE", 0) < 0) {
                fprintf(stderr, "runtime error: %s (line %d)\n",
                        bs_error(S), bs_error_line(S));
            }

            b2_present();
        EndDrawing();
    }

    b2_close();
    bs_free(S);
    CloseAudioDevice();
    CloseWindow();
    return 0;
}