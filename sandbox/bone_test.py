"""Run the CPU rolling-buffer skeleton tests; no game files or Vulkan process required."""
import os
import subprocess
import sys

from harness import DIST

if __name__ == "__main__":
    sys.exit(subprocess.run([os.path.join(DIST, "efmi_vk_bone_test.exe")]).returncode)
