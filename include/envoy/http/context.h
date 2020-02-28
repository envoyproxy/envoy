#pragma once

#include <memory>

#include "envoy/http/codes.h"
#include "envoy/tracing/http_tracer.h"

namespace Envoy {
namespace Http {

struct UserAgentContext;

/**
 * Captures http-related structures with cardinality of one per server.
 */
class Context {
public:
  virtual ~Context() = default;
  virtual Tracing::HttpTracer& tracer() PURE;
  virtual CodeStats& codeStats() PURE;
  virtual const UserAgentContext& userAgentContext() const PURE;
};

using ContextPtr = std::unique_ptr<Context>;

} // namespace Http
} // namespace Envoy
