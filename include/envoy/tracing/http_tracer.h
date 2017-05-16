#pragma once

#include <memory>
#include <string>
#include <vector>

#include "envoy/common/pure.h"
#include "envoy/http/access_log.h"
#include "envoy/http/header_map.h"

namespace Envoy {
namespace Tracing {

enum class OperationName { Ingress, Egress };

/*
 * Tracing configuration, it carries additional data needed to populate the span.
 */
class Config {
public:
  virtual ~Config() {}

  /**
   * @return operation name for tracing, e.g., ingress.
   */
  virtual OperationName operationName() const PURE;

  /**
   * @return list of headers to populate tags on the active span.
   */
  virtual const std::vector<Http::LowerCaseString>& requestHeadersForTags() const PURE;
};

/*
 * Basic abstraction for span.
 */
class Span;

typedef std::unique_ptr<Span> SpanPtr;

class Span {
public:
  virtual ~Span() {}

  virtual void setTag(const std::string& name, const std::string& value) PURE;
  virtual void finishSpan() PURE;
  virtual void injectContext(Http::HeaderMap& request_headers) PURE;
  virtual SpanPtr spawnChild(const std::string& name, SystemTime start_time) PURE;
};

/**
 * Tracing driver is responsible for span creation.
 */
class Driver {
public:
  virtual ~Driver() {}

  /**
   * Start driver specific span.
   */
  virtual SpanPtr startSpan(Http::HeaderMap& request_headers, const std::string& operation_name,
                            SystemTime start_time) PURE;
};

typedef std::unique_ptr<Driver> DriverPtr;

/**
 * HttpTracer is responsible for handling traces and delegate actions to the
 * corresponding drivers.
 */
class HttpTracer {
public:
  virtual ~HttpTracer() {}

  virtual SpanPtr startSpan(const Config& config, Http::HeaderMap& request_headers,
                            const Http::AccessLog::RequestInfo& request_info) PURE;
};

typedef std::unique_ptr<HttpTracer> HttpTracerPtr;

} // Tracing
} // Envoy
