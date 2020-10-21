#include <chrono>

#include "envoy/extensions/filters/http/admission_control/v3alpha/admission_control.pb.h"
#include "envoy/extensions/filters/http/admission_control/v3alpha/admission_control.pb.validate.h"

#include "common/stats/isolated_store_impl.h"

#include "extensions/filters/http/admission_control/admission_control.h"
#include "extensions/filters/http/admission_control/config.h"
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

// Ensure the filter ingest throws an exception if it is passed a config with a default value of 0
// for sr_threshold If exception was not thrown, a default value of 0 for sr_threshold induces a
// divide by zero error
TEST_F(AdmissionControlConfigTest, ZeroSuccessRateThreshold) {
  AdmissionControlFilterFactory admission_control_filter_factory;
  const std::string yaml = R"EOF(
enabled:
  default_value: false
  runtime_key: "foo.enabled"
sampling_window: 1337s
sr_threshold:
  default_value:
    value: 0
  runtime_key: "foo.sr_threshold"
aggression:
  default_value: 4.2
  runtime_key: "foo.aggression"
success_criteria:
  http_criteria:
  grpc_criteria:
)EOF";

  AdmissionControlProto proto;
  TestUtility::loadFromYamlAndValidate(yaml, proto);
  NiceMock<Server::Configuration::MockFactoryContext> factory_context;
  EXPECT_THROW_WITH_MESSAGE(admission_control_filter_factory.createFilterFactoryFromProtoTyped(
                                proto, "whatever", factory_context),
                            EnvoyException, "Success Rate Threshold cannot be zero percent");
}

// Verify the configuration when all fields are set.
TEST_F(AdmissionControlConfigTest, BasicTestAllConfigured) {
  const std::string yaml = R"EOF(
enabled:
  default_value: false
  runtime_key: "foo.enabled"
sampling_window: 1337s
sr_threshold:
  default_value:
    value: 92
  runtime_key: "foo.sr_threshold"
aggression:
  default_value: 4.2
  runtime_key: "foo.aggression"
success_criteria:
  http_criteria:
  grpc_criteria:
)EOF";

  auto config = makeConfig(yaml);

  EXPECT_FALSE(config->filterEnabled());
  EXPECT_EQ(4.2, config->aggression());
  EXPECT_EQ(0.92, config->successRateThreshold());
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
  EXPECT_EQ(1.0, config->aggression());
  EXPECT_EQ(0.95, config->successRateThreshold());
}

// Ensure runtime fields are honored.
TEST_F(AdmissionControlConfigTest, VerifyRuntime) {
  const std::string yaml = R"EOF(
enabled:
  default_value: false
  runtime_key: "foo.enabled"
sampling_window: 1337s
sr_threshold:
  default_value:
    value: 92
  runtime_key: "foo.sr_threshold"
aggression:
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
  EXPECT_CALL(runtime_.snapshot_, getDouble("foo.sr_threshold", 92)).WillOnce(Return(24.0));
  EXPECT_EQ(0.24, config->successRateThreshold());

  // Verify bogus runtime thresholds revert to the default value.
  EXPECT_CALL(runtime_.snapshot_, getDouble("foo.sr_threshold", 92)).WillOnce(Return(250.0));
  EXPECT_EQ(0.92, config->successRateThreshold());
  EXPECT_CALL(runtime_.snapshot_, getDouble("foo.sr_threshold", 92)).WillOnce(Return(-1.0));
  EXPECT_EQ(0.92, config->successRateThreshold());
}

} // namespace
} // namespace AdmissionControl
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
