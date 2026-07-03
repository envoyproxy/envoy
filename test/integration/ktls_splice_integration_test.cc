#include <string>

#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/network/transport_socket.h"
#include "envoy/stats/stats.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/assert.h"
#include "source/common/network/raw_buffer_socket.h"
#include "source/common/protobuf/protobuf.h"
#include "source/extensions/transport_sockets/common/passthrough.h"

#include "test/integration/http_integration.h"
#include "test/test_common/registry.h"
#include "test/test_common/utility.h"

#include "absl/strings/str_cat.h"
#include "gtest/gtest.h"

// The fixture is Linux-only because the fast path uses splice() on loopback fds. Fake kTLS keeps
// the wire plaintext while reporting installed kTLS to arm the splice.
#ifdef __linux__

namespace Envoy {
namespace {

// Test-only upstream socket that wraps raw_buffer but reports trusted kTLS.
class FakeKtlsSocket : public Extensions::TransportSockets::PassthroughSocket {
public:
  FakeKtlsSocket(Network::TransportSocketPtr&& inner_socket)
      : PassthroughSocket(std::move(inner_socket)) {}

  // Backed by a member so the returned OptRef remains valid.
  OptRef<const Network::KtlsBytestreamInfo> ktlsBytestreamInfo() const override {
    info_.installed = true;
    info_.trusted_peer = true;
    return info_;
  }

private:
  // Backing store for ktlsBytestreamInfo.
  mutable Network::KtlsBytestreamInfo info_{};
};

// Wraps an inner raw_buffer upstream factory and produces FakeKtlsSocket instances.
class FakeKtlsSocketFactory : public Extensions::TransportSockets::PassthroughFactory {
public:
  FakeKtlsSocketFactory(Network::UpstreamTransportSocketFactoryPtr&& inner_factory)
      : PassthroughFactory(std::move(inner_factory)) {}

  Network::TransportSocketPtr
  createTransportSocket(Network::TransportSocketOptionsConstSharedPtr options,
                        Upstream::HostDescriptionConstSharedPtr host) const override {
    auto inner_socket = transport_socket_factory_->createTransportSocket(options, host);
    if (inner_socket == nullptr) {
      return nullptr;
    }
    return std::make_unique<FakeKtlsSocket>(std::move(inner_socket));
  }
};

// Config factory for envoy.transport_sockets.fake_ktls.
class FakeKtlsConfigFactory : public Server::Configuration::UpstreamTransportSocketConfigFactory {
public:
  std::string name() const override { return "envoy.transport_sockets.fake_ktls"; }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<Protobuf::Struct>();
  }

  absl::StatusOr<Network::UpstreamTransportSocketFactoryPtr>
  createTransportSocketFactory(const Protobuf::Message&,
                               Server::Configuration::TransportSocketFactoryContext&) override {
    return std::make_unique<FakeKtlsSocketFactory>(
        std::make_unique<Network::RawBufferSocketFactory>());
  }
};

// Builds a deterministic body so the spliced bytes can be asserted byte-for-byte at the client.
std::string makeBody(uint64_t size) {
  std::string body;
  body.reserve(size);
  for (uint64_t i = 0; i < size; i++) {
    body.push_back(static_cast<char>('A' + (i % 26)));
  }
  return body;
}

// Minimum body the splice engages on, mirrored from SpliceCoordinator::MinSpliceBodyBytes.
constexpr uint64_t MinSpliceBodyBytes = 64 * 1024;

class KtlsSpliceIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                  public HttpIntegrationTest {
public:
  KtlsSpliceIntegrationTest() : HttpIntegrationTest(Http::CodecType::HTTP1, GetParam()) {}

  void initializeWithFakeKtlsUpstream() {
    // Enable the dark runtime guard and point cluster_0 at the fake kTLS socket.
    config_helper_.addRuntimeOverride("envoy.reloadable_features.http1_ktls_body_splice", "true");
    config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* cluster_transport_socket =
          bootstrap.mutable_static_resources()->mutable_clusters(0)->mutable_transport_socket();
      cluster_transport_socket->set_name("envoy.transport_sockets.fake_ktls");
      Protobuf::Struct empty;
      cluster_transport_socket->mutable_typed_config()->PackFrom(empty);
    });
    initialize();
  }

  // Sends headers first so engage can commit before the body arrives.
  IntegrationStreamDecoderPtr runDownload(uint64_t body_size) {
    codec_client_ = makeHttpConnection(lookupPort("http"));
    auto response = codec_client_->makeHeaderOnlyRequest(Http::TestRequestHeaderMapImpl{
        {":method", "GET"}, {":path", "/download"}, {":scheme", "http"}, {":authority", "host"}});

    waitForNextUpstreamRequest();
    upstream_request_->encodeHeaders(
        Http::TestResponseHeaderMapImpl{{":status", "200"},
                                        {"content-length", absl::StrCat(body_size)}},
        false);
    // Wait for engage to run before sending the body.
    response->waitForHeaders();

    const std::string body = makeBody(body_size);
    Buffer::OwnedImpl response_data(body);
    upstream_request_->encodeData(response_data, true);

    RELEASE_ASSERT(response->waitForEndStream(), "download did not complete");
    EXPECT_TRUE(response->complete());
    EXPECT_EQ("200", response->headers().getStatusValue());
    EXPECT_EQ(body_size, response->body().size());
    EXPECT_EQ(body, response->body());
    return response;
  }

  // Sends request headers first so upload engage can wait for upstream kTLS-TX before the body.
  IntegrationStreamDecoderPtr runUpload(uint64_t body_size) {
    codec_client_ = makeHttpConnection(lookupPort("http"));
    auto encoder_decoder = codec_client_->startRequest(
        Http::TestRequestHeaderMapImpl{{":method", "PUT"},
                                       {":path", "/upload"},
                                       {":scheme", "http"},
                                       {":authority", "host"},
                                       {"content-length", absl::StrCat(body_size)}});
    auto& encoder = encoder_decoder.first;
    auto response = std::move(encoder_decoder.second);

    // Wait for the upstream stream, not end stream, before sending the body.
    RELEASE_ASSERT(
        fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_),
        "no upstream connection");
    RELEASE_ASSERT(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_),
                   "no upstream stream");

    const std::string body = makeBody(body_size);
    codec_client_->sendData(encoder, body, true);

    RELEASE_ASSERT(upstream_request_->waitForEndStream(*dispatcher_), "upload did not complete");
    // FakeStream::body() drains the accumulated body, so snapshot it once and assert on the copy.
    const std::string received = upstream_request_->body().toString();
    EXPECT_EQ(body_size, received.size());
    EXPECT_EQ(body, received);

    upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);
    RELEASE_ASSERT(response->waitForEndStream(), "upload response did not complete");
    EXPECT_TRUE(response->complete());
    EXPECT_EQ("200", response->headers().getStatusValue());
    return response;
  }

  FakeKtlsConfigFactory config_factory_;
  Registry::InjectFactory<Server::Configuration::UpstreamTransportSocketConfigFactory>
      registered_config_factory_{config_factory_};
};

INSTANTIATE_TEST_SUITE_P(IpVersions, KtlsSpliceIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

// A 128 KiB download (>= the 64 KiB floor) over a fake-kTLS upstream engages the splice and the
// body still arrives byte-for-byte. The engaged counter assertion is what proves the fast-path
// actually fired rather than the buffered path silently carrying the bytes.
TEST_P(KtlsSpliceIntegrationTest, DownloadAboveThresholdEngagesSplice) {
  initializeWithFakeKtlsUpstream();
  auto response = runDownload(128 * 1024);
  test_server_->waitForCounter("cluster.cluster_0.http1_ktls_splice_engaged", testing::Ge(1));
  test_server_->waitForCounter("cluster.cluster_0.http1_ktls_splice_completed", testing::Ge(1));
  EXPECT_EQ(1, test_server_->counter("cluster.cluster_0.http1_ktls_splice_engaged")->value());
  EXPECT_EQ(1, test_server_->counter("cluster.cluster_0.http1_ktls_splice_completed")->value());
}

// Below-threshold bodies use the buffered path and do not engage the splice.
TEST_P(KtlsSpliceIntegrationTest, DownloadBelowThresholdDoesNotEngage) {
  initializeWithFakeKtlsUpstream();
  auto response = runDownload(MinSpliceBodyBytes - 1);
  const Stats::CounterSharedPtr engaged =
      test_server_->counter("cluster.cluster_0.http1_ktls_splice_engaged");
  EXPECT_EQ(0, engaged == nullptr ? 0 : engaged->value());
}

// A 128 KiB PUT body (>= the 64 KiB floor) over the fake-kTLS upstream engages the upload splice
// (downstream source to kTLS-TX upstream sink) and the upstream still receives the body
// byte-for-byte. The engaged counter assertion is what proves the request-body fast-path actually
// fired rather than the buffered path silently carrying the bytes. This is the upload mirror of
// DownloadAboveThresholdEngagesSplice.
TEST_P(KtlsSpliceIntegrationTest, UploadAboveThresholdEngagesSplice) {
  initializeWithFakeKtlsUpstream();
  auto response = runUpload(128 * 1024);
  test_server_->waitForCounter("cluster.cluster_0.http1_ktls_splice_engaged", testing::Ge(1));
  test_server_->waitForCounter("cluster.cluster_0.http1_ktls_splice_completed", testing::Ge(1));
  EXPECT_EQ(1, test_server_->counter("cluster.cluster_0.http1_ktls_splice_engaged")->value());
  EXPECT_EQ(1, test_server_->counter("cluster.cluster_0.http1_ktls_splice_completed")->value());
  // A clean upload completes rather than truncates.
  const Stats::CounterSharedPtr truncated =
      test_server_->counter("cluster.cluster_0.http1_ktls_splice_truncated");
  EXPECT_EQ(0, truncated == nullptr ? 0 : truncated->value());
}

// An eligible upload sent with its body inline. Whether the coordinator abandons the splice (the
// body is fully buffered before engage) or engages it (the body is still arriving) depends on how
// the request is segmented on the wire relative to the upstream connection, so it is not
// deterministic at the integration level; splice_coordinator_test covers both decisions directly.
// Either way the body must reach the upstream byte-for-byte, the armed splice must resolve exactly
// once, and a clean upload must never truncate.
TEST_P(KtlsSpliceIntegrationTest, UploadInlineBodyRelayedIntact) {
  initializeWithFakeKtlsUpstream();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  const std::string body = makeBody(128 * 1024);
  auto response = codec_client_->makeRequestWithBody(
      Http::TestRequestHeaderMapImpl{{":method", "PUT"},
                                     {":path", "/upload"},
                                     {":scheme", "http"},
                                     {":authority", "host"},
                                     {"content-length", absl::StrCat(body.size())}},
      body);

  waitForNextUpstreamRequest();
  const std::string received = upstream_request_->body().toString();
  EXPECT_EQ(body, received);
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());

  // The counters are incremented on the worker thread before the response completes above, so they
  // are settled here. Exactly one of abandon or engage fires for the armed splice, and a clean
  // upload never truncates.
  const auto splice_counter = [this](absl::string_view name) -> uint64_t {
    const Stats::CounterSharedPtr counter =
        test_server_->counter(absl::StrCat("cluster.cluster_0.", name));
    return counter == nullptr ? 0 : counter->value();
  };
  EXPECT_GE(splice_counter("http1_ktls_splice_abandoned") +
                splice_counter("http1_ktls_splice_engaged"),
            1);
  EXPECT_EQ(0, splice_counter("http1_ktls_splice_truncated"));
}

// Engaged download truncation force-closes upstream after reinstalling the downstream sink, so
// teardown must reset the stream without hitting a detached file event.
TEST_P(KtlsSpliceIntegrationTest, EngagedDownloadTruncationDoesNotCrash) {
  initializeWithFakeKtlsUpstream();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeHeaderOnlyRequest(Http::TestRequestHeaderMapImpl{
      {":method", "GET"}, {":path", "/download"}, {":scheme", "http"}, {":authority", "host"}});

  // At/above the 64 KiB floor so the download splice arms and engages. The pump owns the fds once
  // engage() commits, which is the state the crash fix protects on teardown.
  constexpr uint64_t content_length = 128 * 1024;
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(
      Http::TestResponseHeaderMapImpl{{":status", "200"},
                                      {"content-length", absl::StrCat(content_length)}},
      false);
  // Wait for headers so scheduled engage can run before body bytes.
  response->waitForHeaders();

  // Confirm engagement before forcing truncation.
  test_server_->waitForCounter("cluster.cluster_0.http1_ktls_splice_engaged", testing::Ge(1));

  // Send strictly fewer bytes than the advertised Content-Length (end_stream=false), then close the
  // upstream connection so the body is cut mid-transfer. The pump sees EOF before the bound.
  const std::string partial = makeBody(content_length / 2);
  Buffer::OwnedImpl partial_data(partial);
  upstream_request_->encodeData(partial_data, false);
  RELEASE_ASSERT(fake_upstream_connection_->close(), "failed to close upstream");

  // The downstream stream is reset / left incomplete, no clean end-of-stream with the full body. If
  // the teardown had aborted on the detached-sink ENVOY_BUG, the process would have crashed before
  // reaching this point.
  ASSERT_TRUE(response->waitForAnyTermination());
  EXPECT_FALSE(response->complete());

  // Mid-body close counts as truncated after engagement.
  test_server_->waitForCounter("cluster.cluster_0.http1_ktls_splice_truncated", testing::Ge(1));
  EXPECT_GE(test_server_->counter("cluster.cluster_0.http1_ktls_splice_engaged")->value(), 1);
  EXPECT_EQ(0,
            test_server_->counter("cluster.cluster_0.http1_ktls_splice_completed") == nullptr
                ? 0
                : test_server_->counter("cluster.cluster_0.http1_ktls_splice_completed")->value());
}

TEST_P(KtlsSpliceIntegrationTest, EngagedUploadTruncationDoesNotCrash) {
  initializeWithFakeKtlsUpstream();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto encoder_decoder =
      codec_client_->startRequest(Http::TestRequestHeaderMapImpl{{":method", "PUT"},
                                                                 {":path", "/upload"},
                                                                 {":scheme", "http"},
                                                                 {":authority", "host"},
                                                                 {"content-length", "131072"}});
  auto& encoder = encoder_decoder.first;

  RELEASE_ASSERT(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_),
                 "no upstream connection");
  RELEASE_ASSERT(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_),
                 "no upstream stream");
  test_server_->waitForCounter("cluster.cluster_0.http1_ktls_splice_engaged", testing::Ge(1));

  codec_client_->sendData(encoder, makeBody(64 * 1024), false);
  codec_client_->close();

  ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
  test_server_->waitForCounter("cluster.cluster_0.http1_ktls_splice_truncated", testing::Ge(1));
  EXPECT_GE(test_server_->counter("cluster.cluster_0.http1_ktls_splice_engaged")->value(), 1);
  EXPECT_EQ(0,
            test_server_->counter("cluster.cluster_0.http1_ktls_splice_completed") == nullptr
                ? 0
                : test_server_->counter("cluster.cluster_0.http1_ktls_splice_completed")->value());
}

} // namespace
} // namespace Envoy

#endif // __linux__
