"""A small sampling profiler for a process of this project (the replay, the sandbox programs).

Waits for a process with the given image name (or takes --pid), then about every millisecond
suspends each of its threads, reads the thread's context, walks its stack with dbghelp and resumes
it. Frames are named through the modules' PDBs (the project builds with /Zi /DEBUG). At the end it
prints the functions by self samples and by inclusive samples, idle threads (waits in ntdll and the
like) left out. Windows only; no admin rights; nothing is written into the target.

    py -B scripts/sample_profile.py --image replay_gpu.exe --nth 2 --seconds 20 > profile.txt

`--nth 2` takes the second process of that name (the replay runs the disabled case first).
"""
import argparse
import collections
import ctypes
import ctypes.wintypes as wt
import sys
import time

k32 = ctypes.WinDLL("kernel32", use_last_error=True)
dbghelp = ctypes.WinDLL("dbghelp", use_last_error=True)

TH32CS_SNAPPROCESS, TH32CS_SNAPTHREAD = 0x2, 0x4
PROCESS_ALL = 0x0410 | 0x0010 | 0x0400 | 0x1000  # query, vm read, query limited
THREAD_RIGHTS = 0x0002 | 0x0008 | 0x0040  # suspend/resume, get context, query
CONTEXT_FULL = 0x10000B  # AMD64 control | integer | floating point
IMAGE_FILE_MACHINE_AMD64 = 0x8664


class PROCESSENTRY32W(ctypes.Structure):
    _fields_ = [("dwSize", wt.DWORD), ("cntUsage", wt.DWORD), ("th32ProcessID", wt.DWORD), ("th32DefaultHeapID", ctypes.c_void_p),
                ("th32ModuleID", wt.DWORD), ("cntThreads", wt.DWORD), ("th32ParentProcessID", wt.DWORD), ("pcPriClassBase", ctypes.c_long),
                ("dwFlags", wt.DWORD), ("szExeFile", ctypes.c_wchar * 260)]


class THREADENTRY32(ctypes.Structure):
    _fields_ = [("dwSize", wt.DWORD), ("cntUsage", wt.DWORD), ("th32ThreadID", wt.DWORD), ("th32OwnerProcessID", wt.DWORD),
                ("tpBasePri", ctypes.c_long), ("tpDeltaPri", ctypes.c_long), ("dwFlags", wt.DWORD)]


class ADDRESS64(ctypes.Structure):
    _fields_ = [("Offset", ctypes.c_uint64), ("Segment", ctypes.c_uint16), ("Mode", ctypes.c_uint32)]


class KDHELP64(ctypes.Structure):
    _fields_ = [("Thread", ctypes.c_uint64), ("ThCallbackStack", wt.DWORD), ("ThCallbackBStore", wt.DWORD), ("NextCallback", wt.DWORD),
                ("FramePointer", wt.DWORD), ("KiCallUserMode", ctypes.c_uint64), ("KeUserCallbackDispatcher", ctypes.c_uint64),
                ("SystemRangeStart", ctypes.c_uint64), ("KiUserExceptionDispatcher", ctypes.c_uint64), ("StackBase", ctypes.c_uint64),
                ("StackLimit", ctypes.c_uint64), ("BuildVersion", wt.DWORD), ("RetpolineStubFunctionTableSize", wt.DWORD),
                ("RetpolineStubFunctionTable", ctypes.c_uint64), ("RetpolineStubOffset", wt.DWORD), ("RetpolineStubSize", wt.DWORD),
                ("Reserved0", ctypes.c_uint64 * 2)]


class STACKFRAME64(ctypes.Structure):
    _fields_ = [("AddrPC", ADDRESS64), ("AddrReturn", ADDRESS64), ("AddrFrame", ADDRESS64), ("AddrStack", ADDRESS64), ("AddrBStore", ADDRESS64),
                ("FuncTableEntry", ctypes.c_void_p), ("Params", ctypes.c_uint64 * 4), ("Far", wt.BOOL), ("Virtual", wt.BOOL),
                ("Reserved", ctypes.c_uint64 * 3), ("KdHelp", KDHELP64)]


class SYMBOL_INFOW(ctypes.Structure):
    _fields_ = [("SizeOfStruct", wt.ULONG), ("TypeIndex", wt.ULONG), ("Reserved", ctypes.c_uint64 * 2), ("Index", wt.ULONG), ("Size", wt.ULONG),
                ("ModBase", ctypes.c_uint64), ("Flags", wt.ULONG), ("Value", ctypes.c_uint64), ("Address", ctypes.c_uint64), ("Register", wt.ULONG),
                ("Scope", wt.ULONG), ("Tag", wt.ULONG), ("NameLen", wt.ULONG), ("MaxNameLen", wt.ULONG), ("Name", ctypes.c_wchar * 512)]


# AMD64 CONTEXT: 1232 bytes, 16-byte aligned; ContextFlags at 0x30, Rsp 0x98, Rbp 0xA0, Rip 0xF8.
CONTEXT_SIZE = 1232


def aligned_context():
    raw = ctypes.create_string_buffer(CONTEXT_SIZE + 16)
    address = (ctypes.addressof(raw) + 15) & ~15
    return raw, address


def find_processes(image):
    snap = k32.CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0)
    entry = PROCESSENTRY32W(); entry.dwSize = ctypes.sizeof(entry)
    found = []
    ok = k32.Process32FirstW(snap, ctypes.byref(entry))
    while ok:
        if entry.szExeFile.lower() == image.lower():
            found.append(entry.th32ProcessID)
        ok = k32.Process32NextW(snap, ctypes.byref(entry))
    k32.CloseHandle(snap)
    return found


def threads_of(pid):
    snap = k32.CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0)
    entry = THREADENTRY32(); entry.dwSize = ctypes.sizeof(entry)
    found = []
    ok = k32.Thread32First(snap, ctypes.byref(entry))
    while ok:
        if entry.th32OwnerProcessID == pid:
            found.append(entry.th32ThreadID)
        ok = k32.Thread32Next(snap, ctypes.byref(entry))
    k32.CloseHandle(snap)
    return found


IDLE = ("NtWaitFor", "NtDelayExecution", "NtRemoveIoCompletion", "ZwWaitFor", "NtUserMsgWaitForMultipleObjects", "NtWaitForAlertByThreadId",
        "NtUserGetMessage", "RtlSleepConditionVariable", "SleepConditionVariable", "NtWaitForWorkViaWorkerFactory", "NtSignalAndWaitForSingleObject",
        "NtUserWaitMessage", "NtDeviceIoControlFile", "NtReadFile", "ZwFsControlFile", "NtFsControlFile", "ConnectNamedPipe",
        "ZwReadFile", "ZwWaitFor", "NtWaitForMultipleObjects")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--image", default="replay_gpu.exe")
    ap.add_argument("--pid", type=int)
    ap.add_argument("--nth", type=int, default=1, help="the nth process of that image name to appear")
    ap.add_argument("--seconds", type=float, default=30.0)
    ap.add_argument("--interval", type=float, default=0.001)
    ap.add_argument("--depth", type=int, default=48)
    ap.add_argument("--top", type=int, default=45)
    ap.add_argument("--wait", type=float, default=600.0, help="seconds to wait for the process")
    ap.add_argument("--dump", help="write every busy stack (innermost first) with its count to this JSON file")
    ap.add_argument("--delay", type=float, default=0.0, help="seconds to let the process run before sampling (start-up, first-minute texts)")
    args = ap.parse_args()

    pid = args.pid
    if not pid:
        seen = []
        deadline = time.time() + args.wait
        while time.time() < deadline and not pid:
            for candidate in find_processes(args.image):
                if candidate not in seen:
                    seen.append(candidate)
            if len(seen) >= args.nth:
                pid = seen[args.nth - 1]
                break
            time.sleep(0.05)
        if not pid:
            print("no process %s (#%d) appeared" % (args.image, args.nth)); return 1
    process = k32.OpenProcess(PROCESS_ALL, False, pid)
    if not process:
        print("cannot open process %d (%d)" % (pid, ctypes.get_last_error())); return 1
    dbghelp.SymSetOptions(0x2 | 0x4 | 0x10 | 0x200)  # undecorate, deferred loads, load lines off, fail critical errors
    dbghelp.SymInitializeW.argtypes = [wt.HANDLE, wt.LPCWSTR, wt.BOOL]
    if not dbghelp.SymInitializeW(process, None, True):
        print("SymInitialize failed (%d)" % ctypes.get_last_error()); return 1
    dbghelp.SymFromAddrW.argtypes = [wt.HANDLE, ctypes.c_uint64, ctypes.POINTER(ctypes.c_uint64), ctypes.POINTER(SYMBOL_INFOW)]
    dbghelp.SymGetModuleBase64.argtypes = [wt.HANDLE, ctypes.c_uint64]
    dbghelp.SymGetModuleBase64.restype = ctypes.c_uint64
    dbghelp.SymFunctionTableAccess64.argtypes = [wt.HANDLE, ctypes.c_uint64]
    dbghelp.SymFunctionTableAccess64.restype = ctypes.c_void_p
    FTA = ctypes.CFUNCTYPE(ctypes.c_void_p, wt.HANDLE, ctypes.c_uint64)
    GMB = ctypes.CFUNCTYPE(ctypes.c_uint64, wt.HANDLE, ctypes.c_uint64)
    fta = FTA(lambda h, a: dbghelp.SymFunctionTableAccess64(h, a))
    gmb = GMB(lambda h, a: dbghelp.SymGetModuleBase64(h, a))
    dbghelp.StackWalk64.argtypes = [wt.DWORD, wt.HANDLE, wt.HANDLE, ctypes.POINTER(STACKFRAME64), ctypes.c_void_p,
                                    ctypes.c_void_p, FTA, GMB, ctypes.c_void_p]
    k32.GetThreadContext.argtypes = [wt.HANDLE, ctypes.c_void_p]
    k32.OpenThread.restype = wt.HANDLE

    names = {}

    def name_of(address):
        cached = names.get(address)
        if cached is not None:
            return cached
        info = SYMBOL_INFOW(); info.SizeOfStruct = 88; info.MaxNameLen = 511  # sizeof(SYMBOL_INFOW) with one name character
        displacement = ctypes.c_uint64(0)
        if dbghelp.SymFromAddrW(process, address, ctypes.byref(displacement), ctypes.byref(info)):
            value = info.Name
        else:
            base = dbghelp.SymGetModuleBase64(process, address)
            module = ctypes.create_unicode_buffer(260)
            if base and k32.K32GetModuleBaseNameW(process, ctypes.c_void_p(base), module, 260):
                value = "%s+%x" % (module.value, address - base)
            else:
                value = "?+%x" % address
        names[address] = value
        return value

    self_counts = collections.Counter()
    inclusive = collections.Counter()
    stacks = collections.Counter()
    full = collections.Counter()
    samples = busy = 0
    raw, context = aligned_context()
    if args.delay:
        time.sleep(args.delay)
    began = time.time()
    last_refresh = 0.0
    handles = {}
    cpu_seen = {}
    active = set()
    k32.GetThreadTimes.argtypes = [wt.HANDLE, ctypes.POINTER(wt.FILETIME), ctypes.POINTER(wt.FILETIME), ctypes.POINTER(wt.FILETIME), ctypes.POINTER(wt.FILETIME)]

    def cpu_time(handle):
        times = [wt.FILETIME() for _ in range(4)]
        if not k32.GetThreadTimes(handle, *[ctypes.byref(x) for x in times]):
            return 0
        return sum((x.dwHighDateTime << 32) | x.dwLowDateTime for x in times[2:])
    while time.time() - began < args.seconds:
        if time.time() - last_refresh > 0.5:
            dbghelp.SymRefreshModuleList(process)
            last_refresh = time.time()
            exit_code = wt.DWORD()
            k32.GetExitCodeProcess(process, ctypes.byref(exit_code))
            if exit_code.value != 259:
                break
            for tid in threads_of(pid):
                if tid not in handles:
                    h = k32.OpenThread(THREAD_RIGHTS, False, tid)
                    if h:
                        handles[tid] = h
            # Only the threads that ran in the last half second are sampled (the others wait).
            active = set()
            for tid, h in handles.items():
                now_cpu = cpu_time(h)
                if now_cpu - cpu_seen.get(tid, 0) > 50000:  # 5 ms of CPU in 100 ns units
                    active.add(tid)
                cpu_seen[tid] = now_cpu
        for tid, thread in list(handles.items()):
            if tid not in active:
                continue
            if k32.SuspendThread(thread) == 0xFFFFFFFF:
                k32.CloseHandle(thread); del handles[tid]; continue
            try:
                ctypes.memset(context, 0, CONTEXT_SIZE)
                ctypes.c_uint32.from_address(context + 0x30).value = CONTEXT_FULL
                if not k32.GetThreadContext(thread, context):
                    continue
                frame = STACKFRAME64()
                frame.AddrPC.Offset = ctypes.c_uint64.from_address(context + 0xF8).value; frame.AddrPC.Mode = 3
                frame.AddrFrame.Offset = ctypes.c_uint64.from_address(context + 0xA0).value; frame.AddrFrame.Mode = 3
                frame.AddrStack.Offset = ctypes.c_uint64.from_address(context + 0x98).value; frame.AddrStack.Mode = 3
                chain = []
                for _ in range(args.depth):
                    if not dbghelp.StackWalk64(IMAGE_FILE_MACHINE_AMD64, process, thread, ctypes.byref(frame), ctypes.c_void_p(context), None, fta, gmb, None):
                        break
                    if not frame.AddrPC.Offset:
                        break
                    chain.append(frame.AddrPC.Offset)
            finally:
                k32.ResumeThread(thread)
            samples += 1
            if not chain:
                continue
            top = name_of(chain[0])
            if any(top.startswith(prefix) for prefix in IDLE) or (len(chain) > 1 and any(name_of(chain[1]).startswith(p) for p in IDLE)):
                continue
            busy += 1
            self_counts[top] += 1
            named = [name_of(a) for a in chain]
            for n in set(named):
                inclusive[n] += 1
            stacks[" < ".join(named[:6])] += 1
            if args.dump:
                full[tuple(named)] += 1
        time.sleep(args.interval)
    for h in handles.values():
        k32.CloseHandle(h)
    print("pid %d: %d samples, %d busy, %.1f s" % (pid, samples, busy, time.time() - began))
    print("\n== self ==")
    for n, c in self_counts.most_common(args.top):
        print("%6.2f%%  %6d  %s" % (100.0 * c / max(1, busy), c, n[:150]))
    print("\n== inclusive ==")
    for n, c in inclusive.most_common(args.top):
        print("%6.2f%%  %6d  %s" % (100.0 * c / max(1, busy), c, n[:150]))
    if args.dump:
        import json
        with open(args.dump, "w", encoding="utf-8") as out:
            json.dump([[count, list(stack)] for stack, count in full.most_common()], out)
    print("\n== stacks (top six frames) ==")
    for n, c in stacks.most_common(25):
        print("%6.2f%%  %6d  %s" % (100.0 * c / max(1, busy), c, n[:400]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
