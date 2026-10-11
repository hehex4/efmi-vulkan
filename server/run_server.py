"""Entry point of the EFMI Vulkan probe MCP server: python server/run_server.py"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from efmi_vk.mcp_stdio import main  # noqa: E402

if __name__ == "__main__":
    sys.exit(main())
