#pragma once

#include "envoy/config/core/v3/health_check.pb.h"
#include "envoy/extensions/health_checkers/thrift/v3/thrift.pb.h"
#include "envoy/extensions/health_checkers/thrift/v3/thrift.pb.validate.h"

#include "source/common/config/utility.h"
#include "source/common/protobuf/protobuf.h"
#include "source/common/protobuf/utility.h"

namespace Envoy {
namespace Extensions {
namespace HealthCheckers {
namespace ThriftHealthChecker {

namespace {

static const envoy::extensions::health_checkers::thrift::v3::Thrift
getThriftHealthCheckConfig(const envoy::config::core::v3::HealthCheck& health_check_config,
                           ProtobufMessage::ValidationVisitor& validation_visitor) {
  ProtobufTypes::MessagePtr config =
      ProtobufTypes::MessagePtr{new envoy::extensions::health_checkers::thrift::v3::Thrift()};
  Envoy::Config::Utility::translateOpaqueConfig(
      health_check_config.custom_health_check().typed_config(), validation_visitor, *config);
  return MessageUtil::downcastAndValidate<
      const envoy::extensions::health_checkers::thrift::v3::Thrift&>(*config, validation_visitor);
}

} // namespace
} // namespace ThriftHealthChecker
} // namespace HealthCheckers
} // namespace Extensions
} // namespace Envoy
