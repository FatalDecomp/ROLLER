#!/bin/bash
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
OUT_DIR="$SCRIPT_DIR/compiled"
HEADER="$SCRIPT_DIR/../menu_shaders.h"
OVERLAY_HEADER="$SCRIPT_DIR/../debug_overlay_shaders.h"
SCENE_HEADER="$SCRIPT_DIR/../game_scene_shaders.h"
HUD_HEADER="$SCRIPT_DIR/../game_hud_shaders.h"

# Find shadercross binary
SHADERCROSS="${SHADERCROSS:-$(dirname "$SCRIPT_DIR")/../../tools/shadercross/zig-out/bin/shadercross}"
if [ ! -x "$SHADERCROSS" ]; then
    echo "Error: shadercross not found at $SHADERCROSS"
    echo "Build it first: cd tools/shadercross && zig build"
    exit 1
fi

# Find Python: try known working paths before relying on shell aliases
if [ -z "${PYTHON:-}" ]; then
    for candidate in python3 "/c/Program Files/Python/python" "/c/Windows/py.exe" python; do
        if "$candidate" --version >/dev/null 2>&1; then
            PYTHON="$candidate"
            break
        fi
    done
fi

# Python on Windows needs native paths; use cygpath when available
if command -v cygpath >/dev/null 2>&1; then
    W_OUT="$(cygpath -w "$OUT_DIR")"
    W_HEADER="$(cygpath -w "$HEADER")"
    W_OVERLAY="$(cygpath -w "$OVERLAY_HEADER")"
    W_SCENE="$(cygpath -w "$SCENE_HEADER")"
    W_HUD="$(cygpath -w "$HUD_HEADER")"
else
    W_OUT="$OUT_DIR"
    W_HEADER="$HEADER"
    W_OVERLAY="$OVERLAY_HEADER"
    W_SCENE="$SCENE_HEADER"
    W_HUD="$HUD_HEADER"
fi

mkdir -p "$OUT_DIR"

# --- menu shaders ---
echo "Compiling vertex shader..."
"$SHADERCROSS" "$SCRIPT_DIR/menu_vertex.hlsl" -s HLSL -d SPIRV -t vertex   -e main -o "$OUT_DIR/menu_vertex.spv"
"$SHADERCROSS" "$SCRIPT_DIR/menu_vertex.hlsl" -s HLSL -d MSL   -t vertex   -e main -o "$OUT_DIR/menu_vertex.msl"

echo "Compiling pixel shader..."
"$SHADERCROSS" "$SCRIPT_DIR/menu_pixel.hlsl" -s HLSL -d SPIRV -t fragment -e main -o "$OUT_DIR/menu_pixel.spv"
"$SHADERCROSS" "$SCRIPT_DIR/menu_pixel.hlsl" -s HLSL -d MSL   -t fragment -e main -o "$OUT_DIR/menu_pixel.msl"

echo "Compiling mesh vertex shader..."
"$SHADERCROSS" "$SCRIPT_DIR/menu_mesh_vertex.hlsl" -s HLSL -d SPIRV -t vertex   -e main -o "$OUT_DIR/menu_mesh_vertex.spv"
"$SHADERCROSS" "$SCRIPT_DIR/menu_mesh_vertex.hlsl" -s HLSL -d MSL   -t vertex   -e main -o "$OUT_DIR/menu_mesh_vertex.msl"

echo "Compiling mesh pixel shader..."
"$SHADERCROSS" "$SCRIPT_DIR/menu_mesh_pixel.hlsl" -s HLSL -d SPIRV -t fragment -e main -o "$OUT_DIR/menu_mesh_pixel.spv"
"$SHADERCROSS" "$SCRIPT_DIR/menu_mesh_pixel.hlsl" -s HLSL -d MSL   -t fragment -e main -o "$OUT_DIR/menu_mesh_pixel.msl"

# --- overlay shaders ---
echo "Compiling overlay vertex shader..."
"$SHADERCROSS" "$SCRIPT_DIR/overlay_vertex.hlsl" -s HLSL -d SPIRV -t vertex   -e main -o "$OUT_DIR/overlay_vertex.spv"
"$SHADERCROSS" "$SCRIPT_DIR/overlay_vertex.hlsl" -s HLSL -d MSL   -t vertex   -e main -o "$OUT_DIR/overlay_vertex.msl"

echo "Compiling overlay pixel shader..."
"$SHADERCROSS" "$SCRIPT_DIR/overlay_pixel.hlsl" -s HLSL -d SPIRV -t fragment -e main -o "$OUT_DIR/overlay_pixel.spv"
"$SHADERCROSS" "$SCRIPT_DIR/overlay_pixel.hlsl" -s HLSL -d MSL   -t fragment -e main -o "$OUT_DIR/overlay_pixel.msl"

# --- game scene shaders ---
echo "Compiling game scene vertex shader..."
"$SHADERCROSS" "$SCRIPT_DIR/game_scene_vertex.hlsl" -s HLSL -d SPIRV -t vertex   -e main -o "$OUT_DIR/game_scene_vertex.spv"
"$SHADERCROSS" "$SCRIPT_DIR/game_scene_vertex.hlsl" -s HLSL -d MSL   -t vertex   -e main -o "$OUT_DIR/game_scene_vertex.msl"

echo "Compiling game scene pixel shader (opaque)..."
"$SHADERCROSS" "$SCRIPT_DIR/game_scene_pixel.hlsl" -s HLSL -d SPIRV -t fragment -e main -o "$OUT_DIR/game_scene_pixel.spv"
"$SHADERCROSS" "$SCRIPT_DIR/game_scene_pixel.hlsl" -s HLSL -d MSL   -t fragment -e main -o "$OUT_DIR/game_scene_pixel.msl"

echo "Compiling game scene pixel shader (alpha-blend)..."
"$SHADERCROSS" "$SCRIPT_DIR/game_scene_pixel_blend.hlsl" -s HLSL -d SPIRV -t fragment -e main -o "$OUT_DIR/game_scene_pixel_blend.spv"
"$SHADERCROSS" "$SCRIPT_DIR/game_scene_pixel_blend.hlsl" -s HLSL -d MSL   -t fragment -e main -o "$OUT_DIR/game_scene_pixel_blend.msl"

echo "Compiling game car vertex shader..."
"$SHADERCROSS" "$SCRIPT_DIR/game_car_vertex.hlsl" -s HLSL -d SPIRV -t vertex   -e main -o "$OUT_DIR/game_car_vertex.spv"
"$SHADERCROSS" "$SCRIPT_DIR/game_car_vertex.hlsl" -s HLSL -d MSL   -t vertex   -e main -o "$OUT_DIR/game_car_vertex.msl"

echo "Compiling game car pixel shader..."
"$SHADERCROSS" "$SCRIPT_DIR/game_car_pixel.hlsl" -s HLSL -d SPIRV -t fragment -e main -o "$OUT_DIR/game_car_pixel.spv"
"$SHADERCROSS" "$SCRIPT_DIR/game_car_pixel.hlsl" -s HLSL -d MSL   -t fragment -e main -o "$OUT_DIR/game_car_pixel.msl"

# --- HUD shaders ---
echo "Compiling HUD overlay vertex shader..."
"$SHADERCROSS" "$SCRIPT_DIR/game_hud_vertex.hlsl" -s HLSL -d SPIRV -t vertex   -e main -o "$OUT_DIR/game_hud_vertex.spv"
"$SHADERCROSS" "$SCRIPT_DIR/game_hud_vertex.hlsl" -s HLSL -d MSL   -t vertex   -e main -o "$OUT_DIR/game_hud_vertex.msl"

echo "Compiling HUD overlay pixel shader..."
"$SHADERCROSS" "$SCRIPT_DIR/game_hud_pixel.hlsl" -s HLSL -d SPIRV -t fragment -e main -o "$OUT_DIR/game_hud_pixel.spv"
"$SHADERCROSS" "$SCRIPT_DIR/game_hud_pixel.hlsl" -s HLSL -d MSL   -t fragment -e main -o "$OUT_DIR/game_hud_pixel.msl"

# --- CRT shaders ---
CRT_HEADER="$SCRIPT_DIR/../crt_shaders.h"
if command -v cygpath >/dev/null 2>&1; then
    W_CRT="$(cygpath -w "$CRT_HEADER")"
else
    W_CRT="$CRT_HEADER"
fi

echo "Compiling CRT vertex shader..."
"$SHADERCROSS" "$SCRIPT_DIR/crt_vertex.hlsl" -s HLSL -d SPIRV -t vertex   -e main -o "$OUT_DIR/crt_vertex.spv"
"$SHADERCROSS" "$SCRIPT_DIR/crt_vertex.hlsl" -s HLSL -d MSL   -t vertex   -e main -o "$OUT_DIR/crt_vertex.msl"

echo "Compiling CRT pixel shader..."
"$SHADERCROSS" "$SCRIPT_DIR/crt_pixel.hlsl" -s HLSL -d SPIRV -t fragment -e main -o "$OUT_DIR/crt_pixel.spv"
"$SHADERCROSS" "$SCRIPT_DIR/crt_pixel.hlsl" -s HLSL -d MSL   -t fragment -e main -o "$OUT_DIR/crt_pixel.msl"

# --- Header generation via Python (uses Windows-native paths) ---
EMBED_FN='
import os, sys

def embed(path, name):
    with open(path, "rb") as f:
        data = f.read()
    vals = ", ".join(f"0x{b:02x}" for b in data)
    return f"static const unsigned char {name}[] = {{{vals}}};\nstatic const unsigned int {name}_size = {len(data)};\n"
'

echo "Generating menu_shaders.h..."
"$PYTHON" -c "
$EMBED_FN
out_dir = r'$W_OUT'
h = '#ifndef MENU_SHADERS_H\n#define MENU_SHADERS_H\n\n'
for prefix in ['menu', 'menu_mesh']:
    for stage in ['vertex', 'pixel']:
        for fmt, ext in [('spirv', 'spv'), ('msl', 'msl')]:
            p = os.path.join(out_dir, f'{prefix}_{stage}.{ext}')
            if os.path.exists(p):
                h += embed(p, f'{prefix}_{stage}_{fmt}') + '\n'
h += '#endif\n'
with open(r'$W_HEADER', 'w') as f:
    f.write(h)
"
echo "Generated $HEADER"

echo "Generating debug_overlay_shaders.h..."
"$PYTHON" -c "
$EMBED_FN
out_dir = r'$W_OUT'
h = '#ifndef DEBUG_OVERLAY_SHADERS_H\n#define DEBUG_OVERLAY_SHADERS_H\n\n'
for stage in ['vertex', 'pixel']:
    for fmt, ext in [('spirv', 'spv'), ('msl', 'msl')]:
        p = os.path.join(out_dir, f'overlay_{stage}.{ext}')
        if os.path.exists(p):
            h += embed(p, f'overlay_{stage}_{fmt}') + '\n'
h += '#endif\n'
with open(r'$W_OVERLAY', 'w') as f:
    f.write(h)
"
echo "Generated $OVERLAY_HEADER"

echo "Generating game_scene_shaders.h..."
"$PYTHON" -c "
$EMBED_FN
out_dir = r'$W_OUT'
h = '#ifndef GAME_SCENE_SHADERS_H\n#define GAME_SCENE_SHADERS_H\n\n'
for name in ['game_scene_vertex', 'game_scene_pixel', 'game_scene_pixel_blend', 'game_car_vertex', 'game_car_pixel']:
    for fmt, ext in [('spirv', 'spv'), ('msl', 'msl')]:
        p = os.path.join(out_dir, f'{name}.{ext}')
        if os.path.exists(p):
            h += embed(p, f'{name}_{fmt}') + '\n'
h += '#endif\n'
with open(r'$W_SCENE', 'w') as f:
    f.write(h)
"
echo "Generated $SCENE_HEADER"

echo "Generating game_hud_shaders.h..."
"$PYTHON" -c "
$EMBED_FN
out_dir = r'$W_OUT'
h = '#ifndef GAME_HUD_SHADERS_H\n#define GAME_HUD_SHADERS_H\n\n'
for name in ['game_hud_vertex', 'game_hud_pixel']:
    for fmt, ext in [('spirv', 'spv'), ('msl', 'msl')]:
        p = os.path.join(out_dir, f'{name}.{ext}')
        if os.path.exists(p):
            h += embed(p, f'{name}_{fmt}') + '\n'
h += '#endif\n'
with open(r'$W_HUD', 'w') as f:
    f.write(h)
"
echo "Generated $HUD_HEADER"

echo "Generating crt_shaders.h..."
"$PYTHON" -c "
$EMBED_FN
out_dir = r'$W_OUT'
h = '#ifndef CRT_SHADERS_H\n#define CRT_SHADERS_H\n\n'
for stage in ['vertex', 'pixel']:
    for fmt, ext in [('spirv', 'spv'), ('msl', 'msl')]:
        p = os.path.join(out_dir, f'crt_{stage}.{ext}')
        if os.path.exists(p):
            h += embed(p, f'crt_{stage}_{fmt}') + '\n'
h += '#endif\n'
with open(r'$W_CRT', 'w') as f:
    f.write(h)
"
echo "Generated $CRT_HEADER"
