// Retained-MUI value-only drag contracts and inventory slot view.

#pragma once

#include "retained_mui_view.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <utility>

namespace snt::ui {

// Generic, value-only drag payload contract.
struct UiDragPayload {
    std::string type;
    std::string resource_key;
    int32_t count = 0;
};

enum class UiDragEventType : uint8_t {
    Begin,
    Enter,
    Leave,
    Drop,
    Cancel,
};

struct UiDragEvent {
    UiDragEventType type = UiDragEventType::Begin;
    std::string source_id;
    std::string target_id;
    UiDragPayload payload{};
};

// Drag sources receive the initiating pointer button as data rather than
// reading host input directly. SlotView uses Primary for a complete stack and
// Secondary for a half-stack split; custom sources may define their own
// value-only affordances without leaking platform events into the contract.
struct UiDragStartContext {
    UiPointerButton pointer_button = UiPointerButton::Primary;
    Vec2 pointer_position{};
};

// Views opt into dragging through narrow interfaces rather than runtime type
// checks. Both interfaces are safe extension points for game-owned widgets;
// the Mod facade remains value-only and never receives these native objects.
class UiDragSource {
public:
    virtual ~UiDragSource() = default;

    [[nodiscard]] virtual std::optional<UiDragPayload> begin_drag(
        const UiDragStartContext& context) const = 0;
    virtual void on_drag_event(const UiDragEvent& event) = 0;
};

class UiDropTarget {
public:
    virtual ~UiDropTarget() = default;

    [[nodiscard]] virtual bool accepts_drop(const UiDragPayload& payload) const = 0;
    virtual void on_drag_event(const UiDragEvent& event) = 0;
};

// Runtime-owned value state for one active pointer drag. It deliberately
// exposes IDs and payload only, so callers can render a preview or issue a
// command without gaining ownership of the retained source/target views.
class UiDragSession final {
public:
    [[nodiscard]] const std::string& source_root_id() const { return source_root_id_; }
    [[nodiscard]] const std::string& source_view_id() const { return source_view_id_; }
    [[nodiscard]] const std::string& hovered_root_id() const { return hovered_root_id_; }
    [[nodiscard]] const std::string& hovered_view_id() const { return hovered_view_id_; }
    [[nodiscard]] const UiDragPayload& payload() const { return payload_; }
    [[nodiscard]] UiPointerButton pointer_button() const { return pointer_button_; }

private:
    friend class UiInputRouter;

    std::string source_root_id_;
    std::string source_view_id_;
    std::string hovered_root_id_;
    std::string hovered_view_id_;
    UiDragPayload payload_{};
    UiPointerButton pointer_button_ = UiPointerButton::Primary;
};

class SlotView : public View, public UiDragSource, public UiDropTarget {
public:
    struct SlotState {
        // `item_key` is the semantic drag payload. Item presentation is
        // intentionally independent so content can replace an icon without
        // changing inventory, networking, or mod command identities.
        std::string item_key;
        std::string image_key;
        std::string overlay_image_key;
        Color image_tint{255, 255, 255, 255};
        int32_t count = 0;
        bool selected = false;
    };
    using DragHandler = std::function<void(const UiDragEvent&)>;

    explicit SlotView(std::string id = {});
    ViewKind kind() const override { return ViewKind::SlotView; }

    void set_slot_state(SlotState state) {
        if (state_.item_key == state.item_key && state_.image_key == state.image_key &&
            state_.overlay_image_key == state.overlay_image_key &&
            state_.image_tint.r == state.image_tint.r &&
            state_.image_tint.g == state.image_tint.g &&
            state_.image_tint.b == state.image_tint.b &&
            state_.image_tint.a == state.image_tint.a && state_.count == state.count &&
            state_.selected == state.selected) {
            return;
        }
        state_ = std::move(state);
    }
    const SlotState& slot_state() const { return state_; }
    void set_drag_handler(DragHandler handler) { drag_handler_ = std::move(handler); }

    void measure(MeasureSpec width, MeasureSpec height, TextEngine& text_engine) override;
    void paint(Arc2DCommandBuffer& out,
               TextEngine& text_engine,
               const UiTheme& theme) const override;
    [[nodiscard]] std::optional<UiDragPayload> begin_drag(
        const UiDragStartContext& context) const override;
    [[nodiscard]] bool accepts_drop(const UiDragPayload& payload) const override;
    void on_drag_event(const UiDragEvent& event) override;

private:
    SlotState state_{};
    DragHandler drag_handler_;
    bool drag_source_ = false;
    bool drag_hovered_ = false;
};

}  // namespace snt::ui
