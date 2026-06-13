#include "source/common/common/dns_utils.h"
#include "source/common/network/utility.h"

#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace {

TEST(DnsUtils, MultipleGenerateTest) {
  std::list<Network::DnsResponse> responses =
      TestUtility::makeDnsResponse({"10.0.0.1", "10.0.0.2"});
  std::vector<Network::Address::InstanceConstSharedPtr> addresses =
      DnsUtils::generateAddressList(responses, 2);
  ASSERT_EQ(2, addresses.size());
  EXPECT_EQ(addresses[0]->asString(), "10.0.0.1:2");
  EXPECT_EQ(addresses[1]->asString(), "10.0.0.2:2");
}

TEST(DnsUtils, ListChanged) {
  Network::Address::InstanceConstSharedPtr address1 =
      Network::Utility::parseInternetAddressNoThrow("10.0.0.1");
  Network::Address::InstanceConstSharedPtr address1_dup =
      Network::Utility::parseInternetAddressNoThrow("10.0.0.1");
  Network::Address::InstanceConstSharedPtr address2 =
      Network::Utility::parseInternetAddressNoThrow("10.0.0.2");

  std::vector<Network::Address::InstanceConstSharedPtr> addresses1 = {address1, address2};
  std::vector<Network::Address::InstanceConstSharedPtr> addresses2 = {address1_dup, address2};
  EXPECT_FALSE(DnsUtils::listChanged(addresses1, addresses2));

  std::vector<Network::Address::InstanceConstSharedPtr> addresses3 = {address2, address1};
  EXPECT_TRUE(DnsUtils::listChanged(addresses1, addresses3));
  EXPECT_TRUE(DnsUtils::listChanged(addresses1, {address2}));
}

TEST(DnsUtils, ParseHttpsRecordValid) {
  std::vector<uint8_t> rdata = {0x00, 0x01, 0x00, 0x00, 0x05, 0x00, 0x04, 0xaa, 0xbb, 0xcc, 0xdd};
  std::vector<uint8_t> expected = {0xaa, 0xbb, 0xcc, 0xdd};
  EXPECT_EQ(DnsUtils::parseHttpsRecord(rdata), expected);
}

TEST(DnsUtils, ParseHttpsRecordMultipleParams) {
  std::vector<uint8_t> rdata = {
      0x00, 0x01, // Priority
      0x03, 'w',  'w',  'w',  0x07, 'e',  'x',  'a',  'm', 'p', 'l', 'e', 0x03, 'c', 'o', 'm',
      0x00,                                                // End of TargetName
      0x00, 0x01, 0x00, 0x05, 'h',  '2',  ',',  'h',  '3', // Param 1 (alpn)
      0x00, 0x05, 0x00, 0x04, 0xaa, 0xbb, 0xcc, 0xdd,      // Param 2 (ech)
      0x00, 0x06, 0x00, 0x04, 0x01, 0x02, 0x03, 0x04       // Param 3 (ipv4hint)
  };
  std::vector<uint8_t> expected = {0xaa, 0xbb, 0xcc, 0xdd};
  EXPECT_EQ(DnsUtils::parseHttpsRecord(rdata), expected);
}

TEST(DnsUtils, ParseHttpsRecordNoEch) {
  std::vector<uint8_t> rdata = {0x00, 0x01, 0x00, 0x00, 0x01, 0x00, 0x02, 0x11, 0x22};
  EXPECT_TRUE(DnsUtils::parseHttpsRecord(rdata).empty());
}

TEST(DnsUtils, ParseHttpsRecordTooShort) {
  std::vector<uint8_t> rdata = {0x00};
  EXPECT_TRUE(DnsUtils::parseHttpsRecord(rdata).empty());

  rdata = {0x00, 0x01, 0x00};
  EXPECT_TRUE(DnsUtils::parseHttpsRecord(rdata).empty());
}

TEST(DnsUtils, ParseHttpsRecordTruncatedParam) {
  std::vector<uint8_t> rdata = {
      0x00, 0x01, 0x00, 0x00, 0x05, 0x00, 0x0a, // len 10
      0xaa, 0xbb, 0xcc, 0xdd                    // only 4 bytes
  };
  EXPECT_TRUE(DnsUtils::parseHttpsRecord(rdata).empty());
}

TEST(DnsUtils, ParseHttpsRecordCompressedTargetName) {
  std::vector<uint8_t> rdata = {0x00, 0x01, 0xc0, 0x0c, // Pointer
                                0x00, 0x05, 0x00, 0x02, 0x11, 0x22};
  std::vector<uint8_t> expected = {0x11, 0x22};
  EXPECT_EQ(DnsUtils::parseHttpsRecord(rdata), expected);
}

} // namespace
} // namespace Envoy
