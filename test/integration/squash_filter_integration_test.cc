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

  /**
   * Initializer for an individual integration test.
   */
  void SetUp() override {
    fake_upstreams_.emplace_back(
        new AutonomousUpstream(0, FakeHttpConnection::Type::HTTP1, version_));
    registerPort("upstream", fake_upstreams_[0]->localAddress()->ip()->port());
    fake_upstreams_.emplace_back(new FakeUpstream(0, FakeHttpConnection::Type::HTTP1, version_));
    registerPort("upstream_squash", fake_upstreams_[1]->localAddress()->ip()->port());
    fake_upstreams_.back()->set_allow_unexpected_disconnects(true);

    ::setenv("SQUASH_ENV_TEST", ENV_VAR_VALUE, 1);

    createTestServer("test/config/integration/squash_filter.yaml", {"http"});

    codec_client_ = makeHttpConnection(lookupPort("http"));
  }

  /**
   * Destructor for an individual integration test.
   */
  void TearDown() override {
    test_server_.reset();
    fake_upstreams_.clear();
  }

  IntegrationStreamDecoderPtr sendDebugRequest(IntegrationCodecClientPtr& codec_client) {
    IntegrationStreamDecoderPtr response(new IntegrationStreamDecoder(*dispatcher_));
    Http::TestHeaderMapImpl headers{{":method", "GET"},
                                    {":authority", "www.solo.io"},
                                    {"x-squash-debug", "true"},
                                    {":path", "/getsomething"}};
    codec_client->makeHeaderOnlyRequest(headers, *response);
    return response;
  }

  FakeStreamPtr sendSquash(FakeHttpConnectionPtr& fake_squash_connection, std::string status,
                           std::string body) {

    FakeStreamPtr request_stream = fake_squash_connection->waitForNewStream(*dispatcher_);
    request_stream->waitForEndStream(*dispatcher_);
    if (body.empty()) {
      request_stream->encodeHeaders(Http::TestHeaderMapImpl{{":status", status}}, true);
    } else {
      request_stream->encodeHeaders(Http::TestHeaderMapImpl{{":status", status}}, false);
      Buffer::OwnedImpl creatrespbuffer(body);
      request_stream->encodeData(creatrespbuffer, true);
    }
    return request_stream;
  }

  FakeStreamPtr sendSquashCreate(FakeHttpConnectionPtr& fake_squash_connection, std::string body) {
    return sendSquash(fake_squash_connection, "201", body);
  }

  FakeStreamPtr sendSquashOk(FakeHttpConnectionPtr& fake_squash_connection, std::string body) {
    return sendSquash(fake_squash_connection, "200", body);
  }

  IntegrationCodecClientPtr codec_client_;
};

INSTANTIATE_TEST_CASE_P(IpVersions, SquashFilterIntegrationTest,
                        testing::ValuesIn(TestEnvironment::getIpVersionsForTest()));

TEST_P(SquashFilterIntegrationTest, TestHappyPath) {

  IntegrationStreamDecoderPtr response = sendDebugRequest(codec_client_);

  FakeHttpConnectionPtr fake_squash_connection =
      fake_upstreams_[1]->waitForHttpConnection(*dispatcher_);

  // respond to create request
  FakeStreamPtr create_stream =
      sendSquashCreate(fake_squash_connection, "{\"metadata\":{\"name\":\"oF8iVdiJs5\"},"
                                               "\"spec\":{\"attachment\":{\"a\":\"b\"},"
                                               "\"image\":\"debug\",\"node\":\"debug-node\"},"
                                               "\"status\":{\"state\":\"none\"}}");

  // respond to read attachment request
  FakeStreamPtr get_stream = sendSquashOk(
      fake_squash_connection, "{\"metadata\":{\"name\":\"oF8iVdiJs5\"},\"spec\":{"
                              "\"attachment\":{\"a\":\"b\"},\"image\":\"debug\",\"node\":"
                              "\"debug-node\"},\"status\":{\"state\":\"attached\"}}");

  response->waitForEndStream();

  EXPECT_STREQ("POST", create_stream->headers().Method()->value().c_str());
  // make sure the env var was replaced
  ProtobufWkt::Struct actualbody;
  MessageUtil::loadFromJson(TestUtility::bufferToString(create_stream->body()), actualbody);

  ProtobufWkt::Struct expectedbody;
  MessageUtil::loadFromJson("{\"spec\": { \"attachment\" : { \"env\": \"" ENV_VAR_VALUE
                            "\" } , \"match_request\":true} }",
                            expectedbody);

  EXPECT_TRUE(MessageDifferencer::Equals(expectedbody, actualbody));

  // The second request should be fore the created object
  EXPECT_STREQ("GET", get_stream->headers().Method()->value().c_str());
  EXPECT_STREQ("200", response->headers().Status()->value().c_str());

  codec_client_->close();
  fake_squash_connection->close();
  fake_squash_connection->waitForDisconnect();
}

TEST_P(SquashFilterIntegrationTest, ErrorAttaching) {

  IntegrationStreamDecoderPtr response = sendDebugRequest(codec_client_);
  FakeHttpConnectionPtr fake_squash_connection =
      fake_upstreams_[1]->waitForHttpConnection(*dispatcher_);

  // respond to create request
  FakeStreamPtr create_stream =
      sendSquashCreate(fake_squash_connection, "{\"metadata\":{\"name\":\"oF8iVdiJs5\"},"
                                               "\"spec\":{\"attachment\":{\"a\":\"b\"},"
                                               "\"image\":\"debug\",\"node\":\"debug-node\"},"
                                               "\"status\":{\"state\":\"none\"}}");
  // respond to read attachment request with error!
  FakeStreamPtr get_stream = sendSquashOk(
      fake_squash_connection, "{\"metadata\":{\"name\":\"oF8iVdiJs5\"},\"spec\":{"
                              "\"attachment\":{\"a\":\"b\"},\"image\":\"debug\",\"node\":"
                              "\"debug-node\"},\"status\":{\"state\":\"error\"}}");

  response->waitForEndStream();

  EXPECT_STREQ("200", response->headers().Status()->value().c_str());

  codec_client_->close();
  fake_squash_connection->close();
  fake_squash_connection->waitForDisconnect();
}

TEST_P(SquashFilterIntegrationTest, TimeoutAttaching) {

  IntegrationStreamDecoderPtr response = sendDebugRequest(codec_client_);
  FakeHttpConnectionPtr fake_squash_connection =
      fake_upstreams_[1]->waitForHttpConnection(*dispatcher_);

  // respond to create request
  FakeStreamPtr create_stream =
      sendSquashCreate(fake_squash_connection, "{\"metadata\":{\"name\":\"oF8iVdiJs5\"},"
                                               "\"spec\":{\"attachment\":{\"a\":\"b\"},"
                                               "\"image\":\"debug\",\"node\":\"debug-node\"},"
                                               "\"status\":{\"state\":\"none\"}}");
  // respond to read attachment. since attachment_timeout is smaller than the squash
  // attachment_poll_every  config, just one response is enough
  FakeStreamPtr get_stream = sendSquashOk(
      fake_squash_connection, "{\"metadata\":{\"name\":\"oF8iVdiJs5\"},\"spec\":{"
                              "\"attachment\":{\"a\":\"b\"},\"image\":\"debug\",\"node\":"
                              "\"debug-node\"},\"status\":{\"state\":\"attaching\"}}");

  response->waitForEndStream();

  EXPECT_STREQ("200", response->headers().Status()->value().c_str());

  codec_client_->close();
  fake_squash_connection->close();
  fake_squash_connection->waitForDisconnect();
}

TEST_P(SquashFilterIntegrationTest, ErrorNoSquashServer) {

  IntegrationStreamDecoderPtr response = sendDebugRequest(codec_client_);

  // Don't respond to anything. squash filter should timeout within
  // squash_request_timeout and continue the request.
  response->waitForEndStream();

  EXPECT_STREQ("200", response->headers().Status()->value().c_str());

  codec_client_->close();
}

TEST_P(SquashFilterIntegrationTest, BadCreateResponse) {

  IntegrationStreamDecoderPtr response = sendDebugRequest(codec_client_);
  FakeHttpConnectionPtr fake_squash_connection =
      fake_upstreams_[1]->waitForHttpConnection(*dispatcher_);

  // respond to create request
  FakeStreamPtr create_stream = sendSquashCreate(fake_squash_connection, "not json...");

  response->waitForEndStream();

  EXPECT_STREQ("200", response->headers().Status()->value().c_str());

  codec_client_->close();
  fake_squash_connection->close();
  fake_squash_connection->waitForDisconnect();
}

TEST_P(SquashFilterIntegrationTest, BadGetResponse) {

  IntegrationStreamDecoderPtr response = sendDebugRequest(codec_client_);
  FakeHttpConnectionPtr fake_squash_connection =
      fake_upstreams_[1]->waitForHttpConnection(*dispatcher_);

  // respond to create request
  FakeStreamPtr create_stream =
      sendSquashCreate(fake_squash_connection, "{\"metadata\":{\"name\":\"oF8iVdiJs5\"},"
                                               "\"spec\":{\"attachment\":{\"a\":\"b\"},"
                                               "\"image\":\"debug\",\"node\":\"debug-node\"},"
                                               "\"status\":{\"state\":\"none\"}}");
  // respond to read attachment request with error!
  FakeStreamPtr get_stream = sendSquashOk(fake_squash_connection, "not json...");

  response->waitForEndStream();

  EXPECT_STREQ("200", response->headers().Status()->value().c_str());

  codec_client_->close();
  fake_squash_connection->close();
  fake_squash_connection->waitForDisconnect();
}

} // namespace Envoy
