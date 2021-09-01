#pragma once

#include "test/integration/http_integration.h"

#include "gtest/gtest.h"

namespace Envoy {

struct HttpProtocolTestParams {
  Network::Address::IpVersion version;
  Http::CodecType downstream_protocol;
  Http::CodecType upstream_protocol;
};

// Allows easy testing of Envoy code for HTTP/HTTP2 upstream/downstream.
//
// Usage:
//
// using MyTest = HttpProtocolIntegrationTest;
//
// INSTANTIATE_TEST_SUITE_P(Protocols, MyTest,
//                         testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParams()),
//                         HttpProtocolIntegrationTest::protocolTestParamsToString);
//
//
// TEST_P(MyTest, TestInstance) {
// ....
// }
class HttpProtocolIntegrationTest : public testing::TestWithParam<HttpProtocolTestParams>,
                                    public HttpIntegrationTest {
public:
  // By default returns 8 combinations of
  // [HTTP  upstream / HTTP  downstream] x [Ipv4, IPv6]
  // [HTTP  upstream / HTTP2 downstream] x [IPv4, Ipv6]
  // [HTTP2 upstream / HTTP  downstream] x [Ipv4, IPv6]
  // [HTTP2 upstream / HTTP2 downstream] x [IPv4, Ipv6]
  //
  // Upstream and downstream protocols may be changed via the input vectors.
  // Address combinations are propagated from TestEnvironment::getIpVersionsForTest()
  static std::vector<HttpProtocolTestParams> getProtocolTestParams(
      const std::vector<Http::CodecType>& downstream_protocols = {Http::CodecType::HTTP1,
                                                                  Http::CodecType::HTTP2},
      const std::vector<Http::CodecType>& upstream_protocols = {Http::CodecType::HTTP1,
                                                                Http::CodecType::HTTP2});

  // Allows pretty printed test names of the form
  // FooTestCase.BarInstance/IPv4_Http2Downstream_HttpUpstream
  static std::string
  protocolTestParamsToString(const ::testing::TestParamInfo<HttpProtocolTestParams>& p);

  HttpProtocolIntegrationTest()
      : HttpIntegrationTest(
            GetParam().downstream_protocol, GetParam().version,
            ConfigHelper::httpProxyConfig(/*downstream_is_quic=*/GetParam().downstream_protocol ==
                                          Http::CodecType::HTTP3)) {}

  void SetUp() override {
    setDownstreamProtocol(GetParam().downstream_protocol);
    setUpstreamProtocol(GetParam().upstream_protocol);
  }

protected:
  void expectUpstreamWireBytesSentAndReceived(std::string log_name, int log_index,
                                              int h1_wire_bytes_sent, int h1_wire_bytes_received,
                                              int h1_body_bytes_sent, int h1_body_bytes_received,
                                              int h2_wire_bytes_sent, int h2_wire_bytes_received,
                                              int h2_body_bytes_sent, int h2_body_bytes_received) {
    auto integer_near = [](int x, int y) -> bool { return std::abs(x - y) <= (x / 20); };
    std::string access_log = waitForAccessLog(log_name, log_index);
    std::vector<std::string> log_entries = absl::StrSplit(access_log, ' ');
    int wire_bytes_sent = std::stoi(log_entries[0]),
        wire_bytes_received = std::stoi(log_entries[1]),
        body_bytes_sent = std::stoi(log_entries[2]),
        body_bytes_received = std::stoi(log_entries[3]);
    if (upstreamProtocol() == Http::CodecType::HTTP1) {
      EXPECT_TRUE(integer_near(h1_wire_bytes_sent, wire_bytes_sent))
          << "expect: " << h1_wire_bytes_sent << ", actual: " << wire_bytes_sent;
      EXPECT_TRUE(integer_near(h1_wire_bytes_received, wire_bytes_received))
          << "expect: " << h1_wire_bytes_received << ", actual: " << wire_bytes_received;
      EXPECT_TRUE(integer_near(h1_body_bytes_sent, body_bytes_sent))
          << "expect: " << h1_body_bytes_sent << ", actual: " << body_bytes_sent;
      EXPECT_TRUE(integer_near(h1_body_bytes_received, body_bytes_received))
          << "expect: " << h1_body_bytes_received << ", actual: " << body_bytes_received;
    }
    if (upstreamProtocol() == Http::CodecType::HTTP2) {
      // Because of non-deterministic h2 compression, the same plain text length don't map to the
      // same number of wire bytes.
      EXPECT_TRUE(integer_near(h2_wire_bytes_sent, wire_bytes_sent))
          << "expect: " << h2_wire_bytes_sent << ", actual: " << wire_bytes_sent;
      EXPECT_TRUE(integer_near(h2_wire_bytes_received, wire_bytes_received))
          << "expect: " << h2_wire_bytes_received << ", actual: " << wire_bytes_received;
      EXPECT_TRUE(integer_near(h2_body_bytes_sent, body_bytes_sent))
          << "expect: " << h2_body_bytes_sent << ", actual: " << body_bytes_sent;
      EXPECT_TRUE(integer_near(h2_body_bytes_received, body_bytes_received))
          << "expect: " << h2_body_bytes_received << ", actual: " << body_bytes_received;
    }
  }

  void expectDownstreamWireBytesSentAndReceived(std::string log_name, int log_index,
                                                int h1_wire_bytes_sent, int h1_wire_bytes_received,
                                                int h1_body_bytes_sent, int h1_body_bytes_received,
                                                int h2_wire_bytes_sent, int h2_wire_bytes_received,
                                                int h2_body_bytes_sent,
                                                int h2_body_bytes_received) {
    auto integer_near = [](int x, int y) -> bool { return std::abs(x - y) <= (x / 10); };
    std::string access_log = waitForAccessLog(log_name, log_index);
    std::vector<std::string> log_entries = absl::StrSplit(access_log, ' ');
    int wire_bytes_sent = std::stoi(log_entries[0]),
        wire_bytes_received = std::stoi(log_entries[1]),
        body_bytes_sent = std::stoi(log_entries[2]),
        body_bytes_received = std::stoi(log_entries[3]);
    if (downstreamProtocol() == Http::CodecType::HTTP1) {
      EXPECT_TRUE(integer_near(h1_wire_bytes_sent, wire_bytes_sent))
          << "expect: " << h1_wire_bytes_sent << ", actual: " << wire_bytes_sent;
      EXPECT_TRUE(integer_near(h1_wire_bytes_received, wire_bytes_received))
          << "expect: " << h1_wire_bytes_received << ", actual: " << wire_bytes_received;
      EXPECT_TRUE(integer_near(h1_body_bytes_sent, body_bytes_sent))
          << "expect: " << h1_body_bytes_sent << ", actual: " << body_bytes_sent;
      EXPECT_TRUE(integer_near(h1_body_bytes_received, body_bytes_received))
          << "expect: " << h1_body_bytes_received << ", actual: " << body_bytes_received;
    }
    if (downstreamProtocol() == Http::CodecType::HTTP2) {
      // Because of non-deterministic h2 compression, the same plain text length don't map to the
      // same number of wire bytes.
      EXPECT_TRUE(integer_near(h2_wire_bytes_sent, wire_bytes_sent))
          << "expect: " << h2_wire_bytes_sent << ", actual: " << wire_bytes_sent;
      EXPECT_TRUE(integer_near(h2_wire_bytes_received, wire_bytes_received))
          << "expect: " << h2_wire_bytes_received << ", actual: " << wire_bytes_received;
      EXPECT_TRUE(integer_near(h2_body_bytes_sent, body_bytes_sent))
          << "expect: " << h2_body_bytes_sent << ", actual: " << body_bytes_sent;
      EXPECT_TRUE(integer_near(h2_body_bytes_received, body_bytes_received))
          << "expect: " << h2_body_bytes_received << ", actual: " << body_bytes_received;
    }
  }
};

} // namespace Envoy
