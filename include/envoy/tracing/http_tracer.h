#pragma once

#include "envoy/common/pure.h"
#include "envoy/http/access_log.h"
#include "envoy/http/header_map.h"

namespace Tracing {

/*
 * Tracing configuration, it carries additional data needed to populate the span.
 */
class Config {
public:
  virtual ~Config() {}

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
 * Tracing driver is responsible for span creation.
 */
class Driver {
public:
  virtual ~Driver() {}

  virtual SpanPtr startSpan(const std::string& operation_name, SystemTime start_time) PURE;
};

typedef std::unique_ptr<Driver> DriverPtr;

/**
 * HttpTracer is responsible for handling traces and delegate actions to the
 * corresponding drivers.
 */
class HttpTracer {
public:
  virtual ~HttpTracer() {}

  virtual SpanPtr startSpan(const Config& config, const Http::HeaderMap& request_headers,
                            const Http::AccessLog::RequestInfo& request_info) PURE;
};

typedef std::unique_ptr<HttpTracer> HttpTracerPtr;

} // Tracing
