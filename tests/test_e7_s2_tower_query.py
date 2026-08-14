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


class TowerQueryAbiTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.header = (ROLLER / "editor_api.h").read_text(encoding="utf-8")
        cls.cpp = (ROOT / "tests" / "editor_api_cpp_test.cpp").read_text(
            encoding="utf-8"
        )

    def test_tower_info_is_a_sized_versioned_value_type(self) -> None:
        for field in (
            "uiStructSize",
            "uiVersion",
            "uiChunkId",
            "fWorldPosition[3]",
            "fAnchorPosition[3]",
        ):
            self.assertIn(field, self.header)
        self.assertIn("#define ROLLER_ED_TOWER_INFO_VERSION 1u", self.header)

    def test_c_and_cpp_pin_the_same_layout(self) -> None:
        for assertion in (
            "sizeof(tEdTowerInfo) == 36u",
            "ROLLER_ED_ALIGNOF(tEdTowerInfo) == 4u",
            "offsetof(tEdTowerInfo, uiChunkId) == 8u",
            "offsetof(tEdTowerInfo, fWorldPosition) == 12u",
            "offsetof(tEdTowerInfo, fAnchorPosition) == 24u",
        ):
            self.assertIn(assertion, self.header)
        self.assertIn("std::is_standard_layout<tEdTowerInfo>", self.cpp)
        self.assertIn("ROLLER_ED_TOWER_INFO_VERSION == 1u", self.cpp)

    def test_public_queries_return_named_results(self) -> None:
        self.assertRegex(
            self.header,
            r"eRollerEdResult\s+ROLLER_ED_CALL\s+RollerEd_QueryTowerCount\s*\(",
        )
        self.assertRegex(
            self.header,
            r"eRollerEdResult\s+ROLLER_ED_CALL\s+RollerEd_QueryTower\s*\(",
        )


class TowerQueryBoundaryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.api = (ROLLER / "editor_api.c").read_text(encoding="utf-8")
        cls.scene = (ROLLER / "editor_legacy_scene.c").read_text(
            encoding="utf-8"
        )
        cls.lifecycle = (ROOT / "tests" / "editor_api_lifecycle_test.c").read_text(
            encoding="utf-8"
        )
        cls.soak = (ROOT / "tests" / "editor_reload_soak_acceptance.c").read_text(
            encoding="utf-8"
        )

    def test_both_queries_require_the_worker_and_a_ready_scene(self) -> None:
        for signature in (
            "eRollerEdResult ROLLER_ED_CALL RollerEd_QueryTowerCount(",
            "eRollerEdResult ROLLER_ED_CALL RollerEd_QueryTower(",
        ):
            body = without_comments(function_body(self.api, signature))
            self.assertIn("roller_ed_require_worker()", body)
            self.assertIn("s_eSceneState != ROLLER_ED_SCENE_READY", body)
            self.assertIn("ROLLER_ED_RESULT_NO_SCENE", body)

    def test_info_is_validated_and_copied_only_after_success(self) -> None:
        body = without_comments(
            function_body(
                self.api,
                "eRollerEdResult ROLLER_ED_CALL RollerEd_QueryTower(",
            )
        )
        self.assertIn("roller_ed_validate_struct(", body)
        self.assertIn("ROLLER_ED_TOWER_INFO_VERSION", body)
        self.assertIn("uiTowerIndex >= uiCount", body)
        self.assertIn("tEdTowerInfo Info", body)
        self.assertLess(body.index("roller_ed_legacy_scene_query_tower("),
                        body.index("*pInfoOut = Info"))

    def test_legacy_seam_publishes_loaded_positions_and_anchor(self) -> None:
        body = without_comments(
            function_body(
                self.scene, "void roller_ed_legacy_scene_query_tower("
            )
        )
        for table in ("TowerBase[", "TowerX[", "TowerY[", "TowerZ["):
            self.assertIn(table, body)
        self.assertIn("localdata[iChunkIdx].pointAy[3].fX", body)
        self.assertIn("localdata[iChunkIdx].pointAy[3].fY", body)
        self.assertIn("localdata[iChunkIdx].pointAy[3].fZ", body)
        self.assertNotIn("sqrt", body)

    def test_lifecycle_covers_wrong_thread_scene_and_version_errors(self) -> None:
        for result in (
            "ROLLER_ED_RESULT_WRONG_THREAD",
            "ROLLER_ED_RESULT_NO_SCENE",
            "ROLLER_ED_RESULT_INVALID_VERSION",
            "ROLLER_ED_RESULT_INVALID_ARGUMENT",
        ):
            self.assertIn(result, self.lifecycle)
        self.assertIn("memcmp(&TowerInfo, &Before", self.lifecycle)

    def test_real_facade_fixture_round_trips_all_bounded_towers(self) -> None:
        self.assertIn("RollerEd_QueryTowerCount(&uiTowerCount)", self.soak)
        self.assertIn("RollerEd_QueryTower((uint32_t)iTower, &Info)", self.soak)
        self.assertIn("Info.fWorldPosition[0] != TowerX[iTower]", self.soak)
        self.assertIn("-localdata[iChunkIdx].pointAy[3].fX", self.soak)


if __name__ == "__main__":
    unittest.main()
