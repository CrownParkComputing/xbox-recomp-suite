// the SDK's shader translation through DXC: every shape of shader it writes compiles for Vulkan, and lands where the
// renderer binds its resources and constants
#include <cstddef>
#include <map>
#include <random>
#include <string>
#include <vector>

#include "check.h"
#include "gpu/shaders.h"
#include "gpu/vk_shaders.h"

using namespace gpu;
using gpu::vk::Stage;

namespace {

bool Compiles(const std::string& src, const char* entry, Stage stage, std::string* error = nullptr) {
    std::string e;
    std::vector<uint32_t> spv = gpu::vk::CompileHlsl(src, entry, stage, e);
    if (spv.empty() && error) *error = e;
    return !spv.empty();
}

// what a SPIR-V module says about its names, bindings and member offsets
struct Reflection {
    std::map<uint32_t, std::string> names;                    // id -> OpName
    std::map<uint32_t, std::map<uint32_t, std::string>> members;  // struct id -> member -> OpMemberName
    std::map<uint32_t, uint32_t> binding;                     // id -> Binding
    std::map<uint32_t, std::map<uint32_t, uint32_t>> offsets; // struct id -> member -> Offset
    std::map<uint32_t, uint32_t> pointee;                     // variable id -> its pointer type's pointee
    std::map<uint32_t, uint32_t> pointer_to;                  // pointer type id -> pointee type id
    std::map<uint32_t, uint32_t> var_type;                    // variable id -> pointer type id

    explicit Reflection(const std::vector<uint32_t>& w) {
        for (size_t i = 5; i < w.size();) {
            uint32_t op = w[i] & 0xFFFF, n = w[i] >> 16;
            if (!n) break;
            const uint32_t* a = &w[i + 1];
            auto str = [&](size_t at) { return std::string(reinterpret_cast<const char*>(a + at)); };
            if (op == 5) names[a[0]] = str(1);                                     // OpName
            if (op == 6) members[a[0]][a[1]] = str(2);                             // OpMemberName
            if (op == 71 && a[1] == 33) binding[a[0]] = a[2];                      // OpDecorate Binding
            if (op == 72 && a[2] == 35) offsets[a[0]][a[1]] = a[3];                // OpMemberDecorate Offset
            if (op == 32) pointer_to[a[0]] = a[2];                                 // OpTypePointer
            if (op == 59) var_type[a[1]] = a[0];                                   // OpVariable
            i += n;
        }
    }
    // the binding of the variable OpName calls name (DXC names resources by their HLSL names)
    int BindingOf(const std::string& name) const {
        for (auto& [id, n] : names)
            if (n == name && binding.count(id)) return int(binding.at(id));
        return -1;
    }
    // a constant buffer's member offset, by the block's type name and the member's
    int Offset(const std::string& block, const std::string& member) const {
        for (auto& [id, n] : names) {
            if (n != block || !members.count(id)) continue;
            for (auto& [m, mn] : members.at(id))
                if (mn == member && offsets.count(id) && offsets.at(id).count(m)) return int(offsets.at(id).at(m));
        }
        return -1;
    }
};

std::vector<uint32_t> Spirv(const std::string& src, const char* entry, Stage stage) {
    std::string e;
    return gpu::vk::CompileHlsl(src, entry, stage, e);
}

// one instruction of the vertex program microcode, every field in range: temporaries R0 to R12 (R12 reads oPos),
// constants below 192, every MAC and ILU op
void Instruction(std::mt19937& rng, uint32_t out[4], bool last) {
    auto r = [&](uint32_t n) { return uint32_t(rng() % n); };
    uint32_t mac = r(14), ilu = r(8);
    uint32_t t1 = ilu << 25 | mac << 21 | r(192) << 13 | r(16) << 9 | r(2) << 8 | r(256);
    uint32_t a_reg = r(13), b_reg = r(13), c_reg = r(13);
    uint32_t t2 = a_reg << 28 | (1 + r(3)) << 26 | r(2) << 25 | r(256) << 17 | b_reg << 13 | (1 + r(3)) << 11 | r(2) << 10 |
                  r(256) << 2 | (c_reg >> 2);
    uint32_t outputs[] = {0, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
    uint32_t t3 = (c_reg & 3) << 30 | (1 + r(3)) << 28 | r(16) << 24 | r(13) << 20 | r(16) << 16 | r(16) << 12 | r(2) << 11 |
                  outputs[r(11)] << 3 | r(2) << 2 | r(2) << 1 | (last ? 1 : 0);
    out[0] = 0;
    out[1] = t1;
    out[2] = t2;
    out[3] = t3;
}

// a combiner input: a real register, either channel, any mapping
uint32_t CombinerInput(std::mt19937& rng) {
    static const uint32_t regs[] = {0, 1, 2, 3, 4, 5, 8, 9, 10, 11, 12, 13, 14};
    return regs[rng() % 13] | (rng() & 0x10) | ((rng() % 8) << 5);
}
uint32_t CombinerInputs(std::mt19937& rng) {
    return CombinerInput(rng) | CombinerInput(rng) << 8 | CombinerInput(rng) << 16 | CombinerInput(rng) << 24;
}
// outputs: discard or a writable register for each of cd, ab and the sum, with dots, mux and mapping flags
uint32_t CombinerOutput(std::mt19937& rng) {
    static const uint32_t dst[] = {0, 4, 5, 8, 9, 10, 11, 12, 13};
    static const uint32_t map[] = {0, 0x08, 0x10, 0x18, 0x20, 0x30};
    uint32_t fl = (rng() & 0x7) | map[rng() % 6] | (rng() & 0xC0);
    return dst[rng() % 9] | dst[rng() % 9] << 4 | dst[rng() % 9] << 8 | fl << 12;
}

}  // namespace

TEST("shaders: DXC loads") {
    std::string why;
    CHECK_MSG(gpu::vk::CompilerAvailable(why), why);
}

TEST("shaders: fixed function vertex shaders compile for every lighting, light and texgen setting") {
    int n = 0;
    for (uint32_t light : {0u, 1u, 3u, 7u, 15u, 0x5501u, 0xAA0Fu})
        for (uint32_t types : {0u, 0x1u, 0x2u, 0x3u, 0xE4u, 0xFFFFu})
            for (uint32_t gen = 0; gen < 4; gen++) {
                FixedKey f;
                f.light = light;
                f.types = types;
                for (int t = 0; t < 4; t++) f.tex[t] = uint32_t(t) | gen << 4 | (t & 3) << 8 | (gen & 1 ? 0x800 : 0) | 2 << 12;
                uint16_t regs = uint16_t(1 | 4 | 8 | 16 | 1 << 9 | 1 << 10);
                std::string error;
                CHECK_MSG(Compiles(FixedVertexHlsl(regs, false, gen == 2 ? 0x2101 : 0, f), "main", Stage::Vertex, &error), error);
                n++;
            }
    CHECK(Compiles(FixedVertexHlsl(1 | 8 | 1 << 9, true, 0, {}), "main", Stage::Vertex));
    CHECK(n > 100);
}

TEST("shaders: vertex programs compile across every MAC and ILU op and output") {
    std::mt19937 rng(7);
    for (int p = 0; p < 300; p++) {
        std::vector<uint32_t> prog;
        int len = 1 + p % 24;
        for (int i = 0; i < len; i++) {
            uint32_t ins[4];
            Instruction(rng, ins, i == len - 1);
            prog.insert(prog.end(), ins, ins + 4);
        }
        uint16_t regs = uint16_t(rng() | 1);
        uint64_t decode = p % 3 ? 0 : 0x0000000000054321ull & ((uint64_t(regs) * 0x1111) & 0xFFFFF);
        std::string error;
        bool ok = Compiles(VertexProgramHlsl(prog, regs, decode), "main", Stage::Vertex, &error);
        CHECK_MSG(ok, "program " + std::to_string(p) + ": " + error.substr(0, 400));
        if (!ok) break;
    }
}

TEST("shaders: fixed function pixel stages compile for every op, texture kind and flag") {
    for (uint32_t op = 1; op <= 24; op++) {
        for (int kind = 1; kind <= 3; kind++) {
            PixelKey k{};
            for (int t = 0; t < 4; t++) {
                k.tex[t].kind = uint8_t(1 + (kind + t) % 3);
                k.tex[t].rect = (op & 1) && k.tex[t].kind == kTex2D;
                k.tex[t].shadow = (op == 7 && t == 0 && k.tex[t].kind == kTex2D) ? 1 : 0;
                k.tex[t].flags = (op == 9 && k.tex[t].kind == kTex2D) ? kTexWide : 0;
                uint32_t v[9] = {op, uint32_t(t % 6), uint32_t((t + 2) % 6) | 0x20, 3, op, 2, 1 | 0x10, 0, 1};
                for (int j = 0; j < 9; j++) k.ff[t][j] = v[j];
            }
            k.alphakill = op & 5;
            k.flat = op & 1;
            k.two_sided = op & 2;
            k.proj = op & 3;
            std::string error;
            CHECK_MSG(Compiles(PixelHlsl(k), "main", Stage::Pixel, &error), "op " + std::to_string(op) + ": " + error.substr(0, 400));
        }
    }
}

TEST("shaders: register combiner programs compile with every register, mapping and output") {
    std::mt19937 rng(11);
    for (int p = 0; p < 300; p++) {
        PixelKey k{};
        k.programmable = 1;
        k.count = 1 + p % 8 | (rng() & 0x110) << 0 | (rng() & 1) << 8;
        for (int j = 0; j < 8; j++) {
            k.rgb_in[j] = CombinerInputs(rng);
            k.a_in[j] = CombinerInputs(rng);
            k.rgb_out[j] = CombinerOutput(rng);
            k.a_out[j] = CombinerOutput(rng) & 0xFFF;  // alpha has no blue-to-alpha or dots
        }
        // the final combiner reads registers, E and F among them only through EF_PROD
        k.final_abcd = CombinerInputs(rng);
        k.final_efg = (CombinerInputs(rng) & 0xFFFFFF00) | (rng() & 0xE0);
        for (int t = 0; t < 4; t++) k.tex[t].kind = uint8_t(1 + (p + t) % 3);
        // texture modes as the bound kinds take them: project 2D / 3D / cube, or passthrough
        for (int t = 0; t < 4; t++) k.tex_modes |= uint32_t(p % 5 == 0 ? 4 : k.tex[t].kind) << (t * 5);
        std::string error;
        bool ok = Compiles(PixelHlsl(k), "main", Stage::Pixel, &error);
        CHECK_MSG(ok, "program " + std::to_string(p) + ": " + error.substr(0, 400));
        if (!ok) break;
    }
}

TEST("shaders: screen quad and point shaders compile") {
    CHECK(Compiles(QuadHlsl(FixedVertexHlsl(1 | 8 | 1 << 9, true, 0, {})), "main", Stage::Vertex));
    CHECK(Compiles(PointHlsl(), "main", Stage::Geometry));
}


TEST("shaders: registers land on the push descriptor set's bindings") {
    PixelKey k{};
    for (int t = 0; t < 4; t++) {
        k.tex[t].kind = kTex2D;
        uint32_t v[9] = {4, 2, 1, 0, 4, 2, 1, 0, 1};  // modulate texture with current, every stage
        for (int j = 0; j < 9; j++) k.ff[t][j] = v[j];
    }
    Reflection r(Spirv(PixelHlsl(k), "main", Stage::Pixel));
    for (int t = 0; t < 4; t++) {
        CHECK_EQ(r.BindingOf("tex" + std::to_string(t)), int(gpu::vk::kBindingTextures) + t);
        CHECK_EQ(r.BindingOf("smp" + std::to_string(t)), int(gpu::vk::kBindingSamplers) + t);
    }
    // mov oPos, c[5]: a program that reads the constants (a fixed function shader never does, and DXC drops them)
    std::vector<uint32_t> prog = {0, 1u << 21 | 5u << 13, 3u << 26, 0xFu << 24 | 0xFu << 12 | 1u << 11 | 1u};
    Reflection v(Spirv(VertexProgramHlsl(prog, 1, 0), "main", Stage::Vertex));
    CHECK_EQ(v.BindingOf("VC"), 0);
    CHECK_EQ(v.BindingOf("DC"), 1);
}

TEST("shaders: the shared constant buffer is laid out as DrawConstants") {
    FixedKey f;
    f.light = 1;
    f.types = 1;
    Reflection r(Spirv(FixedVertexHlsl(1 | 4 | 8, false, 0, f), "main", Stage::Vertex));
    auto off = [&](const char* m) { return r.Offset("type.DC", m); };
    CHECK_EQ(off("surf"), int(offsetof(DrawConstants, surf)));
    CHECK_EQ(off("pc"), int(offsetof(DrawConstants, pc)));
    CHECK_EQ(off("wvp"), int(offsetof(DrawConstants, wvp)));
    CHECK_EQ(off("ext"), int(offsetof(DrawConstants, ext)));
    CHECK_EQ(off("spread"), int(offsetof(DrawConstants, spread)));
    CHECK_EQ(off("wvn"), int(offsetof(DrawConstants, wvn)));
    CHECK_EQ(off("tm"), int(offsetof(DrawConstants, tm)));
    CHECK_EQ(off("lt"), int(offsetof(DrawConstants, lt)));
}
