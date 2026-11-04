#pragma once

#include <type_traits>
#include <utility>

namespace lv::core
{

// Runs a callable when the scope ends, unless dismiss() was called.
// Usage: auto guard = ScopeGuard([&] { close(fd); });
template <typename F>
class [[nodiscard]] ScopeGuard
{
    static_assert(std::is_nothrow_invocable_v<F&>, "ScopeGuard callables must be noexcept");

public:
    explicit ScopeGuard(F function) noexcept(std::is_nothrow_move_constructible_v<F>)
        : m_function(std::move(function))
    {}

    ScopeGuard(const ScopeGuard&) = delete;
    ScopeGuard& operator=(const ScopeGuard&) = delete;
    ScopeGuard(ScopeGuard&&) = delete;
    ScopeGuard& operator=(ScopeGuard&&) = delete;

    ~ScopeGuard()
    {
        if (m_active) {
            m_function();
        }
    }

    void dismiss() noexcept { m_active = false; }

private:
    F m_function;
    bool m_active = true;
};

}  // namespace lv::core
