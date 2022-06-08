#pragma once

#include "envoy/server/configuration.h"

#include "instance.h"
#include "tracer_factory.h"
#include "gmock/gmock.h"

namespace Envoy {
namespace Server {
namespace Configuration {
class MockTracerFactoryContext : public TracerFactoryContext {
public:
  MockTracerFactoryContext();
  ~MockTracerFactoryContext() override;

  MOCK_METHOD(ServerFactoryContext&, serverFactoryContext, ());
  MOCK_METHOD(ProtobufMessage::ValidationVisitor&, messageValidationVisitor, ());

  testing::NiceMock<Configuration::MockServerFactoryContext> server_factory_context_;
};
} // namespace Configuration

} // namespace Server
} // namespace Envoy
