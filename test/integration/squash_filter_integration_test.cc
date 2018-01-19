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

class SquashFilterIntegrationTestBase : public HttpIntegrationTest,
                                        public testing::TestWithParam<Network::Address::IpVersion> {
public:
  using HttpIntegrationTest::HttpIntegrationTest;

  FakeStreamPtr sendSquash(FakeHttpConnectionPtr& fake_squash_connection, std::string status,
                           std::string body) {

    FakeStreamPtr request_stream = fake_squash_connection->waitForNewStream(*dispatcher_);
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

  FakeStreamPtr sendSquashCreate(FakeHttpConnectionPtr& fake_squash_connection, std::string body) {
    return sendSquash(fake_squash_connection, "201", body);
  }

  FakeStreamPtr sendSquashOk(FakeHttpConnectionPtr& fake_squash_connection, std::string body) {
    return sendSquash(fake_squash_connection, "200", body);
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
};

class SquashFilterIntegrationTestV1 : public SquashFilterIntegrationTestBase {
public:
  SquashFilterIntegrationTestV1()
      : SquashFilterIntegrationTestBase(Http::CodecClient::Type::HTTP1, GetParam()) {}

  void SetUp() override {
    fake_upstreams_.emplace_back(
        new AutonomousUpstream(0, FakeHttpConnection::Type::HTTP1, version_));
    registerPort("upstream", fake_upstreams_[0]->localAddress()->ip()->port());
    fake_upstreams_.emplace_back(new FakeUpstream(0, FakeHttpConnection::Type::HTTP1, version_));
    registerPort("upstream_squash", fake_upstreams_[1]->localAddress()->ip()->port());
    fake_upstreams_.back()->set_allow_unexpected_disconnects(true);

    ::setenv("SQUASH_ENV_TEST", ENV_VAR_VALUE, 1);

    createTestServer("test/config/integration/squash_filter.json", {"http"});

    codec_client_ = makeHttpConnection(lookupPort("http"));
  }
};

INSTANTIATE_TEST_CASE_P(IpVersions, SquashFilterIntegrationTestV1,
                        testing::ValuesIn(TestEnvironment::getIpVersionsForTest()));

TEST_P(SquashFilterIntegrationTestV1, TestHappyPath) {
  IntegrationStreamDecoderPtr response = sendDebugRequest(codec_client_);

  FakeHttpConnectionPtr fake_squash_connection =
      fake_upstreams_[1]->waitForHttpConnection(*dispatcher_);

  // Respond to create request
  FakeStreamPtr create_stream =
      sendSquashCreate(fake_squash_connection, "{\"metadata\":{\"name\":\"oF8iVdiJs5\"},"
                                               "\"spec\":{\"attachment\":{\"a\":\"b\"},"
                                               "\"image\":\"debug\",\"node\":\"debug-node\"},"
                                               "\"status\":{\"state\":\"none\"}}");

  // Respond to read attachment request
  FakeStreamPtr get_stream = sendSquashOk(
      fake_squash_connection, "{\"metadata\":{\"name\":\"oF8iVdiJs5\"},\"spec\":{"
                              "\"attachment\":{\"a\":\"b\"},\"image\":\"debug\",\"node\":"
                              "\"debug-node\"},\"status\":{\"state\":\"attached\"}}");

  response->waitForEndStream();

  EXPECT_STREQ("POST", create_stream->headers().Method()->value().c_str());
  // Make sure the env var was replaced
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

class SquashFilterIntegrationTest : public SquashFilterIntegrationTestBase {

public:
  SquashFilterIntegrationTest()
      : SquashFilterIntegrationTestBase(Http::CodecClient::Type::HTTP1, GetParam()) {}

  void createUpstreams() override {
    HttpIntegrationTest::createUpstreams();
    fake_upstreams_.emplace_back(new FakeUpstream(0, FakeHttpConnection::Type::HTTP2, version_));
  }

  /**
   * Initializer for an individual integration test.
   */
  void initialize() override {
    ::setenv("SQUASH_ENV_TEST", ENV_VAR_VALUE, 1);

    autonomous_upstream_ = true;

    config_helper_.addFilter(ConfigHelper::DEFAULT_SQUASH_FILTER);

    config_helper_.addConfigModifier([](envoy::api::v2::Bootstrap& bootstrap) {
      auto* squash_cluster = bootstrap.mutable_static_resources()->add_clusters();
      squash_cluster->MergeFrom(bootstrap.static_resources().clusters()[0]);
      squash_cluster->set_name("squash");
      squash_cluster->mutable_http2_protocol_options();
    });

    HttpIntegrationTest::initialize();
    fake_upstreams_.back()->set_allow_unexpected_disconnects(true);

    codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));
  }

  /**
   * Initialize before every test.
   */
  void SetUp() override { initialize(); }
};

INSTANTIATE_TEST_CASE_P(IpVersions, SquashFilterIntegrationTest,
                        testing::ValuesIn(TestEnvironment::getIpVersionsForTest()));

TEST_P(SquashFilterIntegrationTest, TestHappyPath) {
  IntegrationStreamDecoderPtr response = sendDebugRequest(codec_client_);

  FakeHttpConnectionPtr fake_squash_connection =
      fake_upstreams_[1]->waitForHttpConnection(*dispatcher_);

  // Respond to create request
  FakeStreamPtr create_stream =
      sendSquashCreate(fake_squash_connection, "{\"metadata\":{\"name\":\"oF8iVdiJs5\"},"
                                               "\"spec\":{\"attachment\":{\"a\":\"b\"},"
                                               "\"image\":\"debug\",\"node\":\"debug-node\"},"
                                               "\"status\":{\"state\":\"none\"}}");

  // Respond to read attachment request
  FakeStreamPtr get_stream = sendSquashOk(
      fake_squash_connection, "{\"metadata\":{\"name\":\"oF8iVdiJs5\"},\"spec\":{"
                              "\"attachment\":{\"a\":\"b\"},\"image\":\"debug\",\"node\":"
                              "\"debug-node\"},\"status\":{\"state\":\"attached\"}}");

  response->waitForEndStream();

  EXPECT_STREQ("POST", create_stream->headers().Method()->value().c_str());
  // Make sure the env var was replaced
  ProtobufWkt::Struct actualbody;
  MessageUtil::loadFromJson(TestUtility::bufferToString(create_stream->body()), actualbody);

  ProtobufWkt::Struct expectedbody;
  MessageUtil::loadFromJson("{\"spec\": { \"attachment\" : { \"env\": \"" ENV_VAR_VALUE
                            "\" } , \"match_request\":true} }",
                            expectedbody);

  EXPECT_TRUE(MessageDifferencer::Equals(expectedbody, actualbody));

  // The second request should be for the created object
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

  // Respond to create request
  FakeStreamPtr create_stream =
      sendSquashCreate(fake_squash_connection, "{\"metadata\":{\"name\":\"oF8iVdiJs5\"},"
                                               "\"spec\":{\"attachment\":{\"a\":\"b\"},"
                                               "\"image\":\"debug\",\"node\":\"debug-node\"},"
                                               "\"status\":{\"state\":\"none\"}}");
  // Respond to read attachment request with error!
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

  // Respond to create request
  FakeStreamPtr create_stream =
      sendSquashCreate(fake_squash_connection, "{\"metadata\":{\"name\":\"oF8iVdiJs5\"},"
                                               "\"spec\":{\"attachment\":{\"a\":\"b\"},"
                                               "\"image\":\"debug\",\"node\":\"debug-node\"},"
                                               "\"status\":{\"state\":\"none\"}}");
  // Respond to read attachment. since attachment_timeout is smaller than attachment_poll_period
  // config, just one response is enough, as the filter will timeout (and continue the iteration)
  // before issuing another get attachment request.
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

  // Respond to create request
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

  // Respond to create request
  FakeStreamPtr create_stream =
      sendSquashCreate(fake_squash_connection, "{\"metadata\":{\"name\":\"oF8iVdiJs5\"},"
                                               "\"spec\":{\"attachment\":{\"a\":\"b\"},"
                                               "\"image\":\"debug\",\"node\":\"debug-node\"},"
                                               "\"status\":{\"state\":\"none\"}}");
  // Respond to read attachment request with error!
  FakeStreamPtr get_stream = sendSquashOk(fake_squash_connection, "not json...");

  response->waitForEndStream();

  EXPECT_STREQ("200", response->headers().Status()->value().c_str());

  codec_client_->close();
  fake_squash_connection->close();
  fake_squash_connection->waitForDisconnect();
}

} // namespace Envoy
