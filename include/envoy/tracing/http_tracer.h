#pragma once

#include "envoy/common/pure.h"
#include "envoy/http/access_log.h"
#include "envoy/http/header_map.h"

namespace Tracing {

/*
 * Tracing configuration, it carries additional data needed to populate the trace.
 */
class TracingConfig {
public:
  virtual ~TracingConfig() {}

  virtual const std::string& operationName() const PURE;
};

class Span {
public:
  virtual ~Span() {}

  virtual void setTag(const std::string& name, const std::string& value) PURE;
  virtual void finishSpan() PURE;
};

typedef std::unique_ptr<Span> SpanPtr;

/**
 * Tracing context.
 */
class TracingContext {
public:
  virtual ~TracingContext() {}

  /**
   * Create span ... FIXFIXFIX
   */
  virtual void startSpan(const Http::AccessLog::RequestInfo& request_info,
                         const Http::HeaderMap& request_headers) PURE;
  /**
   * finish created span.
   */
  virtual void finishSpan(const Http::AccessLog::RequestInfo& request_info,
                          const Http::HeaderMap* response_headers) PURE;
};

typedef std::unique_ptr<TracingContext> TracingContextPtr;

/**
 * FIXFIXIFX
 */
class TracingDriver {
public:
  virtual ~TracingDriver() {}

  virtual SpanPtr startSpan(const std::string& operation_name, SystemTime start_time) PURE;
};

typedef std::unique_ptr<TracingDriver> TracingDriverPtr;

/**
 * HttpTracer is responsible for handling traces and delegate actual flush to sinks.
 */
class HttpTracer {
public:
  virtual ~HttpTracer() {}

  virtual void initializeDriver(TracingDriverPtr&& driver) PURE;
  virtual SpanPtr startSpan(const std::string& operation_name, SystemTime start_time) PURE;
};

typedef std::unique_ptr<HttpTracer> HttpTracerPtr;

} // Tracing
