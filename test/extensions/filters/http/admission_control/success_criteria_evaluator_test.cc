#include <chrono>

#include "envoy/extensions/filters/http/admission_control/v3/admission_control.pb.h"
#include "envoy/extensions/filters/http/admission_control/v3/admission_control.pb.validate.h"

#include "source/common/common/enum_to_int.h"
#include "source/extensions/filters/http/admission_control/admission_control.h"
#include "source/extensions/filters/http/admission_control/evaluators/success_criteria_evaluator.h"

#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AdmissionControl {
namespace {

class SuccessCriteriaTest : public testing::Test {
public:
  SuccessCriteriaTest() = default;

  void makeEvaluator(const std::string& yaml) {
    AdmissionControlProto::SuccessCriteria proto;
    TestUtility::loadFromYamlAndValidate(yaml, proto);

    evaluator_ = std::make_unique<SuccessCriteriaEvaluator>(proto);
  }

  void expectHttpSuccess(int code) { EXPECT_TRUE(evaluator_->isHttpSuccess(code)); }

  void expectHttpFail(int code) { EXPECT_FALSE(evaluator_->isHttpSuccess(code)); }

  void expectGrpcSuccess(int code) { EXPECT_TRUE(evaluator_->isGrpcSuccess(code)); }

  void expectGrpcFail(int code) { EXPECT_FALSE(evaluator_->isGrpcSuccess(code)); }

  void verifyGrpcDefaultEval() {
    expectGrpcSuccess(Grpc::Status::WellKnownGrpcStatus::AlreadyExists);
    expectGrpcSuccess(Grpc::Status::WellKnownGrpcStatus::Canceled);
    expectGrpcSuccess(Grpc::Status::WellKnownGrpcStatus::FailedPrecondition);
    expectGrpcSuccess(Grpc::Status::WellKnownGrpcStatus::InvalidArgument);
    expectGrpcSuccess(Grpc::Status::WellKnownGrpcStatus::NotFound);
    expectGrpcSuccess(Grpc::Status::WellKnownGrpcStatus::Ok);
    expectGrpcSuccess(Grpc::Status::WellKnownGrpcStatus::OutOfRange);
    expectGrpcSuccess(Grpc::Status::WellKnownGrpcStatus::PermissionDenied);
    expectGrpcSuccess(Grpc::Status::WellKnownGrpcStatus::Unauthenticated);
    expectGrpcSuccess(Grpc::Status::WellKnownGrpcStatus::Unimplemented);
    expectGrpcSuccess(Grpc::Status::WellKnownGrpcStatus::Unknown);

    expectGrpcFail(enumToInt(Grpc::Status::WellKnownGrpcStatus::Aborted));
    expectGrpcFail(enumToInt(Grpc::Status::WellKnownGrpcStatus::DataLoss));
    expectGrpcFail(enumToInt(Grpc::Status::WellKnownGrpcStatus::DeadlineExceeded));
    expectGrpcFail(enumToInt(Grpc::Status::WellKnownGrpcStatus::Internal));
    expectGrpcFail(enumToInt(Grpc::Status::WellKnownGrpcStatus::ResourceExhausted));
    expectGrpcFail(enumToInt(Grpc::Status::WellKnownGrpcStatus::Unavailable));
  }

  void verifyHttpDefaultEval() {
    for (int code = 200; code < 600; ++code) {
      if (code < 500) {
        expectHttpSuccess(code);
      } else {
        expectHttpFail(code);
      }
    }
  }

protected:
  std::unique_ptr<SuccessCriteriaEvaluator> evaluator_;
};

// Ensure the HTTP code successful range configurations are honored.
TEST_F(SuccessCriteriaTest, HttpErrorCodes) {
  const std::string yaml = R"EOF(
http_criteria:
  http_success_status:
  - start: 200
    end:   300
  - start: 400
    end:   500
)EOF";

  makeEvaluator(yaml);

  for (int code = 200; code < 600; ++code) {
    if ((code < 300 && code >= 200) || (code < 500 && code >= 400)) {
      expectHttpSuccess(code);
      continue;
    }

    expectHttpFail(code);
  }

  verifyGrpcDefaultEval();
}

// Verify default success values of the evaluator.
TEST_F(SuccessCriteriaTest, DefaultBehaviorTest) {
  const std::string yaml = R"EOF(
http_criteria:
grpc_criteria:
)EOF";

  makeEvaluator(yaml);
  verifyGrpcDefaultEval();
  verifyHttpDefaultEval();
}

// Check that GRPC error code configurations are honored.
TEST_F(SuccessCriteriaTest, GrpcErrorCodes) {
  const std::string yaml = R"EOF(
grpc_criteria:
  grpc_success_status:
  - 7
  - 13
)EOF";

  makeEvaluator(yaml);

  using GrpcStatus = Grpc::Status::WellKnownGrpcStatus;
  for (int code = GrpcStatus::Ok; code <= GrpcStatus::MaximumKnown; ++code) {
    if (code == 7 || code == 13) {
      expectGrpcSuccess(code);
    } else {
      expectGrpcFail(code);
    }
  }

  verifyHttpDefaultEval();
}

// Verify correct gRPC range validation.
TEST_F(SuccessCriteriaTest, GrpcRangeValidation) {
  const std::string yaml = R"EOF(
grpc_criteria:
  grpc_success_status:
    - 17
)EOF";
  EXPECT_THROW_WITH_REGEX(makeEvaluator(yaml), EnvoyException, "invalid gRPC code*");
}

// Verify correct HTTP range validation.
TEST_F(SuccessCriteriaTest, HttpRangeValidation) {
  auto check_ranges = [this](std::string&& yaml) {
    EXPECT_THROW_WITH_REGEX(makeEvaluator(yaml), EnvoyException, "invalid HTTP range*");
  };

  check_ranges(R"EOF(
http_criteria:
  http_success_status:
    - start: 300
      end:   200
)EOF");

  check_ranges(R"EOF(
http_criteria:
  http_success_status:
    - start: 600
      end:   600
)EOF");

  check_ranges(R"EOF(
http_criteria:
  http_success_status:
    - start: 99
      end:   99
)EOF");
}

} // namespace
} // namespace AdmissionControl
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
