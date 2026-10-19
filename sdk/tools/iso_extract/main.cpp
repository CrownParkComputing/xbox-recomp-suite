// iso_extract: files out of an Xbox disc image (a whole disc dump or an xiso), for building from a rip kept as an
// image: the recompiler needs the xbe as a file, and the game then runs from the extracted files beside it.
//   iso_extract <image.iso> <path on the disc, backslash or slash separated> <output file>
//   iso_extract --all <image.iso> <output folder>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "kernel/disc.h"

namespace {

namespace fs = std::filesystem;

bool WriteFile(const kernel::Disc& disc, const kernel::Disc::Entry& e, const fs::path& out) {
    std::ofstream f(out, std::ios::binary);
    std::vector<char> buf(1 << 20);
    for (uint64_t pos = 0; pos < e.size;) {
        uint32_t want = uint32_t(std::min<uint64_t>(buf.size(), e.size - pos));
        uint32_t got = disc.Read(e, pos, buf.data(), want);
        if (got != want) return false;
        f.write(buf.data(), got);
        pos += got;
    }
    return bool(f);
}

// every file under e into dir, folders as folders; the count written, -1 on a failure
long WriteTree(const kernel::Disc& disc, const kernel::Disc::Entry& e, const fs::path& dir) {
    long n = 0;
    std::error_code ec;
    fs::create_directories(dir, ec);
    for (const kernel::Disc::Entry& c : e.children) {
        if (c.dir) {
            long sub = WriteTree(disc, c, dir / c.name);
            if (sub < 0) return -1;
            n += sub;
        } else if (WriteFile(disc, c, dir / c.name)) {
            n++;
        } else {
            std::fprintf(stderr, "%s: cannot write\n", (dir / c.name).string().c_str());
            return -1;
        }
    }
    return n;
}

}  // namespace

int main(int argc, char** argv) {
    bool all = argc == 4 && std::string(argv[1]) == "--all";
    if (argc != 4) {
        std::fprintf(stderr, "usage: %s <image.iso> <path on the disc> <output file>\n       %s --all <image.iso> <output folder>\n",
                     argv[0], argv[0]);
        return 2;
    }
    const char* image = all ? argv[2] : argv[1];
    kernel::Disc disc;
    std::string error;
    if (!disc.Open(image, error)) {
        std::fprintf(stderr, "%s: %s\n", image, error.c_str());
        return 1;
    }
    if (all) {
        long n = WriteTree(disc, *disc.Find(std::vector<std::string>{}), argv[3]);
        if (n < 0) return 1;
        std::printf("%s: %ld files\n", argv[3], n);
        return 0;
    }
    std::string path = argv[2];
    for (char& c : path)
        if (c == '/') c = '\\';
    const kernel::Disc::Entry* e = disc.Find(path);
    if (!e || e->dir) {
        std::fprintf(stderr, "%s: no %s on the disc\n", image, argv[2]);
        return 1;
    }
    if (!WriteFile(disc, *e, argv[3])) {
        std::fprintf(stderr, "%s: cannot write\n", argv[3]);
        return 1;
    }
    std::printf("%s: %u bytes\n", argv[3], e->size);
    return 0;
}
