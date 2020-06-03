#include <chrono>

#include "envoy/extensions/filters/http/admission_control/v3alpha/admission_control.pb.h"
#include "envoy/extensions/filters/http/admission_control/v3alpha/admission_control.pb.validate.h"

#include "extensions/filters/http/admission_control/admission_control.h"
#include "extensions/filters/http/admission_control/evaluators/default_evaluator.h"

#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::NiceMock;
using testing::Return;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AdmissionControl {
namespace {

class DefaultEvaluatorTest : public testing::Test {
public:
  DefaultEvaluatorTest() = default;

  void makeEvaluator(const std::string& yaml) {
    AdmissionControlProto::DefaultEvaluationCriteria proto;
    TestUtility::loadFromYamlAndValidate(yaml, proto);

    evaluator_ = std::make_unique<DefaultResponseEvaluator>(proto);
  }

  void expectHttpSuccess(int code) { EXPECT_TRUE(evaluator_->isHttpSuccess(code)); }

  void expectHttpFail(int code) { EXPECT_FALSE(evaluator_->isHttpSuccess(code)); }

  void expectGrpcSuccess(int code) { EXPECT_TRUE(evaluator_->isGrpcSuccess(code)); }

  void expectGrpcFail(int code) { EXPECT_FALSE(evaluator_->isGrpcSuccess(code)); }

  void verifyGrpcDefaultEval() {
    // Ok.
    expectGrpcSuccess(0);

    // Aborted.
    expectGrpcFail(10);

    // Data loss.
    expectGrpcFail(15);

    // Deadline exceeded.
    expectGrpcFail(4);

    // Internal
    expectGrpcFail(13);

    // Resource exhausted.
    expectGrpcFail(8);

    // Unavailable.
    expectGrpcFail(14);
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
  std::unique_ptr<DefaultResponseEvaluator> evaluator_;
};

// Ensure the HTTP code succesful range configurations are honored.
TEST_F(DefaultEvaluatorTest, HttpErrorCodes) {
  const std::string yaml = R"EOF(
http_success_status:
  - start: 200
    end:   300
  - start: 400
    end:   500
grpc_success_status:
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
TEST_F(DefaultEvaluatorTest, DefaultBehaviorTest) {
  const std::string yaml = R"EOF(
http_success_status:
grpc_success_status:
)EOF";

  makeEvaluator(yaml);
  verifyGrpcDefaultEval();
  verifyHttpDefaultEval();
}

// Check that GRPC error code configurations are honored.
TEST_F(DefaultEvaluatorTest, GrpcErrorCodes) {
  const std::string yaml = R"EOF(
http_success_status:
grpc_success_status:
  - 7
  - 13
)EOF";

  makeEvaluator(yaml);

  for (int code = 0; code < 15; ++code) {
    if (code == 7 || code == 13) {
      expectGrpcSuccess(code);
    } else {
      expectGrpcFail(code);
    }
  }

  verifyHttpDefaultEval();
}

// Verify correct gRPC range validation.
TEST_F(DefaultEvaluatorTest, GrpcRangeValidation) {
  const std::string yaml = R"EOF(
http_success_status:
grpc_success_status:
  - 17
)EOF";
  EXPECT_DEATH({ makeEvaluator(yaml); }, "invalid gRPC code");
}

// Verify correct HTTP range validation.
TEST_F(DefaultEvaluatorTest, HttpRangeValidation) {
  auto check_ranges = [this](std::string&& yaml) {
    EXPECT_DEATH({ makeEvaluator(yaml); }, "invalid HTTP range");
  };

  check_ranges(R"EOF(
http_success_status:
  - start: 300
    end:   200
grpc_success_status:
)EOF");

  check_ranges(R"EOF(
http_success_status:
  - start: 600
    end:   600
grpc_success_status:
)EOF");

  check_ranges(R"EOF(
http_success_status:
  - start: 99
    end:   99
grpc_success_status:
)EOF");
}

} // namespace
} // namespace AdmissionControl
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
