#pragma once

namespace Envoy {
/**
 * Mixin class that makes derived classes not copyable. Like boost::noncopyable without boost.
 */
class NonCopyable {
protected:
  NonCopyable() {}

private:
  NonCopyable(const NonCopyable&);
  NonCopyable& operator=(const NonCopyable&);
};
} // namespace Envoy
