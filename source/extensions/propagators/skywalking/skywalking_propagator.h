#pragma once

#include <string>

#include "envoy/http/header_map.h"

#include "source/common/singleton/const_singleton.h"
#include "source/common/tracing/trace_context_impl.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace Propagators {
namespace SkyWalking {

/**
 * SkyWalking trace propagation specification constants.
 * See
 * https://skywalking.apache.org/docs/main/latest/en/protocols/skywalking-cross-process-propagation-headers-protocol-v3/
 */
namespace Constants {
// SkyWalking trace header name
constexpr absl::string_view kSw8Header = "sw8";
} // namespace Constants

/**
 * SkyWalking trace propagation constants for header names.
 */
class SkyWalkingConstantValues {
public:
  // SkyWalking trace header
  const Tracing::TraceContextHandler SW8{std::string(Constants::kSw8Header)};
};

using SkyWalkingConstants = ConstSingleton<SkyWalkingConstantValues>;

} // namespace SkyWalking
} // namespace Propagators
} // namespace Extensions
} // namespace Envoy
