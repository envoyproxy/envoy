#include "envoy/api/v2/core/health_check.pb.validate.h"
#include "envoy/config/health_checker/mysql/v2/mysql.pb.validate.h"

#include "common/protobuf/utility.h"

namespace Envoy {
namespace Extensions {
namespace HealthCheckers {
namespace MySQLHealthChecker {

static const envoy::config::health_checker::mysql::v2::MySQL
getMySQLHealthCheckConfig(const envoy::api::v2::core::HealthCheck& hc_config) {
  ProtobufTypes::MessagePtr config =
      ProtobufTypes::MessagePtr{new envoy::config::health_checker::mysql::v2::MySQL()};
  MessageUtil::jsonConvert(hc_config.custom_health_check().config(), *config);
  return MessageUtil::downcastAndValidate<const envoy::config::health_checker::mysql::v2::MySQL&>(
      *config);
}

} // namespace MySQLHealthChecker
} // namespace HealthCheckers
} // namespace Extensions
} // namespace Envoy
