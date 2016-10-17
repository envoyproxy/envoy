#pragma once

#include "envoy/common/pure.h"
#include "envoy/http/access_log.h"
#include "envoy/http/header_map.h"

namespace Tracing {

/*
 * Context used by tracers, it carries additional data needed to populate the trace.
 */
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
