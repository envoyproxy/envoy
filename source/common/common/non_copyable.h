#pragma once

namespace Envoy {
/**
 * Mixin class that makes derived classes not copyable and not moveable. Like boost::noncopyable
 * without boost.
 */
class NonCopyable {
protected:
  NonCopyable() = default;

private:
  // Non-moveable.
  NonCopyable(NonCopyable&&);
  NonCopyable& operator=(NonCopyable&&);

  // Non-copyable.
  NonCopyable(const NonCopyable&);
  NonCopyable& operator=(const NonCopyable&);
};
} // namespace Envoy
