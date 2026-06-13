#include "source/extensions/network/dns_resolver/getaddrinfo/dns_packet_parser.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Network {
namespace {

// Helper to convert hex string to bytes
std::vector<uint8_t> hexToBytes(const std::string& hex) {
  std::vector<uint8_t> bytes;
  for (size_t i = 0; i < hex.length(); i += 2) {
    std::string byteString = hex.substr(i, 2);
    uint8_t byte = static_cast<uint8_t>(strtol(byteString.c_str(), nullptr, 16));
    bytes.push_back(byte);
  }
  return bytes;
}

TEST(DnsPacketParserTest, ValidHttpsRecord) {
  // A valid DNS response packet containing one HTTPS record.
  // Query: example.com, Type: HTTPS (65)
  // Answer: example.com, Type: HTTPS, TTL: 300, RDATA: SvcPriority=1, TargetName=., ECHConfig=...

  // Header:
  // Transaction ID: 0x1234
  // Flags: 0x8180 (Standard query response, No error)
  // Questions: 1, Answer RRs: 1, Authority RRs: 0, Additional RRs: 0
  std::string header = "123481800001000100000000";

  // Question: example.com (7example3com0), Type: HTTPS (0041), Class: IN (0001)
  std::string question = "076578616d706c6503636f6d0000410001";

  // Answer:
  // Name: example.com (compressed, pointer to 0x000c (12) which is the start of example.com in
  // question) 0xc00c Type: HTTPS (0041) Class: IN (0001) TTL: 300 (0000012c) RDLength: 10 (000a)
  // RDATA:
  //   SvcPriority: 1 (0001)
  //   TargetName: . (00)
  //   SvcParams:
  //     key: 5 (ech) (0005)
  //     len: 3 (0003)
  //     val: "foo" (666f6f)
  std::string answer = "c00c004100010000012c000a00010000050003666f6f";

  std::vector<uint8_t> packet = hexToBytes(header + question + answer);
  DnsPacketParser parser(std::move(packet));
  std::list<DnsResponse> results = parser.parseHttpsRecords();

  EXPECT_EQ(results.size(), 1);
  if (!results.empty()) {
    const auto& resp = results.front();
    EXPECT_EQ(resp.generic().ttl_.count(), 300);
    std::vector<uint8_t> expected_rdata = hexToBytes("00010000050003666f6f");
    EXPECT_EQ(resp.generic().rdata_, expected_rdata);
  }
}

TEST(DnsPacketParserTest, NonHttpsRecordIgnored) {
  // Header: 1 question, 2 answers
  std::string header = "123481800001000200000000";
  std::string question = "076578616d706c6503636f6d0000410001";

  // Answer 1: A record (Type 1), TTL 300, RDLEN 4, RDATA: 1.2.3.4
  std::string answer_a = "c00c000100010000012c000401020304";
  // Answer 2: HTTPS record, TTL 300, RDLEN 10, RDATA: priority 1, target ., ech "foo"
  std::string answer_https = "c00c004100010000012c000a00010000050003666f6f";

  std::vector<uint8_t> packet = hexToBytes(header + question + answer_a + answer_https);
  DnsPacketParser parser(std::move(packet));
  std::list<DnsResponse> results = parser.parseHttpsRecords();

  EXPECT_EQ(results.size(), 1); // Only HTTPS record is returned
}

TEST(DnsPacketParserTest, DnsErrorResponse) {
  // Header with RCODE = 3 (NXDOMAIN)
  std::string header = "123481830001000000000000";
  std::string question = "076578616d706c6503636f6d0000410001";

  std::vector<uint8_t> packet = hexToBytes(header + question);
  DnsPacketParser parser(std::move(packet));
  std::list<DnsResponse> results = parser.parseHttpsRecords();

  EXPECT_TRUE(results.empty());
}

TEST(DnsPacketParserTest, InvalidHeaderTooShort) {
  std::vector<uint8_t> packet = hexToBytes("123481800001");
  DnsPacketParser parser(std::move(packet));
  std::list<DnsResponse> results = parser.parseHttpsRecords();
  EXPECT_TRUE(results.empty());
}

} // namespace
} // namespace Network
} // namespace Envoy
