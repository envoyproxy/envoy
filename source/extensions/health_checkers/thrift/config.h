#pragma once

#include "envoy/config/core/v3/health_check.pb.h"
#include "envoy/extensions/health_checkers/thrift/v3/thrift.pb.h"
#include "envoy/extensions/health_checkers/thrift/v3/thrift.pb.validate.h"
#include "envoy/server/health_checker_config.h"

#include "source/extensions/health_checkers/thrift/thrift.h"

namespace Envoy {
namespace Extensions {
namespace HealthCheckers {
namespace ThriftHealthChecker {

/**
 * Config registration for the thrift health checker.
 */
class ThriftHealthCheckerFactory : public Server::Configuration::CustomHealthCheckerFactory {
public:
  Upstream::HealthCheckerSharedPtr
  createCustomHealthChecker(const envoy::config::core::v3::HealthCheck& config,
                            Server::Configuration::HealthCheckerFactoryContext& context) override;

  std::string name() const override { return "envoy.health_checkers.thrift"; }
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return ProtobufTypes::MessagePtr{new envoy::extensions::health_checkers::thrift::v3::Thrift()};
  }
};

} // namespace ThriftHealthChecker
} // namespace HealthCheckers
} // namespace Extensions
} // namespace Envoy
