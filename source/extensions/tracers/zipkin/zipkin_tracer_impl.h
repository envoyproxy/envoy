#pragma once

#include "envoy/config/trace/v3/trace.pb.h"
#include "envoy/local_info/local_info.h"
#include "envoy/runtime/runtime.h"
#include "envoy/thread_local/thread_local.h"
#include "envoy/tracing/http_tracer.h"
#include "envoy/upstream/cluster_manager.h"

#include "common/http/async_client_utility.h"
#include "common/http/header_map_impl.h"
#include "common/json/json_loader.h"

#include "extensions/tracers/zipkin/span_buffer.h"
#include "extensions/tracers/zipkin/tracer.h"
#include "extensions/tracers/zipkin/zipkin_core_constants.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace Zipkin {

#define ZIPKIN_TRACER_STATS(COUNTER)                                                               \
  COUNTER(spans_sent)                                                                              \
  COUNTER(timer_flushed)                                                                           \
  COUNTER(reports_sent)                                                                            \
  COUNTER(reports_dropped)                                                                         \
  COUNTER(reports_failed)

struct ZipkinTracerStats {
  ZIPKIN_TRACER_STATS(GENERATE_COUNTER_STRUCT)
};

/**
 * Class for Zipkin spans, wrapping a Zipkin::Span object.
 */
class ZipkinSpan : public Tracing::Span {
public:
  /**
   * Constructor. Wraps a Zipkin::Span object.
   *
   * @param span to be wrapped.
   */
  ZipkinSpan(Zipkin::Span& span, Zipkin::Tracer& tracer);

  /**
   * Calls Zipkin::Span::finishSpan() to perform all actions needed to finalize the span.
   * This function is called by Tracing::HttpTracerUtility::finalizeSpan().
   */
  void finishSpan() override;

  /**
   * This method sets the operation name on the span.
   * @param operation the operation name
   */
  void setOperation(absl::string_view operation) override;

  /**
   * This function adds a Zipkin "string" binary annotation to this span.
   * In Zipkin, binary annotations of the type "string" allow arbitrary key-value pairs
   * to be associated with a span.
   *
   * Note that Tracing::HttpTracerUtility::finalizeSpan() makes several calls to this function,
   * associating several key-value pairs with this span.
   */
  void setTag(absl::string_view name, absl::string_view value) override;

  void log(SystemTime timestamp, const std::string& event) override;

  void injectContext(Http::RequestHeaderMap& request_headers) override;
  Tracing::SpanPtr spawnChild(const Tracing::Config&, const std::string& name,
                              SystemTime start_time) override;

  void setSampled(bool sampled) override;

  /**
   * @return a reference to the Zipkin::Span object.
   */
  Zipkin::Span& span() { return span_; }

private:
  Zipkin::Span span_;
  Zipkin::Tracer& tracer_;
};

using ZipkinSpanPtr = std::unique_ptr<ZipkinSpan>;

/**
 * Class for a Zipkin-specific Driver.
 */
class Driver : public Tracing::Driver {
public:
  /**
   * Constructor. It adds itself and a newly-created Zipkin::Tracer object to a thread-local store.
   * Also, it associates the given random-number generator to the Zipkin::Tracer object it creates.
   */
  Driver(const envoy::config::trace::v3::ZipkinConfig& zipkin_config,
         Upstream::ClusterManager& cluster_manager, Stats::Scope& scope,
         ThreadLocal::SlotAllocator& tls, Runtime::Loader& runtime,
         const LocalInfo::LocalInfo& localinfo, Runtime::RandomGenerator& random_generator,
         TimeSource& time_source);

  /**
   * This function is inherited from the abstract Driver class.
   *
   * It starts a new Zipkin span. Depending on the request headers, it can create a root span,
   * a child span, or a shared-context span.
   *
   * The third parameter (operation_name) does not actually make sense for Zipkin.
   * Thus, this implementation of the virtual function startSpan() ignores the operation name
   * ("ingress" or "egress") passed by the caller.
   */
  Tracing::SpanPtr startSpan(const Tracing::Config&, Http::RequestHeaderMap& request_headers,
                             const std::string&, SystemTime start_time,
                             const Tracing::Decision tracing_decision) override;

  // Getters to return the ZipkinDriver's key members.
  Upstream::ClusterManager& clusterManager() { return cm_; }
  Upstream::ClusterInfoConstSharedPtr cluster() { return cluster_; }
  Runtime::Loader& runtime() { return runtime_; }
  ZipkinTracerStats& tracerStats() { return tracer_stats_; }

private:
  /**
   * Thread-local store containing ZipkinDriver and Zipkin::Tracer objects.
   */
  struct TlsTracer : ThreadLocal::ThreadLocalObject {
    TlsTracer(TracerPtr&& tracer, Driver& driver);

    TracerPtr tracer_;
    Driver& driver_;
  };

  Upstream::ClusterManager& cm_;
  Upstream::ClusterInfoConstSharedPtr cluster_;
  ZipkinTracerStats tracer_stats_;
  ThreadLocal::SlotPtr tls_;
  Runtime::Loader& runtime_;
  const LocalInfo::LocalInfo& local_info_;
  TimeSource& time_source_;
};

/**
 * Information about the Zipkin collector.
 */
struct CollectorInfo {
  // The Zipkin collector endpoint/path to receive the collected trace data. e.g. /api/v1/spans if
  // HTTP_JSON_V1 or /api/v2/spans otherwise.
  std::string endpoint_{DEFAULT_COLLECTOR_ENDPOINT};

  // The version of the collector. This is related to endpoint's supported payload specification and
  // transport. Currently it defaults to envoy::config::trace::v2::ZipkinConfig::HTTP_JSON_V1. In
  // the future, we will throw when collector_endpoint_version is not specified.
  envoy::config::trace::v3::ZipkinConfig::CollectorEndpointVersion version_{
      envoy::config::trace::v3::ZipkinConfig::hidden_envoy_deprecated_HTTP_JSON_V1};

  bool shared_span_context_{DEFAULT_SHARED_SPAN_CONTEXT};
};

/**
 * This class derives from the abstract Zipkin::Reporter.
 * It buffers spans and relies on Http::AsyncClient to send spans to
 * Zipkin using JSON over HTTP.
 *
 * Two runtime parameters control the span buffering/flushing behavior, namely:
 * tracing.zipkin.min_flush_spans and tracing.zipkin.flush_interval_ms.
 *
 * Up to `tracing.zipkin.min_flush_spans` will be buffered. Spans are flushed (sent to Zipkin)
 * either when the buffer is full, or when a timer, set to `tracing.zipkin.flush_interval_ms`,
 * expires, whichever happens first.
 *
 * The default values for the runtime parameters are 5 spans and 5000ms.
 */
class ReporterImpl : public Reporter, Http::AsyncClient::Callbacks {
public:
  /**
   * Constructor.
   *
   * @param driver ZipkinDriver to be associated with the reporter.
   * @param dispatcher Controls the timer used to flush buffered spans.
   * @param collector holds the endpoint version and path information.
   * when making HTTP POST requests carrying spans. This value comes from the
   * Zipkin-related tracing configuration.
   */
  ReporterImpl(Driver& driver, Event::Dispatcher& dispatcher, const CollectorInfo& collector);

  /**
   * Implementation of Zipkin::Reporter::reportSpan().
   *
   * Buffers the given span and calls flushSpans() if the buffer is full.
   *
   * @param span The span to be buffered.
   */
  void reportSpan(Span&& span) override;

  // Http::AsyncClient::Callbacks.
  // The callbacks below record Zipkin-span-related stats.
  void onSuccess(const Http::AsyncClient::Request&, Http::ResponseMessagePtr&&) override;
  void onFailure(const Http::AsyncClient::Request&, Http::AsyncClient::FailureReason) override;

  /**
   * Creates a heap-allocated ZipkinReporter.
   *
   * @param driver ZipkinDriver to be associated with the reporter.
   * @param dispatcher Controls the timer used to flush buffered spans.
   * @param collector holds the endpoint version and path information.
   * when making HTTP POST requests carrying spans. This value comes from the
   * Zipkin-related tracing configuration.
   *
   * @return Pointer to the newly-created ZipkinReporter.
   */
  static ReporterPtr NewInstance(Driver& driver, Event::Dispatcher& dispatcher,
                                 const CollectorInfo& collector);

private:
  /**
   * Enables the span-flushing timer.
   */
  void enableTimer();

  /**
   * Removes all spans from the span buffer and sends them to Zipkin using Http::AsyncClient.
   */
  void flushSpans();

  Driver& driver_;
  Event::TimerPtr flush_timer_;
  const CollectorInfo collector_;
  SpanBufferPtr span_buffer_;
  // Track active HTTP requests to be able to cancel them on destruction.
  Http::AsyncClientRequestTracker active_requests_;
};
} // namespace Zipkin
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
