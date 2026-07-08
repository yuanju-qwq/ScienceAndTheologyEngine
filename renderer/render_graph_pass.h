// Render graph pass: a unit of rendering work declared by the application.
//
// Each pass declares its input/output resources and an execute callback.
// The graph (P2.2) orders passes by dependency; the callback is invoked
// at execute time with a CommandContext ready for recording.
//
// P2.3 (option B): attachments are declared explicitly as color/depth
// slots. The graph inserts `vkCmdPipelineBarrier` before execute() to
// transition each attachment from its current layout to the layout
// required by dynamic rendering (COLOR_ATTACHMENT_OPTIMAL /
// DEPTH_ATTACHMENT_OPTIMAL). Pass callbacks must NOT call
// vkCmdBeginRenderPass / vkCmdEndRenderPass — instead they use
// CommandContext::begin_rendering() / end_rendering(), which wrap
// vkCmdBeginRendering / vkCmdEndRendering (Vulkan 1.3 core).
//
// Reference pattern: Granite Renderer::RenderPass (callback-based execute)
// with vkb-style attachment declarations.

#pragma once

#include "renderer/render_graph_resource.h"

#include <vulkan/vulkan.h>

#include <functional>
#include <string>
#include <vector>

namespace snt::render_backend {
class CommandContext;
}

namespace snt::renderer {

// Attachment declaration: which resource + how it's used in this pass.
// Used for non-attachment resource dependencies (shader read, transfer).
struct PassAttachment {
    RenderResource resource;
    ResourceUsage usage = ResourceUsage::kNone;
};

// Color attachment slot for dynamic rendering.
// `resource` must reference an imported or transient texture.
// `load_op` / `store_op` follow VkAttachmentLoadOp / VkAttachmentStoreOp
// semantics. `clear_value` is used only when load_op == VK_ATTACHMENT_LOAD_OP_CLEAR.
struct ColorAttachmentDecl {
    RenderResource resource;
    VkAttachmentLoadOp load_op = VK_ATTACHMENT_LOAD_OP_CLEAR;
    VkAttachmentStoreOp store_op = VK_ATTACHMENT_STORE_OP_STORE;
    VkClearValue clear_value{};
};

// Depth/stencil attachment slot for dynamic rendering.
// Same semantics as ColorAttachmentDecl; only depth aspect is used
// (stencil ops default to DONT_CARE).
struct DepthAttachmentDecl {
    RenderResource resource;
    VkAttachmentLoadOp load_op = VK_ATTACHMENT_LOAD_OP_CLEAR;
    VkAttachmentStoreOp store_op = VK_ATTACHMENT_STORE_OP_STORE;
    VkClearValue clear_value{};
};

// Execute callback signature.
// The CommandContext is already in recording state when the callback runs;
// the pass should NOT call begin/end_recording (graph manages lifecycle).
// The graph has already inserted pre-pass barriers + called
// CommandContext::begin_rendering() before invoking the callback; the
// callback must call CommandContext::end_rendering() before returning
// (or leave the render pass scope to the graph — see P2.3 contract).
using PassExecuteCallback =
    std::function<void(snt::render_backend::CommandContext&)>;

// A single render pass node in the graph.
// Owned by RenderGraph; the application fills in fields after add_pass().
struct RenderGraphPass {
    std::string name;

    // Non-attachment resource dependencies (shader read, transfer src/dst).
    // The graph inserts barriers for these before execute().
    std::vector<PassAttachment> inputs;
    std::vector<PassAttachment> outputs;

    // Attachment declarations for dynamic rendering. If color_attachments
    // and depth_attachment are both empty, the pass runs without a render
    // pass scope (e.g. compute-only or pure transfer passes).
    std::vector<ColorAttachmentDecl> color_attachments;

    // Optional depth attachment. `resource.valid()` decides whether it's set.
    DepthAttachmentDecl depth_attachment;

    // Explicit pass-to-pass dependencies (by name). The graph topologically
    // sorts passes using these edges before execute (P2.3). A pass with no
    // dependencies runs first. Cycles are rejected with an error.
    //
    // Rationale: P2.3 starts with explicit dependencies (simpler than
    // deriving from attachment read/write sets). Implicit dependency
    // derivation from attachment overlap lands in P2.5 along with the
    // transient resource pool.
    std::vector<std::string> depends_on;

    PassExecuteCallback execute;
};

}  // namespace snt::renderer
