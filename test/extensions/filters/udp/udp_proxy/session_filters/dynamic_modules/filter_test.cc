#include "source/common/network/utility.h"
#include "source/common/stats/isolated_store_impl.h"
#include "source/extensions/dynamic_modules/abi/abi.h"
#include "source/extensions/filters/udp/udp_proxy/session_filters/dynamic_modules/filter.h"

#include "test/extensions/dynamic_modules/util.h"
#include "test/extensions/filters/udp/udp_proxy/mocks.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Extensions {
namespace UdpFilters {
namespace UdpProxy {
namespace SessionFilters {
namespace DynamicModules {

using ProtoConfig = envoy::extensions::filters::udp::udp_proxy::session::dynamic_modules::v3::
    DynamicModuleSessionFilter;

class DynamicModuleUdpSessionFilterTest : public testing::Test {
public:
  void SetUp() override {
    auto dynamic_module = Extensions::DynamicModules::newDynamicModule(
        Extensions::DynamicModules::testSharedObjectPath("udp_session_no_op", "c"), false);
    ASSERT_TRUE(dynamic_module.ok()) << dynamic_module.status().message();

    ProtoConfig proto_config;
    proto_config.set_filter_name("test_filter");
    proto_config.mutable_filter_config()->set_value("some_config");

    filter_config_ = std::make_shared<DynamicModuleUdpSessionFilterConfig>(
        proto_config, std::move(dynamic_module.value()), *stats_.rootScope(), time_system_);
    // Re-open stat creation so tests can call `define_*` from the test thread.
    filter_config_->stat_creation_frozen_ = false;
  }

  Network::UdpRecvData makeData(const std::string& payload) {
    Network::UdpRecvData data;
    data.buffer_ = std::make_unique<Buffer::OwnedImpl>(payload);
    data.addresses_.peer_ = Network::Utility::parseInternetAddressAndPortNoThrow("1.2.3.4:1234");
    data.addresses_.local_ = Network::Utility::parseInternetAddressAndPortNoThrow("5.6.7.8:5678");
    return data;
  }

  Event::SimulatedTimeSystem time_system_;
  Stats::IsolatedStoreImpl stats_;
  DynamicModuleUdpSessionFilterConfigSharedPtr filter_config_;
};

TEST_F(DynamicModuleUdpSessionFilterTest, BasicLifecycle) {
  auto filter = std::make_shared<DynamicModuleUdpSessionFilter>(filter_config_);
  NiceMock<MockReadFilterCallbacks> read_callbacks;
  NiceMock<MockWriteFilterCallbacks> write_callbacks;
  filter->initializeReadFilterCallbacks(read_callbacks);
  filter->initializeWriteFilterCallbacks(write_callbacks);

  EXPECT_EQ(Network::UdpSessionReadFilterStatus::Continue, filter->onNewSession());

  auto read_data = makeData("hello");
  EXPECT_EQ(Network::UdpSessionReadFilterStatus::Continue, filter->onData(read_data));
  EXPECT_EQ(nullptr, filter->currentData());

  auto write_data = makeData("world");
  EXPECT_EQ(Network::UdpSessionWriteFilterStatus::Continue, filter->onWrite(write_data));
  EXPECT_EQ(nullptr, filter->currentData());

  // onSessionComplete is idempotent and forwards to the (optional) module hook.
  filter->onSessionComplete();
  filter->onSessionComplete();
}

TEST_F(DynamicModuleUdpSessionFilterTest, ReadStopIteration) {
  auto filter = std::make_shared<DynamicModuleUdpSessionFilter>(filter_config_);
  NiceMock<MockReadFilterCallbacks> read_callbacks;
  filter->initializeReadFilterCallbacks(read_callbacks);

  // Swap the on_data hook to return StopIteration.
  filter_config_->on_filter_on_data_ =
      +[](envoy_dynamic_module_type_udp_session_filter_envoy_ptr,
          envoy_dynamic_module_type_udp_session_filter_module_ptr)
      -> envoy_dynamic_module_type_on_udp_session_read_filter_status {
    return envoy_dynamic_module_type_on_udp_session_read_filter_status_StopIteration;
  };

  auto data = makeData("hello");
  EXPECT_EQ(Network::UdpSessionReadFilterStatus::StopIteration, filter->onData(data));
}

TEST_F(DynamicModuleUdpSessionFilterTest, WriteStopIteration) {
  auto filter = std::make_shared<DynamicModuleUdpSessionFilter>(filter_config_);
  NiceMock<MockWriteFilterCallbacks> write_callbacks;
  filter->initializeWriteFilterCallbacks(write_callbacks);

  filter_config_->on_filter_on_write_ =
      +[](envoy_dynamic_module_type_udp_session_filter_envoy_ptr,
          envoy_dynamic_module_type_udp_session_filter_module_ptr)
      -> envoy_dynamic_module_type_on_udp_session_write_filter_status {
    return envoy_dynamic_module_type_on_udp_session_write_filter_status_StopIteration;
  };

  auto data = makeData("world");
  EXPECT_EQ(Network::UdpSessionWriteFilterStatus::StopIteration, filter->onWrite(data));
}

TEST_F(DynamicModuleUdpSessionFilterTest, ConfigMissingSymbols) {
  auto dynamic_module = Extensions::DynamicModules::newDynamicModule(
      Extensions::DynamicModules::testSharedObjectPath("no_op", "c"), false);
  ASSERT_TRUE(dynamic_module.ok()) << dynamic_module.status().message();

  ProtoConfig proto_config;
  proto_config.set_filter_name("test_filter");

  EXPECT_THROW_WITH_MESSAGE(
      std::make_shared<DynamicModuleUdpSessionFilterConfig>(
          proto_config, std::move(dynamic_module.value()), *stats_.rootScope(), time_system_),
      EnvoyException,
      "Dynamic module does not support UDP session filters: Failed to resolve symbol "
      "envoy_dynamic_module_on_udp_session_filter_config_new");
}

TEST_F(DynamicModuleUdpSessionFilterTest, NullInModuleFilter) {
  // Swap on_filter_new_ to return null, simulating a module that rejects the session.
  filter_config_->on_filter_new_ =
      +[](envoy_dynamic_module_type_udp_session_filter_config_module_ptr,
          envoy_dynamic_module_type_udp_session_filter_envoy_ptr)
      -> envoy_dynamic_module_type_udp_session_filter_module_ptr { return nullptr; };

  auto filter = std::make_shared<DynamicModuleUdpSessionFilter>(filter_config_);
  EXPECT_EQ(Network::UdpSessionReadFilterStatus::Continue, filter->onNewSession());
  auto data = makeData("hello");
  EXPECT_EQ(Network::UdpSessionReadFilterStatus::Continue, filter->onData(data));
  EXPECT_EQ(Network::UdpSessionWriteFilterStatus::Continue, filter->onWrite(data));
  filter->onSessionComplete();
}

} // namespace DynamicModules
} // namespace SessionFilters
} // namespace UdpProxy
} // namespace UdpFilters
} // namespace Extensions
} // namespace Envoy
