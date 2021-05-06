#include <memory>
#include <string>

#include "extensions/filters/network/redis_proxy/feature/feature.h"

#include "test/mocks/runtime/mocks.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/utility.h"

using testing::Eq;
using testing::Matcher;
using testing::NiceMock;
using testing::Return;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace RedisProxy {
namespace Feature {

TEST(FeatureConfigTest, Constructor) {
  Stats::TestUtil::TestStore store;
  Api::ApiPtr api = Api::createApiForTest();
  Event::DispatcherPtr dispatcher(api->allocateDispatcher("test_thread"));
  envoy::extensions::filters::network::redis_proxy::v3::RedisProxy_FeatureConfig feature_config;
  feature_config.mutable_hotkey();
  FeatureConfigSharedPtr feature =
      std::make_shared<FeatureConfig>(feature_config, *dispatcher, "", store);

  EXPECT_EQ(true, bool(feature->hotkeyCollector()));
}

} // namespace Feature
} // namespace RedisProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
