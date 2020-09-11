#pragma once

#include <iostream>
#include <memory>

#include "envoy/common/pure.h"

#include "common/tracing/http_tracer_impl.h"

#include "extensions/tracers/skywalking/skywalking_types.h"
#include "extensions/tracers/skywalking/trace_segment_reporter.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace SkyWalking {

class Span;

class Tracer {
public:
  explicit Tracer(Upstream::ClusterManager& cm, Stats::Scope& scope, TimeSource& time_source,
                  Event::Dispatcher& dispatcher,
                  const envoy::config::core::v3::GrpcService& grpc_service,
                  const envoy::config::trace::v3::ClientConfig& client_config)
      : time_source_(time_source),
        reporter_(std::make_unique<TraceSegmentReporter>(
            cm.grpcAsyncClientManager().factoryForGrpcService(grpc_service, scope, false),
            dispatcher, client_config)) {}
  void report(const SegmentContext& segment_context) { return reporter_->report(segment_context); }
  ~Tracer() { reporter_->closeStream(); }

  Tracing::SpanPtr startSpan(const Tracing::Config& config, SystemTime start_time,
                             const std::string& operation_name,
                             SegmentContextSharedPtr span_context, Span* parent_span);

private:
  TimeSource& time_source_;
  TraceSegmentReporterPtr reporter_;
};

using TracerPtr = std::unique_ptr<Tracer>;

class Span : public Tracing::Span {
public:
  Span(SegmentContextSharedPtr segment_context, SpanStore* span_store, Tracer& tracer)
      : segment_context_(std::move(segment_context)), span_store_(std::move(span_store)),
        tracer_(tracer) {}

  // Tracing::Span
  void setOperation(absl::string_view operation) override;
  void setTag(absl::string_view name, absl::string_view value) override;
  void log(SystemTime timestamp, const std::string& event) override;
  void finishSpan() override;
  void injectContext(Http::RequestHeaderMap& request_headers) override;
  Tracing::SpanPtr spawnChild(const Tracing::Config& config, const std::string& name,
                              SystemTime start_time) override;
  void setSampled(bool sampled) override;
  std::string getBaggage(absl::string_view key) override;
  void setBaggage(absl::string_view key, absl::string_view value) override;

  SpanStore* spanStore() const { return span_store_; }

private:
  SegmentContextSharedPtr segment_context_;
  SpanStore* span_store_;

  Tracer& tracer_;
};

} // namespace SkyWalking
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
