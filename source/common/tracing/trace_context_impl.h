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
  /**
   * Construct a handler with the given key. Note that the key will be lowercase and copied.
   * @param key the key of the handler.
   */
  TraceContextHandler(absl::string_view key);

  /**
   * Get the key of the handler.
   * @return absl::string_view the key of the handler. Note that the key is lowercase.
   */
  const Http::LowerCaseString& key() const { return key_; }

  /**
   * Erase the key/value pair from the trace context.
   * @param trace_context the trace context to erase the key/value pair.
   */
  void remove(TraceContext& trace_context) const;

  /**
   * Get the value from the trace context by the key.
   * @param trace_context the trace context to get the value.
   * @return absl::optional<absl::string_view> the value of the key. If the key is not found, then
   * absl::nullopt will be returned.
   */
  absl::optional<absl::string_view> get(const TraceContext& trace_context) const;

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

private:
  const Http::LowerCaseString key_;
  absl::optional<InlineHandle> handle_;
};

} // namespace Tracing
} // namespace Envoy
