"""Production motor backend must remain fail-closed until both axes are wired."""
from pathlib import Path
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]
SRC=ROOT/"controller/firmware/src"
TEST=ROOT/"controller/tests/test_goto_motors.cpp"

def test_goto_motor_not_armed_without_hardware():
    with tempfile.TemporaryDirectory(prefix="midnight-goto-motors-") as path:
        output=Path(path)/"motors_test"
        subprocess.run(["g++","-std=c++17","-O2","-Wall","-Wextra","-Werror",
                        str(TEST),str(SRC/"goto_motors.cpp"),"-o",str(output)],check=True)
        result=subprocess.run([str(output)],capture_output=True,text=True,check=True)
        assert "fail closed" in result.stdout
