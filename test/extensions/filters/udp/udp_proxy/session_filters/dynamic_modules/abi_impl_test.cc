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

namespace {
envoy_dynamic_module_type_module_buffer toBuffer(const std::string& s) {
  return {const_cast<char*>(s.data()), s.size()};
}
} // namespace

class DynamicModuleUdpSessionAbiImplTest : public testing::Test {
public:
  void SetUp() override {
    auto dynamic_module = Extensions::DynamicModules::newDynamicModule(
        Extensions::DynamicModules::testSharedObjectPath("udp_session_no_op", "c"), false);
    ASSERT_TRUE(dynamic_module.ok()) << dynamic_module.status().message();

    ProtoConfig proto_config;
    proto_config.set_filter_name("test_filter");

    filter_config_ = std::make_shared<DynamicModuleUdpSessionFilterConfig>(
        proto_config, std::move(dynamic_module.value()), *stats_.rootScope(), time_system_);
    filter_config_->stat_creation_frozen_ = false;

    filter_ = std::make_shared<DynamicModuleUdpSessionFilter>(filter_config_);
    filter_->initializeReadFilterCallbacks(read_callbacks_);
    filter_->initializeWriteFilterCallbacks(write_callbacks_);
  }

  envoy_dynamic_module_type_udp_session_filter_envoy_ptr filterPtr() {
    return filter_->thisAsVoidPtr();
  }
  envoy_dynamic_module_type_udp_session_filter_config_envoy_ptr configPtr() {
    return static_cast<void*>(filter_config_.get());
  }

  Event::SimulatedTimeSystem time_system_;
  Stats::IsolatedStoreImpl stats_;
  NiceMock<MockReadFilterCallbacks> read_callbacks_;
  NiceMock<MockWriteFilterCallbacks> write_callbacks_;
  DynamicModuleUdpSessionFilterConfigSharedPtr filter_config_;
  DynamicModuleUdpSessionFilterSharedPtr filter_;
};

TEST_F(DynamicModuleUdpSessionAbiImplTest, DatagramDataAndAddresses) {
  Network::UdpRecvData data;
  data.buffer_ = std::make_unique<Buffer::OwnedImpl>("hello");
  data.addresses_.peer_ = Network::Utility::parseInternetAddressAndPortNoThrow("1.2.3.4:1234");
  data.addresses_.local_ = Network::Utility::parseInternetAddressAndPortNoThrow("5.6.7.8:5678");
  filter_->setCurrentDataForTest(&data);

  EXPECT_EQ(5, envoy_dynamic_module_callback_udp_session_filter_get_datagram_data_size(filterPtr()));
  EXPECT_EQ(1,
            envoy_dynamic_module_callback_udp_session_filter_get_datagram_data_chunks_size(
                filterPtr()));

  envoy_dynamic_module_type_envoy_buffer chunk;
  EXPECT_TRUE(
      envoy_dynamic_module_callback_udp_session_filter_get_datagram_data_chunks(filterPtr(), &chunk));
  EXPECT_EQ("hello", absl::string_view(chunk.ptr, chunk.length));

  // Replace the payload.
  std::string replacement = "modified";
  EXPECT_TRUE(envoy_dynamic_module_callback_udp_session_filter_set_datagram_data(
      filterPtr(), toBuffer(replacement)));
  EXPECT_EQ("modified", data.buffer_->toString());

  envoy_dynamic_module_type_envoy_buffer addr;
  uint32_t port = 0;
  EXPECT_TRUE(envoy_dynamic_module_callback_udp_session_filter_get_peer_address(filterPtr(), &addr,
                                                                                &port));
  EXPECT_EQ("1.2.3.4", absl::string_view(addr.ptr, addr.length));
  EXPECT_EQ(1234, port);

  EXPECT_TRUE(envoy_dynamic_module_callback_udp_session_filter_get_local_address(filterPtr(), &addr,
                                                                                 &port));
  EXPECT_EQ("5.6.7.8", absl::string_view(addr.ptr, addr.length));
  EXPECT_EQ(5678, port);
}

TEST_F(DynamicModuleUdpSessionAbiImplTest, DatagramCallbacksWithoutCurrentData) {
  // With no current datagram, getters return 0/false.
  EXPECT_EQ(0, envoy_dynamic_module_callback_udp_session_filter_get_datagram_data_size(filterPtr()));
  std::string data = "x";
  EXPECT_FALSE(envoy_dynamic_module_callback_udp_session_filter_set_datagram_data(filterPtr(),
                                                                                  toBuffer(data)));
  envoy_dynamic_module_type_envoy_buffer addr;
  uint32_t port = 0;
  EXPECT_FALSE(envoy_dynamic_module_callback_udp_session_filter_get_peer_address(filterPtr(), &addr,
                                                                                 &port));
}

TEST_F(DynamicModuleUdpSessionAbiImplTest, SessionIdAndContinue) {
  EXPECT_CALL(read_callbacks_, sessionId()).WillOnce(testing::Return(42));
  EXPECT_EQ(42, envoy_dynamic_module_callback_udp_session_filter_get_session_id(filterPtr()));

  EXPECT_CALL(read_callbacks_, continueFilterChain()).WillOnce(testing::Return(true));
  EXPECT_TRUE(envoy_dynamic_module_callback_udp_session_filter_continue_filter_chain(filterPtr()));
}

TEST_F(DynamicModuleUdpSessionAbiImplTest, InjectDatagrams) {
  std::string payload = "data";
  std::string peer = "9.9.9.9";
  std::string local = "8.8.8.8";

  EXPECT_CALL(read_callbacks_, injectDatagramToFilterChain(testing::_));
  EXPECT_TRUE(envoy_dynamic_module_callback_udp_session_filter_inject_read_datagram(
      filterPtr(), toBuffer(payload), toBuffer(peer), 9999, toBuffer(local), 8888));

  EXPECT_CALL(write_callbacks_, injectDatagramToFilterChain(testing::_));
  EXPECT_TRUE(envoy_dynamic_module_callback_udp_session_filter_inject_write_datagram(
      filterPtr(), toBuffer(payload), toBuffer(peer), 9999, toBuffer(local), 8888));

  // An unparseable address fails injection.
  std::string bad = "not-an-address";
  EXPECT_FALSE(envoy_dynamic_module_callback_udp_session_filter_inject_read_datagram(
      filterPtr(), toBuffer(payload), toBuffer(bad), 0, toBuffer(local), 8888));
}

TEST_F(DynamicModuleUdpSessionAbiImplTest, DynamicMetadataRoundTrip) {
  std::string ns = "my.namespace";
  std::string key = "k";
  std::string value = "v";
  envoy_dynamic_module_callback_udp_session_filter_set_dynamic_metadata_string(
      filterPtr(), toBuffer(ns), toBuffer(key), toBuffer(value));

  envoy_dynamic_module_type_envoy_buffer out;
  EXPECT_TRUE(envoy_dynamic_module_callback_udp_session_filter_get_dynamic_metadata_string(
      filterPtr(), toBuffer(ns), toBuffer(key), &out));
  EXPECT_EQ("v", absl::string_view(out.ptr, out.length));

  std::string num_key = "n";
  envoy_dynamic_module_callback_udp_session_filter_set_dynamic_metadata_number(
      filterPtr(), toBuffer(ns), toBuffer(num_key), 3.5);
  double num = 0;
  EXPECT_TRUE(envoy_dynamic_module_callback_udp_session_filter_get_dynamic_metadata_number(
      filterPtr(), toBuffer(ns), toBuffer(num_key), &num));
  EXPECT_EQ(3.5, num);

  // Missing key returns false.
  std::string missing = "missing";
  EXPECT_FALSE(envoy_dynamic_module_callback_udp_session_filter_get_dynamic_metadata_string(
      filterPtr(), toBuffer(ns), toBuffer(missing), &out));
}

TEST_F(DynamicModuleUdpSessionAbiImplTest, Metrics) {
  std::string counter_name = "my_counter";
  size_t counter_id = 0;
  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_Success,
            envoy_dynamic_module_callback_udp_session_filter_config_define_counter(
                configPtr(), toBuffer(counter_name), &counter_id));
  EXPECT_EQ(1, counter_id);

  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_Success,
            envoy_dynamic_module_callback_udp_session_filter_increment_counter(filterPtr(),
                                                                               counter_id, 7));

  // Unknown id returns MetricNotFound.
  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_MetricNotFound,
            envoy_dynamic_module_callback_udp_session_filter_increment_counter(filterPtr(), 999, 1));

  // Config-context mutation also works.
  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_Success,
            envoy_dynamic_module_callback_udp_session_filter_config_increment_counter(
                configPtr(), counter_id, 1));

  // Once frozen, defining new metrics is rejected.
  filter_config_->stat_creation_frozen_ = true;
  size_t gauge_id = 0;
  std::string gauge_name = "my_gauge";
  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_Frozen,
            envoy_dynamic_module_callback_udp_session_filter_config_define_gauge(
                configPtr(), toBuffer(gauge_name), &gauge_id));
}

} // namespace DynamicModules
} // namespace SessionFilters
} // namespace UdpProxy
} // namespace UdpFilters
} // namespace Extensions
} // namespace Envoy
