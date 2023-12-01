#pragma once

#include "envoy/http/header_map.h"
#include "envoy/tracing/trace_context.h"

namespace Envoy {
namespace Tracing {

using InlineHandle = Http::CustomInlineHeaderRegistry::Handle<
    Http::CustomInlineHeaderRegistry::Type::RequestHeaders>;

/**
 * A handler class for trace context. This class could be used to set, get and erase the key/value
 * pair in the trace context. This handler is optimized for HTTP to support reference semantics and
 * inline header. If HTTP header is used as the trace context and the key is registered in the
 * custom inline header registry, then inline handle will be used to improve the performance.
 */
class TraceContextHandler {
public:
  TraceContextHandler(absl::string_view key);

  /*
   * Set the key/value pair in the trace context.
   * @param trace_context the trace context to set the key/value pair.
   * @param value the value to set.
   */
  void set(TraceContext& trace_context, absl::string_view value) const;

  /**
   * Set the key/value pair in the trace context. This method should only be used when the handler
   * has longer lifetime than current stream.
   * @param trace_context the trace context to set the key/value pair.
   * @param value the value to set.
   */
  void setRefKey(TraceContext& trace_context, absl::string_view value) const;

  /**
   * Set the key/value pair in the trace context. This method should only be used when both the
   * handler and the value have longer lifetime than current stream.
   * @param trace_context the trace context to set the key/value pair.
   * @param value the value to set.
   */
  void setRef(TraceContext& trace_context, absl::string_view value) const;

  absl::optional<absl::string_view> get(const TraceContext& trace_context) const;
  void erase(TraceContext& trace_context) const;
  absl::string_view key() const { return key_.get(); }

private:
  const Http::LowerCaseString key_;
  absl::optional<InlineHandle> handle_;
};

} // namespace Tracing
} // namespace Envoy
