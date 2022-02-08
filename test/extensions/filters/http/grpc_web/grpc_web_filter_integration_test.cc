#include <memory>

#include "source/common/common/base64.h"

#include "test/integration/http_integration.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace {

constexpr absl::string_view text{"application/grpc-web-text"};
constexpr absl::string_view binary{"application/grpc-web"};
constexpr uint64_t MAX_BUFFERED_PLAINTEXT_LENGTH = 16384;

using ContentType = std::string;
using Accept = std::string;
using TestParams = std::tuple<Network::Address::IpVersion, Http::CodecType, ContentType, Accept>;

class GrpcWebFilterIntegrationTest : public testing::TestWithParam<TestParams>,
                                     public HttpIntegrationTest {
public:
  GrpcWebFilterIntegrationTest()
      : HttpIntegrationTest(std::get<1>(GetParam()), std::get<0>(GetParam())) {}

  void SetUp() override {
    setUpstreamProtocol(Http::CodecType::HTTP2);
    config_helper_.prependFilter("name: envoy.filters.http.grpc_web");
  }

  void initialize() override {
    if (downstream_protocol_ == Http::CodecType::HTTP1) {
      config_helper_.addConfigModifier(setEnableDownstreamTrailersHttp1());
    }
    setUpstreamProtocol(Http::CodecType::HTTP2);

    HttpIntegrationTest::initialize();
  }

  void setLocalReplyConfig(const std::string& yaml) {
    envoy::extensions::filters::network::http_connection_manager::v3::LocalReplyConfig
        local_reply_config;
    TestUtility::loadFromYaml(yaml, local_reply_config);
    config_helper_.setLocalReply(local_reply_config);
  }

  void testUpstreamDisconnect(const std::string& expected_grpc_message_value,
                              const std::string& local_reply_config_yaml = EMPTY_STRING) {
    if (!local_reply_config_yaml.empty()) {
      setLocalReplyConfig(local_reply_config_yaml);
    }

    initialize();

    Http::TestRequestTrailerMapImpl request_trailers{{"request1", "trailer1"},
                                                     {"request2", "trailer2"}};

    codec_client_ = makeHttpConnection(lookupPort("http"));
    auto encoder_decoder =
        codec_client_->startRequest(Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                                                   {":path", "/test/long/url"},
                                                                   {":scheme", "http"},
                                                                   {"content-type", content_type_},
                                                                   {"accept", accept_},
                                                                   {":authority", "host"}});
    request_encoder_ = &encoder_decoder.first;
    auto response = std::move(encoder_decoder.second);
    codec_client_->sendData(*request_encoder_, 1, false);
    codec_client_->sendTrailers(*request_encoder_, request_trailers);
    waitForNextUpstreamRequest();

    ASSERT_TRUE(fake_upstream_connection_->close());
    ASSERT_TRUE(response->waitForEndStream());
    EXPECT_TRUE(response->complete());

    EXPECT_EQ("503", response->headers().getStatusValue());
    EXPECT_EQ(absl::StrCat(accept_, "+proto"), response->headers().getContentTypeValue());
    EXPECT_EQ(expected_grpc_message_value, response->headers().getGrpcMessageValue());
    EXPECT_EQ(0U, response->body().length());

    codec_client_->close();
  }

  void testBadUpstreamResponse(const std::string& start, const std::string& end,
                               const std::string& expected, bool remove_content_type = false) {
    initialize();
    Http::TestRequestTrailerMapImpl request_trailers{{"request1", "trailer1"},
                                                     {"request2", "trailer2"}};

    codec_client_ = makeHttpConnection(lookupPort("http"));
    auto encoder_decoder =
        codec_client_->startRequest(Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                                                   {":path", "/test/long/url"},
                                                                   {":scheme", "http"},
                                                                   {"content-type", content_type_},
                                                                   {"accept", accept_},
                                                                   {":authority", "host"}});
    request_encoder_ = &encoder_decoder.first;
    auto response = std::move(encoder_decoder.second);
    codec_client_->sendData(*request_encoder_, 1, false);
    codec_client_->sendTrailers(*request_encoder_, request_trailers);
    waitForNextUpstreamRequest();

    // Sending back non gRPC-Web response.
    if (remove_content_type) {
      default_response_headers_.removeContentType();
    } else {
      default_response_headers_.setReferenceContentType(
          Http::Headers::get().ContentTypeValues.Json);
    }
    upstream_request_->encodeHeaders(default_response_headers_, /*end_stream=*/false);
    upstream_request_->encodeData(start, /*end_stream=*/false);
    upstream_request_->encodeData(end, /*end_stream=*/true);
    ASSERT_TRUE(response->waitForEndStream());

    EXPECT_TRUE(response->complete());
    EXPECT_EQ(expected, response->headers().getGrpcMessageValue());
    EXPECT_EQ("200", response->headers().getStatusValue());
    EXPECT_EQ(absl::StrCat(accept_, "+proto"), response->headers().getContentTypeValue());
    EXPECT_EQ(0U, response->body().length());
    codec_client_->close();
  }

  static std::string testParamsToString(const testing::TestParamInfo<TestParams> params) {
    return fmt::format(
        "{}_{}_{}_{}",
        TestUtility::ipTestParamsToString(testing::TestParamInfo<Network::Address::IpVersion>(
            std::get<0>(params.param), params.index)),
        std::get<1>(params.param) == Http::CodecType::HTTP2 ? "Http2" : "Http",
        std::get<2>(params.param) == text ? "SendText" : "SendBinary",
        std::get<3>(params.param) == text ? "AcceptText" : "AcceptBinary");
  }

  const Envoy::Http::CodecType downstream_protocol_{std::get<1>(GetParam())};
  const ContentType content_type_{std::get<2>(GetParam())};
  const Accept accept_{std::get<3>(GetParam())};
};

INSTANTIATE_TEST_SUITE_P(
    Params, GrpcWebFilterIntegrationTest,
    testing::Combine(testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                     testing::Values(Http::CodecType::HTTP1, Http::CodecType::HTTP2),
                     testing::Values(ContentType{text}, ContentType{binary}),
                     testing::Values(Accept{text}, Accept{binary})),
    GrpcWebFilterIntegrationTest::testParamsToString);

TEST_P(GrpcWebFilterIntegrationTest, GrpcWebTrailersNotDuplicated) {
  initialize();

  Http::TestRequestTrailerMapImpl request_trailers{{"request1", "trailer1"},
                                                   {"request2", "trailer2"}};
  Http::TestResponseTrailerMapImpl response_trailers{{"response1", "trailer1"},
                                                     {"response2", "trailer2"}};

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto encoder_decoder =
      codec_client_->startRequest(Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                                                 {":path", "/test/long/url"},
                                                                 {":scheme", "http"},
                                                                 {"content-type", content_type_},
                                                                 {"accept", accept_},
                                                                 {":authority", "host"}});
  request_encoder_ = &encoder_decoder.first;
  auto response = std::move(encoder_decoder.second);

  const std::string body = "hello";
  const std::string encoded_body =
      content_type_ == text ? Base64::encode(body.data(), body.length()) : body;
  codec_client_->sendData(*request_encoder_, encoded_body, false);
  codec_client_->sendTrailers(*request_encoder_, request_trailers);
  waitForNextUpstreamRequest();
  default_response_headers_.setReferenceContentType(Http::Headers::get().ContentTypeValues.Grpc);
  upstream_request_->encodeHeaders(default_response_headers_, false);
  upstream_request_->encodeData(1, false);
  upstream_request_->encodeTrailers(response_trailers);
  ASSERT_TRUE(response->waitForEndStream());

  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_EQ(body.length(), upstream_request_->bodyLength());
  EXPECT_THAT(*upstream_request_->trailers(), HeaderMapEqualRef(&request_trailers));

  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
  const auto response_body = accept_ == text ? Base64::decode(response->body()) : response->body();
  EXPECT_TRUE(absl::StrContains(response_body, "response1:trailer1"));
  EXPECT_TRUE(absl::StrContains(response_body, "response2:trailer2"));

  if (downstream_protocol_ == Http::CodecType::HTTP1) {
    // When the downstream protocol is HTTP/1.1 we expect the trailers to be in the response-body.
    EXPECT_EQ(nullptr, response->trailers());
  }

  if (downstream_protocol_ == Http::CodecType::HTTP2) {
    // When the downstream protocol is HTTP/2 expect that the trailers are included in the
    // response-body.
    EXPECT_EQ(nullptr, response->trailers());
  }
}

TEST_P(GrpcWebFilterIntegrationTest, UpstreamDisconnect) {
  testUpstreamDisconnect("upstream connect error or disconnect/reset before headers. reset reason: "
                         "connection termination");
}

TEST_P(GrpcWebFilterIntegrationTest, UpstreamDisconnectWithLargeBody) {
  // The local reply is configured to send a large (its length is greater than
  // MAX_BUFFERED_PLAINTEXT_LENGTH) body.
  const std::string local_reply_body = std::string(2 * MAX_BUFFERED_PLAINTEXT_LENGTH, 'a');
  const std::string local_reply_config_yaml = fmt::format(R"EOF(
body_format:
  text_format_source:
    inline_string: "{}"
)EOF",
                                                          local_reply_body);
  testUpstreamDisconnect(local_reply_body, local_reply_config_yaml);
}

TEST_P(GrpcWebFilterIntegrationTest, BadUpstreamResponse) {
  testBadUpstreamResponse("{", "\"percentage\": \"100%\"}",
                          /*expected=*/"{\"percentage\": \"100%25\"}");
}

TEST_P(GrpcWebFilterIntegrationTest, BadUpstreamResponseWithoutContentType) {
  testBadUpstreamResponse("{", "\"percentage\": \"100%\"}",
                          /*expected=*/"{\"percentage\": \"100%25\"}",
                          /*remove_content_type=*/true);
}

TEST_P(GrpcWebFilterIntegrationTest, BadUpstreamResponseLargeEnd) {
  // When we have buffered data in encoding buffer, we limit the length to
  // MAX_BUFFERED_PLAINTEXT_LENGTH.
  const std::string start(MAX_BUFFERED_PLAINTEXT_LENGTH, 'a');
  const std::string end(MAX_BUFFERED_PLAINTEXT_LENGTH, 'b');
  testBadUpstreamResponse(start, end, /*expected=*/start);
}

TEST_P(GrpcWebFilterIntegrationTest, BadUpstreamResponseLargeFirst) {
  const std::string start(2 * MAX_BUFFERED_PLAINTEXT_LENGTH, 'a');
  const std::string end(MAX_BUFFERED_PLAINTEXT_LENGTH, 'b');
  testBadUpstreamResponse(start, end, /*expected=*/start.substr(0, MAX_BUFFERED_PLAINTEXT_LENGTH));
}

} // namespace
} // namespace Envoy
