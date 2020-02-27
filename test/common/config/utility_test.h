#include "envoy/common/exception.h"

#include "common/config/utility.h"

#include "test/mocks/runtime/mocks.h"

using testing::_;
using testing::NiceMock;

namespace Envoy {
namespace Config {

template <typename T> void testPrepareDnsRefreshStrategy() {
  NiceMock<Runtime::MockRandomGenerator> random;

  {
    // dns_failure_refresh_rate not set.
    T config;
    BackOffStrategyPtr strategy = Utility::prepareDnsRefreshStrategy<T>(config, 5000, random);
    EXPECT_NE(nullptr, dynamic_cast<FixedBackOffStrategy*>(strategy.get()));
  }

  {
    // dns_failure_refresh_rate set.
    T config;
    config.mutable_dns_failure_refresh_rate()->mutable_base_interval()->set_seconds(7);
    config.mutable_dns_failure_refresh_rate()->mutable_max_interval()->set_seconds(10);
    BackOffStrategyPtr strategy = Utility::prepareDnsRefreshStrategy<T>(config, 5000, random);
    EXPECT_NE(nullptr, dynamic_cast<JitteredBackOffStrategy*>(strategy.get()));
  }

  {
    // dns_failure_refresh_rate set with invalid max_interval.
    T config;
    config.mutable_dns_failure_refresh_rate()->mutable_base_interval()->set_seconds(7);
    config.mutable_dns_failure_refresh_rate()->mutable_max_interval()->set_seconds(2);
    EXPECT_THROW_WITH_REGEX(Utility::prepareDnsRefreshStrategy<T>(config, 5000, random),
                            EnvoyException,
                            "dns_failure_refresh_rate must have max_interval greater than "
                            "or equal to the base_interval");
  }
}

} // namespace Config
} // namespace Envoy
