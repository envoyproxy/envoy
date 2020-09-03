#include <string>

#include "envoy/extensions/filters/http/cdn_loop/v3alpha/cdn_loop.pb.h"

#include "test/integration/http_protocol_integration.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace CdnLoop {
namespace {

const std::string MaxDefaultConfig = R"EOF(
name: envoy.filters.http.cdn_loop
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.cdn_loop.v3alpha.CdnLoopConfig
  cdn_id: cdn
)EOF";

const std::string MaxOf2Config = R"EOF(
name: envoy.filters.http.cdn_loop
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.cdn_loop.v3alpha.CdnLoopConfig
  cdn_id: cdn
  max_allowed_occurences: 2
)EOF";

class CdnLoopFilterIntegrationTest : public HttpProtocolIntegrationTest {};

TEST_P(CdnLoopFilterIntegrationTest, NoCdnLoopHeader) {
  config_helper_.addFilter(MaxDefaultConfig);
  initialize();
  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));

  Http::TestRequestHeaderMapImpl request_headers{
      {":method", "GET"}, {":path", "/"}, {":scheme", "http"}, {":authority", "host"}};

  auto response = sendRequestAndWaitForResponse(request_headers, 0, default_response_headers_, 0);

  const auto* payload_entry = upstream_request_->headers().get(Http::LowerCaseString("CDN-Loop"));
  ASSERT_NE(payload_entry, nullptr);
  EXPECT_EQ(payload_entry->value().getStringView(), "cdn");
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
}

TEST_P(CdnLoopFilterIntegrationTest, CdnLoopHeaderWithOtherCdns) {
  config_helper_.addFilter(MaxDefaultConfig);
  initialize();
  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));

  Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"},
                                                 {":path", "/"},
                                                 {":scheme", "http"},
                                                 {":authority", "host"},
                                                 {"CDN-Loop", "cdn1,cdn2"}};

  auto response = sendRequestAndWaitForResponse(request_headers, 0, default_response_headers_, 0);

  const auto* payload_entry = upstream_request_->headers().get(Http::LowerCaseString("CDN-Loop"));
  ASSERT_NE(payload_entry, nullptr);
  EXPECT_EQ(payload_entry->value().getStringView(), "cdn1,cdn2,cdn");
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
}

TEST_P(CdnLoopFilterIntegrationTest, CdnLoop0Allowed1Seen) {
  config_helper_.addFilter(MaxDefaultConfig);
  initialize();
  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));

  Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"},
                                                 {":path", "/"},
                                                 {":scheme", "http"},
                                                 {":authority", "host"},
                                                 {"CDN-Loop", "cdn"}};

  auto response = codec_client_->makeHeaderOnlyRequest(request_headers);
  response->waitForEndStream();
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("502", response->headers().getStatusValue());
}

TEST_P(CdnLoopFilterIntegrationTest, UnparseableHeader) {
  config_helper_.addFilter(MaxDefaultConfig);
  initialize();
  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));

  Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"},
                                                 {":path", "/"},
                                                 {":scheme", "http"},
                                                 {":authority", "host"},
                                                 {"CDN-Loop", "[bad-header"}};

  auto response = codec_client_->makeHeaderOnlyRequest(request_headers);
  response->waitForEndStream();
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("400", response->headers().getStatusValue());
}

TEST_P(CdnLoopFilterIntegrationTest, CdnLoop2Allowed1Seen) {
  config_helper_.addFilter(MaxOf2Config);
  initialize();
  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));

  Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"},
                                                 {":path", "/"},
                                                 {":scheme", "http"},
                                                 {":authority", "host"},
                                                 {"CDN-Loop", "cdn"}};

  auto response = sendRequestAndWaitForResponse(request_headers, 0, default_response_headers_, 0);

  const auto* payload_entry = upstream_request_->headers().get(Http::LowerCaseString("CDN-Loop"));
  ASSERT_NE(payload_entry, nullptr);
  EXPECT_EQ(payload_entry->value().getStringView(), "cdn,cdn");
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
}

TEST_P(CdnLoopFilterIntegrationTest, CdnLoop2Allowed2Seen) {
  config_helper_.addFilter(MaxOf2Config);
  initialize();
  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));

  Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"},
                                                 {":path", "/"},
                                                 {":scheme", "http"},
                                                 {":authority", "host"},
                                                 {"CDN-Loop", "cdn, cdn"}};

  auto response = sendRequestAndWaitForResponse(request_headers, 0, default_response_headers_, 0);

  const auto* payload_entry = upstream_request_->headers().get(Http::LowerCaseString("CDN-Loop"));
  ASSERT_NE(payload_entry, nullptr);
  EXPECT_EQ(payload_entry->value().getStringView(), "cdn, cdn,cdn");
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
}

TEST_P(CdnLoopFilterIntegrationTest, CdnLoop2Allowed3Seen) {
  config_helper_.addFilter(MaxOf2Config);
  initialize();
  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));

  Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"},
                                                 {":path", "/"},
                                                 {":scheme", "http"},
                                                 {":authority", "host"},
                                                 {"CDN-Loop", "cdn, cdn, cdn"}};

  auto response = codec_client_->makeHeaderOnlyRequest(request_headers);
  response->waitForEndStream();
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("502", response->headers().getStatusValue());
}

INSTANTIATE_TEST_SUITE_P(Protocols, CdnLoopFilterIntegrationTest,
                         testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParams()),
                         HttpProtocolIntegrationTest::protocolTestParamsToString);

} // namespace
} // namespace CdnLoop
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
