#include <chrono>

#include "envoy/extensions/filters/http/admission_control/v3alpha/admission_control.pb.h"
#include "envoy/extensions/filters/http/admission_control/v3alpha/admission_control.pb.validate.h"

#include "common/stats/isolated_store_impl.h"

#include "extensions/filters/http/admission_control/admission_control.h"
#include "extensions/filters/http/admission_control/evaluators/success_criteria_evaluator.h"

#include "test/mocks/runtime/mocks.h"
#include "test/mocks/server/factory_context.h"
#include "test/mocks/thread_local/mocks.h"
#include "test/test_common/simulated_time_system.h"
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

class AdmissionControlConfigTest : public testing::Test {
public:
  AdmissionControlConfigTest() = default;

  std::shared_ptr<AdmissionControlFilterConfig> makeConfig(const std::string& yaml) {
    AdmissionControlProto proto;
    TestUtility::loadFromYamlAndValidate(yaml, proto);
    auto tls = context_.threadLocal().allocateSlot();
    auto evaluator = std::make_unique<SuccessCriteriaEvaluator>(proto.success_criteria());
    return std::make_shared<AdmissionControlFilterConfig>(proto, runtime_, random_, scope_,
                                                          std::move(tls), std::move(evaluator));
  }

protected:
  NiceMock<Runtime::MockLoader> runtime_;
  NiceMock<Server::Configuration::MockFactoryContext> context_;
  Stats::IsolatedStoreImpl scope_;
  NiceMock<Random::MockRandomGenerator> random_;
};

// Verify the configuration when all fields are set.
TEST_F(AdmissionControlConfigTest, BasicTestAllConfigured) {
  const std::string yaml = R"EOF(
enabled:
  default_value: false
  runtime_key: "foo.enabled"
sampling_window: 1337s
aggression_coefficient:
  default_value: 4.2
  runtime_key: "foo.aggression"
success_criteria:
  http_criteria:
  grpc_criteria:
)EOF";

  auto config = makeConfig(yaml);

  EXPECT_FALSE(config->filterEnabled());
  EXPECT_EQ(4.2, config->aggression());
}

// Verify the config defaults when not specified.
TEST_F(AdmissionControlConfigTest, BasicTestMinimumConfigured) {
  // Empty config. No fields are required.
  AdmissionControlProto proto;

  const std::string yaml = R"EOF(
success_criteria:
  http_criteria:
  grpc_criteria:
)EOF";
  auto config = makeConfig(yaml);

  EXPECT_TRUE(config->filterEnabled());
  EXPECT_EQ(2.0, config->aggression());
}

// Ensure runtime fields are honored.
TEST_F(AdmissionControlConfigTest, VerifyRuntime) {
  const std::string yaml = R"EOF(
enabled:
  default_value: false
  runtime_key: "foo.enabled"
sampling_window: 1337s
aggression_coefficient:
  default_value: 4.2
  runtime_key: "foo.aggression"
success_criteria:
  http_criteria:
  grpc_criteria:
)EOF";

  auto config = makeConfig(yaml);

  EXPECT_CALL(runtime_.snapshot_, getBoolean("foo.enabled", false)).WillOnce(Return(true));
  EXPECT_TRUE(config->filterEnabled());
  EXPECT_CALL(runtime_.snapshot_, getDouble("foo.aggression", 4.2)).WillOnce(Return(1.3));
  EXPECT_EQ(1.3, config->aggression());
}

} // namespace
} // namespace AdmissionControl
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
