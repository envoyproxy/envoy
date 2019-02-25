#pragma once

#include "envoy/server/instance.h"
#include "envoy/server/tracer_config.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace Common {

/**
 * Common base class for tracer factory registrations. Removes a substantial amount of
 * boilerplate.
 */
template <class ConfigProto> class FactoryBase : public Server::Configuration::TracerFactory {
public:
  // Server::Configuration::TracerFactory
  virtual Tracing::HttpTracerPtr createHttpTracer(const Protobuf::Message& config,
                                                  Server::Instance& server) override {
    return createHttpTracerTyped(MessageUtil::downcastAndValidate<const ConfigProto&>(config),
                                 server);
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<ConfigProto>();
  }

  std::string name() override { return name_; }

protected:
  FactoryBase(const std::string& name) : name_(name) {}

private:
  virtual Tracing::HttpTracerPtr createHttpTracerTyped(const ConfigProto& proto_config,
                                                       Server::Instance& server) PURE;

  const std::string name_;
};

} // namespace Common
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
