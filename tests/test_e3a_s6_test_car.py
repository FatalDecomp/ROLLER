import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
ROLLER = ROOT / "PROJECTS" / "ROLLER"


def function_body(source: str, signature: str) -> str:
    start = source.index(signature)
    brace = source.index("{", start)
    depth = 0
    for index in range(brace, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[brace + 1 : index]
    raise AssertionError(f"unterminated function: {signature}")


def without_comments(source: str) -> str:
    source = re.sub(r"/\*.*?\*/", "", source, flags=re.DOTALL)
    return re.sub(r"//.*", "", source)


class OverlayAbiTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.header = (ROLLER / "editor_api.h").read_text(encoding="utf-8")

    def test_the_selection_is_appended_not_inserted(self) -> None:
        # E3A-S2's fields keep their offsets, so a host built against v2 still
        # reads the same bytes; only the tail is new.
        for field, offset in (
            ("uiSurfaceClassMask", 20),
            ("uiWireframeClassMask", 24),
            ("uiTestCarDesign", 28),
            ("uiTestCarAiLine", 32),
        ):
            self.assertIn(
                f"offsetof(tEdOverlayState, {field}) == {offset}u", self.header
            )
        self.assertIn(
            "ROLLER_ED_STATIC_ASSERT(sizeof(tEdOverlayState) == 36u", self.header
        )

    def test_only_this_struct_version_moved(self) -> None:
        self.assertIn("#define ROLLER_ED_OVERLAY_STATE_VERSION 3u", self.header)
        self.assertIn("#define ROLLER_ED_API_VERSION 1u", self.header)
        for name in (
            "ROLLER_ED_BOOTSTRAP_INFO_VERSION",
            "ROLLER_ED_INIT_INFO_VERSION",
            "ROLLER_ED_CAMERA_STATE_VERSION",
            "ROLLER_ED_REFERENCE_MESH_VERSION",
            "ROLLER_ED_GEOMETRY_SIZES_VERSION",
        ):
            self.assertIn(f"#define {name} 1u", self.header)

    def test_million_plus_is_a_flag_and_the_counts_are_public(self) -> None:
        self.assertIn(
            "ROLLER_ED_OVERLAY_TEST_CAR_MILLION_PLUS = 1u << 10", self.header
        )
        # The advanced-cars skin is a flag too: the Y model variants share
        # their twin's plan and differ only by texture bank and palette remap,
        # so it modifies a design rather than being one.
        self.assertIn(
            "ROLLER_ED_OVERLAY_TEST_CAR_ADVANCED = 1u << 11", self.header
        )
        self.assertIn("#define ROLLER_ED_TEST_CAR_DESIGN_COUNT 14u", self.header)
        self.assertIn("#define ROLLER_ED_TEST_CAR_AI_LINE_COUNT 4u", self.header)

    def test_the_new_flag_is_known_to_the_core(self) -> None:
        overlay = (ROLLER / "editor_overlay.h").read_text(encoding="utf-8")
        known = overlay[
            overlay.index("#define ROLLER_ED_OVERLAY_KNOWN_FLAGS") :
        ].split("\n\n")[0]
        self.assertIn("ROLLER_ED_OVERLAY_TEST_CAR_MILLION_PLUS", known)


class FacadeValidationTests(unittest.TestCase):
    def test_the_selection_is_range_checked_at_the_boundary(self) -> None:
        source = (ROLLER / "editor_api.c").read_text(encoding="utf-8")
        body = without_comments(
            function_body(
                source,
                "eRollerEdResult ROLLER_ED_CALL RollerEd_SetOverlayState(",
            )
        )
        self.assertIn("ROLLER_ED_TEST_CAR_DESIGN_COUNT", body)
        self.assertIn("ROLLER_ED_TEST_CAR_AI_LINE_COUNT", body)
        self.assertIn("ROLLER_ED_RESULT_INVALID_ARGUMENT", body)
        # Validation happens before the state reaches the legacy seam.
        self.assertLess(
            body.index("uiTestCarDesign"),
            body.index("roller_ed_legacy_scene_set_overlay_state"),
        )

    def test_the_overlay_still_does_not_move_the_epoch(self) -> None:
        # AD-7d. The test car is a view setting like every other overlay, so
        # switching it on must not invalidate E4A-S5's per-epoch extraction.
        source = (ROLLER / "editor_api.c").read_text(encoding="utf-8")
        body = function_body(
            source,
            "eRollerEdResult ROLLER_ED_CALL RollerEd_SetOverlayState(",
        )
        self.assertNotIn("roller_ed_advance_geometry_epoch", body)


class TestCarModuleTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.header = (ROLLER / "editor_test_car.h").read_text(encoding="utf-8")
        cls.source = (ROLLER / "editor_test_car.c").read_text(encoding="utf-8")

    def test_the_simulation_is_never_woken(self) -> None:
        # E1-S6 sets numcars = 0 before the loader's first car-sized
        # initialization; raising it would hand the update loop a car to drive.
        body = without_comments(self.source)
        self.assertNotIn("numcars", body)
        self.assertNotIn("placecars", body)
        self.assertNotIn("testteaminit", body)
        self.assertNotIn("initcarview", body)
        self.assertNotIn("InitCars", body)

    def test_the_car_is_render_data_only(self) -> None:
        body = without_comments(
            function_body(self.source, "bool ed_test_car_prepare(")
        )
        # nCurrChunk = -1 is what makes the pose world-space and every motion
        # offset inert in DisplayCarWithPose.
        self.assertIn("nCurrChunk = -1", body)
        self.assertIn("memset(&Car[ED_TEST_CAR_SLOT]", body)
        self.assertIn("byCarDesignIdx", body)

    def test_the_car_stands_on_the_ai_line_the_overlay_draws(self) -> None:
        body = without_comments(
            function_body(self.source, "bool ed_test_car_pose(")
        )
        # AD-6a: one derivation, shared with E3A-S4's AI-line overlay, so the
        # car cannot drift away from the line the editor is showing.
        self.assertIn("ed_helper_ai_line_point", body)
        # The chunk's own slope and banking are its attitude, so they come
        # straight from localdata.
        self.assertIn("localdata[uiChunkId].iPitch", body)
        self.assertIn("localdata[uiChunkId].iRoll", body)

    def test_the_heading_comes_from_geometry_not_from_localdata(self) -> None:
        # localdata[].iYaw is a per-chunk *turn rate*, not a heading:
        # transfrm.c stores the delta angle and wraps it to a signed half
        # circle. Using it pointed the car along world +X on every straight.
        pose = without_comments(
            function_body(self.source, "bool ed_test_car_pose(")
        )
        self.assertNotIn("localdata[uiChunkId].iYaw", pose)
        self.assertIn("ed_test_car_heading", pose)

        heading = without_comments(
            function_body(self.source, "static bool ed_test_car_heading(")
        )
        self.assertIn("ed_helper_center_point", heading)
        self.assertIn("atan2", heading)
        self.assertIn("ED_TEST_CAR_ANGLE_COUNT", heading)

    def test_million_plus_is_a_half_turn(self) -> None:
        body = without_comments(
            function_body(self.source, "bool ed_test_car_pose(")
        )
        self.assertIn("bMillionPlus", body)
        self.assertIn("ED_TEST_CAR_HALF_TURN", body)
        self.assertIn("#define ED_TEST_CAR_HALF_TURN 0x2000", self.source)

    def test_every_index_is_bounds_checked(self) -> None:
        prepare = without_comments(
            function_body(self.source, "bool ed_test_car_prepare(")
        )
        pose = without_comments(
            function_body(self.source, "bool ed_test_car_pose(")
        )
        self.assertIn("ROLLER_ED_TEST_CAR_DESIGN_COUNT", prepare)
        self.assertIn("ROLLER_ED_TEST_CAR_AI_LINE_COUNT", pose)
        self.assertIn("TRAK_LEN", pose)

    def test_the_advanced_skin_loads_its_own_texture_bank(self) -> None:
        # ROLLER's advanced set is the same file name with a leading 'y'.
        # LoadCarTexture makes that substitution in place on the first byte of
        # the name buffer, which cannot be left to it here: the buffer holds an
        # absolute path by then, so the swap would rewrite its drive letter.
        body = without_comments(
            function_body(self.source, "static bool ed_test_car_load_texture(")
        )
        self.assertIn("bAdvanced", body)
        self.assertIn("'y'", body)
        self.assertIn("TEX_OFF_ADVANCED_CARS", body)
        # Restored, so the game's own view of the flag is untouched.
        self.assertIn("textures_off = iSavedTexturesOff;", body)

    def test_the_skin_is_part_of_what_was_prepared(self) -> None:
        # The same design has two banks, so switching X to Y has to reload.
        body = without_comments(
            function_body(self.source, "bool ed_test_car_prepare(")
        )
        self.assertIn("bAdvanced == s_bPreparedAdvanced", body)

    def test_the_draw_holds_the_flag_for_the_frame(self) -> None:
        # Both renderers read textures_off every frame: the GPU keys its
        # cached car mesh on it, and both apply the mirror remap through it.
        body = without_comments(function_body(self.source, "void ed_test_car_draw("))
        self.assertIn("TEX_OFF_ADVANCED_CARS", body)
        self.assertIn("bAdvanced", body)

    def test_a_missing_car_bank_is_recoverable(self) -> None:
        # Working rule: no library path may call ErrorBoxExit or exit. E0-S7's
        # boundary turns the legacy loader's fatal dialog into a recorded
        # error, and this is where it becomes a plain false.
        body = self.source
        self.assertIn("roller_core_error_clear", body)
        self.assertIn("roller_core_error_pending", body)
        self.assertIn("defined(ROLLER_EDITOR_CORE)", body)

    def test_the_texture_follows_the_document_first_asset_order(self) -> None:
        # E1-S4: a bare legacy filename would resolve against the process
        # working directory, which is not the editor's document.
        self.assertIn("loadtrack_resolve_editor_asset", self.source)
        loader = (ROLLER / "loadtrak.c").read_text(encoding="utf-8")
        body = without_comments(
            function_body(loader, "int loadtrack_resolve_editor_asset(")
        )
        self.assertLess(
            body.index("s_szEditorDocumentAssetRoot"),
            body.index("s_szEditorFallbackAssetRoot"),
        )


class RenderPassTests(unittest.TestCase):
    def test_the_car_draws_after_the_scene_and_the_helpers(self) -> None:
        scene = (ROLLER / "editor_legacy_scene.c").read_text(encoding="utf-8")
        # Twice: the software and GPU render paths.
        self.assertEqual(scene.count("ed_test_car_draw(g_pGameRenderer);"), 2)
        for start in (
            scene.index("if (s_eActiveRenderer == ROLLER_ED_RENDERER_SOFTWARE)"),
        ):
            body = scene[start:]
            self.assertLess(
                body.index("drawtrk3_editor_draw_helpers("),
                body.index("ed_test_car_draw("),
            )

    def test_shutdown_forgets_the_prepared_design(self) -> None:
        scene = (ROLLER / "editor_legacy_scene.c").read_text(encoding="utf-8")
        body = function_body(scene, "void roller_ed_legacy_scene_shutdown(")
        self.assertIn("ed_test_car_reset();", body)
        # Before the renderer is destroyed: the design is only meaningful
        # while the texture bank it registered still exists.
        self.assertLess(
            body.index("ed_test_car_reset();"), body.index("game_render_destroy")
        )

    def test_the_draw_reads_the_overlay_rather_than_a_parameter(self) -> None:
        source = (ROLLER / "editor_test_car.c").read_text(encoding="utf-8")
        body = without_comments(function_body(source, "void ed_test_car_draw("))
        self.assertIn("roller_ed_overlay_test_car(", body)
        self.assertIn("game_render_draw_car(", body)


class BuildRegistrationTests(unittest.TestCase):
    def test_the_translation_unit_is_registered_everywhere(self) -> None:
        manifest = (ROOT / "roller-core.srclist").read_text(encoding="utf-8")
        self.assertIn("KEEP|PROJECTS/ROLLER/editor_test_car.c", manifest)
        cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        self.assertIn("PROJECTS/ROLLER/editor_test_car.c", cmake)
        build_zig = (ROOT / "build.zig").read_text(encoding="utf-8")
        self.assertIn('"PROJECTS/ROLLER/editor_test_car.c"', build_zig)
        self.assertIn("test-e3a-s6-test-car", build_zig)


if __name__ == "__main__":
    unittest.main()
