"""Headless ordered GPU work inside preserved classic/dynamic rendering."""
from pathlib import Path
import os
import subprocess
from harness import ROOT, DIST, inject_env


def main():
    root = Path(ROOT) / "build" / "native-prepared-pass"
    root.mkdir(parents=True, exist_ok=True)
    validation = Path(os.environ.get("EFMI_VK_VALIDATION_DIR", "F:/vulkan/validation-1.4.321.0/bin"))
    if not (validation / "VkLayer_khronos_validation.json").is_file():
        raise RuntimeError("Required validation layer is unavailable")
    env = inject_env(str(root / "unexpected-injection.log"))
    env.update(VK_LAYER_PATH=str(validation), VK_LAYER_VALIDATE_SYNC="1", VK_LAYER_DUPLICATE_MESSAGE_LIMIT="0",
               XDG_CACHE_HOME=str(root), TMPDIR=str(root), TEMP=str(root), TMP=str(root))
    result = subprocess.run([str(Path(DIST) / "efmi_vk_native_prepared_gpu.exe"), str(root)],
                            cwd=root, env=env, capture_output=True, encoding="utf-8", errors="replace", timeout=60)
    text = result.stdout + result.stderr
    (root / "result.out").write_text(text, encoding="utf-8")
    print(text, end="")
    return result.returncode


if __name__ == "__main__":
    raise SystemExit(main())
