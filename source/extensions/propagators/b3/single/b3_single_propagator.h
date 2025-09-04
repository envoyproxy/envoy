#pragma once

#include <string>

#include "envoy/http/header_map.h"

#include "source/common/singleton/const_singleton.h"
#include "source/common/tracing/trace_context_impl.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace Propagators {
namespace B3 {
namespace Single {

/**
 * B3 single-header format constants.
 * See https://github.com/openzipkin/b3-propagation
 */
namespace Constants {
// B3 single header format
constexpr absl::string_view kB3Header = "b3";
} // namespace Constants

/**
 * B3 Single-header Trace Propagation constants for header names as defined in:
 * https://github.com/openzipkin/b3-propagation
 */
class SingleConstantValues {
public:
  // B3 single header format
  const Tracing::TraceContextHandler B3{std::string(Constants::kB3Header)};
};

using SingleConstants = ConstSingleton<SingleConstantValues>;

} // namespace Single

// Convenient alias for easier usage, similar to other propagators like SkyWalking and XRay
using B3SingleConstants = Single::SingleConstants;

} // namespace B3
} // namespace Propagators
} // namespace Extensions
} // namespace Envoy
