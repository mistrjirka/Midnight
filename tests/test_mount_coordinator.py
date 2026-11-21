"""C++ controller owns target, requests capture, rejects stale/gallery points."""
from pathlib import Path
import subprocess
import tempfile
ROOT=Path(__file__).resolve().parents[1]
SRC=ROOT/"controller/firmware/src"
TEST=ROOT/"controller/tests"
def test_mount_coordinator():
 with tempfile.TemporaryDirectory(prefix="mount-coordinator-") as temp:
  out=Path(temp)/"test"
  model=Path(temp)/"model.o"
  subprocess.run(["gcc","-std=c99","-O2","-Wall","-Wextra","-Werror",
    "-c",str(SRC/"mount_model_bridge.c"),"-o",str(model)],check=True)
  subprocess.run(["g++","-std=c++17","-O2","-Wall","-Wextra","-Werror",
    str(TEST/"test_mount_coordinator.cpp"),
    str(SRC/"mount_coordinator.cpp"),
    str(SRC/"pointing_wire.cpp"),str(model),
    "-o",str(out)],check=True)
  subprocess.run([str(out)],capture_output=True,check=True)
