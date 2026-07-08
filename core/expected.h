// Expected<T, E> — a Rust-like Result type for C++20.
//
// Holds either a success value (Ok) or an error (Err). Designed to be
// API-compatible with std::expected<T,E> (C++23) so the codebase can
// migrate later by aliasing Expected to std::expected when C++23 lands.
//
// Design highlights:
//   - [[nodiscard]]: callers cannot silently drop an Expected. They must
//     explicitly check or move it.
//   - Implicit construction from T (Ok) and from E (Err), so functions can
//     write `return value;` or `return Error{...};` naturally.
//   - No exceptions: a deliberately-failing Expected can be constructed
//     from an Error without throwing.
//   - Cheap success path: Ok variant stores T inline (no heap allocation).
//
// Usage:
//   Expected<VulkanDevice*> VulkanDevice::init(...) {
//       if (vkCreateDevice(...) != VK_SUCCESS) {
//           return Error{ErrorCode::kVulkanInitFailed, "vkCreateDevice failed"};
//       }
//       return this;  // implicit Ok
//   }
//
//   Expected<void> do_work() {
//       auto dev = vk_device.init(...);
//       if (!dev) return dev.error().with_context("vk_device.init");
//       ... // use *dev
//       return {};  // Ok<void>
//   }
//
// API parity with std::expected (C++23):
//   has_value()       bool
//   operator bool()   bool (same as has_value)
//   value()           T&        (abort if !has_value; use unchecked in perf)
//   error()           E&        (abort if has_value)
//   operator*()       T&        (unchecked, like std::expected)
//   operator->()      T*        (unchecked)
//   value_or(default) T
//   and_then(f)       Expected<U, E>   (monadic, C++23 parity)
//   or_else(f)        Expected<T, F>   (monadic, C++23 parity)
//   map(f)            Expected<U, E>   (monadic, C++23 parity)
//
// We deliberately omit throwing accessors (std::expected::value() throws
// if !has_value). Engine code should never throw; use has_value() first
// or operator*() with a prior check.

#pragma once

#include <cassert>
#include <concepts>
#include <optional>  // std::nullopt_t for Expected<void>
#include <type_traits>
#include <utility>

#include "core/snt_assert.h"  // SNT_ASSERT (debug-break + abort)
#include "core/error.h"    // default Error type for Expected<T>

namespace snt::core {

// Primary template. Specialization for void below.
template <typename T, typename E = Error>
class Expected {
    static_assert(!std::is_same_v<T, void>,
                  "Use Expected<void> instead of Expected<void, E>");
    static_assert(std::is_object_v<T>, "T must be an object type");
    static_assert(std::is_object_v<E>, "E must be an object type");

public:
    // --- Constructors ---

    // Ok: implicit from T (allows `return value;` in functions).
    template <typename U>
        requires std::convertible_to<U&&, T>
    Expected(U&& value)
        : has_value_(true) {
        new (&storage_) T(std::forward<U>(value));
    }

    // Err: implicit from E.
    template <typename U>
        requires std::convertible_to<U&&, E>
    Expected(U&& err)
        : has_value_(false) {
        new (&storage_) E(std::forward<U>(err));
    }

    // Copy / move / destruct.
    Expected(const Expected& other) : has_value_(other.has_value_) {
        if (has_value_) new (&storage_) T(other.value_unchecked());
        else            new (&storage_) E(other.error_unchecked());
    }
    Expected(Expected&& other) noexcept(
        std::is_nothrow_move_constructible_v<T> &&
        std::is_nothrow_move_constructible_v<E>)
        : has_value_(other.has_value_) {
        if (has_value_) new (&storage_) T(std::move(other.value_unchecked()));
        else            new (&storage_) E(std::move(other.error_unchecked()));
    }
    Expected& operator=(const Expected& other) {
        if (this != &other) {
            destroy();
            has_value_ = other.has_value_;
            if (has_value_) new (&storage_) T(other.value_unchecked());
            else            new (&storage_) E(other.error_unchecked());
        }
        return *this;
    }
    Expected& operator=(Expected&& other) noexcept(
        std::is_nothrow_move_constructible_v<T> &&
        std::is_nothrow_move_constructible_v<E>) {
        if (this != &other) {
            destroy();
            has_value_ = other.has_value_;
            if (has_value_) new (&storage_) T(std::move(other.value_unchecked()));
            else            new (&storage_) E(std::move(other.error_unchecked()));
        }
        return *this;
    }
    ~Expected() { destroy(); }

    // --- Inspectors ---

    [[nodiscard]] bool has_value() const { return has_value_; }
    [[nodiscard]] explicit operator bool() const { return has_value_; }

    // Checked access — aborts in Debug if wrong variant is accessed.
    T& value() {
        SNT_ASSERT_MSG(has_value_, "Expected::value() called on Err variant");
        return value_unchecked();
    }
    const T& value() const {
        SNT_ASSERT_MSG(has_value_, "Expected::value() called on Err variant");
        return value_unchecked();
    }
    E& error() {
        SNT_ASSERT_MSG(!has_value_, "Expected::error() called on Ok variant");
        return error_unchecked();
    }
    const E& error() const {
        SNT_ASSERT_MSG(!has_value_, "Expected::error() called on Ok variant");
        return error_unchecked();
    }

    // Unchecked access — caller is responsible for checking has_value().
    T& operator*() { return value_unchecked(); }
    const T& operator*() const { return value_unchecked(); }
    T* operator->() { return &value_unchecked(); }
    const T* operator->() const { return &value_unchecked(); }

    // value_or: returns the success value or a fallback.
    template <typename U>
    T value_or(U&& fallback) const {
        return has_value_ ? value_unchecked() : T(std::forward<U>(fallback));
    }

    // --- Monadic operations (C++23 API parity) ---

    // and_then: apply f to the success value; pass through on error.
    // f must return Expected<U, E> for some U.
    template <typename F>
        requires std::invocable<F, T&>
    auto and_then(F&& f) -> std::invoke_result_t<F, T&> {
        using ResultType = std::invoke_result_t<F, T&>;
        if (has_value_) return f(value_unchecked());
        return ResultType(std::move(error_unchecked()));
    }

    // map: transform the success value via f; pass through on error.
    // f must return a plain value U (not Expected).
    template <typename F>
        requires std::invocable<F, T&>
    auto map(F&& f) -> Expected<std::invoke_result_t<F, T&>, E> {
        using U = std::invoke_result_t<F, T&>;
        if (has_value_) return Expected<U, E>(f(value_unchecked()));
        return Expected<U, E>(std::move(error_unchecked()));
    }

    // or_else: apply f to the error; pass through on success.
    // f must return Expected<T, F> for some F.
    template <typename F>
        requires std::invocable<F, E&>
    auto or_else(F&& f) -> std::invoke_result_t<F, E&> {
        using ResultType = std::invoke_result_t<F, E&>;
        if (!has_value_) return f(error_unchecked());
        return ResultType(std::move(value_unchecked()));
    }

private:
    T& value_unchecked() { return *reinterpret_cast<T*>(&storage_); }
    const T& value_unchecked() const { return *reinterpret_cast<const T*>(&storage_); }
    E& error_unchecked() { return *reinterpret_cast<E*>(&storage_); }
    const E& error_unchecked() const { return *reinterpret_cast<const E*>(&storage_); }

    void destroy() {
        if (has_value_) value_unchecked().~T();
        else            error_unchecked().~E();
    }

    // Storage for either T or E. Aligned for both; never constructed as a union
    // member (we manually placement-new in constructors).
    alignas(std::max(alignof(T), alignof(E)))
        std::byte storage_[std::max(sizeof(T), sizeof(E))];
    bool has_value_;
};

// Expected<void, E> specialization: represents an operation that can
// succeed (no value) or fail (with an Error). The canonical "void init()"
// replacement.
template <typename E>
class Expected<void, E> {
public:
    // Ok: default-constructed Expected represents success.
    Expected() : has_value_(true), error_() {}
    Expected(std::nullopt_t) : has_value_(true), error_() {}

    // Err: implicit from E.
    template <typename U>
        requires std::convertible_to<U&&, E>
    Expected(U&& err) : has_value_(false), error_(std::forward<U>(err)) {}

    // Copy / move: trivial since E is copyable.
    Expected(const Expected&) = default;
    Expected(Expected&&) noexcept = default;
    Expected& operator=(const Expected&) = default;
    Expected& operator=(Expected&&) noexcept = default;

    [[nodiscard]] bool has_value() const { return has_value_; }
    [[nodiscard]] explicit operator bool() const { return has_value_; }

    // has_value() is the only valid inspector — there is no value() for void.
    E& error() {
        SNT_ASSERT_MSG(!has_value_, "Expected<void>::error() called on Ok variant");
        return error_;
    }
    const E& error() const {
        SNT_ASSERT_MSG(!has_value_, "Expected<void>::error() called on Ok variant");
        return error_;
    }

private:
    bool has_value_;
    E    error_;  // default-constructed when has_value_ == true
};

// Expected<void> alias: the most common "operation that can fail".
// `Expected<void> init();` is the idiomatic signature for an operation
// that returns nothing on success.
template <typename E = Error>
using Result = Expected<void, E>;

// Factory helpers for explicit Ok / Err construction.
// Usage:
//   return Ok();           // Expected<void>
//   return Ok(value);      // Expected<T>
//   return Err(error);     // Expected<*, E>
template <typename T>
auto Ok(T&& value) {
    return Expected<std::decay_t<T>>(std::forward<T>(value));
}
inline Expected<void> Ok() { return Expected<void>(); }

template <typename E>
auto Err(E&& error) {
    return Expected<void, std::decay_t<E>>(std::forward<E>(error));
}

}  // namespace snt::core
