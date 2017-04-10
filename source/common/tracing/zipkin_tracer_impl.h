#pragma once

#include "envoy/runtime/runtime.h"
#include "envoy/thread_local/thread_local.h"
#include "envoy/tracing/http_tracer.h"
#include "envoy/upstream/cluster_manager.h"

#include "common/http/header_map_impl.h"
#include "common/json/json_loader.h"

#include "zipkin/tracer.h"
#include "zipkin/span_buffer.h"

namespace Tracing {

#define ZIPKIN_TRACER_STATS(COUNTER)                                                               \
  COUNTER(spans_sent)                                                                              \
  COUNTER(timer_flushed)                                                                           \
  COUNTER(reports_sent)                                                                            \
  COUNTER(reports_dropped)

struct ZipkinTracerStats {
  ZIPKIN_TRACER_STATS(GENERATE_COUNTER_STRUCT)
};

class ZipkinSpan : public Span {
public:
  ZipkinSpan(Zipkin::Span& span);

  void finishSpan() override;
  void setTag(const std::string& name, const std::string& value) override;

  bool hasCSAnnotation();

private:
  Zipkin::Span span_;
};

typedef std::unique_ptr<ZipkinSpan> ZipkinSpanPtr;

class ZipkinDriver : public Driver {
public:
  ZipkinDriver(const Json::Object& config, Upstream::ClusterManager& cluster_manager,
               Stats::Store& stats, ThreadLocal::Instance& tls, Runtime::Loader& runtime,
               const LocalInfo::LocalInfo& localinfo);

  SpanPtr startSpan(Http::HeaderMap& request_headers, const std::string&,
                    SystemTime start_time) override;

  Upstream::ClusterManager& clusterManager() { return cm_; }
  Upstream::ClusterInfoConstSharedPtr cluster() { return cluster_; }
  Runtime::Loader& runtime() { return runtime_; }
  ZipkinTracerStats& tracerStats() { return tracer_stats_; }

private:
  struct TlsZipkinTracer : ThreadLocal::ThreadLocalObject {
    TlsZipkinTracer(Zipkin::Tracer tracer, ZipkinDriver& driver);

    void shutdown() override {}

    Zipkin::Tracer tracer_;
    ZipkinDriver& driver_;
  };

  Upstream::ClusterManager& cm_;
  Upstream::ClusterInfoConstSharedPtr cluster_;
  ZipkinTracerStats tracer_stats_;
  ThreadLocal::Instance& tls_;
  Runtime::Loader& runtime_;
  const LocalInfo::LocalInfo& local_info_;
  uint32_t tls_slot_;
};

class ZipkinReporter : public Zipkin::Reporter, Http::AsyncClient::Callbacks {
public:
  ZipkinReporter(ZipkinDriver& driver, Event::Dispatcher& dispatcher,
                 const std::string& collector_endpoint);

  void reportSpan(Zipkin::Span&& span) override;

  void onSuccess(Http::MessagePtr&&) override;
  void onFailure(Http::AsyncClient::FailureReason) override;

  static std::unique_ptr<Zipkin::Reporter> NewInstance(ZipkinDriver& driver,
                                                       Event::Dispatcher& dispatcher,
                                                       const std::string& collector_endpoint);

private:
  void enableTimer();
  void flushSpans();

  ZipkinDriver& driver_;
  Event::TimerPtr flush_timer_;
  Zipkin::SpanBuffer span_buffer_;
  std::string collector_endpoint_;
};
} // Tracing
