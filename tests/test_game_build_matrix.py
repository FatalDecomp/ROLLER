import copy
import json
from pathlib import Path
import sys
import unittest


REPOSITORY_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPOSITORY_ROOT / "tools"))

import check_game_build_matrix as matrix  # noqa: E402


class GameBuildMatrixTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.contract = json.loads(
            (REPOSITORY_ROOT / ".github/game-build-targets.json").read_text(
                encoding="utf-8"
            )
        )
        cls.workflow = (REPOSITORY_ROOT / ".github/workflows/build.yml").read_text(
            encoding="utf-8"
        )
        cls.android_gradle = (REPOSITORY_ROOT / "android/app/build.gradle").read_text(
            encoding="utf-8"
        )
        cls.mise_config = (REPOSITORY_ROOT / "mise.toml").read_text(
            encoding="utf-8"
        )
        cls.build_wrapper = (REPOSITORY_ROOT / "scripts/build.py").read_text(
            encoding="utf-8"
        )

    def validate(self, **overrides):
        values = {
            "contract": self.contract,
            "workflow": self.workflow,
            "android_gradle": self.android_gradle,
            "mise_config": self.mise_config,
            "build_wrapper": self.build_wrapper,
        }
        values.update(overrides)
        return matrix.validate(**values)

    def test_repository_retains_six_game_build_targets(self) -> None:
        self.assertEqual(
            self.validate(),
            (6, 5, ["arm64-v8a", "x86_64"]),
        )

    def test_dropping_a_desktop_target_is_rejected(self) -> None:
        workflow = self.workflow.replace(
            "          - target: x86_64-windows\n"
            "            runs-on: ubuntu-latest\n"
            "            os: windows\n"
            "            arch: x86_64\n"
            '            ext: ".exe"\n',
            "",
        )
        with self.assertRaisesRegex(matrix.MatrixError, "desktop Zig matrix"):
            self.validate(workflow=workflow)

    def test_bypassing_the_canonical_zig_wrapper_is_rejected(self) -> None:
        workflow = self.workflow.replace("mise run build \\", "zig build \\", 1)
        with self.assertRaisesRegex(matrix.MatrixError, "canonical mise build task"):
            self.validate(workflow=workflow)

    def test_android_abi_drift_is_rejected(self) -> None:
        android_gradle = self.android_gradle.replace(
            "ndk { abiFilters 'arm64-v8a', 'x86_64' }",
            "ndk { abiFilters 'arm64-v8a' }",
        )
        with self.assertRaisesRegex(matrix.MatrixError, "Android ABIs"):
            self.validate(android_gradle=android_gradle)

    def test_contract_requires_exactly_six_unique_targets(self) -> None:
        contract = copy.deepcopy(self.contract)
        contract["targets"].pop()
        with self.assertRaisesRegex(matrix.MatrixError, "exactly six targets"):
            self.validate(contract=contract)


if __name__ == "__main__":
    unittest.main()
