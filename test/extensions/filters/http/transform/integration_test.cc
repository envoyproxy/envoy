#include "source/extensions/filters/http/transform/transform.h"

#include "test/integration/http_integration.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Transform {
namespace {

class TransformIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                 public HttpIntegrationTest {
public:
  TransformIntegrationTest() : HttpIntegrationTest(Http::CodecClient::Type::HTTP1, GetParam()) {}

  void setFilter(const std::string& filter_config) {
    config_helper_.prependFilter(filter_config, true);
  }

  void setPerFilterConfig(const std::string& route_config) {
    config_helper_.addConfigModifier(
        [route_config](
            envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
                hcm) {
          auto* route = hcm.mutable_route_config()->mutable_virtual_hosts(0)->mutable_routes(0);
          route->mutable_match()->set_path("/default/route");

          // Per route header mutation.
          ProtoConfig proto_config;
          TestUtility::loadFromYaml(route_config, proto_config);

          Protobuf::Any per_route_config;
          per_route_config.PackFrom(proto_config);
          route->mutable_typed_per_filter_config()->insert({"transform", per_route_config});
        });
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, TransformIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

const std::string default_filter_config = R"EOF(
name: transform
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.transform.v3.TransformConfig
  request_transformation:
    headers_mutations:
    - append:
        header:
          key: "model-header"
          value: "%REQUEST_BODY(model)%"
        append_action: OVERWRITE_IF_EXISTS_OR_ADD
    body_transformation:
      body_format:
        json_format:
          model: "new-model"
      action: MERGE
  response_transformation:
    headers_mutations:
    - append:
        header:
          key: "prompt-tokens"
          value: "%RESPONSE_BODY(usage:prompt_tokens)%"
        append_action: OVERWRITE_IF_EXISTS_OR_ADD
    - append:
        header:
          key: "completion-tokens"
          value: "%RESPONSE_BODY(usage:completion_tokens)%"
        append_action: OVERWRITE_IF_EXISTS_OR_ADD
)EOF";

const std::string no_transform_config = R"EOF(
name: transform
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.transform.v3.TransformConfig
)EOF";

const std::string per_route_override_config = R"EOF(
request_transformation:
  headers_mutations:
  - append:
      header:
        key: "model-header"
        value: "%REQUEST_BODY(model)%"
      append_action: OVERWRITE_IF_EXISTS_OR_ADD
)EOF";

TEST_P(TransformIntegrationTest, DefaultTransform) {
  setFilter(default_filter_config);
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  default_request_headers_.setPath("/default/route");
  default_request_headers_.setContentType("application/json");
  auto encoder_and_response = codec_client_->startRequest(default_request_headers_, false);

  Buffer::OwnedImpl empty_buffer;
  Buffer::OwnedImpl request_body(
      R"({"model": "gpt-3.5-turbo","messages": [{"role": "user","content": "Hello!"}]})");
  Buffer::OwnedImpl response_body(R"({"usage": {"prompt_tokens": 5, "completion_tokens": 7}})");

  encoder_and_response.first.encodeData(empty_buffer, false);
  encoder_and_response.first.encodeData(request_body, false);
  Http::TestRequestTrailerMapImpl request_trailers;
  encoder_and_response.first.encodeTrailers(request_trailers);

  waitForNextUpstreamRequest();

  EXPECT_EQ(upstream_request_->headers().get(Http::LowerCaseString("model-header"))[0]->value(),
            "gpt-3.5-turbo");
  EXPECT_TRUE(TestUtility::jsonStringEqual(
      upstream_request_->body().toString(),
      R"({"model":"new-model","messages":[{"role":"user","content":"Hello!"}]})"));

  default_response_headers_.setContentType("application/json");
  upstream_request_->encodeHeaders(default_response_headers_, false);
  upstream_request_->encodeData(response_body, false);
  Http::TestResponseTrailerMapImpl response_trailers;
  upstream_request_->encodeTrailers(response_trailers);

  ASSERT_TRUE(encoder_and_response.second->waitForEndStream());

  EXPECT_EQ(encoder_and_response.second->headers()
                .get(Http::LowerCaseString("prompt-tokens"))[0]
                ->value(),
            "5");
  EXPECT_EQ(encoder_and_response.second->headers()
                .get(Http::LowerCaseString("completion-tokens"))[0]
                ->value(),
            "7");

  cleanupUpstreamAndDownstream();
}

TEST_P(TransformIntegrationTest, OverrideTransformPerRoute) {
  setFilter(default_filter_config);
  setPerFilterConfig(per_route_override_config);

  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  default_request_headers_.setPath("/default/route");
  default_request_headers_.setContentType("application/json");
  auto encoder_and_response = codec_client_->startRequest(default_request_headers_, false);
  Buffer::OwnedImpl empty_buffer;
  Buffer::OwnedImpl request_body(
      R"({"model": "gpt-4","messages": [{"role": "user","content": "Hello!"}]})");
  Buffer::OwnedImpl response_body(R"({"usage": {"prompt_tokens": 10, "completion_tokens": 15}})");

  encoder_and_response.first.encodeData(empty_buffer, false);
  encoder_and_response.first.encodeData(request_body, false);
  Http::TestRequestTrailerMapImpl request_trailers;
  encoder_and_response.first.encodeTrailers(request_trailers);

  waitForNextUpstreamRequest();

  EXPECT_EQ(upstream_request_->headers().get(Http::LowerCaseString("model-header"))[0]->value(),
            "gpt-4");
  // No body transformation should happen since the configuration is overridden by route config.
  EXPECT_TRUE(TestUtility::jsonStringEqual(
      upstream_request_->body().toString(),
      R"({"model":"gpt-4","messages":[{"role":"user","content":"Hello!"}]})"));

  default_response_headers_.setContentType("application/json");
  upstream_request_->encodeHeaders(default_response_headers_, false);
  upstream_request_->encodeData(response_body, false);
  Http::TestResponseTrailerMapImpl response_trailers;
  upstream_request_->encodeTrailers(response_trailers);

  ASSERT_TRUE(encoder_and_response.second->waitForEndStream());

  // Since the configuration is overridden by route config, no response transformation should
  // happen.
  EXPECT_TRUE(
      encoder_and_response.second->headers().get(Http::LowerCaseString("prompt-tokens")).empty());
  EXPECT_TRUE(encoder_and_response.second->headers()
                  .get(Http::LowerCaseString("completion-tokens"))
                  .empty());

  cleanupUpstreamAndDownstream();
}

TEST_P(TransformIntegrationTest, NoTransformIsConfigured) {
  setFilter(no_transform_config);

  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  default_request_headers_.setPath("/default/route");
  default_request_headers_.setContentType("application/json");
  auto encoder_and_response = codec_client_->startRequest(default_request_headers_, false);
  Buffer::OwnedImpl empty_buffer;
  Buffer::OwnedImpl request_body(
      R"({"model": "gpt-4","messages": [{"role": "user","content": "Hello!"}]})");
  Buffer::OwnedImpl response_body(R"({"usage": {"prompt_tokens": 10, "completion_tokens": 15}})");

  encoder_and_response.first.encodeData(empty_buffer, false);
  encoder_and_response.first.encodeData(request_body, false);
  Http::TestRequestTrailerMapImpl request_trailers;
  encoder_and_response.first.encodeTrailers(request_trailers);

  waitForNextUpstreamRequest();

  // No transformation should happen since no configuration is configured.
  EXPECT_TRUE(upstream_request_->headers().get(Http::LowerCaseString("model-header")).empty());
  EXPECT_TRUE(TestUtility::jsonStringEqual(
      upstream_request_->body().toString(),
      R"({"model":"gpt-4","messages":[{"role":"user","content":"Hello!"}]})"));

  default_response_headers_.setContentType("application/json");
  upstream_request_->encodeHeaders(default_response_headers_, false);
  upstream_request_->encodeData(response_body, false);
  Http::TestResponseTrailerMapImpl response_trailers;
  upstream_request_->encodeTrailers(response_trailers);

  ASSERT_TRUE(encoder_and_response.second->waitForEndStream());

  EXPECT_TRUE(
      encoder_and_response.second->headers().get(Http::LowerCaseString("prompt-tokens")).empty());
  EXPECT_TRUE(encoder_and_response.second->headers()
                  .get(Http::LowerCaseString("completion-tokens"))
                  .empty());

  cleanupUpstreamAndDownstream();
}

TEST_P(TransformIntegrationTest, HeadersOnlyRequest) {
  setFilter(default_filter_config);

  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  default_request_headers_.setPath("/default/route");
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);

  waitForNextUpstreamRequest();

  upstream_request_->encodeHeaders(default_response_headers_, false);
  Http::TestResponseTrailerMapImpl response_trailers;
  upstream_request_->encodeTrailers(response_trailers);

  ASSERT_TRUE(response->waitForEndStream());

  cleanupUpstreamAndDownstream();
}

} // namespace
} // namespace Transform
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
