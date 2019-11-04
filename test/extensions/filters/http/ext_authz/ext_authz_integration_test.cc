#include "envoy/service/auth/v2/external_auth.pb.h"

#include "extensions/filters/http/well_known_names.h"

#include "test/integration/http_integration.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

using testing::AssertionResult;

namespace Envoy {
namespace {

class ExtAuthzIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                public HttpIntegrationTest {
public:
  ExtAuthzIntegrationTest() : HttpIntegrationTest(Http::CodecClient::Type::HTTP1, GetParam()) {}

  void createUpstreams() override {
    HttpIntegrationTest::createUpstreams();
    fake_upstreams_.emplace_back(
        new FakeUpstream(0, FakeHttpConnection::Type::HTTP2, version_, timeSystem()));
  }

  void initializeFilterAndDownstreamProtocol(const std::string& filter_config,
                                             Http::CodecClient::Type downstream_protocol) {
    config_helper_.addFilter(filter_config);

    config_helper_.addConfigModifier([](envoy::config::bootstrap::v2::Bootstrap& bootstrap) {
      auto* ext_authz_cluster = bootstrap.mutable_static_resources()->add_clusters();
      ext_authz_cluster->MergeFrom(bootstrap.static_resources().clusters()[0]);
      ext_authz_cluster->set_name("ext_authz_cluster");
      ext_authz_cluster->mutable_http2_protocol_options();
    });

    setDownstreamProtocol(downstream_protocol);
    HttpIntegrationTest::initialize();
  }

  void cleanup() {
    if (fake_ext_authz_connection_ != nullptr) {
      AssertionResult result = fake_ext_authz_connection_->close();
      RELEASE_ASSERT(result, result.message());
      result = fake_ext_authz_connection_->waitForDisconnect();
      RELEASE_ASSERT(result, result.message());
    }
    if (fake_upstream_connection_ != nullptr) {
      AssertionResult result = fake_upstream_connection_->close();
      RELEASE_ASSERT(result, result.message());
      result = fake_upstream_connection_->waitForDisconnect();
      RELEASE_ASSERT(result, result.message());
    }
  }

  ABSL_MUST_USE_RESULT
  AssertionResult waitForExtAuthzConnection() {
    return fake_upstreams_[1]->waitForHttpConnection(*dispatcher_, fake_ext_authz_connection_);
  }

  ABSL_MUST_USE_RESULT
  AssertionResult waitForExtAuthzStream() {
    return fake_ext_authz_connection_->waitForNewStream(*dispatcher_, ext_authz_request_);
  }

  ABSL_MUST_USE_RESULT
  AssertionResult waitForExtAuthzRequest(const std::string& expected_check_request_yaml) {
    envoy::service::auth::v2::CheckRequest check_request;
    VERIFY_ASSERTION(ext_authz_request_->waitForGrpcMessage(*dispatcher_, check_request));

    EXPECT_EQ("POST", ext_authz_request_->headers().Method()->value().getStringView());
    EXPECT_EQ("/envoy.service.auth.v2.Authorization/Check",
              ext_authz_request_->headers().Path()->value().getStringView());
    EXPECT_EQ("application/grpc",
              ext_authz_request_->headers().ContentType()->value().getStringView());

    envoy::service::auth::v2::CheckRequest expected_check_request;
    TestUtility::loadFromYaml(expected_check_request_yaml, expected_check_request);

    auto* attributes = check_request.mutable_attributes();
    auto* http_request = attributes->mutable_request()->mutable_http();

    // Clear fields which are not relevant.
    attributes->clear_source();
    attributes->clear_destination();
    attributes->clear_metadata_context();
    http_request->clear_id();
    http_request->clear_headers();

    EXPECT_EQ(check_request.DebugString(), expected_check_request.DebugString());

    return AssertionSuccess();
  }

  void expectCheckRequestWithBody(Http::CodecClient::Type downstream_protocol, uint64_t body_size) {
    TestUtility::feedBufferWithRandomCharacters(data_, body_size);
    const uint64_t max_request_bytes = 1024;
    const std::string filter_config = fmt::format(R"EOF(
name: envoy.ext_authz
config:
  grpc_service:
    envoy_grpc:
      cluster_name: ext_authz_cluster

  with_request_body:
    max_request_bytes: {}
    allow_partial_message: true
)EOF",
                                                  max_request_bytes);

    initializeFilterAndDownstreamProtocol(filter_config, downstream_protocol);
    codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));

    const std::string body_string = data_.toString();
    const uint64_t body_length = data_.length();
    const std::string expected_body_string =
        body_length > max_request_bytes ? body_string.substr(0, max_request_bytes) : body_string;

    BufferingStreamDecoderPtr response = IntegrationUtil::makeSingleRequest(
        lookupPort("http"), "POST", "/test", body_string, downstream_protocol_, version_);

    ASSERT_TRUE(waitForExtAuthzConnection());
    ASSERT_TRUE(waitForExtAuthzStream());
    ASSERT_TRUE(waitForExtAuthzRequest(
        fmt::format(R"EOF(
attributes:
  request:
    http:
      method: POST
      path: /test
      host: host
      size: "{}"
      body: "{}"
      {}
)EOF",
                    body_length, expected_body_string,
                    downstream_protocol == Http::CodecClient::Type::HTTP1 ?
                                                                          R"EOF(
      protocol: HTTP/1.1
)EOF"
                                                                          :
                                                                          R"EOF(
      scheme: http
      protocol: HTTP/2
)EOF")));

    EXPECT_TRUE(response->complete());
    cleanup();
  }

  FakeHttpConnectionPtr fake_ext_authz_connection_;
  FakeStreamPtr ext_authz_request_;
  Buffer::OwnedImpl data_;
};

INSTANTIATE_TEST_SUITE_P(IpVersions, ExtAuthzIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

// Verifies that the request body is included in the CheckRequest when the downstream protocol is
// HTTP/1.1.
TEST_P(ExtAuthzIntegrationTest, HTTP1DownstreamRequestWithBody) {
  expectCheckRequestWithBody(Http::CodecClient::Type::HTTP1, 4);
}

// Verifies that the request body is included in the CheckRequest when the downstream protocol is
// HTTP/1.1 and the size of the request body is larger than max_request_bytes.
TEST_P(ExtAuthzIntegrationTest, HTTP1DownstreamRequestWithLargeBody) {
  expectCheckRequestWithBody(Http::CodecClient::Type::HTTP1, 2048);
}

// Verifies that the request body is included in the CheckRequest when the downstream protocol is
// HTTP/2.
TEST_P(ExtAuthzIntegrationTest, HTTP2DownstreamRequestWithBody) {
  expectCheckRequestWithBody(Http::CodecClient::Type::HTTP2, 4);
}

// Verifies that the request body is included in the CheckRequest when the downstream protocol is
// HTTP/2 and the size of the request body is larger than max_request_bytes.
TEST_P(ExtAuthzIntegrationTest, HTTP2DownstreamRequestWithLargeBody) {
  expectCheckRequestWithBody(Http::CodecClient::Type::HTTP2, 2048);
}

} // namespace
} // namespace Envoy
