#include <chrono>

#include "envoy/config/filter/http/adaptive_concurrency/v2alpha/adaptive_concurrency.pb.h"
#include "envoy/config/filter/http/adaptive_concurrency/v2alpha/adaptive_concurrency.pb.validate.h"

#include "common/stats/isolated_store_impl.h"

#include "extensions/filters/http/adaptive_concurrency/adaptive_concurrency_filter.h"
#include "extensions/filters/http/adaptive_concurrency/concurrency_controller/concurrency_controller.h"
#include "extensions/filters/http/adaptive_concurrency/concurrency_controller/gradient_controller.h"

#include "test/mocks/event/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::NiceMock;
using testing::Return;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AdaptiveConcurrency {
namespace ConcurrencyController {
namespace {

class GradientControllerConfigTest : public testing::Test {
public:
  GradientControllerConfigTest() {}
};

class GradientControllerTest : public testing::Test {
public:
  GradientControllerTest() {}

  NiceMock<Event::MockDispatcher> dispatcher_;
  Event::SimulatedTimeSystem time_system_;
  Stats::IsolatedStoreImpl stats_;
  NiceMock<Runtime::MockLoader> runtime_;
};

TEST_F(GradientControllerConfigTest, BasicTest) {
  const std::string yaml = R"EOF(
sample_aggregate_percentile: 0.42
concurrency_limit_params:
  max_gradient: 2.1
  max_concurrency_limit: 1337
  concurrency_update_interval: 
    nanos: 123000000
min_rtt_calc_params:
  interval:
    seconds: 31
  request_count: 52
)EOF";

  envoy::config::filter::http::adaptive_concurrency::v2alpha::GradientControllerConfig proto =
      TestUtility::parseYaml<
          envoy::config::filter::http::adaptive_concurrency::v2alpha::GradientControllerConfig>(
          yaml);
  GradientControllerConfig config(proto);

  EXPECT_EQ(config.min_rtt_calc_interval(), std::chrono::seconds(31));
  EXPECT_EQ(config.sample_rtt_calc_interval(), std::chrono::milliseconds(123));
  EXPECT_EQ(config.max_concurrency_limit(), 1337);
  EXPECT_EQ(config.min_rtt_aggregate_request_count(), 52);
  EXPECT_EQ(config.max_gradient(), 2.1);
  EXPECT_EQ(config.sample_aggregate_percentile(), 0.42);
}

} // namespace
} // namespace ConcurrencyController
} // namespace AdaptiveConcurrency
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
