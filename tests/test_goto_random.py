"""240 deterministic random camera orientations and motor geometries.

Includes J2000 RA wrap, reversed polarities, tracking on/off, unknown speeds,
skewed axes and up to tens of degrees of sky travel. Does not move hardware.
"""
from pathlib import Path
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]
SRC=ROOT/"controller/firmware/src"
TEST=ROOT/"controller/tests/test_goto_random.cpp"

def test_random_camera_pointings_and_axis_orientations():
    with tempfile.TemporaryDirectory(prefix="midnight-random-goto-") as temp:
        model=Path(temp)/"model.o"
        output=Path(temp)/"random_goto"
        subprocess.run([
            "gcc","-std=c99","-O2","-Wall","-Wextra","-Werror",
            "-c",str(SRC/"mount_model_bridge.c"),"-o",str(model)
        ],check=True)
        subprocess.run([
            "g++","-std=c++17","-O2","-Wall","-Wextra","-Werror",
            str(TEST),str(SRC/"goto_loop.cpp"),
            str(SRC/"pointing_wire.cpp"),str(model),"-o",str(output)
        ],check=True)
        result=subprocess.run([str(output)],capture_output=True,text=True,check=True)
        assert "RANDOM GO-TO 240/240 PASS" in result.stdout
