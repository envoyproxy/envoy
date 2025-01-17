#include <cstdlib>

#include "envoy/config/bootstrap/v3/bootstrap.pb.h"

#include "source/common/protobuf/protobuf.h"

#include "test/integration/autonomous_upstream.h"
#include "test/integration/http_integration.h"
#include "test/integration/integration.h"
#include "test/integration/utility.h"
#include "test/test_common/environment.h"

#define ENV_VAR_VALUE "somerandomevalue"

using Envoy::Protobuf::util::MessageDifferencer;

namespace Envoy {

class SquashFilterIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                    public HttpIntegrationTest {
public:
  SquashFilterIntegrationTest() : HttpIntegrationTest(Http::CodecType::HTTP1, GetParam()) {}

  ~SquashFilterIntegrationTest() override {
    if (fake_squash_connection_) {
      AssertionResult result = fake_squash_connection_->close();
      RELEASE_ASSERT(result, result.message());
      result = fake_squash_connection_->waitForDisconnect();
      RELEASE_ASSERT(result, result.message());
    }
  }

  FakeStreamPtr sendSquash(const std::string& status, const std::string& body) {

    if (!fake_squash_connection_) {
      AssertionResult result =
          fake_upstreams_[1]->waitForHttpConnection(*dispatcher_, fake_squash_connection_);
      RELEASE_ASSERT(result, result.message());
    }

    FakeStreamPtr request_stream;
    AssertionResult result =
        fake_squash_connection_->waitForNewStream(*dispatcher_, request_stream);
    RELEASE_ASSERT(result, result.message());
    result = request_stream->waitForEndStream(*dispatcher_);
    RELEASE_ASSERT(result, result.message());
    if (body.empty()) {
      request_stream->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", status}}, true);
    } else {
      request_stream->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", status}}, false);
      Buffer::OwnedImpl responseBuffer(body);
      request_stream->encodeData(responseBuffer, true);
    }

    return request_stream;
  }

  FakeStreamPtr sendSquashCreate(const std::string& body = SQUASH_CREATE_DEFAULT) {
    return sendSquash("201", body);
  }

  FakeStreamPtr sendSquashOk(const std::string& body) { return sendSquash("200", body); }

  IntegrationStreamDecoderPtr sendDebugRequest(IntegrationCodecClientPtr& codec_client) {
    Http::TestRequestHeaderMapImpl headers{{":method", "GET"},
                                           {":authority", "www.solo.io"},
                                           {"x-squash-debug", "true"},
                                           {":path", "/getsomething"}};
    return codec_client->makeHeaderOnlyRequest(headers);
  }

  void createUpstreams() override {
    HttpIntegrationTest::createUpstreams();
    addFakeUpstream(Http::CodecType::HTTP2);
  }

  /**
   * Initializer for an individual integration test.
   */
  void initialize() override {
    TestEnvironment::setEnvVar("SQUASH_ENV_TEST", ENV_VAR_VALUE, 1);

    autonomous_upstream_ = true;

    config_helper_.prependFilter(ConfigHelper::defaultSquashFilter());

    config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* squash_cluster = bootstrap.mutable_static_resources()->add_clusters();
      squash_cluster->MergeFrom(bootstrap.static_resources().clusters()[0]);
      squash_cluster->set_name("squash");
      ConfigHelper::setHttp2(*squash_cluster);
    });

    HttpIntegrationTest::initialize();
    codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));
  }

  /**
   * Initialize before every test.
   */
  void SetUp() override { initialize(); }

  FakeHttpConnectionPtr fake_squash_connection_;
  static const std::string SQUASH_CREATE_DEFAULT;
  static std::string squashGetAttachmentBodyWithState(const std::string& state) {
    return "{\"metadata\":{\"name\":\"oF8iVdiJs5\"},\"spec\":{"
           "\"attachment\":{\"a\":\"b\"},\"image\":\"debug\",\"node\":"
           "\"debug-node\"},\"status\":{\"state\":\"" +
           state + "\"}}";
  }
};

const std::string SquashFilterIntegrationTest::SQUASH_CREATE_DEFAULT =
    "{\"metadata\":{\"name\":\"oF8iVdiJs5\"},"
    "\"spec\":{\"attachment\":{\"a\":\"b\"},"
    "\"image\":\"debug\",\"node\":\"debug-node\"},"
    "\"status\":{\"state\":\"none\"}}";

INSTANTIATE_TEST_SUITE_P(IpVersions, SquashFilterIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(SquashFilterIntegrationTest, TestHappyPath) {
  auto response = sendDebugRequest(codec_client_);

  // Respond to create request
  FakeStreamPtr create_stream = sendSquashCreate();

  // Respond to read attachment request
  FakeStreamPtr get_stream = sendSquashOk(squashGetAttachmentBodyWithState("attached"));

  ASSERT_TRUE(response->waitForEndStream());

  EXPECT_EQ("POST", create_stream->headers().getMethodValue());
  EXPECT_EQ("/api/v2/debugattachment/", create_stream->headers().getPathValue());
  // Make sure the env var was replaced
  ProtobufWkt::Struct actualbody;
  TestUtility::loadFromJson(create_stream->body().toString(), actualbody);

  ProtobufWkt::Struct expectedbody;
  TestUtility::loadFromJson("{\"spec\": { \"attachment\" : { \"env\": \"" ENV_VAR_VALUE
                            "\" } , \"match_request\":true} }",
                            expectedbody);

  EXPECT_TRUE(MessageDifferencer::Equals(expectedbody, actualbody));
  // The second request should be for the created object
  EXPECT_EQ("GET", get_stream->headers().getMethodValue());
  EXPECT_EQ("/api/v2/debugattachment/oF8iVdiJs5", get_stream->headers().getPathValue());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
}

TEST_P(SquashFilterIntegrationTest, ErrorAttaching) {
  auto response = sendDebugRequest(codec_client_);

  // Respond to create request
  FakeStreamPtr create_stream = sendSquashCreate();
  // Respond to read attachment request with error!
  FakeStreamPtr get_stream = sendSquashOk(squashGetAttachmentBodyWithState("error"));

  ASSERT_TRUE(response->waitForEndStream());

  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
}

TEST_P(SquashFilterIntegrationTest, TimeoutAttaching) {
  auto response = sendDebugRequest(codec_client_);

  // Respond to create request
  FakeStreamPtr create_stream = sendSquashCreate();
  // Respond to read attachment. since attachment_timeout is smaller than attachment_poll_period
  // config, just one response is enough, as the filter will timeout (and continue the iteration)
  // before issuing another get attachment request.
  FakeStreamPtr get_stream = sendSquashOk(squashGetAttachmentBodyWithState("attaching"));

  ASSERT_TRUE(response->waitForEndStream());

  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
}

TEST_P(SquashFilterIntegrationTest, ErrorNoSquashServer) {
  auto response = sendDebugRequest(codec_client_);

  // Don't respond to anything. squash filter should timeout within
  // squash_request_timeout and continue the request.
  ASSERT_TRUE(response->waitForEndStream());

  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
}

TEST_P(SquashFilterIntegrationTest, BadCreateResponse) {
  auto response = sendDebugRequest(codec_client_);

  // Respond to create request
  FakeStreamPtr create_stream = sendSquashCreate("not json...");

  ASSERT_TRUE(response->waitForEndStream());

  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
}

TEST_P(SquashFilterIntegrationTest, BadGetResponse) {
  auto response = sendDebugRequest(codec_client_);

  // Respond to create request
  FakeStreamPtr create_stream = sendSquashCreate();
  // Respond to read attachment request with error!
  FakeStreamPtr get_stream = sendSquashOk("not json...");

  ASSERT_TRUE(response->waitForEndStream());

  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
}

} // namespace Envoy
