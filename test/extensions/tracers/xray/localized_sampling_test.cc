#include "extensions/tracers/xray/localized_sampling.h"

#include "test/mocks/runtime/mocks.h"
#include "test/test_common/simulated_time_system.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::NiceMock;
using testing::Return;

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace XRay {

namespace {

class LocalizedSamplingStrategyTest : public ::testing::Test {
protected:
  Event::SimulatedTimeSystem time_system_;
};

TEST_F(LocalizedSamplingStrategyTest, EmptyRules) {
  NiceMock<Runtime::MockRandomGenerator> random_generator;
  LocalizedSamplingStrategy strategy{"", random_generator, time_system_};
  ASSERT_TRUE(strategy.usingDefaultManifest());
}

TEST_F(LocalizedSamplingStrategyTest, BadJson) {
  NiceMock<Runtime::MockRandomGenerator> random_generator;
  LocalizedSamplingStrategy strategy{"{{}", random_generator, time_system_};
  ASSERT_TRUE(strategy.usingDefaultManifest());
}

TEST_F(LocalizedSamplingStrategyTest, ValidCustomRules) {
  NiceMock<Runtime::MockRandomGenerator> random_generator;
  constexpr auto rules_json = R"EOF(
{
  "version": 2,
  "rules": [
    {
      "description": "X-Ray rule",
      "host": "*",
      "http_method": "*",
      "url_path": "/api/move/*",
      "fixed_target": 0,
      "rate": 0.05
    }
  ],
  "default": {
    "fixed_target": 1,
    "rate": 0.1
  }
}
  )EOF";
  LocalizedSamplingStrategy strategy{rules_json, random_generator, time_system_};
  ASSERT_FALSE(strategy.usingDefaultManifest());
}

TEST_F(LocalizedSamplingStrategyTest, InvalidRate) {
  NiceMock<Runtime::MockRandomGenerator> random_generator;
  constexpr auto rules_json = R"EOF(
{
  "version": 2,
  "rules": [
    {
      "description": "X-Ray rule",
      "host": "*",
      "http_method": "*",
      "url_path": "/api/move/*",
      "fixed_target": 0,
      "rate": 1.5
    }
  ],
  "default": {
    "fixed_target": 1,
    "rate": 0
  }
}
  )EOF";
  LocalizedSamplingStrategy strategy{rules_json, random_generator, time_system_};
  ASSERT_TRUE(strategy.usingDefaultManifest());
}

TEST_F(LocalizedSamplingStrategyTest, InvalidFixedTarget) {
  NiceMock<Runtime::MockRandomGenerator> random_generator;
  constexpr auto rules_json = R"EOF(
{
  "version": 2,
  "rules": [
    {
      "description": "X-Ray rule",
      "host": "*",
      "http_method": "*",
      "url_path": "/api/move/*",
      "fixed_target": 4.2,
      "rate": 0.1
    }
  ],
  "default": {
    "fixed_target": 1,
    "rate": 0.1
  }
}
  )EOF";
  LocalizedSamplingStrategy strategy{rules_json, random_generator, time_system_};
  ASSERT_TRUE(strategy.usingDefaultManifest());
}

TEST_F(LocalizedSamplingStrategyTest, DefaultRuleMissingRate) {
  NiceMock<Runtime::MockRandomGenerator> random_generator;
  constexpr auto rules_json = R"EOF(
{
  "version": 2,
  "rules": [
    {
      "description": "X-Ray rule",
      "host": "*",
      "http_method": "*",
      "url_path": "/api/move/*",
      "fixed_target": 0,
      "rate": 0.05
    }
  ],
  "default": {
    "fixed_target": 1
  }
}
  )EOF";
  LocalizedSamplingStrategy strategy{rules_json, random_generator, time_system_};
  ASSERT_TRUE(strategy.usingDefaultManifest());
}

TEST_F(LocalizedSamplingStrategyTest, DefaultRuleMissingFixedTarget) {
  NiceMock<Runtime::MockRandomGenerator> random_generator;
  constexpr auto rules_json = R"EOF(
{
  "version": 2,
  "rules": [
    {
      "description": "X-Ray rule",
      "host": "*",
      "http_method": "*",
      "url_path": "/api/move/*",
      "fixed_target": 0,
      "rate": 0.05
    }
  ],
  "default": {
    "rate": 0.5
  }
}
  )EOF";
  LocalizedSamplingStrategy strategy{rules_json, random_generator, time_system_};
  ASSERT_TRUE(strategy.usingDefaultManifest());
}

TEST_F(LocalizedSamplingStrategyTest, WrongVersion) {
  NiceMock<Runtime::MockRandomGenerator> random_generator;
  constexpr auto wrong_version = R"EOF(
{
  "version": 1,
  "rules": [
    {
      "description": "X-Ray rule",
      "host": "*",
      "http_method": "*",
      "url_path": "/api/move/*",
      "fixed_target": 0,
      "rate": 0.05
    }
  ],
  "default": {
    "fixed_target": 1,
    "rate": 0.5
  }
}
  )EOF";
  LocalizedSamplingStrategy strategy{wrong_version, random_generator, time_system_};
  ASSERT_TRUE(strategy.usingDefaultManifest());
}

TEST_F(LocalizedSamplingStrategyTest, MissingVersion) {
  NiceMock<Runtime::MockRandomGenerator> random_generator;
  constexpr auto missing_version = R"EOF(
{
  "rules": [
    {
      "description": "X-Ray rule",
      "host": "*",
      "http_method": "*",
      "url_path": "/api/move/*",
      "fixed_target": 0,
      "rate": 0.05
    }
  ],
  "default": {
    "fixed_target": 1,
    "rate": 0.5
  }
}
  )EOF";
  LocalizedSamplingStrategy strategy{missing_version, random_generator, time_system_};
  ASSERT_TRUE(strategy.usingDefaultManifest());
}

TEST_F(LocalizedSamplingStrategyTest, MissingDefaultRules) {
  NiceMock<Runtime::MockRandomGenerator> random_generator;
  constexpr auto rules_json = R"EOF(
{
  "version": 2,
  "rules": [
    {
      "description": "X-Ray rule",
      "host": "*",
      "http_method": "*",
      "url_path": "/api/move/*",
      "fixed_target": 0,
      "rate": 0.05
    }
  ]
}
  )EOF";
  LocalizedSamplingStrategy strategy{rules_json, random_generator, time_system_};
  ASSERT_TRUE(strategy.usingDefaultManifest());
}

TEST_F(LocalizedSamplingStrategyTest, CustomRuleHostIsNotString) {
  NiceMock<Runtime::MockRandomGenerator> random_generator;
  constexpr auto rules_json = R"EOF(
{
  "version": 2,
  "rules": [
    {
      "description": "X-Ray rule",
      "host": null,
      "http_method": "*",
      "url_path": "/api/move/*",
      "fixed_target": 0,
      "rate": 0.05
    }
  ],
  "default": {
    "fixed_target": 1,
    "rate": 0.1
  }
}
  )EOF";
  LocalizedSamplingStrategy strategy{rules_json, random_generator, time_system_};
  ASSERT_TRUE(strategy.usingDefaultManifest());
}

TEST_F(LocalizedSamplingStrategyTest, CustomRuleHttpMethodIsNotString) {
  NiceMock<Runtime::MockRandomGenerator> random_generator;
  constexpr auto rules_json = R"EOF(
{
  "version": 2,
  "rules": [
    {
      "description": "X-Ray rule",
      "host": "*",
      "http_method": 42,
      "url_path": "/api/move/*",
      "fixed_target": 0,
      "rate": 0.05
    }
  ],
  "default": {
    "fixed_target": 1,
    "rate": 0.1
  }
}
  )EOF";
  LocalizedSamplingStrategy strategy{rules_json, random_generator, time_system_};
  ASSERT_TRUE(strategy.usingDefaultManifest());
}

TEST_F(LocalizedSamplingStrategyTest, CustomRuleUrlPathIsNotString) {
  NiceMock<Runtime::MockRandomGenerator> random_generator;
  constexpr auto rules_json = R"EOF(
{
  "version": 2,
  "rules": [
    {
      "description": "X-Ray rule",
      "host": "*",
      "http_method": "*",
      "url_path": { "another": "object" },
      "fixed_target": 0,
      "rate": 0.05
    }
  ],
  "default": {
    "fixed_target": 1,
    "rate": 0.1
  }
}
  )EOF";
  LocalizedSamplingStrategy strategy{rules_json, random_generator, time_system_};
  ASSERT_TRUE(strategy.usingDefaultManifest());
}

TEST_F(LocalizedSamplingStrategyTest, CustomRuleMissingFixedTarget) {
  NiceMock<Runtime::MockRandomGenerator> random_generator;
  constexpr auto rules_json = R"EOF(
{
  "version": 2,
  "rules": [
    {
      "description": "X-Ray rule",
      "host": "*",
      "http_method": "*",
      "url_path": "/api/move/*",
      "rate": 0.05
    }
  ],
  "default": {
    "fixed_target": 1,
    "rate": 0.1
  }
}
  )EOF";
  LocalizedSamplingStrategy strategy{rules_json, random_generator, time_system_};
  ASSERT_TRUE(strategy.usingDefaultManifest());
}

TEST_F(LocalizedSamplingStrategyTest, CustomRuleMissingRate) {
  NiceMock<Runtime::MockRandomGenerator> random_generator;
  constexpr auto rules_json = R"EOF(
{
  "version": 2,
  "rules": [
    {
      "description": "X-Ray rule",
      "host": "*",
      "http_method": "*",
      "url_path": "/api/move/*",
      "fixed_target": 0
    }
  ],
  "default": {
    "fixed_target": 1,
    "rate": 0.1
  }
}
  )EOF";
  LocalizedSamplingStrategy strategy{rules_json, random_generator, time_system_};
  ASSERT_TRUE(strategy.usingDefaultManifest());
}

TEST_F(LocalizedSamplingStrategyTest, CustomRuleArrayElementWithWrongType) {
  NiceMock<Runtime::MockRandomGenerator> random_generator;
  constexpr auto rules_json = R"EOF(
{
  "version": 2,
  "rules": [
    {
      "description": "X-Ray rule",
      "host": "*",
      "http_method": "*",
      "url_path": "/api/move/*",
      "fixed_target": 0
    },
    "should be an array, not string"
  ],
  "default": {
    "fixed_target": 1,
    "rate": 0.1
  }
}
  )EOF";
  LocalizedSamplingStrategy strategy{rules_json, random_generator, time_system_};
  ASSERT_TRUE(strategy.usingDefaultManifest());
}

TEST_F(LocalizedSamplingStrategyTest, CustomRuleNegativeFixedRate) {
  NiceMock<Runtime::MockRandomGenerator> random_generator;
  constexpr auto rules_json = R"EOF(
{
  "version": 2,
  "rules": [
    {
      "description": "X-Ray rule",
      "host": "*",
      "http_method": "*",
      "url_path": "/api/move/*",
      "fixed_target": -1,
      "rate": 0.05
    }
  ],
  "default": {
    "fixed_target": 1,
    "rate": 0.1
  }
}
  )EOF";
  LocalizedSamplingStrategy strategy{rules_json, random_generator, time_system_};
  ASSERT_TRUE(strategy.usingDefaultManifest());
}

TEST_F(LocalizedSamplingStrategyTest, CustomRuleNegativeRate) {
  NiceMock<Runtime::MockRandomGenerator> random_generator;
  constexpr auto rules_json = R"EOF(
{
  "version": 2,
  "rules": [
    {
      "description": "X-Ray rule",
      "host": "*",
      "http_method": "*",
      "url_path": "/api/move/*",
      "fixed_target": 0,
      "rate": 0.05
    }
  ],
  "default": {
    "fixed_target": 1,
    "rate": -0.1
  }
}
  )EOF";
  LocalizedSamplingStrategy strategy{rules_json, random_generator, time_system_};
  ASSERT_TRUE(strategy.usingDefaultManifest());
}

TEST_F(LocalizedSamplingStrategyTest, TraceOnlyFromReservoir) {
  NiceMock<Runtime::MockRandomGenerator> rng;
  EXPECT_CALL(rng, random()).WillRepeatedly(Return(90));
  constexpr auto rules_json = R"EOF(
{
  "version": 2,
  "rules": [
    {
      "description": "X-Ray rule",
      "host": "*",
      "http_method": "*",
      "url_path": "*",
      "fixed_target": 1,
      "rate": 0.5
    }
  ],
  "default": {
    "fixed_target": 1,
    "rate": 0.5
  }
}
  )EOF";

  LocalizedSamplingStrategy strategy{rules_json, rng, time_system_};
  ASSERT_FALSE(strategy.usingDefaultManifest());

  SamplingRequest req;
  ASSERT_TRUE(strategy.shouldTrace(req)); // first one should be traced
  int i = 10;
  while (i-- > 0) {
    ASSERT_FALSE(strategy.shouldTrace(req));
  }
}

TEST_F(LocalizedSamplingStrategyTest, TraceFromReservoirAndByRate) {
  NiceMock<Runtime::MockRandomGenerator> rng;
  EXPECT_CALL(rng, random()).WillRepeatedly(Return(1));
  constexpr auto rules_json = R"EOF(
{
  "version": 2,
  "rules": [
    {
      "description": "X-Ray rule",
      "host": "*",
      "http_method": "*",
      "url_path": "*",
      "fixed_target": 1,
      "rate": 0.1
    }
  ],
  "default": {
    "fixed_target": 1,
    "rate": 0.5
  }
}
  )EOF";

  LocalizedSamplingStrategy strategy{rules_json, rng, time_system_};
  ASSERT_FALSE(strategy.usingDefaultManifest());

  SamplingRequest req;
  int i = 10;
  while (i-- > 0) {
    ASSERT_TRUE(strategy.shouldTrace(req));
  }
}

TEST_F(LocalizedSamplingStrategyTest, NoMatchingHost) {
  NiceMock<Runtime::MockRandomGenerator> rng;
  // this following value doesn't affect the test
  EXPECT_CALL(rng, random()).WillRepeatedly(Return(50 /*50 percent*/));
  // the following rules say:
  // "Sample 1 request/sec then 90% of the requests there after. Requests must have example.com as
  // its host"
  constexpr auto rules_json = R"EOF(
{
  "version": 2,
  "rules": [
    {
      "description": "Player moves.",
      "host": "example.com",
      "http_method": "*",
      "url_path": "*",
      "fixed_target": 1,
      "rate": 0.9
    }
  ],
  "default": {
    "fixed_target": 0,
    "rate": 0
  }
}
  )EOF";

  LocalizedSamplingStrategy strategy{rules_json, rng, time_system_};
  ASSERT_FALSE(strategy.usingDefaultManifest());

  SamplingRequest req;
  req.host_ = "amazon.com"; // host does not match, so default rules apply.
  int i = 10;
  while (i-- > 0) {
    ASSERT_FALSE(strategy.shouldTrace(req));
  }
}

TEST_F(LocalizedSamplingStrategyTest, NoMatchingHttpMethod) {
  NiceMock<Runtime::MockRandomGenerator> rng;
  // this following value doesn't affect the test
  EXPECT_CALL(rng, random()).WillRepeatedly(Return(50 /*50 percent*/));
  // the following rules say:
  // "Sample 1 request/sec then 90% of the requests there after. Requests must have example.com as
  // its host"
  constexpr auto rules_json = R"EOF(
{
  "version": 2,
  "rules": [
    {
      "description": "Player moves.",
      "host": "*",
      "http_method": "POST",
      "url_path": "*",
      "fixed_target": 1,
      "rate": 0.9
    }
  ],
  "default": {
    "fixed_target": 0,
    "rate": 0
  }
}
  )EOF";

  LocalizedSamplingStrategy strategy{rules_json, rng, time_system_};
  ASSERT_FALSE(strategy.usingDefaultManifest());

  SamplingRequest req;
  req.http_method_ = "GET"; // method does not match, so default rules apply.
  int i = 10;
  while (i-- > 0) {
    ASSERT_FALSE(strategy.shouldTrace(req));
  }
}

TEST_F(LocalizedSamplingStrategyTest, NoMatchingPath) {
  NiceMock<Runtime::MockRandomGenerator> rng;
  // this following value doesn't affect the test
  EXPECT_CALL(rng, random()).WillRepeatedly(Return(50 /*50 percent*/));
  // the following rules say:
  // "Sample 1 request/sec then 90% of the requests there after. Requests must have example.com as
  // its host"
  constexpr auto rules_json = R"EOF(
{
  "version": 2,
  "rules": [
    {
      "description": "X-Ray rule",
      "host": "*",
      "http_method": "*",
      "url_path": "/available/*",
      "fixed_target": 1,
      "rate": 0.9
    }
  ],
  "default": {
    "fixed_target": 0,
    "rate": 0
  }
}
  )EOF";

  LocalizedSamplingStrategy strategy{rules_json, rng, time_system_};
  ASSERT_FALSE(strategy.usingDefaultManifest());

  SamplingRequest req;
  req.http_url_ = "/"; // method does not match, so default rules apply.
  int i = 10;
  while (i-- > 0) {
    ASSERT_FALSE(strategy.shouldTrace(req));
  }
}

} // namespace
} // namespace XRay
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
