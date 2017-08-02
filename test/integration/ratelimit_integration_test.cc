#include "common/buffer/zero_copy_input_stream_impl.h"
#include "common/grpc/codec.h"
#include "common/grpc/common.h"
#include "common/ratelimit/ratelimit.pb.h"

#include "test/integration/integration.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace {

class RatelimitIntegrationTest : public BaseIntegrationTest,
                                 public testing::TestWithParam<Network::Address::IpVersion> {
public:
  RatelimitIntegrationTest() : BaseIntegrationTest(GetParam()) {}

  void SetUp() override {
    fake_upstreams_.emplace_back(new FakeUpstream(0, FakeHttpConnection::Type::HTTP2, version_));
    registerPort("upstream_0", fake_upstreams_.back()->localAddress()->ip()->port());
    fake_upstreams_.emplace_back(new FakeUpstream(0, FakeHttpConnection::Type::HTTP2, version_));
    registerPort("upstream_1", fake_upstreams_.back()->localAddress()->ip()->port());
    createTestServer("test/config/integration/server_ratelimit.json", {"http"});

    upstream_response_.reset(new IntegrationStreamDecoder(*dispatcher_));
  }

  void TearDown() override {
    test_server_.reset();
    fake_upstreams_.clear();
  }

  void initiateClientConnection() {
    auto conn = makeClientConnection(lookupPort("http"));
    codec_client_ = makeHttpConnection(std::move(conn), Http::CodecClient::Type::HTTP1);
    Http::TestHeaderMapImpl headers{{":method", "POST"},       {":path", "/test/long/url"},
                                    {":scheme", "http"},       {":authority", "host"},
                                    {"x-lyft-user-id", "123"}, {"x-forwarded-for", "10.0.0.1"}};
    codec_client_->makeRequestWithBody(headers, request_size_, *upstream_response_);
  }

  void waitForRatelimitRequest() {
    fake_ratelimit_connection_ = fake_upstreams_[1]->waitForHttpConnection(*dispatcher_);
    ratelimit_request_ = fake_ratelimit_connection_->waitForNewStream();
    ratelimit_request_->waitForEndStream(*dispatcher_);
    EXPECT_STREQ("POST", ratelimit_request_->headers().Method()->value().c_str());
    EXPECT_STREQ("/pb.lyft.ratelimit.RateLimitService/ShouldRateLimit",
                 ratelimit_request_->headers().Path()->value().c_str());
    EXPECT_STREQ("application/grpc", ratelimit_request_->headers().ContentType()->value().c_str());

    pb::lyft::ratelimit::RateLimitRequest expected_request_msg;
    expected_request_msg.set_domain("some_domain");
    auto* entry = expected_request_msg.add_descriptors()->add_entries();
    entry->set_key("destination_cluster");
    entry->set_value("traffic");

    Grpc::Decoder decoder;
    std::vector<Grpc::Frame> decoded_frames;
    EXPECT_TRUE(decoder.decode(ratelimit_request_->body(), decoded_frames));
    EXPECT_EQ(1, decoded_frames.size());
    pb::lyft::ratelimit::RateLimitRequest request_msg;
    Buffer::ZeroCopyInputStreamImpl stream(std::move(decoded_frames[0].data_));
    EXPECT_TRUE(decoded_frames[0].flags_ == Grpc::GRPC_FH_DEFAULT);
    EXPECT_TRUE(request_msg.ParseFromZeroCopyStream(&stream));

    EXPECT_EQ(expected_request_msg.DebugString(), request_msg.DebugString());
  }

  void waitForSuccessfulUpstreamResponse() {
    fake_upstream_connection_ = fake_upstreams_[0]->waitForHttpConnection(*dispatcher_);
    upstream_request_ = fake_upstream_connection_->waitForNewStream();
    upstream_request_->waitForEndStream(*dispatcher_);

    upstream_request_->encodeHeaders(Http::TestHeaderMapImpl{{":status", "200"}}, false);
    upstream_request_->encodeData(response_size_, true);
    upstream_response_->waitForEndStream();

    EXPECT_TRUE(upstream_request_->complete());
    EXPECT_EQ(request_size_, upstream_request_->bodyLength());

    EXPECT_TRUE(upstream_response_->complete());
    EXPECT_STREQ("200", upstream_response_->headers().Status()->value().c_str());
    EXPECT_EQ(response_size_, upstream_response_->body().size());
  }

  void waitForFailedUpstreamResponse(uint32_t response_code) {
    upstream_response_->waitForEndStream();
    EXPECT_TRUE(upstream_response_->complete());
    EXPECT_STREQ(std::to_string(response_code).c_str(),
                 upstream_response_->headers().Status()->value().c_str());
  }

  void sendRateLimitResponse(pb::lyft::ratelimit::RateLimitResponse_Code code) {
    ratelimit_request_->encodeHeaders(Http::TestHeaderMapImpl{{":status", "200"}}, false);
    pb::lyft::ratelimit::RateLimitResponse response_msg;
    response_msg.set_overall_code(code);
    auto serialized_response = Grpc::Common::serializeBody(response_msg);
    ratelimit_request_->encodeData(*serialized_response, false);
    ratelimit_request_->encodeTrailers(Http::TestHeaderMapImpl{{"grpc-status", "0"}});
  }

  void cleanup() {
    codec_client_->close();
    if (fake_ratelimit_connection_ != nullptr) {
      fake_ratelimit_connection_->close();
      fake_ratelimit_connection_->waitForDisconnect();
    }
    if (fake_upstream_connection_ != nullptr) {
      fake_upstream_connection_->close();
      fake_upstream_connection_->waitForDisconnect();
    }
  }

  IntegrationCodecClientPtr codec_client_;
  FakeHttpConnectionPtr fake_upstream_connection_;
  FakeHttpConnectionPtr fake_ratelimit_connection_;
  IntegrationStreamDecoderPtr upstream_response_;
  FakeStreamPtr upstream_request_;
  FakeStreamPtr ratelimit_request_;

  const uint64_t request_size_ = 1024;
  const uint64_t response_size_ = 512;
};

INSTANTIATE_TEST_CASE_P(IpVersions, RatelimitIntegrationTest,
                        testing::ValuesIn(TestEnvironment::getIpVersionsForTest()));

TEST_P(RatelimitIntegrationTest, Ok) {
  initiateClientConnection();
  waitForRatelimitRequest();
  sendRateLimitResponse(pb::lyft::ratelimit::RateLimitResponse_Code_OK);
  waitForSuccessfulUpstreamResponse();
  cleanup();

  EXPECT_EQ(1, test_server_->counter("cluster.traffic.ratelimit.ok")->value());
  EXPECT_EQ(nullptr, test_server_->counter("cluster.traffic.ratelimit.over_limit"));
  EXPECT_EQ(nullptr, test_server_->counter("cluster.traffic.ratelimit.error"));
}

TEST_P(RatelimitIntegrationTest, OverLimit) {
  initiateClientConnection();
  waitForRatelimitRequest();
  sendRateLimitResponse(pb::lyft::ratelimit::RateLimitResponse_Code_OVER_LIMIT);
  waitForFailedUpstreamResponse(429);
  cleanup();

  EXPECT_EQ(nullptr, test_server_->counter("cluster.traffic.ratelimit.ok"));
  EXPECT_EQ(1, test_server_->counter("cluster.traffic.ratelimit.over_limit")->value());
  EXPECT_EQ(nullptr, test_server_->counter("cluster.traffic.ratelimit.error"));
}

TEST_P(RatelimitIntegrationTest, Error) {
  initiateClientConnection();
  waitForRatelimitRequest();
  ratelimit_request_->encodeHeaders(Http::TestHeaderMapImpl{{":status", "404"}}, true);
  // Rate limiter fails open
  waitForSuccessfulUpstreamResponse();
  cleanup();

  EXPECT_EQ(nullptr, test_server_->counter("cluster.traffic.ratelimit.ok"));
  EXPECT_EQ(nullptr, test_server_->counter("cluster.traffic.ratelimit.over_limit"));
  EXPECT_EQ(1, test_server_->counter("cluster.traffic.ratelimit.error")->value());
}

TEST_P(RatelimitIntegrationTest, Timeout) {
  initiateClientConnection();
  waitForRatelimitRequest();
  // Keep polling stats until the HTTP ratelimit wait times out.
  const uint32_t sleep_ms = 100;
  for (int32_t timeout_wait_ms = 50000; timeout_wait_ms > 0; timeout_wait_ms -= sleep_ms) {
    if (test_server_->counter("cluster.ratelimit.upstream_rq_timeout")->value() > 0) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(sleep_ms));
  }
  // Rate limiter fails open
  waitForSuccessfulUpstreamResponse();
  cleanup();

  EXPECT_EQ(1, test_server_->counter("cluster.ratelimit.upstream_rq_timeout")->value());
  EXPECT_EQ(1, test_server_->counter("cluster.ratelimit.upstream_rq_504")->value());
}

TEST_P(RatelimitIntegrationTest, ConnectImmediateDisconnect) {
  initiateClientConnection();
  fake_ratelimit_connection_ = fake_upstreams_[1]->waitForHttpConnection(*dispatcher_);
  fake_ratelimit_connection_->close();
  fake_ratelimit_connection_->waitForDisconnect(true);
  fake_ratelimit_connection_ = nullptr;
  // Rate limiter fails open
  waitForSuccessfulUpstreamResponse();
  cleanup();
}

TEST_P(RatelimitIntegrationTest, FailedConnect) {
  fake_upstreams_[1].reset();
  initiateClientConnection();
  // Rate limiter fails open
  waitForSuccessfulUpstreamResponse();
  cleanup();
}

} // namespace
} // namespace Envoy
