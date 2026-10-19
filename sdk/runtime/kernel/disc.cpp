#include "kernel/disc.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <cerrno>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace kernel {

namespace {

constexpr uint32_t kSector = 2048;
// the game partition: at the start of an xiso, past the video partition of a whole original Xbox disc dump
constexpr uint64_t kPartitions[] = {0, 0x18300000};
// volume descriptor, sector 32 of the partition: magic, root table sector and size, FILETIME, magic again at +0x7EC
constexpr uint32_t kVolume = 32 * kSector;
constexpr char kMagic[] = "MICROSOFT*XBOX*MEDIA";
constexpr size_t kMagicSize = sizeof(kMagic) - 1;
// A folder is a table of entries in a binary tree: +0 left and +2 right entry (in 4-byte units, 0 for none),
// +4 sector, +8 size, +0xC attributes (0x10 folder), +0xD name length, +0xE name. A table of 0xFF is empty.
constexpr uint32_t kEntryHead = 0xE;
constexpr uint8_t kFolder = 0x10;
constexpr uint32_t kMaxTable = 16 << 20;

std::unique_ptr<Disc> g_mounted;

bool SameName(std::string_view a, std::string_view b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
               return std::toupper(uint8_t(x)) == std::toupper(uint8_t(y));
           });
}

std::string Upper(std::string s) {
    for (char& c : s) c = char(std::toupper(uint8_t(c)));
    return s;
}

}  // namespace

Disc::~Disc() {
    if (file_ == -1) return;
#ifdef _WIN32
    CloseHandle(HANDLE(file_));
#else
    close(int(file_));
#endif
}

// each read carries its own position, so threads reading at once do not queue on one handle
uint32_t Disc::ReadAt(uint64_t at, void* out, uint32_t len) const {
#ifdef _WIN32
    thread_local HANDLE event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    OVERLAPPED o{};
    o.Offset = DWORD(at);
    o.OffsetHigh = DWORD(at >> 32);
    o.hEvent = event;
    DWORD got = 0;
    if (!ReadFile(HANDLE(file_), out, len, nullptr, &o) && GetLastError() != ERROR_IO_PENDING) return 0;
    if (!GetOverlappedResult(HANDLE(file_), &o, &got, TRUE)) return 0;
    return got;
#else
    ssize_t got = pread(int(file_), out, len, off_t(at));
    return got > 0 ? uint32_t(got) : 0;
#endif
}

bool Disc::Open(const std::filesystem::path& path, std::string& error) {
    path_ = path;
#ifdef _WIN32
    file_ = intptr_t(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr));
#else
    file_ = open(path.c_str(), O_RDONLY);
#endif
    if (file_ == -1) {
        error = "cannot open the file";
        return false;
    }
    return ReadVolume(error);
}

#ifndef _WIN32
bool Disc::OpenFd(int fd, const std::filesystem::path& name, std::string& error) {
    path_ = name;
    file_ = fd >= 0 ? fcntl(fd, F_DUPFD_CLOEXEC, 0) : -1;
    if (file_ == -1) {
        error = "cannot duplicate the selected image descriptor";
        return false;
    }
    return ReadVolume(error);
}

bool Disc::OpenTree(Entry root, std::function<int(uint64_t)> open,
                    const std::filesystem::path& name, std::string& error) {
    std::function<bool(Entry&, int)> sort = [&](Entry& dir, int depth) {
        if (!dir.dir || depth > 32) return false;
        std::sort(dir.children.begin(), dir.children.end(),
                  [](const Entry& a, const Entry& b) { return Upper(a.name) < Upper(b.name); });
        std::string previous;
        for (auto& entry : dir.children) {
            if (entry.name.empty() || entry.name == "." || entry.name == ".." ||
                entry.name.find_first_of("/\\") != std::string::npos ||
                SameName(previous, entry.name)) return false;
            previous = entry.name;
            if (entry.dir && !sort(entry, depth + 1)) return false;
        }
        return true;
    };
    if (!open || !sort(root, 0)) {
        error = "invalid game folder";
        return false;
    }
    root_ = std::move(root);
    path_ = name;
    open_document_ = std::move(open);
    return true;
}
#endif

bool Disc::ReadVolume(std::string& error) {
    for (uint64_t base : kPartitions) {
        uint8_t volume[kSector];
        if (ReadAt(base + kVolume, volume, kSector) != kSector || std::memcmp(volume, kMagic, kMagicSize) != 0 ||
            std::memcmp(volume + 0x7EC, kMagic, kMagicSize) != 0)
            continue;
        base_ = base;
        uint32_t sector, size;
        std::memcpy(&sector, volume + 0x14, 4);
        std::memcpy(&size, volume + 0x18, 4);
        std::memcpy(&time_, volume + 0x1C, 8);
        root_ = {{}, base + uint64_t(sector) * kSector, size, true, {}};
        if (!ReadDir(root_, 0)) {
            error = "its folders cannot be read";
            return false;
        }
        return true;
    }
    error = "no Xbox disc image";
    return false;
}

bool Disc::ReadDir(Entry& dir, int depth) {
    if (!dir.size) return true;
    if (depth > 32 || dir.size > kMaxTable) return false;
    std::vector<uint8_t> table(dir.size);
    if (ReadAt(dir.offset, table.data(), dir.size) != dir.size) return false;
    std::vector<bool> seen(dir.size / 4 + 1);
    std::vector<uint32_t> todo{0};
    while (!todo.empty()) {
        uint32_t at = todo.back();
        todo.pop_back();
        if (at + kEntryHead > dir.size || seen[at / 4]) continue;
        seen[at / 4] = true;
        uint16_t left, right;
        uint32_t sector, size;
        std::memcpy(&left, &table[at], 2);
        std::memcpy(&right, &table[at + 2], 2);
        if (left == 0xFFFF) continue;
        std::memcpy(&sector, &table[at + 4], 4);
        std::memcpy(&size, &table[at + 8], 4);
        uint8_t attributes = table[at + 0xC], length = table[at + 0xD];
        if (at + kEntryHead + length > dir.size) continue;
        if (left) todo.push_back(uint32_t(left) * 4);
        if (right) todo.push_back(uint32_t(right) * 4);
        dir.children.push_back({std::string(reinterpret_cast<const char*>(&table[at + kEntryHead]), length),
                                base_ + uint64_t(sector) * kSector, size, (attributes & kFolder) != 0, {}});
    }
    std::sort(dir.children.begin(), dir.children.end(),
              [](const Entry& a, const Entry& b) { return Upper(a.name) < Upper(b.name); });
    for (Entry& child : dir.children)
        if (child.dir && !ReadDir(child, depth + 1)) return false;
    return true;
}

const Disc::Entry* Disc::Find(const std::vector<std::string>& parts) const {
    std::vector<const Entry*> path{&root_};
    for (const std::string& part : parts) {
        if (part.empty() || part == ".") continue;
        if (part == "..") {
            if (path.size() > 1) path.pop_back();
            continue;
        }
        const Entry* dir = path.back();
        auto it = std::find_if(dir->children.begin(), dir->children.end(), [&](const Entry& e) { return SameName(e.name, part); });
        if (it == dir->children.end()) return nullptr;
        path.push_back(&*it);
    }
    return path.back();
}

const Disc::Entry* Disc::Find(std::string_view path) const {
    std::vector<std::string> parts;
    size_t s = 0;
    while (s <= path.size()) {
        size_t e = std::min(path.find('\\', s), path.size());
        parts.emplace_back(path.substr(s, e - s));
        s = e + 1;
    }
    return Find(parts);
}

uint32_t Disc::Read(const Entry& file, uint64_t pos, void* out, uint32_t len) const {
    if (file.dir || pos >= file.size) return 0;
    len = uint32_t(std::min<uint64_t>(len, file.size - pos));
#ifndef _WIN32
    if (open_document_) {
        std::shared_ptr<int> fd;
        {
            std::lock_guard lock(documents_mutex_);
            auto it = std::find_if(documents_.begin(), documents_.end(),
                                   [&](const auto& cached) { return cached.first == file.offset; });
            if (it != documents_.end()) {
                fd = it->second;
                documents_.splice(documents_.begin(), documents_, it);
            } else {
                const int opened = open_document_(file.offset);
                if (opened < 0) return 0;
                fd = std::shared_ptr<int>(new int(opened), [](int* value) { close(*value); delete value; });
                documents_.emplace_front(file.offset, fd);
                if (documents_.size() > 64) documents_.pop_back();
            }
        }
        // Retain the descriptor during concurrent reads, including eviction.
        ssize_t got;
        do { got = pread(*fd, out, len, off_t(pos)); } while (got < 0 && errno == EINTR);
        return got > 0 ? uint32_t(got) : 0;
    }
#endif
    return ReadAt(file.offset + pos, out, len);
}

bool Disc::Load(std::string_view path, std::vector<uint8_t>& out) const {
    const Entry* e = Find(path);
    if (!e || e->dir) return false;
    out.resize(e->size);
    return Read(*e, 0, out.data(), e->size) == e->size;
}

void MountDisc(std::unique_ptr<Disc> disc) { g_mounted = std::move(disc); }

const Disc* MountedDisc() { return g_mounted.get(); }

}  // namespace kernel
