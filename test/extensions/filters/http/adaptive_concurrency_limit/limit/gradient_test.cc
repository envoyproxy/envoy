#include <chrono>

#include "envoy/config/filter/http/adaptive_concurrency_limit/v2alpha/adaptive_concurrency_limit.pb.validate.h"
#include "envoy/registry/registry.h"

#include "extensions/filters/http/adaptive_concurrency_limit/common/common.h"
#include "extensions/filters/http/adaptive_concurrency_limit/limit/gradient.h"

#include "test/mocks/runtime/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::NiceMock;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AdaptiveConcurrencyLimit {
namespace Limit {

class GradientLimitTest : public testing::Test {
protected:
  GradientLimitTest() {}

  void setLimit(const std::string& yaml_config) {
    envoy::config::filter::http::adaptive_concurrency_limit::v2alpha::AdaptiveConcurrencyLimit::
        Limit config;
    MessageUtil::loadFromYaml(yaml_config, config);

    envoy::config::filter::http::adaptive_concurrency_limit::v2alpha::GradientLimitConfig
        limit_specific_config;
    MessageUtil::jsonConvert(config.limit_specific_config(), limit_specific_config);

    limit_.reset(
        new Gradient(config.common_config(), limit_specific_config, random_, "test_cluster"));
  }

  static std::chrono::nanoseconds millisecondsToNanoseconds(const uint32_t ms) {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::milliseconds(ms));
  }

  NiceMock<Runtime::MockRandomGenerator> random_;
  std::unique_ptr<Gradient> limit_;
};

TEST_F(GradientLimitTest, InitialLimit) {
  std::string yaml = R"EOF(
name: envoy.filters.http.adaptive_concurrency_limit.limit.gradient
common_config:
  initial_limit: 1
  min_limit: 1
  max_limit: 1000
limit_specific_config:
  smoothing: 0.5
  rtt_tolerance: 2.0
  probe_interval: 2
)EOF";

  setLimit(yaml);
  EXPECT_EQ(1, limit_->getLimit());
}

TEST_F(GradientLimitTest, IncreaseLimit) {
  std::string yaml = R"EOF(
name: envoy.filters.http.adaptive_concurrency_limit.limit.gradient
common_config:
  initial_limit: 1
  min_limit: 1
  max_limit: 1000
limit_specific_config:
  smoothing: 0.5
  rtt_tolerance: 2.0
  probe_interval: 10
)EOF";

  setLimit(yaml);
  Common::SampleWindow sample;
  sample.addSample(millisecondsToNanoseconds(10), 10);
  limit_->update(sample);
  EXPECT_EQ(2, limit_->getLimit());
  limit_->update(sample);
  EXPECT_EQ(3, limit_->getLimit());
  limit_->update(sample);
  EXPECT_EQ(4, limit_->getLimit());
}

TEST_F(GradientLimitTest, IncreaseLimitWithinRttTolerance) {
  std::string yaml = R"EOF(
name: envoy.filters.http.adaptive_concurrency_limit.limit.gradient
common_config:
  initial_limit: 100
  min_limit: 1
  max_limit: 1000
limit_specific_config:
  smoothing: 0.5
  rtt_tolerance: 2.0
  probe_interval: 20
)EOF";

  setLimit(yaml);
  Common::SampleWindow sample;
  sample.addSample(millisecondsToNanoseconds(10), 100);
  EXPECT_FALSE(sample.didDrop());
  limit_->update(sample);
  EXPECT_EQ(110, limit_->getLimit());
  limit_->update(sample);
  EXPECT_EQ(120, limit_->getLimit());
  limit_->update(sample);
  EXPECT_EQ(130, limit_->getLimit());
  Common::SampleWindow sample2;
  sample2.addSample(millisecondsToNanoseconds(15), 100);
  limit_->update(sample2);
  EXPECT_EQ(141, limit_->getLimit());
}

TEST_F(GradientLimitTest, DecreaseLimit) {
  std::string yaml = R"EOF(
name: envoy.filters.http.adaptive_concurrency_limit.limit.gradient
common_config:
  initial_limit: 100
  min_limit: 1
  max_limit: 1000
limit_specific_config:
  smoothing: 0.5
  rtt_tolerance: 2.0
  probe_interval: 20
)EOF";

  setLimit(yaml);
  Common::SampleWindow sample;
  sample.addSample(millisecondsToNanoseconds(10), 100);
  EXPECT_FALSE(sample.didDrop());
  limit_->update(sample);
  EXPECT_EQ(110, limit_->getLimit());
  limit_->update(sample);
  EXPECT_EQ(120, limit_->getLimit());
  limit_->update(sample);
  EXPECT_EQ(130, limit_->getLimit());
  Common::SampleWindow sample2;
  sample2.addSample(millisecondsToNanoseconds(1000), 70);
  limit_->update(sample2);
  EXPECT_EQ(103, limit_->getLimit());
}

TEST_F(GradientLimitTest, AggressiveDecreaseOnDrop) {
  std::string yaml = R"EOF(
name: envoy.filters.http.adaptive_concurrency_limit.limit.gradient
common_config:
  initial_limit: 100
  min_limit: 1
  max_limit: 1000
limit_specific_config:
  smoothing: 1.0
  rtt_tolerance: 2.0
  probe_interval: 20
)EOF";

  setLimit(yaml);
  EXPECT_EQ(100, limit_->getLimit());
  Common::SampleWindow sample;
  sample.addDroppedSample(100);
  limit_->update(sample);
  EXPECT_EQ(50, limit_->getLimit());
}

TEST_F(GradientLimitTest, DecreaseLimitIsDependentOnSmoothing) {
  std::string yaml_smoothing = R"EOF(
name: envoy.filters.http.adaptive_concurrency_limit.limit.gradient
common_config:
  initial_limit: 100
  min_limit: 1
  max_limit: 1000
limit_specific_config:
  smoothing: 0.5
  rtt_tolerance: 2.0
  probe_interval: 20
)EOF";

  setLimit(yaml_smoothing);
  EXPECT_EQ(100, limit_->getLimit());
  Common::SampleWindow sample;
  sample.addDroppedSample(100);
  limit_->update(sample);
  EXPECT_EQ(75, limit_->getLimit());

  std::string yaml = R"EOF(
name: envoy.filters.http.adaptive_concurrency_limit.limit.gradient
common_config:
  initial_limit: 100
  min_limit: 1
  max_limit: 1000
limit_specific_config:
  smoothing: 1.0
  rtt_tolerance: 2.0
  probe_interval: 20
)EOF";

  setLimit(yaml);
  EXPECT_EQ(100, limit_->getLimit());
  Common::SampleWindow sample2;
  sample2.addDroppedSample(100);
  limit_->update(sample2);
  EXPECT_EQ(50, limit_->getLimit());
}

TEST(GradientLimitFactoryTest, CreateLimit) {
  auto factory = Registry::FactoryRegistry<FactoryBase<
      envoy::config::filter::http::adaptive_concurrency_limit::v2alpha::GradientLimitConfig,
      Common::SampleWindow>>::
      getFactory("envoy.filters.http.adaptive_concurrency_limit.limit.gradient");
  EXPECT_NE(factory, nullptr);

  envoy::config::filter::http::adaptive_concurrency_limit::v2alpha::AdaptiveConcurrencyLimit::Limit
      config;

  MessageUtil::loadFromYaml(R"EOF(
name: envoy.filters.http.adaptive_concurrency_limit.limit.gradient
common_config:
  initial_limit: 1
  min_limit: 1
  max_limit: 1000
limit_specific_config:
  smoothing: 0.5
  rtt_tolerance: 2.0
  probe_interval: 2
)EOF",
                            config);

  NiceMock<Runtime::MockRandomGenerator> random_generator;
  auto limit = factory->createLimit(config, random_generator, "test_cluster");
  EXPECT_NE(limit, nullptr);
}

} // namespace Limit
} // namespace AdaptiveConcurrencyLimit
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy