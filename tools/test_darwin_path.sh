#!/bin/sh
# Host test of darwin_path() (source/darwin.c) with the paths Rayman 2 builds: fopenFullPath()
# glues "<bundle>/" + "\..\Documents\RAYMAN_2_*" (Windows separators -> '/', "//" collapsed),
# CPA engine "%s\%s" paths, relative paths against the working directory.
# Usage: sh tools/test_darwin_path.sh
set -e
D=$(mktemp -d)
trap 'rm -rf "$D"' EXIT
cd "$(dirname "$0")/.."
{
    printf '#include <assert.h>\n#include <stdio.h>\n#include <string.h>\n'
    printf '#define DATA_PATH "ux0:data/rayman2thegreatescape/"\n#define DOCS_PATH DATA_PATH "saves/"\n'
    printf '#define BUNDLE_PATH DATA_PATH "game_app/"\n#define EXE_NAME "Rayman2"\n'
    sed -n '/^static char cwd\[/,/^}/p' source/darwin.c
    sed -n '/^const char \*darwin_path/,/^}/p' source/darwin.c
    cat <<'C'
static const char *t(const char *in) { static char o[512]; return darwin_path(in, o, sizeof(o)); }
#define EQ(a, b) do { const char *r = t(a); if (strcmp(r, b)) { printf("FAIL %s -> %s (want %s)\n", a, r, b); return 1; } } while (0)
int main(void) {
    EQ("ux0:data/rayman2thegreatescape/game_app//../Documents/RAYMAN_2_SY_0", "ux0:data/rayman2thegreatescape/saves/RAYMAN_2_SY_0");
    EQ("ux0:data/rayman2thegreatescape/game_app/\\..\\Documents\\RAYMAN_2_SY_0", "ux0:data/rayman2thegreatescape/saves/RAYMAN_2_SY_0");
    EQ("ux0:data/rayman2thegreatescape/game_app/DATA/WORLD/SOUNDIPHONE/ASTROTH1.BGM", "ux0:data/rayman2thegreatescape/game_app/DATA/WORLD/SOUNDIPHONE/ASTROTH1.BGM");
    EQ("DATA\\WORLD\\LEVELS\\FIX.SNA", "ux0:data/rayman2thegreatescape/game_app/DATA/WORLD/LEVELS/FIX.SNA");
    EQ("./DATA/GAME.DSB", "ux0:data/rayman2thegreatescape/game_app/DATA/GAME.DSB");
    EQ("/var/mobile/Applications/X/Rayman2.app/DATA/GAME.DSB", "ux0:data/rayman2thegreatescape/var/mobile/Applications/X/Rayman2.app/DATA/GAME.DSB");
    EQ("ux0:data/rayman2thegreatescape/Rayman2.app/DATA/GAME.DSB", "ux0:data/rayman2thegreatescape/game_app/DATA/GAME.DSB");
    EQ("ux0:data/rayman2thegreatescape/Documents", "ux0:data/rayman2thegreatescape/saves/");
    puts("darwin_path ok (8 cases)");
    return 0;
}
C
} > "$D/t.c"
cc -w -o "$D/t" "$D/t.c" && "$D/t"
