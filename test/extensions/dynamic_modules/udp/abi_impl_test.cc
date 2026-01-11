#include <vector>

#include "source/common/network/address_impl.h"
#include "source/common/network/utility.h"
#include "source/common/stats/isolated_store_impl.h"
#include "source/extensions/dynamic_modules/abi.h"
#include "source/extensions/filters/udp/dynamic_modules/filter.h"

#include "test/extensions/dynamic_modules/util.h"
#include "test/mocks/network/mocks.h"

namespace Envoy {
namespace Extensions {
namespace UdpFilters {
namespace DynamicModules {

class DynamicModuleUdpListenerFilterAbiCallbackTest : public testing::Test {
public:
  void SetUp() override {
    auto dynamic_module = Extensions::DynamicModules::newDynamicModule(
        Extensions::DynamicModules::testSharedObjectPath("udp_no_op", "c"), false);
    EXPECT_TRUE(dynamic_module.ok()) << dynamic_module.status().message();

    envoy::extensions::filters::udp::dynamic_modules::v3::DynamicModuleUdpListenerFilter
        proto_config;
    proto_config.set_filter_name("test_filter");
    proto_config.mutable_filter_config()->set_value("some_config");

    filter_config_ = std::make_shared<DynamicModuleUdpListenerFilterConfig>(
        proto_config, std::move(dynamic_module.value()), *stats_.rootScope());

    filter_ = std::make_shared<DynamicModuleUdpListenerFilter>(callbacks_, filter_config_);
  }

  void TearDown() override { filter_.reset(); }

  void* filterPtr() { return static_cast<void*>(filter_.get()); }

  Stats::IsolatedStoreImpl stats_;
  DynamicModuleUdpListenerFilterConfigSharedPtr filter_config_;
  std::shared_ptr<DynamicModuleUdpListenerFilter> filter_;
  NiceMock<Network::MockUdpReadFilterCallbacks> callbacks_;
};

// =============================================================================
// Tests for get_datagram_data (size + chunks retrieval).
// =============================================================================

TEST_F(DynamicModuleUdpListenerFilterAbiCallbackTest, GetDatagramDataWithSingleChunk) {
  Network::UdpRecvData data;
  data.buffer_ = std::make_unique<Buffer::OwnedImpl>("hello world");
  data.addresses_.peer_ = Network::Utility::parseInternetAddressAndPortNoThrow("1.2.3.4:1234");
  data.addresses_.local_ = Network::Utility::parseInternetAddressAndPortNoThrow("5.6.7.8:5678");

  // Set current data to simulate being inside onData callback.
  filter_->setCurrentDataForTest(&data);

  size_t chunks_size =
      envoy_dynamic_module_callback_udp_listener_filter_get_datagram_data_chunks_size(filterPtr());

  EXPECT_GE(chunks_size, 1);

  std::vector<envoy_dynamic_module_type_envoy_buffer> chunks(chunks_size);
  ASSERT_TRUE(envoy_dynamic_module_callback_udp_listener_filter_get_datagram_data_chunks(
      filterPtr(), chunks.data()));
  size_t returned_length =
      envoy_dynamic_module_callback_udp_listener_filter_get_datagram_data_size(filterPtr());

  // Verify the data.
  size_t total_length = 0;
  std::string reconstructed;
  for (size_t i = 0; i < chunks_size; i++) {
    total_length += chunks[i].length;
    reconstructed.append(chunks[i].ptr, chunks[i].length);
  }
  EXPECT_EQ(returned_length, total_length);
  EXPECT_EQ(11, total_length);
  EXPECT_EQ("hello world", reconstructed);

  filter_->setCurrentDataForTest(nullptr);
}

TEST_F(DynamicModuleUdpListenerFilterAbiCallbackTest, GetDatagramDataNoCurrentData) {
  // No current data set outside of onData callback.
  size_t chunks_size =
      envoy_dynamic_module_callback_udp_listener_filter_get_datagram_data_chunks_size(filterPtr());
  EXPECT_EQ(0, chunks_size);

  EXPECT_FALSE(envoy_dynamic_module_callback_udp_listener_filter_get_datagram_data_chunks(
      filterPtr(), nullptr));
  size_t returned_length =
      envoy_dynamic_module_callback_udp_listener_filter_get_datagram_data_size(filterPtr());
  EXPECT_EQ(0, returned_length);
}

TEST_F(DynamicModuleUdpListenerFilterAbiCallbackTest, GetDatagramDataMultipleChunks) {
  Network::UdpRecvData data;
  // Create buffer with multiple chunks.
  data.buffer_ = std::make_unique<Buffer::OwnedImpl>();
  data.buffer_->add("chunk1");
  data.buffer_->add("chunk2");
  data.addresses_.peer_ = Network::Utility::parseInternetAddressAndPortNoThrow("1.2.3.4:1234");

  filter_->setCurrentDataForTest(&data);

  size_t chunks_size =
      envoy_dynamic_module_callback_udp_listener_filter_get_datagram_data_chunks_size(filterPtr());
  EXPECT_GE(chunks_size, 1);

  std::vector<envoy_dynamic_module_type_envoy_buffer> chunks(chunks_size);
  ASSERT_TRUE(envoy_dynamic_module_callback_udp_listener_filter_get_datagram_data_chunks(
      filterPtr(), chunks.data()));
  size_t returned_length =
      envoy_dynamic_module_callback_udp_listener_filter_get_datagram_data_size(filterPtr());

  size_t total_length = 0;
  std::string combined;
  for (size_t i = 0; i < chunks_size; i++) {
    total_length += chunks[i].length;
    combined.append(chunks[i].ptr, chunks[i].length);
  }
  EXPECT_EQ(returned_length, total_length);
  EXPECT_EQ(12, total_length);
  EXPECT_EQ("chunk1chunk2", combined);

  filter_->setCurrentDataForTest(nullptr);
}

TEST_F(DynamicModuleUdpListenerFilterAbiCallbackTest, GetDatagramDataNullChunksSizeOut) {
  Network::UdpRecvData data;
  data.buffer_ = std::make_unique<Buffer::OwnedImpl>("test");
  data.addresses_.peer_ = Network::Utility::parseInternetAddressAndPortNoThrow("1.2.3.4:1234");

  filter_->setCurrentDataForTest(&data);

  size_t chunks_size =
      envoy_dynamic_module_callback_udp_listener_filter_get_datagram_data_chunks_size(filterPtr());
  std::vector<envoy_dynamic_module_type_envoy_buffer> chunks(chunks_size);
  ASSERT_TRUE(envoy_dynamic_module_callback_udp_listener_filter_get_datagram_data_chunks(
      filterPtr(), chunks.data()));

  filter_->setCurrentDataForTest(nullptr);
}

TEST_F(DynamicModuleUdpListenerFilterAbiCallbackTest, GetDatagramDataEmptyBuffer) {
  Network::UdpRecvData data;
  data.buffer_ = std::make_unique<Buffer::OwnedImpl>();
  data.addresses_.peer_ = Network::Utility::parseInternetAddressAndPortNoThrow("1.2.3.4:1234");

  filter_->setCurrentDataForTest(&data);

  size_t chunks_size =
      envoy_dynamic_module_callback_udp_listener_filter_get_datagram_data_chunks_size(filterPtr());

  EXPECT_EQ(0, chunks_size);

  EXPECT_FALSE(envoy_dynamic_module_callback_udp_listener_filter_get_datagram_data_chunks(
      filterPtr(), nullptr));
  size_t returned_length =
      envoy_dynamic_module_callback_udp_listener_filter_get_datagram_data_size(filterPtr());
  EXPECT_EQ(0, returned_length);

  filter_->setCurrentDataForTest(nullptr);
}

// =============================================================================
// Tests for set_datagram_data.
// =============================================================================

TEST_F(DynamicModuleUdpListenerFilterAbiCallbackTest, SetDatagramDataReplaceContent) {
  Network::UdpRecvData data;
  data.buffer_ = std::make_unique<Buffer::OwnedImpl>("original");
  data.addresses_.peer_ = Network::Utility::parseInternetAddressAndPortNoThrow("1.2.3.4:1234");

  filter_->setCurrentDataForTest(&data);

  const char* new_data = "modified";
  envoy_dynamic_module_type_module_buffer new_buffer = {new_data, 8};

  bool ok =
      envoy_dynamic_module_callback_udp_listener_filter_set_datagram_data(filterPtr(), new_buffer);

  EXPECT_TRUE(ok);
  EXPECT_EQ("modified", data.buffer_->toString());

  filter_->setCurrentDataForTest(nullptr);
}

TEST_F(DynamicModuleUdpListenerFilterAbiCallbackTest, SetDatagramDataClearBuffer) {
  Network::UdpRecvData data;
  data.buffer_ = std::make_unique<Buffer::OwnedImpl>("original");
  data.addresses_.peer_ = Network::Utility::parseInternetAddressAndPortNoThrow("1.2.3.4:1234");

  filter_->setCurrentDataForTest(&data);

  envoy_dynamic_module_type_module_buffer empty_buffer = {nullptr, 0};

  bool ok = envoy_dynamic_module_callback_udp_listener_filter_set_datagram_data(filterPtr(),
                                                                                empty_buffer);

  EXPECT_TRUE(ok);
  EXPECT_EQ(0, data.buffer_->length());

  filter_->setCurrentDataForTest(nullptr);
}

TEST_F(DynamicModuleUdpListenerFilterAbiCallbackTest, SetDatagramDataNoCurrentData) {
  const char* new_data = "test";
  envoy_dynamic_module_type_module_buffer new_buffer = {new_data, 4};

  bool ok =
      envoy_dynamic_module_callback_udp_listener_filter_set_datagram_data(filterPtr(), new_buffer);

  EXPECT_FALSE(ok);
}

TEST_F(DynamicModuleUdpListenerFilterAbiCallbackTest, SetDatagramDataNullPointerWithLength) {
  Network::UdpRecvData data;
  data.buffer_ = std::make_unique<Buffer::OwnedImpl>("original");
  data.addresses_.peer_ = Network::Utility::parseInternetAddressAndPortNoThrow("1.2.3.4:1234");

  filter_->setCurrentDataForTest(&data);

  envoy_dynamic_module_type_module_buffer invalid_buffer = {nullptr, 10};

  bool ok = envoy_dynamic_module_callback_udp_listener_filter_set_datagram_data(filterPtr(),
                                                                                invalid_buffer);

  EXPECT_TRUE(ok);
  EXPECT_EQ(0, data.buffer_->length());

  filter_->setCurrentDataForTest(nullptr);
}

// =============================================================================
// Tests for get_peer_address.
// =============================================================================

TEST_F(DynamicModuleUdpListenerFilterAbiCallbackTest, GetPeerAddressIpv4) {
  Network::UdpRecvData data;
  data.buffer_ = std::make_unique<Buffer::OwnedImpl>("test");
  data.addresses_.peer_ =
      Network::Utility::parseInternetAddressAndPortNoThrow("192.168.1.100:8080");
  data.addresses_.local_ = Network::Utility::parseInternetAddressAndPortNoThrow("10.0.0.1:9090");

  filter_->setCurrentDataForTest(&data);

  envoy_dynamic_module_type_envoy_buffer address_buf = {nullptr, 0};
  uint32_t port = 0;

  bool ok = envoy_dynamic_module_callback_udp_listener_filter_get_peer_address(filterPtr(),
                                                                               &address_buf, &port);

  EXPECT_TRUE(ok);
  EXPECT_NE(nullptr, address_buf.ptr);
  EXPECT_GT(address_buf.length, 0);
  EXPECT_EQ("192.168.1.100", std::string(address_buf.ptr, address_buf.length));
  EXPECT_EQ(8080, port);

  filter_->setCurrentDataForTest(nullptr);
}

TEST_F(DynamicModuleUdpListenerFilterAbiCallbackTest, GetPeerAddressIpv6) {
  Network::UdpRecvData data;
  data.buffer_ = std::make_unique<Buffer::OwnedImpl>("test");
  data.addresses_.peer_ = Network::Utility::parseInternetAddressAndPortNoThrow("[::1]:12345");
  data.addresses_.local_ = Network::Utility::parseInternetAddressAndPortNoThrow("[::2]:54321");

  filter_->setCurrentDataForTest(&data);

  envoy_dynamic_module_type_envoy_buffer address_buf = {nullptr, 0};
  uint32_t port = 0;

  bool ok = envoy_dynamic_module_callback_udp_listener_filter_get_peer_address(filterPtr(),
                                                                               &address_buf, &port);

  EXPECT_TRUE(ok);
  EXPECT_NE(nullptr, address_buf.ptr);
  EXPECT_GT(address_buf.length, 0);
  EXPECT_EQ("::1", std::string(address_buf.ptr, address_buf.length));
  EXPECT_EQ(12345, port);

  filter_->setCurrentDataForTest(nullptr);
}

TEST_F(DynamicModuleUdpListenerFilterAbiCallbackTest, GetPeerAddressNoCurrentData) {
  envoy_dynamic_module_type_envoy_buffer address_buf = {nullptr, 0};
  uint32_t port = 0;

  bool ok = envoy_dynamic_module_callback_udp_listener_filter_get_peer_address(filterPtr(),
                                                                               &address_buf, &port);

  EXPECT_FALSE(ok);
}

TEST_F(DynamicModuleUdpListenerFilterAbiCallbackTest, GetPeerAddressNullPeer) {
  Network::UdpRecvData data;
  data.buffer_ = std::make_unique<Buffer::OwnedImpl>("test");
  data.addresses_.peer_ = nullptr;

  filter_->setCurrentDataForTest(&data);

  envoy_dynamic_module_type_envoy_buffer address_buf = {nullptr, 0};
  uint32_t port = 0;

  bool ok = envoy_dynamic_module_callback_udp_listener_filter_get_peer_address(filterPtr(),
                                                                               &address_buf, &port);

  EXPECT_FALSE(ok);

  filter_->setCurrentDataForTest(nullptr);
}

// =============================================================================
// Tests for get_local_address.
// =============================================================================

TEST_F(DynamicModuleUdpListenerFilterAbiCallbackTest, GetLocalAddressFromRecvData) {
  Network::UdpRecvData data;
  data.buffer_ = std::make_unique<Buffer::OwnedImpl>("test");
  data.addresses_.peer_ = Network::Utility::parseInternetAddressAndPortNoThrow("1.2.3.4:1234");
  data.addresses_.local_ = Network::Utility::parseInternetAddressAndPortNoThrow("10.20.30.40:5555");

  filter_->setCurrentDataForTest(&data);

  envoy_dynamic_module_type_envoy_buffer address_buf = {nullptr, 0};
  uint32_t port = 0;

  bool ok = envoy_dynamic_module_callback_udp_listener_filter_get_local_address(
      filterPtr(), &address_buf, &port);

  EXPECT_TRUE(ok);
  EXPECT_NE(nullptr, address_buf.ptr);
  EXPECT_GT(address_buf.length, 0);
  EXPECT_EQ("10.20.30.40", std::string(address_buf.ptr, address_buf.length));
  EXPECT_EQ(5555, port);

  filter_->setCurrentDataForTest(nullptr);
}

TEST_F(DynamicModuleUdpListenerFilterAbiCallbackTest, GetLocalAddressFromCallbacks) {
  // No current data, should fall back to callbacks.
  auto local_addr = Network::Utility::parseInternetAddressAndPortNoThrow("127.0.0.1:8888");
  auto mock_listener = std::make_shared<NiceMock<Network::MockUdpListener>>();
  EXPECT_CALL(*mock_listener, localAddress()).WillRepeatedly(testing::ReturnRef(local_addr));
  EXPECT_CALL(callbacks_, udpListener()).WillRepeatedly(testing::ReturnRef(*mock_listener));

  envoy_dynamic_module_type_envoy_buffer address_buf = {nullptr, 0};
  uint32_t port = 0;

  bool ok = envoy_dynamic_module_callback_udp_listener_filter_get_local_address(
      filterPtr(), &address_buf, &port);

  EXPECT_TRUE(ok);
  EXPECT_NE(nullptr, address_buf.ptr);
  EXPECT_EQ("127.0.0.1", std::string(address_buf.ptr, address_buf.length));
  EXPECT_EQ(8888, port);
}

TEST_F(DynamicModuleUdpListenerFilterAbiCallbackTest, GetLocalAddressIpv6) {
  Network::UdpRecvData data;
  data.buffer_ = std::make_unique<Buffer::OwnedImpl>("test");
  data.addresses_.peer_ = Network::Utility::parseInternetAddressAndPortNoThrow("[::1]:1234");
  data.addresses_.local_ = Network::Utility::parseInternetAddressAndPortNoThrow("[fe80::1]:9999");

  filter_->setCurrentDataForTest(&data);

  envoy_dynamic_module_type_envoy_buffer address_buf = {nullptr, 0};
  uint32_t port = 0;

  bool ok = envoy_dynamic_module_callback_udp_listener_filter_get_local_address(
      filterPtr(), &address_buf, &port);

  EXPECT_TRUE(ok);
  EXPECT_NE(nullptr, address_buf.ptr);
  EXPECT_EQ("fe80::1", std::string(address_buf.ptr, address_buf.length));
  EXPECT_EQ(9999, port);

  filter_->setCurrentDataForTest(nullptr);
}

TEST_F(DynamicModuleUdpListenerFilterAbiCallbackTest, GetLocalAddressNoData) {
  // No current data, but callbacks will try to access udpListener.
  // Mock a listener with a null/invalid address to test the error path.
  auto local_addr = Network::Utility::parseInternetAddressAndPortNoThrow("pipe://test");
  auto mock_listener = std::make_shared<NiceMock<Network::MockUdpListener>>();
  EXPECT_CALL(*mock_listener, localAddress()).WillRepeatedly(testing::ReturnRef(local_addr));
  EXPECT_CALL(callbacks_, udpListener()).WillRepeatedly(testing::ReturnRef(*mock_listener));

  envoy_dynamic_module_type_envoy_buffer address_buf = {nullptr, 0};
  uint32_t port = 0;

  bool ok = envoy_dynamic_module_callback_udp_listener_filter_get_local_address(
      filterPtr(), &address_buf, &port);

  // Should return false since the address is not an IP address.
  EXPECT_FALSE(ok);
}

// =============================================================================
// Tests for send_datagram.
// =============================================================================

TEST_F(DynamicModuleUdpListenerFilterAbiCallbackTest, SendDatagramWithExplicitAddress) {
  Network::UdpRecvData data;
  data.buffer_ = std::make_unique<Buffer::OwnedImpl>("original");
  data.addresses_.peer_ = Network::Utility::parseInternetAddressAndPortNoThrow("1.2.3.4:1234");
  data.addresses_.local_ = Network::Utility::parseInternetAddressAndPortNoThrow("10.0.0.1:5000");

  auto mock_listener = std::make_shared<NiceMock<Network::MockUdpListener>>();
  EXPECT_CALL(callbacks_, udpListener()).WillRepeatedly(testing::ReturnRef(*mock_listener));

  // Expect send to be called with the right data.
  EXPECT_CALL(*mock_listener, send(testing::_))
      .WillOnce(testing::Invoke([](const Network::UdpSendData& send_data) {
        EXPECT_EQ("response data", send_data.buffer_.toString());
        EXPECT_EQ("192.168.1.1", send_data.peer_address_.ip()->addressAsString());
        EXPECT_EQ(7777, send_data.peer_address_.ip()->port());
        return Api::IoCallUint64Result(13, Api::IoError::none());
      }));

  filter_->setCurrentDataForTest(&data);

  const char* response = "response data";
  const char* peer_ip = "192.168.1.1";
  envoy_dynamic_module_type_module_buffer data_buf = {response, 13};
  envoy_dynamic_module_type_module_buffer peer_addr_buf = {peer_ip, 11};

  EXPECT_TRUE(envoy_dynamic_module_callback_udp_listener_filter_send_datagram(filterPtr(), data_buf,
                                                                              peer_addr_buf, 7777));

  filter_->setCurrentDataForTest(nullptr);
}

TEST_F(DynamicModuleUdpListenerFilterAbiCallbackTest, SendDatagramToOriginalPeer) {
  Network::UdpRecvData data;
  data.buffer_ = std::make_unique<Buffer::OwnedImpl>("request");
  data.addresses_.peer_ = Network::Utility::parseInternetAddressAndPortNoThrow("9.8.7.6:4321");
  data.addresses_.local_ = Network::Utility::parseInternetAddressAndPortNoThrow("10.0.0.1:5000");

  auto mock_listener = std::make_shared<NiceMock<Network::MockUdpListener>>();
  EXPECT_CALL(callbacks_, udpListener()).WillRepeatedly(testing::ReturnRef(*mock_listener));

  EXPECT_CALL(*mock_listener, send(testing::_))
      .WillOnce(testing::Invoke([](const Network::UdpSendData& send_data) {
        EXPECT_EQ("echo back", send_data.buffer_.toString());
        EXPECT_EQ("9.8.7.6", send_data.peer_address_.ip()->addressAsString());
        EXPECT_EQ(4321, send_data.peer_address_.ip()->port());
        return Api::IoCallUint64Result(9, Api::IoError::none());
      }));

  filter_->setCurrentDataForTest(&data);

  const char* response = "echo back";
  envoy_dynamic_module_type_module_buffer data_buf = {response, 9};
  envoy_dynamic_module_type_module_buffer peer_addr_buf = {nullptr, 0};

  EXPECT_TRUE(envoy_dynamic_module_callback_udp_listener_filter_send_datagram(filterPtr(), data_buf,
                                                                              peer_addr_buf, 0));

  filter_->setCurrentDataForTest(nullptr);
}

TEST_F(DynamicModuleUdpListenerFilterAbiCallbackTest, SendDatagramEmptyData) {
  Network::UdpRecvData data;
  data.buffer_ = std::make_unique<Buffer::OwnedImpl>("test");
  data.addresses_.peer_ = Network::Utility::parseInternetAddressAndPortNoThrow("1.2.3.4:1234");
  data.addresses_.local_ = Network::Utility::parseInternetAddressAndPortNoThrow("10.0.0.1:5000");

  auto mock_listener = std::make_shared<NiceMock<Network::MockUdpListener>>();
  EXPECT_CALL(callbacks_, udpListener()).WillRepeatedly(testing::ReturnRef(*mock_listener));

  EXPECT_CALL(*mock_listener, send(testing::_))
      .WillOnce(testing::Invoke([](const Network::UdpSendData& send_data) {
        EXPECT_EQ(0, send_data.buffer_.length());
        return Api::IoCallUint64Result(0, Api::IoError::none());
      }));

  filter_->setCurrentDataForTest(&data);

  envoy_dynamic_module_type_module_buffer data_buf = {nullptr, 0};
  envoy_dynamic_module_type_module_buffer peer_addr_buf = {nullptr, 0};

  EXPECT_TRUE(envoy_dynamic_module_callback_udp_listener_filter_send_datagram(filterPtr(), data_buf,
                                                                              peer_addr_buf, 0));

  filter_->setCurrentDataForTest(nullptr);
}

TEST_F(DynamicModuleUdpListenerFilterAbiCallbackTest, SendDatagramInvalidPeerAddress) {
  Network::UdpRecvData data;
  data.buffer_ = std::make_unique<Buffer::OwnedImpl>("test");
  data.addresses_.peer_ = Network::Utility::parseInternetAddressAndPortNoThrow("1.2.3.4:1234");
  data.addresses_.local_ = Network::Utility::parseInternetAddressAndPortNoThrow("10.0.0.1:5000");

  auto mock_listener = std::make_shared<NiceMock<Network::MockUdpListener>>();
  EXPECT_CALL(callbacks_, udpListener()).WillRepeatedly(testing::ReturnRef(*mock_listener));

  // Should not call send since address is invalid.
  EXPECT_CALL(*mock_listener, send(testing::_)).Times(0);

  filter_->setCurrentDataForTest(&data);

  const char* response = "data";
  const char* invalid_ip = "not.an.ip.address";
  envoy_dynamic_module_type_module_buffer data_buf = {response, 4};
  envoy_dynamic_module_type_module_buffer peer_addr_buf = {invalid_ip, 17};

  EXPECT_FALSE(envoy_dynamic_module_callback_udp_listener_filter_send_datagram(
      filterPtr(), data_buf, peer_addr_buf, 8888));

  filter_->setCurrentDataForTest(nullptr);
}

TEST_F(DynamicModuleUdpListenerFilterAbiCallbackTest, SendDatagramNoPeerAddress) {
  // No current data means no peer address to send to.
  auto mock_listener = std::make_shared<NiceMock<Network::MockUdpListener>>();
  EXPECT_CALL(callbacks_, udpListener()).WillRepeatedly(testing::ReturnRef(*mock_listener));

  EXPECT_CALL(*mock_listener, send(testing::_)).Times(0);

  const char* response = "data";
  envoy_dynamic_module_type_module_buffer data_buf = {response, 4};
  envoy_dynamic_module_type_module_buffer peer_addr_buf = {nullptr, 0};

  EXPECT_FALSE(envoy_dynamic_module_callback_udp_listener_filter_send_datagram(
      filterPtr(), data_buf, peer_addr_buf, 0));
}

TEST_F(DynamicModuleUdpListenerFilterAbiCallbackTest, SendDatagramNoCallbacks) {
  NiceMock<Network::MockUdpReadFilterCallbacks> local_callbacks;
  auto filter_without_listener =
      std::make_shared<DynamicModuleUdpListenerFilter>(local_callbacks, filter_config_);

  Network::UdpRecvData data;
  data.buffer_ = std::make_unique<Buffer::OwnedImpl>("test");
  data.addresses_.peer_ = Network::Utility::parseInternetAddressAndPortNoThrow("1.2.3.4:1234");
  data.addresses_.local_ = Network::Utility::parseInternetAddressAndPortNoThrow("10.0.0.1:5000");

  filter_without_listener->onData(data);

  const char* response = "data";
  envoy_dynamic_module_type_module_buffer data_buf = {response, 4};
  envoy_dynamic_module_type_module_buffer peer_addr_buf = {nullptr, 0};

  // Should not crash.
  EXPECT_FALSE(envoy_dynamic_module_callback_udp_listener_filter_send_datagram(
      static_cast<void*>(filter_without_listener.get()), data_buf, peer_addr_buf, 0));
}

TEST_F(DynamicModuleUdpListenerFilterAbiCallbackTest, SendDatagramIpv6) {
  Network::UdpRecvData data;
  data.buffer_ = std::make_unique<Buffer::OwnedImpl>("test");
  data.addresses_.peer_ = Network::Utility::parseInternetAddressAndPortNoThrow("[::1]:1234");
  data.addresses_.local_ = Network::Utility::parseInternetAddressAndPortNoThrow("[::2]:5000");

  auto mock_listener = std::make_shared<NiceMock<Network::MockUdpListener>>();
  EXPECT_CALL(callbacks_, udpListener()).WillRepeatedly(testing::ReturnRef(*mock_listener));

  EXPECT_CALL(*mock_listener, send(testing::_))
      .WillOnce(testing::Invoke([](const Network::UdpSendData& send_data) {
        EXPECT_EQ("ipv6 data", send_data.buffer_.toString());
        EXPECT_EQ("2001:db8::1", send_data.peer_address_.ip()->addressAsString());
        EXPECT_EQ(9999, send_data.peer_address_.ip()->port());
        return Api::IoCallUint64Result(9, Api::IoError::none());
      }));

  filter_->setCurrentDataForTest(&data);

  const char* response = "ipv6 data";
  const char* peer_ip = "2001:db8::1";
  envoy_dynamic_module_type_module_buffer data_buf = {response, 9};
  envoy_dynamic_module_type_module_buffer peer_addr_buf = {peer_ip, 11};

  EXPECT_TRUE(envoy_dynamic_module_callback_udp_listener_filter_send_datagram(filterPtr(), data_buf,
                                                                              peer_addr_buf, 9999));

  filter_->setCurrentDataForTest(nullptr);
}

} // namespace DynamicModules
} // namespace UdpFilters
} // namespace Extensions
} // namespace Envoy
