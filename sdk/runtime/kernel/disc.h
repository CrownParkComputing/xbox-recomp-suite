#pragma once
// Xbox DVD file system images (.iso): a whole disc dump, its game partition behind the video one, or the game
// partition alone (xiso). Read only.
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>
#ifndef _WIN32
#include <functional>
#include <list>
#include <mutex>
#endif

namespace kernel {

class Disc {
public:
    struct Entry {
        std::string name;
        uint64_t offset = 0;  // bytes into the image file; a folder's is its entry table
        uint32_t size = 0;
        bool dir = false;
        std::vector<Entry> children;  // folders, sorted by name
    };
    Disc() = default;
    Disc(const Disc&) = delete;
    Disc& operator=(const Disc&) = delete;
    ~Disc();
    // false with the reason when the file is no Xbox disc image
    bool Open(const std::filesystem::path& path, std::string& error);
#ifndef _WIN32
    // Borrow a seekable descriptor supplied by Android's document picker.
    // The disc owns a duplicate; the caller retains ownership of its descriptor.
    bool OpenFd(int fd, const std::filesystem::path& name, std::string& error);
    // An Android document tree. Each file's offset identifies a document. The
    // callback transfers a fresh descriptor to this reader, which owns it.
    bool OpenTree(Entry root, std::function<int(uint64_t)> open,
                  const std::filesystem::path& name, std::string& error);
#endif
    // from the root, any case, ".." steps up; null when missing
    const Entry* Find(const std::vector<std::string>& parts) const;
    const Entry* Find(std::string_view path) const;  // backslash separated
    // at a position in the file, from any thread; the count read, short at the file's end
    uint32_t Read(const Entry& file, uint64_t pos, void* out, uint32_t len) const;
    bool Load(std::string_view path, std::vector<uint8_t>& out) const;
    uint64_t Time() const { return time_; }  // the volume's FILETIME, the only time the disc keeps
    const std::filesystem::path& Path() const { return path_; }

private:
    uint32_t ReadAt(uint64_t at, void* out, uint32_t len) const;
    bool ReadVolume(std::string& error);
    bool ReadDir(Entry& dir, int depth);

    intptr_t file_ = -1;  // HANDLE or fd
    std::filesystem::path path_;
    uint64_t base_ = 0;  // the game partition
    uint64_t time_ = 0;
    Entry root_;
#ifndef _WIN32
    std::function<int(uint64_t)> open_document_;
    mutable std::mutex documents_mutex_;
    mutable std::list<std::pair<uint64_t, std::shared_ptr<int>>> documents_;
#endif
};

// D: from a disc image instead of a folder, set before Boot
void MountDisc(std::unique_ptr<Disc> disc);
const Disc* MountedDisc();

}  // namespace kernel
