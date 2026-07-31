from pathlib import Path
import re
import unittest


REPOSITORY_ROOT = Path(__file__).resolve().parents[1]

FUNCTION_DECLARATION = re.compile(
    r"^(?:bool|void|int|char\s*\*|uint8\s*\*)\s*"
    r"([A-Za-z_][A-Za-z0-9_]*)\s*\([^;{}]*\)\s*;",
    re.MULTILINE,
)
FUNCTION_DEFINITION = re.compile(
    r"^(?:bool|void|int|char\s*\*|uint8\s*\*)\s*"
    r"([A-Za-z_][A-Za-z0-9_]*)\s*\([^;{}]*\)\s*\{",
    re.MULTILINE,
)
EXTERN_DECLARATION = re.compile(
    r"^extern\s+.+?\s+\**([A-Za-z_][A-Za-z0-9_]*)"
    r"\s*(?:\[[^;]*\])?;",
    re.MULTILINE,
)


def read(relative_path: str) -> str:
    return (REPOSITORY_ROOT / relative_path).read_text(encoding="ascii")


def assert_stub_covers_header(
    test_case: unittest.TestCase, header_path: str, stub_path: str
) -> None:
    header = read(header_path)
    stub = read(stub_path)
    declared_functions = set(FUNCTION_DECLARATION.findall(header))
    defined_functions = set(FUNCTION_DEFINITION.findall(stub))
    test_case.assertEqual(declared_functions - defined_functions, set())

    declared_globals = set(EXTERN_DECLARATION.findall(header))
    missing_globals = {
        name
        for name in declared_globals
        if not re.search(
            rf"^(?!extern\b)[^;\n]*\b{re.escape(name)}\b[^;\n]*;",
            stub,
            re.MULTILINE,
        )
    }
    test_case.assertEqual(missing_globals, set())


class SoundStubApiTests(unittest.TestCase):
    def test_sound_stub_defines_the_complete_public_boundary(self) -> None:
        assert_stub_covers_header(
            self,
            "PROJECTS/ROLLER/sound.h",
            "PROJECTS/ROLLER/sound_stub.c",
        )

    def test_cd_stub_defines_the_complete_public_boundary(self) -> None:
        assert_stub_covers_header(
            self,
            "PROJECTS/ROLLER/cdx.h",
            "PROJECTS/ROLLER/cdx_stub.c",
        )

    def test_low_level_sound_stub_defines_the_complete_public_boundary(self) -> None:
        assert_stub_covers_header(
            self,
            "PROJECTS/ROLLER/rollersound.h",
            "PROJECTS/ROLLER/rollersound_stub.c",
        )

    def test_game_and_core_select_different_implementations(self) -> None:
        manifest = read("roller-core.srclist")
        self.assertIn("EXCLUDE|PROJECTS/ROLLER/sound.c\n", manifest)
        self.assertIn("EXCLUDE|PROJECTS/ROLLER/cdx.c\n", manifest)
        self.assertIn("STUB_SWAP|PROJECTS/ROLLER/sound_stub.c\n", manifest)
        self.assertIn("STUB_SWAP|PROJECTS/ROLLER/rollersound_stub.c\n", manifest)
        self.assertIn("STUB_SWAP|PROJECTS/ROLLER/cdx_stub.c\n", manifest)


if __name__ == "__main__":
    unittest.main()
