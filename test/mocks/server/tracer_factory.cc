#include "tracer_factory.h"

#include <string>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::Invoke;
using testing::Return;
using testing::ReturnPointee;
using testing::ReturnRef;
using testing::SaveArg;

namespace Envoy {
namespace Server {
namespace Configuration {
MockTracerFactory::MockTracerFactory(const std::string& name) : name_(name) {
  ON_CALL(*this, createEmptyConfigProto()).WillByDefault(Invoke([] {
    return std::make_unique<ProtobufWkt::Struct>();
  }));
}

MockTracerFactory::~MockTracerFactory() = default;

} // namespace Configuration

} // namespace Server

} // namespace Envoy
