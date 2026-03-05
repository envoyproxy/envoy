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
  AdmissionControlImpl(const envoy::config::core::v3::TypedExtensionConfig& config,
                       ProtobufMessage::ValidationVisitor& validation_visitor,
                       Runtime::Loader& runtime, std::string runtime_key_prefix,
                       ClusterCircuitBreakersStats cb_stats, Stats::Scope& scope) {
    auto& attempt_factory =
        Config::Utility::getAndCheckFactory<AttemptAdmissionControllerFactory>(config);
    auto retry_factory_config = Envoy::Config::Utility::translateToFactoryConfig(
        config, validation_visitor, attempt_factory);
    attempt_ = attempt_factory.createAdmissionController(
        *retry_factory_config, validation_visitor, runtime, runtime_key_prefix, cb_stats, scope);
  }

  AttemptAdmissionControllerSharedPtr attempt() override { return attempt_; }

private:
  AttemptAdmissionControllerSharedPtr attempt_;
};
using AdmissionControlImplSharedPtr = std::shared_ptr<AdmissionControlImpl>;

} // namespace Upstream
} // namespace Envoy
