#pragma once

#include <cstdint>

#include "envoy/upstream/admission_control.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Upstream {

using testing::NiceMock;

class MockRetryStreamAdmissionController : public RetryStreamAdmissionController {
public:
  MockRetryStreamAdmissionController();
  ~MockRetryStreamAdmissionController() override;

  MOCK_METHOD(void, onTryStarted, (uint64_t));
  MOCK_METHOD(void, onTrySucceeded, (uint64_t));
  MOCK_METHOD(void, onSuccessfulTryFinished, ());
  MOCK_METHOD(void, onTryAborted, (uint64_t));
  MOCK_METHOD(bool, isRetryAdmitted, (uint64_t, uint64_t, bool));
};

using MockRetryStreamAdmissionControllerPtr =
    std::unique_ptr<NiceMock<MockRetryStreamAdmissionController>>;

class MockRetryAdmissionController : public RetryAdmissionController {
public:
  MockRetryAdmissionController();
  ~MockRetryAdmissionController() override;

  MOCK_METHOD(RetryStreamAdmissionControllerPtr, createStreamAdmissionController,
              (const StreamInfo::StreamInfo&));

public:
  // Note that by default this will get re-initialized on each call to
  // createStreamAdmissionController to avoid sharing. To change the behavior, you can override the
  // default behavior for createStreamAdmissionController.
  MockRetryStreamAdmissionControllerPtr stream_admission_controller_;
};

using MockRetryAdmissionControllerSharedPtr =
    std::shared_ptr<NiceMock<MockRetryAdmissionController>>;

class MockAdmissionControl : public AdmissionControl {
public:
  MockAdmissionControl();
  ~MockAdmissionControl() override;

  MOCK_METHOD(RetryAdmissionControllerSharedPtr, retry, ());

  MockRetryAdmissionControllerSharedPtr retry_admission_controller_;
};

} // namespace Upstream
} // namespace Envoy
