#pragma once

#include <chrono>

#include "envoy/extensions/tracers/fluentd/v3/fluentd.pb.h"
#include "envoy/extensions/tracers/fluentd/v3/fluentd.pb.validate.h"
#include "envoy/thread_local/thread_local.h"
#include "envoy/tracing/trace_driver.h"

#include "source/common/common/logger.h"
#include "source/common/common/statusor.h"
#include "source/common/http/header_map_impl.h"
#include "source/common/tracing/trace_context_impl.h"
#include "source/extensions/common/fluentd/fluentd_base.h"
#include "source/extensions/tracers/common/factory_base.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace Fluentd {

using namespace Envoy::Extensions::Common::Fluentd;
using FluentdConfig = envoy::extensions::tracers::fluentd::v3::FluentdConfig;
using FluentdConfigSharedPtr = std::shared_ptr<FluentdConfig>;

// Span context definitions
class SpanContext {
public:
  SpanContext() = default;
  SpanContext(absl::string_view version, absl::string_view trace_id, absl::string_view span_id,
              bool sampled, std::string tracestate)
      : version_(version), trace_id_(trace_id), span_id_(span_id), sampled_(sampled),
        tracestate_(std::move(tracestate)) {}

  const std::string& version() const { return version_; }

  const std::string& traceId() const { return trace_id_; }

  const std::string& spanId() const { return span_id_; }

  bool sampled() const { return sampled_; }
  void setSampled(bool sampled) { sampled_ = sampled; }

  const std::string& tracestate() const { return tracestate_; }

private:
  std::string version_;
  std::string trace_id_;
  std::string span_id_;
  bool sampled_{false};
  std::string tracestate_;
};

// Trace context definitions
class FluentdConstantValues {
public:
  const Tracing::TraceContextHandler TRACE_PARENT{"traceparent"};
  const Tracing::TraceContextHandler TRACE_STATE{"tracestate"};
};

using FluentdConstants = ConstSingleton<FluentdConstantValues>;

// SpanContextExtractor extracts the span context from the trace context
class SpanContextExtractor {
public:
  SpanContextExtractor(Tracing::TraceContext& trace_context);
  ~SpanContextExtractor();
  absl::StatusOr<SpanContext> extractSpanContext();
  bool propagationHeaderPresent();

private:
  const Tracing::TraceContext& trace_context_;
};

// FluentdTracerImpl implements a FluentdTracer, handling tracing and buffer/connection logic
class FluentdTracerImpl : public FluentdBase,
                          public std::enable_shared_from_this<FluentdTracerImpl> {
public:
  FluentdTracerImpl(Upstream::ThreadLocalCluster& cluster, Tcp::AsyncTcpClientPtr client,
                    Event::Dispatcher& dispatcher, const FluentdConfig& config,
                    BackOffStrategyPtr backoff_strategy, Stats::Scope& parent_scope,
                    Random::RandomGenerator& random);

  Tracing::SpanPtr startSpan(SystemTime start_time, const std::string& operation_name,
                             Tracing::Decision tracing_decision);

  Tracing::SpanPtr startSpan(SystemTime start_time, const std::string& operation_name,
                             const SpanContext& parent_context);

  void packMessage(MessagePackPacker& packer) override;

private:
  std::map<std::string, std::string> option_;
  Random::RandomGenerator& random_;
  TimeSource& time_source_;
};

using FluentdTracerWeakPtr = std::weak_ptr<FluentdTracerImpl>;
using FluentdTracerSharedPtr = std::shared_ptr<FluentdTracerImpl>;

// FluentdTracerCache is used to cache entries before they are sent to the Fluentd server
class FluentdTracerCacheImpl
    : public FluentdCacheBase<FluentdTracerImpl, FluentdConfig, FluentdTracerSharedPtr,
                              FluentdTracerWeakPtr>,
      public Singleton::Instance {
public:
  FluentdTracerCacheImpl(Upstream::ClusterManager& cluster_manager, Stats::Scope& parent_scope,
                         ThreadLocal::SlotAllocator& tls)
      : FluentdCacheBase(cluster_manager, parent_scope, tls, "tracing.fluentd") {}

protected:
  FluentdTracerSharedPtr createInstance(Upstream::ThreadLocalCluster& cluster,
                                        Tcp::AsyncTcpClientPtr client,
                                        Event::Dispatcher& dispatcher, const FluentdConfig& config,
                                        BackOffStrategyPtr backoff_strategy,
                                        Random::RandomGenerator& random) override {
    return std::make_shared<FluentdTracerImpl>(cluster, std::move(client), dispatcher, config,
                                               std::move(backoff_strategy), *stats_scope_, random);
  }
};

using FluentdTracerCacheSharedPtr = std::shared_ptr<FluentdTracerCacheImpl>;

using TracerPtr = std::unique_ptr<FluentdTracerImpl>;

// Driver manages and creates Fluentd tracers
class Driver : Logger::Loggable<Logger::Id::tracing>, public Tracing::Driver {
public:
  Driver(const FluentdConfigSharedPtr fluentd_config,
         Server::Configuration::TracerFactoryContext& context,
         FluentdTracerCacheSharedPtr tracer_cache);

  // Tracing::Driver
  Tracing::SpanPtr startSpan(const Tracing::Config& config, Tracing::TraceContext& trace_context,
                             const StreamInfo::StreamInfo& stream_info,
                             const std::string& operation_name,
                             Tracing::Decision tracing_decision) override;

private:
  class ThreadLocalTracer : public ThreadLocal::ThreadLocalObject {
  public:
    ThreadLocalTracer(FluentdTracerSharedPtr tracer) : tracer_(std::move(tracer)) {}

    FluentdTracerImpl& tracer() { return *tracer_; }

    FluentdTracerSharedPtr tracer_;
  };

private:
  ThreadLocal::SlotPtr tls_slot_;
  const FluentdConfigSharedPtr fluentd_config_;
  FluentdTracerCacheSharedPtr tracer_cache_;
};

// Span holds the span context and handles span operations
class Span : public Tracing::Span {
public:
  Span(SystemTime start_time, const std::string& operation_name, FluentdTracerSharedPtr tracer,
       SpanContext&& span_context, TimeSource& time_source, bool use_local_decision);

  // Tracing::Span
  void setOperation(absl::string_view operation) override;
  void setTag(absl::string_view name, absl::string_view value) override;
  void log(SystemTime timestamp, const std::string& event) override;
  void finishSpan() override;
  void injectContext(Tracing::TraceContext& trace_context,
                     const Tracing::UpstreamContext& upstream) override;
  Tracing::SpanPtr spawnChild(const Tracing::Config& config, const std::string& name,
                              SystemTime start_time) override;
  void setSampled(bool sampled) override { span_context_.setSampled(sampled); }
  bool sampled() const { return span_context_.sampled(); }
  bool useLocalDecision() const override { return use_local_decision_; }

  std::string getBaggage(absl::string_view key) override;
  void setBaggage(absl::string_view key, absl::string_view value) override;
  std::string getTraceId() const override;
  std::string getSpanId() const override;

private:
  // config
  SystemTime start_time_;
  std::string operation_;

  FluentdTracerSharedPtr tracer_;
  SpanContext span_context_;
  std::map<std::string, std::string> tags_;
  Envoy::TimeSource& time_source_;
  const bool use_local_decision_{false};
};

} // namespace Fluentd
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
