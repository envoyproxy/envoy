#pragma once

#include "envoy/tracing/trace_context.h"

#include "source/extensions/filters/network/generic_proxy/interface/stream.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace GenericProxy {

/*
 * Simple wrapper around a StreamRequest that provides the TraceContext interface.
 */
class TraceContextBridge : public Tracing::TraceContext {
public:
  TraceContextBridge(StreamRequest& request) : request_(request) {}

  // Tracing::TraceContext
  absl::string_view protocol() const override;
  absl::string_view host() const override;
  absl::string_view path() const override;
  absl::string_view method() const override;
  void forEach(IterateCallback callback) const override;
  absl::optional<absl::string_view> get(absl::string_view key) const override;
  void set(absl::string_view key, absl::string_view val) override;
  void remove(absl::string_view key) override;

private:
  RequestHeaderFrame& request_;
};

} // namespace GenericProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
