"""Names the module+offset frames of the layer's crash log lines ("crash: ...", util.cpp install_crash_log) with
the build's PDBs through dbghelp. The DLLs must be the ones that ran.

    python scripts/symbolize_crash.py LOG DLL_DIR [PDB_DIR...]

Typically LOG is dist/efmi_vk_layer.log or a test's layer.log, DLL_DIR is dist and the PDB directories are
layer/build and layer/build/module."""
import ctypes
import ctypes.wintypes as wt
import re
import sys
from pathlib import Path

log, dll_dir = Path(sys.argv[1]), Path(sys.argv[2])
pdb_dirs = [Path(p) for p in sys.argv[3:]]
dbghelp = ctypes.WinDLL("dbghelp")
process = ctypes.c_void_p(0x1234)
dbghelp.SymSetOptions(0x00000002 | 0x00000010 | 0x00000040)  # UNDNAME | LOAD_LINES | LOAD_ANYTHING (deferred loads left the PDB unread)
dbghelp.SymInitializeW.argtypes = [ctypes.c_void_p, wt.LPCWSTR, wt.BOOL]
search = ";".join(str(p) for p in pdb_dirs + [dll_dir])
assert dbghelp.SymInitializeW(process, search, False), ctypes.get_last_error()
dbghelp.SymLoadModuleExW.restype = ctypes.c_uint64
dbghelp.SymLoadModuleExW.argtypes = [ctypes.c_void_p, ctypes.c_void_p, wt.LPCWSTR, wt.LPCWSTR, ctypes.c_uint64, wt.DWORD, ctypes.c_void_p, wt.DWORD]


class SYMBOL_INFOW(ctypes.Structure):
    _fields_ = [("SizeOfStruct", wt.ULONG), ("TypeIndex", wt.ULONG), ("Reserved", ctypes.c_uint64 * 2), ("Index", wt.ULONG), ("Size", wt.ULONG),
                ("ModBase", ctypes.c_uint64), ("Flags", wt.ULONG), ("Value", ctypes.c_uint64), ("Address", ctypes.c_uint64), ("Register", wt.ULONG),
                ("Scope", wt.ULONG), ("Tag", wt.ULONG), ("NameLen", wt.ULONG), ("MaxNameLen", wt.ULONG), ("Name", ctypes.c_wchar * 1024)]


class IMAGEHLP_LINEW64(ctypes.Structure):
    _fields_ = [("SizeOfStruct", wt.DWORD), ("Key", ctypes.c_void_p), ("LineNumber", wt.DWORD), ("FileName", wt.LPWSTR), ("Address", ctypes.c_uint64)]


dbghelp.SymFromAddrW.argtypes = [ctypes.c_void_p, ctypes.c_uint64, ctypes.POINTER(ctypes.c_uint64), ctypes.POINTER(SYMBOL_INFOW)]
dbghelp.SymGetLineFromAddrW64.argtypes = [ctypes.c_void_p, ctypes.c_uint64, ctypes.POINTER(wt.DWORD), ctypes.POINTER(IMAGEHLP_LINEW64)]
bases = {}
base_next = 0x10000000


def base_of(name: str):
    global base_next
    if name in bases:
        return bases[name]
    path = dll_dir / name
    if not path.is_file():
        bases[name] = 0; return 0
    base = dbghelp.SymLoadModuleExW(process, None, str(path), None, base_next, 0, None, 0)
    if not base:
        bases[name] = 0; return 0
    bases[name] = base; base_next += 0x10000000
    return base


for line in log.read_text(encoding="utf-8", errors="replace").splitlines():
    if "crash:" not in line and "exception:" not in line:
        continue
    def name(m):
        module, offset = m.group(1), int(m.group(2), 16)
        base = base_of(module)
        if not base:
            return m.group(0)
        info = SYMBOL_INFOW(); info.SizeOfStruct = 88; info.MaxNameLen = 1024
        disp = ctypes.c_uint64()
        text = m.group(0)
        if dbghelp.SymFromAddrW(process, base + offset, ctypes.byref(disp), ctypes.byref(info)):
            text += " = %s+0x%x" % (info.Name, disp.value)
        line_info = IMAGEHLP_LINEW64(); line_info.SizeOfStruct = ctypes.sizeof(line_info); d32 = wt.DWORD()
        if dbghelp.SymGetLineFromAddrW64(process, base + offset, ctypes.byref(d32), ctypes.byref(line_info)):
            text += " (%s:%d)" % (Path(line_info.FileName).name, line_info.LineNumber)
        return text
    print(re.sub(r"([A-Za-z0-9_.\-]+\.(?:dll|exe))\+0x([0-9a-fA-F]+)", name, line))
