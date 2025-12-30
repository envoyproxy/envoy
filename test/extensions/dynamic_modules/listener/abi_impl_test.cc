#include <chrono>
#include <vector>

#include "source/common/network/address_impl.h"
#include "source/common/router/string_accessor_impl.h"
#include "source/extensions/dynamic_modules/abi.h"
#include "source/extensions/filters/listener/dynamic_modules/filter.h"

#include "test/extensions/dynamic_modules/util.h"
#include "test/mocks/network/mocks.h"

namespace Envoy {
namespace Extensions {
namespace DynamicModules {
namespace ListenerFilters {

#ifdef SOL_IP
// Helper action to set sockaddr in arg2 for getSocketOption mocking.
ACTION_P(SetArg2Sockaddr, val) {
  const sockaddr_in& sin = reinterpret_cast<const sockaddr_in&>(val);
  (static_cast<sockaddr_in*>(arg2))->sin_addr = sin.sin_addr;
  (static_cast<sockaddr_in*>(arg2))->sin_family = sin.sin_family;
  (static_cast<sockaddr_in*>(arg2))->sin_port = sin.sin_port;
}
#endif // SOL_IP

// A simple mock implementation of ListenerFilterBuffer for testing.
class MockListenerFilterBuffer : public Network::ListenerFilterBuffer {
public:
  MockListenerFilterBuffer(Buffer::Instance& buffer) : buffer_(buffer) {}

  const Buffer::ConstRawSlice rawSlice() const override {
    Buffer::RawSliceVector slices = buffer_.getRawSlices();
    if (slices.empty()) {
      return {nullptr, 0};
    }
    return {slices[0].mem_, slices[0].len_};
  }

  bool drain(uint64_t length) override {
    if (length > buffer_.length()) {
      length = buffer_.length();
    }
    buffer_.drain(length);
    return true;
  }

private:
  Buffer::Instance& buffer_;
};

class DynamicModuleListenerFilterAbiCallbackTest : public testing::Test {
public:
  void SetUp() override {
    auto dynamic_module = newDynamicModule(testSharedObjectPath("listener_no_op", "c"), false);
    EXPECT_TRUE(dynamic_module.ok()) << dynamic_module.status().message();

    auto filter_config_or_status =
        newDynamicModuleListenerFilterConfig("test_filter", "", std::move(dynamic_module.value()));
    EXPECT_TRUE(filter_config_or_status.ok()) << filter_config_or_status.status().message();
    filter_config_ = filter_config_or_status.value();

    filter_ = std::make_shared<DynamicModuleListenerFilter>(filter_config_);
    filter_->initializeInModuleFilter();

    filter_->setCallbacksForTest(&callbacks_);
  }

  void TearDown() override { filter_.reset(); }

  void* filterPtr() { return static_cast<void*>(filter_.get()); }

  DynamicModuleListenerFilterConfigSharedPtr filter_config_;
  std::shared_ptr<DynamicModuleListenerFilter> filter_;
  NiceMock<Network::MockListenerFilterCallbacks> callbacks_;
};

// =============================================================================
// Tests for get_buffer_chunk.
// =============================================================================

TEST_F(DynamicModuleListenerFilterAbiCallbackTest, GetBufferChunkWithData) {
  Buffer::OwnedImpl buffer("hello world");
  MockListenerFilterBuffer mock_buffer(buffer);
  filter_->setCurrentBufferForTest(&mock_buffer);

  envoy_dynamic_module_type_envoy_buffer chunk = {nullptr, 0};
  bool ok = envoy_dynamic_module_callback_listener_filter_get_buffer_chunk(filterPtr(), &chunk);

  EXPECT_TRUE(ok);
  EXPECT_NE(nullptr, chunk.ptr);
  EXPECT_EQ(11, chunk.length);
  EXPECT_EQ("hello world", std::string(chunk.ptr, chunk.length));

  filter_->setCurrentBufferForTest(nullptr);
}

TEST_F(DynamicModuleListenerFilterAbiCallbackTest, GetBufferChunkNullBuffer) {
  envoy_dynamic_module_type_envoy_buffer chunk = {nullptr, 0};
  bool ok = envoy_dynamic_module_callback_listener_filter_get_buffer_chunk(filterPtr(), &chunk);

  EXPECT_FALSE(ok);
  EXPECT_EQ(nullptr, chunk.ptr);
  EXPECT_EQ(0, chunk.length);
}

TEST_F(DynamicModuleListenerFilterAbiCallbackTest, GetBufferChunkEmptyBuffer) {
  Buffer::OwnedImpl empty_buffer;
  MockListenerFilterBuffer mock_buffer(empty_buffer);
  filter_->setCurrentBufferForTest(&mock_buffer);

  envoy_dynamic_module_type_envoy_buffer chunk = {nullptr, 0};
  bool ok = envoy_dynamic_module_callback_listener_filter_get_buffer_chunk(filterPtr(), &chunk);

  EXPECT_TRUE(ok);
  EXPECT_EQ(0, chunk.length);

  filter_->setCurrentBufferForTest(nullptr);
}

// =============================================================================
// Tests for drain_buffer.
// =============================================================================

TEST_F(DynamicModuleListenerFilterAbiCallbackTest, DrainBufferWithData) {
  Buffer::OwnedImpl buffer("hello world");
  MockListenerFilterBuffer mock_buffer(buffer);
  filter_->setCurrentBufferForTest(&mock_buffer);

  bool ok = envoy_dynamic_module_callback_listener_filter_drain_buffer(filterPtr(), 6);
  EXPECT_TRUE(ok);
  EXPECT_EQ("world", buffer.toString());

  filter_->setCurrentBufferForTest(nullptr);
}

TEST_F(DynamicModuleListenerFilterAbiCallbackTest, DrainBufferNullBuffer) {
  bool ok = envoy_dynamic_module_callback_listener_filter_drain_buffer(filterPtr(), 10);
  EXPECT_FALSE(ok);
}

TEST_F(DynamicModuleListenerFilterAbiCallbackTest, DrainBufferZeroLength) {
  Buffer::OwnedImpl buffer("test");
  MockListenerFilterBuffer mock_buffer(buffer);
  filter_->setCurrentBufferForTest(&mock_buffer);

  bool ok = envoy_dynamic_module_callback_listener_filter_drain_buffer(filterPtr(), 0);
  EXPECT_FALSE(ok);
  EXPECT_EQ("test", buffer.toString());

  filter_->setCurrentBufferForTest(nullptr);
}

// =============================================================================
// Tests for set_detected_transport_protocol.
// =============================================================================

TEST_F(DynamicModuleListenerFilterAbiCallbackTest, SetDetectedTransportProtocol) {
  EXPECT_CALL(callbacks_.socket_, setDetectedTransportProtocol(absl::string_view("tls")));

  char protocol[] = "tls";
  envoy_dynamic_module_type_module_buffer protocol_buf = {protocol, 3};
  envoy_dynamic_module_callback_listener_filter_set_detected_transport_protocol(filterPtr(),
                                                                                protocol_buf);
}

TEST_F(DynamicModuleListenerFilterAbiCallbackTest, SetDetectedTransportProtocolNullCallbacks) {
  auto filter = std::make_shared<DynamicModuleListenerFilter>(filter_config_);
  filter->initializeInModuleFilter();
  // Callbacks not set.

  char protocol[] = "tls";
  envoy_dynamic_module_type_module_buffer protocol_buf = {protocol, 3};
  // Should not crash.
  envoy_dynamic_module_callback_listener_filter_set_detected_transport_protocol(
      static_cast<void*>(filter.get()), protocol_buf);
}

TEST_F(DynamicModuleListenerFilterAbiCallbackTest, SetDetectedTransportProtocolNullProtocol) {
  // Should not crash with null protocol.
  envoy_dynamic_module_type_module_buffer protocol_buf = {nullptr, 3};
  envoy_dynamic_module_callback_listener_filter_set_detected_transport_protocol(filterPtr(),
                                                                                protocol_buf);
}

TEST_F(DynamicModuleListenerFilterAbiCallbackTest, SetDetectedTransportProtocolZeroLength) {
  char protocol[] = "tls";
  envoy_dynamic_module_type_module_buffer protocol_buf = {protocol, 0};
  // Should not call socket method with zero length.
  envoy_dynamic_module_callback_listener_filter_set_detected_transport_protocol(filterPtr(),
                                                                                protocol_buf);
}

// =============================================================================
// Tests for set_requested_server_name.
// =============================================================================

TEST_F(DynamicModuleListenerFilterAbiCallbackTest, SetRequestedServerName) {
  EXPECT_CALL(callbacks_.socket_, setRequestedServerName(absl::string_view("example.com")));

  char name[] = "example.com";
  envoy_dynamic_module_type_module_buffer name_buf = {name, 11};
  envoy_dynamic_module_callback_listener_filter_set_requested_server_name(filterPtr(), name_buf);
}

TEST_F(DynamicModuleListenerFilterAbiCallbackTest, SetRequestedServerNameNullCallbacks) {
  auto filter = std::make_shared<DynamicModuleListenerFilter>(filter_config_);
  filter->initializeInModuleFilter();

  char name[] = "example.com";
  envoy_dynamic_module_type_module_buffer name_buf = {name, 11};
  // Should not crash.
  envoy_dynamic_module_callback_listener_filter_set_requested_server_name(
      static_cast<void*>(filter.get()), name_buf);
}

TEST_F(DynamicModuleListenerFilterAbiCallbackTest, SetRequestedServerNameNullName) {
  envoy_dynamic_module_type_module_buffer name_buf = {nullptr, 5};
  envoy_dynamic_module_callback_listener_filter_set_requested_server_name(filterPtr(), name_buf);
}

// =============================================================================
// Tests for set_requested_application_protocols.
// =============================================================================

TEST_F(DynamicModuleListenerFilterAbiCallbackTest, SetRequestedApplicationProtocols) {
  std::vector<std::string> expected = {"h2", "http/1.1"};
  EXPECT_CALL(callbacks_.socket_, setRequestedApplicationProtocols(testing::_));

  char proto1[] = "h2";
  char proto2[] = "http/1.1";
  envoy_dynamic_module_type_module_buffer protocols[] = {{proto1, 2}, {proto2, 8}};

  envoy_dynamic_module_callback_listener_filter_set_requested_application_protocols(filterPtr(),
                                                                                    protocols, 2);
}

TEST_F(DynamicModuleListenerFilterAbiCallbackTest, SetRequestedApplicationProtocolsNullCallbacks) {
  auto filter = std::make_shared<DynamicModuleListenerFilter>(filter_config_);
  filter->initializeInModuleFilter();

  char proto1[] = "h2";
  envoy_dynamic_module_type_module_buffer protocols[] = {{proto1, 2}};

  // Should not crash.
  envoy_dynamic_module_callback_listener_filter_set_requested_application_protocols(
      static_cast<void*>(filter.get()), protocols, 1);
}

TEST_F(DynamicModuleListenerFilterAbiCallbackTest, SetRequestedApplicationProtocolsNullArray) {
  envoy_dynamic_module_callback_listener_filter_set_requested_application_protocols(filterPtr(),
                                                                                    nullptr, 1);
}

TEST_F(DynamicModuleListenerFilterAbiCallbackTest, SetRequestedApplicationProtocolsZeroCount) {
  char proto1[] = "h2";
  envoy_dynamic_module_type_module_buffer protocols[] = {{proto1, 2}};

  envoy_dynamic_module_callback_listener_filter_set_requested_application_protocols(filterPtr(),
                                                                                    protocols, 0);
}

// =============================================================================
// Tests for `set_ja3_hash`.
// =============================================================================

TEST_F(DynamicModuleListenerFilterAbiCallbackTest, SetJa3Hash) {
  EXPECT_CALL(callbacks_.socket_, setJA3Hash("abc123"));

  char hash[] = "abc123";
  envoy_dynamic_module_type_module_buffer hash_buf = {hash, 6};
  envoy_dynamic_module_callback_listener_filter_set_ja3_hash(filterPtr(), hash_buf);
}

TEST_F(DynamicModuleListenerFilterAbiCallbackTest, SetJa3HashNullCallbacks) {
  auto filter = std::make_shared<DynamicModuleListenerFilter>(filter_config_);
  filter->initializeInModuleFilter();

  char hash[] = "abc123";
  envoy_dynamic_module_type_module_buffer hash_buf = {hash, 6};
  envoy_dynamic_module_callback_listener_filter_set_ja3_hash(static_cast<void*>(filter.get()),
                                                             hash_buf);
}

TEST_F(DynamicModuleListenerFilterAbiCallbackTest, SetJa3HashNullHash) {
  envoy_dynamic_module_type_module_buffer hash_buf = {nullptr, 6};
  envoy_dynamic_module_callback_listener_filter_set_ja3_hash(filterPtr(), hash_buf);
}

// =============================================================================
// Tests for `set_ja4_hash`.
// =============================================================================

TEST_F(DynamicModuleListenerFilterAbiCallbackTest, SetJa4Hash) {
  EXPECT_CALL(callbacks_.socket_, setJA4Hash("def456"));

  char hash[] = "def456";
  envoy_dynamic_module_type_module_buffer hash_buf = {hash, 6};
  envoy_dynamic_module_callback_listener_filter_set_ja4_hash(filterPtr(), hash_buf);
}

TEST_F(DynamicModuleListenerFilterAbiCallbackTest, SetJa4HashNullCallbacks) {
  auto filter = std::make_shared<DynamicModuleListenerFilter>(filter_config_);
  filter->initializeInModuleFilter();

  char hash[] = "def456";
  envoy_dynamic_module_type_module_buffer hash_buf = {hash, 6};
  envoy_dynamic_module_callback_listener_filter_set_ja4_hash(static_cast<void*>(filter.get()),
                                                             hash_buf);
}

// =============================================================================
// Tests for get_remote_address.
// =============================================================================

TEST_F(DynamicModuleListenerFilterAbiCallbackTest, GetRemoteAddressWithIp) {
  // Set up the connection info provider on the socket mock.
  auto address = Network::Utility::parseInternetAddressNoThrow("1.2.3.4", 8080);
  callbacks_.socket_.connection_info_provider_ =
      std::make_shared<Network::ConnectionInfoSetterImpl>(address, address);

  envoy_dynamic_module_type_envoy_buffer address_out = {nullptr, 0};
  uint32_t port_out = 0;
  bool found = envoy_dynamic_module_callback_listener_filter_get_remote_address(
      filterPtr(), &address_out, &port_out);

  EXPECT_TRUE(found);
  EXPECT_GT(address_out.length, 0);
  EXPECT_NE(nullptr, address_out.ptr);
  EXPECT_EQ(8080, port_out);
}

TEST_F(DynamicModuleListenerFilterAbiCallbackTest, GetRemoteAddressNullCallbacks) {
  auto filter = std::make_shared<DynamicModuleListenerFilter>(filter_config_);
  filter->initializeInModuleFilter();

  envoy_dynamic_module_type_envoy_buffer address_out = {nullptr, 0};
  uint32_t port_out = 0;
  bool found = envoy_dynamic_module_callback_listener_filter_get_remote_address(
      static_cast<void*>(filter.get()), &address_out, &port_out);

  EXPECT_FALSE(found);
  EXPECT_EQ(nullptr, address_out.ptr);
  EXPECT_EQ(0, address_out.length);
  EXPECT_EQ(0, port_out);
}

TEST_F(DynamicModuleListenerFilterAbiCallbackTest, GetRemoteAddressNonIp) {
  // Use a pipe address which has no IP.
  Network::Address::InstanceConstSharedPtr pipe =
      *Network::Address::PipeInstance::create("/tmp/test.sock");
  callbacks_.socket_.connection_info_provider_ =
      std::make_shared<Network::ConnectionInfoSetterImpl>(pipe, pipe);

  envoy_dynamic_module_type_envoy_buffer address_out = {nullptr, 0};
  uint32_t port_out = 0;
  bool found = envoy_dynamic_module_callback_listener_filter_get_remote_address(
      filterPtr(), &address_out, &port_out);

  EXPECT_FALSE(found);
  EXPECT_EQ(nullptr, address_out.ptr);
  EXPECT_EQ(0, address_out.length);
  EXPECT_EQ(0, port_out);
}

// =============================================================================
// Tests for get_local_address.
// =============================================================================

TEST_F(DynamicModuleListenerFilterAbiCallbackTest, GetLocalAddressWithIp) {
  auto address = Network::Utility::parseInternetAddressNoThrow("5.6.7.8", 9090);
  callbacks_.socket_.connection_info_provider_ =
      std::make_shared<Network::ConnectionInfoSetterImpl>(address, address);

  envoy_dynamic_module_type_envoy_buffer address_out = {nullptr, 0};
  uint32_t port_out = 0;
  bool found = envoy_dynamic_module_callback_listener_filter_get_local_address(
      filterPtr(), &address_out, &port_out);

  EXPECT_TRUE(found);
  EXPECT_GT(address_out.length, 0);
  EXPECT_NE(nullptr, address_out.ptr);
  EXPECT_EQ(9090, port_out);
}

TEST_F(DynamicModuleListenerFilterAbiCallbackTest, GetLocalAddressNullCallbacks) {
  auto filter = std::make_shared<DynamicModuleListenerFilter>(filter_config_);
  filter->initializeInModuleFilter();

  envoy_dynamic_module_type_envoy_buffer address_out = {nullptr, 0};
  uint32_t port_out = 0;
  bool found = envoy_dynamic_module_callback_listener_filter_get_local_address(
      static_cast<void*>(filter.get()), &address_out, &port_out);

  EXPECT_FALSE(found);
  EXPECT_EQ(nullptr, address_out.ptr);
  EXPECT_EQ(0, address_out.length);
  EXPECT_EQ(0, port_out);
}

TEST_F(DynamicModuleListenerFilterAbiCallbackTest, GetLocalAddressNonIp) {
  Network::Address::InstanceConstSharedPtr pipe =
      *Network::Address::PipeInstance::create("/tmp/test.sock");
  callbacks_.socket_.connection_info_provider_ =
      std::make_shared<Network::ConnectionInfoSetterImpl>(pipe, pipe);

  envoy_dynamic_module_type_envoy_buffer address_out = {nullptr, 0};
  uint32_t port_out = 0;
  bool found = envoy_dynamic_module_callback_listener_filter_get_local_address(
      filterPtr(), &address_out, &port_out);

  EXPECT_FALSE(found);
  EXPECT_EQ(nullptr, address_out.ptr);
  EXPECT_EQ(0, address_out.length);
  EXPECT_EQ(0, port_out);
}

// =============================================================================
// Tests for get_direct_remote_address / get_direct_local_address.
// =============================================================================

TEST_F(DynamicModuleListenerFilterAbiCallbackTest, GetDirectRemoteAddressUsesDirectAddress) {
  auto remote_address = Network::Utility::parseInternetAddressNoThrow("10.0.0.2", 443);
  auto direct_remote = Network::Utility::parseInternetAddressNoThrow("192.168.1.10", 15000);
  callbacks_.socket_.connection_info_provider_ =
      std::make_shared<Network::ConnectionInfoSetterImpl>(remote_address, remote_address);
  callbacks_.socket_.connection_info_provider_->setDirectRemoteAddressForTest(direct_remote);

  envoy_dynamic_module_type_envoy_buffer address_out = {nullptr, 0};
  uint32_t port_out = 0;
  bool found = envoy_dynamic_module_callback_listener_filter_get_direct_remote_address(
      filterPtr(), &address_out, &port_out);

  EXPECT_TRUE(found);
  EXPECT_EQ(direct_remote->ip()->port(), port_out);
  EXPECT_EQ(direct_remote->ip()->addressAsString(),
            std::string(address_out.ptr, address_out.length));
}

TEST_F(DynamicModuleListenerFilterAbiCallbackTest, GetDirectRemoteAddressNullCallbacks) {
  auto filter = std::make_shared<DynamicModuleListenerFilter>(filter_config_);
  filter->initializeInModuleFilter();

  envoy_dynamic_module_type_envoy_buffer address_out = {nullptr, 0};
  uint32_t port_out = 0;
  bool found = envoy_dynamic_module_callback_listener_filter_get_direct_remote_address(
      static_cast<void*>(filter.get()), &address_out, &port_out);

  EXPECT_FALSE(found);
  EXPECT_EQ(nullptr, address_out.ptr);
  EXPECT_EQ(0, address_out.length);
  EXPECT_EQ(0, port_out);
}

TEST_F(DynamicModuleListenerFilterAbiCallbackTest, GetDirectRemoteAddressNonIp) {
  Network::Address::InstanceConstSharedPtr pipe =
      *Network::Address::PipeInstance::create("/tmp/test.sock");
  callbacks_.socket_.connection_info_provider_ =
      std::make_shared<Network::ConnectionInfoSetterImpl>(pipe, pipe);

  envoy_dynamic_module_type_envoy_buffer address_out = {nullptr, 0};
  uint32_t port_out = 0;
  bool found = envoy_dynamic_module_callback_listener_filter_get_direct_remote_address(
      filterPtr(), &address_out, &port_out);

  EXPECT_FALSE(found);
  EXPECT_EQ(nullptr, address_out.ptr);
  EXPECT_EQ(0, address_out.length);
  EXPECT_EQ(0, port_out);
}

TEST_F(DynamicModuleListenerFilterAbiCallbackTest, GetDirectLocalAddressUsesDirectAddress) {
  auto local_address = Network::Utility::parseInternetAddressNoThrow("127.0.0.1", 8080);
  callbacks_.socket_.connection_info_provider_ =
      std::make_shared<Network::ConnectionInfoSetterImpl>(local_address, local_address);

  envoy_dynamic_module_type_envoy_buffer address_out = {nullptr, 0};
  uint32_t port_out = 0;
  bool found = envoy_dynamic_module_callback_listener_filter_get_direct_local_address(
      filterPtr(), &address_out, &port_out);

  EXPECT_TRUE(found);
  EXPECT_EQ(8080, port_out);
  EXPECT_EQ(local_address->ip()->addressAsString(),
            std::string(address_out.ptr, address_out.length));
}

TEST_F(DynamicModuleListenerFilterAbiCallbackTest, GetDirectLocalAddressNullCallbacks) {
  auto filter = std::make_shared<DynamicModuleListenerFilter>(filter_config_);
  filter->initializeInModuleFilter();

  envoy_dynamic_module_type_envoy_buffer address_out = {nullptr, 0};
  uint32_t port_out = 0;
  bool found = envoy_dynamic_module_callback_listener_filter_get_direct_local_address(
      static_cast<void*>(filter.get()), &address_out, &port_out);

  EXPECT_FALSE(found);
  EXPECT_EQ(nullptr, address_out.ptr);
  EXPECT_EQ(0, address_out.length);
  EXPECT_EQ(0, port_out);
}

TEST_F(DynamicModuleListenerFilterAbiCallbackTest, GetDirectLocalAddressNonIp) {
  Network::Address::InstanceConstSharedPtr pipe =
      *Network::Address::PipeInstance::create("/tmp/test.sock");
  callbacks_.socket_.connection_info_provider_ =
      std::make_shared<Network::ConnectionInfoSetterImpl>(pipe, pipe);

  envoy_dynamic_module_type_envoy_buffer address_out = {nullptr, 0};
  uint32_t port_out = 0;
  bool found = envoy_dynamic_module_callback_listener_filter_get_direct_local_address(
      filterPtr(), &address_out, &port_out);

  EXPECT_FALSE(found);
  EXPECT_EQ(nullptr, address_out.ptr);
  EXPECT_EQ(0, address_out.length);
  EXPECT_EQ(0, port_out);
}

// =============================================================================
// Tests for get_original_dst.
// =============================================================================

TEST_F(DynamicModuleListenerFilterAbiCallbackTest, GetOriginalDstReturnsFalseWhenUnavailable) {
  auto address = Network::Utility::parseInternetAddressNoThrow("10.0.0.3", 8443);
  callbacks_.socket_.connection_info_provider_ =
      std::make_shared<Network::ConnectionInfoSetterImpl>(address, address);

  envoy_dynamic_module_type_envoy_buffer address_out = {nullptr, 0};
  uint32_t port_out = 0;
  bool found = envoy_dynamic_module_callback_listener_filter_get_original_dst(
      filterPtr(), &address_out, &port_out);

  EXPECT_FALSE(found);
  EXPECT_EQ(nullptr, address_out.ptr);
  EXPECT_EQ(0, port_out);
}

TEST_F(DynamicModuleListenerFilterAbiCallbackTest, GetOriginalDstNullCallbacks) {
  auto filter = std::make_shared<DynamicModuleListenerFilter>(filter_config_);
  filter->initializeInModuleFilter();

  envoy_dynamic_module_type_envoy_buffer address_out = {nullptr, 0};
  uint32_t port_out = 0;
  bool found = envoy_dynamic_module_callback_listener_filter_get_original_dst(
      static_cast<void*>(filter.get()), &address_out, &port_out);

  EXPECT_FALSE(found);
  EXPECT_EQ(nullptr, address_out.ptr);
  EXPECT_EQ(0, address_out.length);
  EXPECT_EQ(0, port_out);
}

TEST_F(DynamicModuleListenerFilterAbiCallbackTest, GetOriginalDstNonIpAddress) {
  Network::Address::InstanceConstSharedPtr pipe =
      *Network::Address::PipeInstance::create("/tmp/test.sock");
  callbacks_.socket_.connection_info_provider_ =
      std::make_shared<Network::ConnectionInfoSetterImpl>(pipe, pipe);

  // Mock addressType to return Pipe so the non-IP check is triggered.
  ON_CALL(callbacks_.socket_, addressType())
      .WillByDefault(testing::Return(Network::Address::Type::Pipe));

  envoy_dynamic_module_type_envoy_buffer address_out = {nullptr, 0};
  uint32_t port_out = 0;
  bool found = envoy_dynamic_module_callback_listener_filter_get_original_dst(
      filterPtr(), &address_out, &port_out);

  EXPECT_FALSE(found);
  EXPECT_EQ(nullptr, address_out.ptr);
  EXPECT_EQ(0, address_out.length);
  EXPECT_EQ(0, port_out);
}

#ifdef SOL_IP
TEST_F(DynamicModuleListenerFilterAbiCallbackTest, GetOriginalDstSuccessIpv4) {
  auto address = Network::Utility::parseInternetAddressNoThrow("10.0.0.3", 8443);
  callbacks_.socket_.connection_info_provider_ =
      std::make_shared<Network::ConnectionInfoSetterImpl>(address, address);

  // Set up mock IoHandle to return a valid fd.
  auto mock_io_handle = std::make_unique<NiceMock<Network::MockIoHandle>>();
  auto* mock_io_handle_ptr = mock_io_handle.get();
  EXPECT_CALL(*mock_io_handle_ptr, fdDoNotUse()).WillRepeatedly(testing::Return(5));
  EXPECT_CALL(callbacks_.socket_, ioHandle())
      .WillRepeatedly(testing::ReturnRef(*mock_io_handle_ptr));
  callbacks_.socket_.io_handle_ = std::move(mock_io_handle);

  // Mock addressType to return IP.
  ON_CALL(callbacks_.socket_, addressType())
      .WillByDefault(testing::Return(Network::Address::Type::Ip));

  // Mock ipVersion for getOriginalDst.
  EXPECT_CALL(callbacks_.socket_, ipVersion())
      .WillRepeatedly(testing::Return(Network::Address::IpVersion::v4));

  // Mock getSocketOption to return a valid SO_ORIGINAL_DST address.
  sockaddr_storage storage;
  auto& sin = reinterpret_cast<sockaddr_in&>(storage);
  sin.sin_family = AF_INET;
  sin.sin_port = htons(9527);
  sin.sin_addr.s_addr = inet_addr("12.34.56.78");

  EXPECT_CALL(callbacks_.socket_, getSocketOption(testing::Eq(SOL_IP), testing::Eq(SO_ORIGINAL_DST),
                                                  testing::_, testing::_))
      .WillOnce(
          testing::DoAll(SetArg2Sockaddr(storage), testing::Return(Api::SysCallIntResult{0, 0})));

  envoy_dynamic_module_type_envoy_buffer address_out = {nullptr, 0};
  uint32_t port_out = 0;
  bool found = envoy_dynamic_module_callback_listener_filter_get_original_dst(
      filterPtr(), &address_out, &port_out);

  EXPECT_TRUE(found);
  EXPECT_NE(nullptr, address_out.ptr);
  EXPECT_GT(address_out.length, 0);
  EXPECT_EQ("12.34.56.78", std::string(address_out.ptr, address_out.length));
  EXPECT_EQ(9527, port_out);
}
#endif // SOL_IP

// =============================================================================
// Tests for get_address_type and is_local_address_restored.
// =============================================================================

TEST_F(DynamicModuleListenerFilterAbiCallbackTest, GetAddressTypePipe) {
  ON_CALL(callbacks_.socket_, addressType())
      .WillByDefault(testing::Return(Network::Address::Type::Pipe));
  auto type = envoy_dynamic_module_callback_listener_filter_get_address_type(filterPtr());
  EXPECT_EQ(envoy_dynamic_module_type_address_type_Pipe, type);
}

TEST_F(DynamicModuleListenerFilterAbiCallbackTest, GetAddressTypeIp) {
  ON_CALL(callbacks_.socket_, addressType())
      .WillByDefault(testing::Return(Network::Address::Type::Ip));
  auto type = envoy_dynamic_module_callback_listener_filter_get_address_type(filterPtr());
  EXPECT_EQ(envoy_dynamic_module_type_address_type_Ip, type);
}

TEST_F(DynamicModuleListenerFilterAbiCallbackTest, GetAddressTypeEnvoyInternal) {
  ON_CALL(callbacks_.socket_, addressType())
      .WillByDefault(testing::Return(Network::Address::Type::EnvoyInternal));
  auto type = envoy_dynamic_module_callback_listener_filter_get_address_type(filterPtr());
  EXPECT_EQ(envoy_dynamic_module_type_address_type_EnvoyInternal, type);
}

TEST_F(DynamicModuleListenerFilterAbiCallbackTest, GetAddressTypeNullCallbacks) {
  auto filter = std::make_shared<DynamicModuleListenerFilter>(filter_config_);
  filter->initializeInModuleFilter();

  auto type = envoy_dynamic_module_callback_listener_filter_get_address_type(
      static_cast<void*>(filter.get()));
  EXPECT_EQ(envoy_dynamic_module_type_address_type_Unknown, type);
}

TEST_F(DynamicModuleListenerFilterAbiCallbackTest, IsLocalAddressRestoredTrueAfterRestore) {
  auto local_address = Network::Utility::parseInternetAddressNoThrow("127.0.0.1", 80);
  auto restored = Network::Utility::parseInternetAddressNoThrow("127.0.0.2", 81);
  callbacks_.socket_.connection_info_provider_ =
      std::make_shared<Network::ConnectionInfoSetterImpl>(local_address, local_address);
  callbacks_.socket_.connection_info_provider_->restoreLocalAddress(restored);

  EXPECT_TRUE(envoy_dynamic_module_callback_listener_filter_is_local_address_restored(filterPtr()));
}

TEST_F(DynamicModuleListenerFilterAbiCallbackTest, IsLocalAddressRestoredFalseBeforeRestore) {
  auto local_address = Network::Utility::parseInternetAddressNoThrow("127.0.0.1", 80);
  callbacks_.socket_.connection_info_provider_ =
      std::make_shared<Network::ConnectionInfoSetterImpl>(local_address, local_address);

  EXPECT_FALSE(
      envoy_dynamic_module_callback_listener_filter_is_local_address_restored(filterPtr()));
}

TEST_F(DynamicModuleListenerFilterAbiCallbackTest, IsLocalAddressRestoredNullCallbacks) {
  auto filter = std::make_shared<DynamicModuleListenerFilter>(filter_config_);
  filter->initializeInModuleFilter();

  EXPECT_FALSE(envoy_dynamic_module_callback_listener_filter_is_local_address_restored(
      static_cast<void*>(filter.get())));
}

// =============================================================================
// Tests for set_remote_address.
// =============================================================================

TEST_F(DynamicModuleListenerFilterAbiCallbackTest, SetRemoteAddressIpv4) {
  // Set up initial connection info provider.
  auto local_address = Network::Utility::parseInternetAddressNoThrow("127.0.0.1", 80);
  callbacks_.socket_.connection_info_provider_ =
      std::make_shared<Network::ConnectionInfoSetterImpl>(local_address, local_address);

  char address[] = "10.0.0.1";
  envoy_dynamic_module_type_module_buffer addr_buf = {address, 8};
  bool result = envoy_dynamic_module_callback_listener_filter_set_remote_address(
      filterPtr(), addr_buf, 8080, false);
  EXPECT_TRUE(result);
}

TEST_F(DynamicModuleListenerFilterAbiCallbackTest, SetRemoteAddressIpv6) {
  auto local_address = Network::Utility::parseInternetAddressNoThrow("::1", 80);
  callbacks_.socket_.connection_info_provider_ =
      std::make_shared<Network::ConnectionInfoSetterImpl>(local_address, local_address);

  char address[] = "2001:db8::1";
  envoy_dynamic_module_type_module_buffer addr_buf = {address, 11};
  bool result = envoy_dynamic_module_callback_listener_filter_set_remote_address(
      filterPtr(), addr_buf, 8080, true);
  EXPECT_TRUE(result);
}

TEST_F(DynamicModuleListenerFilterAbiCallbackTest, SetRemoteAddressInvalidAddress) {
  char address[] = "invalid";
  envoy_dynamic_module_type_module_buffer addr_buf = {address, 7};
  bool result = envoy_dynamic_module_callback_listener_filter_set_remote_address(
      filterPtr(), addr_buf, 8080, false);
  EXPECT_FALSE(result);
}

TEST_F(DynamicModuleListenerFilterAbiCallbackTest, SetRemoteAddressNullCallbacks) {
  auto filter = std::make_shared<DynamicModuleListenerFilter>(filter_config_);
  filter->initializeInModuleFilter();

  char address[] = "10.0.0.1";
  envoy_dynamic_module_type_module_buffer addr_buf = {address, 8};
  bool result = envoy_dynamic_module_callback_listener_filter_set_remote_address(
      static_cast<void*>(filter.get()), addr_buf, 8080, false);
  EXPECT_FALSE(result);
}

TEST_F(DynamicModuleListenerFilterAbiCallbackTest, SetRemoteAddressNullAddress) {
  envoy_dynamic_module_type_module_buffer addr_buf = {nullptr, 8};
  bool result = envoy_dynamic_module_callback_listener_filter_set_remote_address(
      filterPtr(), addr_buf, 8080, false);
  EXPECT_FALSE(result);
}

// =============================================================================
// Tests for restore_local_address.
// =============================================================================

TEST_F(DynamicModuleListenerFilterAbiCallbackTest, RestoreLocalAddressIpv4) {
  auto remote_address = Network::Utility::parseInternetAddressNoThrow("192.168.1.1", 80);
  callbacks_.socket_.connection_info_provider_ =
      std::make_shared<Network::ConnectionInfoSetterImpl>(remote_address, remote_address);

  char address[] = "10.0.0.1";
  envoy_dynamic_module_type_module_buffer addr_buf = {address, 8};
  bool result = envoy_dynamic_module_callback_listener_filter_restore_local_address(
      filterPtr(), addr_buf, 9090, false);
  EXPECT_TRUE(result);
}

TEST_F(DynamicModuleListenerFilterAbiCallbackTest, RestoreLocalAddressInvalidAddress) {
  char address[] = "not_an_ip";
  envoy_dynamic_module_type_module_buffer addr_buf = {address, 9};
  bool result = envoy_dynamic_module_callback_listener_filter_restore_local_address(
      filterPtr(), addr_buf, 9090, false);
  EXPECT_FALSE(result);
}

TEST_F(DynamicModuleListenerFilterAbiCallbackTest, RestoreLocalAddressNullCallbacks) {
  auto filter = std::make_shared<DynamicModuleListenerFilter>(filter_config_);
  filter->initializeInModuleFilter();

  char address[] = "10.0.0.1";
  envoy_dynamic_module_type_module_buffer addr_buf = {address, 8};
  bool result = envoy_dynamic_module_callback_listener_filter_restore_local_address(
      static_cast<void*>(filter.get()), addr_buf, 9090, false);
  EXPECT_FALSE(result);
}

// =============================================================================
// Tests for continue_filter_chain.
// =============================================================================

TEST_F(DynamicModuleListenerFilterAbiCallbackTest, ContinueFilterChainSuccess) {
  EXPECT_CALL(callbacks_, continueFilterChain(true));
  envoy_dynamic_module_callback_listener_filter_continue_filter_chain(filterPtr(), true);
}

TEST_F(DynamicModuleListenerFilterAbiCallbackTest, ContinueFilterChainFailure) {
  EXPECT_CALL(callbacks_, continueFilterChain(false));
  envoy_dynamic_module_callback_listener_filter_continue_filter_chain(filterPtr(), false);
}

TEST_F(DynamicModuleListenerFilterAbiCallbackTest, ContinueFilterChainNullCallbacks) {
  auto filter = std::make_shared<DynamicModuleListenerFilter>(filter_config_);
  filter->initializeInModuleFilter();

  // Should not crash.
  envoy_dynamic_module_callback_listener_filter_continue_filter_chain(
      static_cast<void*>(filter.get()), true);
}

TEST_F(DynamicModuleListenerFilterAbiCallbackTest, UseOriginalDst) {
  EXPECT_CALL(callbacks_, useOriginalDst(true));
  envoy_dynamic_module_callback_listener_filter_use_original_dst(filterPtr(), true);
}

TEST_F(DynamicModuleListenerFilterAbiCallbackTest, UseOriginalDstNullCallbacks) {
  auto filter = std::make_shared<DynamicModuleListenerFilter>(filter_config_);
  filter->initializeInModuleFilter();

  envoy_dynamic_module_callback_listener_filter_use_original_dst(static_cast<void*>(filter.get()),
                                                                 false);
}

// =============================================================================
// Tests for close_socket.
// =============================================================================

TEST_F(DynamicModuleListenerFilterAbiCallbackTest, CloseSocketNullCallbacks) {
  auto filter = std::make_shared<DynamicModuleListenerFilter>(filter_config_);
  filter->initializeInModuleFilter();

  // Should not crash.
  envoy_dynamic_module_callback_listener_filter_close_socket(static_cast<void*>(filter.get()));
}

// =============================================================================
// Tests for set_dynamic_metadata.
// =============================================================================

TEST_F(DynamicModuleListenerFilterAbiCallbackTest, SetDynamicMetadata) {
  EXPECT_CALL(callbacks_, setDynamicMetadata(std::string("test_ns"), testing::_));

  char ns[] = "test_ns";
  char key[] = "my_key";
  char value[] = "my_value";
  envoy_dynamic_module_type_module_buffer ns_buf = {ns, 7};
  envoy_dynamic_module_type_module_buffer key_buf = {key, 6};
  envoy_dynamic_module_type_module_buffer value_buf = {value, 8};
  envoy_dynamic_module_callback_listener_filter_set_dynamic_metadata(filterPtr(), ns_buf, key_buf,
                                                                     value_buf);
}

TEST_F(DynamicModuleListenerFilterAbiCallbackTest, SetDynamicMetadataNullCallbacks) {
  auto filter = std::make_shared<DynamicModuleListenerFilter>(filter_config_);
  filter->initializeInModuleFilter();

  char ns[] = "test_ns";
  char key[] = "my_key";
  char value[] = "my_value";
  envoy_dynamic_module_type_module_buffer ns_buf = {ns, 7};
  envoy_dynamic_module_type_module_buffer key_buf = {key, 6};
  envoy_dynamic_module_type_module_buffer value_buf = {value, 8};
  // Should not crash.
  envoy_dynamic_module_callback_listener_filter_set_dynamic_metadata(
      static_cast<void*>(filter.get()), ns_buf, key_buf, value_buf);
}

TEST_F(DynamicModuleListenerFilterAbiCallbackTest, SetDynamicMetadataNullNamespace) {
  char key[] = "my_key";
  char value[] = "my_value";
  envoy_dynamic_module_type_module_buffer ns_buf = {nullptr, 7};
  envoy_dynamic_module_type_module_buffer key_buf = {key, 6};
  envoy_dynamic_module_type_module_buffer value_buf = {value, 8};
  // Should not crash with null namespace.
  envoy_dynamic_module_callback_listener_filter_set_dynamic_metadata(filterPtr(), ns_buf, key_buf,
                                                                     value_buf);
}

TEST_F(DynamicModuleListenerFilterAbiCallbackTest, SetDynamicMetadataNullKey) {
  char ns[] = "test_ns";
  char value[] = "my_value";
  envoy_dynamic_module_type_module_buffer ns_buf = {ns, 7};
  envoy_dynamic_module_type_module_buffer key_buf = {nullptr, 6};
  envoy_dynamic_module_type_module_buffer value_buf = {value, 8};
  // Should not crash with null key.
  envoy_dynamic_module_callback_listener_filter_set_dynamic_metadata(filterPtr(), ns_buf, key_buf,
                                                                     value_buf);
}

TEST_F(DynamicModuleListenerFilterAbiCallbackTest, SetDynamicMetadataNullValue) {
  char ns[] = "test_ns";
  char key[] = "my_key";
  envoy_dynamic_module_type_module_buffer ns_buf = {ns, 7};
  envoy_dynamic_module_type_module_buffer key_buf = {key, 6};
  envoy_dynamic_module_type_module_buffer value_buf = {nullptr, 8};
  // Should not crash with null value.
  envoy_dynamic_module_callback_listener_filter_set_dynamic_metadata(filterPtr(), ns_buf, key_buf,
                                                                     value_buf);
}

// =============================================================================
// Tests for set_filter_state.
// =============================================================================

TEST_F(DynamicModuleListenerFilterAbiCallbackTest, SetFilterState) {
  char key[] = "my_state_key";
  char value[] = "my_state_value";
  envoy_dynamic_module_type_module_buffer key_buf = {key, 12};
  envoy_dynamic_module_type_module_buffer value_buf = {value, 14};
  envoy_dynamic_module_callback_listener_filter_set_filter_state(filterPtr(), key_buf, value_buf);

  // Verify the state was set by retrieving it.
  envoy_dynamic_module_type_envoy_buffer result_buf = {nullptr, 0};
  bool found = envoy_dynamic_module_callback_listener_filter_get_filter_state(filterPtr(), key_buf,
                                                                              &result_buf);
  EXPECT_TRUE(found);
  EXPECT_EQ(14, result_buf.length);
  EXPECT_NE(nullptr, result_buf.ptr);
  EXPECT_EQ("my_state_value", std::string(result_buf.ptr, result_buf.length));
}

TEST_F(DynamicModuleListenerFilterAbiCallbackTest, SetFilterStateNullCallbacks) {
  auto filter = std::make_shared<DynamicModuleListenerFilter>(filter_config_);
  filter->initializeInModuleFilter();

  char key[] = "my_state_key";
  char value[] = "my_state_value";
  envoy_dynamic_module_type_module_buffer key_buf = {key, 12};
  envoy_dynamic_module_type_module_buffer value_buf = {value, 14};
  // Should not crash.
  envoy_dynamic_module_callback_listener_filter_set_filter_state(static_cast<void*>(filter.get()),
                                                                 key_buf, value_buf);
}

TEST_F(DynamicModuleListenerFilterAbiCallbackTest, SetFilterStateNullKey) {
  char value[] = "my_state_value";
  envoy_dynamic_module_type_module_buffer key_buf = {nullptr, 12};
  envoy_dynamic_module_type_module_buffer value_buf = {value, 14};
  // Should not crash with null key.
  envoy_dynamic_module_callback_listener_filter_set_filter_state(filterPtr(), key_buf, value_buf);
}

TEST_F(DynamicModuleListenerFilterAbiCallbackTest, SetFilterStateNullValue) {
  char key[] = "my_state_key";
  envoy_dynamic_module_type_module_buffer key_buf = {key, 12};
  envoy_dynamic_module_type_module_buffer value_buf = {nullptr, 14};
  // Should not crash with null value.
  envoy_dynamic_module_callback_listener_filter_set_filter_state(filterPtr(), key_buf, value_buf);
}

// =============================================================================
// Tests for get_filter_state.
// =============================================================================

TEST_F(DynamicModuleListenerFilterAbiCallbackTest, GetFilterStateExisting) {
  // First set a state.
  char key[] = "test_key";
  char value[] = "test_value";
  envoy_dynamic_module_type_module_buffer key_buf = {key, 8};
  envoy_dynamic_module_type_module_buffer value_buf = {value, 10};
  envoy_dynamic_module_callback_listener_filter_set_filter_state(filterPtr(), key_buf, value_buf);

  // Now retrieve it.
  envoy_dynamic_module_type_envoy_buffer result_buf = {nullptr, 0};
  bool found = envoy_dynamic_module_callback_listener_filter_get_filter_state(filterPtr(), key_buf,
                                                                              &result_buf);
  EXPECT_TRUE(found);
  EXPECT_EQ(10, result_buf.length);
  EXPECT_NE(nullptr, result_buf.ptr);
  EXPECT_EQ("test_value", std::string(result_buf.ptr, result_buf.length));
}

TEST_F(DynamicModuleListenerFilterAbiCallbackTest, GetFilterStateNonExisting) {
  char key[] = "nonexistent_key";
  envoy_dynamic_module_type_module_buffer key_buf = {key, 15};
  envoy_dynamic_module_type_envoy_buffer result_buf = {nullptr, 0};
  bool found = envoy_dynamic_module_callback_listener_filter_get_filter_state(filterPtr(), key_buf,
                                                                              &result_buf);
  EXPECT_FALSE(found);
  EXPECT_EQ(0, result_buf.length);
  EXPECT_EQ(nullptr, result_buf.ptr);
}

TEST_F(DynamicModuleListenerFilterAbiCallbackTest, GetFilterStateNullCallbacks) {
  auto filter = std::make_shared<DynamicModuleListenerFilter>(filter_config_);
  filter->initializeInModuleFilter();

  char key[] = "test_key";
  envoy_dynamic_module_type_module_buffer key_buf = {key, 8};
  envoy_dynamic_module_type_envoy_buffer result_buf = {nullptr, 0};
  bool found = envoy_dynamic_module_callback_listener_filter_get_filter_state(
      static_cast<void*>(filter.get()), key_buf, &result_buf);
  EXPECT_FALSE(found);
  EXPECT_EQ(0, result_buf.length);
  EXPECT_EQ(nullptr, result_buf.ptr);
}

TEST_F(DynamicModuleListenerFilterAbiCallbackTest, GetFilterStateNullKey) {
  envoy_dynamic_module_type_module_buffer key_buf = {nullptr, 8};
  envoy_dynamic_module_type_envoy_buffer result_buf = {nullptr, 0};
  bool found = envoy_dynamic_module_callback_listener_filter_get_filter_state(filterPtr(), key_buf,
                                                                              &result_buf);
  EXPECT_FALSE(found);
  EXPECT_EQ(0, result_buf.length);
  EXPECT_EQ(nullptr, result_buf.ptr);
}

// =============================================================================
// Tests for stream info helpers.
// =============================================================================

TEST_F(DynamicModuleListenerFilterAbiCallbackTest, SetDownstreamTransportFailureReason) {
  EXPECT_CALL(callbacks_.stream_info_,
              setDownstreamTransportFailureReason(absl::string_view("tls_error")));

  char reason[] = "tls_error";
  envoy_dynamic_module_type_module_buffer reason_buf = {reason, 9};
  envoy_dynamic_module_callback_listener_filter_set_downstream_transport_failure_reason(filterPtr(),
                                                                                        reason_buf);
}

TEST_F(DynamicModuleListenerFilterAbiCallbackTest, SetDownstreamTransportFailureReasonNull) {
  envoy_dynamic_module_type_module_buffer reason_buf = {nullptr, 5};
  envoy_dynamic_module_callback_listener_filter_set_downstream_transport_failure_reason(filterPtr(),
                                                                                        reason_buf);
}

TEST_F(DynamicModuleListenerFilterAbiCallbackTest,
       SetDownstreamTransportFailureReasonNullCallbacks) {
  auto filter = std::make_shared<DynamicModuleListenerFilter>(filter_config_);
  filter->initializeInModuleFilter();

  char reason[] = "tls_error";
  envoy_dynamic_module_type_module_buffer reason_buf = {reason, 9};
  envoy_dynamic_module_callback_listener_filter_set_downstream_transport_failure_reason(
      static_cast<void*>(filter.get()), reason_buf);
}

TEST_F(DynamicModuleListenerFilterAbiCallbackTest, GetConnectionStartTimeMs) {
  const std::chrono::system_clock::time_point start_time =
      std::chrono::system_clock::from_time_t(123);
  EXPECT_CALL(callbacks_.stream_info_, startTime()).WillOnce(testing::Return(start_time));

  const uint64_t millis =
      envoy_dynamic_module_callback_listener_filter_get_connection_start_time_ms(filterPtr());
  EXPECT_EQ(123000, millis);
}

TEST_F(DynamicModuleListenerFilterAbiCallbackTest, GetConnectionStartTimeMsNullCallbacks) {
  auto filter = std::make_shared<DynamicModuleListenerFilter>(filter_config_);
  filter->initializeInModuleFilter();

  const uint64_t millis =
      envoy_dynamic_module_callback_listener_filter_get_connection_start_time_ms(
          static_cast<void*>(filter.get()));
  EXPECT_EQ(0, millis);
}

} // namespace ListenerFilters
} // namespace DynamicModules
} // namespace Extensions
} // namespace Envoy
