#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"

#include "source/common/websocket/codec.h"

#include "test/integration/http_protocol_integration.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

using testing::Eq;

namespace Envoy {
namespace {

// Regression coverage for: two WebSocket sessions multiplexed as independent HTTP/2 streams over
// ONE downstream connection must get independent per-session token buckets, not a single bucket
// shared across the whole connection. Before the fix, the bucket was stored in
// StreamInfo::FilterState with LifeSpan::Connection (visible to every stream on the connection);
// it's now stored with LifeSpan::Request (private to each stream).
//
// This deliberately never completes (or even waits on) an upstream response: the filter's rate
// limiting runs entirely on the downstream decode path (decodeHeaders/decodeData), independent of
// what upstream does, so there's nothing to gain - and real fragility to avoid - in driving a
// full upstream round trip for an extended-CONNECT WebSocket stream here.
class WsLocalRateLimitIntegrationTest : public HttpProtocolIntegrationTest {
protected:
  void initializeFilter() {
    config_helper_.addConfigModifier(
        [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
               hcm) {
          hcm.mutable_http2_protocol_options()->set_allow_connect(true);
          // Harmless no-op when downstream isn't HTTP/3; lets the same initializeFilter() serve
          // both the HTTP/2 and HTTP/3 instantiations below.
          hcm.mutable_http3_protocol_options()->set_allow_extended_connect(true);
          hcm.add_upgrade_configs()->set_upgrade_type("websocket");
        });
    // max_tokens/tokens_per_fill = 1 with a fill_interval far longer than the test's runtime, so
    // the first frame on a given session's bucket is allowed and every subsequent one on that
    // same bucket is rejected, with no refill happening mid-test. rejection_message is set so
    // this test exercises sendRejectionFrame() - without it, a rejected frame is just silently
    // dropped and that code path (which calls decoder_callbacks_->encodeData(), unsafe unless
    // response headers already exist for the stream) goes uncovered.
    config_helper_.prependFilter(R"EOF(
name: envoy.filters.http.ws_local_ratelimit
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.ws_local_ratelimit.v3alpha.WsLocalRateLimit
  stat_prefix: ws_local_rate_limiter
  rejection_message: "rate limit exceeded"
  token_bucket:
    max_tokens: 1
    tokens_per_fill: 1
    fill_interval: 1000s
)EOF");
    initialize();
  }

  Http::TestRequestHeaderMapImpl websocketUpgradeHeaders() {
    return Http::TestRequestHeaderMapImpl{{":authority", "sni.lyft.com"},
                                          {":path", "/"},
                                          {":method", "GET"},
                                          {":scheme", "http"},
                                          {"upgrade", "websocket"},
                                          {"connection", "keep-alive, upgrade"}};
  }

  // Encodes a single unmasked WebSocket text frame carrying `payload`.
  Buffer::OwnedImpl encodeTextFrame(absl::string_view payload) {
    WebSocket::Encoder encoder;
    WebSocket::Frame frame;
    frame.final_fragment_ = true;
    frame.opcode_ = WebSocket::kFrameOpcodeText;
    frame.masking_key_ = std::nullopt;
    frame.payload_length_ = payload.size();
    frame.payload_ = std::make_unique<Buffer::OwnedImpl>(payload.data(), payload.size());

    Buffer::OwnedImpl out;
    auto header = encoder.encodeFrameHeader(frame);
    out.add(header->data(), header->size());
    out.add(*frame.payload_);
    return out;
  }
};

// Downstream HTTP/2 is what makes stream multiplexing over one connection possible. Upstream
// protocol is irrelevant here since the test never touches the upstream side at all.
INSTANTIATE_TEST_SUITE_P(H2Protocols, WsLocalRateLimitIntegrationTest,
                         testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParams(
                             {Http::CodecType::HTTP2}, {Http::CodecType::HTTP1})),
                         HttpProtocolIntegrationTest::protocolTestParamsToString);

// Same idea over HTTP/3: multiple WebSocket sessions can be multiplexed as independent QUIC
// streams over ONE UDP/QUIC connection (RFC 9220 extended CONNECT). Every TEST_P below also runs
// under this instantiation, including TwoStreamsOnOneConnectionGetIndependentBuckets - so it
// doubles as the "one UDP connection, multiple QUIC streams" case, not just this instantiation's
// own TwoSeparateConnectionsGetIndependentBuckets test below.
INSTANTIATE_TEST_SUITE_P(H3Protocols, WsLocalRateLimitIntegrationTest,
                         testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParams(
                             {Http::CodecType::HTTP3}, {Http::CodecType::HTTP1})),
                         HttpProtocolIntegrationTest::protocolTestParamsToString);

// HTTP/1.1 downstream: not a multiplexing-capable protocol at all (one connection can only ever
// carry one in-flight stream), so this isn't testing the LifeSpan::Connection vs
// LifeSpan::Request bug - that bug can't manifest here, since Connection-level and Request-level
// scoping are equivalent when a connection only ever has one stream. This instantiation exists as
// a baseline regression check that the filter still works on its original, most common real-world
// shape (classic HTTP/1.1 "Upgrade: websocket") - only TwoSeparateConnectionsGetIndependentBuckets
// runs under it; TwoStreamsOnOneConnectionGetIndependentBuckets explicitly skips HTTP/1.1 below
// since opening a second concurrent stream on one connection is meaningless (and unsupported) for
// HTTP/1.1.
INSTANTIATE_TEST_SUITE_P(H1Protocols, WsLocalRateLimitIntegrationTest,
                         testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParams(
                             {Http::CodecType::HTTP1}, {Http::CodecType::HTTP1})),
                         HttpProtocolIntegrationTest::protocolTestParamsToString);

TEST_P(WsLocalRateLimitIntegrationTest, TwoStreamsOnOneConnectionGetIndependentBuckets) {
  // HTTP/1.1 can't have two concurrent in-flight streams on one connection at all - this test is
  // specifically about multiplexing, which only H2Protocols/H3Protocols can exercise.
  if (downstreamProtocol() == Http::CodecType::HTTP1) {
    return;
  }

  initializeFilter();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  // Two independent WebSocket sessions, opened as two streams on the SAME downstream connection
  // (codec_client_ is reused, not recreated) - two HTTP/2 streams over one TCP connection under
  // the H2Protocols instantiation, two QUIC streams over one UDP connection under H3Protocols.
  auto encoder_decoder_a = codec_client_->startRequest(websocketUpgradeHeaders());
  Http::RequestEncoder* encoder_a = &encoder_decoder_a.first;

  auto encoder_decoder_b = codec_client_->startRequest(websocketUpgradeHeaders());
  Http::RequestEncoder* encoder_b = &encoder_decoder_b.first;

  // Session A sends two frames: the first consumes its only token, the second is rejected.
  Buffer::OwnedImpl frame_a1 = encodeTextFrame("hello-a-1");
  codec_client_->sendData(*encoder_a, frame_a1, false);
  Buffer::OwnedImpl frame_a2 = encodeTextFrame("hello-a-2");
  codec_client_->sendData(*encoder_a, frame_a2, false);

  test_server_->waitForCounter("ws_local_rate_limiter.ws_local_rate_limit.ok", Eq(1));
  test_server_->waitForCounter("ws_local_rate_limiter.ws_local_rate_limit.rate_limited", Eq(1));

  // Session B sends one frame on the OTHER stream of the SAME connection. If the two sessions
  // incorrectly shared one connection-level bucket (the bug this test guards against), this
  // frame would also be rejected because session A already exhausted the shared bucket.
  Buffer::OwnedImpl frame_b1 = encodeTextFrame("hello-b-1");
  codec_client_->sendData(*encoder_b, frame_b1, false);

  test_server_->waitForCounter("ws_local_rate_limiter.ws_local_rate_limit.ok", Eq(2));
  test_server_->waitForCounter("ws_local_rate_limiter.ws_local_rate_limit.rate_limited", Eq(1));

  // Neither stream ever received a response, so both must be explicitly reset - otherwise the
  // fixture's teardown assertion that codec_client_->numActiveRequests() == 0 fails.
  codec_client_->sendReset(*encoder_a);
  codec_client_->sendReset(*encoder_b);
  codec_client_->close();
}

// Baseline sanity check, distinct from the multiplexing test above: two WebSocket sessions on
// two SEPARATE downstream connections (two separate UDP 4-tuples under H3Protocols, two separate
// TCP connections under H2Protocols) must each get their own full budget, unaffected by each
// other - trivially expected regardless of the LifeSpan::Connection vs LifeSpan::Request bug,
// since each connection has its own independent StreamInfo::FilterState tree from the start. This
// mainly confirms the filter's WebSocket detection and rate limiting work at all end-to-end over
// a given protocol before anything more specific (like real multiplexing) is asked of it.
TEST_P(WsLocalRateLimitIntegrationTest, TwoSeparateConnectionsGetIndependentBuckets) {
  initializeFilter();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  IntegrationCodecClientPtr codec_client_b = makeHttpConnection(lookupPort("http"));

  auto encoder_decoder_a = codec_client_->startRequest(websocketUpgradeHeaders());
  Http::RequestEncoder* encoder_a = &encoder_decoder_a.first;

  auto encoder_decoder_b = codec_client_b->startRequest(websocketUpgradeHeaders());
  Http::RequestEncoder* encoder_b = &encoder_decoder_b.first;

  // Connection A: first frame consumes its only token, second is rejected.
  Buffer::OwnedImpl frame_a1 = encodeTextFrame("hello-a-1");
  codec_client_->sendData(*encoder_a, frame_a1, false);
  Buffer::OwnedImpl frame_a2 = encodeTextFrame("hello-a-2");
  codec_client_->sendData(*encoder_a, frame_a2, false);

  test_server_->waitForCounter("ws_local_rate_limiter.ws_local_rate_limit.ok", Eq(1));
  test_server_->waitForCounter("ws_local_rate_limiter.ws_local_rate_limit.rate_limited", Eq(1));

  // Connection B has its own full budget, unaffected by A's already being exhausted.
  Buffer::OwnedImpl frame_b1 = encodeTextFrame("hello-b-1");
  codec_client_b->sendData(*encoder_b, frame_b1, false);

  test_server_->waitForCounter("ws_local_rate_limiter.ws_local_rate_limit.ok", Eq(2));
  test_server_->waitForCounter("ws_local_rate_limiter.ws_local_rate_limit.rate_limited", Eq(1));

  codec_client_->sendReset(*encoder_a);
  codec_client_->close();
  codec_client_b->sendReset(*encoder_b);
  codec_client_b->close();
}

} // namespace
} // namespace Envoy
