#include "test/integration/http_protocol_integration.h"

using testing::HasSubstr;
using testing::Not;
using testing::StartsWith;

namespace Envoy {

class HystrixIntegrationTest : public HttpProtocolIntegrationTest {};

INSTANTIATE_TEST_SUITE_P(Protocols, HystrixIntegrationTest,
                         testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParams(
                             {Http::CodecClient::Type::HTTP1, Http::CodecClient::Type::HTTP2},
                             {FakeHttpConnection::Type::HTTP1})),
                         HttpProtocolIntegrationTest::protocolTestParamsToString);

TEST_P(HystrixIntegrationTest, NoChunkEncoding) {
  config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    auto* metrics_sink = bootstrap.add_stats_sinks();
    metrics_sink->set_name("envoy.stat_sinks.hystrix");
    bootstrap.mutable_stats_flush_interval()->CopyFrom(
        Protobuf::util::TimeUtil::MillisecondsToDuration(100));
  });
  initialize();

  if (downstreamProtocol() == Http::CodecClient::Type::HTTP1) {
    // For HTTP/1.1 we use a raw client to make absolutely sure there is no chunk encoding.
    Buffer::OwnedImpl buffer("GET /hystrix_event_stream HTTP/1.1\r\nHost: admin\r\n\r\n");
    std::string response;
    RawConnectionDriver connection(
        lookupPort("admin"), buffer,
        [&](Network::ClientConnection& client, const Buffer::Instance& data) -> void {
          response.append(data.toString());
          // Wait until there is a flush.
          if (response.find("rollingCountCollapsedRequests") != std::string::npos) {
            client.close(Network::ConnectionCloseType::NoFlush);
          }
        },
        version_);
    connection.run();
    EXPECT_THAT(response, StartsWith("HTTP/1.1 200 OK\r\n"));
    // Make sure that the response is not actually chunk encoded, but it does have the hystrix flush
    // trailer.
    EXPECT_THAT(response, Not(HasSubstr("chunked")));
    EXPECT_THAT(response, Not(HasSubstr("3\r\n:\n\n")));
    EXPECT_THAT(response, HasSubstr(":\n\n"));
    connection.close();
  } else {
    codec_client_ = makeHttpConnection(lookupPort("admin"));
    auto response = codec_client_->makeHeaderOnlyRequest(
        Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                       {":path", "/hystrix_event_stream"},
                                       {":scheme", "http"},
                                       {":authority", "admin"}});
    response->waitForBodyData(1);
    EXPECT_THAT(response->body(), HasSubstr("rollingCountCollapsedRequests"));
    codec_client_->close();
  }
}

} // namespace Envoy
