"""H-bridge drive-state contract and local deadline, no GPIO required."""
from pathlib import Path
import subprocess
import tempfile
ROOT=Path(__file__).resolve().parents[1]
SRC=ROOT/"controller/firmware/src"
TEST=ROOT/"controller/tests"
def test_hbridge_truth_table_and_timeout():
 with tempfile.TemporaryDirectory(prefix="midnight-hbridge-") as tmp:
  binary=Path(tmp)/"hbridge"
  subprocess.run(["g++","-std=c++17","-Wall","-Wextra","-Werror",
    str(TEST/"test_axis2_pulse_logic.cpp"),
    str(SRC/"axis2_pulse_logic.cpp"),"-o",str(binary)],check=True)
  subprocess.run([str(binary)],check=True,capture_output=True)
