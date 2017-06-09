#pragma once

#include <string>

#include "envoy/local_info/local_info.h"
#include "envoy/runtime/runtime.h"
#include "envoy/thread_local/thread_local.h"
#include "envoy/tracing/http_tracer.h"
#include "envoy/upstream/cluster_manager.h"

#include "common/http/header_map_impl.h"
#include "common/json/json_loader.h"

namespace Envoy {
namespace Tracing {

enum class Reason {
  NotTraceableRequestId,
  HealthCheck,
  Sampling,
  ServiceForced,
  ClientForced,
};

struct Decision {
  Reason reason;
  bool is_tracing;
};

class HttpTracerUtility {
public:
  /**
   * Get string representation of the operation.
   * @param operation name to convert.
   * @return string representation of the operation.
   */
  static const std::string& toString(OperationName operation_name);

  /**
   * Request might be traceable if x-request-id is traceable uuid or we do sampling tracing.
   * Note: there is a global switch which turns off tracing completely on server side.
   *
   * @return decision if request is traceable or not and Reason why.
   **/
  static Decision isTracing(const Http::AccessLog::RequestInfo& request_info,
                            const Http::HeaderMap& request_headers);

  /**
   * Mutate request headers if request needs to be traced.
   */
  static void mutateHeaders(Http::HeaderMap& request_headers, Runtime::Loader& runtime);

  /**
   * 1) Fill in span tags based on the response headers.
   * 2) Finish active span.
   */
  static void finalizeSpan(Span& active_span, const Http::HeaderMap& request_headers,
                           const Http::AccessLog::RequestInfo& request_info,
                           const Config& tracing_config);

  static const std::string INGRESS_OPERATION;
  static const std::string EGRESS_OPERATION;
};

class NullSpan : public Span {
public:
  // Tracing::Span
  void setTag(const std::string&, const std::string&) override {}
  void finishSpan(SpanFinalizer&) override {}
  void injectContext(Http::HeaderMap&) override {}
  SpanPtr spawnChild(const std::string&, SystemTime) override { return SpanPtr{new NullSpan()}; }
};

class NullFinalizer : public SpanFinalizer {
public:
  // Tracing::SpanFinalizer
  void finalize(Span&) override {}
};

/**
 * Finalizer for Spans covering standard request ingress.
 */
class HttpConnManFinalizerImpl : public SpanFinalizer {
public:
  HttpConnManFinalizerImpl(Http::HeaderMap* request_headers,
                           Http::AccessLog::RequestInfo& request_info, Config& tracing_config);

  void finalize(Span& span) override;

private:
  Http::HeaderMap* request_headers_;
  Http::AccessLog::RequestInfo& request_info_;
  Config& tracing_config_;
};

class HttpNullTracer : public HttpTracer {
public:
  // Tracing::HttpTracer
  SpanPtr startSpan(const Config&, Http::HeaderMap&, const Http::AccessLog::RequestInfo&) override {
    return SpanPtr{new NullSpan()};
  }
};

class HttpTracerImpl : public HttpTracer {
public:
  HttpTracerImpl(DriverPtr&& driver, const LocalInfo::LocalInfo& local_info);

  // Tracing::HttpTracer
  SpanPtr startSpan(const Config& config, Http::HeaderMap& request_headers,
                    const Http::AccessLog::RequestInfo& request_info) override;

private:
  DriverPtr driver_;
  const LocalInfo::LocalInfo& local_info_;
};

} // Tracing
} // Envoy
