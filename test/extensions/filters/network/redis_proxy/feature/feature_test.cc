#include <memory>
#include <string>

#include "extensions/filters/network/redis_proxy/feature/feature.h"

#include "test/mocks/runtime/mocks.h"
#include "test/test_common/utility.h"

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
  FeatureConfigSharedPtr feature_no_hotkey =
      std::make_shared<FeatureConfig>(feature_config, *dispatcher, "", store);
  HotKey::HotKeyCollectorSharedPtr hk_collector_no_hotkey = feature_no_hotkey->hotkeyCollector();
  EXPECT_EQ(false, bool(hk_collector_no_hotkey));

  feature_config.mutable_hotkey();
  FeatureConfigSharedPtr feature_with_hotkey =
      std::make_shared<FeatureConfig>(feature_config, *dispatcher, "", store);
  HotKey::HotKeyCollectorSharedPtr hk_collector_with_hotkey =
      feature_with_hotkey->hotkeyCollector();
  EXPECT_EQ(true, bool(hk_collector_with_hotkey));
}

} // namespace Feature
} // namespace RedisProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
