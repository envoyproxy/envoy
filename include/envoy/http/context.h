#pragma once

#include <memory>

#include "envoy/config/trace/v3/trace.pb.h"
#include "envoy/http/codes.h"

namespace Envoy {
namespace Http {

struct UserAgentContext;

/**
 * Captures http-related structures with cardinality of one per server.
 */
class Context {
public:
  virtual ~Context() = default;

  /**
   * Get the default tracing configuration, i.e. one from the bootstrap config.
   *
   * Once deprecation window for the tracer provider configuration in the bootstrap config is over,
   * this method will no longer be necessary.
   *
   * @return Tracing.
   */
  virtual const envoy::config::trace::v3::Tracing& defaultTracingConfig() PURE;

  virtual CodeStats& codeStats() PURE;
  virtual const UserAgentContext& userAgentContext() const PURE;
};

using ContextPtr = std::unique_ptr<Context>;

} // namespace Http
} // namespace Envoy
