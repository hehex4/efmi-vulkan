"""Runs the offline C++ tracker tests built alongside the layer."""

import os
import subprocess
import sys

from harness import DIST


if __name__ == "__main__":
    sys.exit(subprocess.run([os.path.join(DIST, "efmi_vk_tracker_test.exe")]).returncode)
