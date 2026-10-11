"""Builds layer and sandbox, then runs the whole regression suite.

    python sandbox/run_all.py [--no-build]

Set EFMI_VK_DIST to build and test in another directory than dist/, for example
while a running game holds the DLL in dist/.

The layer is enabled only inside the child processes the tests start. Nothing is
registered system-wide and the game is not involved.
"""

from __future__ import annotations

import os
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HERE = os.path.dirname(os.path.abspath(__file__))

STEPS = [
    ("text check", [os.path.join(ROOT, "scripts", "check_text.py")]),
    ("unit tests", [os.path.join(HERE, "unit_test.py")]),
    ("native tracker tests", [os.path.join(HERE, "native_test.py")]),
    ("probe end to end", [os.path.join(HERE, "probe_test.py")]),
    ("modes", [os.path.join(HERE, "modes_test.py")]),
    ("many-draw timing", [os.path.join(HERE, "timing_test.py")]),
    ("Vulkan validation", [os.path.join(HERE, "validation_test.py")]),
    ("MCP end to end", [os.path.join(HERE, "mcp_test.py")]),
    ("probe evidence", [os.path.join(HERE, "evidence_test.py")]),
]


def main() -> int:
    steps = list(STEPS)
    if "--no-build" not in sys.argv[1:]:
        build = [os.path.join(ROOT, "scripts", "build.py")]
        # A running process may hold the DLL in dist/. With EFMI_VK_DIST set, build and
        # test in that directory instead.
        if os.environ.get("EFMI_VK_DIST"):
            build += ["--out", os.environ["EFMI_VK_DIST"]]
        steps.insert(0, ("build", build))
    failed = []
    for name, args in steps:
        start = time.monotonic()
        proc = subprocess.run([sys.executable, "-B"] + args, cwd=HERE, capture_output=True, text=True, errors="replace")
        elapsed = time.monotonic() - start
        output = (proc.stdout or "") + (proc.stderr or "")
        print("[%s] %s (%.1fs)" % ("ok" if proc.returncode == 0 else "FAILED", name, elapsed))
        for line in output.splitlines():
            if proc.returncode != 0 or "checks passed" in line or line.startswith("built ") or "clean" in line:
                print("    " + line)
        if proc.returncode != 0:
            failed.append(name)
            if name == "build":
                break
    print("run_all: %s" % ("all steps passed" if not failed else "FAILED: " + ", ".join(failed)))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
