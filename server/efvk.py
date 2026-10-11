"""Command-line access to the probe tools.

    python server/efvk.py status
    python server/efvk.py capture_frame
    python server/efvk.py list_draws kind=indexed limit=20
    python server/efvk.py match_mod path="D:\\...\\Mods\\some mod"

The first argument is a tool name without the ``efvk_`` prefix; the rest are
``key=value`` arguments. Several tools can be chained with ``--`` and share one
session, so a capture stays available for the tools after it:

    python server/efvk.py capture_frame -- match_mod path=mod.ini
"""

import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from efmi_vk.tools import TOOLS, Session, ToolError  # noqa: E402


def parse_value(text: str):
    if text in ("true", "false"):
        return text == "true"
    try:
        return int(text, 0)
    except ValueError:
        return text


def main(argv) -> int:
    if not argv or argv[0] in ("-h", "--help"):
        print(__doc__)
        print("tools: " + ", ".join(t["name"][5:] for t in TOOLS))
        return 0
    commands = [[]]
    for a in argv:
        if a == "--":
            commands.append([])
        else:
            commands[-1].append(a)
    session = Session()
    status = 0
    try:
        for cmd in commands:
            if not cmd:
                continue
            name = cmd[0] if cmd[0].startswith("efvk_") else "efvk_" + cmd[0]
            args = {}
            for item in cmd[1:]:
                key, sep, value = item.partition("=")
                if not sep:
                    print("argument %r is not key=value" % item)
                    return 2
                args[key] = parse_value(value)
            try:
                print(json.dumps(session.call(name, args), indent=2, ensure_ascii=True))
            except ToolError as e:
                print("error: %s" % e)
                status = 1
    finally:
        session.close()
    return status


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
