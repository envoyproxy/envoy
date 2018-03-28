#pragma once

#include "envoy/local_info/local_info.h"
#include "envoy/runtime/runtime.h"
#include "envoy/thread_local/thread_local.h"
#include "envoy/tracing/http_tracer.h"
#include "envoy/upstream/cluster_manager.h"

#include "common/http/header_map_impl.h"
#include "common/json/json_loader.h"
#include "common/tracing/zipkin/span_buffer.h"
#include "common/tracing/zipkin/tracer.h"

namespace Envoy {
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
  void setOperation(const std::string& operation) override;

  /**
   * This function adds a Zipkin "string" binary annotation to this span.
   * In Zipkin, binary annotations of the type "string" allow arbitrary key-value pairs
   * to be associated with a span.
   *
   * Note that Tracing::HttpTracerUtility::finalizeSpan() makes several calls to this function,
   * associating several key-value pairs with this span.
   */
  void setTag(const std::string& name, const std::string& value) override;

  void injectContext(Http::HeaderMap& request_headers) override;
  Tracing::SpanPtr spawnChild(const Tracing::Config&, const std::string& name,
                              SystemTime start_time) override;

  /**
   * @return a reference to the Zipkin::Span object.
   */
  Zipkin::Span& span() { return span_; }

private:
  Zipkin::Span span_;
  Zipkin::Tracer& tracer_;
};

typedef std::unique_ptr<ZipkinSpan> ZipkinSpanPtr;

/**
 * Class for a Zipkin-specific Driver.
 */
class Driver : public Tracing::Driver {
public:
  /**
   * Constructor. It adds itself and a newly-created Zipkin::Tracer object to a thread-local store.
   * Also, it associates the given random-number generator to the Zipkin::Tracer object it creates.
   */
  Driver(const Json::Object& config, Upstream::ClusterManager& cluster_manager, Stats::Store& stats,
         ThreadLocal::SlotAllocator& tls, Runtime::Loader& runtime,
         const LocalInfo::LocalInfo& localinfo, Runtime::RandomGenerator& random_generator);

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
  Tracing::SpanPtr startSpan(const Tracing::Config&, Http::HeaderMap& request_headers,
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
   * @param collector_endpoint String representing the Zipkin endpoint to be used
   * when making HTTP POST requests carrying spans. This value comes from the
   * Zipkin-related tracing configuration.
   */
  ReporterImpl(Driver& driver, Event::Dispatcher& dispatcher,
               const std::string& collector_endpoint);

  /**
   * Implementation of Zipkin::Reporter::reportSpan().
   *
   * Buffers the given span and calls flushSpans() if the buffer is full.
   *
   * @param span The span to be buffered.
   */
  void reportSpan(const Span& span) override;

  // Http::AsyncClient::Callbacks.
  // The callbacks below record Zipkin-span-related stats.
  void onSuccess(Http::MessagePtr&&) override;
  void onFailure(Http::AsyncClient::FailureReason) override;

  /**
   * Creates a heap-allocated ZipkinReporter.
   *
   * @param driver ZipkinDriver to be associated with the reporter.
   * @param dispatcher Controls the timer used to flush buffered spans.
   * @param collector_endpoint String representing the Zipkin endpoint to be used
   * when making HTTP POST requests carrying spans. This value comes from the
   * Zipkin-related tracing configuration.
   *
   * @return Pointer to the newly-created ZipkinReporter.
   */
  static ReporterPtr NewInstance(Driver& driver, Event::Dispatcher& dispatcher,
                                 const std::string& collector_endpoint);

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
  SpanBuffer span_buffer_;
  const std::string collector_endpoint_;
};
} // Zipkin
} // namespace Envoy
