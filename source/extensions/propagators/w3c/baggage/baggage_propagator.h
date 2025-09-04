#pragma once

#include <cstddef>
#include <string>

#include "envoy/http/header_map.h"

#include "source/common/singleton/const_singleton.h"
#include "source/common/tracing/trace_context_impl.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace Propagators {
namespace W3c {
namespace Baggage {

/**
 * W3C Baggage specification constants.
 * See https://www.w3.org/TR/baggage/
 */
namespace Constants {
// Header names as defined in W3C specification
constexpr absl::string_view kBaggageHeader = "baggage";

// W3C Baggage specification limits
constexpr size_t kMaxBaggageSize = 8192;   // 8KB total size limit
constexpr size_t kMaxBaggageMembers = 180; // Practical limit to prevent abuse
constexpr size_t kMaxKeyLength = 256;
constexpr size_t kMaxValueLength = 4096;
} // namespace Constants

/**
 * W3C Baggage constants for header names as defined in:
 * https://www.w3.org/TR/baggage/
 */
class BaggageConstantValues {
public:
  const Tracing::TraceContextHandler BAGGAGE{std::string(Constants::kBaggageHeader)};
};

using BaggageConstants = ConstSingleton<BaggageConstantValues>;

} // namespace Baggage

// Convenient alias for easier usage, similar to other propagators like SkyWalking and XRay
using W3cBaggageConstants = Baggage::BaggageConstants;

} // namespace W3c
} // namespace Propagators
} // namespace Extensions
} // namespace Envoy
