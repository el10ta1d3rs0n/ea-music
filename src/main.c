#include "raylib.h"
#include "ui.h"

int main(void)
{
    SetConfigFlags(FLAG_WINDOW_RESIZABLE |
                   FLAG_VSYNC_HINT |
                   FLAG_MSAA_4X_HINT);

    InitWindow(1000, 720, "EA-MUSIC");

    if (!IsWindowReady()) {
        return 1;
    }

    SetTargetFPS(60);
    UIInit();

    while (!WindowShouldClose()) {
        UIUpdate(GetFrameTime());

        BeginDrawing();
        UIDraw();
        EndDrawing();
    }

    UIClose();
    CloseWindow();
    return 0;
}
