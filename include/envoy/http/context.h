#pragma once

#include <memory>

#include "envoy/http/codes.h"
#include "envoy/tracing/http_tracer.h"

namespace Envoy {
namespace Http {

/**
 * Captures http-related structures with cardinality of one per server.
 */
class Context {
public:
  virtual ~Context() = default;
  virtual Tracing::HttpTracer& tracer() PURE;
  virtual CodeStats& codeStats() PURE;
};

using ContextPtr = std::unique_ptr<Context>;

} // namespace Http
} // namespace Envoy
