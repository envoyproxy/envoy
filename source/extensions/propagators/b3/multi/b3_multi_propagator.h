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
namespace Multi {

/**
 * B3 multi-header format constants.
 * See https://github.com/openzipkin/b3-propagation
 */
namespace Constants {
// B3 multi-header format header names
constexpr absl::string_view kTraceIdHeader = "x-b3-traceid";
constexpr absl::string_view kSpanIdHeader = "x-b3-spanid";
constexpr absl::string_view kParentSpanIdHeader = "x-b3-parentspanid";
constexpr absl::string_view kSampledHeader = "x-b3-sampled";
constexpr absl::string_view kFlagsHeader = "x-b3-flags";
} // namespace Constants

/**
 * B3 Multi-header Trace Propagation constants for header names as defined in:
 * https://github.com/openzipkin/b3-propagation
 */
class MultiConstantValues {
public:
  // B3 multi-header format headers
  const Tracing::TraceContextHandler X_B3_TRACE_ID{std::string(Constants::kTraceIdHeader)};
  const Tracing::TraceContextHandler X_B3_SPAN_ID{std::string(Constants::kSpanIdHeader)};
  const Tracing::TraceContextHandler X_B3_PARENT_SPAN_ID{
      std::string(Constants::kParentSpanIdHeader)};
  const Tracing::TraceContextHandler X_B3_SAMPLED{std::string(Constants::kSampledHeader)};
  const Tracing::TraceContextHandler X_B3_FLAGS{std::string(Constants::kFlagsHeader)};
};

using MultiConstants = ConstSingleton<MultiConstantValues>;

} // namespace Multi

// Convenient alias for easier usage, similar to other propagators like SkyWalking and XRay
using B3Constants = Multi::MultiConstants;

} // namespace B3
} // namespace Propagators
} // namespace Extensions
} // namespace Envoy
