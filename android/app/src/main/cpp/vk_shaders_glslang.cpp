// HLSL -> SPIR-V through glslang for Android, in place of the desktop DXC library.
// The flags mirror vk_shaders.cpp's DXC command line so SPIR-V lands where the renderer binds:
// b0/b1 at bindings 0/1, t0..t3 at 2..5, s0..s3 at 6..9, Direct3D's constant-buffer layout, a pixel shader's
// SV_Position.w as Direct3D gives it, HLSL 2018 semantics and every entry point named "main" in SPIR-V.
#include "gpu/vk_shaders.h"

#include <glslang/Public/ResourceLimits.h>
#include <glslang/Public/ShaderLang.h>
// glslang installs the SPIR-V front end as <glslang/SPIRV/...>; in its source tree (FetchContent) it is <SPIRV/...>
#if __has_include(<glslang/SPIRV/GlslangToSpv.h>)
#include <glslang/SPIRV/GlslangToSpv.h>
#else
#include <SPIRV/GlslangToSpv.h>
#endif

#include <cstring>
#include <mutex>

namespace gpu::vk {

namespace {

EShLanguage Lang(Stage s) {
    switch (s) {
        case Stage::Vertex: return EShLangVertex;
        case Stage::Geometry: return EShLangGeometry;
        default: return EShLangFragment;
    }
}

// one process-wide glslang, as InitializeProcess asks; the compiler objects themselves are per call
bool Init(std::string& error) {
    static std::once_flag once;
    static bool ok = false;
    static std::string failed;
    std::call_once(once, [] {
        ok = glslang::InitializeProcess();
        if (!ok) failed = "glslang::InitializeProcess failed";
    });
    if (!ok) error = failed;
    return ok;
}

}  // namespace

std::vector<uint32_t> CompileHlsl(const std::string& src, const char* entry, Stage stage, std::string& error) {
    if (!Init(error)) return {};
    EShLanguage lang = Lang(stage);
    glslang::TShader shader(lang);
    const char* sources[] = {src.c_str()};
    shader.setStrings(sources, 1);
    // the SPIR-V entry point is always named "main", whatever the HLSL function is called: the renderer builds every
    // pipeline stage with VkPipelineShaderStageCreateInfo::pName = "main" (desktop DXC is told the same with
    // -fspv-entrypoint-name=main). setSourceEntryPoint is what finds the HLSL function in the source
    shader.setEntryPoint("main");
    shader.setSourceEntryPoint(entry);

    // the device's ceiling, not the desktop's: it is a Vulkan 1.1 device, so the client is 1.1 and the target is
    // SPIR-V 1.3, the highest a 1.1 implementation must accept. The binding shifts below do not change with it.
    shader.setEnvInput(glslang::EShSourceHlsl, lang, glslang::EShClientVulkan, 100);
    shader.setEnvClient(glslang::EShClientVulkan, glslang::EShTargetVulkan_1_1);
    shader.setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_3);

    // -fvk-b-shift / -fvk-t-shift / -fvk-s-shift: the renderer's descriptor set 0 layout
    shader.setShiftBinding(glslang::EResUbo, kBindingConstants);
    shader.setShiftBinding(glslang::EResTexture, kBindingTextures);
    shader.setShiftBinding(glslang::EResSampler, kBindingSamplers);

    // -fvk-use-dx-position-w
    shader.setDxPositionW(true);
    // the D3D compiler numbers inter-stage variables by semantic order; map them the same way (glslang's default for
    // HLSL). ENABLE_HLSL exposes the HLSL-only entry points and is defined by the build.
#ifdef ENABLE_HLSL
    shader.setHlslIoMapping(true);
#endif

    const EShMessages messages = static_cast<EShMessages>(
        EShMsgSpvRules | EShMsgVulkanRules | EShMsgReadHlsl | EShMsgHlslOffsets | EShMsgHlslLegalization);
    if (!shader.parse(GetDefaultResources(), 450, false, messages)) {
        error = shader.getInfoLog();
        return {};
    }
    glslang::TProgram program;
    program.addShader(&shader);
    if (!program.link(messages)) {
        error = program.getInfoLog();
        return {};
    }
    // the pass that applies the binding shifts (setShiftBinding): glslang's driver calls it explicitly after link
    if (!program.mapIO()) {
        error = program.getInfoLog();
        return {};
    }
    std::vector<uint32_t> spirv;
    glslang::SpvOptions options;
    // the HLSL legalization glslang only runs with the optimizer: it forwards and drops the writes of opaque
    // types the front end leaves behind, which are illegal SPIR-V (the shadow comparison hook makes one)
    options.disableOptimizer = false;
    options.optimizeSize = false;
    glslang::GlslangToSpv(*program.getIntermediate(lang), spirv, &options);
    return spirv;
}

bool CompilerAvailable(std::string& error) { return Init(error); }

}  // namespace gpu::vk
