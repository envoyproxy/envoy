#include <chrono>

#include "envoy/extensions/filters/http/admission_control/v3alpha/admission_control.pb.h"
#include "envoy/extensions/filters/http/admission_control/v3alpha/admission_control.pb.validate.h"

#include "extensions/filters/http/admission_control/evaluators/default_evaluator.h"

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

  DefaultResponseEvaluator makeEvaluator(const std::string& yaml) {
    AdmissionControlProto::DefaultEvaluationCriteria proto;
    TestUtility::loadFromYamlAndValidate(yaml, proto);

    evaluator_ = std::make_unique<DefaultResponseEvaluator>(proto);
  }

  void expectHttpSuccess(int code) {
    EXPECT_TRUE(evaluator_->isHttpSuccess(code));
  }

  void expectHttpFail(int code) {
    EXPECT_FALSE(evaluator_->isHttpSuccess(code));
  }

  void expectGrpcSuccess(int code) {
    EXPECT_TRUE(evaluator_->isGrpcSuccess(code));
  }

  void expectGrpcFail(int code) {
    EXPECT_FALSE(evaluator_->isGrpcSuccess(code));
  }

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
  std::unique_ptr<DefaultResposeEvalutor> evaluator_;
};

// Ensure the HTTP error code range configurations are honored.
TEST_F(DefaultEvaluatorTest, HttpErrorCodes) {
  const std::string yaml = R"EOF(
http_status:
  - start: 300
    end:   400
grpc_status:
)EOF";

  makeEvaluator(yaml);

  for (int code = 200; code < 500; ++code) {
    if (code < 400 && code >= 300) {
      expectHttpFail(code);
      continue;
    }

    expectHttpSuccess(code);
  }
}

// Verify default behavior of the filter.
TEST_F(DefaultEvalutorTest, DefaultBehaviorTest) {
  const std::string yaml = R"EOF(
http_status:
grpc_status:
)EOF";

  makeEvaluator(yaml);
  verifyGrpcDefaultEval();
  verifyHttpDefaultEval();
}

// Check that GRPC error code configurations are honored.
TEST_F(DefaultEvaluatorTest, GrpcErrorCodes) {
  const std::string yaml = R"EOF(
http_status:
grpc_status:
  - 7
  - 13
)EOF";

  auto evaluator = makeEvaluator(yaml);

  for (int code = 0; code < 15; ++code) {
    if (code == 7 || code == 13) {
      EXPECT_TRUE(evaluator.isGrpcSuccess(code));
    } else {
      EXPECT_FALSE(evaluator.isGrpcSuccess(code));
    }
  }
}

} // namespace
} // namespace AdmissionControl
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy

