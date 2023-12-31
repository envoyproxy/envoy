#pragma once

#include <cstdint>

#include "envoy/upstream/admission_control.h"

#include "test/integration/retry_admission_control/config.pb.h"
#include "test/integration/retry_admission_control/config.pb.validate.h"
#include "test/test_common/registry.h"

namespace Envoy {

class CustomStreamRetryAdmissionController : public Upstream::RetryStreamAdmissionController {
public:
  CustomStreamRetryAdmissionController(bool allow) : allow_(allow) {}

  void onTryStarted(uint64_t) override {}
  void onTryAborted(uint64_t) override {}
  void onTrySucceeded(uint64_t) override {}
  void onSuccessfulTryFinished() override {}
  bool isRetryAdmitted(uint64_t, uint64_t, bool) override { return allow_; }

private:
  bool allow_;
};

class CustomRetryAdmissionController : public Upstream::RetryAdmissionController {
public:
  CustomRetryAdmissionController(bool allow) : allow_(allow) {}

  Upstream::RetryStreamAdmissionControllerPtr
  createStreamAdmissionController(const StreamInfo::StreamInfo&) override {
    return std::make_unique<CustomStreamRetryAdmissionController>(allow_);
  }

private:
  bool allow_;
};

class CustomRetryAdmissionControllerFactory : public Upstream::RetryAdmissionControllerFactory {
public:
  Upstream::RetryAdmissionControllerSharedPtr
  createAdmissionController(const Protobuf::Message& generic_config,
                            ProtobufMessage::ValidationVisitor& validation_visitor,
                            Runtime::Loader&, std::string,
                            Upstream::ClusterCircuitBreakersStats) override {
    const auto& config = MessageUtil::downcastAndValidate<
        const test::integration::retry_admission_control::CustomRetryAdmissionControlConfig&>(
        generic_config, validation_visitor);
    return std::make_shared<CustomRetryAdmissionController>(config.allow_retries());
  };

  std::string name() const override { return "envoy.retry_admission_control.custom"; }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<
        test::integration::retry_admission_control::CustomRetryAdmissionControlConfig>();
  }
};

} // namespace Envoy
