"""Compile and run ESP32 protocol tests with the production C++ decoder."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
APP = ROOT / "controller" / "firmware" / "src"
TESTS = ROOT / "controller" / "tests"


def test_esp32_ptp_reassembly_and_pointing():
    with tempfile.TemporaryDirectory(prefix="midnight-esp32-cpp-") as tmp:
        for name, sources in (
            ("pointing", [TESTS / "test_pointing_wire.cpp", APP / "pointing_wire.cpp"]),
            ("ptp", [
                TESTS / "test_ptp_wire.cpp", APP / "ptp_wire.cpp",
                APP / "pointing_wire.cpp",
            ]),
        ):
            path = Path(tmp) / name
            subprocess.run(
                ["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-O2",
                 *map(str, sources), "-o", str(path)],
                check=True,
            )
            subprocess.run([str(path)], check=True, capture_output=True)
