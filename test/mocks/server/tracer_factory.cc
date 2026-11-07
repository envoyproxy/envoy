#include "tracer_factory.h"

#include <string>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Server {
namespace Configuration {

MockTracerFactory::MockTracerFactory(const std::string& name) : name_(name) {
  ON_CALL(*this, createEmptyConfigProto()).WillByDefault([] {
    return std::make_unique<Protobuf::Struct>();
  });
}

MockTracerFactory::~MockTracerFactory() = default;

} // namespace Configuration
} // namespace Server
} // namespace Envoy
