#pragma once

#include <source_location>
#include <string_view>

// LV_ASSERT(condition): checks a programming invariant.
// Active when LV_ASSERTS_ENABLED is 1 (debug and sanitizer builds, i.e. when NDEBUG is not
// defined). In release builds the condition is not evaluated. Never put side effects into it.
// A failed assertion calls the installed handler; the default handler logs and aborts.
// NOLINTBEGIN(cppcoreguidelines-macro-usage): needed in #if, a constexpr cannot replace it
#if !defined(LV_ASSERTS_ENABLED)
#if defined(NDEBUG)
#define LV_ASSERTS_ENABLED 0
#else
#define LV_ASSERTS_ENABLED 1
#endif
#endif
// NOLINTEND(cppcoreguidelines-macro-usage)

namespace lv::core
{

// Called with the failed expression and its location. The handler must not return normally
// (abort, or throw in tests). Thread-safety: install before starting threads.
using AssertHandler = void (*)(std::string_view expression, const std::source_location& location);

// Installs a handler and returns the previous one. nullptr restores the default (log + abort).
AssertHandler setAssertHandler(AssertHandler handler) noexcept;

namespace detail
{

[[noreturn]] void assertionFailed(std::string_view expression,
                                  const std::source_location& location);

// Used by LV_ASSERT so the caller's condition is not negated textually (clang-tidy would
// otherwise suggest De Morgan rewrites at every call site).
[[nodiscard]] constexpr bool isFalse(bool value) noexcept
{
    return !value;
}

}  // namespace detail

}  // namespace lv::core

#if LV_ASSERTS_ENABLED
#define LV_ASSERT(condition)                                                                       \
    do {                                                                                           \
        if (::lv::core::detail::isFalse(static_cast<bool>(condition))) {                           \
            ::lv::core::detail::assertionFailed(#condition, std::source_location::current());      \
        }                                                                                          \
    } while (false)
#else
#define LV_ASSERT(condition)                                                                       \
    do {                                                                                           \
    } while (false)
#endif
