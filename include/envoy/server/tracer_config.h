#pragma once

#include "envoy/common/pure.h"
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
class TracerFactory {
public:
  virtual ~TracerFactory() {}

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

  /**
   * @return ProtobufTypes::MessagePtr create empty config proto message for v2. The tracing
   *         config, which arrives in an opaque message, will be parsed into this empty proto.
   */
  virtual ProtobufTypes::MessagePtr createEmptyConfigProto() PURE;

  /**
   * Returns the identifying name for a particular implementation of tracer produced by the
   * factory.
   */
  virtual std::string name() PURE;
};

} // namespace Configuration
} // namespace Server
} // namespace Envoy