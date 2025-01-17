#include "source/common/buffer/buffer_impl.h"

#include "test/integration/http_integration.h"
#include "test/proto/helloworld.pb.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace {

class ConnectIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                               public HttpIntegrationTest {
public:
  ConnectIntegrationTest()
      : HttpIntegrationTest(Http::CodecType::HTTP1, GetParam()),
        dispatcher_(api_->allocateDispatcher("test_thread")) {}

  void SetUp() override {
    const std::string filter =
        R"yaml(
            name: connect_grpc_bridge
            typed_config:
              "@type": type.googleapis.com/envoy.extensions.filters.http.connect_grpc_bridge.v3.FilterConfig
        )yaml";
    config_helper_.prependFilter(filter);
  }

protected:
  Event::DispatcherPtr dispatcher_;
};

TEST_P(ConnectIntegrationTest, ConnectFilterRequestBufferLimit) {
  config_helper_.setBufferLimits(1024, 1024);
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeRequestWithBody(
      Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                     {":path", "/Service/Method"},
                                     {":scheme", "http"},
                                     {":authority", "host"},
                                     {"content-type", "application/proto"},
                                     {"connect-protocol-version", "1"}},
      1024 * 65, false);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_THAT(response->headers(), Http::HttpStatusIs("413"));
}

TEST_P(ConnectIntegrationTest, ConnectFilterResponseBufferLimit) {
  config_helper_.setBufferLimits(1024, 1024);
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeHeaderOnlyRequest(
      Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                     {":path", "/Service/Method"},
                                     {":scheme", "http"},
                                     {":authority", "host"},
                                     {"content-type", "application/proto"},
                                     {"connect-protocol-version", "1"}});
  waitForNextUpstreamRequest();

  upstream_request_->encodeHeaders(
      Http::TestResponseHeaderMapImpl{{":status", "200"},
                                      {"content-type", "application/grpc+proto"}},
      false);
  upstream_request_->encodeData(1024 * 65, false);
  ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_THAT(response->headers(), Http::HttpStatusIs("500"));
}

TEST_P(ConnectIntegrationTest, ConnectFilterUnaryRequestE2E) {
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  helloworld::HelloRequest connect_request;
  connect_request.set_name("test");

  std::string connect_request_pb;
  connect_request.SerializeToString(&connect_request_pb);

  auto response = codec_client_->makeRequestWithBody(
      Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                     {":path", "/Service/Method"},
                                     {":scheme", "http"},
                                     {":authority", "host"},
                                     {"content-type", "application/proto"},
                                     {"connect-protocol-version", "1"},
                                     {"connect-timeout-ms", "10000"}},
      connect_request_pb, true);
  waitForNextUpstreamRequest();

  helloworld::HelloRequest grpc_request;
  ASSERT_TRUE(upstream_request_->waitForGrpcMessage(*dispatcher_, grpc_request));
  EXPECT_THAT(grpc_request, ProtoEq(connect_request));

  EXPECT_THAT(upstream_request_->headers(),
              HeaderHasValueRef(Http::Headers::get().ContentType, "application/grpc+proto"));
  EXPECT_THAT(upstream_request_->headers(),
              HeaderHasValueRef(Http::Headers::get().Path, "/Service/Method"));
  EXPECT_THAT(upstream_request_->headers(),
              HeaderHasValueRef(Http::CustomHeaders::get().GrpcTimeout, "10000m"));

  helloworld::HelloReply grpc_response;
  grpc_response.set_message("success");
  upstream_request_->startGrpcStream(false);
  upstream_request_->encodeHeaders(
      Http::TestResponseHeaderMapImpl{{":status", "200"}, {"content-type", "application/grpc"}},
      false);
  upstream_request_->sendGrpcMessage(grpc_response);
  upstream_request_->finishGrpcStream(Grpc::Status::Ok);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_THAT(response->headers(), Http::HttpStatusIs("200"));
  EXPECT_THAT(response->headers(),
              HeaderHasValueRef(Http::Headers::get().ContentType, "application/proto"));

  helloworld::HelloReply connect_response;
  ASSERT_TRUE(connect_response.ParseFromString(response->body()));
  EXPECT_THAT(connect_response, ProtoEq(grpc_response));
}

TEST_P(ConnectIntegrationTest, ConnectFilterStreamingRequestE2E) {
  setUpstreamProtocol(Http::CodecType::HTTP2);
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  helloworld::HelloRequest connect_request;
  connect_request.set_name("test");

  auto response = codec_client_->makeRequestWithBody(
      Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                     {":path", "/Service/Method"},
                                     {":scheme", "http"},
                                     {":authority", "host"},
                                     {"content-type", "application/connect+proto"},
                                     {"connect-protocol-version", "1"},
                                     {"connect-timeout-ms", "10000"}},
      Grpc::Common::serializeToGrpcFrame(connect_request)->toString(), true);
  waitForNextUpstreamRequest();

  helloworld::HelloRequest grpc_request;
  ASSERT_TRUE(upstream_request_->waitForGrpcMessage(*dispatcher_, grpc_request));
  EXPECT_THAT(grpc_request, ProtoEq(connect_request));

  EXPECT_THAT(upstream_request_->headers(),
              HeaderHasValueRef(Http::Headers::get().ContentType, "application/grpc+proto"));
  EXPECT_THAT(upstream_request_->headers(),
              HeaderHasValueRef(Http::Headers::get().Path, "/Service/Method"));
  EXPECT_THAT(upstream_request_->headers(),
              HeaderHasValueRef(Http::CustomHeaders::get().GrpcTimeout, "10000m"));

  helloworld::HelloReply grpc_response;
  grpc_response.set_message("success");
  upstream_request_->startGrpcStream(false);
  upstream_request_->encodeHeaders(
      Http::TestResponseHeaderMapImpl{{":status", "200"},
                                      {"content-type", "application/grpc+proto"}},
      false);
  upstream_request_->sendGrpcMessage(grpc_response);
  upstream_request_->finishGrpcStream(Grpc::Status::Ok);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_THAT(response->headers(), Http::HttpStatusIs("200"));
  EXPECT_THAT(response->headers(),
              HeaderHasValueRef(Http::Headers::get().ContentType, "application/connect+proto"));

  Buffer::OwnedImpl response_body{response->body()};
  ASSERT_THAT(response_body.length(), testing::Gt(5));
  EXPECT_EQ('\0', response_body.drainInt<uint8_t>());
  auto len = response_body.drainBEInt<uint32_t>();
  helloworld::HelloReply connect_response;
  ASSERT_TRUE(connect_response.ParseFromString(response_body.toString().substr(0, len)));
  EXPECT_THAT(connect_response, ProtoEq(grpc_response));
  response_body.drain(len);

  // Connect end-of-stream frame
  ASSERT_THAT(response_body.length(), testing::Gt(5));
  EXPECT_EQ('\2', response_body.drainInt<uint8_t>());
  EXPECT_EQ(2U, response_body.drainBEInt<uint32_t>());
  EXPECT_EQ(response_body.toString(), "{}");
}

INSTANTIATE_TEST_SUITE_P(IpVersions, ConnectIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

} // namespace
} // namespace Envoy
