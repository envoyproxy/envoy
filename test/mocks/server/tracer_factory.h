#pragma once

#include "envoy/protobuf/message_validator.h"
#include "envoy/server/tracer_config.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Server {
namespace Configuration {
class MockTracerFactory : public TracerFactory {
public:
  explicit MockTracerFactory(const std::string& name);
  ~MockTracerFactory() override;

  std::string name() const override { return name_; }

  MOCK_METHOD(ProtobufTypes::MessagePtr, createEmptyConfigProto, ());
  MOCK_METHOD(Tracing::HttpTracerSharedPtr, createHttpTracer,
              (const Protobuf::Message& config, TracerFactoryContext& context));

private:
  std::string name_;
};
} // namespace Configuration
} // namespace Server
} // namespace Envoy
