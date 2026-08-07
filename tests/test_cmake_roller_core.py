import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest


REPOSITORY_ROOT = Path(__file__).resolve().parents[1]


def write_fake_package(
    package_root: Path, package_name: str, target_name: str, version: str
) -> None:
    (package_root / f"{package_name}Config.cmake").write_text(
        f"add_library({target_name} INTERFACE IMPORTED)\n", encoding="ascii"
    )
    (package_root / f"{package_name}ConfigVersion.cmake").write_text(
        f'set(PACKAGE_VERSION "{version}")\n'
        "if(PACKAGE_FIND_VERSION VERSION_GREATER PACKAGE_VERSION)\n"
        "  set(PACKAGE_VERSION_COMPATIBLE FALSE)\n"
        "else()\n"
        "  set(PACKAGE_VERSION_COMPATIBLE TRUE)\n"
        "endif()\n",
        encoding="ascii",
    )


class CMakeRollerCoreTests(unittest.TestCase):
    def test_build_options_and_warning_flags_are_target_scoped(self) -> None:
        cmake_lists = (REPOSITORY_ROOT / "CMakeLists.txt").read_text(
            encoding="ascii"
        )
        self.assertIn(
            'option(ROLLER_BUILD_GAME "Build the ROLLER executable" ON)',
            cmake_lists,
        )
        self.assertIn(
            'option(ROLLER_BUILD_EDITOR_CORE "Build roller-core" OFF)',
            cmake_lists,
        )
        self.assertNotIn("add_compile_options(", cmake_lists)
        self.assertIn("target_compile_options(${target_name} PRIVATE", cmake_lists)

    def test_core_only_configuration_has_no_game_target_or_dependencies(self) -> None:
        cmake = shutil.which("cmake")
        self.assertIsNotNone(cmake, "CMake is required for the roller-core check")

        with tempfile.TemporaryDirectory() as temporary_directory:
            temporary_root = Path(temporary_directory)
            package_root = temporary_root / "packages"
            package_root.mkdir()
            write_fake_package(package_root, "SDL3", "SDL3::SDL3", "3.2.22")
            write_fake_package(
                package_root,
                "SDL3_image",
                "SDL3_image::SDL3_image",
                "3.2.4",
            )

            build_root = temporary_root / "build"
            query_root = build_root / ".cmake" / "api" / "v1" / "query"
            query_root.mkdir(parents=True)
            (query_root / "codemodel-v2").write_text("", encoding="ascii")

            command = [
                cmake,
                "-S",
                str(REPOSITORY_ROOT),
                "-B",
                str(build_root),
                "-DROLLER_BUILD_GAME=OFF",
                "-DROLLER_BUILD_EDITOR_CORE=ON",
                f"-DCMAKE_PREFIX_PATH={package_root}",
                "-DCMAKE_DISABLE_FIND_PACKAGE_WildMidi=TRUE",
                "-DCMAKE_DISABLE_FIND_PACKAGE_libcdio=TRUE",
            ]
            if sys.platform != "win32":
                command.extend(["-G", "Ninja"])

            result = subprocess.run(
                command,
                cwd=REPOSITORY_ROOT,
                check=False,
                capture_output=True,
                text=True,
            )
            self.assertEqual(
                result.returncode,
                0,
                msg=f"CMake configure failed:\n{result.stdout}\n{result.stderr}",
            )

            reply_root = build_root / ".cmake" / "api" / "v1" / "reply"
            index_path = next(reply_root.glob("index-*.json"))
            index = json.loads(index_path.read_text(encoding="utf-8"))
            codemodel_path = reply_root / index["reply"]["codemodel-v2"]["jsonFile"]
            codemodel = json.loads(codemodel_path.read_text(encoding="utf-8"))
            targets = codemodel["configurations"][0]["targets"]
            targets_by_name = {target["name"]: target for target in targets}

            self.assertIn("roller-core", targets_by_name)
            self.assertNotIn("roller", targets_by_name)

            core_target_path = (
                reply_root / targets_by_name["roller-core"]["jsonFile"]
            )
            core_target = json.loads(core_target_path.read_text(encoding="utf-8"))
            source_names = {
                Path(source["path"]).name for source in core_target["sources"]
            }
            self.assertEqual(len(source_names), 75)
            self.assertIn("editor_camera.c", source_names)
            self.assertIn("editor_overlay.c", source_names)
            self.assertIn("editor_helpers.c", source_names)
            self.assertIn("editor_test_car.c", source_names)
            self.assertIn("editor_api.c", source_names)
            self.assertIn("editor_legacy_scene.c", source_names)
            self.assertIn("editor_core_host.c", source_names)
            self.assertIn("frontend.c", source_names)
            self.assertIn("network.c", source_names)
            self.assertIn("roller_core_error.c", source_names)
            self.assertIn("sound_stub.c", source_names)
            self.assertIn("rollersound_stub.c", source_names)
            self.assertIn("cdx_stub.c", source_names)
            self.assertNotIn("sound.c", source_names)
            self.assertNotIn("rollersound.c", source_names)
            self.assertNotIn("cdx.c", source_names)
            self.assertIn("editor-core-link-test", targets_by_name)


if __name__ == "__main__":
    unittest.main()
