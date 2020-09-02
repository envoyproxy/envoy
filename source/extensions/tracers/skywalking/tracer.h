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

class Tracer {
public:
  explicit Tracer(Upstream::ClusterManager& cm, Stats::Scope& scope,
                  const LocalInfo::LocalInfo& local_info, TimeSource& time_source,
                  Random::RandomGenerator& random_generator, Event::Dispatcher& dispatcher,
                  const envoy::config::core::v3::GrpcService& grpc_service)
      : node_(local_info.nodeName()), service_(local_info.clusterName()),
        address_(absl::StrCat(local_info.address()->asString(), ":",
                              local_info.address()->ip()->port())),
        time_source_(time_source), random_generator_(random_generator),
        reporter_(std::make_unique<TraceSegmentReporter>(
            cm.grpcAsyncClientManager().factoryForGrpcService(grpc_service, scope, false),
            dispatcher)) {}

  ~Tracer();

  void report(const SpanObject& span_object);

  Tracing::SpanPtr startSpan(const Tracing::Config& config, SystemTime start_time,
                             const SpanContext& span_context,
                             const SpanContext& previous_span_context);

  const std::string& node() const { return node_; }
  const std::string& service() const { return service_; }

private:
  const std::string node_;
  const std::string service_;
  const std::string address_;

  TimeSource& time_source_;
  Random::RandomGenerator& random_generator_;
  TraceSegmentReporterPtr reporter_;
};

using TracerPtr = std::unique_ptr<Tracer>;

class Span : public Tracing::Span {
public:
  Span(SpanObject span_object, Tracer& tracer);

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

private:
  SpanObject span_object_;
  Tracer& tracer_;
};

} // namespace SkyWalking
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
