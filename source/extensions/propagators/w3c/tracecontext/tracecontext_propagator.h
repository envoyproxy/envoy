#pragma once

#include <cstdint>
#include <string>

#include "envoy/http/header_map.h"

#include "source/common/singleton/const_singleton.h"
#include "source/common/tracing/trace_context_impl.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace Propagators {
namespace W3c {
namespace TraceContext {

/**
 * W3C Trace Context specification constants.
 * See https://www.w3.org/TR/trace-context/
 */
namespace Constants {
// W3C traceparent header format: version-trace-id-parent-id-trace-flags
constexpr int kTraceparentHeaderSize = 55; // 2 + 1 + 32 + 1 + 16 + 1 + 2
constexpr int kVersionSize = 2;
constexpr int kTraceIdSize = 32;
constexpr int kParentIdSize = 16;
constexpr int kTraceFlagsSize = 2;

// Header names as defined in W3C specification
constexpr absl::string_view kTraceparentHeader = "traceparent";
constexpr absl::string_view kTracestateHeader = "tracestate";

// Current version
constexpr absl::string_view kCurrentVersion = "00";

// Trace flags
constexpr uint8_t kSampledFlag = 0x01;
} // namespace Constants

/**
 * W3C Trace Context constants for header names as defined in:
 * https://www.w3.org/TR/trace-context/
 */
class TraceContextConstantValues {
public:
  const Tracing::TraceContextHandler TRACE_PARENT{std::string(Constants::kTraceparentHeader)};
  const Tracing::TraceContextHandler TRACE_STATE{std::string(Constants::kTracestateHeader)};
};

using TraceContextConstants = ConstSingleton<TraceContextConstantValues>;

} // namespace TraceContext

// Convenient alias for easier usage, similar to other propagators like SkyWalking and XRay
using W3cConstants = TraceContext::TraceContextConstants;

} // namespace W3c
} // namespace Propagators
} // namespace Extensions
} // namespace Envoy
