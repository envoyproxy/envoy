#include "source/common/quic/envoy_quic_utils.h"

#include "test/mocks/api/mocks.h"
#include "test/test_common/threadsafe_singleton_injector.h"

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

  for (const std::string& ip_str : {"fd00:0:0:1::1", "1.2.3.4"}) {
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

class MockHeaderValidator : public HeaderValidator {
public:
  ~MockHeaderValidator() override = default;
  MOCK_METHOD(Http::HeaderUtility::HeaderValidationResult, validateHeader,
              (absl::string_view header_name, absl::string_view header_value));
};

TEST(EnvoyQuicUtilsTest, HeadersConversion) {
  spdy::Http2HeaderBlock headers_block;
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
  NiceMock<MockHeaderValidator> validator;
  absl::string_view details;
  quic::QuicRstStreamErrorCode rst = quic::QUIC_REFUSED_STREAM;
  auto envoy_headers = http2HeaderBlockToEnvoyTrailers<Http::RequestHeaderMapImpl>(
      headers_block, 100, validator, details, rst);
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
  quic_headers.OnHeaderBlockStart();
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
      quic_headers, validator, 100, details, rst);
  EXPECT_EQ(*envoy_headers, *envoy_headers2);
  EXPECT_EQ(rst, quic::QUIC_REFUSED_STREAM); // With no error it will be untouched.

  quic::QuicHeaderList quic_headers2;
  quic_headers2.OnHeaderBlockStart();
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
                                                                           100, details, rst));
  EXPECT_EQ(rst, quic::QUIC_BAD_APPLICATION_PAYLOAD);
}

TEST(EnvoyQuicUtilsTest, HeadersSizeBounds) {
  spdy::Http2HeaderBlock headers_block;
  headers_block[":authority"] = "www.google.com";
  headers_block[":path"] = "/index.hml";
  headers_block[":scheme"] = "https";
  headers_block["foo"] = std::string("bar\0eep\0baz", 11);
  absl::string_view details;
  // 6 headers are allowed.
  NiceMock<MockHeaderValidator> validator;
  quic::QuicRstStreamErrorCode rst = quic::QUIC_REFUSED_STREAM;
  EXPECT_NE(nullptr, http2HeaderBlockToEnvoyTrailers<Http::RequestHeaderMapImpl>(
                         headers_block, 6, validator, details, rst));
  // Given the cap is 6, make sure anything lower, exact or otherwise, is rejected.
  EXPECT_EQ(nullptr, http2HeaderBlockToEnvoyTrailers<Http::RequestHeaderMapImpl>(
                         headers_block, 5, validator, details, rst));
  EXPECT_EQ("http3.too_many_trailers", details);
  EXPECT_EQ(nullptr, http2HeaderBlockToEnvoyTrailers<Http::RequestHeaderMapImpl>(
                         headers_block, 4, validator, details, rst));
  EXPECT_EQ(rst, quic::QUIC_STREAM_EXCESSIVE_LOAD);
}

TEST(EnvoyQuicUtilsTest, TrailerCharacters) {
  spdy::Http2HeaderBlock headers_block;
  headers_block[":authority"] = "www.google.com";
  headers_block[":path"] = "/index.hml";
  headers_block[":scheme"] = "https";
  absl::string_view details;
  NiceMock<MockHeaderValidator> validator;
  EXPECT_CALL(validator, validateHeader(_, _))
      .WillRepeatedly(Return(Http::HeaderUtility::HeaderValidationResult::REJECT));
  quic::QuicRstStreamErrorCode rst = quic::QUIC_REFUSED_STREAM;
  EXPECT_EQ(nullptr, http2HeaderBlockToEnvoyTrailers<Http::RequestHeaderMapImpl>(
                         headers_block, 5, validator, details, rst));
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

  // Test converting values.
  config.mutable_max_concurrent_streams()->set_value(2);
  config.mutable_initial_stream_window_size()->set_value(3);
  config.mutable_initial_connection_window_size()->set_value(50);
  convertQuicConfig(config, quic_config);
  EXPECT_EQ(2, quic_config.GetMaxBidirectionalStreamsToSend());
  EXPECT_EQ(2, quic_config.GetMaxUnidirectionalStreamsToSend());
  EXPECT_EQ(3, quic_config.GetInitialMaxStreamDataBytesIncomingBidirectionalToSend());
}

} // namespace Quic
} // namespace Envoy
