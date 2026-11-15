"""Headless actual overlap and a bounded global-chain counterexample, not game proof."""
import os
from pathlib import Path
import subprocess
from harness import Checks, DIST, INJECT_DLL, inject_env, layer_env, test_dir


def main():
    checks = Checks("queue overlap")
    validation = os.environ.get("EFMI_VK_VALIDATION_DIR", "F:/vulkan/validation-1.4.321.0/bin")
    if not Path(validation, "VkLayer_khronos_validation.json").is_file():
        raise RuntimeError("validation layer missing")
    for entry in ("none", "layer", "inject"):
        folder = Path(test_dir("queue-overlap-"))
        log = folder / "layer.log"
        env = layer_env(str(log)) if entry == "layer" else inject_env(str(log))
        env.update(VK_LAYER_PATH=os.pathsep.join([DIST, validation]), VK_LAYER_VALIDATE_SYNC="1",
                   VK_LAYER_DUPLICATE_MESSAGE_LIMIT="0", XDG_CACHE_HOME=str(folder),
                   TMPDIR=str(folder), TEMP=str(folder), TMP=str(folder))
        args = [str(Path(DIST, "efmi_vk_queue_overlap.exe"))]
        if entry == "inject": args += ["--inject", INJECT_DLL]
        result = subprocess.run(args, cwd=folder, env=env, capture_output=True, text=True, timeout=25)
        output = result.stdout + result.stderr
        (folder / "stdout.txt").write_text(output, encoding="utf-8")
        print(output, end="")
        checks.equal(result.returncode, 0, entry + ": bounded normal exit")
        checks.check("validation: errors=0 warnings=0" in output, entry + ": clean validation")
        checks.check("global-chain counterexample: pending consumer observed; readback=00000000" in output,
                     entry + ": added global chain stalls and recovers with initial data")
        checks.check("readiness gates: producer released before earlier consumer; readback=71a24b9d" in output,
                     entry + ": ready producer executes ahead of its waiting consumer")
        checks.check("EXPECTED BINARY CANARY:" in output and
                     "binary forwarding: missing gate detected; complete dependency chain accepted" in output,
                     entry + ": binary consumer waits for submission of the complete producer dependency chain")
        checks.check((f"entry={entry}" in log.read_text()) if entry != "none" and log.exists() else
                     (entry == "none" and not log.exists()), entry + ": intended loader path")
        print("artifacts:", folder)
    return checks.finish()


if __name__ == "__main__":
    raise SystemExit(main())
