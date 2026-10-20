"""Keep BLACK's renderer, with the SDK's Vulkan 1.1/extension compatibility."""
from pathlib import Path


def once(text, old, new):
    if text.count(old) != 1:
        raise ValueError(f"BLACK renderer changed: expected one {old!r}")
    return text.replace(old, new)


def stage_vulkan(source: Path, sdk: Path, stage: Path):
    renderer = (sdk / "runtime/gpu/vulkan.cpp").read_text()
    sdk_header = (sdk / "runtime/gpu/vulkan.h").read_text()
    header = (source / "src/vk/vk_backend.h").read_text()
    helpers = renderer[renderer.index("VkRenderPass VulkanRenderer::RenderPassFor("):
                       renderer.index("// Direct3D's y up:", renderer.index("VkRenderPass VulkanRenderer::RenderPassFor("))]
    helpers = helpers.replace("VulkanRenderer::", "VulkanBackend::")
    helpers = helpers.replace('    if (SkipPart("norp")) return;\n', '')
    # BLACK supports MSAA; render passes must agree with both images and pipelines.
    helpers = helpers.replace("RenderPassFor(VkFormat color, bool depth)",
                              "RenderPassFor(VkFormat color, bool depth, uint32_t samples)")
    helpers = helpers.replace("(uint64_t(ctx_.caps.depth_format) << 33)",
                              "(uint64_t(ctx_.caps.depth_format) << 33) | (uint64_t(samples) << 48)")
    helpers = helpers.replace("att[n].samples = VK_SAMPLE_COUNT_1_BIT;",
                              "att[n].samples = VkSampleCountFlagBits(samples);")
    helpers = helpers.replace("a.depth != nullptr);", "a.depth != nullptr, (a.color ? a.color : a.depth)->desc.samples);")
    prototypes = """    VkRenderPass RenderPassFor(VkFormat color, bool depth, uint32_t samples = 1);
    VkFramebuffer FramebufferFor(VkRenderPass pass, const Attachments& a, uint32_t w, uint32_t h);
    void DropSlotFramebuffers();
    void DropFramebuffers();
    void DropSwapchainFramebuffers();
"""
    header = once(header, "    void SetViewport(", prototypes + "    void SetViewport(")
    fields = sdk_header[sdk_header.index("    struct FbKey {"):sdk_header.index("    std::unordered_map<uint64_t, VkSampler> samplers_;")]
    header = once(header, "    std::unordered_map<uint64_t, VkSampler> samplers_;",
                  fields + "    std::unordered_map<uint64_t, VkSampler> samplers_;")
    include = stage / "include/vk"
    include.mkdir(parents=True, exist_ok=True)
    header = once(header, "    uint32_t samples = 1;     // the target's", "    uint32_t rotation = 0;    // swapchain pre-rotation, quarter turns\n    uint32_t samples = 1;     // the target's")
    (include / "vk_backend.h").write_text(header)
    shaders = (source / "src/render/pass_shaders.h").read_text()
    shaders = once(shaders, "float4 uvrect; float4 opts;", "float4 uvrect; float4 opts; float4 rotation;")
    shaders = once(shaders, "    o.uv = uvrect.xy + t * uvrect.zw;", """    if (rotation.x == 1) t = float2(t.y, 1 - t.x);
    else if (rotation.x == 2) t = 1 - t;
    else if (rotation.x == 3) t = float2(1 - t.y, t.x);
    o.uv = uvrect.xy + t * uvrect.zw;""")
    (stage / "include/render").mkdir(parents=True, exist_ok=True)
    (stage / "include/render/pass_shaders.h").write_text(shaders)

    backend = (source / "src/vk/vk_backend.cpp").read_text()
    backend = once(backend, "    vkCmdEndRendering(Cb());",
                   "    if (ctx_.caps.dynamic_rendering) ctx_.d.CmdEndRendering(Cb());\n    else vkCmdEndRenderPass(Cb());")
    start = backend.index("void VulkanBackend::BeginRendering(")
    end = backend.index("// Direct3D's y up:", start)
    backend = backend[:start] + helpers + backend[end:]
    backend = once(backend, "    ShutdownUi();", "    ShutdownUi();\n    DropFramebuffers();\n    for (auto& [key, pass] : passes_) if (pass) vkDestroyRenderPass(ctx_.dev, pass, nullptr);")
    backend = once(backend, "        if (!swap_->Init(ctx_, surface))", "        swap_->on_views_gone = [this] { DropSwapchainFramebuffers(); };\n        if (!swap_->Init(ctx_, surface))")
    backend = once(backend, "        f.images.clear();\n        f.buffers.clear();\n        for (VkSampler", "        DropSlotFramebuffers();\n        f.images.clear();\n        f.buffers.clear();\n        for (VkSampler")
    backend = once(backend, "vkQueueSubmit2(ctx_.queue", "ctx_.d.QueueSubmit2(ctx_.queue")
    backend = once(backend, "vkCmdBindVertexBuffers2(Cb()", "ctx_.d.BindVertexBuffers2(Cb()")
    backend = once(backend, "vkCmdSetPrimitiveTopology(Cb()", "ctx_.d.SetPrimitiveTopology(Cb()")
    # Both pipeline families must specify the matching render pass on older drivers.
    marker = "    VkGraphicsPipelineCreateInfo pi{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, &ri};"
    if backend.count(marker) != 2:
        raise ValueError("BLACK graphics pipeline sites changed")
    first = backend.index(marker) + len(marker)
    backend = backend[:first] + "\n    if (!ctx_.caps.dynamic_rendering) { pi.pNext = nullptr; pi.renderPass = RenderPassFor(rtv, depth_out, samples); }" + backend[first:]
    second = backend.index(marker, first) + len(marker)
    backend = backend[:second] + "\n    if (!ctx_.caps.dynamic_rendering) { pi.pNext = nullptr; pi.renderPass = RenderPassFor(k.has_rt ? rtfmt : VK_FORMAT_UNDEFINED, k.has_ds != 0, std::max(k.msaa, 1u)); }" + backend[second:]
    backend = once(backend, "float c[8] = {uvrect[0], uvrect[1], uvrect[2], uvrect[3], o.gamma ? 1.0f : 0.0f, o.alpha ? 1.0f : 0.0f, float(o.taps), o.mul};",
                   "float c[12] = {uvrect[0], uvrect[1], uvrect[2], uvrect[3], o.gamma ? 1.0f : 0.0f, o.alpha ? 1.0f : 0.0f, float(o.taps), o.mul, float(o.rotation), 0, 0, 0};")
    (stage / "vk_backend.cpp").write_text(backend)

    present = (source / "src/vk/vk_present.cpp").read_text()
    present = once(present, "    ii.UseDynamicRendering = true;", "    ii.UseDynamicRendering = ctx_.caps.dynamic_rendering;")
    marker = "    std::lock_guard lock(app::UiLock());"
    at = present.index(marker, present.index("void VulkanBackend::InitUi()"))
    present = present[:at] + "    if (!ctx_.caps.dynamic_rendering) ii.RenderPass = RenderPassFor(ui_format, false);\n" + present[at:]
    present = once(present, "    f.images.clear();", "    DropSlotFramebuffers();\n    f.images.clear();")
    present = once(present, "    float aspect = game::Aspect();", """    const auto transform = swap_->Transform();
    const uint32_t rotation = transform == VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR ? 1 :
                              transform == VK_SURFACE_TRANSFORM_ROTATE_180_BIT_KHR ? 2 :
                              transform == VK_SURFACE_TRANSFORM_ROTATE_270_BIT_KHR ? 3 : 0;
    float aspect = game::Aspect();""")
    present = once(present, "    Blit(r, uv, {.gamma = gamma_, .rtv = rtv});", "    Blit(r, uv, {.gamma = gamma_, .rtv = rtv, .rotation = rotation});")
    sdk_present = (sdk / "runtime/gpu/vulkan_present.cpp").read_text()
    a = sdk_present.index("    // The game blit and host panel must use the same display pre-rotation.")
    b = sdk_present.index("    ImGui_ImplVulkan_RenderDrawData", a)
    # Apply the SDK panel transform to the draw data used by this renderer.
    present = once(present, "    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), Cb());",
                   "    ImDrawData* draw = ImGui::GetDrawData();\n" + sdk_present[a:b] + "    ImGui_ImplVulkan_RenderDrawData(draw, Cb());")
    (stage / "vk_present.cpp").write_text(present)
    return ["-DXBE_BLACK_BACKEND=" + str(stage / "vk_backend.cpp"),
            "-DXBE_BLACK_PRESENT=" + str(stage / "vk_present.cpp"),
            "-DXBE_PORT_INCLUDE=" + str(stage / "include")]
