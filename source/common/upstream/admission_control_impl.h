#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>

#include "envoy/config/core/v3/extension.pb.h"
#include "envoy/upstream/admission_control.h"

#include "source/common/common/assert.h"
#include "source/common/config/utility.h"
#include "source/common/protobuf/message_validator_impl.h"

namespace Envoy {
namespace Upstream {
class AdmissionControlImpl : public AdmissionControl {
public:
  AdmissionControlImpl(const envoy::config::core::v3::TypedExtensionConfig& retry_config,
                       ProtobufMessage::ValidationVisitor& validation_visitor,
                       Runtime::Loader& runtime, std::string runtime_key_prefix,
                       ClusterCircuitBreakersStats cb_stats) {
    auto& retry_factory =
        Config::Utility::getAndCheckFactory<RetryAdmissionControllerFactory>(retry_config);
    auto retry_factory_config = Envoy::Config::Utility::translateToFactoryConfig(
        retry_config, validation_visitor, retry_factory);
    retry_ = retry_factory.createAdmissionController(*retry_factory_config, validation_visitor,
                                                     runtime, runtime_key_prefix, cb_stats);
  }

  RetryAdmissionControllerSharedPtr retry() override { return retry_; }

private:
  RetryAdmissionControllerSharedPtr retry_;
};
using AdmissionControlImplSharedPtr = std::shared_ptr<AdmissionControlImpl>;

} // namespace Upstream
} // namespace Envoy
