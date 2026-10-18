#pragma once

#include <atomic>
#include <concepts>
#include <cstdint>
#include <utility>

namespace visor::base {

/// A type that can carry an identity: an unsigned integer, with zero free to
/// mean "none". Bool is an unsigned integer by the type traits but not a
/// usable identity width.
template <typename T>
concept IdValue = std::unsigned_integral<T> && !std::same_as<T, bool>;

/// An identity value whose atomic is lock-free. The default generator needs
/// this; a custom generator that synchronizes another way can use plain
/// IdValue instead.
template <typename T>
concept AtomicIdValue = IdValue<T> && requires { requires std::atomic<T>::is_always_lock_free; };

/// The interface a generator must meet: one noexcept next() returning T.
/// The policy owns when and how identities advance; IdSession only forwards.
template <typename Generator, typename T>
concept IdGenerator = requires(Generator& generator) {
    { generator.next() } noexcept -> std::same_as<T>;
};

/**
 * @brief The default identity generator: a monotonic atomic counter that
 *        reserves zero.
 *
 * The counter starts at the constructor's value (one by default). next()
 * returns the previous value and advances by one; a wrap past zero skips the
 * empty value and returns one. Relaxed ordering is deliberate: an identity is
 * a label, not a synchronization edge. The lock-free requirement comes from
 * the AtomicIdValue constraint, so a target whose atomic needs a runtime lock
 * fails to compile instead of hiding a lock in the generator.
 */
template <AtomicIdValue T>
class AtomicIdGenerator {
  public:
    explicit constexpr AtomicIdGenerator(T first = T{1}) noexcept : counter_{first} {}

    /// The next identity. Zero is skipped on wrap.
    [[nodiscard]] T next() noexcept {
        T id = counter_.fetch_add(T{1}, std::memory_order_relaxed);
        if (id == T{0}) {
            id = counter_.fetch_add(T{1}, std::memory_order_relaxed);
        }
        return id;
    }

  private:
    std::atomic<T> counter_;
};

/**
 * @brief Hands out identities by delegating to a generator policy.
 *
 * One instance defines one identity space: objects that must not collide
 * share the instance, while separate instances may hand out the same values.
 * The default generator is AtomicIdGenerator<T>; a caller with a different
 * progression supplies a type meeting IdGenerator and passes its constructor
 * arguments through. next() itself has no opinion about the progression, so
 * a counter, a stride, or a replayable table are all one policy away.
 *
 * Constructor arguments are forwarded to the generator; with none, the
 * generator is default-constructed. Identities repeat after the generator's
 * own sequence wraps, so size the value to outlast the session.
 */
template <IdValue T, IdGenerator<T> Generator = AtomicIdGenerator<T>>
class IdSession {
  public:
    IdSession() = default;

    template <typename... Args>
        requires std::constructible_from<Generator, Args...>
    explicit IdSession(Args&&... args) noexcept : generator_(std::forward<Args>(args)...) {}

    /// The next identity from the configured generator.
    [[nodiscard]] T next() noexcept { return generator_.next(); }

  private:
    Generator generator_;
};

} // namespace visor::base
