#include "common/quic/envoy_quic_utils.h"

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif

#include "quiche/quic/test_tools/quic_test_utils.h"

#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include "test/mocks/api/mocks.h"
#include "test/test_common/threadsafe_singleton_injector.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
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
}

class MockHeaderValidator : public HeaderValidator {
public:
  ~MockHeaderValidator() override = default;
  MOCK_METHOD(Http::HeaderUtility::HeaderValidationResult, validateHeader,
              (const std::string& header_name, absl::string_view header_value));
};

TEST(EnvoyQuicUtilsTest, HeadersConversion) {
  spdy::SpdyHeaderBlock headers_block;
  headers_block[":authority"] = "www.google.com";
  headers_block[":path"] = "/index.hml";
  headers_block[":scheme"] = "https";
  // "value1" and "value2" should be coalesced into one header by QUICHE and split again while
  // converting to Envoy headers.
  headers_block.AppendValueOrAddHeader("key", "value1");
  headers_block.AppendValueOrAddHeader("key", "value2");
  auto envoy_headers = spdyHeaderBlockToEnvoyHeaders<Http::RequestHeaderMapImpl>(headers_block);
  // Envoy header block is 1 header larger because QUICHE header block does coalescing.
  EXPECT_EQ(headers_block.size() + 1u, envoy_headers->size());
  EXPECT_EQ("www.google.com", envoy_headers->getHostValue());
  EXPECT_EQ("/index.hml", envoy_headers->getPathValue());
  EXPECT_EQ("https", envoy_headers->getSchemeValue());
  EXPECT_EQ("value1", envoy_headers->get(Http::LowerCaseString("key"))[0]->value().getStringView());
  EXPECT_EQ("value2", envoy_headers->get(Http::LowerCaseString("key"))[1]->value().getStringView());

  quic::QuicHeaderList quic_headers;
  quic_headers.OnHeaderBlockStart();
  quic_headers.OnHeader(":authority", "www.google.com");
  quic_headers.OnHeader(":path", "/index.hml");
  quic_headers.OnHeader(":scheme", "https");
  quic_headers.OnHeader("key", "value1");
  quic_headers.OnHeader("key", "value2");
  quic_headers.OnHeader("key-to-drop", "");
  quic_headers.OnHeaderBlockEnd(0, 0);
  MockHeaderValidator validator;
  EXPECT_CALL(validator, validateHeader(_, _))
      .WillRepeatedly([](const std::string& header_name, absl::string_view) {
        if (header_name == "key-to-drop") {
          return Http::HeaderUtility::HeaderValidationResult::DROP;
        }
        return Http::HeaderUtility::HeaderValidationResult::ACCEPT;
      });
  auto envoy_headers2 =
      quicHeadersToEnvoyHeaders<Http::RequestHeaderMapImpl>(quic_headers, validator);
  EXPECT_EQ(*envoy_headers, *envoy_headers2);

  quic::QuicHeaderList quic_headers2;
  quic_headers2.OnHeaderBlockStart();
  quic_headers2.OnHeader(":authority", "www.google.com");
  quic_headers2.OnHeader(":path", "/index.hml");
  quic_headers2.OnHeader(":scheme", "https");
  quic_headers2.OnHeader("invalid_key", "");
  quic_headers2.OnHeaderBlockEnd(0, 0);
  EXPECT_CALL(validator, validateHeader(_, _))
      .WillRepeatedly([](const std::string& header_name, absl::string_view) {
        if (header_name == "invalid_key") {
          return Http::HeaderUtility::HeaderValidationResult::REJECT;
        }
        return Http::HeaderUtility::HeaderValidationResult::ACCEPT;
      });
  EXPECT_EQ(nullptr,
            quicHeadersToEnvoyHeaders<Http::RequestHeaderMapImpl>(quic_headers2, validator));
}

} // namespace Quic
} // namespace Envoy
