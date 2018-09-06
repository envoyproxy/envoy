#pragma once

#include <cstddef>

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace JwtAuthn {

/**
 * Struct to keep track of verifier completed and responded state for a request.
 */
struct ResponseData {
  // if verifier node has responded to a request or not.
  bool has_responded_;
  // number of completed inner verifier for an any/all verifier.
  std::size_t count_;
};

} // namespace JwtAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
