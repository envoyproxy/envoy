#pragma once

#include "envoy/common/pure.h"
#include "envoy/config/typed_config.h"
#include "envoy/server/instance.h"
#include "envoy/tracing/http_tracer.h"

#include "common/protobuf/protobuf.h"

namespace Envoy {
namespace Server {
namespace Configuration {

/**
 * Implemented by each Tracer and registered via Registry::registerFactory() or the convenience
 * class RegisterFactory.
 */
class TracerFactory : public Config::TypedFactory {
public:
  virtual ~TracerFactory() = default;

  /**
   * Create a particular HttpTracer implementation. If the implementation is unable to produce an
   * HttpTracer with the provided parameters, it should throw an EnvoyException in the case of
   * general error or a Json::Exception if the json configuration is erroneous. The returned
   * pointer should always be valid.
   * @param config supplies the proto configuration for the HttpTracer
   * @param server supplies the server instance
   */
  virtual Tracing::HttpTracerPtr createHttpTracer(const Protobuf::Message& config,
                                                  Instance& server) PURE;

  std::string category() const override { return "tracers"; }
};

} // namespace Configuration
} // namespace Server
} // namespace Envoy
