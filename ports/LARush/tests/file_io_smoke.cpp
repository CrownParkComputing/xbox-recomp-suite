// Exercise real kernel I/O exports on a synthetic save file, before game boot.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "kernel/kernel.h"

X86_FUNC(sub_001B2594) {
    using namespace kernel;
    unsigned checks = 0;
    auto check = [&](bool ok, const char* message) {
        ++checks;
        if (!ok) { std::fprintf(stderr, "file I/O smoke failed: %s\n", message); std::exit(1); }
    };
    auto call = [&](uint32_t ordinal, std::initializer_list<uint32_t> args) {
        return CallGuest(ctx, 0xFE000000u + ordinal * 4, args);
    };
    const uint32_t memory = mem::AllocSystem(8192);
    const uint32_t handle_out = memory, iosb = memory + 8, attrs = memory + 32;
    const uint32_t name = memory + 48, path = memory + 64, offset = memory + 128;
    const uint32_t input = memory + 1024, output = memory + 2048;
    const char filename[] = "U:\\io-smoke.bin";
    std::memcpy(HostPtr(path), filename, sizeof(filename));
    W16(name, sizeof(filename) - 1); W16(name + 2, sizeof(filename)); W32(name + 4, path);
    W32(attrs, 0); W32(attrs + 4, name); W32(attrs + 8, 0x40);
    for (uint32_t i = 0; i < 103; ++i) G<uint8_t>(input + i) = uint8_t(i * 17 + 3);
    check(call(190, {handle_out, 0xC0000000, attrs, iosb, 0, 0, 3, 5, 0x20}) == kSuccess, "create synchronous file");
    uint32_t file = R32(handle_out);
    W64(offset, 0);
    check(call(236, {file, 0, 0, 0, iosb, input, 103, offset}) == kSuccess, "synchronous write returns success");
    check(R32(iosb) == kSuccess && R32(iosb + 4) == 103, "write completion byte count");
    check(call(219, {file, 0, 0, 0, iosb, output, 128, offset}) == kSuccess, "partial synchronous read succeeds");
    check(R32(iosb + 4) == 103 && !std::memcmp(HostPtr(input), HostPtr(output), 103), "partial synchronous payload and count");
    check(call(187, {file}) == kSuccess, "close synchronous file");
    // Eight outstanding reads reproduce the frontend resource pipeline. Each
    // request keeps its own status, offset, event, and result buffer.
    check(call(202, {handle_out, 0xC0000000, attrs, iosb, 3, 0}) == kSuccess, "open overlapped file");
    file = R32(handle_out);
    for (uint32_t i = 0; i < 8; ++i) {
        const uint32_t request = memory + 256 + i * 64;
        check(call(189, {request, 0, 0, 0}) == kSuccess, "create completion event");
        W64(request + 16, i * 13);
        check(call(219, {file, R32(request), 0, 0, request + 8, output + i * 128, 13, request + 16}) == kPending,
              "overlapped read reports pending");
    }
    for (uint32_t i = 0; i < 8; ++i) {
        const uint32_t request = memory + 256 + i * 64;
        const uint32_t bytes = i == 7 ? 12 : 13;
        check(R32(request + 8) == kSuccess && R32(request + 12) == bytes, "independent completion byte count");
        check(!std::memcmp(HostPtr(input + i * 13), HostPtr(output + i * 128), bytes), "independent read payload");
        W64(offset, 0);
        check(call(233, {R32(request), 0, offset}) == kSuccess, "completion event is signalled");
        check(call(187, {R32(request)}) == kSuccess, "close completion event");
    }
    W64(offset, 128);
    check(call(219, {file, 0, 0, 0, iosb, output, 1, offset}) == kEndOfFile, "EOF remains an error with zero bytes");
    check(R32(iosb) == kEndOfFile && R32(iosb + 4) == 0, "EOF completion status");
    W64(offset, 0);
    check(call(236, {file, 0, 0, 0, iosb, input, 13, offset}) == kPending, "overlapped write reports pending");
    check(R32(iosb) == kSuccess && R32(iosb + 4) == 13, "overlapped write completion");
    check(call(187, {file}) == kSuccess, "close overlapped file");
    // NtCreateFile and NtOpenFile both preserve the synchronous mode flag.
    check(call(190, {handle_out, 0x80000000, attrs, iosb, 0, 0, 3, 1, 0}) == kSuccess, "create-file API opens async handle");
    file = R32(handle_out);
    check(call(219, {file, 0, 0, 0, iosb, output, 13, offset}) == kPending, "create-file asynchronous read");
    check(call(187, {file}) == kSuccess, "close create-file handle");
    check(call(202, {handle_out, 0x80000000, attrs, iosb, 3, 0x20}) == kSuccess, "open-file API opens synchronous handle");
    file = R32(handle_out);
    check(call(219, {file, 0, 0, 0, iosb, output, 13, offset}) == kSuccess, "open-file synchronous read");
    check(call(187, {file}) == kSuccess, "close final file");
    std::fprintf(stderr, "file I/O smoke: %u checks passed\n", checks);
    std::exit(0);
}
