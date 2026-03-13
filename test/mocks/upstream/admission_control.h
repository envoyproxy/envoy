#pragma once

#include <cstdint>

#include "envoy/upstream/admission_control.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Upstream {

using testing::NiceMock;

class MockAttemptStreamAdmissionController : public AttemptStreamAdmissionController {
public:
  MockAttemptStreamAdmissionController();
  ~MockAttemptStreamAdmissionController() override = default;

  MOCK_METHOD(void, onTryStarted, (uint32_t));
  MOCK_METHOD(void, onTrySucceeded, (uint32_t));
  MOCK_METHOD(void, onSuccessfulTryFinished, ());
  MOCK_METHOD(void, onTryAborted, (uint32_t));
  MOCK_METHOD(bool, isAttemptAdmitted, (uint32_t, uint32_t, bool));
  MOCK_METHOD(bool, isInitialAttemptAdmitted, ());
  MOCK_METHOD(absl::string_view, initialAttemptDetails, (), (const));
};

using MockAttemptStreamAdmissionControllerPtr =
    std::unique_ptr<NiceMock<MockAttemptStreamAdmissionController>>;

class MockAttemptAdmissionController : public AttemptAdmissionController {
public:
  MockAttemptAdmissionController();
  ~MockAttemptAdmissionController() override = default;

  MOCK_METHOD(AttemptStreamAdmissionControllerPtr, createStreamAdmissionController,
              (const StreamInfo::StreamInfo&));

public:
  // Note that by default this will get re-initialized on each call to
  // createStreamAdmissionController to avoid sharing. To change the behavior, you can override the
  // default behavior for createStreamAdmissionController.
  MockAttemptStreamAdmissionControllerPtr stream_admission_controller_;
};

using MockAttemptAdmissionControllerSharedPtr =
    std::shared_ptr<NiceMock<MockAttemptAdmissionController>>;

class MockAdmissionControl : public AdmissionControl {
public:
  MockAdmissionControl();
  ~MockAdmissionControl() override = default;

  MOCK_METHOD(AttemptAdmissionControllerSharedPtr, attempt, ());

  MockAttemptAdmissionControllerSharedPtr attempt_admission_controller_;
};

} // namespace Upstream
} // namespace Envoy
