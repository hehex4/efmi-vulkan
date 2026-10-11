"""Runs the offline C++ tracker tests built alongside the layer."""

import os
import subprocess
import sys

from harness import DIST, test_dir


if __name__ == "__main__":
    # The tracker links util.cpp, whose default log is beside the executable.
    # Always override it so testing dist/ cannot append to the historical game log.
    folder = test_dir("native-")
    env = dict(os.environ, EFMI_VK_LOG=os.path.join(folder, "tracker.log"))
    sys.exit(subprocess.run([os.path.join(DIST, "efmi_vk_tracker_test.exe")], env=env, cwd=folder).returncode)
