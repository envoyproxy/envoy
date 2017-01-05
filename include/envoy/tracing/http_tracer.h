#pragma once

#include "envoy/common/pure.h"
#include "envoy/http/access_log.h"
#include "envoy/http/header_map.h"

namespace Tracing {

/*
 * Tracing configuration, it carries additional data needed to populate the span.
 */
class TracingConfig {
public:
  virtual ~TracingConfig() {}

  virtual const std::string& operationName() const PURE;
};

/*
 * Basic abstraction for span.
 */
class Span {
public:
  virtual ~Span() {}

  virtual void setTag(const std::string& name, const std::string& value) PURE;
  virtual void finishSpan() PURE;
};

typedef std::unique_ptr<Span> SpanPtr;

/**
 * Tracing context, it carries currently active span.
 */
class TracingContext {
public:
  virtual ~TracingContext() {}

  /**
   * Start span.
   */
  virtual void startSpan(const Http::AccessLog::RequestInfo& request_info,
                         const Http::HeaderMap& request_headers) PURE;
  /**
   * Finish created span.
   */
  virtual void finishSpan(const Http::AccessLog::RequestInfo& request_info,
                          const Http::HeaderMap* response_headers) PURE;
};

typedef std::unique_ptr<TracingContext> TracingContextPtr;

/**
 * Tracing driver is responsible for span creation.
 *
 * TODO: make tracing driver to be responsible for context extraction/injection.
 */
class TracingDriver {
public:
  virtual ~TracingDriver() {}

  virtual SpanPtr startSpan(const std::string& operation_name, SystemTime start_time) PURE;
};

typedef std::unique_ptr<TracingDriver> TracingDriverPtr;

/**
 * HttpTracer is responsible for handling traces and delegate actions to the
 * corresponding drivers.
 */
class HttpTracer {
public:
  virtual ~HttpTracer() {}

  virtual void initializeDriver(TracingDriverPtr&& driver) PURE;
  virtual SpanPtr startSpan(const std::string& operation_name, SystemTime start_time) PURE;
};

typedef std::unique_ptr<HttpTracer> HttpTracerPtr;

} // Tracing
