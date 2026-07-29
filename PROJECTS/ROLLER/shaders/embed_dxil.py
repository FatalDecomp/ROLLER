#!/usr/bin/env python3
"""Embed the editor/game scene DXIL blobs in a C header."""

from pathlib import Path


SHADERS = (
    "game_scene_vertex",
    "game_scene_pixel",
    "game_scene_pixel_blend",
    "game_scene_track_darken_pixel",
    "game_scene_sign_pixel",
    "game_car_vertex",
    "game_car_pixel",
    "game_hud_vertex",
    "game_hud_pixel",
    "game_particle_vertex",
    "game_particle_pixel",
    "game_particle_tex_vertex",
    "game_particle_tex_pixel",
)


def embed(path: Path, name: str) -> str:
    data = path.read_bytes()
    rows = []
    for offset in range(0, len(data), 16):
        chunk = data[offset : offset + 16]
        rows.append("    " + ", ".join(f"0x{byte:02x}" for byte in chunk))
    values = ",\n".join(rows)
    return (
        f"static const unsigned char {name}_dxil[] = {{\n{values}\n}};\n"
        f"static const unsigned int {name}_dxil_size = {len(data)};\n"
    )


def main() -> None:
    shader_dir = Path(__file__).resolve().parent
    compiled_dir = shader_dir / "compiled"
    output_path = shader_dir.parent / "game_dxil_shaders.h"
    sections = ["#ifndef GAME_DXIL_SHADERS_H", "#define GAME_DXIL_SHADERS_H", ""]
    for shader in SHADERS:
        sections.append(embed(compiled_dir / f"{shader}.dxil", shader))
    sections.append("#endif /* GAME_DXIL_SHADERS_H */\n")
    output_path.write_text("\n".join(sections), encoding="ascii", newline="\n")


if __name__ == "__main__":
    main()
