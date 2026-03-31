#include "admission_control.h"

#include <cstdint>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Upstream {

using testing::_;
using testing::Invoke;
using testing::Return;

MockAttemptStreamAdmissionController::MockAttemptStreamAdmissionController() {
  ON_CALL(*this, isAttemptAdmitted(_, _, _)).WillByDefault(Return(true));
  ON_CALL(*this, isInitialAttemptAdmitted()).WillByDefault(Return(true));
};

MockAttemptAdmissionController::MockAttemptAdmissionController()
    : stream_admission_controller_(
          std::make_unique<NiceMock<MockAttemptStreamAdmissionController>>()) {
  ON_CALL(*this, createStreamAdmissionController(_))
      .WillByDefault(Invoke(
          [this](const StreamInfo::StreamInfo&) mutable -> AttemptStreamAdmissionControllerPtr {
            // shuffle things around so we don't hold onto a ref to the old ptr
            auto ptr = std::move(stream_admission_controller_);
            stream_admission_controller_ =
                std::make_unique<NiceMock<MockAttemptStreamAdmissionController>>();
            return ptr;
          }));
};

MockAdmissionControl::MockAdmissionControl()
    : attempt_admission_controller_(std::make_shared<NiceMock<MockAttemptAdmissionController>>()) {
  ON_CALL(*this, attempt()).WillByDefault(Return(attempt_admission_controller_));
};

} // namespace Upstream
} // namespace Envoy
