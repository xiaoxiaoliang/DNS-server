/**
 * @file optional.h
 * @brief C++11 compatible optional<T> — 替代 std::optional (C++17)
 */
#pragma once
#include <type_traits>
#include <new>
#include <utility>

struct nullopt_t {};
const nullopt_t nullopt;

template<typename T>
class optional {
    typename std::aligned_storage<sizeof(T), alignof(T)>::type storage_;
    bool has_ = false;

    T* ptr()             { return reinterpret_cast<T*>(&storage_); }
    const T* ptr() const { return reinterpret_cast<const T*>(&storage_); }
public:
    optional() = default;
    optional(nullopt_t) {}
    optional(const T& v) : has_(true) { new(ptr()) T(v); }
    optional(T&& v) : has_(true) { new(ptr()) T(std::move(v)); }

    optional(const optional& o) : has_(o.has_) { if (has_) new(ptr()) T(*o); }
    optional(optional&& o) noexcept : has_(o.has_) {
        if (has_) { new(ptr()) T(std::move(*o)); o.has_ = false; }
    }
    ~optional() { if (has_) ptr()->~T(); }

    optional& operator=(const optional& o) {
        if (this != &o) { if (has_) ptr()->~T(); has_ = o.has_; if (has_) new(ptr()) T(*o); }
        return *this;
    }
    optional& operator=(optional&& o) noexcept {
        if (this != &o) { if (has_) ptr()->~T(); has_ = o.has_; if (has_) { new(ptr()) T(std::move(*o)); o.has_ = false; } }
        return *this;
    }

    bool has_value() const { return has_; }
    explicit operator bool() const { return has_; }
    T& operator*() { return *ptr(); }
    const T& operator*() const { return *ptr(); }
    T* operator->() { return ptr(); }
    const T* operator->() const { return ptr(); }
};
