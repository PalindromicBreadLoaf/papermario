#include "asset_loader.h"

#define DEFAULT_ROM_PATH "ver/us/baserom.z64"

int main(int argc, char *argv[]) {
    const char *rom_path = (argc > 1) ? argv[1] : DEFAULT_ROM_PATH;
    asset_loader_init(rom_path);
    asset_loader_shutdown();
    return 0;
}
