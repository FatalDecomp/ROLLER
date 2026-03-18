#!/bin/bash
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
OUT_DIR="$SCRIPT_DIR/compiled"
HEADER="$SCRIPT_DIR/../menu_shaders.h"

# Find shadercross binary
SHADERCROSS="${SHADERCROSS:-$(dirname "$SCRIPT_DIR")/../../tools/shadercross/zig-out/bin/shadercross}"
if [ ! -x "$SHADERCROSS" ]; then
    echo "Error: shadercross not found at $SHADERCROSS"
    echo "Build it first: cd tools/shadercross && zig build"
    exit 1
fi

mkdir -p "$OUT_DIR"

echo "Compiling vertex shader..."
"$SHADERCROSS" "$SCRIPT_DIR/menu_vertex.hlsl" -s HLSL -d SPIRV -t vertex -e main -o "$OUT_DIR/menu_vertex.spv"
"$SHADERCROSS" "$SCRIPT_DIR/menu_vertex.hlsl" -s HLSL -d MSL -t vertex -e main -o "$OUT_DIR/menu_vertex.msl"

echo "Compiling pixel shader..."
"$SHADERCROSS" "$SCRIPT_DIR/menu_pixel.hlsl" -s HLSL -d SPIRV -t fragment -e main -o "$OUT_DIR/menu_pixel.spv"
"$SHADERCROSS" "$SCRIPT_DIR/menu_pixel.hlsl" -s HLSL -d MSL -t fragment -e main -o "$OUT_DIR/menu_pixel.msl"

echo "Compiling mesh vertex shader..."
"$SHADERCROSS" "$SCRIPT_DIR/menu_mesh_vertex.hlsl" -s HLSL -d SPIRV -t vertex -e main -o "$OUT_DIR/menu_mesh_vertex.spv"
"$SHADERCROSS" "$SCRIPT_DIR/menu_mesh_vertex.hlsl" -s HLSL -d MSL -t vertex -e main -o "$OUT_DIR/menu_mesh_vertex.msl"

echo "Compiling mesh pixel shader..."
"$SHADERCROSS" "$SCRIPT_DIR/menu_mesh_pixel.hlsl" -s HLSL -d SPIRV -t fragment -e main -o "$OUT_DIR/menu_mesh_pixel.spv"
"$SHADERCROSS" "$SCRIPT_DIR/menu_mesh_pixel.hlsl" -s HLSL -d MSL -t fragment -e main -o "$OUT_DIR/menu_mesh_pixel.msl"

echo "Generating menu_shaders.h..."
python3 -c "
import os

def embed(path, name):
    with open(path, 'rb') as f:
        data = f.read()
    vals = ', '.join(f'0x{b:02x}' for b in data)
    return f'static const unsigned char {name}[] = {{{vals}}};\nstatic const unsigned int {name}_size = {len(data)};\n'

out_dir = '$OUT_DIR'
h = '#ifndef MENU_SHADERS_H\n#define MENU_SHADERS_H\n\n'
for prefix in ['menu', 'menu_mesh']:
    for stage in ['vertex', 'pixel']:
        for fmt, ext in [('spirv', 'spv'), ('msl', 'msl')]:
            p = os.path.join(out_dir, f'{prefix}_{stage}.{ext}')
            if os.path.exists(p):
                h += embed(p, f'{prefix}_{stage}_{fmt}') + '\n'
h += '#endif\n'
with open('$HEADER', 'w') as f:
    f.write(h)
"

echo "Generated $HEADER"
