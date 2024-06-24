#pragma once

#include <string>

#include "envoy/data/accesslog/v3/accesslog.pb.h"
#include "envoy/stream_info/stream_info.h"
#include "envoy/tracing/trace_context.h"

#include "absl/container/flat_hash_map.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Tracing {

/**
 * The context for the custom tag to obtain the tag value.
 */
struct CustomTagContext {
  const TraceContext& trace_context;
  const StreamInfo::StreamInfo& stream_info;
};

class Span;

/**
 * Tracing custom tag, with tag name and how it would be applied to the span.
 */
class CustomTag {
public:
  virtual ~CustomTag() = default;

  /**
   * @return the tag name view.
   */
  virtual absl::string_view tag() const PURE;

  /**
   * The way how to apply the custom tag to the span,
   * generally obtain the tag value from the context and attached it to the span.
   * @param span the active span.
   * @param ctx the custom tag context.
   */
  virtual void applySpan(Span& span, const CustomTagContext& ctx) const PURE;

  /**
   * Get string tag value from various type of custom tags. (e.g. Literal, Environment, Header,
   * Metadata)
   * @param log entry.
   * @param ctx the custom tag context.
   */
  virtual void applyLog(envoy::data::accesslog::v3::AccessLogCommon& entry,
                        const CustomTagContext& ctx) const PURE;
};

using CustomTagConstSharedPtr = std::shared_ptr<const CustomTag>;
using CustomTagMap = absl::flat_hash_map<std::string, CustomTagConstSharedPtr>;

} // namespace Tracing
} // namespace Envoy
