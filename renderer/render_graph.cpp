// RenderGraph implementation.
//
// P2.2 scope:
//   - init() creates a VkCommandPool on the graphics family.
//   - add_pass() stores a RenderGraphPass and returns a pointer for filling.
//   - execute() iterates passes in registration order, for each:
//       * acquire a CommandContext (one per pass, recycled after submit)
//       * begin_recording -> invoke pass.execute(ctx) -> end_recording
//       * submit to graphics queue + wait for completion (serial execution)
//   - reset() clears the pass list but keeps the command pool.
//
// P2.3 will add: dependency-driven ordering, automatic barriers,
// transient resource allocation.

#define SNT_LOG_CHANNEL "renderer"
#include "core/log.h"

#include "renderer/render_graph.h"
#include "renderer/transient_pool.h"

#include "render_backend/command_context.h"
#include "render_backend/vulkan_device.h"

#include <volk.h>
#include <vk_mem_alloc.h>

#include <deque>
#include <format>
#include <unordered_map>
#include <vector>

namespace snt::renderer {

// ---------------------------------------------------------------------------
// ResourceEntry: tracks a single registered resource (transient or external).
// ---------------------------------------------------------------------------
struct ResourceEntry {
    ResourceType type = ResourceType::kInvalid;

    // For transient textures: descriptor + lazy-allocated Vulkan objects.
    TextureDesc tex_desc;
    BufferDesc  buf_desc;
    VkImage     image       = VK_NULL_HANDLE;
    VkImageView view        = VK_NULL_HANDLE;
    VkBuffer    buffer      = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;

    // External resources: graph does not own these.
    bool is_external = false;

    // Current layout (tracked for barrier insertion, P2.3.5).
    VkImageLayout current_layout = VK_IMAGE_LAYOUT_UNDEFINED;

    // Desired terminal layout (P2.3.6). Imported resources like swapchain
    // images must end the frame in PRESENT_SRC_KHR; transient resources
    // have no terminal requirement (kUndefined). The graph inserts a final
    // barrier after all passes if current_layout != terminal_layout.
    VkImageLayout terminal_layout = VK_IMAGE_LAYOUT_UNDEFINED;
    bool has_terminal_layout = false;
};

// ---------------------------------------------------------------------------
// Impl: holds all state that depends on Vulkan types.
// ---------------------------------------------------------------------------
struct RenderGraph::Impl {
    snt::render_backend::VulkanDevice* device = nullptr;
    VkCommandPool command_pool = VK_NULL_HANDLE;
    uint32_t frames_in_flight = 2;

    // Registered passes. Pointers handed out by add_pass() index into here.
    // reset() clears this vector; new add_pass() calls reallocate.
    std::vector<RenderGraphPass> passes;

    // CommandContext pool, indexed by [frame_index][pass_index].
    std::vector<std::vector<snt::render_backend::CommandContext>> contexts;

    // Resource pool: id -> ResourceEntry. ids are assigned sequentially.
    std::vector<ResourceEntry> resources;
    uint32_t next_resource_id = 0;

    // Transient resource allocator (VMA-backed).
    TransientPool transient_pool;
};

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------
RenderGraph::RenderGraph() : impl_(std::make_unique<Impl>()) {
    SNT_LOG_INFO("RenderGraph created (P2.2)");
}

RenderGraph::~RenderGraph() {
    destroy();
}

snt::core::Expected<void> RenderGraph::init(snt::render_backend::VulkanDevice& device,
                       uint32_t frames_in_flight) {
    if (impl_->device != nullptr) {
        return snt::core::Error{snt::core::ErrorCode::kInvalidState,
                                "RenderGraph::init already called"};
    }
    if (frames_in_flight == 0) {
        return snt::core::Error{snt::core::ErrorCode::kInvalidArgument,
                                "RenderGraph::init: frames_in_flight=0"};
    }
    impl_->device = &device;
    impl_->frames_in_flight = frames_in_flight;

    // Create a command pool with RESET_COMMAND_BUFFER flag. We need
    // individual buffer resets because each pass records fresh each frame.
    VkCommandPoolCreateInfo pool_info{
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = device.graphics_family(),
    };
    if (vkCreateCommandPool(device.logical(), &pool_info, nullptr,
                            &impl_->command_pool) != VK_SUCCESS) {
        return snt::core::Error{snt::core::ErrorCode::kVulkanCommandPoolFailed,
                                "RenderGraph: vkCreateCommandPool failed"};
    }

    // Pre-size the per-frame CommandContext slots. Each slot will hold
    // `max_passes` contexts, grown lazily on execute_record_only().
    impl_->contexts.resize(frames_in_flight);

    // Initialize the transient resource pool with VMA.
    if (auto r = impl_->transient_pool.init(device.vma_allocator()); !r) {
        snt::core::Error e = r.error();
        e.with_context("RenderGraph::init");
        return e;
    }

    SNT_LOG_INFO("RenderGraph initialized (frames_in_flight=%u)", frames_in_flight);
    return {};
}

void RenderGraph::destroy() {
    if (!impl_->device) return;

    // Wait idle before tearing down any resources.
    impl_->device->wait_idle();

    // CommandContexts own command buffers allocated from our pool; reset
    // them first so the buffers are freed back to the pool.
    impl_->contexts.clear();
    impl_->passes.clear();

    // Release transient resources + TransientPool.
    impl_->resources.clear();
    impl_->transient_pool.destroy();

    if (impl_->command_pool != VK_NULL_HANDLE) {
        vkDestroyCommandPool(impl_->device->logical(), impl_->command_pool,
                             nullptr);
        impl_->command_pool = VK_NULL_HANDLE;
    }

    impl_->device = nullptr;
}

// ---------------------------------------------------------------------------
// Resource creation / import (P2.3)
// ---------------------------------------------------------------------------
// Transient resources are registered with a descriptor but NOT allocated
// yet. Allocation happens lazily on first use inside execute_record_only()
// (P2.3.6 will wire the lazy alloc). For P2.3.4 (dependency derivation) we
// only need the descriptor + a valid handle.
// ---------------------------------------------------------------------------
RenderResource RenderGraph::create_texture(const TextureDesc& desc) {
    RenderResource r;
    r.id = impl_->next_resource_id++;
    r.type = ResourceType::kTexture;

    ResourceEntry e{};
    e.type = ResourceType::kTexture;
    e.tex_desc = desc;
    e.current_layout = VK_IMAGE_LAYOUT_UNDEFINED;
    impl_->resources.push_back(e);
    return r;
}

RenderResource RenderGraph::create_buffer(const BufferDesc& desc) {
    RenderResource r;
    r.id = impl_->next_resource_id++;
    r.type = ResourceType::kBuffer;

    ResourceEntry e{};
    e.type = ResourceType::kBuffer;
    e.buf_desc = desc;
    impl_->resources.push_back(e);
    return r;
}

RenderResource RenderGraph::import_texture(VkImage image, VkImageView view,
                                           VkFormat format,
                                           VkImageLayout current_layout,
                                           uint32_t width, uint32_t height,
                                           uint32_t mip_levels,
                                           uint32_t array_layers,
                                           VkImageLayout terminal_layout) {
    RenderResource r;
    r.id = impl_->next_resource_id++;
    r.type = ResourceType::kTexture;

    ResourceEntry e{};
    e.type = ResourceType::kTexture;
    e.image = image;
    e.view = view;
    e.is_external = true;
    e.current_layout = current_layout;
    e.terminal_layout = terminal_layout;
    e.has_terminal_layout = (terminal_layout != VK_IMAGE_LAYOUT_UNDEFINED);
    // Fill tex_desc so the graph can derive render area + correct barrier
    // subresource ranges. width/height/mip_levels/array_layers come from
    // the caller (swapchain extent or depth image extent).
    e.tex_desc.format = static_cast<uint32_t>(format);
    e.tex_desc.width = width;
    e.tex_desc.height = height;
    e.tex_desc.mip_levels = mip_levels;
    e.tex_desc.array_layers = array_layers;
    impl_->resources.push_back(e);
    return r;
}

// ---------------------------------------------------------------------------
// Pass registration
// ---------------------------------------------------------------------------
RenderGraphPass* RenderGraph::add_pass(const std::string& name) {
    if (!impl_->device) {
        SNT_LOG_ERROR("RenderGraph::add_pass called before init");
        return nullptr;
    }
    impl_->passes.push_back(RenderGraphPass{.name = name});
    return &impl_->passes.back();
}

// ---------------------------------------------------------------------------
// Execute: serial pass-by-pass submission (P2.2 baseline, Mode A)
// ---------------------------------------------------------------------------
snt::core::Expected<void> RenderGraph::execute() {
    // Mode A uses frame slot 0 by default (no frames-in-flight pipelining).
    if (auto r = execute_record_only(0); !r) {
        snt::core::Error e = r.error();
        e.with_context("RenderGraph::execute");
        return e;
    }

    // Submit each context's command buffer + wait serially.
    // This is the baseline; P2.3 will replace with dependency scheduling.
    auto& slot = impl_->contexts[0];
    for (size_t i = 0; i < impl_->passes.size(); ++i) {
        snt::render_backend::CommandContext& ctx = slot[i];
        VkCommandBuffer cb = ctx.handle();

        VkSubmitInfo submit_info{
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .commandBufferCount = 1,
            .pCommandBuffers = &cb,
        };

        VkFenceCreateInfo fence_info{
            .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        };
        VkFence fence = VK_NULL_HANDLE;
        if (vkCreateFence(impl_->device->logical(), &fence_info, nullptr,
                          &fence) != VK_SUCCESS) {
            return snt::core::Error{snt::core::ErrorCode::kVulkanCommandBufferFailed,
                                    std::format("Pass {}: vkCreateFence failed", i)};
        }

        if (vkQueueSubmit(impl_->device->graphics_queue(), 1, &submit_info,
                          fence) != VK_SUCCESS) {
            vkDestroyFence(impl_->device->logical(), fence, nullptr);
            return snt::core::Error{snt::core::ErrorCode::kVulkanCommandBufferFailed,
                                    std::format("Pass {}: vkQueueSubmit failed", i)};
        }

        // Wait for this pass to finish before starting the next.
        vkWaitForFences(impl_->device->logical(), 1, &fence, VK_TRUE,
                        UINT64_MAX);
        vkDestroyFence(impl_->device->logical(), fence, nullptr);
    }
    return {};
}

// ---------------------------------------------------------------------------
// ResourceUsage → Vulkan layout / access mask / stage mask mapping (P2.3.3)
// ---------------------------------------------------------------------------
// Used by barrier insertion to compute the target layout for each resource
// when it enters a pass. Covers both attachment usages (color/depth output)
// and non-attachment usages (shader read, transfer src/dst).
// ---------------------------------------------------------------------------
static VkImageLayout usage_to_image_layout(ResourceUsage u) {
    switch (u) {
        case ResourceUsage::kShaderRead:    return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        case ResourceUsage::kColorOutput:   return VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        case ResourceUsage::kDepthOutput:   return VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        case ResourceUsage::kTransferSrc:   return VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        case ResourceUsage::kTransferDst:   return VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        case ResourceUsage::kNone:
        default:                            return VK_IMAGE_LAYOUT_UNDEFINED;
    }
}

static VkAccessFlags usage_to_access_mask(ResourceUsage u) {
    switch (u) {
        case ResourceUsage::kShaderRead:    return VK_ACCESS_SHADER_READ_BIT;
        case ResourceUsage::kColorOutput:   return VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                                                     VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        case ResourceUsage::kDepthOutput:   return VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                                                     VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        case ResourceUsage::kTransferSrc:   return VK_ACCESS_TRANSFER_READ_BIT;
        case ResourceUsage::kTransferDst:   return VK_ACCESS_TRANSFER_WRITE_BIT;
        case ResourceUsage::kNone:
        default:                            return 0;
    }
}

static VkPipelineStageFlags usage_to_stage_mask(ResourceUsage u) {
    switch (u) {
        case ResourceUsage::kShaderRead:    return VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        case ResourceUsage::kColorOutput:   return VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        case ResourceUsage::kDepthOutput:   return VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                                                     VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        case ResourceUsage::kTransferSrc:
        case ResourceUsage::kTransferDst:   return VK_PIPELINE_STAGE_TRANSFER_BIT;
        case ResourceUsage::kNone:
        default:                            return VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    }
}

// ---------------------------------------------------------------------------
// Insert a single image memory barrier on `cmd` for `entry`.
// Transitions `entry.current_layout` -> `target_layout`, then updates
// `entry.current_layout` to the new layout. No-op if layouts match.
// `src_access` / `dst_access` / `src_stage` / `dst_stage` control the
// synchronization scope; callers compute them from ResourceUsage.
// ---------------------------------------------------------------------------
static void transition_image_layout(VkCommandBuffer cmd,
                                    ResourceEntry& entry,
                                    VkImageLayout target_layout,
                                    VkAccessFlags src_access,
                                    VkAccessFlags dst_access,
                                    VkPipelineStageFlags src_stage,
                                    VkPipelineStageFlags dst_stage) {
    if (entry.image == VK_NULL_HANDLE) return;  // lazy alloc not wired yet
    if (entry.current_layout == target_layout) return;

    VkImageMemoryBarrier barrier{
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = src_access,
        .dstAccessMask = dst_access,
        .oldLayout = entry.current_layout,
        .newLayout = target_layout,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = entry.image,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = entry.tex_desc.mip_levels,
            .baseArrayLayer = 0,
            .layerCount = entry.tex_desc.array_layers,
        },
    };

    // Depth/stencil formats need the depth aspect mask.
    VkFormat fmt = static_cast<VkFormat>(entry.tex_desc.format);
    if (fmt >= VK_FORMAT_D16_UNORM && fmt <= VK_FORMAT_D32_SFLOAT_S8_UINT) {
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    }

    vkCmdPipelineBarrier(cmd, src_stage, dst_stage, 0,
                         0, nullptr,
                         0, nullptr,
                         1, &barrier);

    entry.current_layout = target_layout;
}

// ---------------------------------------------------------------------------
// Implicit dependency derivation (P2.3.4)
// ---------------------------------------------------------------------------
// Derives pass-to-pass dependencies from attachment read/write overlap.
// Rule: if pass B reads a resource that pass A writes, B depends on A.
// Writes are merged into `depends_on` before topological sort.
// ---------------------------------------------------------------------------
static void derive_implicit_dependencies(std::vector<RenderGraphPass>* passes) {
    const size_t n = passes->size();

    // Map: resource id -> list of (pass_index, is_write).
    std::unordered_map<uint32_t, std::vector<std::pair<size_t, bool>>> readers_writers;
    for (size_t i = 0; i < n; ++i) {
        RenderGraphPass& p = (*passes)[i];
        for (const auto& a : p.inputs) {
            readers_writers[a.resource.id].emplace_back(i, /*is_write=*/false);
        }
        for (const auto& a : p.outputs) {
            readers_writers[a.resource.id].emplace_back(i, /*is_write=*/true);
        }
        // Color/depth attachment writes count as writes for dependency
        // derivation. Reads of color/depth from a previous pass
        // (e.g. shadow map sampled in forward) must go through `inputs`
        // with kShaderRead.
        for (const auto& c : p.color_attachments) {
            readers_writers[c.resource.id].emplace_back(i, /*is_write=*/true);
        }
        if (p.depth_attachment.resource.valid()) {
            readers_writers[p.depth_attachment.resource.id].emplace_back(i, /*is_write=*/true);
        }
    }

    // For each resource: every reader depends on every writer that came
    // before it (by registration order). This is the RAW / WAW / WAR
    // dependency; for simplicity we add writer→reader edges only (RAW).
    for (auto& kv : readers_writers) {
        const auto& accesses = kv.second;
        for (const auto& [reader_idx, is_read_write] : accesses) {
            if (is_read_write) continue;  // skip writers
            for (const auto& [writer_idx, w_is_write] : accesses) {
                if (!w_is_write) continue;
                if (writer_idx == reader_idx) continue;
                // reader depends on writer
                RenderGraphPass& reader = (*passes)[reader_idx];
                const std::string& writer_name = (*passes)[writer_idx].name;
                // Avoid duplicate entries.
                bool exists = false;
                for (const auto& d : reader.depends_on) {
                    if (d == writer_name) { exists = true; break; }
                }
                if (!exists) {
                    reader.depends_on.push_back(writer_name);
                }
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Topological sort (P2.3)
// ---------------------------------------------------------------------------
// Orders passes by their `depends_on` edges. Returns a permutation of pass
// indices; on cycle detection returns false + writes the offending pass name.
// ---------------------------------------------------------------------------
static bool topological_sort(const std::vector<RenderGraphPass>& passes,
                             std::vector<size_t>* out_order,
                             std::string* out_cycle_name) {
    const size_t n = passes.size();
    out_order->clear();
    out_order->reserve(n);

    // Build name -> index map.
    std::unordered_map<std::string, size_t> name_to_index;
    for (size_t i = 0; i < n; ++i) {
        name_to_index[passes[i].name] = i;
    }

    // Build adjacency list + in-degree count.
    std::vector<std::vector<size_t>> adj(n);  // adj[u] = passes that depend on u
    std::vector<size_t> in_degree(n, 0);
    for (size_t i = 0; i < n; ++i) {
        for (const auto& dep_name : passes[i].depends_on) {
            auto it = name_to_index.find(dep_name);
            if (it == name_to_index.end()) {
                SNT_LOG_ERROR("Pass '%s' depends on unknown pass '%s'",
                              passes[i].name.c_str(), dep_name.c_str());
                return false;
            }
            size_t dep_idx = it->second;
            adj[dep_idx].push_back(i);
            ++in_degree[i];
        }
    }

    // Kahn's algorithm: start with in_degree==0 passes, process in order.
    std::deque<size_t> queue;
    for (size_t i = 0; i < n; ++i) {
        if (in_degree[i] == 0) queue.push_back(i);
    }

    while (!queue.empty()) {
        size_t u = queue.front();
        queue.pop_front();
        out_order->push_back(u);
        for (size_t v : adj[u]) {
            if (--in_degree[v] == 0) {
                queue.push_back(v);
            }
        }
    }

    if (out_order->size() != n) {
        // Cycle: some passes never reached in_degree 0.
        if (out_cycle_name) {
            for (size_t i = 0; i < n; ++i) {
                if (in_degree[i] > 0) {
                    *out_cycle_name = passes[i].name;
                    break;
                }
            }
        }
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// execute_record_only: record all passes, do not submit (Mode B)
// ---------------------------------------------------------------------------
// `frame_index` selects which frame slot's CommandContexts to use. The
// caller MUST pass the same frame_index it uses for its frames-in-flight
// synchronization (e.g. VulkanFrame::current_frame()) so that a command
// buffer is not reset while still pending on the GPU.
//
// P2.3: passes are topologically sorted by their `depends_on` edges before
// recording. The recorded command buffer at slot[frame_index][pass_index]
// corresponds to the pass at `passes[sorted_order[pass_index]]`.
// ---------------------------------------------------------------------------
snt::core::Expected<void> RenderGraph::execute_record_only(uint32_t frame_index) {
    if (!impl_->device) {
        return snt::core::Error{snt::core::ErrorCode::kInvalidState,
                                "RenderGraph::execute called before init"};
    }
    if (frame_index >= impl_->frames_in_flight) {
        return snt::core::Error{snt::core::ErrorCode::kInvalidArgument,
                                std::format("execute_record_only: frame_index {} >= frames_in_flight {}",
                                            frame_index, impl_->frames_in_flight)};
    }
    if (impl_->passes.empty()) {
        return {};  // nothing to do — not an error
    }

    // --- Derive implicit dependencies from attachment overlap (P2.3.4) ---
    derive_implicit_dependencies(&impl_->passes);

    // --- Topological sort (P2.3) ---
    std::vector<size_t> order;
    std::string cycle_name;
    if (!topological_sort(impl_->passes, &order, &cycle_name)) {
        return snt::core::Error{snt::core::ErrorCode::kInvalidState,
                                std::format("RenderGraph: dependency cycle detected (starting at '{}')",
                                            cycle_name)};
    }

    auto& slot = impl_->contexts[frame_index];
    if (slot.size() < impl_->passes.size()) {
        slot.resize(impl_->passes.size());
    }

    // Helper: look up a ResourceEntry by id. Returns nullptr if out of range.
    auto lookup_entry = [&](uint32_t id) -> ResourceEntry* {
        if (id >= impl_->resources.size()) return nullptr;
        return &impl_->resources[id];
    };

    // Record passes in topological order. The recorded command buffer at
    // slot[pass_index] corresponds to the pass at passes[order[pass_index]].
    for (size_t pass_index = 0; pass_index < order.size(); ++pass_index) {
        RenderGraphPass& pass = impl_->passes[order[pass_index]];
        snt::render_backend::CommandContext& ctx = slot[pass_index];

        if (!pass.execute) {
            return snt::core::Error{snt::core::ErrorCode::kInvalidState,
                                    std::format("Pass '{}' has no execute callback", pass.name)};
        }

        if (auto r = ctx.begin_recording(*impl_->device, impl_->command_pool); !r) {
            snt::core::Error e = r.error();
            e.with_context(std::format("Pass '{}': begin_recording", pass.name));
            return e;
        }

        VkCommandBuffer cmd = ctx.handle();

        // --- P2.3.5: insert pre-pass barriers for non-attachment resources ---
        // inputs/outputs use ResourceUsage to compute target layout. The
        // src access/stage are derived from current_layout (best-effort:
        // we use the same mask as the new usage; this is conservative but
        // correct for the simple read-before-write cases we support).
        for (const auto& in : pass.inputs) {
            ResourceEntry* e = lookup_entry(in.resource.id);
            if (!e) continue;
            VkImageLayout target = usage_to_image_layout(in.usage);
            transition_image_layout(cmd, *e, target,
                                    /*src_access=*/0,
                                    usage_to_access_mask(in.usage),
                                    /*src_stage=*/VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                    usage_to_stage_mask(in.usage));
        }
        for (const auto& out : pass.outputs) {
            ResourceEntry* e = lookup_entry(out.resource.id);
            if (!e) continue;
            VkImageLayout target = usage_to_image_layout(out.usage);
            transition_image_layout(cmd, *e, target,
                                    /*src_access=*/0,
                                    usage_to_access_mask(out.usage),
                                    /*src_stage=*/VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                    usage_to_stage_mask(out.usage));
        }

        // --- P2.3.5: insert pre-pass barriers for color/depth attachments ---
        // Attachments transition to COLOR_ATTACHMENT_OPTIMAL /
        // DEPTH_STENCIL_ATTACHMENT_OPTIMAL before dynamic rendering begins.
        for (const auto& c : pass.color_attachments) {
            ResourceEntry* e = lookup_entry(c.resource.id);
            if (!e) continue;
            transition_image_layout(cmd, *e,
                                    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                    /*src_access=*/0,
                                    VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                                        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                                    /*src_stage=*/VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
        }
        if (pass.depth_attachment.resource.valid()) {
            ResourceEntry* e = lookup_entry(pass.depth_attachment.resource.id);
            if (e) {
                transition_image_layout(cmd, *e,
                                        VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                                        /*src_access=*/0,
                                        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                                            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                                        /*src_stage=*/VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                                            VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT);
            }
        }

        // --- P2.3 (option B): begin dynamic rendering ---
        // If the pass declared any attachments, wrap the execute callback
        // in vkCmdBeginRendering / vkCmdEndRendering so the callback only
        // records draw/dispatch calls. Passes with no attachments (compute,
        // transfer) skip this and execute raw.
        const bool has_attachments = !pass.color_attachments.empty() ||
                                     pass.depth_attachment.resource.valid();
        // VkRenderingAttachmentInfo must remain in scope until
        // vkCmdBeginRendering returns; declare outside the if-block.
        std::vector<VkRenderingAttachmentInfo> color_attachment_infos;
        VkRenderingAttachmentInfo depth_attachment_info{};
        VkRenderingInfo rendering_info{};
        if (has_attachments) {
            // Build color attachment infos.
            color_attachment_infos.reserve(pass.color_attachments.size());
            for (const auto& c : pass.color_attachments) {
                ResourceEntry* e = lookup_entry(c.resource.id);
                VkRenderingAttachmentInfo info{
                    .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
                    .imageView = e ? e->view : VK_NULL_HANDLE,
                    .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                    .loadOp = c.load_op,
                    .storeOp = c.store_op,
                    .clearValue = c.clear_value,
                };
                color_attachment_infos.push_back(info);
            }

            // Build depth attachment info if declared.
            if (pass.depth_attachment.resource.valid()) {
                ResourceEntry* e = lookup_entry(pass.depth_attachment.resource.id);
                depth_attachment_info = {
                    .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
                    .imageView = e ? e->view : VK_NULL_HANDLE,
                    .imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                    .loadOp = pass.depth_attachment.load_op,
                    .storeOp = pass.depth_attachment.store_op,
                    .clearValue = pass.depth_attachment.clear_value,
                };
            }

            // Render area: derive from the first color attachment's extent,
            // or depth if no color. Falls back to 0x0 if unknown (caller
            // must ensure at least one attachment has a known extent).
            uint32_t w = 0, h = 0;
            if (!pass.color_attachments.empty()) {
                ResourceEntry* e = lookup_entry(pass.color_attachments[0].resource.id);
                if (e) { w = e->tex_desc.width; h = e->tex_desc.height; }
            } else if (pass.depth_attachment.resource.valid()) {
                ResourceEntry* e = lookup_entry(pass.depth_attachment.resource.id);
                if (e) { w = e->tex_desc.width; h = e->tex_desc.height; }
            }

            rendering_info = {
                .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
                .renderArea = {.offset = {0, 0}, .extent = {w, h}},
                .layerCount = 1,
                .colorAttachmentCount =
                    static_cast<uint32_t>(color_attachment_infos.size()),
                .pColorAttachments = color_attachment_infos.data(),
                .pDepthAttachment =
                    pass.depth_attachment.resource.valid() ? &depth_attachment_info
                                                            : nullptr,
            };

            ctx.begin_rendering(rendering_info);
        }

        pass.execute(ctx);

        if (has_attachments) {
            ctx.end_rendering();
        }

        // --- P2.3.6: terminal barriers ---
        // On the LAST pass (by topological order), append terminal layout
        // transitions for imported resources (e.g. swapchain → PRESENT_SRC)
        // BEFORE end_recording(). This keeps everything on one command
        // buffer per pass, avoiding dangling trailing buffers across frames.
        if (pass_index == order.size() - 1) {
            for (auto& e : impl_->resources) {
                if (!e.has_terminal_layout) continue;
                if (e.current_layout == e.terminal_layout) continue;
                if (e.image == VK_NULL_HANDLE) continue;

                // Source access/stage derived from the producer usage.
                // PRESENT_SRC_KHR is fed by color attachment output.
                VkAccessFlags src_access = 0;
                VkPipelineStageFlags src_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
                if (e.terminal_layout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR) {
                    src_access = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
                    src_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
                }

                transition_image_layout(cmd, e, e.terminal_layout,
                                        src_access,
                                        /*dst_access=*/0,
                                        src_stage,
                                        VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
            }
        }

        ctx.end_recording();
    }

    return {};
}

// ---------------------------------------------------------------------------
// Per-frame reset
// ---------------------------------------------------------------------------
void RenderGraph::reset() {
    impl_->passes.clear();
    // Clear per-frame transient resources (external resources are also
    // cleared — callers must re-import them each frame if they want to
    // reference external images as attachments).
    impl_->resources.clear();
    impl_->next_resource_id = 0;
    impl_->transient_pool.reset();
    // CommandContexts are kept (their command buffers are reusable).
}

// ---------------------------------------------------------------------------
// Access recorded command buffer (Mode B helper)
// ---------------------------------------------------------------------------
VkCommandBuffer RenderGraph::recorded_command_buffer(uint32_t frame_index,
                                                     size_t pass_index) const {
    if (frame_index >= impl_->contexts.size()) return VK_NULL_HANDLE;
    const auto& slot = impl_->contexts[frame_index];
    if (pass_index >= slot.size()) return VK_NULL_HANDLE;
    return slot[pass_index].handle();
}

uint32_t RenderGraph::recorded_command_buffers(uint32_t frame_index,
                                               std::vector<VkCommandBuffer>* out_buffers) const {
    out_buffers->clear();
    if (frame_index >= impl_->contexts.size()) return 0;
    const auto& slot = impl_->contexts[frame_index];
    out_buffers->reserve(slot.size());
    for (const auto& ctx : slot) {
        VkCommandBuffer cb = ctx.handle();
        if (cb != VK_NULL_HANDLE) {
            out_buffers->push_back(cb);
        }
    }
    return static_cast<uint32_t>(out_buffers->size());
}

}  // namespace snt::renderer
