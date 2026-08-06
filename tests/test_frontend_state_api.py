import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
ROLLER = ROOT / "PROJECTS" / "ROLLER"
HEADER = ROLLER / "frontend.h"


class FrontendStateApiContractTests(unittest.TestCase):
    def test_header_exposes_fsm_backed_state_api(self) -> None:
        header = HEADER.read_text(encoding="utf-8")

        declarations = (
            "eFrontendState frontend_current_state(void);",
            "eFrontendState frontend_pending_state(void);",
            "void frontend_request_state(eFrontendState eState);",
        )
        for declaration in declarations:
            with self.subTest(declaration=declaration):
                if declaration not in header:
                    self.fail(f"missing frontend state API: {declaration}")

    def test_legacy_state_globals_are_removed(self) -> None:
        for path in ROLLER.glob("*.[ch]"):
            source = path.read_text(encoding="utf-8")
            for symbol in ("eFrontendCurrentState", "eFrontendNextState"):
                with self.subTest(path=path.name, symbol=symbol):
                    if symbol in source:
                        self.fail(f"{symbol} remains in {path.name}")


if __name__ == "__main__":
    unittest.main()
