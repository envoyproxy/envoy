#include "source/extensions/retry/admission_control/static_limits/config.h"

#include "envoy/extensions/retry/admission_control/static_limits/v3/static_limits_config.pb.h"
#include "envoy/registry/registry.h"
#include "envoy/upstream/admission_control.h"

#include "source/common/protobuf/message_validator_impl.h"
#include "source/common/protobuf/utility.h"
#include "source/extensions/retry/admission_control/static_limits/static_limits.h"

namespace Envoy {
namespace Extensions {
namespace Retry {
namespace AdmissionControl {

REGISTER_FACTORY(StaticLimitsFactory, Upstream::RetryAdmissionControllerFactory);

Upstream::RetryAdmissionControllerSharedPtr StaticLimitsFactory::createAdmissionController(
    const Protobuf::Message& config, ProtobufMessage::ValidationVisitor& validation_visitor,
    Runtime::Loader& runtime, std::string runtime_key_prefix,
    Upstream::ClusterCircuitBreakersStats cb_stats) {
  const auto& static_limits_config = MessageUtil::downcastAndValidate<
      const envoy::extensions::retry::admission_control::static_limits::v3::StaticLimitsConfig&>(
      config, validation_visitor);
  std::string max_active_retries_key = runtime_key_prefix + "max_retries";
  uint64_t max_concurrent_retries = static_limits_config.has_max_concurrent_retries()
                                        ? static_limits_config.max_concurrent_retries().value()
                                        : 3UL /* default */;
  return std::make_shared<StaticLimits>(max_concurrent_retries, runtime, max_active_retries_key,
                                        cb_stats);
}

} // namespace AdmissionControl
} // namespace Retry
} // namespace Extensions
} // namespace Envoy
