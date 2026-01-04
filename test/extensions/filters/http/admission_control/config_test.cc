#include <chrono>

#include "envoy/extensions/filters/http/admission_control/v3/admission_control.pb.h"
#include "envoy/extensions/filters/http/admission_control/v3/admission_control.pb.validate.h"

#include "source/common/stats/isolated_store_impl.h"
#include "source/extensions/filters/http/admission_control/admission_control.h"
#include "source/extensions/filters/http/admission_control/config.h"
#include "source/extensions/filters/http/admission_control/evaluators/success_criteria_evaluator.h"

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
    auto tls = ThreadLocal::TypedSlot<ThreadLocalControllerImpl>::makeUnique(
        context_.server_factory_context_.threadLocal());
    auto evaluator_or = SuccessCriteriaEvaluator::create(proto.success_criteria());
    EXPECT_TRUE(evaluator_or.ok());
    std::shared_ptr<ResponseEvaluator> evaluator = std::move(evaluator_or.value());

    auto global = std::make_shared<AdmissionControlRuleConfig>(
        std::move(tls), std::move(evaluator), std::vector<Http::HeaderUtility::HeaderDataPtr>{},
        proto.has_aggression() ? std::make_unique<Runtime::Double>(proto.aggression(), runtime_)
                               : nullptr,
        proto.has_sr_threshold()
            ? std::make_unique<Runtime::Percentage>(proto.sr_threshold(), runtime_)
            : nullptr,
        proto.has_rps_threshold()
            ? std::make_unique<Runtime::UInt32>(proto.rps_threshold(), runtime_)
            : nullptr,
        proto.has_max_rejection_probability()
            ? std::make_unique<Runtime::Percentage>(proto.max_rejection_probability(), runtime_)
            : nullptr);
    std::vector<AdmissionControlRuleConfigSharedPtr> rules;
    for (const auto& rule_proto : proto.rules()) {
      auto rule_tls = ThreadLocal::TypedSlot<ThreadLocalControllerImpl>::makeUnique(
          context_.server_factory_context_.threadLocal());
      auto rule_evaluator_or = SuccessCriteriaEvaluator::create(rule_proto.success_criteria());
      EXPECT_TRUE(rule_evaluator_or.ok());
      std::shared_ptr<ResponseEvaluator> rule_evaluator = std::move(rule_evaluator_or.value());
      auto rule_filter_headers = Http::HeaderUtility::buildHeaderDataVector(
          rule_proto.headers(), context_.server_factory_context_);
      rules.push_back(std::make_shared<AdmissionControlRuleConfig>(
          rule_proto, runtime_, std::move(rule_tls), std::move(rule_evaluator),
          std::move(rule_filter_headers)));
    }

    return std::make_shared<AdmissionControlFilterConfig>(proto, runtime_, random_, scope_,
                                                          std::move(global), std::move(rules));
  }

protected:
  NiceMock<Runtime::MockLoader> runtime_;
  NiceMock<Server::Configuration::MockFactoryContext> context_;
  AdmissionControlFilterFactory::DualInfo dual_info_{context_};
  Stats::IsolatedStoreImpl store_;
  Stats::Scope& scope_{*store_.rootScope()};
  NiceMock<Random::MockRandomGenerator> random_;
};

// Ensure the filter ingest throws an exception if it is passed a config with a default value of 0
// for sr_threshold If exception was not thrown, a default value of 0 for sr_threshold induces a
// divide by zero error.
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
  auto status_or = admission_control_filter_factory.createFilterFactoryFromProtoTyped(
      proto, "whatever", dual_info_, factory_context.serverFactoryContext());
  EXPECT_FALSE(status_or.ok());
  EXPECT_EQ("Success rate threshold cannot be less than 1.0%.", status_or.status().message());
}

TEST_F(AdmissionControlConfigTest, SmallSuccessRateThreshold) {
  AdmissionControlFilterFactory admission_control_filter_factory;
  const std::string yaml = R"EOF(
enabled:
  default_value: false
  runtime_key: "foo.enabled"
sampling_window: 1337s
sr_threshold:
  default_value:
    value: 1.22e-22
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
  auto status_or = admission_control_filter_factory.createFilterFactoryFromProtoTyped(
      proto, "whatever", dual_info_, factory_context.serverFactoryContext());
  EXPECT_FALSE(status_or.ok());
  EXPECT_EQ("Success rate threshold cannot be less than 1.0%.", status_or.status().message());
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
rps_threshold:
  default_value: 5
  runtime_key: "foo.rps_threshold"
max_rejection_probability:
  default_value:
    value: 70.0
  runtime_key: "foo.max_rejection_probability"
success_criteria:
  http_criteria:
  grpc_criteria:
)EOF";

  auto filter_config = makeConfig(yaml);
  Http::TestRequestHeaderMapImpl headers{};
  auto config = filter_config->findMatchingRule(headers);

  EXPECT_FALSE(filter_config->filterEnabled());
  EXPECT_EQ(4.2, config->aggression());
  EXPECT_EQ(0.92, config->successRateThreshold());
  EXPECT_EQ(5, config->rpsThreshold());
  EXPECT_EQ(0.7, config->maxRejectionProbability());
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
  auto filter_config = makeConfig(yaml);
  Http::TestRequestHeaderMapImpl headers{};
  auto config = filter_config->findMatchingRule(headers);

  EXPECT_TRUE(filter_config->filterEnabled());
  EXPECT_EQ(1.0, config->aggression());
  EXPECT_EQ(0.95, config->successRateThreshold());
  EXPECT_EQ(0, config->rpsThreshold());
  EXPECT_EQ(0.8, config->maxRejectionProbability());
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
rps_threshold:
  default_value: 5
  runtime_key: "foo.rps_threshold"
max_rejection_probability:
  default_value:
    value: 70.0
  runtime_key: "foo.max_rejection_probability"
success_criteria:
  http_criteria:
  grpc_criteria:
)EOF";

  auto filter_config = makeConfig(yaml);
  Http::TestRequestHeaderMapImpl headers{};
  auto config = filter_config->findMatchingRule(headers);

  EXPECT_CALL(runtime_.snapshot_, getBoolean("foo.enabled", false)).WillOnce(Return(true));
  EXPECT_TRUE(filter_config->filterEnabled());
  EXPECT_CALL(runtime_.snapshot_, getDouble("foo.aggression", 4.2)).WillOnce(Return(1.3));
  EXPECT_EQ(1.3, config->aggression());
  EXPECT_CALL(runtime_.snapshot_, getDouble("foo.sr_threshold", 92)).WillOnce(Return(24.0));
  EXPECT_EQ(0.24, config->successRateThreshold());
  EXPECT_CALL(runtime_.snapshot_, getInteger("foo.rps_threshold", 5)).WillOnce(Return(12));
  EXPECT_EQ(12, config->rpsThreshold());
  EXPECT_CALL(runtime_.snapshot_, getDouble("foo.max_rejection_probability", 70.0))
      .WillOnce(Return(32.0));
  EXPECT_EQ(0.32, config->maxRejectionProbability());

  // Verify bogus runtime thresholds revert to the default value.
  EXPECT_CALL(runtime_.snapshot_, getDouble("foo.sr_threshold", 92)).WillOnce(Return(250.0));
  EXPECT_EQ(0.92, config->successRateThreshold());
  EXPECT_CALL(runtime_.snapshot_, getDouble("foo.sr_threshold", 92)).WillOnce(Return(-1.0));
  EXPECT_EQ(0.92, config->successRateThreshold());
  EXPECT_CALL(runtime_.snapshot_, getInteger("foo.rps_threshold", 5)).WillOnce(Return(99ull << 40));
  EXPECT_EQ(5, config->rpsThreshold());
  EXPECT_CALL(runtime_.snapshot_, getDouble("foo.max_rejection_probability", 70.0))
      .WillOnce(Return(-2.0));
  EXPECT_EQ(0.7, config->maxRejectionProbability());
  EXPECT_CALL(runtime_.snapshot_, getDouble("foo.max_rejection_probability", 70.0))
      .WillOnce(Return(300.0));
  EXPECT_EQ(0.7, config->maxRejectionProbability());
}

TEST_F(AdmissionControlConfigTest, BasicTestRuleConfig) {
  const std::string yaml = R"EOF(
enabled:
  default_value: false
  runtime_key: "global.enabled"
sampling_window: 100s
sr_threshold:
  default_value:
    value: 100
  runtime_key: "global.sr_threshold"
aggression:
  default_value: 1.1
  runtime_key: "global.aggression"
rps_threshold:
  default_value: 1
  runtime_key: "global.rps_threshold"
max_rejection_probability:
  default_value:
    value: 100.0
  runtime_key: "global.max_rejection_probability"
success_criteria:
  http_criteria:
  grpc_criteria:
rules:
  - headers:
    - name: "x-service"
      string_match:
        exact: "api"
    success_criteria:
      http_criteria:
      grpc_criteria:
    sr_threshold:
      default_value:
        value: 90.0
      runtime_key: "rule1.sr_threshold"
    rps_threshold:
      default_value: 2
      runtime_key: "rule2.rps_threshold"
    aggression:
      default_value: 1.2
      runtime_key: "rule1.aggression"
    max_rejection_probability:
      default_value:
        value: 90.0
      runtime_key: "rule1.max_rejection_probability"
  - headers:
    - name: "x-service"
      string_match:
        exact: "web"
    success_criteria:
      http_criteria:
      grpc_criteria:
    sr_threshold:
      default_value:
        value: 80.0
      runtime_key: "rule2.sr_threshold"
    rps_threshold:
      default_value: 2
      runtime_key: "rule2.rps_threshold"
    aggression:
      default_value: 2.2
      runtime_key: rule2.aggression
    max_rejection_probability:
      default_value:
        value: 80.0
      runtime_key: "rule2.max_rejection_probability"
    )EOF";

  auto filter_config = makeConfig(yaml);
  Http::TestRequestHeaderMapImpl rule1_headers{{"x-service", "api"}};
  auto config = filter_config->findMatchingRule(rule1_headers);
  EXPECT_EQ(1.2, config->aggression());
  EXPECT_EQ(2, config->rpsThreshold());
  EXPECT_EQ(0.90, config->successRateThreshold());
  EXPECT_EQ(0.90, config->maxRejectionProbability());

  Http::TestRequestHeaderMapImpl rule1_headers2{{"x-service", "web"}};
  config = filter_config->findMatchingRule(rule1_headers2);
  EXPECT_EQ(2.2, config->aggression());
  EXPECT_EQ(2, config->rpsThreshold());
  EXPECT_EQ(0.80, config->successRateThreshold());
  EXPECT_EQ(0.80, config->maxRejectionProbability());

  Http::TestRequestHeaderMapImpl global_headers{{"x-service", "other"}};
  config = filter_config->findMatchingRule(global_headers);
  EXPECT_EQ(1.1, config->aggression());
  EXPECT_EQ(1, config->rpsThreshold());
  EXPECT_EQ(1.0, config->successRateThreshold());
  EXPECT_EQ(1.0, config->maxRejectionProbability());
}

TEST_F(AdmissionControlConfigTest, BasicTestRuleProtoConfig) {
  AdmissionControlFilterFactory admission_control_filter_factory;
  const std::string yaml = R"EOF(
enabled:
  default_value: false
  runtime_key: "global.enabled"
sampling_window: 100s
sr_threshold:
  default_value:
    value: 100
  runtime_key: "global.sr_threshold"
aggression:
  default_value: 1.1
  runtime_key: "global.aggression"
rps_threshold:
  default_value: 1
  runtime_key: "global.rps_threshold"
max_rejection_probability:
  default_value:
    value: 100.0
  runtime_key: "global.max_rejection_probability"
success_criteria:
  http_criteria:
  grpc_criteria:
rules:
  - headers:
    - name: "x-service"
      string_match:
        exact: "api"
    success_criteria:
      http_criteria:
      grpc_criteria:
    sr_threshold:
      default_value:
        value: 90.0
      runtime_key: "rule1.sr_threshold"
    rps_threshold:
      default_value: 2
      runtime_key: "rule2.rps_threshold"
    aggression:
      default_value: 1.2
      runtime_key: "rule1.aggression"
    max_rejection_probability:
      default_value:
        value: 90.0
      runtime_key: "rule1.max_rejection_probability"
  - headers:
    - name: "x-service"
      string_match:
        exact: "web"
    success_criteria:
      http_criteria:
      grpc_criteria:
    sr_threshold:
      default_value:
        value: 80.0
      runtime_key: "rule2.sr_threshold"
    rps_threshold:
      default_value: 2
      runtime_key: "rule2.rps_threshold"
    aggression:
      default_value: 2.2
      runtime_key: rule2.aggression
    max_rejection_probability:
      default_value:
        value: 80.0
      runtime_key: "rule2.max_rejection_probability"
    )EOF";

  AdmissionControlProto proto;
  TestUtility::loadFromYamlAndValidate(yaml, proto);
  NiceMock<Server::Configuration::MockFactoryContext> factory_context;
  auto status_or = admission_control_filter_factory.createFilterFactoryFromProtoTyped(
      proto, "whatever", dual_info_, factory_context.serverFactoryContext());
  EXPECT_TRUE(status_or.ok());
}
} // namespace
} // namespace AdmissionControl
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
