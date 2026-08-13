import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
ROLLER = ROOT / "PROJECTS" / "ROLLER"


class TowerLimitTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.header = (ROLLER / "tower.h").read_text(encoding="utf-8")
        cls.source = (ROLLER / "tower.c").read_text(encoding="utf-8")
        cls.loader = (ROLLER / "loadtrak.c").read_text(encoding="utf-8")
        cls.soak = (ROOT / "tests" / "editor_reload_soak_acceptance.c").read_text(
            encoding="utf-8"
        )

    def test_one_constant_sizes_every_tower_table(self) -> None:
        self.assertIn("#define MAX_TOWERS 32", self.header)
        for declaration in (
            "TowerX[MAX_TOWERS]",
            "TowerY[MAX_TOWERS]",
            "TowerZ[MAX_TOWERS]",
            "TowerBase[MAX_TOWERS]",
        ):
            self.assertIn(declaration, self.header)
            self.assertIn(declaration, self.source)

    def test_loader_checks_the_limit_before_its_first_write(self) -> None:
        guard = self.loader.index("else if (NumTowers < MAX_TOWERS) {")
        first_write = self.loader.index("*pTowerBasePtr++ = iChunkIdx;", guard)
        following_chunk_work = self.loader.index("rotatepoint(", guard)
        guarded_tower_decode = self.loader[guard:following_chunk_work]

        self.assertLess(guard, first_write)
        self.assertIn("++NumTowers;", guarded_tower_decode)
        self.assertEqual(self.loader.count("++NumTowers;"), 1)

    def test_sanitizer_soak_loads_more_than_the_table_can_hold(self) -> None:
        self.assertIn("SOAK_TOWER_INPUT_COUNT = MAX_TOWERS + 2", self.soak)
        self.assertIn("RollerEd_LoadTrackFile(pContext->szTowerLimitTrack", self.soak)
        self.assertIn("NumTowers != MAX_TOWERS", self.soak)
        self.assertIn("TowerSect[iChunk] != -1", self.soak)

    def test_soak_checks_every_retained_tower_field(self) -> None:
        for field in (
            "iChunkIdx",
            "iHOffset",
            "iVOffset",
            "iEnabled",
            "iTowerType",
        ):
            self.assertIn(f"TowerBase[iTower].{field}", self.soak)


if __name__ == "__main__":
    unittest.main()
