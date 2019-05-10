#pragma once

// NOLINT(namespace-envoy)

// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy code. It serves purely as a
// porting layer for QUICHE.

#include <utility>

namespace http2 {
namespace test {

class Http2Random;

// Reconstruct an object so that it is initialized as when it was first
// constructed. Runs the destructor to handle objects that might own resources,
// and runs the constructor with the provided arguments, if any.
template <class T, class... Args>
void Http2ReconstructObjectImpl(T* ptr, Http2Random* /*rng*/, Args&&... args) {
  ptr->~T();
  ::new (ptr) T(std::forward<Args>(args)...);
}

// This version applies default-initialization to the object.
template <class T> void Http2DefaultReconstructObjectImpl(T* ptr, Http2Random* /*rng*/) {
  ptr->~T();
  ::new (ptr) T;
}

} // namespace test
} // namespace http2
