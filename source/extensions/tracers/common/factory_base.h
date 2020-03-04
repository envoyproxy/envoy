#pragma once

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
  Tracing::HttpTracerPtr
  createHttpTracer(const Protobuf::Message& config,
                   Server::Configuration::TracerFactoryContext& context) override {
    return createHttpTracerTyped(MessageUtil::downcastAndValidate<const ConfigProto&>(
                                     config, context.messageValidationVisitor()),
                                 context);
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<ConfigProto>();
  }

  std::string name() const override { return name_; }

protected:
  FactoryBase(const std::string& name) : name_(name) {}

private:
  virtual Tracing::HttpTracerPtr
  createHttpTracerTyped(const ConfigProto& proto_config,
                        Server::Configuration::TracerFactoryContext& context) PURE;

  const std::string name_;
};

} // namespace Common
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
