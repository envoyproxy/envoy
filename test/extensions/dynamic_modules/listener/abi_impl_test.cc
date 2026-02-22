#include <chrono>
#include <vector>

#include "source/common/http/message_impl.h"
#include "source/common/network/address_impl.h"
#include "source/common/network/io_socket_error_impl.h"
#include "source/common/router/string_accessor_impl.h"
#include "source/common/stats/isolated_store_impl.h"
#include "source/extensions/dynamic_modules/abi/abi.h"
#include "source/extensions/filters/listener/dynamic_modules/filter.h"

#include "test/extensions/dynamic_modules/util.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/network/io_handle.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/ssl/mocks.h"
#include "test/mocks/tracing/mocks.h"
#include "test/mocks/upstream/cluster_manager.h"
#include "test/mocks/upstream/host.h"

#include "gmock/gmock.h"

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

    auto filter_config_or_status = newDynamicModuleListenerFilterConfig(
        "test_filter", "", DefaultMetricsNamespace, std::move(dynamic_module.value()),
        cluster_manager_, *stats_.rootScope(), main_thread_dispatcher_);
    EXPECT_TRUE(filter_config_or_status.ok()) << filter_config_or_status.status().message();
    filter_config_ = filter_config_or_status.value();

    ON_CALL(callbacks_, dispatcher()).WillByDefault(testing::ReturnRef(worker_thread_dispatcher_));

    filter_ = std::make_shared<DynamicModuleListenerFilter>(filter_config_);
    filter_->onAccept(callbacks_);
  }

  void TearDown() override { filter_.reset(); }

  void* filterPtr() { return static_cast<void*>(filter_.get()); }

  Stats::IsolatedStoreImpl stats_;
  NiceMock<Upstream::MockClusterManager> cluster_manager_;
  DynamicModuleListenerFilterConfigSharedPtr filter_config_;
  std::shared_ptr<DynamicModuleListenerFilter> filter_;
  NiceMock<Network::MockListenerFilterCallbacks> callbacks_;
  NiceMock<Event::MockDispatcher> main_thread_dispatcher_;
  NiceMock<Event::MockDispatcher> worker_thread_dispatcher_{"worker_0"};
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
  filter->onAccept(callbacks_);
  filter->setCallbacksForTest(nullptr);
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
  filter->onAccept(callbacks_);
  filter->setCallbacksForTest(nullptr);

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
  filter->onAccept(callbacks_);
  filter->setCallbacksForTest(nullptr);

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
  filter->onAccept(callbacks_);
  filter->setCallbacksForTest(nullptr);

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
  filter->onAccept(callbacks_);
  filter->setCallbacksForTest(nullptr);

  char hash[] = "def456";
  envoy_dynamic_module_type_module_buffer hash_buf = {hash, 6};
  envoy_dynamic_module_callback_listener_filter_set_ja4_hash(static_cast<void*>(filter.get()),
                                                             hash_buf);
}

// =============================================================================
// Tests for get_requested_server_name.
// =============================================================================

TEST_F(DynamicModuleListenerFilterAbiCallbackTest, GetRequestedServerName) {
  EXPECT_CALL(callbacks_.socket_, requestedServerName())
      .WillOnce(testing::Return(absl::string_view("example.com")));

  envoy_dynamic_module_type_envoy_buffer result = {nullptr, 0};
  bool ok =
      envoy_dynamic_module_callback_listener_filter_get_requested_server_name(filterPtr(), &result);
  EXPECT_TRUE(ok);
  EXPECT_EQ(11, result.length);
  EXPECT_EQ("example.com", std::string(result.ptr, result.length));
}

TEST_F(DynamicModuleListenerFilterAbiCallbackTest, GetRequestedServerNameEmpty) {
  EXPECT_CALL(callbacks_.socket_, requestedServerName())
      .WillOnce(testing::Return(absl::string_view("")));

  envoy_dynamic_module_type_envoy_buffer result = {nullptr, 0};
  bool ok =
      envoy_dynamic_module_callback_listener_filter_get_requested_server_name(filterPtr(), &result);
  EXPECT_FALSE(ok);
  EXPECT_EQ(nullptr, result.ptr);
  EXPECT_EQ(0, result.length);
}

TEST_F(DynamicModuleListenerFilterAbiCallbackTest, GetRequestedServerNameNullCallbacks) {
  auto filter = std::make_shared<DynamicModuleListenerFilter>(filter_config_);
  filter->onAccept(callbacks_);
  filter->setCallbacksForTest(nullptr);

  envoy_dynamic_module_type_envoy_buffer result = {nullptr, 0};
  bool ok = envoy_dynamic_module_callback_listener_filter_get_requested_server_name(
      static_cast<void*>(filter.get()), &result);
  EXPECT_FALSE(ok);
  EXPECT_EQ(nullptr, result.ptr);
  EXPECT_EQ(0, result.length);
}

// =============================================================================
// Tests for get_detected_transport_protocol.
// =============================================================================

TEST_F(DynamicModuleListenerFilterAbiCallbackTest, GetDetectedTransportProtocol) {
  EXPECT_CALL(callbacks_.socket_, detectedTransportProtocol())
      .WillOnce(testing::Return(absl::string_view("tls")));

  envoy_dynamic_module_type_envoy_buffer result = {nullptr, 0};
  bool ok = envoy_dynamic_module_callback_listener_filter_get_detected_transport_protocol(
      filterPtr(), &result);
  EXPECT_TRUE(ok);
  EXPECT_EQ(3, result.length);
  EXPECT_EQ("tls", std::string(result.ptr, result.length));
}

TEST_F(DynamicModuleListenerFilterAbiCallbackTest, GetDetectedTransportProtocolEmpty) {
  EXPECT_CALL(callbacks_.socket_, detectedTransportProtocol())
      .WillOnce(testing::Return(absl::string_view("")));

  envoy_dynamic_module_type_envoy_buffer result = {nullptr, 0};
  bool ok = envoy_dynamic_module_callback_listener_filter_get_detected_transport_protocol(
      filterPtr(), &result);
  EXPECT_FALSE(ok);
  EXPECT_EQ(nullptr, result.ptr);
  EXPECT_EQ(0, result.length);
}

TEST_F(DynamicModuleListenerFilterAbiCallbackTest, GetDetectedTransportProtocolNullCallbacks) {
  auto filter = std::make_shared<DynamicModuleListenerFilter>(filter_config_);
  filter->onAccept(callbacks_);
  filter->setCallbacksForTest(nullptr);

  envoy_dynamic_module_type_envoy_buffer result = {nullptr, 0};
  bool ok = envoy_dynamic_module_callback_listener_filter_get_detected_transport_protocol(
      static_cast<void*>(filter.get()), &result);
  EXPECT_FALSE(ok);
  EXPECT_EQ(nullptr, result.ptr);
  EXPECT_EQ(0, result.length);
}

// =============================================================================
// Tests for get_requested_application_protocols.
// =============================================================================

TEST_F(DynamicModuleListenerFilterAbiCallbackTest, GetRequestedApplicationProtocols) {
  std::vector<std::string> protocols = {"h2", "http/1.1"};
  EXPECT_CALL(callbacks_.socket_, requestedApplicationProtocols())
      .WillRepeatedly(testing::ReturnRef(protocols));

  size_t size =
      envoy_dynamic_module_callback_listener_filter_get_requested_application_protocols_size(
          filterPtr());
  EXPECT_EQ(2, size);

  std::vector<envoy_dynamic_module_type_envoy_buffer> out(size, {nullptr, 0});
  bool ok = envoy_dynamic_module_callback_listener_filter_get_requested_application_protocols(
      filterPtr(), out.data());
  EXPECT_TRUE(ok);
  EXPECT_EQ("h2", std::string(out[0].ptr, out[0].length));
  EXPECT_EQ("http/1.1", std::string(out[1].ptr, out[1].length));
}

TEST_F(DynamicModuleListenerFilterAbiCallbackTest, GetRequestedApplicationProtocolsEmpty) {
  std::vector<std::string> protocols;
  EXPECT_CALL(callbacks_.socket_, requestedApplicationProtocols())
      .WillOnce(testing::ReturnRef(protocols));

  size_t size =
      envoy_dynamic_module_callback_listener_filter_get_requested_application_protocols_size(
          filterPtr());
  EXPECT_EQ(0, size);
}

TEST_F(DynamicModuleListenerFilterAbiCallbackTest,
       GetRequestedApplicationProtocolsSizeNullCallbacks) {
  auto filter = std::make_shared<DynamicModuleListenerFilter>(filter_config_);
  filter->onAccept(callbacks_);
  filter->setCallbacksForTest(nullptr);

  size_t size =
      envoy_dynamic_module_callback_listener_filter_get_requested_application_protocols_size(
          static_cast<void*>(filter.get()));
  EXPECT_EQ(0, size);
}

TEST_F(DynamicModuleListenerFilterAbiCallbackTest, GetRequestedApplicationProtocolsNullCallbacks) {
  auto filter = std::make_shared<DynamicModuleListenerFilter>(filter_config_);
  filter->onAccept(callbacks_);
  filter->setCallbacksForTest(nullptr);

  envoy_dynamic_module_type_envoy_buffer out = {nullptr, 0};
  bool ok = envoy_dynamic_module_callback_listener_filter_get_requested_application_protocols(
      static_cast<void*>(filter.get()), &out);
  EXPECT_FALSE(ok);
}

// =============================================================================
// Tests for `get_ja3_hash`.
// =============================================================================

TEST_F(DynamicModuleListenerFilterAbiCallbackTest, GetJa3Hash) {
  EXPECT_CALL(callbacks_.socket_, ja3Hash()).WillOnce(testing::Return(absl::string_view("abc123")));

  envoy_dynamic_module_type_envoy_buffer result = {nullptr, 0};
  bool ok = envoy_dynamic_module_callback_listener_filter_get_ja3_hash(filterPtr(), &result);
  EXPECT_TRUE(ok);
  EXPECT_EQ(6, result.length);
  EXPECT_EQ("abc123", std::string(result.ptr, result.length));
}

TEST_F(DynamicModuleListenerFilterAbiCallbackTest, GetJa3HashEmpty) {
  EXPECT_CALL(callbacks_.socket_, ja3Hash()).WillOnce(testing::Return(absl::string_view("")));

  envoy_dynamic_module_type_envoy_buffer result = {nullptr, 0};
  bool ok = envoy_dynamic_module_callback_listener_filter_get_ja3_hash(filterPtr(), &result);
  EXPECT_FALSE(ok);
  EXPECT_EQ(nullptr, result.ptr);
  EXPECT_EQ(0, result.length);
}

TEST_F(DynamicModuleListenerFilterAbiCallbackTest, GetJa3HashNullCallbacks) {
  auto filter = std::make_shared<DynamicModuleListenerFilter>(filter_config_);
  filter->onAccept(callbacks_);
  filter->setCallbacksForTest(nullptr);

  envoy_dynamic_module_type_envoy_buffer result = {nullptr, 0};
  bool ok = envoy_dynamic_module_callback_listener_filter_get_ja3_hash(
      static_cast<void*>(filter.get()), &result);
  EXPECT_FALSE(ok);
  EXPECT_EQ(nullptr, result.ptr);
  EXPECT_EQ(0, result.length);
}

// =============================================================================
// Tests for `get_ja4_hash`.
// =============================================================================

TEST_F(DynamicModuleListenerFilterAbiCallbackTest, GetJa4Hash) {
  EXPECT_CALL(callbacks_.socket_, ja4Hash()).WillOnce(testing::Return(absl::string_view("def456")));

  envoy_dynamic_module_type_envoy_buffer result = {nullptr, 0};
  bool ok = envoy_dynamic_module_callback_listener_filter_get_ja4_hash(filterPtr(), &result);
  EXPECT_TRUE(ok);
  EXPECT_EQ(6, result.length);
  EXPECT_EQ("def456", std::string(result.ptr, result.length));
}

TEST_F(DynamicModuleListenerFilterAbiCallbackTest, GetJa4HashEmpty) {
  EXPECT_CALL(callbacks_.socket_, ja4Hash()).WillOnce(testing::Return(absl::string_view("")));

  envoy_dynamic_module_type_envoy_buffer result = {nullptr, 0};
  bool ok = envoy_dynamic_module_callback_listener_filter_get_ja4_hash(filterPtr(), &result);
  EXPECT_FALSE(ok);
  EXPECT_EQ(nullptr, result.ptr);
  EXPECT_EQ(0, result.length);
}

TEST_F(DynamicModuleListenerFilterAbiCallbackTest, GetJa4HashNullCallbacks) {
  auto filter = std::make_shared<DynamicModuleListenerFilter>(filter_config_);
  filter->onAccept(callbacks_);
  filter->setCallbacksForTest(nullptr);

  envoy_dynamic_module_type_envoy_buffer result = {nullptr, 0};
  bool ok = envoy_dynamic_module_callback_listener_filter_get_ja4_hash(
      static_cast<void*>(filter.get()), &result);
  EXPECT_FALSE(ok);
  EXPECT_EQ(nullptr, result.ptr);
  EXPECT_EQ(0, result.length);
}

// =============================================================================
// Tests for is_ssl.
// =============================================================================

TEST_F(DynamicModuleListenerFilterAbiCallbackTest, IsSslTrue) {
  auto ssl = std::make_shared<NiceMock<Ssl::MockConnectionInfo>>();
  auto address = Network::Utility::parseInternetAddressNoThrow("1.2.3.4", 8080);
  callbacks_.socket_.connection_info_provider_ =
      std::make_shared<Network::ConnectionInfoSetterImpl>(address, address);
  callbacks_.socket_.connection_info_provider_->setSslConnection(ssl);

  bool result = envoy_dynamic_module_callback_listener_filter_is_ssl(filterPtr());
  EXPECT_TRUE(result);
}

TEST_F(DynamicModuleListenerFilterAbiCallbackTest, IsSslFalse) {
  auto address = Network::Utility::parseInternetAddressNoThrow("1.2.3.4", 8080);
  callbacks_.socket_.connection_info_provider_ =
      std::make_shared<Network::ConnectionInfoSetterImpl>(address, address);

  bool result = envoy_dynamic_module_callback_listener_filter_is_ssl(filterPtr());
  EXPECT_FALSE(result);
}

TEST_F(DynamicModuleListenerFilterAbiCallbackTest, IsSslNullCallbacks) {
  auto filter = std::make_shared<DynamicModuleListenerFilter>(filter_config_);
  filter->onAccept(callbacks_);
  filter->setCallbacksForTest(nullptr);

  bool result =
      envoy_dynamic_module_callback_listener_filter_is_ssl(static_cast<void*>(filter.get()));
  EXPECT_FALSE(result);
}

// =============================================================================
// Tests for get_ssl_uri_sans.
// =============================================================================

TEST_F(DynamicModuleListenerFilterAbiCallbackTest, GetSslUriSans) {
  auto ssl = std::make_shared<NiceMock<Ssl::MockConnectionInfo>>();
  std::vector<std::string> sans = {"spiffe://example.com/sa", "spiffe://example.com/sb"};
  EXPECT_CALL(*ssl, uriSanPeerCertificate())
      .WillRepeatedly(testing::Return(absl::Span<const std::string>(sans)));

  auto address = Network::Utility::parseInternetAddressNoThrow("1.2.3.4", 8080);
  callbacks_.socket_.connection_info_provider_ =
      std::make_shared<Network::ConnectionInfoSetterImpl>(address, address);
  callbacks_.socket_.connection_info_provider_->setSslConnection(ssl);

  size_t size = envoy_dynamic_module_callback_listener_filter_get_ssl_uri_sans_size(filterPtr());
  EXPECT_EQ(2, size);

  std::vector<envoy_dynamic_module_type_envoy_buffer> out(size, {nullptr, 0});
  bool ok = envoy_dynamic_module_callback_listener_filter_get_ssl_uri_sans(filterPtr(), out.data());
  EXPECT_TRUE(ok);
  EXPECT_EQ("spiffe://example.com/sa", std::string(out[0].ptr, out[0].length));
  EXPECT_EQ("spiffe://example.com/sb", std::string(out[1].ptr, out[1].length));
}

TEST_F(DynamicModuleListenerFilterAbiCallbackTest, GetSslUriSansEmpty) {
  auto ssl = std::make_shared<NiceMock<Ssl::MockConnectionInfo>>();
  std::vector<std::string> sans;
  EXPECT_CALL(*ssl, uriSanPeerCertificate())
      .WillRepeatedly(testing::Return(absl::Span<const std::string>(sans)));

  auto address = Network::Utility::parseInternetAddressNoThrow("1.2.3.4", 8080);
  callbacks_.socket_.connection_info_provider_ =
      std::make_shared<Network::ConnectionInfoSetterImpl>(address, address);
  callbacks_.socket_.connection_info_provider_->setSslConnection(ssl);

  size_t size = envoy_dynamic_module_callback_listener_filter_get_ssl_uri_sans_size(filterPtr());
  EXPECT_EQ(0, size);
}

TEST_F(DynamicModuleListenerFilterAbiCallbackTest, GetSslUriSansNoSsl) {
  auto address = Network::Utility::parseInternetAddressNoThrow("1.2.3.4", 8080);
  callbacks_.socket_.connection_info_provider_ =
      std::make_shared<Network::ConnectionInfoSetterImpl>(address, address);

  size_t size = envoy_dynamic_module_callback_listener_filter_get_ssl_uri_sans_size(filterPtr());
  EXPECT_EQ(0, size);

  envoy_dynamic_module_type_envoy_buffer out = {nullptr, 0};
  bool ok = envoy_dynamic_module_callback_listener_filter_get_ssl_uri_sans(filterPtr(), &out);
  EXPECT_FALSE(ok);
}

TEST_F(DynamicModuleListenerFilterAbiCallbackTest, GetSslUriSansNullCallbacks) {
  auto filter = std::make_shared<DynamicModuleListenerFilter>(filter_config_);
  filter->onAccept(callbacks_);
  filter->setCallbacksForTest(nullptr);

  size_t size = envoy_dynamic_module_callback_listener_filter_get_ssl_uri_sans_size(
      static_cast<void*>(filter.get()));
  EXPECT_EQ(0, size);

  envoy_dynamic_module_type_envoy_buffer out = {nullptr, 0};
  bool ok = envoy_dynamic_module_callback_listener_filter_get_ssl_uri_sans(
      static_cast<void*>(filter.get()), &out);
  EXPECT_FALSE(ok);
}

// =============================================================================
// Tests for get_ssl_dns_sans.
// =============================================================================

TEST_F(DynamicModuleListenerFilterAbiCallbackTest, GetSslDnsSans) {
  auto ssl = std::make_shared<NiceMock<Ssl::MockConnectionInfo>>();
  std::vector<std::string> sans = {"example.com", "www.example.com"};
  EXPECT_CALL(*ssl, dnsSansPeerCertificate())
      .WillRepeatedly(testing::Return(absl::Span<const std::string>(sans)));

  auto address = Network::Utility::parseInternetAddressNoThrow("1.2.3.4", 8080);
  callbacks_.socket_.connection_info_provider_ =
      std::make_shared<Network::ConnectionInfoSetterImpl>(address, address);
  callbacks_.socket_.connection_info_provider_->setSslConnection(ssl);

  size_t size = envoy_dynamic_module_callback_listener_filter_get_ssl_dns_sans_size(filterPtr());
  EXPECT_EQ(2, size);

  std::vector<envoy_dynamic_module_type_envoy_buffer> out(size, {nullptr, 0});
  bool ok = envoy_dynamic_module_callback_listener_filter_get_ssl_dns_sans(filterPtr(), out.data());
  EXPECT_TRUE(ok);
  EXPECT_EQ("example.com", std::string(out[0].ptr, out[0].length));
  EXPECT_EQ("www.example.com", std::string(out[1].ptr, out[1].length));
}

TEST_F(DynamicModuleListenerFilterAbiCallbackTest, GetSslDnsSansEmpty) {
  auto ssl = std::make_shared<NiceMock<Ssl::MockConnectionInfo>>();
  std::vector<std::string> sans;
  EXPECT_CALL(*ssl, dnsSansPeerCertificate())
      .WillRepeatedly(testing::Return(absl::Span<const std::string>(sans)));

  auto address = Network::Utility::parseInternetAddressNoThrow("1.2.3.4", 8080);
  callbacks_.socket_.connection_info_provider_ =
      std::make_shared<Network::ConnectionInfoSetterImpl>(address, address);
  callbacks_.socket_.connection_info_provider_->setSslConnection(ssl);

  size_t size = envoy_dynamic_module_callback_listener_filter_get_ssl_dns_sans_size(filterPtr());
  EXPECT_EQ(0, size);
}

TEST_F(DynamicModuleListenerFilterAbiCallbackTest, GetSslDnsSansNoSsl) {
  auto address = Network::Utility::parseInternetAddressNoThrow("1.2.3.4", 8080);
  callbacks_.socket_.connection_info_provider_ =
      std::make_shared<Network::ConnectionInfoSetterImpl>(address, address);

  size_t size = envoy_dynamic_module_callback_listener_filter_get_ssl_dns_sans_size(filterPtr());
  EXPECT_EQ(0, size);

  envoy_dynamic_module_type_envoy_buffer out = {nullptr, 0};
  bool ok = envoy_dynamic_module_callback_listener_filter_get_ssl_dns_sans(filterPtr(), &out);
  EXPECT_FALSE(ok);
}

TEST_F(DynamicModuleListenerFilterAbiCallbackTest, GetSslDnsSansNullCallbacks) {
  auto filter = std::make_shared<DynamicModuleListenerFilter>(filter_config_);
  filter->onAccept(callbacks_);
  filter->setCallbacksForTest(nullptr);

  size_t size = envoy_dynamic_module_callback_listener_filter_get_ssl_dns_sans_size(
      static_cast<void*>(filter.get()));
  EXPECT_EQ(0, size);

  envoy_dynamic_module_type_envoy_buffer out = {nullptr, 0};
  bool ok = envoy_dynamic_module_callback_listener_filter_get_ssl_dns_sans(
      static_cast<void*>(filter.get()), &out);
  EXPECT_FALSE(ok);
}

// =============================================================================
// Tests for get_ssl_subject.
// =============================================================================

TEST_F(DynamicModuleListenerFilterAbiCallbackTest, GetSslSubject) {
  auto ssl = std::make_shared<NiceMock<Ssl::MockConnectionInfo>>();
  std::string subject = "CN=example.com";
  EXPECT_CALL(*ssl, subjectPeerCertificate()).WillOnce(testing::ReturnRef(subject));

  auto address = Network::Utility::parseInternetAddressNoThrow("1.2.3.4", 8080);
  callbacks_.socket_.connection_info_provider_ =
      std::make_shared<Network::ConnectionInfoSetterImpl>(address, address);
  callbacks_.socket_.connection_info_provider_->setSslConnection(ssl);

  envoy_dynamic_module_type_envoy_buffer result = {nullptr, 0};
  bool ok = envoy_dynamic_module_callback_listener_filter_get_ssl_subject(filterPtr(), &result);
  EXPECT_TRUE(ok);
  EXPECT_EQ("CN=example.com", std::string(result.ptr, result.length));
}

TEST_F(DynamicModuleListenerFilterAbiCallbackTest, GetSslSubjectNoSsl) {
  auto address = Network::Utility::parseInternetAddressNoThrow("1.2.3.4", 8080);
  callbacks_.socket_.connection_info_provider_ =
      std::make_shared<Network::ConnectionInfoSetterImpl>(address, address);

  envoy_dynamic_module_type_envoy_buffer result = {nullptr, 0};
  bool ok = envoy_dynamic_module_callback_listener_filter_get_ssl_subject(filterPtr(), &result);
  EXPECT_FALSE(ok);
  EXPECT_EQ(nullptr, result.ptr);
  EXPECT_EQ(0, result.length);
}

TEST_F(DynamicModuleListenerFilterAbiCallbackTest, GetSslSubjectNullCallbacks) {
  auto filter = std::make_shared<DynamicModuleListenerFilter>(filter_config_);
  filter->onAccept(callbacks_);
  filter->setCallbacksForTest(nullptr);

  envoy_dynamic_module_type_envoy_buffer result = {nullptr, 0};
  bool ok = envoy_dynamic_module_callback_listener_filter_get_ssl_subject(
      static_cast<void*>(filter.get()), &result);
  EXPECT_FALSE(ok);
  EXPECT_EQ(nullptr, result.ptr);
  EXPECT_EQ(0, result.length);
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
  filter->onAccept(callbacks_);
  filter->setCallbacksForTest(nullptr);

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
  filter->onAccept(callbacks_);
  filter->setCallbacksForTest(nullptr);

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
  filter->onAccept(callbacks_);
  filter->setCallbacksForTest(nullptr);

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
  filter->onAccept(callbacks_);
  filter->setCallbacksForTest(nullptr);

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
  filter->onAccept(callbacks_);
  filter->setCallbacksForTest(nullptr);

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
  filter->onAccept(callbacks_);
  filter->setCallbacksForTest(nullptr);

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
  filter->onAccept(callbacks_);
  filter->setCallbacksForTest(nullptr);

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
  filter->onAccept(callbacks_);
  filter->setCallbacksForTest(nullptr);

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
  filter->onAccept(callbacks_);
  filter->setCallbacksForTest(nullptr);

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
  filter->onAccept(callbacks_);
  filter->setCallbacksForTest(nullptr);

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
  filter->onAccept(callbacks_);
  filter->setCallbacksForTest(nullptr);

  envoy_dynamic_module_callback_listener_filter_use_original_dst(static_cast<void*>(filter.get()),
                                                                 false);
}

// =============================================================================
// Tests for close_socket.
// =============================================================================

TEST_F(DynamicModuleListenerFilterAbiCallbackTest, CloseSocketWithDetails) {
  NiceMock<Network::MockIoHandle> io_handle;
  EXPECT_CALL(callbacks_.socket_, ioHandle()).WillOnce(testing::ReturnRef(io_handle));
  EXPECT_CALL(io_handle, close())
      .WillOnce(testing::Return(testing::ByMove(Api::IoCallUint64Result(0, Api::IoError::none()))));
  EXPECT_CALL(callbacks_.stream_info_,
              setConnectionTerminationDetails(absl::string_view("connection_rejected")));

  char details[] = "connection_rejected";
  envoy_dynamic_module_type_module_buffer details_buf = {details, 19};
  envoy_dynamic_module_callback_listener_filter_close_socket(filterPtr(), details_buf);
}

TEST_F(DynamicModuleListenerFilterAbiCallbackTest, CloseSocketEmptyDetails) {
  NiceMock<Network::MockIoHandle> io_handle;
  EXPECT_CALL(callbacks_.socket_, ioHandle()).WillOnce(testing::ReturnRef(io_handle));
  EXPECT_CALL(io_handle, close())
      .WillOnce(testing::Return(testing::ByMove(Api::IoCallUint64Result(0, Api::IoError::none()))));
  // Empty details should not call setConnectionTerminationDetails.
  EXPECT_CALL(callbacks_.stream_info_, setConnectionTerminationDetails(testing::_)).Times(0);

  envoy_dynamic_module_type_module_buffer details_buf = {nullptr, 0};
  envoy_dynamic_module_callback_listener_filter_close_socket(filterPtr(), details_buf);
}

TEST_F(DynamicModuleListenerFilterAbiCallbackTest, CloseSocketNullCallbacks) {
  auto filter = std::make_shared<DynamicModuleListenerFilter>(filter_config_);
  filter->onAccept(callbacks_);
  filter->setCallbacksForTest(nullptr);

  char details[] = "connection_rejected";
  envoy_dynamic_module_type_module_buffer details_buf = {details, 19};
  // Should not crash.
  envoy_dynamic_module_callback_listener_filter_close_socket(static_cast<void*>(filter.get()),
                                                             details_buf);
}

// =============================================================================
// Tests for write_to_socket.
// =============================================================================

TEST_F(DynamicModuleListenerFilterAbiCallbackTest, WriteToSocketSuccess) {
  NiceMock<Network::MockIoHandle> io_handle;
  EXPECT_CALL(callbacks_.socket_, ioHandle()).WillOnce(testing::ReturnRef(io_handle));
  EXPECT_CALL(io_handle, write(testing::_))
      .WillOnce(testing::Invoke([](Buffer::Instance& buffer) -> Api::IoCallUint64Result {
        uint64_t len = buffer.length();
        buffer.drain(len);
        return Api::IoCallUint64Result(len, Api::IoError::none());
      }));

  char data[] = "S";
  envoy_dynamic_module_type_module_buffer data_buf = {data, 1};
  int64_t result =
      envoy_dynamic_module_callback_listener_filter_write_to_socket(filterPtr(), data_buf);
  EXPECT_EQ(1, result);
}

TEST_F(DynamicModuleListenerFilterAbiCallbackTest, WriteToSocketMultipleBytes) {
  NiceMock<Network::MockIoHandle> io_handle;
  EXPECT_CALL(callbacks_.socket_, ioHandle()).WillOnce(testing::ReturnRef(io_handle));
  EXPECT_CALL(io_handle, write(testing::_))
      .WillOnce(testing::Invoke([](Buffer::Instance& buffer) -> Api::IoCallUint64Result {
        uint64_t len = buffer.length();
        buffer.drain(len);
        return Api::IoCallUint64Result(len, Api::IoError::none());
      }));

  char data[] = "hello world";
  envoy_dynamic_module_type_module_buffer data_buf = {data, 11};
  int64_t result =
      envoy_dynamic_module_callback_listener_filter_write_to_socket(filterPtr(), data_buf);
  EXPECT_EQ(11, result);
}

TEST_F(DynamicModuleListenerFilterAbiCallbackTest, WriteToSocketNullData) {
  envoy_dynamic_module_type_module_buffer data_buf = {nullptr, 5};
  int64_t result =
      envoy_dynamic_module_callback_listener_filter_write_to_socket(filterPtr(), data_buf);
  EXPECT_EQ(-1, result);
}

TEST_F(DynamicModuleListenerFilterAbiCallbackTest, WriteToSocketZeroLength) {
  char data[] = "hello";
  envoy_dynamic_module_type_module_buffer data_buf = {data, 0};
  int64_t result =
      envoy_dynamic_module_callback_listener_filter_write_to_socket(filterPtr(), data_buf);
  EXPECT_EQ(-1, result);
}

TEST_F(DynamicModuleListenerFilterAbiCallbackTest, WriteToSocketNullCallbacks) {
  auto filter = std::make_shared<DynamicModuleListenerFilter>(filter_config_);
  filter->onAccept(callbacks_);
  filter->setCallbacksForTest(nullptr);

  char data[] = "S";
  envoy_dynamic_module_type_module_buffer data_buf = {data, 1};
  int64_t result = envoy_dynamic_module_callback_listener_filter_write_to_socket(
      static_cast<void*>(filter.get()), data_buf);
  EXPECT_EQ(-1, result);
}

TEST_F(DynamicModuleListenerFilterAbiCallbackTest, WriteToSocketIoError) {
  NiceMock<Network::MockIoHandle> io_handle;
  EXPECT_CALL(callbacks_.socket_, ioHandle()).WillOnce(testing::ReturnRef(io_handle));
  EXPECT_CALL(io_handle, write(testing::_))
      .WillOnce(testing::Invoke([](Buffer::Instance&) -> Api::IoCallUint64Result {
        return Api::IoCallUint64Result(0, Network::IoSocketError::create(ECONNRESET));
      }));

  char data[] = "S";
  envoy_dynamic_module_type_module_buffer data_buf = {data, 1};
  int64_t result =
      envoy_dynamic_module_callback_listener_filter_write_to_socket(filterPtr(), data_buf);
  EXPECT_EQ(-1, result);
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
  envoy_dynamic_module_callback_listener_filter_set_dynamic_metadata_string(filterPtr(), ns_buf,
                                                                            key_buf, value_buf);
}

TEST_F(DynamicModuleListenerFilterAbiCallbackTest, SetDynamicMetadataNullCallbacks) {
  auto filter = std::make_shared<DynamicModuleListenerFilter>(filter_config_);
  filter->onAccept(callbacks_);
  filter->setCallbacksForTest(nullptr);

  char ns[] = "test_ns";
  char key[] = "my_key";
  char value[] = "my_value";
  envoy_dynamic_module_type_module_buffer ns_buf = {ns, 7};
  envoy_dynamic_module_type_module_buffer key_buf = {key, 6};
  envoy_dynamic_module_type_module_buffer value_buf = {value, 8};
  // Should not crash.
  envoy_dynamic_module_callback_listener_filter_set_dynamic_metadata_string(
      static_cast<void*>(filter.get()), ns_buf, key_buf, value_buf);
}

TEST_F(DynamicModuleListenerFilterAbiCallbackTest, SetDynamicMetadataNullNamespace) {
  char key[] = "my_key";
  char value[] = "my_value";
  envoy_dynamic_module_type_module_buffer ns_buf = {nullptr, 7};
  envoy_dynamic_module_type_module_buffer key_buf = {key, 6};
  envoy_dynamic_module_type_module_buffer value_buf = {value, 8};
  // Should not crash with null namespace.
  envoy_dynamic_module_callback_listener_filter_set_dynamic_metadata_string(filterPtr(), ns_buf,
                                                                            key_buf, value_buf);
}

TEST_F(DynamicModuleListenerFilterAbiCallbackTest, SetDynamicMetadataNullKey) {
  char ns[] = "test_ns";
  char value[] = "my_value";
  envoy_dynamic_module_type_module_buffer ns_buf = {ns, 7};
  envoy_dynamic_module_type_module_buffer key_buf = {nullptr, 6};
  envoy_dynamic_module_type_module_buffer value_buf = {value, 8};
  // Should not crash with null key.
  envoy_dynamic_module_callback_listener_filter_set_dynamic_metadata_string(filterPtr(), ns_buf,
                                                                            key_buf, value_buf);
}

TEST_F(DynamicModuleListenerFilterAbiCallbackTest, SetDynamicMetadataNullValue) {
  char ns[] = "test_ns";
  char key[] = "my_key";
  envoy_dynamic_module_type_module_buffer ns_buf = {ns, 7};
  envoy_dynamic_module_type_module_buffer key_buf = {key, 6};
  envoy_dynamic_module_type_module_buffer value_buf = {nullptr, 8};
  // Should not crash with null value.
  envoy_dynamic_module_callback_listener_filter_set_dynamic_metadata_string(filterPtr(), ns_buf,
                                                                            key_buf, value_buf);
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
  filter->onAccept(callbacks_);
  filter->setCallbacksForTest(nullptr);

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
  filter->onAccept(callbacks_);
  filter->setCallbacksForTest(nullptr);

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
  filter->onAccept(callbacks_);
  filter->setCallbacksForTest(nullptr);

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
  filter->onAccept(callbacks_);
  filter->setCallbacksForTest(nullptr);

  const uint64_t millis =
      envoy_dynamic_module_callback_listener_filter_get_connection_start_time_ms(
          static_cast<void*>(filter.get()));
  EXPECT_EQ(0, millis);
}

// =============================================================================
// Tests for get_dynamic_metadata and set_dynamic_typed_metadata.
// =============================================================================

TEST_F(DynamicModuleListenerFilterAbiCallbackTest, GetDynamicMetadataSuccess) {
  envoy::config::core::v3::Metadata metadata;
  (*metadata.mutable_filter_metadata())["test_ns"]
      .mutable_fields()
      ->operator[]("key1")
      .set_string_value("value1");

  EXPECT_CALL(callbacks_, dynamicMetadata()).WillRepeatedly(testing::ReturnRef(metadata));

  char ns[] = "test_ns";
  char key[] = "key1";
  envoy_dynamic_module_type_module_buffer ns_buf = {ns, 7};
  envoy_dynamic_module_type_module_buffer key_buf = {key, 4};
  envoy_dynamic_module_type_envoy_buffer value_out = {nullptr, 0};

  bool found = envoy_dynamic_module_callback_listener_filter_get_dynamic_metadata_string(
      filterPtr(), ns_buf, key_buf, &value_out);

  EXPECT_TRUE(found);
  EXPECT_NE(nullptr, value_out.ptr);
  EXPECT_EQ("value1", std::string(value_out.ptr, value_out.length));
}

TEST_F(DynamicModuleListenerFilterAbiCallbackTest, GetDynamicMetadataNamespaceNotFound) {
  envoy::config::core::v3::Metadata metadata;
  EXPECT_CALL(callbacks_, dynamicMetadata()).WillRepeatedly(testing::ReturnRef(metadata));

  char ns[] = "missing_ns";
  char key[] = "key1";
  envoy_dynamic_module_type_module_buffer ns_buf = {ns, 10};
  envoy_dynamic_module_type_module_buffer key_buf = {key, 4};
  envoy_dynamic_module_type_envoy_buffer value_out = {nullptr, 0};

  bool found = envoy_dynamic_module_callback_listener_filter_get_dynamic_metadata_string(
      filterPtr(), ns_buf, key_buf, &value_out);

  EXPECT_FALSE(found);
  EXPECT_EQ(nullptr, value_out.ptr);
  EXPECT_EQ(0, value_out.length);
}

TEST_F(DynamicModuleListenerFilterAbiCallbackTest, GetDynamicMetadataKeyNotFound) {
  envoy::config::core::v3::Metadata metadata;
  (*metadata.mutable_filter_metadata())["test_ns"]
      .mutable_fields()
      ->operator[]("key1")
      .set_string_value("value1");

  EXPECT_CALL(callbacks_, dynamicMetadata()).WillRepeatedly(testing::ReturnRef(metadata));

  char ns[] = "test_ns";
  char key[] = "missing_key";
  envoy_dynamic_module_type_module_buffer ns_buf = {ns, 7};
  envoy_dynamic_module_type_module_buffer key_buf = {key, 11};
  envoy_dynamic_module_type_envoy_buffer value_out = {nullptr, 0};

  bool found = envoy_dynamic_module_callback_listener_filter_get_dynamic_metadata_string(
      filterPtr(), ns_buf, key_buf, &value_out);

  EXPECT_FALSE(found);
  EXPECT_EQ(nullptr, value_out.ptr);
  EXPECT_EQ(0, value_out.length);
}

TEST_F(DynamicModuleListenerFilterAbiCallbackTest, GetDynamicMetadataNullCallbacks) {
  auto filter = std::make_shared<DynamicModuleListenerFilter>(filter_config_);
  filter->onAccept(callbacks_);
  filter->setCallbacksForTest(nullptr);

  char ns[] = "test_ns";
  char key[] = "key1";
  envoy_dynamic_module_type_module_buffer ns_buf = {ns, 7};
  envoy_dynamic_module_type_module_buffer key_buf = {key, 4};
  envoy_dynamic_module_type_envoy_buffer value_out = {nullptr, 0};

  bool found = envoy_dynamic_module_callback_listener_filter_get_dynamic_metadata_string(
      static_cast<void*>(filter.get()), ns_buf, key_buf, &value_out);

  EXPECT_FALSE(found);
  EXPECT_EQ(nullptr, value_out.ptr);
  EXPECT_EQ(0, value_out.length);
}

TEST_F(DynamicModuleListenerFilterAbiCallbackTest, SetDynamicTypedMetadataSuccess) {
  char ns[] = "test_ns";
  char key[] = "key1";
  char value[] = "value1";
  envoy_dynamic_module_type_module_buffer ns_buf = {ns, 7};
  envoy_dynamic_module_type_module_buffer key_buf = {key, 4};
  envoy_dynamic_module_type_module_buffer value_buf = {value, 6};

  EXPECT_CALL(callbacks_, setDynamicMetadata(testing::_, testing::_));

  envoy_dynamic_module_callback_listener_filter_set_dynamic_metadata_string(filterPtr(), ns_buf,
                                                                            key_buf, value_buf);
}

TEST_F(DynamicModuleListenerFilterAbiCallbackTest, SetDynamicTypedMetadataNullCallbacks) {
  auto filter = std::make_shared<DynamicModuleListenerFilter>(filter_config_);
  filter->onAccept(callbacks_);
  filter->setCallbacksForTest(nullptr);

  char ns[] = "test_ns";
  char key[] = "key1";
  char value[] = "value1";
  envoy_dynamic_module_type_module_buffer ns_buf = {ns, 7};
  envoy_dynamic_module_type_module_buffer key_buf = {key, 4};
  envoy_dynamic_module_type_module_buffer value_buf = {value, 6};

  // TODO(wbpcode): this should never happen in practice, but ensure it doesn't crash.
  envoy_dynamic_module_callback_listener_filter_set_dynamic_metadata_string(
      static_cast<void*>(filter.get()), ns_buf, key_buf, value_buf);
}

// =============================================================================
// Tests for max_read_bytes.
// =============================================================================

TEST_F(DynamicModuleListenerFilterAbiCallbackTest, MaxReadBytes) {
  const size_t max_bytes =
      envoy_dynamic_module_callback_listener_filter_max_read_bytes(filterPtr());
  // The default maxReadBytes() implementation returns 0, but filters can override it.
  EXPECT_EQ(0, max_bytes);
}

// =============================================================================
// Tests for scheduler callbacks.
// =============================================================================

TEST_F(DynamicModuleListenerFilterAbiCallbackTest, ListenerFilterSchedulerNewDelete) {
  // Set up the dispatcher for the filter.
  NiceMock<Event::MockDispatcher> worker_dispatcher;
  EXPECT_CALL(callbacks_, dispatcher()).WillRepeatedly(testing::ReturnRef(worker_dispatcher));

  auto* scheduler = envoy_dynamic_module_callback_listener_filter_scheduler_new(filterPtr());
  EXPECT_NE(nullptr, scheduler);

  envoy_dynamic_module_callback_listener_filter_scheduler_delete(scheduler);
}

TEST_F(DynamicModuleListenerFilterAbiCallbackTest, ListenerFilterSchedulerCommit) {
  // Set up the dispatcher for the filter.
  NiceMock<Event::MockDispatcher> worker_dispatcher;
  EXPECT_CALL(callbacks_, dispatcher()).WillRepeatedly(testing::ReturnRef(worker_dispatcher));

  auto* scheduler = envoy_dynamic_module_callback_listener_filter_scheduler_new(filterPtr());
  EXPECT_NE(nullptr, scheduler);

  // Expect the callback to be posted.
  EXPECT_CALL(worker_dispatcher, post(_));

  envoy_dynamic_module_callback_listener_filter_scheduler_commit(scheduler, 123);

  // Clean up.
  envoy_dynamic_module_callback_listener_filter_scheduler_delete(scheduler);
}

TEST_F(DynamicModuleListenerFilterAbiCallbackTest, ListenerFilterConfigSchedulerNewDelete) {
  auto* scheduler = envoy_dynamic_module_callback_listener_filter_config_scheduler_new(
      static_cast<void*>(filter_config_.get()));
  EXPECT_NE(nullptr, scheduler);

  envoy_dynamic_module_callback_listener_filter_config_scheduler_delete(scheduler);
}

TEST_F(DynamicModuleListenerFilterAbiCallbackTest, ListenerFilterConfigSchedulerCommit) {
  auto* scheduler = envoy_dynamic_module_callback_listener_filter_config_scheduler_new(
      static_cast<void*>(filter_config_.get()));
  EXPECT_NE(nullptr, scheduler);

  // Expect the callback to be posted.
  EXPECT_CALL(main_thread_dispatcher_, post(_));

  envoy_dynamic_module_callback_listener_filter_config_scheduler_commit(scheduler, 456);

  // Clean up.
  envoy_dynamic_module_callback_listener_filter_config_scheduler_delete(scheduler);
}

TEST_F(DynamicModuleListenerFilterAbiCallbackTest,
       ListenerFilterSchedulerCommitInvokesOnScheduled) {
  // Set up the dispatcher for the filter.
  NiceMock<Event::MockDispatcher> worker_dispatcher;
  EXPECT_CALL(callbacks_, dispatcher()).WillRepeatedly(testing::ReturnRef(worker_dispatcher));

  auto* scheduler = envoy_dynamic_module_callback_listener_filter_scheduler_new(filterPtr());
  EXPECT_NE(nullptr, scheduler);

  // Capture the posted callback and invoke it to verify onScheduled is called.
  Event::PostCb captured_cb;
  EXPECT_CALL(worker_dispatcher, post(_)).WillOnce(testing::Invoke([&](Event::PostCb cb) {
    captured_cb = std::move(cb);
  }));

  envoy_dynamic_module_callback_listener_filter_scheduler_commit(scheduler, 789);

  // Invoke the captured callback to simulate the dispatcher running the event.
  // This should call filter_->onScheduled(789), which invokes the module's on_scheduled hook.
  // Since the no_op module's on_scheduled is a no-op, we just verify it doesn't crash.
  captured_cb();

  // Clean up.
  envoy_dynamic_module_callback_listener_filter_scheduler_delete(scheduler);
}

TEST_F(DynamicModuleListenerFilterAbiCallbackTest,
       ListenerFilterConfigSchedulerCommitInvokesOnScheduled) {
  auto* scheduler = envoy_dynamic_module_callback_listener_filter_config_scheduler_new(
      static_cast<void*>(filter_config_.get()));
  EXPECT_NE(nullptr, scheduler);

  // Capture the posted callback and invoke it to verify onScheduled is called.
  Event::PostCb captured_cb;
  EXPECT_CALL(main_thread_dispatcher_, post(_)).WillOnce(testing::Invoke([&](Event::PostCb cb) {
    captured_cb = std::move(cb);
  }));

  envoy_dynamic_module_callback_listener_filter_config_scheduler_commit(scheduler, 999);

  // Invoke the captured callback to simulate the dispatcher running the event.
  // This should call filter_config_->onScheduled(999), which invokes the module's
  // on_config_scheduled hook. Since the no_op module's hook is a no-op, we just verify it doesn't
  // crash.
  captured_cb();

  // Clean up.
  envoy_dynamic_module_callback_listener_filter_config_scheduler_delete(scheduler);
}

TEST_F(DynamicModuleListenerFilterAbiCallbackTest,
       ListenerFilterSchedulerCommitAfterFilterDestroyedDoesNotCrash) {
  // Set up the dispatcher for the filter.
  NiceMock<Event::MockDispatcher> worker_dispatcher;
  EXPECT_CALL(callbacks_, dispatcher()).WillRepeatedly(testing::ReturnRef(worker_dispatcher));

  auto* scheduler = envoy_dynamic_module_callback_listener_filter_scheduler_new(filterPtr());
  EXPECT_NE(nullptr, scheduler);

  // Capture the posted callback.
  Event::PostCb captured_cb;
  EXPECT_CALL(worker_dispatcher, post(_)).WillOnce(testing::Invoke([&](Event::PostCb cb) {
    captured_cb = std::move(cb);
  }));

  envoy_dynamic_module_callback_listener_filter_scheduler_commit(scheduler, 123);

  // Destroy the filter before invoking the callback.
  filter_.reset();

  // Invoke the captured callback - should not crash because the scheduler holds a weak_ptr.
  captured_cb();

  // Clean up.
  envoy_dynamic_module_callback_listener_filter_scheduler_delete(scheduler);
}

TEST_F(DynamicModuleListenerFilterAbiCallbackTest,
       ListenerFilterConfigSchedulerCommitAfterConfigDestroyedDoesNotCrash) {
  auto* scheduler = envoy_dynamic_module_callback_listener_filter_config_scheduler_new(
      static_cast<void*>(filter_config_.get()));
  EXPECT_NE(nullptr, scheduler);

  // Capture the posted callback.
  Event::PostCb captured_cb;
  EXPECT_CALL(main_thread_dispatcher_, post(_)).WillOnce(testing::Invoke([&](Event::PostCb cb) {
    captured_cb = std::move(cb);
  }));

  envoy_dynamic_module_callback_listener_filter_config_scheduler_commit(scheduler, 456);

  // Destroy the filter and config before invoking the callback.
  filter_.reset();
  filter_config_.reset();

  // Invoke the captured callback - should not crash because the scheduler holds a weak_ptr.
  captured_cb();

  // Clean up.
  envoy_dynamic_module_callback_listener_filter_config_scheduler_delete(scheduler);
}

// =============================================================================
// Tests for socket option callbacks.
// =============================================================================

TEST_F(DynamicModuleListenerFilterAbiCallbackTest, GetSocketFdSuccess) {
  NiceMock<Network::MockIoHandle> io_handle;
  EXPECT_CALL(callbacks_.socket_, ioHandle()).WillOnce(testing::ReturnRef(io_handle));
  EXPECT_CALL(io_handle, fdDoNotUse()).WillOnce(testing::Return(42));

  int64_t fd = envoy_dynamic_module_callback_listener_filter_get_socket_fd(filterPtr());
  EXPECT_EQ(42, fd);
}

TEST_F(DynamicModuleListenerFilterAbiCallbackTest, GetSocketFdNullCallbacks) {
  auto filter = std::make_shared<DynamicModuleListenerFilter>(filter_config_);
  filter->onAccept(callbacks_);
  filter->setCallbacksForTest(nullptr);
  // Callbacks not set.

  int64_t fd =
      envoy_dynamic_module_callback_listener_filter_get_socket_fd(static_cast<void*>(filter.get()));
  EXPECT_EQ(-1, fd);
}

TEST_F(DynamicModuleListenerFilterAbiCallbackTest, SetSocketOptionIntSuccess) {
  Api::SysCallIntResult success_result{0, 0};
  EXPECT_CALL(callbacks_.socket_, setSocketOption(1, 2, testing::_, sizeof(int)))
      .WillOnce(testing::Return(success_result));

  bool result =
      envoy_dynamic_module_callback_listener_filter_set_socket_option_int(filterPtr(), 1, 2, 123);
  EXPECT_TRUE(result);
}

TEST_F(DynamicModuleListenerFilterAbiCallbackTest, SetSocketOptionIntFailure) {
  Api::SysCallIntResult fail_result{-1, EINVAL};
  EXPECT_CALL(callbacks_.socket_, setSocketOption(1, 2, testing::_, sizeof(int)))
      .WillOnce(testing::Return(fail_result));

  bool result =
      envoy_dynamic_module_callback_listener_filter_set_socket_option_int(filterPtr(), 1, 2, 123);
  EXPECT_FALSE(result);
}

TEST_F(DynamicModuleListenerFilterAbiCallbackTest, SetSocketOptionIntNullCallbacks) {
  auto filter = std::make_shared<DynamicModuleListenerFilter>(filter_config_);
  filter->onAccept(callbacks_);
  filter->setCallbacksForTest(nullptr);
  // Callbacks not set.

  bool result = envoy_dynamic_module_callback_listener_filter_set_socket_option_int(
      static_cast<void*>(filter.get()), 1, 2, 123);
  EXPECT_FALSE(result);
}

TEST_F(DynamicModuleListenerFilterAbiCallbackTest, SetSocketOptionBytesSuccess) {
  Api::SysCallIntResult success_result{0, 0};
  EXPECT_CALL(callbacks_.socket_, setSocketOption(1, 2, testing::_, 5))
      .WillOnce(testing::Return(success_result));

  char value[] = "hello";
  envoy_dynamic_module_type_module_buffer value_buf = {value, 5};
  bool result = envoy_dynamic_module_callback_listener_filter_set_socket_option_bytes(
      filterPtr(), 1, 2, value_buf);
  EXPECT_TRUE(result);
}

TEST_F(DynamicModuleListenerFilterAbiCallbackTest, SetSocketOptionBytesFailure) {
  Api::SysCallIntResult fail_result{-1, EINVAL};
  EXPECT_CALL(callbacks_.socket_, setSocketOption(1, 2, testing::_, 5))
      .WillOnce(testing::Return(fail_result));

  char value[] = "hello";
  envoy_dynamic_module_type_module_buffer value_buf = {value, 5};
  bool result = envoy_dynamic_module_callback_listener_filter_set_socket_option_bytes(
      filterPtr(), 1, 2, value_buf);
  EXPECT_FALSE(result);
}

TEST_F(DynamicModuleListenerFilterAbiCallbackTest, SetSocketOptionBytesNullValue) {
  envoy_dynamic_module_type_module_buffer value_buf = {nullptr, 5};
  bool result = envoy_dynamic_module_callback_listener_filter_set_socket_option_bytes(
      filterPtr(), 1, 2, value_buf);
  EXPECT_FALSE(result);
}

TEST_F(DynamicModuleListenerFilterAbiCallbackTest, SetSocketOptionBytesNullCallbacks) {
  auto filter = std::make_shared<DynamicModuleListenerFilter>(filter_config_);
  filter->onAccept(callbacks_);
  filter->setCallbacksForTest(nullptr);
  // Callbacks not set.

  char value[] = "hello";
  envoy_dynamic_module_type_module_buffer value_buf = {value, 5};
  bool result = envoy_dynamic_module_callback_listener_filter_set_socket_option_bytes(
      static_cast<void*>(filter.get()), 1, 2, value_buf);
  EXPECT_FALSE(result);
}

TEST_F(DynamicModuleListenerFilterAbiCallbackTest, GetSocketOptionIntSuccess) {
  Api::SysCallIntResult success_result{0, 0};
  EXPECT_CALL(callbacks_.socket_, getSocketOption(1, 2, testing::_, testing::_))
      .WillOnce(
          testing::DoAll(testing::WithArg<2>([](void* optval) { *static_cast<int*>(optval) = 42; }),
                         testing::Return(success_result)));

  int64_t value_out = 0;
  bool result = envoy_dynamic_module_callback_listener_filter_get_socket_option_int(filterPtr(), 1,
                                                                                    2, &value_out);
  EXPECT_TRUE(result);
  EXPECT_EQ(42, value_out);
}

TEST_F(DynamicModuleListenerFilterAbiCallbackTest, GetSocketOptionIntFailure) {
  Api::SysCallIntResult fail_result{-1, ENOPROTOOPT};
  EXPECT_CALL(callbacks_.socket_, getSocketOption(1, 2, testing::_, testing::_))
      .WillOnce(testing::Return(fail_result));

  int64_t value_out = 0;
  bool result = envoy_dynamic_module_callback_listener_filter_get_socket_option_int(filterPtr(), 1,
                                                                                    2, &value_out);
  EXPECT_FALSE(result);
}

TEST_F(DynamicModuleListenerFilterAbiCallbackTest, GetSocketOptionIntNullOut) {
  bool result = envoy_dynamic_module_callback_listener_filter_get_socket_option_int(filterPtr(), 1,
                                                                                    2, nullptr);
  EXPECT_FALSE(result);
}

TEST_F(DynamicModuleListenerFilterAbiCallbackTest, GetSocketOptionIntNullCallbacks) {
  auto filter = std::make_shared<DynamicModuleListenerFilter>(filter_config_);
  filter->onAccept(callbacks_);
  filter->setCallbacksForTest(nullptr);
  // Callbacks not set.

  int64_t value_out = 0;
  bool result = envoy_dynamic_module_callback_listener_filter_get_socket_option_int(
      static_cast<void*>(filter.get()), 1, 2, &value_out);
  EXPECT_FALSE(result);
}

TEST_F(DynamicModuleListenerFilterAbiCallbackTest, GetSocketOptionBytesSuccess) {
  Api::SysCallIntResult success_result{0, 0};
  EXPECT_CALL(callbacks_.socket_, getSocketOption(1, 2, testing::_, testing::_))
      .WillOnce(testing::DoAll(testing::Invoke([](int, int, void* optval, socklen_t* optlen) {
                                 const char* data = "test";
                                 memcpy(optval, data, 4);
                                 *optlen = 4;
                               }),
                               testing::Return(success_result)));

  char buffer[16] = {0};
  size_t actual_size = 0;
  bool result = envoy_dynamic_module_callback_listener_filter_get_socket_option_bytes(
      filterPtr(), 1, 2, buffer, sizeof(buffer), &actual_size);
  EXPECT_TRUE(result);
  EXPECT_EQ(4, actual_size);
  EXPECT_EQ("test", std::string(buffer, actual_size));
}

TEST_F(DynamicModuleListenerFilterAbiCallbackTest, GetSocketOptionBytesFailure) {
  Api::SysCallIntResult fail_result{-1, ENOPROTOOPT};
  EXPECT_CALL(callbacks_.socket_, getSocketOption(1, 2, testing::_, testing::_))
      .WillOnce(testing::Return(fail_result));

  char buffer[16] = {0};
  size_t actual_size = 0;
  bool result = envoy_dynamic_module_callback_listener_filter_get_socket_option_bytes(
      filterPtr(), 1, 2, buffer, sizeof(buffer), &actual_size);
  EXPECT_FALSE(result);
}

TEST_F(DynamicModuleListenerFilterAbiCallbackTest, GetSocketOptionBytesNullBuffer) {
  size_t actual_size = 0;
  bool result = envoy_dynamic_module_callback_listener_filter_get_socket_option_bytes(
      filterPtr(), 1, 2, nullptr, 16, &actual_size);
  EXPECT_FALSE(result);
}

TEST_F(DynamicModuleListenerFilterAbiCallbackTest, GetSocketOptionBytesNullActualSize) {
  char buffer[16] = {0};
  bool result = envoy_dynamic_module_callback_listener_filter_get_socket_option_bytes(
      filterPtr(), 1, 2, buffer, sizeof(buffer), nullptr);
  EXPECT_FALSE(result);
}

TEST_F(DynamicModuleListenerFilterAbiCallbackTest, GetSocketOptionBytesNullCallbacks) {
  auto filter = std::make_shared<DynamicModuleListenerFilter>(filter_config_);
  filter->onAccept(callbacks_);
  filter->setCallbacksForTest(nullptr);
  // Callbacks not set.

  char buffer[16] = {0};
  size_t actual_size = 0;
  bool result = envoy_dynamic_module_callback_listener_filter_get_socket_option_bytes(
      static_cast<void*>(filter.get()), 1, 2, buffer, sizeof(buffer), &actual_size);
  EXPECT_FALSE(result);
}

TEST_F(DynamicModuleListenerFilterAbiCallbackTest, GetWorkerIndex) {
  auto filter = std::make_shared<DynamicModuleListenerFilter>(filter_config_);
  filter->onAccept(callbacks_);
  filter->setCallbacksForTest(nullptr);
  // Callbacks not set.

  uint32_t worker_index =
      envoy_dynamic_module_callback_listener_filter_get_worker_index(filterPtr());
  EXPECT_EQ(0u, worker_index);
}

// =============================================================================
// Tests for HTTP callouts.
// =============================================================================

class DynamicModuleListenerFilterHttpCalloutTest : public testing::Test {
public:
  void SetUp() override {
    auto dynamic_module = newDynamicModule(testSharedObjectPath("listener_no_op", "c"), false);
    EXPECT_TRUE(dynamic_module.ok()) << dynamic_module.status().message();

    auto filter_config_or_status = newDynamicModuleListenerFilterConfig(
        "test_filter", "", DefaultMetricsNamespace, std::move(dynamic_module.value()),
        cluster_manager_, *stats_.rootScope(), main_thread_dispatcher_);
    EXPECT_TRUE(filter_config_or_status.ok()) << filter_config_or_status.status().message();
    filter_config_ = filter_config_or_status.value();

    ON_CALL(callbacks_, dispatcher()).WillByDefault(testing::ReturnRef(worker_thread_dispatcher_));

    filter_ = std::make_shared<DynamicModuleListenerFilter>(filter_config_);
    filter_->onAccept(callbacks_);
  }

  void TearDown() override { filter_.reset(); }

  void* filterPtr() { return static_cast<void*>(filter_.get()); }

  Stats::IsolatedStoreImpl stats_;
  NiceMock<Upstream::MockClusterManager> cluster_manager_;
  NiceMock<Event::MockDispatcher> main_thread_dispatcher_;
  DynamicModuleListenerFilterConfigSharedPtr filter_config_;
  std::shared_ptr<DynamicModuleListenerFilter> filter_;
  NiceMock<Network::MockListenerFilterCallbacks> callbacks_;
  NiceMock<Event::MockDispatcher> worker_thread_dispatcher_{"worker_0"};
};

TEST_F(DynamicModuleListenerFilterHttpCalloutTest, SendHttpCalloutClusterNotFound) {
  EXPECT_CALL(cluster_manager_, getThreadLocalCluster("nonexistent_cluster"))
      .WillOnce(testing::Return(nullptr));

  uint64_t callout_id = 0;
  std::vector<envoy_dynamic_module_type_module_http_header> headers = {
      {.key_ptr = ":method", .key_length = 7, .value_ptr = "GET", .value_length = 3},
      {.key_ptr = ":path", .key_length = 5, .value_ptr = "/test", .value_length = 5},
      {.key_ptr = "host", .key_length = 4, .value_ptr = "example.com", .value_length = 11},
  };

  auto result = envoy_dynamic_module_callback_listener_filter_http_callout(
      filterPtr(), &callout_id, {"nonexistent_cluster", 19}, headers.data(), headers.size(),
      {nullptr, 0}, 5000);

  EXPECT_EQ(envoy_dynamic_module_type_http_callout_init_result_ClusterNotFound, result);
  EXPECT_EQ(0, callout_id);
}

TEST_F(DynamicModuleListenerFilterHttpCalloutTest, SendHttpCalloutMissingRequiredHeaders) {
  uint64_t callout_id = 0;
  // Missing :method header.
  std::vector<envoy_dynamic_module_type_module_http_header> headers = {
      {.key_ptr = ":path", .key_length = 5, .value_ptr = "/test", .value_length = 5},
      {.key_ptr = "host", .key_length = 4, .value_ptr = "example.com", .value_length = 11},
  };

  auto result = envoy_dynamic_module_callback_listener_filter_http_callout(
      filterPtr(), &callout_id, {"test_cluster", 12}, headers.data(), headers.size(), {nullptr, 0},
      5000);

  EXPECT_EQ(envoy_dynamic_module_type_http_callout_init_result_MissingRequiredHeaders, result);
}

TEST_F(DynamicModuleListenerFilterHttpCalloutTest, SendHttpCalloutCannotCreateRequest) {
  NiceMock<Upstream::MockThreadLocalCluster> cluster;
  NiceMock<Http::MockAsyncClient> async_client;

  EXPECT_CALL(cluster_manager_, getThreadLocalCluster("test_cluster"))
      .WillOnce(testing::Return(&cluster));
  EXPECT_CALL(cluster, httpAsyncClient()).WillOnce(testing::ReturnRef(async_client));
  EXPECT_CALL(async_client, send_(testing::_, testing::_, testing::_))
      .WillOnce(testing::Return(nullptr));

  uint64_t callout_id = 0;
  std::vector<envoy_dynamic_module_type_module_http_header> headers = {
      {.key_ptr = ":method", .key_length = 7, .value_ptr = "GET", .value_length = 3},
      {.key_ptr = ":path", .key_length = 5, .value_ptr = "/test", .value_length = 5},
      {.key_ptr = "host", .key_length = 4, .value_ptr = "example.com", .value_length = 11},
  };

  auto result = envoy_dynamic_module_callback_listener_filter_http_callout(
      filterPtr(), &callout_id, {"test_cluster", 12}, headers.data(), headers.size(), {nullptr, 0},
      5000);

  EXPECT_EQ(envoy_dynamic_module_type_http_callout_init_result_CannotCreateRequest, result);
}

TEST_F(DynamicModuleListenerFilterHttpCalloutTest, SendHttpCalloutSuccess) {
  NiceMock<Upstream::MockThreadLocalCluster> cluster;
  NiceMock<Http::MockAsyncClient> async_client;
  Http::MockAsyncClientRequest request(&async_client);

  EXPECT_CALL(cluster_manager_, getThreadLocalCluster("test_cluster"))
      .WillOnce(testing::Return(&cluster));
  EXPECT_CALL(cluster, httpAsyncClient()).WillOnce(testing::ReturnRef(async_client));
  EXPECT_CALL(async_client, send_(testing::_, testing::_, testing::_))
      .WillOnce(testing::Return(&request));

  uint64_t callout_id = 0;
  std::vector<envoy_dynamic_module_type_module_http_header> headers = {
      {.key_ptr = ":method", .key_length = 7, .value_ptr = "POST", .value_length = 4},
      {.key_ptr = ":path", .key_length = 5, .value_ptr = "/api/v1/data", .value_length = 12},
      {.key_ptr = "host", .key_length = 4, .value_ptr = "api.example.com", .value_length = 15},
      {.key_ptr = "content-type",
       .key_length = 12,
       .value_ptr = "application/json",
       .value_length = 16},
  };

  const char* body_data = R"({"key": "value"})";
  envoy_dynamic_module_type_module_buffer body = {body_data, strlen(body_data)};

  auto result = envoy_dynamic_module_callback_listener_filter_http_callout(
      filterPtr(), &callout_id, {"test_cluster", 12}, headers.data(), headers.size(), body, 5000);

  EXPECT_EQ(envoy_dynamic_module_type_http_callout_init_result_Success, result);
  EXPECT_GT(callout_id, 0);

  EXPECT_CALL(request, cancel());
  filter_.reset();
}

TEST_F(DynamicModuleListenerFilterHttpCalloutTest, SendHttpCalloutSuccessWithCallback) {
  NiceMock<Upstream::MockThreadLocalCluster> cluster;
  NiceMock<Http::MockAsyncClient> async_client;
  Http::MockAsyncClientRequest request(&async_client);
  const Http::AsyncClient::Callbacks* captured_callback = nullptr;

  EXPECT_CALL(cluster_manager_, getThreadLocalCluster("test_cluster"))
      .WillOnce(testing::Return(&cluster));
  EXPECT_CALL(cluster, httpAsyncClient()).WillOnce(testing::ReturnRef(async_client));
  EXPECT_CALL(async_client, send_(testing::_, testing::_, testing::_))
      .WillOnce(testing::DoAll(testing::WithArg<1>([&](const Http::AsyncClient::Callbacks& cb) {
                                 captured_callback = &cb;
                               }),
                               testing::Return(&request)));

  uint64_t callout_id = 0;
  std::vector<envoy_dynamic_module_type_module_http_header> headers = {
      {.key_ptr = ":method", .key_length = 7, .value_ptr = "GET", .value_length = 3},
      {.key_ptr = ":path", .key_length = 5, .value_ptr = "/test", .value_length = 5},
      {.key_ptr = "host", .key_length = 4, .value_ptr = "example.com", .value_length = 11},
  };

  auto result = envoy_dynamic_module_callback_listener_filter_http_callout(
      filterPtr(), &callout_id, {"test_cluster", 12}, headers.data(), headers.size(), {nullptr, 0},
      5000);

  EXPECT_EQ(envoy_dynamic_module_type_http_callout_init_result_Success, result);
  EXPECT_GT(callout_id, 0);
  EXPECT_NE(nullptr, captured_callback);

  // Simulate a successful response.
  Http::ResponseMessagePtr response =
      std::make_unique<Http::ResponseMessageImpl>(Http::ResponseHeaderMapImpl::create());
  response->headers().setStatus(200);
  response->body().add("response body");
  const_cast<Http::AsyncClient::Callbacks*>(captured_callback)
      ->onSuccess(request, std::move(response));

  filter_.reset();
}

TEST_F(DynamicModuleListenerFilterHttpCalloutTest, SendHttpCalloutFailureReset) {
  NiceMock<Upstream::MockThreadLocalCluster> cluster;
  NiceMock<Http::MockAsyncClient> async_client;
  Http::MockAsyncClientRequest request(&async_client);
  const Http::AsyncClient::Callbacks* captured_callback = nullptr;

  EXPECT_CALL(cluster_manager_, getThreadLocalCluster("test_cluster"))
      .WillOnce(testing::Return(&cluster));
  EXPECT_CALL(cluster, httpAsyncClient()).WillOnce(testing::ReturnRef(async_client));
  EXPECT_CALL(async_client, send_(testing::_, testing::_, testing::_))
      .WillOnce(testing::DoAll(testing::WithArg<1>([&](const Http::AsyncClient::Callbacks& cb) {
                                 captured_callback = &cb;
                               }),
                               testing::Return(&request)));

  uint64_t callout_id = 0;
  std::vector<envoy_dynamic_module_type_module_http_header> headers = {
      {.key_ptr = ":method", .key_length = 7, .value_ptr = "GET", .value_length = 3},
      {.key_ptr = ":path", .key_length = 5, .value_ptr = "/test", .value_length = 5},
      {.key_ptr = "host", .key_length = 4, .value_ptr = "example.com", .value_length = 11},
  };

  auto result = envoy_dynamic_module_callback_listener_filter_http_callout(
      filterPtr(), &callout_id, {"test_cluster", 12}, headers.data(), headers.size(), {nullptr, 0},
      5000);

  EXPECT_EQ(envoy_dynamic_module_type_http_callout_init_result_Success, result);
  EXPECT_NE(nullptr, captured_callback);

  // Simulate a failure with Reset reason.
  const_cast<Http::AsyncClient::Callbacks*>(captured_callback)
      ->onFailure(request, Http::AsyncClient::FailureReason::Reset);

  filter_.reset();
}

TEST_F(DynamicModuleListenerFilterHttpCalloutTest,
       SendHttpCalloutFailureExceedResponseBufferLimit) {
  NiceMock<Upstream::MockThreadLocalCluster> cluster;
  NiceMock<Http::MockAsyncClient> async_client;
  Http::MockAsyncClientRequest request(&async_client);
  const Http::AsyncClient::Callbacks* captured_callback = nullptr;

  EXPECT_CALL(cluster_manager_, getThreadLocalCluster("test_cluster"))
      .WillOnce(testing::Return(&cluster));
  EXPECT_CALL(cluster, httpAsyncClient()).WillOnce(testing::ReturnRef(async_client));
  EXPECT_CALL(async_client, send_(testing::_, testing::_, testing::_))
      .WillOnce(testing::DoAll(testing::WithArg<1>([&](const Http::AsyncClient::Callbacks& cb) {
                                 captured_callback = &cb;
                               }),
                               testing::Return(&request)));

  uint64_t callout_id = 0;
  std::vector<envoy_dynamic_module_type_module_http_header> headers = {
      {.key_ptr = ":method", .key_length = 7, .value_ptr = "GET", .value_length = 3},
      {.key_ptr = ":path", .key_length = 5, .value_ptr = "/test", .value_length = 5},
      {.key_ptr = "host", .key_length = 4, .value_ptr = "example.com", .value_length = 11},
  };

  auto result = envoy_dynamic_module_callback_listener_filter_http_callout(
      filterPtr(), &callout_id, {"test_cluster", 12}, headers.data(), headers.size(), {nullptr, 0},
      5000);

  EXPECT_EQ(envoy_dynamic_module_type_http_callout_init_result_Success, result);
  EXPECT_NE(nullptr, captured_callback);

  // Simulate a failure with ExceedResponseBufferLimit reason.
  const_cast<Http::AsyncClient::Callbacks*>(captured_callback)
      ->onFailure(request, Http::AsyncClient::FailureReason::ExceedResponseBufferLimit);

  filter_.reset();
}

TEST_F(DynamicModuleListenerFilterHttpCalloutTest, OnBeforeFinalizeUpstreamSpanNoop) {
  NiceMock<Upstream::MockThreadLocalCluster> cluster;
  NiceMock<Http::MockAsyncClient> async_client;
  Http::MockAsyncClientRequest request(&async_client);
  const Http::AsyncClient::Callbacks* captured_callback = nullptr;

  EXPECT_CALL(cluster_manager_, getThreadLocalCluster("test_cluster"))
      .WillOnce(testing::Return(&cluster));
  EXPECT_CALL(cluster, httpAsyncClient()).WillOnce(testing::ReturnRef(async_client));
  EXPECT_CALL(async_client, send_(testing::_, testing::_, testing::_))
      .WillOnce(testing::DoAll(testing::WithArg<1>([&](const Http::AsyncClient::Callbacks& cb) {
                                 captured_callback = &cb;
                               }),
                               testing::Return(&request)));

  uint64_t callout_id = 0;
  std::vector<envoy_dynamic_module_type_module_http_header> headers = {
      {.key_ptr = ":method", .key_length = 7, .value_ptr = "GET", .value_length = 3},
      {.key_ptr = ":path", .key_length = 5, .value_ptr = "/test", .value_length = 5},
      {.key_ptr = "host", .key_length = 4, .value_ptr = "example.com", .value_length = 11},
  };

  auto result = envoy_dynamic_module_callback_listener_filter_http_callout(
      filterPtr(), &callout_id, {"test_cluster", 12}, headers.data(), headers.size(), {nullptr, 0},
      5000);

  EXPECT_EQ(envoy_dynamic_module_type_http_callout_init_result_Success, result);
  ASSERT_NE(nullptr, captured_callback);

  // No-op path: should be safe to call and not crash.
  Envoy::Tracing::MockSpan span;
  const_cast<Http::AsyncClient::Callbacks*>(captured_callback)
      ->onBeforeFinalizeUpstreamSpan(span, nullptr);

  EXPECT_CALL(request, cancel());
  filter_.reset();
}

TEST_F(DynamicModuleListenerFilterHttpCalloutTest, FilterDestructionCancelsPendingCallouts) {
  NiceMock<Upstream::MockThreadLocalCluster> cluster;
  NiceMock<Http::MockAsyncClient> async_client;
  Http::MockAsyncClientRequest request(&async_client);

  EXPECT_CALL(cluster_manager_, getThreadLocalCluster("test_cluster"))
      .WillOnce(testing::Return(&cluster));
  EXPECT_CALL(cluster, httpAsyncClient()).WillOnce(testing::ReturnRef(async_client));
  EXPECT_CALL(async_client, send_(testing::_, testing::_, testing::_))
      .WillOnce(testing::Return(&request));

  uint64_t callout_id = 0;
  std::vector<envoy_dynamic_module_type_module_http_header> headers = {
      {.key_ptr = ":method", .key_length = 7, .value_ptr = "GET", .value_length = 3},
      {.key_ptr = ":path", .key_length = 5, .value_ptr = "/test", .value_length = 5},
      {.key_ptr = "host", .key_length = 4, .value_ptr = "example.com", .value_length = 11},
  };

  auto result = envoy_dynamic_module_callback_listener_filter_http_callout(
      filterPtr(), &callout_id, {"test_cluster", 12}, headers.data(), headers.size(), {nullptr, 0},
      5000);

  EXPECT_EQ(envoy_dynamic_module_type_http_callout_init_result_Success, result);

  EXPECT_CALL(request, cancel());
  // Destroy the filter. This should cancel all pending callouts.
  filter_.reset();
}

} // namespace ListenerFilters
} // namespace DynamicModules
} // namespace Extensions
} // namespace Envoy
