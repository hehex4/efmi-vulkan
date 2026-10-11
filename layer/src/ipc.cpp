// Named pipe server of the probe.
//
// Pipe: \\.\pipe\efmi-vk-<PID>. One client at a time.
// Message, both directions: [u32 header_len][u32 payload_len][header JSON][payload].
#include "state.h"
#include "json.h"

#include <mutex>
#include <sddl.h>

namespace efvk {

namespace {

const uint32_t kMaxHeader = 1u << 20;
const uint32_t kMaxPayload = 64u << 20;

bool read_all(HANDLE pipe, void* data, uint32_t size)
{
    uint8_t* p = static_cast<uint8_t*>(data);
    while (size) {
        DWORD got = 0;
        if (!ReadFile(pipe, p, size, &got, nullptr) || got == 0)
            return false;
        p += got;
        size -= got;
    }
    return true;
}

bool write_all(HANDLE pipe, const void* data, uint32_t size)
{
    const uint8_t* p = static_cast<const uint8_t*>(data);
    while (size) {
        DWORD put = 0;
        if (!WriteFile(pipe, p, size, &put, nullptr) || put == 0)
            return false;
        p += put;
        size -= put;
    }
    return true;
}

std::string error_reply(const char* msg)
{
    JsonWriter w;
    w.begin_object();
    w.kb("ok", false);
    w.kv("error", msg);
    w.end_object();
    return w.out;
}

std::string dispatch(const JsonRequest& req, std::vector<uint8_t>& payload)
{
    std::string cmd = req.get("cmd");
    if (cmd == "status")
        return probe_status();
    if (cmd == "capture") {
        uint32_t timeout = static_cast<uint32_t>(req.get_u64("timeout_ms", 5000));
        if (timeout > 60000)
            timeout = 60000;
        // Default bound for the descriptor contents of one captured frame: 64 MiB of JSON.
        return probe_capture(timeout, req.get_u64("draws", 1) != 0, req.get_u64("descriptor_budget", 64ull << 20));
    }
    if (cmd == "buffers")
        return probe_buffers(static_cast<uint32_t>(req.get_u64("usage", 0)), req.get_u64("min_size", 0),
                             static_cast<uint32_t>(req.get_u64("limit", 1000)));
    if (cmd == "read_buffer")
        return probe_read_buffer(static_cast<uint32_t>(req.get_u64("id", 0)), req.get_u64("offset", 0),
                                 req.get_u64("size", 0), payload);
    if (cmd == "hash")
        return probe_hash(static_cast<uint32_t>(req.get_u64("id", 0)), req.get_u64("offset", 0), req.get_u64("size", 0));
    if (cmd == "pipeline")
        return probe_pipeline(static_cast<uint32_t>(req.get_u64("id", 0)));
    if (cmd == "api_usage")
        return probe_api_usage();
    if (cmd == "shader")
        return probe_shader(static_cast<uint32_t>(req.get_u64("id", 0)), req.get_u64("code", 0) != 0, payload);
    return error_reply("unknown command");
}

void serve(HANDLE pipe)
{
    std::string header;
    std::vector<uint8_t> scratch;
    for (;;) {
        uint32_t lens[2];
        if (!read_all(pipe, lens, sizeof(lens)))
            return;
        if (lens[0] > kMaxHeader || lens[1] > kMaxPayload)
            return;
        header.resize(lens[0]);
        if (lens[0] && !read_all(pipe, &header[0], lens[0]))
            return;
        scratch.resize(lens[1]);
        if (lens[1] && !read_all(pipe, scratch.data(), lens[1]))
            return;

        std::vector<uint8_t> payload;
        std::string reply;
        JsonRequest req;
        if (!req.parse(header))
            reply = error_reply("malformed request header");
        else
            reply = dispatch(req, payload);

        uint32_t out[2] = {static_cast<uint32_t>(reply.size()), static_cast<uint32_t>(payload.size())};
        if (!write_all(pipe, out, sizeof(out)) || !write_all(pipe, reply.data(), out[0]))
            return;
        if (out[1] && !write_all(pipe, payload.data(), out[1]))
            return;
    }
}

DWORD WINAPI pipe_thread(void*)
{
    wchar_t name[64];
    swprintf(name, 64, L"\\\\.\\pipe\\efmi-vk-%lu", GetCurrentProcessId());

    // The game may run elevated while the client does not. Give the interactive
    // user read and write access; system and administrators keep full control.
    SECURITY_ATTRIBUTES sa = {};
    sa.nLength = sizeof(sa);
    PSECURITY_DESCRIPTOR sd = nullptr;
    if (ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:(A;;GA;;;SY)(A;;GA;;;BA)(A;;GRGW;;;IU)", SDDL_REVISION_1,
                                                              &sd, nullptr))
        sa.lpSecurityDescriptor = sd;

    logf("probe pipe \\\\.\\pipe\\efmi-vk-%lu", GetCurrentProcessId());
    for (;;) {
        HANDLE pipe = CreateNamedPipeW(name, PIPE_ACCESS_DUPLEX,
                                       PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS, 1,
                                       1 << 20, 1 << 16, 0, sd ? &sa : nullptr);
        if (pipe == INVALID_HANDLE_VALUE) {
            logf("CreateNamedPipe failed: %lu", GetLastError());
            Sleep(2000);
            continue;
        }
        BOOL connected = ConnectNamedPipe(pipe, nullptr) ? TRUE : (GetLastError() == ERROR_PIPE_CONNECTED);
        if (connected)
            serve(pipe);
        FlushFileBuffers(pipe);
        DisconnectNamedPipe(pipe);
        CloseHandle(pipe);
    }
}

std::once_flag g_once;

} // namespace

void ipc_start()
{
    std::call_once(g_once, [] {
        HANDLE t = CreateThread(nullptr, 0, pipe_thread, nullptr, 0, nullptr);
        if (t)
            CloseHandle(t);
        else
            logf("CreateThread for the probe pipe failed: %lu", GetLastError());
    });
}

} // namespace efvk
