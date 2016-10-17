#pragma once

#include "envoy/common/pure.h"
#include "envoy/http/access_log.h"
#include "envoy/http/header_map.h"

namespace Tracing {

enum class TracingType {
  // Trace all traceable requests.
  All,
  // Trace only when there is an upstream failure reason.
  UpstreamFailureReason
};

/**
 * Configuration for tracing which is set on the connection manager level.
 * Http Tracing can be enabled/disabled on a per connection manager basis.
 * Here we specify some specific for connection manager settings.
 */
struct TracingConnectionManagerConfig {
  std::string operation_name_;
  TracingType tracing_type_;
};

class TracingContext {
public:
  virtual ~TracingContext() {}

  virtual const std::string& operationName() const PURE;
};

/**
 * Http sink for traces. Sink is responsible for delivering trace to the collector.
 */
class HttpSink {
public:
  virtual ~HttpSink() {}

  virtual void flushTrace(const Http::HeaderMap& request_headers,
                          const Http::HeaderMap& response_headers,
                          const Http::AccessLog::RequestInfo& request_info,
                          const TracingContext& tracing_context) PURE;
};

typedef std::unique_ptr<HttpSink> HttpSinkPtr;

/**
 * HttpTracer is responsible for handling traces and delegate actual flush to sinks.
 */
class HttpTracer {
public:
  virtual ~HttpTracer() {}

  virtual void addSink(HttpSinkPtr&& sink) PURE;
  virtual void trace(const Http::HeaderMap* request_headers,
                     const Http::HeaderMap* response_headers,
                     const Http::AccessLog::RequestInfo& request_info,
                     const TracingContext& tracing_context) PURE;
};

typedef std::unique_ptr<HttpTracer> HttpTracerPtr;

} // Tracing
