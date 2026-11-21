"""Run the production GoTo state machine with two simulated skewed motors.

All motion is simulated. The actual motor port stays fail-closed.
"""
from pathlib import Path
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]
SRC=ROOT/"controller"/"firmware"/"src"
TEST=ROOT/"controller"/"tests"

def test_closed_loop_calibration_and_iterative_goto():
    with tempfile.TemporaryDirectory(prefix="midnight-goto-loop-") as work:
        work=Path(work)
        model=work/"model.o"
        binary=work/"test_goto"
        subprocess.run([
            "gcc","-std=c99","-O2","-Wall","-Wextra","-Werror",
            "-c",str(SRC/"mount_model_bridge.c"),"-o",str(model)
        ],check=True)
        subprocess.run([
            "g++","-std=c++17","-O2","-Wall","-Wextra","-Werror",
            str(TEST/"test_goto_loop.cpp"),str(SRC/"goto_loop.cpp"),
            str(SRC/"pointing_wire.cpp"),str(model),
            "-o",str(binary)
        ],check=True)
        completed=subprocess.run([str(binary)],capture_output=True,text=True,check=True)
        import re
        count=re.search(r"in (\d+) corrections, (\d+) simulated pulses",completed.stdout)
        assert count,completed.stdout
        assert 1 <= int(count.group(1)) <= 40
        assert int(count.group(2)) >= int(count.group(1))+4
        assert "STOP" in completed.stdout
