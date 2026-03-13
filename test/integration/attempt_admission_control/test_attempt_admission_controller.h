#pragma once

#include <cstdint>

#include "envoy/upstream/admission_control.h"

#include "test/integration/attempt_admission_control/config.pb.h"
#include "test/integration/attempt_admission_control/config.pb.validate.h"
#include "test/test_common/registry.h"

namespace Envoy {

class TestAttemptStreamAdmissionController : public Upstream::AttemptStreamAdmissionController {
public:
  TestAttemptStreamAdmissionController(
      const test::integration::attempt_admission_control::TestAttemptAdmissionControlConfig& config)
      : allow_initial_attempt_(config.allow_initial_request()),
        retries_to_allow_(config.retries_to_allow()) {}

  void onTryStarted(uint32_t) override {}
  void onTryAborted(uint32_t) override {}
  void onTrySucceeded(uint32_t) override {}
  void onSuccessfulTryFinished() override {}
  bool isAttemptAdmitted(uint32_t, uint32_t retry_attempt_number, bool) override {
    return retry_attempt_number <= retries_to_allow_;
  }
  bool isInitialAttemptAdmitted() override { return allow_initial_attempt_; }
  absl::string_view initialAttemptDetails() const override { return ""; }

private:
  const bool allow_initial_attempt_;
  const uint32_t retries_to_allow_;
};

class TestAttemptAdmissionController : public Upstream::AttemptAdmissionController {
public:
  TestAttemptAdmissionController(
      const test::integration::attempt_admission_control::TestAttemptAdmissionControlConfig& config)
      : config_(config) {}

  Upstream::AttemptStreamAdmissionControllerPtr
  createStreamAdmissionController(const StreamInfo::StreamInfo&) override {
    return std::make_unique<TestAttemptStreamAdmissionController>(config_);
  }

private:
  const test::integration::attempt_admission_control::TestAttemptAdmissionControlConfig config_;
};

class TestAttemptAdmissionControllerFactory : public Upstream::AttemptAdmissionControllerFactory {
public:
  Upstream::AttemptAdmissionControllerSharedPtr
  createAdmissionController(const Protobuf::Message& generic_config,
                            ProtobufMessage::ValidationVisitor& validation_visitor,
                            Runtime::Loader&, std::string,
                            const Upstream::ClusterCircuitBreakersStats&, Stats::Scope&) override {
    const auto& config = MessageUtil::downcastAndValidate<
        const test::integration::attempt_admission_control::TestAttemptAdmissionControlConfig&>(
        generic_config, validation_visitor);
    return std::make_shared<TestAttemptAdmissionController>(config);
  };

  std::string name() const override { return "envoy.attempt_admission_control.test"; }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<
        test::integration::attempt_admission_control::TestAttemptAdmissionControlConfig>();
  }
};

} // namespace Envoy
