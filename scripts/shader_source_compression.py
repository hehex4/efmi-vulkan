"""Windows buffer-mode XPRESS_HUFF for bounded, lazy original shader evidence."""
import ctypes


def unpack(data, expected):
    if not 32 <= expected <= 4 * 1024 * 1024:
        raise ValueError('Invalid source expansion budget')
    api = ctypes.WinDLL('cabinet', use_last_error=True)
    pointer, size = ctypes.c_void_p, ctypes.c_size_t
    api.CreateDecompressor.argtypes = [ctypes.c_ulong, pointer, ctypes.POINTER(pointer)]
    api.Decompress.argtypes = [pointer, pointer, size, pointer, size, ctypes.POINTER(size)]
    api.CloseDecompressor.argtypes = [pointer]
    handle = pointer()
    if not api.CreateDecompressor(4, None, ctypes.byref(handle)):
        raise ctypes.WinError(ctypes.get_last_error())
    try:
        output, written = ctypes.create_string_buffer(expected), size()
        if not api.Decompress(handle, data, len(data), output, expected, ctypes.byref(written)) or written.value != expected:
            raise ValueError('Invalid compressed source shader')
        return output.raw
    finally:
        api.CloseDecompressor(handle)


def pack(data):
    api = ctypes.WinDLL('cabinet', use_last_error=True)
    pointer, size = ctypes.c_void_p, ctypes.c_size_t
    api.CreateCompressor.argtypes = [ctypes.c_ulong, pointer, ctypes.POINTER(pointer)]
    api.Compress.argtypes = [pointer, pointer, size, pointer, size, ctypes.POINTER(size)]
    api.CloseCompressor.argtypes = [pointer]
    handle = pointer()
    if not api.CreateCompressor(4, None, ctypes.byref(handle)):
        raise ctypes.WinError(ctypes.get_last_error())
    try:
        required = size()
        if api.Compress(handle, data, len(data), None, 0, ctypes.byref(required)) or ctypes.get_last_error() != 122:
            raise ValueError('Unexpected source compression size query')
        if required.value > 4 * 1024 * 1024 + 65536:
            raise ValueError('Compressed source shader exceeds budget')
        output = ctypes.create_string_buffer(required.value)
        if not api.Compress(handle, data, len(data), output, len(output), ctypes.byref(required)):
            raise ctypes.WinError(ctypes.get_last_error())
        return output.raw[:required.value]
    finally:
        api.CloseCompressor(handle)
