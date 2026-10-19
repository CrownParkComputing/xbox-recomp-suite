// a rip kept as an Xbox disc image: the build's iso_extract takes default.xbe, or every file, out of it, from an xiso
// and from a whole disc dump (the game partition 0x18300000 in), files in folders included
#include <unistd.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "check.h"
#include "kernel/disc.h"

namespace {

constexpr uint32_t kSector = 2048;

// a directory table of one entry; sector and size of what it names
std::vector<uint8_t> Table(const std::string& name, uint32_t sector, uint32_t size, bool folder) {
    std::vector<uint8_t> t(kSector, 0xFF);
    uint16_t none = 0;
    std::memcpy(&t[0], &none, 2);
    std::memcpy(&t[2], &none, 2);
    std::memcpy(&t[4], &sector, 4);
    std::memcpy(&t[8], &size, 4);
    t[0xC] = folder ? 0x10 : 0x20;
    t[0xD] = uint8_t(name.size());
    std::memcpy(&t[0xE], name.data(), name.size());
    return t;
}

// root: one entry, default.xbe or a folder holding it; the volume descriptor at sector 32 of the partition
std::filesystem::path MakeImage(uint64_t base, bool in_folder, const std::vector<uint8_t>& xbe) {
    auto path = std::filesystem::temp_directory_path() / ("iso_test_" + std::to_string(getpid()) + "_" + std::to_string(base) +
                                                          (in_folder ? "_dir" : "") + ".iso");
    std::fstream f(path, std::ios::binary | std::ios::out | std::ios::trunc);
    auto at = [&](uint64_t sector, const void* p, size_t n) {
        f.seekp(std::streamoff(base + sector * kSector));
        f.write(static_cast<const char*>(p), std::streamsize(n));
    };
    uint8_t volume[kSector] = {};
    const char magic[] = "MICROSOFT*XBOX*MEDIA";
    uint32_t root_sector = 33, root_size = kSector;
    std::memcpy(volume, magic, 20);
    std::memcpy(volume + 0x14, &root_sector, 4);
    std::memcpy(volume + 0x18, &root_size, 4);
    std::memcpy(volume + 0x7EC, magic, 20);
    at(32, volume, kSector);
    if (in_folder) {
        at(33, Table("GAME", 34, kSector, true).data(), kSector);
        at(34, Table("default.xbe", 35, uint32_t(xbe.size()), false).data(), kSector);
    } else {
        at(33, Table("default.xbe", 35, uint32_t(xbe.size()), false).data(), kSector);
    }
    at(35, xbe.data(), xbe.size());
    return path;
}

std::vector<uint8_t> Xbe() {
    std::vector<uint8_t> x(5000);
    for (size_t i = 0; i < x.size(); i++) x[i] = uint8_t(i * 7 + 3);
    std::memcpy(x.data(), "XBEH", 4);
    return x;
}

std::vector<uint8_t> ReadFile(const std::filesystem::path& p) {
    std::ifstream f(p, std::ios::binary);
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(f), {});
}

}  // namespace

TEST("iso: the disc reader finds default.xbe in an xiso and in a whole disc dump") {
    std::vector<uint8_t> xbe = Xbe();
    for (uint64_t base : {uint64_t(0), uint64_t(0x18300000)}) {
        auto image = MakeImage(base, false, xbe);
        kernel::Disc disc;
        std::string error;
        CHECK_MSG(disc.Open(image, error), error);
        std::vector<uint8_t> got;
        CHECK(disc.Load("DEFAULT.XBE", got));
        CHECK(got == xbe);
        std::filesystem::remove(image);
    }
}

TEST("iso: iso_extract writes the named file, in a folder too, and fails on one that is missing") {
    std::vector<uint8_t> xbe = Xbe();
    auto image = MakeImage(0x18300000, true, xbe);
    auto out = std::filesystem::temp_directory_path() / ("iso_test_out_" + std::to_string(getpid()) + ".xbe");
    std::string tool = XBE_ISO_EXTRACT;
    std::string cmd = tool + " '" + image.string() + "' GAME/default.xbe '" + out.string() + "' > /dev/null";
    CHECK_EQ(std::system(cmd.c_str()), 0);
    CHECK(ReadFile(out) == xbe);
    std::string missing = tool + " '" + image.string() + "' default.xbe '" + out.string() + "' 2> /dev/null";
    CHECK_MSG(std::system(missing.c_str()) != 0, "default.xbe is in GAME, not the root");
    std::filesystem::remove(out);
    std::filesystem::remove(image);
}

TEST("iso: iso_extract --all writes every file with its folders") {
    std::vector<uint8_t> xbe = Xbe();
    auto image = MakeImage(0, true, xbe);
    auto dir = std::filesystem::temp_directory_path() / ("iso_test_all_" + std::to_string(getpid()));
    std::string cmd = std::string(XBE_ISO_EXTRACT) + " --all '" + image.string() + "' '" + dir.string() + "' > /dev/null";
    CHECK_EQ(std::system(cmd.c_str()), 0);
    CHECK(ReadFile(dir / "GAME" / "default.xbe") == xbe);
    std::filesystem::remove_all(dir);
    std::filesystem::remove(image);
}
