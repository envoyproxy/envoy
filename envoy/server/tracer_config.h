#pragma once

#include "envoy/common/pure.h"
#include "envoy/config/typed_config.h"
#include "envoy/server/filter_config.h"
#include "envoy/tracing/tracer.h"

#include "source/common/protobuf/protobuf.h"

namespace Envoy {
namespace Server {
namespace Configuration {

/**
 * Context passed to HTTP tracers to access server resources.
 */
class TracerFactoryContext {
public:
  virtual ~TracerFactoryContext() = default;

  /**
   * @return ServerFactoryContext which lifetime is no shorter than the server.
   */
  virtual ServerFactoryContext& serverFactoryContext() PURE;

  /**
   * @return ProtobufMessage::ValidationVisitor& validation visitor for tracer configuration
   *         messages.
   */
  virtual ProtobufMessage::ValidationVisitor& messageValidationVisitor() PURE;
};

using TracerFactoryContextPtr = std::unique_ptr<TracerFactoryContext>;

/**
 * Implemented by each Tracer and registered via Registry::registerFactory() or the convenience
 * class RegisterFactory.
 */
class TracerFactory : public Config::TypedFactory {
public:
  ~TracerFactory() override = default;

  /**
   * Create a particular trace driver implementation. If the implementation is unable to produce
   * a trace driver with the provided parameters, it should throw an EnvoyException in the case
   * of general error or a Json::Exception if the json configuration is erroneous. The returned
   * pointer should always be valid.
   *
   * NOTE: Due to the corner case of OpenCensus, who can only support a single tracing
   *       configuration per entire process, the returned Driver instance is not guaranteed
   *       to be unique.
   *       That is why the return type has been changed to std::shared_ptr<> instead of a more
   *       idiomatic std::unique_ptr<>.
   *
   * @param config supplies the proto configuration for the Tracer
   * @param context supplies the factory context
   */
  virtual Tracing::DriverSharedPtr createTracerDriver(const Protobuf::Message& config,
                                                      TracerFactoryContext& context) PURE;

  std::string category() const override { return "envoy.tracers"; }
};

} // namespace Configuration
} // namespace Server
} // namespace Envoy
