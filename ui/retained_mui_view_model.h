// Retained-MUI observable value-model interface.

#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <variant>

namespace snt::ui {

using BindingValue = std::variant<std::monostate, bool, int64_t, double, std::string>;

class ViewModel {
private:
    struct State;

public:
    using Observer = std::function<void(std::string_view, const BindingValue&)>;

    // A subscription owns one observer registration. It is movable but not
    // copyable; destroying it is safe even after its ViewModel has died.
    class Subscription {
    public:
        Subscription() = default;
        ~Subscription();

        Subscription(const Subscription&) = delete;
        Subscription& operator=(const Subscription&) = delete;
        Subscription(Subscription&& other) noexcept;
        Subscription& operator=(Subscription&& other) noexcept;

        void reset();
        bool connected() const;

    private:
        friend class ViewModel;

        Subscription(std::weak_ptr<State> state, std::string key, uint64_t observer_id);

        std::weak_ptr<State> state_;
        std::string key_;
        uint64_t observer_id_ = 0;
    };

    ViewModel();
    ~ViewModel();

    ViewModel(const ViewModel&) = delete;
    ViewModel& operator=(const ViewModel&) = delete;
    ViewModel(ViewModel&&);
    ViewModel& operator=(ViewModel&&);

    void set(std::string key, BindingValue value);
    const BindingValue* get(std::string_view key) const;

    [[nodiscard]] Subscription bind(std::string key, Observer observer);

private:
    std::shared_ptr<State> state_;
};

}  // namespace snt::ui
