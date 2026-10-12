"""Runs the offline C++ tracker tests built alongside the layer."""

import os
import subprocess
import sys

from harness import DIST, PINNED_SETTINGS, test_dir


if __name__ == "__main__":
    # The tracker links util.cpp, whose default log is beside the executable.
    # Always override it so testing dist/ cannot append to the historical game log.
    folder = test_dir("native-")
    # It also reads the settings file beside the executable; pin the defaults against whatever
    # dist/efmi_vk.cfg holds for the next game session.
    env = dict(os.environ, EFMI_VK_LOG=os.path.join(folder, "tracker.log"), EFMI_VK_IMAGE_HASH="1", **PINNED_SETTINGS)
    sys.exit(subprocess.run([os.path.join(DIST, "efmi_vk_tracker_test.exe")], env=env, cwd=folder).returncode)
