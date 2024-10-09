#include "source/common/quic/envoy_quic_utils.h"

#include "test/mocks/api/mocks.h"
#include "test/test_common/threadsafe_singleton_injector.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "quiche/quic/test_tools/quic_test_utils.h"

using testing::_;
using testing::NiceMock;
using testing::Return;

namespace Envoy {
namespace Quic {

TEST(EnvoyQuicUtilsTest, ConversionBetweenQuicAddressAndEnvoyAddress) {
  // Mock out socket() system call to test both V4 and V6 address conversion.
  testing::NiceMock<Envoy::Api::MockOsSysCalls> os_sys_calls;
  TestThreadsafeSingletonInjector<Envoy::Api::OsSysCallsImpl> os_calls{&os_sys_calls};
  ON_CALL(os_sys_calls, socket(_, _, _)).WillByDefault(Return(Api::SysCallSocketResult{1, 0}));
  ON_CALL(os_sys_calls, close(_)).WillByDefault(Return(Api::SysCallIntResult{0, 0}));

  quic::QuicSocketAddress quic_uninitialized_addr;
  EXPECT_EQ(nullptr, quicAddressToEnvoyAddressInstance(quic_uninitialized_addr));

  for (const std::string ip_str : {"fd00:0:0:1::1", "1.2.3.4"}) {
    quic::QuicIpAddress quic_ip;
    quic_ip.FromString(ip_str);
    quic::QuicSocketAddress quic_addr(quic_ip, 12345);
    Network::Address::InstanceConstSharedPtr envoy_addr =
        quicAddressToEnvoyAddressInstance(quic_addr);
    EXPECT_EQ(quic_addr.ToString(), envoy_addr->asStringView());
    EXPECT_EQ(quic_addr, envoyIpAddressToQuicSocketAddress(envoy_addr->ip()));
  }
  EXPECT_FALSE(envoyIpAddressToQuicSocketAddress(nullptr).IsInitialized());
}

class MockServerHeaderValidator : public HeaderValidator {
public:
  ~MockServerHeaderValidator() override = default;
  MOCK_METHOD(Http::HeaderUtility::HeaderValidationResult, validateHeader,
              (absl::string_view header_name, absl::string_view header_value));
};

TEST(EnvoyQuicUtilsTest, HeadersConversion) {
  quiche::HttpHeaderBlock headers_block;
  headers_block[":authority"] = "www.google.com";
  headers_block[":path"] = "/index.hml";
  headers_block[":scheme"] = "https";
  // "value1" and "value2" should be coalesced into one header by QUICHE and split again while
  // converting to Envoy headers.
  headers_block.AppendValueOrAddHeader("key", "value1");
  headers_block.AppendValueOrAddHeader("key", "value2");
  headers_block.AppendValueOrAddHeader("key1", "value1");
  headers_block.AppendValueOrAddHeader("key1", "");
  headers_block.AppendValueOrAddHeader("key1", "value2");
  NiceMock<MockServerHeaderValidator> validator;
  absl::string_view details;
  quic::QuicRstStreamErrorCode rst = quic::QUIC_REFUSED_STREAM;
  auto envoy_headers = http2HeaderBlockToEnvoyTrailers<Http::RequestHeaderMapImpl>(
      headers_block, 60, 100, validator, details, rst);
  // Envoy header block is 3 headers larger because QUICHE header block does coalescing.
  EXPECT_EQ(headers_block.size() + 3u, envoy_headers->size());
  EXPECT_EQ("www.google.com", envoy_headers->getHostValue());
  EXPECT_EQ("/index.hml", envoy_headers->getPathValue());
  EXPECT_EQ("https", envoy_headers->getSchemeValue());
  EXPECT_EQ("value1", envoy_headers->get(Http::LowerCaseString("key"))[0]->value().getStringView());
  EXPECT_EQ("value2", envoy_headers->get(Http::LowerCaseString("key"))[1]->value().getStringView());
  EXPECT_EQ("value1",
            envoy_headers->get(Http::LowerCaseString("key1"))[0]->value().getStringView());
  EXPECT_EQ("", envoy_headers->get(Http::LowerCaseString("key1"))[1]->value().getStringView());
  EXPECT_EQ("value2",
            envoy_headers->get(Http::LowerCaseString("key1"))[2]->value().getStringView());

  EXPECT_EQ(rst, quic::QUIC_REFUSED_STREAM); // With no error it will be untouched.

  quic::QuicHeaderList quic_headers;
  quic_headers.OnHeader(":authority", "www.google.com");
  quic_headers.OnHeader(":path", "/index.hml");
  quic_headers.OnHeader(":scheme", "https");
  quic_headers.OnHeader("key", "value1");
  quic_headers.OnHeader("key", "value2");
  quic_headers.OnHeader("key1", "value1");
  quic_headers.OnHeader("key1", "");
  quic_headers.OnHeader("key1", "value2");
  quic_headers.OnHeader("key-to-drop", "");
  quic_headers.OnHeaderBlockEnd(0, 0);
  EXPECT_CALL(validator, validateHeader(_, _))
      .WillRepeatedly([](absl::string_view header_name, absl::string_view) {
        if (header_name == "key-to-drop") {
          return Http::HeaderUtility::HeaderValidationResult::DROP;
        }
        return Http::HeaderUtility::HeaderValidationResult::ACCEPT;
      });
  auto envoy_headers2 = quicHeadersToEnvoyHeaders<Http::RequestHeaderMapImpl>(
      quic_headers, validator, 60, 100, details, rst);
  EXPECT_EQ(*envoy_headers, *envoy_headers2);
  EXPECT_EQ(rst, quic::QUIC_REFUSED_STREAM); // With no error it will be untouched.

  quic::QuicHeaderList quic_headers2;
  quic_headers2.OnHeader(":authority", "www.google.com");
  quic_headers2.OnHeader(":path", "/index.hml");
  quic_headers2.OnHeader(":scheme", "https");
  quic_headers2.OnHeader("invalid_key", "");
  quic_headers2.OnHeaderBlockEnd(0, 0);
  EXPECT_CALL(validator, validateHeader(_, _))
      .WillRepeatedly([](absl::string_view header_name, absl::string_view) {
        if (header_name == "invalid_key") {
          return Http::HeaderUtility::HeaderValidationResult::REJECT;
        }
        return Http::HeaderUtility::HeaderValidationResult::ACCEPT;
      });
  EXPECT_EQ(nullptr, quicHeadersToEnvoyHeaders<Http::RequestHeaderMapImpl>(quic_headers2, validator,
                                                                           60, 100, details, rst));
  EXPECT_EQ(rst, quic::QUIC_BAD_APPLICATION_PAYLOAD);
}

TEST(EnvoyQuicUtilsTest, HeadersSizeBounds) {
  quiche::HttpHeaderBlock headers_block;
  headers_block[":authority"] = "www.google.com";
  headers_block[":path"] = "/index.hml";
  headers_block[":scheme"] = "https";
  headers_block["foo"] = std::string("bar\0eep\0baz", 11);
  absl::string_view details;
  // 6 headers are allowed.
  NiceMock<MockServerHeaderValidator> validator;
  quic::QuicRstStreamErrorCode rst = quic::QUIC_REFUSED_STREAM;
  EXPECT_NE(nullptr, http2HeaderBlockToEnvoyTrailers<Http::RequestHeaderMapImpl>(
                         headers_block, 60, 6, validator, details, rst));
  // Given the cap is 6, make sure anything lower, exact or otherwise, is rejected.
  EXPECT_EQ(nullptr, http2HeaderBlockToEnvoyTrailers<Http::RequestHeaderMapImpl>(
                         headers_block, 60, 5, validator, details, rst));
  EXPECT_EQ("http3.too_many_trailers", details);
  EXPECT_EQ(nullptr, http2HeaderBlockToEnvoyTrailers<Http::RequestHeaderMapImpl>(
                         headers_block, 60, 4, validator, details, rst));
  EXPECT_EQ(rst, quic::QUIC_STREAM_EXCESSIVE_LOAD);
}

TEST(EnvoyQuicUtilsTest, TrailersSizeBounds) {
  quiche::HttpHeaderBlock headers_block;
  headers_block[":authority"] = "www.google.com";
  headers_block[":path"] = "/index.hml";
  headers_block[":scheme"] = "https";
  headers_block["foo"] = std::string("bar\0eep\0baz", 11);
  absl::string_view details;
  NiceMock<MockServerHeaderValidator> validator;
  quic::QuicRstStreamErrorCode rst = quic::QUIC_REFUSED_STREAM;
  EXPECT_NE(nullptr, http2HeaderBlockToEnvoyTrailers<Http::RequestHeaderMapImpl>(
                         headers_block, 60, 6, validator, details, rst));
  EXPECT_EQ(nullptr, http2HeaderBlockToEnvoyTrailers<Http::RequestHeaderMapImpl>(
                         headers_block, 60, 2, validator, details, rst));
  EXPECT_EQ("http3.too_many_trailers", details);
  EXPECT_EQ(nullptr, http2HeaderBlockToEnvoyTrailers<Http::RequestHeaderMapImpl>(
                         headers_block, 60, 2, validator, details, rst));
  EXPECT_EQ(rst, quic::QUIC_STREAM_EXCESSIVE_LOAD);
}

TEST(EnvoyQuicUtilsTest, TrailerCharacters) {
  quiche::HttpHeaderBlock headers_block;
  headers_block[":authority"] = "www.google.com";
  headers_block[":path"] = "/index.hml";
  headers_block[":scheme"] = "https";
  absl::string_view details;
  NiceMock<MockServerHeaderValidator> validator;
  EXPECT_CALL(validator, validateHeader(_, _))
      .WillRepeatedly(Return(Http::HeaderUtility::HeaderValidationResult::REJECT));
  quic::QuicRstStreamErrorCode rst = quic::QUIC_REFUSED_STREAM;
  EXPECT_EQ(nullptr, http2HeaderBlockToEnvoyTrailers<Http::RequestHeaderMapImpl>(
                         headers_block, 60, 5, validator, details, rst));
  EXPECT_EQ(rst, quic::QUIC_BAD_APPLICATION_PAYLOAD);
}

TEST(EnvoyQuicUtilsTest, deduceSignatureAlgorithmFromNullPublicKey) {
  std::string error;
  EXPECT_EQ(0, deduceSignatureAlgorithmFromPublicKey(nullptr, &error));
  EXPECT_EQ("Invalid leaf cert, bad public key", error);
}

TEST(EnvoyQuicUtilsTest, ConvertQuicConfig) {
  envoy::config::core::v3::QuicProtocolOptions config;
  quic::QuicConfig quic_config;

  // Test defaults.
  convertQuicConfig(config, quic_config);
  EXPECT_EQ(100, quic_config.GetMaxBidirectionalStreamsToSend());
  EXPECT_EQ(100, quic_config.GetMaxUnidirectionalStreamsToSend());
  EXPECT_EQ(16777216, quic_config.GetInitialMaxStreamDataBytesIncomingBidirectionalToSend());
  EXPECT_EQ(25165824, quic_config.GetInitialSessionFlowControlWindowToSend());
  EXPECT_TRUE(quic_config.SendConnectionOptions().empty());
  EXPECT_TRUE(quic_config.ClientRequestedIndependentOptions(quic::Perspective::IS_CLIENT).empty());
  EXPECT_EQ(quic::QuicTime::Delta::FromSeconds(quic::kMaximumIdleTimeoutSecs),
            quic_config.IdleNetworkTimeout());

  // Test converting values.
  config.mutable_max_concurrent_streams()->set_value(2);
  config.mutable_initial_stream_window_size()->set_value(3);
  config.mutable_initial_connection_window_size()->set_value(50);
  config.set_connection_options("5RTO,ACKD");
  config.set_client_connection_options("6RTO,AKD4");
  config.mutable_idle_network_timeout()->set_seconds(30);
  convertQuicConfig(config, quic_config);
  EXPECT_EQ(2, quic_config.GetMaxBidirectionalStreamsToSend());
  EXPECT_EQ(2, quic_config.GetMaxUnidirectionalStreamsToSend());
  EXPECT_EQ(3, quic_config.GetInitialMaxStreamDataBytesIncomingBidirectionalToSend());
  EXPECT_EQ(quic::QuicTime::Delta::FromSeconds(30), quic_config.IdleNetworkTimeout());
  EXPECT_EQ(2, quic_config.SendConnectionOptions().size());
  EXPECT_EQ(2, quic_config.ClientRequestedIndependentOptions(quic::Perspective::IS_CLIENT).size());
  std::string quic_copts = "";
  for (auto& copt : quic_config.SendConnectionOptions()) {
    quic_copts.append(quic::QuicTagToString(copt));
  }
  EXPECT_EQ(quic_copts, "5RTOACKD");
  std::string quic_ccopts = "";
  for (auto& ccopt : quic_config.ClientRequestedIndependentOptions(quic::Perspective::IS_CLIENT)) {
    quic_ccopts.append(quic::QuicTagToString(ccopt));
  }
  EXPECT_EQ(quic_ccopts, "6RTOAKD4");
}

TEST(EnvoyQuicUtilsTest, HeaderMapMaxSizeLimit) {
  NiceMock<MockServerHeaderValidator> validator;
  absl::string_view details;
  quic::QuicRstStreamErrorCode rst = quic::QUIC_REFUSED_STREAM;
  quic::QuicHeaderList quic_headers;
  quic_headers.OnHeader(":authority", "www.google.com");
  quic_headers.OnHeader(":path", "/index.hml");
  quic_headers.OnHeader(":scheme", "https");
  quic_headers.OnHeaderBlockEnd(0, 0);
  EXPECT_CALL(validator, validateHeader(_, _))
      .WillRepeatedly([](absl::string_view, absl::string_view) {
        return Http::HeaderUtility::HeaderValidationResult::ACCEPT;
      });
  // Request header map test.
  auto request_header = quicHeadersToEnvoyHeaders<Http::RequestHeaderMapImpl>(
      quic_headers, validator, 60, 100, details, rst);
  EXPECT_EQ(request_header->maxHeadersCount(), 100);
  EXPECT_EQ(request_header->maxHeadersKb(), 60);

  // Response header map test.
  auto response_header = quicHeadersToEnvoyHeaders<Http::ResponseHeaderMapImpl>(
      quic_headers, validator, 60, 100, details, rst);
  EXPECT_EQ(response_header->maxHeadersCount(), 100);
  EXPECT_EQ(response_header->maxHeadersKb(), 60);

  quiche::HttpHeaderBlock headers_block;
  headers_block[":authority"] = "www.google.com";
  headers_block[":path"] = "/index.hml";
  headers_block[":scheme"] = "https";

  // Request trailer map test.
  auto request_trailer = http2HeaderBlockToEnvoyTrailers<Http::RequestTrailerMapImpl>(
      headers_block, 60, 100, validator, details, rst);
  EXPECT_EQ(request_trailer->maxHeadersCount(), 100);
  EXPECT_EQ(request_trailer->maxHeadersKb(), 60);

  // Response trailer map test.
  auto response_trailer = http2HeaderBlockToEnvoyTrailers<Http::ResponseTrailerMapImpl>(
      headers_block, 60, 100, validator, details, rst);
  EXPECT_EQ(response_trailer->maxHeadersCount(), 100);
  EXPECT_EQ(response_trailer->maxHeadersKb(), 60);
}

TEST(EnvoyQuicUtilsTest, EnvoyResetReasonToQuicResetErrorCodeImpossibleCases) {
  EXPECT_ENVOY_BUG(envoyResetReasonToQuicRstError(Http::StreamResetReason::Overflow),
                   "Resource overflow ");
  EXPECT_ENVOY_BUG(
      envoyResetReasonToQuicRstError(Http::StreamResetReason::RemoteRefusedStreamReset),
      "Remote reset ");
  EXPECT_ENVOY_BUG(
      envoyResetReasonToQuicRstError(Http::StreamResetReason::Http1PrematureUpstreamHalfClose),
      "not applicable");
}

TEST(EnvoyQuicUtilsTest, QuicResetErrorToEnvoyResetReason) {
  EXPECT_EQ(quicRstErrorToEnvoyLocalResetReason(quic::QUIC_STREAM_NO_ERROR),
            Http::StreamResetReason::LocalReset);
  EXPECT_EQ(quicRstErrorToEnvoyRemoteResetReason(quic::QUIC_STREAM_CONNECTION_ERROR),
            Http::StreamResetReason::ConnectionTermination);
  EXPECT_EQ(quicRstErrorToEnvoyRemoteResetReason(quic::QUIC_STREAM_CONNECT_ERROR),
            Http::StreamResetReason::ConnectError);
}

TEST(EnvoyQuicUtilsTest, CreateConnectionSocket) {
  Network::Address::InstanceConstSharedPtr local_addr =
      std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1");
  Network::Address::InstanceConstSharedPtr peer_addr =
      std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1", 54321, nullptr);
  auto connection_socket = createConnectionSocket(peer_addr, local_addr, nullptr);
  EXPECT_TRUE(connection_socket->isOpen());
  EXPECT_TRUE(connection_socket->ioHandle().wasConnected());
  connection_socket->close();

  Network::Address::InstanceConstSharedPtr no_local_addr = nullptr;
  connection_socket = createConnectionSocket(peer_addr, no_local_addr, nullptr);
  EXPECT_TRUE(connection_socket->isOpen());
  EXPECT_TRUE(connection_socket->ioHandle().wasConnected());
  EXPECT_EQ("127.0.0.1", no_local_addr->ip()->addressAsString());
  if (Runtime::runtimeFeatureEnabled("envoy.reloadable_features.udp_set_do_not_fragment")) {
    int value = 0;
    socklen_t val_length = sizeof(value);
#ifdef ENVOY_IP_DONTFRAG
    RELEASE_ASSERT(connection_socket->getSocketOption(IPPROTO_IP, IP_DONTFRAG, &value, &val_length)
                           .return_value_ == 0,
                   "Failed getsockopt IP_DONTFRAG");
    EXPECT_EQ(value, 1);
#else
    RELEASE_ASSERT(
        connection_socket->getSocketOption(IPPROTO_IP, IP_MTU_DISCOVER, &value, &val_length)
                .return_value_ == 0,
        "Failed getsockopt IP_MTU_DISCOVER");
    EXPECT_EQ(value, IP_PMTUDISC_DO);
#endif
  }
  connection_socket->close();

  Network::Address::InstanceConstSharedPtr local_addr_v6 =
      std::make_shared<Network::Address::Ipv6Instance>("::1", 0, nullptr, /*v6only*/ true);
  Network::Address::InstanceConstSharedPtr peer_addr_v6 =
      std::make_shared<Network::Address::Ipv6Instance>("::1", 54321, nullptr, /*v6only*/ false);
  connection_socket = createConnectionSocket(peer_addr_v6, local_addr_v6, nullptr);
  EXPECT_TRUE(connection_socket->isOpen());
  EXPECT_TRUE(connection_socket->ioHandle().wasConnected());
  EXPECT_TRUE(local_addr_v6->ip()->ipv6()->v6only());
  if (Runtime::runtimeFeatureEnabled("envoy.reloadable_features.udp_set_do_not_fragment")) {
    int value = 0;
    socklen_t val_length = sizeof(value);
#ifdef ENVOY_IP_DONTFRAG
    RELEASE_ASSERT(
        connection_socket->getSocketOption(IPPROTO_IPV6, IPV6_DONTFRAG, &value, &val_length)
                .return_value_ == 0,
        "Failed getsockopt IPV6_DONTFRAG");
    ;
    EXPECT_EQ(value, 1);
#else
    RELEASE_ASSERT(
        connection_socket->getSocketOption(IPPROTO_IPV6, IPV6_MTU_DISCOVER, &value, &val_length)
                .return_value_ == 0,
        "Failed getsockopt IPV6_MTU_DISCOVER");
    EXPECT_EQ(value, IPV6_PMTUDISC_DO);
    // The v4 socket option is not applied to v6-only socket.
    value = 0;
    val_length = sizeof(value);
    RELEASE_ASSERT(
        connection_socket->getSocketOption(IPPROTO_IP, IP_MTU_DISCOVER, &value, &val_length)
                .return_value_ == 0,
        "Failed getsockopt IP_MTU_DISCOVER");
    EXPECT_NE(value, IP_PMTUDISC_DO);
#endif
  }
  connection_socket->close();

  Network::Address::InstanceConstSharedPtr no_local_addr_v6 = nullptr;
  connection_socket = createConnectionSocket(peer_addr_v6, no_local_addr_v6, nullptr);
  EXPECT_TRUE(connection_socket->isOpen());
  EXPECT_TRUE(connection_socket->ioHandle().wasConnected());
  EXPECT_EQ("::1", no_local_addr_v6->ip()->addressAsString());
  EXPECT_FALSE(no_local_addr_v6->ip()->ipv6()->v6only());
  if (Runtime::runtimeFeatureEnabled("envoy.reloadable_features.udp_set_do_not_fragment")) {
    int value = 0;
    socklen_t val_length = sizeof(value);
#ifdef ENVOY_IP_DONTFRAG
    RELEASE_ASSERT(
        connection_socket->getSocketOption(IPPROTO_IPV6, IPV6_DONTFRAG, &value, &val_length)
                .return_value_ == 0,
        "Failed getsockopt IPV6_DONTFRAG");
    EXPECT_EQ(value, 1);
#else
    RELEASE_ASSERT(
        connection_socket->getSocketOption(IPPROTO_IPV6, IPV6_MTU_DISCOVER, &value, &val_length)
                .return_value_ == 0,
        "Failed getsockopt IPV6_MTU_DISCOVER");
    EXPECT_EQ(value, IPV6_PMTUDISC_DO);
    // The v4 socket option is also applied to dual stack socket.
    value = 0;
    val_length = sizeof(value);
    RELEASE_ASSERT(
        connection_socket->getSocketOption(IPPROTO_IP, IP_MTU_DISCOVER, &value, &val_length)
                .return_value_ == 0,
        "Failed getsockopt IP_MTU_DISCOVER");
    EXPECT_EQ(value, IP_PMTUDISC_DO);
#endif
  }
  connection_socket->close();
}

} // namespace Quic
} // namespace Envoy
