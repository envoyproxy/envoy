#pragma once

#include <memory>

#include "envoy/extensions/retry/admission_control/static_limits/v3/static_limits_config.pb.h"
#include "envoy/extensions/retry/admission_control/static_limits/v3/static_limits_config.pb.validate.h"
#include "envoy/upstream/admission_control.h"

#include "source/extensions/retry/admission_control/static_limits/static_limits.h"

namespace Envoy {
namespace Extensions {
namespace Retry {
namespace AdmissionControl {

class StaticLimitsFactory : public Upstream::RetryAdmissionControllerFactory {
public:
  Upstream::RetryAdmissionControllerSharedPtr
  createAdmissionController(const Protobuf::Message& config,
                            ProtobufMessage::ValidationVisitor& validation_visitor,
                            Runtime::Loader& runtime, std::string runtime_key_prefix,
                            Upstream::ClusterCircuitBreakersStats cb_stats) override;

  std::string name() const override { return "envoy.retry_admission_control.static_limits"; }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<
        envoy::extensions::retry::admission_control::static_limits::v3::StaticLimitsConfig>();
  }
};

} // namespace AdmissionControl
} // namespace Retry
} // namespace Extensions
} // namespace Envoy
