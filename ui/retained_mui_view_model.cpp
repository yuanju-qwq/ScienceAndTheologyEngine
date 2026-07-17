// Retained-MUI observable view-model implementation.
//
// This module owns binding values and subscription lifetime only. It has no
// dependency on view layout, rendering, or platform input.

#include "retained_mui_view_model.h"

#include <algorithm>
#include <unordered_map>
#include <utility>

namespace snt::ui {

struct ViewModel::State {
    struct ObserverSlot {
        uint64_t id = 0;
        Observer callback;
    };

    std::unordered_map<std::string, BindingValue> values;
    std::unordered_map<std::string, std::vector<ObserverSlot>> observers;
    uint64_t next_observer_id = 1;
};

ViewModel::Subscription::Subscription(std::weak_ptr<State> state,
                                      std::string key,
                                      uint64_t observer_id)
    : state_(std::move(state)), key_(std::move(key)), observer_id_(observer_id) {}

ViewModel::Subscription::~Subscription() {
    reset();
}

ViewModel::Subscription::Subscription(Subscription&& other) noexcept
    : state_(std::move(other.state_)),
      key_(std::move(other.key_)),
      observer_id_(std::exchange(other.observer_id_, 0)) {}

ViewModel::Subscription& ViewModel::Subscription::operator=(Subscription&& other) noexcept {
    if (this == &other) return *this;
    reset();
    state_ = std::move(other.state_);
    key_ = std::move(other.key_);
    observer_id_ = std::exchange(other.observer_id_, 0);
    return *this;
}

void ViewModel::Subscription::reset() {
    if (observer_id_ == 0) return;
    if (auto state = state_.lock()) {
        auto it = state->observers.find(key_);
        if (it != state->observers.end()) {
            auto& slots = it->second;
            slots.erase(std::remove_if(slots.begin(), slots.end(),
                                       [id = observer_id_](const State::ObserverSlot& slot) {
                                           return slot.id == id;
                                       }),
                        slots.end());
            if (slots.empty()) state->observers.erase(it);
        }
    }
    state_.reset();
    key_.clear();
    observer_id_ = 0;
}

bool ViewModel::Subscription::connected() const {
    if (observer_id_ == 0) return false;
    const auto state = state_.lock();
    if (!state) return false;
    const auto it = state->observers.find(key_);
    if (it == state->observers.end()) return false;
    return std::any_of(it->second.begin(), it->second.end(),
                       [id = observer_id_](const State::ObserverSlot& slot) {
                           return slot.id == id;
                       });
}

ViewModel::ViewModel()
    : state_(std::make_shared<State>()) {}

ViewModel::~ViewModel() = default;

ViewModel::ViewModel(ViewModel&& other)
    : state_(std::move(other.state_)) {
    if (!state_) state_ = std::make_shared<State>();
    other.state_ = std::make_shared<State>();
}

ViewModel& ViewModel::operator=(ViewModel&& other) {
    if (this == &other) return *this;
    state_ = std::move(other.state_);
    if (!state_) state_ = std::make_shared<State>();
    other.state_ = std::make_shared<State>();
    return *this;
}

void ViewModel::set(std::string key, BindingValue value) {
    const std::string stable_key = key;
    state_->values[std::move(key)] = std::move(value);
    const auto it = state_->observers.find(stable_key);
    if (it == state_->observers.end()) return;

    // Observer callbacks may tear down their own subscriptions. Dispatch a
    // stable copy so the registry remains valid throughout this notification.
    const auto observers = it->second;
    const BindingValue current = state_->values[stable_key];
    for (const State::ObserverSlot& slot : observers) {
        if (slot.callback) slot.callback(stable_key, current);
    }
}

const BindingValue* ViewModel::get(std::string_view key) const {
    const auto it = state_->values.find(std::string(key));
    return it == state_->values.end() ? nullptr : &it->second;
}

ViewModel::Subscription ViewModel::bind(std::string key, Observer observer) {
    if (!observer) return {};

    const uint64_t observer_id = state_->next_observer_id++;
    auto& slots = state_->observers[key];
    slots.push_back({.id = observer_id, .callback = std::move(observer)});
    Subscription subscription(state_, key, observer_id);

    const auto value_it = state_->values.find(key);
    if (value_it != state_->values.end()) {
        // The initial observer may synchronously bind or unsubscribe other
        // callbacks. Invoke copies so a re-entrant vector mutation cannot
        // invalidate the slot currently being called.
        const Observer initial_observer = slots.back().callback;
        const BindingValue initial_value = value_it->second;
        initial_observer(key, initial_value);
    }
    return subscription;
}

}  // namespace snt::ui
