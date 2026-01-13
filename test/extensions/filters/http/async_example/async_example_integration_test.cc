
#include "test/integration/http_integration.h"
#include "source/extensions/filters/http/async_example/v3/async_example.pb.h"

#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace {

class AsyncExampleIntegrationTest : public HttpIntegrationTest,
                                    public testing::TestWithParam<Http::CodecType> {
public:
  AsyncExampleIntegrationTest()
      : HttpIntegrationTest(GetParam(), Network::Address::IpVersion::v4) {}

  void initializeFilter() {
    config_helper_.addConfigModifier(
        [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
               hcm) {
          auto* filter = hcm.mutable_http_filters()->Add();
          filter->set_name("envoy.filters.http.async_example");
          filter->mutable_typed_config()->PackFrom(
              envoy::extensions::filters::http::async_example::v3::AsyncExample());

          // Move to the beginning of the filter chain (before router)
          for (int i = hcm.http_filters_size() - 1; i > 0; --i) {
            hcm.mutable_http_filters()->SwapElements(i, i - 1);
          }
        });
  }
};

INSTANTIATE_TEST_SUITE_P(Codecs, AsyncExampleIntegrationTest,
                         testing::Values(Http::CodecType::HTTP1, Http::CodecType::HTTP2,
                                         Http::CodecType::HTTP3),
                         [](const testing::TestParamInfo<Http::CodecType>& params) {
                           return std::string(downstreamToString(params.param));
                         });

TEST_P(AsyncExampleIntegrationTest, LargeBodyAsyncTest) {
  initializeFilter();
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  std::string body;
  for (int i = 0; i < 100000; ++i) {
    body += std::to_string(i) + " ";
  }
  // Remove last space
  if (!body.empty()) {
    body.pop_back();
  }

  auto response = codec_client_->makeRequestWithBody(
      Http::TestRequestHeaderMapImpl{
          {":method", "POST"}, {":path", "/"}, {":scheme", "http"}, {":authority", "host"}},
      body);

  waitForNextUpstreamRequest();
  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));
  EXPECT_EQ(body.size(), upstream_request_->bodyLength());
  EXPECT_EQ(body, upstream_request_->body().toString());

  upstream_request_->encodeHeaders(default_response_headers_, true);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
}

} // namespace
} // namespace Envoy
