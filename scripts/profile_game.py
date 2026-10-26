"""Samples the threads that call into the layer in a running process (the probe's `profile`, sampler.cpp)
and names where their time goes, with the build's PDBs for the layer and the exports for the rest.

    python scripts/profile_game.py PID [--seconds 10] [--hz 250] [--out build/profile-PID.json] [--pdb DIR ...]

Read-only on the process apart from the samples (each suspends one thread for a few microseconds). Prints
the functions most often at the top of a stack (exclusive) and anywhere in it (inclusive), per thread
and overall, and how much of the sampled time was inside the layer's modules. A stack whose top frame is
a kernel wait (ntdll Nt/ZwWait...) is a waiting thread and counted as such."""
from __future__ import annotations

import argparse
import ctypes
import ctypes.wintypes as wt
import json
import sys
from collections import Counter, defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "server"))
from efmi_vk.probe_client import ProbeClient  # noqa: E402

LAYER_MODULES = ("efmi_vk_inject.dll", "efmi_vk_layer.dll", "efmi_vk_backend")  # the layer (injected or registered), the module (any copy)
DRIVERS = ("nvoglv64", "nvgpucomp", "vulkan-1", "amdvlk", "igvk", "nvwgf2um", "win32u", "gdi32")
WAITS = ("NtWaitFor", "ZwWaitFor", "NtDelayExecution", "ZwDelayExecution", "NtRemoveIoCompletion", "ZwRemoveIoCompletion",
         "NtWaitForAlertByThreadId", "ZwWaitForAlertByThreadId", "NtSignalAndWaitForSingleObject")


class SYMBOL_INFOW(ctypes.Structure):
    _fields_ = [("SizeOfStruct", wt.ULONG), ("TypeIndex", wt.ULONG), ("Reserved", ctypes.c_uint64 * 2), ("Index", wt.ULONG),
                ("Size", wt.ULONG), ("ModBase", ctypes.c_uint64), ("Flags", wt.ULONG), ("Value", ctypes.c_uint64),
                ("Address", ctypes.c_uint64), ("Register", wt.ULONG), ("Scope", wt.ULONG), ("Tag", wt.ULONG),
                ("NameLen", wt.ULONG), ("MaxNameLen", wt.ULONG), ("Name", ctypes.c_wchar * 1024)]


class Symbols:
    """dbghelp over the modules the process had loaded, each at its own base, read from disk."""

    def __init__(self, modules: list[dict], pdb_dirs: list[Path]):
        self.dbghelp = ctypes.WinDLL("dbghelp")
        self.process = ctypes.c_void_p(0x5157)
        self.dbghelp.SymSetOptions(0x00000002 | 0x00000040)  # UNDNAME | LOAD_ANYTHING
        self.dbghelp.SymInitializeW.argtypes = [ctypes.c_void_p, wt.LPCWSTR, wt.BOOL]
        search = ";".join(str(p) for p in pdb_dirs)
        if not self.dbghelp.SymInitializeW(self.process, search or None, False):
            raise RuntimeError("SymInitialize failed")
        self.dbghelp.SymLoadModuleExW.restype = ctypes.c_uint64
        self.dbghelp.SymLoadModuleExW.argtypes = [ctypes.c_void_p, ctypes.c_void_p, wt.LPCWSTR, wt.LPCWSTR, ctypes.c_uint64,
                                                  wt.DWORD, ctypes.c_void_p, wt.DWORD]
        self.dbghelp.SymFromAddrW.argtypes = [ctypes.c_void_p, ctypes.c_uint64, ctypes.POINTER(ctypes.c_uint64), ctypes.POINTER(SYMBOL_INFOW)]
        self.modules = modules
        self.loaded = {}
        self.cache = {}

    def name(self, frame: str) -> str:
        if frame in self.cache:
            return self.cache[frame]
        index, offset = frame.split("+0x")
        if index == "?":
            self.cache[frame] = "?"
            return "?"
        module = self.modules[int(index)]
        path = module["path"]
        short = Path(path).name
        if index not in self.loaded:
            self.loaded[index] = self.dbghelp.SymLoadModuleExW(self.process, None, path, None, module["base"], module["size"], None, 0)
        text = "%s+0x%s" % (short, offset)
        if self.loaded[index]:
            info = SYMBOL_INFOW()
            info.SizeOfStruct = 88  # sizeof(SYMBOL_INFOW) in C, with its one-character name
            info.MaxNameLen = 1023
            displacement = ctypes.c_uint64(0)
            if self.dbghelp.SymFromAddrW(self.process, module["base"] + int(offset, 16), ctypes.byref(displacement), ctypes.byref(info)):
                text = "%s!%s" % (short, info.Name)
                # Far from the symbol: a module without a PDB named by its nearest export, not its function.
                if displacement.value > 0x1000:
                    text += "+0x%x?" % displacement.value
        self.cache[frame] = text
        return text


def in_layer(frame_name: str) -> bool:
    module = frame_name.split("!")[0].split("+")[0].lower()
    return module.startswith(LAYER_MODULES)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("pid", type=int)
    ap.add_argument("--seconds", type=float, default=10.0)
    ap.add_argument("--hz", type=int, default=250)
    ap.add_argument("--out", type=Path)
    ap.add_argument("--input", type=Path, help="name an earlier result instead of sampling")
    ap.add_argument("--pdb", type=Path, action="append", default=[], help="folders with the PDBs of the layer and the module")
    ap.add_argument("--top", type=int, default=40)
    args = ap.parse_args()
    if args.input:
        result = json.loads(args.input.read_text(encoding="utf-8"))
    else:
        with ProbeClient(args.pid) as client:
            result, _ = client.request("profile", milliseconds=int(args.seconds * 1000), hz=args.hz)
        out = args.out or ROOT / "build" / ("profile-%d.json" % args.pid)
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_text(json.dumps(result), encoding="utf-8")
        print("raw samples in %s" % out)
    pdbs = args.pdb or [ROOT / "layer" / "build", ROOT / "layer" / "build" / "module", ROOT / "dist"]
    symbols = Symbols(result["modules"], pdbs)
    total = result["samples"]
    print("%d samples over %d ms at %d Hz, %d threads, %d distinct stacks" % (
        total, result["milliseconds"], result["hz"], len(result["threads"]), result["distinct_stacks"]))
    per_thread = defaultdict(lambda: {"samples": 0, "waiting": 0, "layer": 0, "layer_waits": 0, "exclusive": Counter(), "inclusive": Counter()})
    overall_ex, overall_in, lock_waits = Counter(), Counter(), Counter()
    for stack in result["stacks"]:
        n, thread = stack["n"], stack["thread"]
        names = [symbols.name(f) for f in stack["frames"]]
        t = per_thread[thread]
        t["samples"] += n
        top = names[0] if names else "?"
        if any(w in top for w in WAITS):
            t["waiting"] += n
            # A wait the layer itself makes (a lock, a fence of its own): the innermost layer frame with
            # no driver or loader frame between it and the wait; a wait forwarded to the driver through a
            # hook (present, the application's fence waits) is the application's.
            at = next((i for i, x in enumerate(names) if in_layer(x)), None)
            if at is not None and not any(x.lower().startswith(DRIVERS) for x in names[:at]):
                t["layer_waits"] += n
                lock_waits[names[at]] += n
            continue
        if any(in_layer(x) for x in names):
            t["layer"] += n
        t["exclusive"][top] += n
        overall_ex[top] += n
        for x in set(names):
            t["inclusive"][x] += n
            overall_in[x] += n
    busy_total = sum(t["samples"] - t["waiting"] for t in per_thread.values())
    layer_total = sum(t["layer"] for t in per_thread.values())
    print("busy samples %d (%.0f%%), of them with a layer frame %d (%.0f%% of busy)" % (
        busy_total, 100.0 * busy_total / max(total, 1), layer_total, 100.0 * layer_total / max(busy_total, 1)))
    for thread, t in sorted(per_thread.items(), key=lambda kv: -(kv[1]["samples"] - kv[1]["waiting"])):
        busy = t["samples"] - t["waiting"]
        if not busy:
            continue
        print("\nthread %d: %d samples, busy %d, in the layer %d (%.0f%% of busy), waiting inside the layer %d" % (
            thread, t["samples"], busy, t["layer"], 100.0 * t["layer"] / busy, t["layer_waits"]))
        for name, n in t["exclusive"].most_common(12):
            print("   excl %5.1f%%  %s" % (100.0 * n / busy, name))
    if lock_waits:
        print("\nwaits inside the layer, by the innermost layer frame (all samples):")
        for name, n in lock_waits.most_common(15):
            print("  %5.1f%%  %s" % (100.0 * n / max(total, 1), name))
    print("\noverall exclusive (busy samples):")
    for name, n in overall_ex.most_common(args.top):
        print("  %5.1f%%  %s" % (100.0 * n / max(busy_total, 1), name))
    print("\noverall inclusive, layer functions (busy samples):")
    for name, n in [kv for kv in overall_in.most_common(400) if in_layer(kv[0])][:args.top]:
        print("  %5.1f%%  %s" % (100.0 * n / max(busy_total, 1), name))
    return 0


if __name__ == "__main__":
    sys.exit(main())
