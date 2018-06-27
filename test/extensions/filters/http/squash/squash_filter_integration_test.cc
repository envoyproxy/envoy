#include <stdlib.h>

#include <cstdlib>

#include "common/protobuf/protobuf.h"

#include "test/integration/autonomous_upstream.h"
#include "test/integration/http_integration.h"
#include "test/integration/integration.h"
#include "test/integration/utility.h"

#define ENV_VAR_VALUE "somerandomevalue"

using Envoy::Protobuf::util::MessageDifferencer;

namespace Envoy {

class SquashFilterIntegrationTest : public HttpIntegrationTest,
                                    public testing::TestWithParam<Network::Address::IpVersion> {
public:
  SquashFilterIntegrationTest() : HttpIntegrationTest(Http::CodecClient::Type::HTTP1, GetParam()) {}

  ~SquashFilterIntegrationTest() {
    if (fake_squash_connection_) {
      fake_squash_connection_->close();
      fake_squash_connection_->waitForDisconnect();
    }
  }

  FakeStreamPtr sendSquash(const std::string& status, const std::string& body) {

    if (!fake_squash_connection_) {
      fake_squash_connection_ = fake_upstreams_[1]->waitForHttpConnection(*dispatcher_);
    }

    FakeStreamPtr request_stream = fake_squash_connection_->waitForNewStream(*dispatcher_);
    request_stream->waitForEndStream(*dispatcher_);
    if (body.empty()) {
      request_stream->encodeHeaders(Http::TestHeaderMapImpl{{":status", status}}, true);
    } else {
      request_stream->encodeHeaders(Http::TestHeaderMapImpl{{":status", status}}, false);
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
    Http::TestHeaderMapImpl headers{{":method", "GET"},
                                    {":authority", "www.solo.io"},
                                    {"x-squash-debug", "true"},
                                    {":path", "/getsomething"}};
    return codec_client->makeHeaderOnlyRequest(headers);
  }

  void createUpstreams() override {
    HttpIntegrationTest::createUpstreams();
    fake_upstreams_.emplace_back(new FakeUpstream(0, FakeHttpConnection::Type::HTTP2, version_));
    fake_upstreams_.back()->set_allow_unexpected_disconnects(true);
  }

  /**
   * Initializer for an individual integration test.
   */
  void initialize() override {
    ::setenv("SQUASH_ENV_TEST", ENV_VAR_VALUE, 1);

    autonomous_upstream_ = true;

    config_helper_.addFilter(ConfigHelper::DEFAULT_SQUASH_FILTER);

    config_helper_.addConfigModifier([](envoy::config::bootstrap::v2::Bootstrap& bootstrap) {
      auto* squash_cluster = bootstrap.mutable_static_resources()->add_clusters();
      squash_cluster->MergeFrom(bootstrap.static_resources().clusters()[0]);
      squash_cluster->set_name("squash");
      squash_cluster->mutable_http2_protocol_options();
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

INSTANTIATE_TEST_CASE_P(IpVersions, SquashFilterIntegrationTest,
                        testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                        TestUtility::ipTestParamsToString);

TEST_P(SquashFilterIntegrationTest, TestHappyPath) {
  auto response = sendDebugRequest(codec_client_);

  // Respond to create request
  FakeStreamPtr create_stream = sendSquashCreate();

  // Respond to read attachment request
  FakeStreamPtr get_stream = sendSquashOk(squashGetAttachmentBodyWithState("attached"));

  response->waitForEndStream();

  EXPECT_STREQ("POST", create_stream->headers().Method()->value().c_str());
  EXPECT_STREQ("/api/v2/debugattachment/", create_stream->headers().Path()->value().c_str());
  // Make sure the env var was replaced
  ProtobufWkt::Struct actualbody;
  MessageUtil::loadFromJson(create_stream->body().toString(), actualbody);

  ProtobufWkt::Struct expectedbody;
  MessageUtil::loadFromJson("{\"spec\": { \"attachment\" : { \"env\": \"" ENV_VAR_VALUE
                            "\" } , \"match_request\":true} }",
                            expectedbody);

  EXPECT_TRUE(MessageDifferencer::Equals(expectedbody, actualbody));
  // The second request should be for the created object
  EXPECT_STREQ("GET", get_stream->headers().Method()->value().c_str());
  EXPECT_STREQ("/api/v2/debugattachment/oF8iVdiJs5", get_stream->headers().Path()->value().c_str());
  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("200", response->headers().Status()->value().c_str());
}

TEST_P(SquashFilterIntegrationTest, ErrorAttaching) {
  auto response = sendDebugRequest(codec_client_);

  // Respond to create request
  FakeStreamPtr create_stream = sendSquashCreate();
  // Respond to read attachment request with error!
  FakeStreamPtr get_stream = sendSquashOk(squashGetAttachmentBodyWithState("error"));

  response->waitForEndStream();

  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("200", response->headers().Status()->value().c_str());
}

TEST_P(SquashFilterIntegrationTest, TimeoutAttaching) {
  auto response = sendDebugRequest(codec_client_);

  // Respond to create request
  FakeStreamPtr create_stream = sendSquashCreate();
  // Respond to read attachment. since attachment_timeout is smaller than attachment_poll_period
  // config, just one response is enough, as the filter will timeout (and continue the iteration)
  // before issuing another get attachment request.
  FakeStreamPtr get_stream = sendSquashOk(squashGetAttachmentBodyWithState("attaching"));

  response->waitForEndStream();

  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("200", response->headers().Status()->value().c_str());
}

TEST_P(SquashFilterIntegrationTest, ErrorNoSquashServer) {
  auto response = sendDebugRequest(codec_client_);

  // Don't respond to anything. squash filter should timeout within
  // squash_request_timeout and continue the request.
  response->waitForEndStream();

  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("200", response->headers().Status()->value().c_str());
}

TEST_P(SquashFilterIntegrationTest, BadCreateResponse) {
  auto response = sendDebugRequest(codec_client_);

  // Respond to create request
  FakeStreamPtr create_stream = sendSquashCreate("not json...");

  response->waitForEndStream();

  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("200", response->headers().Status()->value().c_str());
}

TEST_P(SquashFilterIntegrationTest, BadGetResponse) {
  auto response = sendDebugRequest(codec_client_);

  // Respond to create request
  FakeStreamPtr create_stream = sendSquashCreate();
  // Respond to read attachment request with error!
  FakeStreamPtr get_stream = sendSquashOk("not json...");

  response->waitForEndStream();

  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("200", response->headers().Status()->value().c_str());
}

} // namespace Envoy
