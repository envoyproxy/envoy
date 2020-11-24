#pragma once

#include "envoy/server/fatal_action_config.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Server {
namespace Configuration {
class MockFatalActionFactory : public FatalActionFactory {
public:
  MOCK_METHOD(FatalActionPtr, createFatalActionFromProto,
              (const envoy::config::bootstrap::v3::FatalAction& config, Instance* server),
              (override));
  MOCK_METHOD(ProtobufTypes::MessagePtr, createEmptyConfigProto, (), (override));
  MOCK_METHOD(std::string, name, (), (const, override));
};
} // namespace Configuration
} // namespace Server
} // namespace Envoy
