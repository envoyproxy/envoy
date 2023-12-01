#include "envoy/http/header_map.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/network/address_impl.h"
#include "source/common/network/socket_option_factory.h"
#include "source/common/network/utility.h"
#include "source/common/router/config_impl.h"
#include "source/common/router/router.h"
#include "source/common/router/upstream_codec_filter.h"
#include "source/common/router/upstream_request.h"
#include "source/extensions/common/proxy_protocol/proxy_protocol_header.h"
#include "source/extensions/upstreams/http/udp/upstream_request.h"

#include "test/common/http/common.h"
#include "test/mocks/common.h"
#include "test/mocks/router/mocks.h"
#include "test/mocks/router/router_filter_interface.h"
#include "test/mocks/server/factory_context.h"
#include "test/mocks/server/instance.h"
#include "test/test_common/threadsafe_singleton_injector.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Upstreams {
namespace Http {
namespace Udp {

using ::testing::NiceMock;
using ::testing::Return;

class UdpUpstreamTest : public ::testing::Test {
public:
  UdpUpstreamTest() {
    auto mock_socket = std::make_unique<NiceMock<Network::MockSocket>>();
    mock_socket_ = mock_socket.get();
    EXPECT_CALL(*mock_socket_->io_handle_, createFileEvent_);
    auto mock_host = std::make_shared<NiceMock<Upstream::MockHost>>();
    mock_host_ = mock_host.get();
    ON_CALL(*mock_host_, address)
        .WillByDefault(
            Return(Network::Utility::parseInternetAddressAndPortNoThrow("127.0.0.1:80", false)));
    udp_upstream_ =
        std::make_unique<UdpUpstream>(&mock_upstream_to_downstream_, std::move(mock_socket),
                                      std::move(mock_host), mock_dispatcher_);
  }

protected:
  ::Envoy::Http::TestRequestHeaderMapImpl connect_udp_headers_{
      {":path", "/.well-known/masque/udp/foo.lyft.com/80/"},
      {"upgrade", "connect-udp"},
      {"connection", "upgrade"},
      {":authority", "example.org"}};

  NiceMock<Router::MockUpstreamToDownstream> mock_upstream_to_downstream_;
  NiceMock<Network::MockSocket>* mock_socket_;
  NiceMock<Event::MockDispatcher> mock_dispatcher_;
  NiceMock<Upstream::MockHost>* mock_host_;
  std::unique_ptr<UdpUpstream> udp_upstream_;
};

TEST_F(UdpUpstreamTest, ExchangeCapsules) {
  // Swallow the request headers and generate response headers.
  EXPECT_CALL(mock_upstream_to_downstream_, decodeHeaders)
      .WillOnce([](Envoy::Http::ResponseHeaderMapPtr&& headers, bool end_stream) {
        EXPECT_EQ(headers->getStatusValue(), "200");
        EXPECT_FALSE(end_stream);
      });
  EXPECT_TRUE(udp_upstream_->encodeHeaders(connect_udp_headers_, false).ok());

  // Swallow read disable.
  udp_upstream_->readDisable(false);

  // Sends a capsule to upstream.
  const std::string sent_capsule_fragment =
      absl::HexStringToBytes("00"             // DATAGRAM Capsule Type
                             "08"             // Capsule Length
                             "00"             // Context ID
                             "a1a2a3a4a5a6a7" // UDP Proxying Payload
      );
  Buffer::OwnedImpl sent_capsule(sent_capsule_fragment);
  EXPECT_CALL(*mock_socket_->io_handle_, sendmsg(_, _, _, _, _))
      .WillOnce([](const Buffer::RawSlice* slices, uint64_t num_slice, int /*flags*/,
                   const Network::Address::Ip* /*self_ip*/,
                   const Network::Address::Instance& /*peer_address*/) {
        Buffer::OwnedImpl buffer(absl::HexStringToBytes("a1a2a3a4a5a6a7"));
        EXPECT_TRUE(TestUtility::rawSlicesEqual(buffer.getRawSlices().data(), slices, num_slice));
        return Api::ioCallUint64ResultNoError();
      });
  udp_upstream_->encodeData(sent_capsule, false);

  // Receives data from upstream and converts it to capsule.
  const std::string decoded_capsule_fragment =
      absl::HexStringToBytes("00"             // DATAGRAM Capsule Type
                             "08"             // Capsule Length
                             "00"             // Context ID
                             "b1b2b3b4b5b6b7" // UDP Proxying Payload
      );
  Buffer::InstancePtr received_data =
      std::make_unique<Buffer::OwnedImpl>(absl::HexStringToBytes("b1b2b3b4b5b6b7"));
  EXPECT_CALL(mock_upstream_to_downstream_,
              decodeData(BufferStringEqual(decoded_capsule_fragment), false));
  Envoy::MonotonicTime timestamp;
  udp_upstream_->processPacket(nullptr, nullptr, std::move(received_data), timestamp);
}

TEST_F(UdpUpstreamTest, HeaderOnlyRequest) {
  EXPECT_CALL(mock_upstream_to_downstream_, decodeHeaders)
      .WillOnce([](Envoy::Http::ResponseHeaderMapPtr&& headers, bool end_stream) {
        EXPECT_EQ(headers->getStatusValue(), "400");
        EXPECT_TRUE(end_stream);
      });
  EXPECT_TRUE(udp_upstream_->encodeHeaders(connect_udp_headers_, true).ok());
}

TEST_F(UdpUpstreamTest, SwallowMetadata) {
  Envoy::Http::MetadataMapVector metadata_map_vector;
  udp_upstream_->encodeMetadata(metadata_map_vector);
  EXPECT_CALL(*mock_socket_->io_handle_, sendmsg).Times(0);
}

TEST_F(UdpUpstreamTest, SwallowTrailers) {
  Envoy::Http::TestRequestTrailerMapImpl trailers{{"foo", "bar"}};
  udp_upstream_->encodeTrailers(trailers);
  EXPECT_CALL(*mock_socket_->io_handle_, sendmsg).Times(0);
}

TEST_F(UdpUpstreamTest, DatagramsDropped) {
  udp_upstream_->onDatagramsDropped(1);
  EXPECT_EQ(udp_upstream_->numOfDroppedDatagrams(), 1);
  udp_upstream_->onDatagramsDropped(3);
  EXPECT_EQ(udp_upstream_->numOfDroppedDatagrams(), 4);
}

TEST_F(UdpUpstreamTest, InvalidCapsule) {
  EXPECT_TRUE(udp_upstream_->encodeHeaders(connect_udp_headers_, false).ok());
  // Sends an invalid capsule.
  const std::string invalid_capsule_fragment =
      absl::HexStringToBytes("0x1eca6a00" // DATAGRAM Capsule Type
                             "01"         // Capsule Length
                             "c0"         // Invalid VarInt62
      );
  Buffer::OwnedImpl invalid_capsule(invalid_capsule_fragment);
  EXPECT_CALL(mock_upstream_to_downstream_, onResetStream);
  udp_upstream_->encodeData(invalid_capsule, true);
}

TEST_F(UdpUpstreamTest, MalformedContextIdDatagram) {
  EXPECT_TRUE(udp_upstream_->encodeHeaders(connect_udp_headers_, false).ok());
  // Sends a capsule with an invalid variable length integer.
  const std::string invalid_context_id_fragment =
      absl::HexStringToBytes("00" // DATAGRAM Capsule Type
                             "01" // Capsule Length
                             "c0" // Context ID (Invalid VarInt62)
      );
  Buffer::OwnedImpl invalid_context_id_capsule(invalid_context_id_fragment);
  EXPECT_CALL(mock_upstream_to_downstream_, onResetStream);
  udp_upstream_->encodeData(invalid_context_id_capsule, true);
}

TEST_F(UdpUpstreamTest, RemainingDataWhenStreamEnded) {
  EXPECT_CALL(mock_upstream_to_downstream_, decodeHeaders)
      .WillOnce([](Envoy::Http::ResponseHeaderMapPtr&& headers, bool end_stream) {
        EXPECT_EQ(headers->getStatusValue(), "200");
        EXPECT_FALSE(end_stream);
      });
  EXPECT_TRUE(udp_upstream_->encodeHeaders(connect_udp_headers_, false).ok());

  // Sends a capsule to upstream with a large length value.
  const std::string sent_capsule_fragment =
      absl::HexStringToBytes("00"             // DATAGRAM Capsule Type
                             "ff"             // Capsule Length
                             "00"             // Context ID
                             "a1a2a3a4a5a6a7" // UDP Proxying Payload
      );
  Buffer::OwnedImpl sent_capsule(sent_capsule_fragment);
  EXPECT_CALL(mock_upstream_to_downstream_, onResetStream);
  udp_upstream_->encodeData(sent_capsule, true);
}

TEST_F(UdpUpstreamTest, SocketConnectError) {
  EXPECT_CALL(mock_upstream_to_downstream_, decodeHeaders).Times(0);
  EXPECT_CALL(*mock_socket_, connect(_)).WillOnce(Return(Api::SysCallIntResult{-1, EADDRINUSE}));
  EXPECT_FALSE(udp_upstream_->encodeHeaders(connect_udp_headers_, false).ok());
}

class UdpConnPoolTest : public ::testing::Test {
public:
  UdpConnPoolTest() {
    ON_CALL(*mock_thread_local_cluster_.lb_.host_, address)
        .WillByDefault(
            Return(Network::Utility::parseInternetAddressAndPortNoThrow("127.0.0.1:80", false)));
    udp_conn_pool_ = std::make_unique<UdpConnPool>(mock_thread_local_cluster_, nullptr);
    EXPECT_CALL(*mock_thread_local_cluster_.lb_.host_, address).Times(2);
    EXPECT_CALL(*mock_thread_local_cluster_.lb_.host_, cluster);
    mock_thread_local_cluster_.lb_.host_->cluster_.source_address_ =
        Network::Utility::parseInternetAddressAndPortNoThrow("127.0.0.1:10001", false);
  }

protected:
  NiceMock<Envoy::Upstream::MockThreadLocalCluster> mock_thread_local_cluster_;
  std::unique_ptr<UdpConnPool> udp_conn_pool_;
  Router::MockGenericConnectionPoolCallbacks mock_callback_;
};

TEST_F(UdpConnPoolTest, BindToUpstreamLocalAddress) {
  EXPECT_CALL(mock_callback_, upstreamToDownstream);
  NiceMock<Network::MockConnection> downstream_connection_;
  EXPECT_CALL(mock_callback_.upstream_to_downstream_, connection)
      .WillRepeatedly(
          Return(Envoy::OptRef<const Envoy::Network::Connection>(downstream_connection_)));
  EXPECT_CALL(mock_callback_, onPoolReady);
  // Mock syscall to make the bind call succeed.
  NiceMock<Envoy::Api::MockOsSysCalls> mock_os_sys_calls;
  Envoy::TestThreadsafeSingletonInjector<Envoy::Api::OsSysCallsImpl> os_sys_calls(
      &mock_os_sys_calls);
  EXPECT_CALL(mock_os_sys_calls, bind).WillOnce(Return(Api::SysCallIntResult{0, 0}));
  udp_conn_pool_->newStream(&mock_callback_);
}

TEST_F(UdpConnPoolTest, ApplySocketOptionsFailure) {
  Upstream::UpstreamLocalAddress upstream_local_address = {
      mock_thread_local_cluster_.lb_.host_->cluster_.source_address_,
      Network::SocketOptionFactory::buildIpFreebindOptions()};
  // Return a socket option to make the setsockopt syscall is called.
  EXPECT_CALL(*mock_thread_local_cluster_.lb_.host_->cluster_.upstream_local_address_selector_,
              getUpstreamLocalAddressImpl)
      .WillOnce(Return(upstream_local_address));
  EXPECT_CALL(mock_callback_, onPoolFailure);
  // Mock syscall to make the setsockopt call fail.
  NiceMock<Envoy::Api::MockOsSysCalls> mock_os_sys_calls;
  Envoy::TestThreadsafeSingletonInjector<Envoy::Api::OsSysCallsImpl> os_sys_calls(
      &mock_os_sys_calls);
  // Use ON_CALL since the applyOptions call fail without calling the setsockopt_ in Windows.
  ON_CALL(mock_os_sys_calls, setsockopt_).WillByDefault(Return(-1));
  udp_conn_pool_->newStream(&mock_callback_);
}

TEST_F(UdpConnPoolTest, BindFailure) {
  EXPECT_CALL(mock_callback_, onPoolFailure);
  // Mock syscall to make the bind call fail.
  NiceMock<Envoy::Api::MockOsSysCalls> mock_os_sys_calls;
  Envoy::TestThreadsafeSingletonInjector<Envoy::Api::OsSysCallsImpl> os_sys_calls(
      &mock_os_sys_calls);
  EXPECT_CALL(mock_os_sys_calls, bind).WillOnce(Return(Api::SysCallIntResult{-1, 0}));
  udp_conn_pool_->newStream(&mock_callback_);
}

} // namespace Udp
} // namespace Http
} // namespace Upstreams
} // namespace Extensions
} // namespace Envoy
