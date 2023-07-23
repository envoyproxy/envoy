#include "envoy/extensions/common/dynamic_forward_proxy/v3/dns_cache.pb.h"

#include "source/common/config/utility.h"
#include "source/extensions/common/dynamic_forward_proxy/dns_cache_impl.h"
#include "source/extensions/common/dynamic_forward_proxy/dns_cache_resource_manager.h"

#include "test/extensions/common/dynamic_forward_proxy/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/stats/mocks.h"
#include "test/test_common/utility.h"

using testing::_;
using testing::NiceMock;

namespace Envoy {
namespace Extensions {
namespace Common {
namespace DynamicForwardProxy {
namespace {

class DnsCacheResourceManagerTest : public testing::Test {
public:
  DnsCacheResourceManagerTest() { ON_CALL(store_, gauge(_, _)).WillByDefault(ReturnRef(gauge_)); }

  void setupResourceManager(std::string& config_yaml) {
    envoy::extensions::common::dynamic_forward_proxy::v3::DnsCacheCircuitBreakers cb_config;
    TestUtility::loadFromYaml(config_yaml, cb_config);

    resource_manager_ = std::make_unique<DnsCacheResourceManagerImpl>(*store_.rootScope(), loader_,
                                                                      "dummy", cb_config);
  }

  void cleanup() {
    auto& pending_requests = resource_manager_->pendingRequests();
    while (pending_requests.count() != 0) {
      pending_requests.dec();
    }
  }

  std::unique_ptr<DnsCacheResourceManager> resource_manager_;
  NiceMock<Stats::MockStore> store_;
  NiceMock<Stats::MockGauge> gauge_;
  NiceMock<Runtime::MockLoader> loader_;
};

TEST_F(DnsCacheResourceManagerTest, CheckDnsResource) {
  std::string config_yaml = R"EOF(
    max_pending_requests: 3
  )EOF";
  setupResourceManager(config_yaml);

  auto& pending_requests = resource_manager_->pendingRequests();
  EXPECT_EQ(3, pending_requests.max());
  EXPECT_EQ(0, pending_requests.count());
  EXPECT_TRUE(pending_requests.canCreate());

  pending_requests.inc();
  EXPECT_EQ(1, pending_requests.count());
  EXPECT_TRUE(pending_requests.canCreate());

  pending_requests.inc();
  pending_requests.inc();
  EXPECT_EQ(3, pending_requests.count());
  EXPECT_FALSE(pending_requests.canCreate());

  pending_requests.dec();
  EXPECT_EQ(2, pending_requests.count());
  EXPECT_TRUE(pending_requests.canCreate());

  EXPECT_EQ(0, resource_manager_->stats().rq_pending_open_.value());
  EXPECT_EQ(0, resource_manager_->stats().rq_pending_remaining_.value());

  cleanup();
}
} // namespace
} // namespace DynamicForwardProxy
} // namespace Common
} // namespace Extensions
} // namespace Envoy
