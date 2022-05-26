#include "test/integration/http_protocol_integration.h"

#include "absl/strings/str_cat.h"

namespace Envoy {
std::vector<HttpProtocolTestParams> HttpProtocolIntegrationTest::getProtocolTestParams(
    const std::vector<Http::CodecType>& downstream_protocols,
    const std::vector<Http::CodecType>& upstream_protocols) {
  std::vector<HttpProtocolTestParams> ret;

  const auto addHttp2TestParametersWithNewCodecWrapperOrDeferredProcessing =
      [&ret](Network::Address::IpVersion ip_version, Http::CodecType downstream_protocol,
             Http::CodecType upstream_protocol) {
        for (Http2Impl impl : {Http2Impl::WrappedNghttp2, Http2Impl::Oghttp2}) {
          ret.push_back(HttpProtocolTestParams{ip_version, downstream_protocol, upstream_protocol,
                                               impl, false});
          ret.push_back(HttpProtocolTestParams{ip_version, downstream_protocol, upstream_protocol,
                                               impl, true});
        }
        ret.push_back(HttpProtocolTestParams{ip_version, downstream_protocol, upstream_protocol,
                                             Http2Impl::Nghttp2, true});
      };

  for (auto ip_version : TestEnvironment::getIpVersionsForTest()) {
    for (auto downstream_protocol : downstream_protocols) {
      for (auto upstream_protocol : upstream_protocols) {
#ifdef ENVOY_ENABLE_QUIC
        ret.push_back(HttpProtocolTestParams{ip_version, downstream_protocol, upstream_protocol,
                                             Http2Impl::Nghttp2, false});
        if (downstream_protocol == Http::CodecType::HTTP2 ||
            upstream_protocol == Http::CodecType::HTTP2) {
          addHttp2TestParametersWithNewCodecWrapperOrDeferredProcessing(
              ip_version, downstream_protocol, upstream_protocol);
        }
#else
        if (downstream_protocol == Http::CodecType::HTTP3 ||
            upstream_protocol == Http::CodecType::HTTP3) {
          ENVOY_LOG_MISC(warn, "Skipping HTTP/3 as support is compiled out");
        } else {
          ret.push_back(HttpProtocolTestParams{ip_version, downstream_protocol, upstream_protocol,
                                               Http2Impl::Nghttp2, false});
          if (downstream_protocol == Http::CodecType::HTTP2 ||
              upstream_protocol == Http::CodecType::HTTP2) {
            addHttp2TestParametersWithNewCodecWrapperOrDeferredProcessing(
                ip_version, downstream_protocol, upstream_protocol);
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

absl::string_view implementationToString(Http2Impl impl) {
  switch (impl) {
  case Http2Impl::Nghttp2:
    return "Nghttp2";
  case Http2Impl::WrappedNghttp2:
    return "WrappedNghttp2";
  case Http2Impl::Oghttp2:
    return "Oghttp2";
  }
  return "UnknownHttp2Impl";
}

std::string HttpProtocolIntegrationTest::protocolTestParamsToString(
    const ::testing::TestParamInfo<HttpProtocolTestParams>& params) {
  return absl::StrCat((params.param.version == Network::Address::IpVersion::v4 ? "IPv4_" : "IPv6_"),
                      downstreamToString(params.param.downstream_protocol),
                      upstreamToString(params.param.upstream_protocol),
                      implementationToString(params.param.http2_implementation),
                      params.param.defer_processing_backedup_streams ? "WithDeferredProcessing"
                                                                     : "NoDeferredProcessing");
}

void HttpProtocolIntegrationTest::expectUpstreamBytesSentAndReceived(
    BytesCountExpectation h1_expectation, BytesCountExpectation h2_expectation,
    BytesCountExpectation h3_expectation, const int id) {
  auto integer_near = [](int x, int y) -> bool { return std::abs(x - y) <= (x / 20); };
  std::string access_log = waitForAccessLog(access_log_name_, id);
  std::vector<std::string> log_entries = absl::StrSplit(access_log, ' ');
  int wire_bytes_sent = std::stoi(log_entries[0]), wire_bytes_received = std::stoi(log_entries[1]),
      header_bytes_sent = std::stoi(log_entries[2]),
      header_bytes_received = std::stoi(log_entries[3]);
  switch (upstreamProtocol()) {
  case Http::CodecType::HTTP1: {
    EXPECT_EQ(h1_expectation.wire_bytes_sent_, wire_bytes_sent)
        << "expect: " << h1_expectation.wire_bytes_sent_ << ", actual: " << wire_bytes_sent;
    EXPECT_EQ(h1_expectation.wire_bytes_received_, wire_bytes_received)
        << "expect: " << h1_expectation.wire_bytes_received_ << ", actual: " << wire_bytes_received;
    EXPECT_EQ(h1_expectation.header_bytes_sent_, header_bytes_sent)
        << "expect: " << h1_expectation.header_bytes_sent_ << ", actual: " << header_bytes_sent;
    EXPECT_EQ(h1_expectation.header_bytes_received_, header_bytes_received)
        << "expect: " << h1_expectation.header_bytes_received_
        << ", actual: " << header_bytes_received;
    return;
  }
  case Http::CodecType::HTTP2: {
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
    return;
  }
  case Http::CodecType::HTTP3: {
    // Because of non-deterministic h2 compression, the same plain text length don't map to the
    // Same number of wire bytes.
    EXPECT_TRUE(integer_near(h3_expectation.wire_bytes_sent_, wire_bytes_sent))
        << "expect: " << h3_expectation.wire_bytes_sent_ << ", actual: " << wire_bytes_sent;
    EXPECT_TRUE(integer_near(h3_expectation.wire_bytes_received_, wire_bytes_received))
        << "expect: " << h3_expectation.wire_bytes_received_ << ", actual: " << wire_bytes_received;
    EXPECT_TRUE(integer_near(h3_expectation.header_bytes_sent_, header_bytes_sent))
        << "expect: " << h3_expectation.header_bytes_sent_ << ", actual: " << header_bytes_sent;
    EXPECT_TRUE(integer_near(h3_expectation.header_bytes_received_, header_bytes_received))
        << "expect: " << h3_expectation.header_bytes_received_
        << ", actual: " << header_bytes_received;
    return;
  }

  default:
    EXPECT_TRUE(false) << "Unexpected codec type: " << static_cast<int>(upstreamProtocol());
  }
}

void HttpProtocolIntegrationTest::expectDownstreamBytesSentAndReceived(
    BytesCountExpectation h1_expectation, BytesCountExpectation h2_expectation,
    BytesCountExpectation h3_expectation, const int id) {
  auto integer_near = [](int x, int y) -> bool { return std::abs(x - y) <= (x / 5); };
  std::string access_log = waitForAccessLog(access_log_name_, id);
  std::vector<std::string> log_entries = absl::StrSplit(access_log, ' ');
  int wire_bytes_sent = std::stoi(log_entries[0]), wire_bytes_received = std::stoi(log_entries[1]),
      header_bytes_sent = std::stoi(log_entries[2]),
      header_bytes_received = std::stoi(log_entries[3]);
  switch (downstreamProtocol()) {
  case Http::CodecType::HTTP1: {
    EXPECT_TRUE(integer_near(h1_expectation.wire_bytes_sent_, wire_bytes_sent))
        << "expect: " << h1_expectation.wire_bytes_sent_ << ", actual: " << wire_bytes_sent;
    EXPECT_EQ(h1_expectation.wire_bytes_received_, wire_bytes_received)
        << "expect: " << h1_expectation.wire_bytes_received_ << ", actual: " << wire_bytes_received;
    EXPECT_TRUE(integer_near(h1_expectation.header_bytes_sent_, header_bytes_sent))
        << "expect: " << h1_expectation.header_bytes_sent_ << ", actual: " << header_bytes_sent;
    EXPECT_EQ(h1_expectation.header_bytes_received_, header_bytes_received)
        << "expect: " << h1_expectation.header_bytes_received_
        << ", actual: " << header_bytes_received;
    return;
  }
  case Http::CodecType::HTTP2: {
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
    return;
  }
  case Http::CodecType::HTTP3: {
    // Because of non-deterministic h3 compression, the same plain text length don't map to the
    // same number of wire bytes.
    EXPECT_TRUE(integer_near(h3_expectation.wire_bytes_sent_, wire_bytes_sent))
        << "expect: " << h3_expectation.wire_bytes_sent_ << ", actual: " << wire_bytes_sent;
    EXPECT_TRUE(integer_near(h3_expectation.wire_bytes_received_, wire_bytes_received))
        << "expect: " << h3_expectation.wire_bytes_received_ << ", actual: " << wire_bytes_received;
    EXPECT_TRUE(integer_near(h3_expectation.header_bytes_sent_, header_bytes_sent))
        << "expect: " << h3_expectation.header_bytes_sent_ << ", actual: " << header_bytes_sent;
    EXPECT_TRUE(integer_near(h3_expectation.header_bytes_received_, header_bytes_received))
        << "expect: " << h3_expectation.header_bytes_received_
        << ", actual: " << header_bytes_received;
    return;
  }
  default:
    EXPECT_TRUE(false) << "Unexpected codec type: " << static_cast<int>(downstreamProtocol());
  }
}

} // namespace Envoy
