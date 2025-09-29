#pragma once

#include <string>

#include "envoy/http/header_map.h"

#include "source/common/singleton/const_singleton.h"
#include "source/common/tracing/trace_context_impl.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace Propagators {
namespace XRay {

/**
 * AWS X-Ray trace propagation specification constants.
 * See
 * https://docs.aws.amazon.com/xray/latest/devguide/xray-concepts.html#xray-concepts-tracingheader
 */
namespace Constants {
// AWS X-Ray trace header name
constexpr absl::string_view kTraceIdHeader = "x-amzn-trace-id";
} // namespace Constants

/**
 * AWS X-Ray trace propagation constants for header names as defined in:
 * https://docs.aws.amazon.com/xray/latest/devguide/xray-concepts.html#xray-concepts-tracingheader
 */
class XRayConstantValues {
public:
  // AWS X-Ray trace header
  const Tracing::TraceContextHandler X_AMZN_TRACE_ID{std::string(Constants::kTraceIdHeader)};
};

using XRayConstants = ConstSingleton<XRayConstantValues>;

} // namespace XRay
} // namespace Propagators
} // namespace Extensions
} // namespace Envoy
