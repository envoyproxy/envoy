#include "test/integration/http_protocol_integration.h"

#include "absl/strings/str_cat.h"

namespace Envoy {
std::vector<HttpProtocolTestParams> HttpProtocolIntegrationTest::getProtocolTestParams(
    const std::vector<Http::CodecType>& downstream_protocols,
    const std::vector<Http::CodecType>& upstream_protocols) {
  std::vector<HttpProtocolTestParams> ret;

  for (auto ip_version : TestEnvironment::getIpVersionsForTest()) {
    for (auto downstream_protocol : downstream_protocols) {
      for (auto upstream_protocol : upstream_protocols) {
#ifdef ENVOY_ENABLE_QUIC
        ret.push_back(
            HttpProtocolTestParams{ip_version, downstream_protocol, upstream_protocol, false});
        if (downstream_protocol == Http::CodecType::HTTP2 ||
            upstream_protocol == Http::CodecType::HTTP2) {
          ret.push_back(
              HttpProtocolTestParams{ip_version, downstream_protocol, upstream_protocol, true});
        }
#else
        if (downstream_protocol == Http::CodecType::HTTP3 ||
            upstream_protocol == Http::CodecType::HTTP3) {
          ENVOY_LOG_MISC(warn, "Skipping HTTP/3 as support is compiled out");
        } else {
          ret.push_back(
              HttpProtocolTestParams{ip_version, downstream_protocol, upstream_protocol, false});
          if (downstream_protocol == Http::CodecType::HTTP2 ||
              upstream_protocol == Http::CodecType::HTTP2) {
            ret.push_back(
                HttpProtocolTestParams{ip_version, downstream_protocol, upstream_protocol, true});
          }
        }
#endif
      }
    }
  }
  return ret;
}

absl::string_view upstreamToString(Http::CodecType type) {
  switch (type) {
  case Http::CodecType::HTTP1:
    return "HttpUpstream";
  case Http::CodecType::HTTP2:
    return "Http2Upstream";
  case Http::CodecType::HTTP3:
    return "Http3Upstream";
  }
  return "UnknownUpstream";
}

absl::string_view downstreamToString(Http::CodecType type) {
  switch (type) {
  case Http::CodecType::HTTP1:
    return "HttpDownstream_";
  case Http::CodecType::HTTP2:
    return "Http2Downstream_";
  case Http::CodecType::HTTP3:
    return "Http3Downstream_";
  }
  return "UnknownDownstream";
}

std::string HttpProtocolIntegrationTest::protocolTestParamsToString(
    const ::testing::TestParamInfo<HttpProtocolTestParams>& params) {
  return absl::StrCat((params.param.version == Network::Address::IpVersion::v4 ? "IPv4_" : "IPv6_"),
                      downstreamToString(params.param.downstream_protocol),
                      upstreamToString(params.param.upstream_protocol),
                      params.param.http2_new_codec_wrapper ? "WrappedHttp2" : "BareHttp2");
}

void HttpProtocolIntegrationTest::expectUpstreamBytesSentAndReceived(
    BytesCountExpectation h1_expectation, BytesCountExpectation h2_expectation, const int id) {
  auto integer_near = [](int x, int y) -> bool { return std::abs(x - y) <= (x / 20); };
  std::string access_log = waitForAccessLog(access_log_name_, id);
  std::vector<std::string> log_entries = absl::StrSplit(access_log, ' ');
  int wire_bytes_sent = std::stoi(log_entries[0]), wire_bytes_received = std::stoi(log_entries[1]),
      header_bytes_sent = std::stoi(log_entries[2]),
      header_bytes_received = std::stoi(log_entries[3]);
  if (upstreamProtocol() == Http::CodecType::HTTP1) {
    EXPECT_EQ(h1_expectation.wire_bytes_sent_, wire_bytes_sent)
        << "expect: " << h1_expectation.wire_bytes_sent_ << ", actual: " << wire_bytes_sent;
    EXPECT_EQ(h1_expectation.wire_bytes_received_, wire_bytes_received)
        << "expect: " << h1_expectation.wire_bytes_received_ << ", actual: " << wire_bytes_received;
    EXPECT_EQ(h1_expectation.header_bytes_sent_, header_bytes_sent)
        << "expect: " << h1_expectation.header_bytes_sent_ << ", actual: " << header_bytes_sent;
    EXPECT_EQ(h1_expectation.header_bytes_received_, header_bytes_received)
        << "expect: " << h1_expectation.header_bytes_received_
        << ", actual: " << header_bytes_received;
  }
  if (upstreamProtocol() == Http::CodecType::HTTP2) {
    // Because of non-deterministic h2 compression, the same plain text length don't map to the
    // same number of wire bytes.
    EXPECT_TRUE(integer_near(h2_expectation.wire_bytes_sent_, wire_bytes_sent))
        << "expect: " << h2_expectation.wire_bytes_sent_ << ", actual: " << wire_bytes_sent;
    EXPECT_TRUE(integer_near(h2_expectation.wire_bytes_received_, wire_bytes_received))
        << "expect: " << h2_expectation.wire_bytes_received_ << ", actual: " << wire_bytes_received;
    EXPECT_TRUE(integer_near(h2_expectation.header_bytes_sent_, header_bytes_sent))
        << "expect: " << h2_expectation.header_bytes_sent_ << ", actual: " << header_bytes_sent;
    EXPECT_TRUE(integer_near(h2_expectation.header_bytes_received_, header_bytes_received))
        << "expect: " << h2_expectation.header_bytes_received_
        << ", actual: " << header_bytes_received;
  }
}

void HttpProtocolIntegrationTest::expectDownstreamBytesSentAndReceived(
    BytesCountExpectation h1_expectation, BytesCountExpectation h2_expectation, const int id) {
  auto integer_near = [](int x, int y) -> bool { return std::abs(x - y) <= (x / 5); };
  std::string access_log = waitForAccessLog(access_log_name_, id);
  std::vector<std::string> log_entries = absl::StrSplit(access_log, ' ');
  int wire_bytes_sent = std::stoi(log_entries[0]), wire_bytes_received = std::stoi(log_entries[1]),
      header_bytes_sent = std::stoi(log_entries[2]),
      header_bytes_received = std::stoi(log_entries[3]);
  if (downstreamProtocol() == Http::CodecType::HTTP1) {
    EXPECT_TRUE(integer_near(h1_expectation.wire_bytes_sent_, wire_bytes_sent))
        << "expect: " << h1_expectation.wire_bytes_sent_ << ", actual: " << wire_bytes_sent;
    EXPECT_EQ(h1_expectation.wire_bytes_received_, wire_bytes_received)
        << "expect: " << h1_expectation.wire_bytes_received_ << ", actual: " << wire_bytes_received;
    EXPECT_TRUE(integer_near(h1_expectation.header_bytes_sent_, header_bytes_sent))
        << "expect: " << h1_expectation.header_bytes_sent_ << ", actual: " << header_bytes_sent;
    EXPECT_EQ(h1_expectation.header_bytes_received_, header_bytes_received)
        << "expect: " << h1_expectation.header_bytes_received_
        << ", actual: " << header_bytes_received;
  }
  if (downstreamProtocol() == Http::CodecType::HTTP2) {
    // Because of non-deterministic h2 compression, the same plain text length don't map to the
    // same number of wire bytes.
    EXPECT_TRUE(integer_near(h2_expectation.wire_bytes_sent_, wire_bytes_sent))
        << "expect: " << h2_expectation.wire_bytes_sent_ << ", actual: " << wire_bytes_sent;
    EXPECT_TRUE(integer_near(h2_expectation.wire_bytes_received_, wire_bytes_received))
        << "expect: " << h2_expectation.wire_bytes_received_ << ", actual: " << wire_bytes_received;
    EXPECT_TRUE(integer_near(h2_expectation.header_bytes_sent_, header_bytes_sent))
        << "expect: " << h2_expectation.header_bytes_sent_ << ", actual: " << header_bytes_sent;
    EXPECT_TRUE(integer_near(h2_expectation.header_bytes_received_, header_bytes_received))
        << "expect: " << h2_expectation.header_bytes_received_
        << ", actual: " << header_bytes_received;
  }
}

} // namespace Envoy
