#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "test/integration/http_protocol_integration.h"
#include "test/test_common/utility.h"

#include "absl/synchronization/notification.h"
#include "absl/time/time.h"
#include "gtest/gtest.h"
#include "quiche/common/quiche_buffer_allocator.h"
#include "quiche/common/simple_buffer_allocator.h"
#include "quiche/web_transport/web_transport.h"

namespace Envoy {
namespace {

// Echoes everything read on a WebTransport stream back out the same (bidirectional) stream. Used on
// the upstream (fake) side as the "application" the proxy bridges to. Runs entirely on the upstream
// dispatcher thread (all webtransport callbacks for a session fire on that session's thread).
class EchoStreamVisitor : public webtransport::StreamVisitor {
public:
  explicit EchoStreamVisitor(webtransport::Stream* stream) : stream_(stream) {}

  void OnCanRead() override {
    std::string data;
    webtransport::Stream::ReadResult result = stream_->Read(&data);
    (void)result;
    pending_.append(data);
    flush();
  }
  void OnCanWrite() override { flush(); }
  void OnResetStreamReceived(webtransport::StreamErrorCode /*error*/) override {}
  void OnStopSendingReceived(webtransport::StreamErrorCode /*error*/) override {}
  void OnWriteSideInDataRecvdState() override {}

private:
  void flush() {
    while (!pending_.empty() && stream_->CanWrite()) {
      quiche::QuicheMemSlice slice(
          quiche::QuicheBuffer::Copy(quiche::SimpleBufferAllocator::Get(), pending_));
      absl::Span<quiche::QuicheMemSlice> span(&slice, 1);
      if (!stream_->Writev(span, webtransport::StreamWriteOptions()).ok()) {
        return;
      }
      pending_.clear();
    }
  }

  webtransport::Stream* stream_;
  std::string pending_;
};

// Session visitor installed on the upstream WebTransport session: accepts incoming bidirectional
// streams (opened by the proxy's bridge) and wires an echo visitor onto each.
class EchoSessionVisitor : public webtransport::SessionVisitor {
public:
  explicit EchoSessionVisitor(webtransport::Session* session) : session_(session) {}

  void OnIncomingBidirectionalStreamAvailable() override {
    while (webtransport::Stream* stream = session_->AcceptIncomingBidirectionalStream()) {
      auto visitor = std::make_unique<EchoStreamVisitor>(stream);
      EchoStreamVisitor* raw = visitor.get();
      stream->SetVisitor(std::move(visitor));
      // Drain anything that arrived before the visitor was installed.
      raw->OnCanRead();
    }
  }
  void OnSessionReady() override {}
  void OnSessionClosed(webtransport::SessionErrorCode /*error_code*/,
                       const std::string& /*error_message*/) override {}
  void OnIncomingUnidirectionalStreamAvailable() override {}
  void OnDatagramReceived(absl::string_view datagram) override {
    session_->SendOrQueueDatagram(datagram);
  }
  void OnCanCreateNewOutgoingBidirectionalStream() override {}
  void OnCanCreateNewOutgoingUnidirectionalStream() override {}

private:
  webtransport::Session* session_;
};

// Stream visitor installed on the downstream client's opened stream: captures the echoed bytes.
class CaptureStreamVisitor : public webtransport::StreamVisitor {
public:
  explicit CaptureStreamVisitor(webtransport::Stream* stream) : stream_(stream) {}

  void OnCanRead() override {
    std::string data;
    webtransport::Stream::ReadResult result = stream_->Read(&data);
    (void)result;
    received_.append(data);
  }
  void OnCanWrite() override {}
  void OnResetStreamReceived(webtransport::StreamErrorCode /*error*/) override {}
  void OnStopSendingReceived(webtransport::StreamErrorCode /*error*/) override {}
  void OnWriteSideInDataRecvdState() override {}

  const std::string& received() const { return received_; }

private:
  webtransport::Stream* stream_;
  std::string received_;
};

// Session visitor installed on the downstream client's session: captures echoed datagrams.
class CaptureSessionVisitor : public webtransport::SessionVisitor {
public:
  void OnDatagramReceived(absl::string_view datagram) override {
    datagrams_.emplace_back(datagram);
  }
  void OnSessionReady() override {}
  void OnSessionClosed(webtransport::SessionErrorCode /*error_code*/,
                       const std::string& /*error_message*/) override {}
  void OnIncomingBidirectionalStreamAvailable() override {}
  void OnIncomingUnidirectionalStreamAvailable() override {}
  void OnCanCreateNewOutgoingBidirectionalStream() override {}
  void OnCanCreateNewOutgoingUnidirectionalStream() override {}

  bool received(absl::string_view datagram) const {
    return std::find(datagrams_.begin(), datagrams_.end(), std::string(datagram)) !=
           datagrams_.end();
  }

private:
  std::vector<std::string> datagrams_;
};

// End-to-end WebTransport bridging: a real HTTP/3 client opens a WebTransport CONNECT that Envoy
// proxies to an HTTP/3 upstream, and the per-session bridge forwards WebTransport streams/datagrams
// between the two QUIC sessions. Pinned to HTTP/3 on both hops (WebTransport is HTTP/3 only).
class WebTransportIntegrationTest : public HttpProtocolIntegrationTest {
public:
  void initialize() override {
    config_helper_.addRuntimeOverride("envoy.reloadable_features.quic_support_web_transport",
                                      "true");
    config_helper_.addConfigModifier(
        [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
               hcm) {
          // A WebTransport extended CONNECT is normalized to the HTTP/1 upgrade form (GET + :path +
          // "Upgrade: webtransport"), so it is matched by a normal path match, NOT a
          // connect_matcher. Forward it (do not terminate) by enabling the "webtransport" upgrade
          // on the route and the connection manager.
          auto* route = hcm.mutable_route_config()->mutable_virtual_hosts(0)->mutable_routes(0);
          route->mutable_match()->set_prefix("/");
          route->mutable_route()->add_upgrade_configs()->set_upgrade_type("webtransport");

          hcm.add_upgrade_configs()->set_upgrade_type("webtransport");
          hcm.mutable_http3_protocol_options()->set_allow_extended_connect(true);
        });
    HttpProtocolIntegrationTest::initialize();
  }

  // WebTransport request in HTTP/1 upgrade form; the H/3 client codec converts it to the extended
  // CONNECT (:method CONNECT, :protocol webtransport) on the wire.
  Http::TestRequestHeaderMapImpl webTransportHeaders() {
    // The authority must match the upstream test cert (*.lyft.com): configureUpstreamTls enables
    // auto_sni, so the upstream TLS SNI is derived from :authority. A non-matching name fails the
    // upstream certificate verification.
    return {{":method", "GET"},        {":path", "/"},       {"upgrade", "webtransport"},
            {"connection", "upgrade"}, {":scheme", "https"}, {":authority", "foo.lyft.com"}};
  }

  // Installs the echo application on the upstream WebTransport session. Runs on the upstream
  // dispatcher thread, where that session's QUICHE callbacks fire. Returns false if the upstream
  // stream has no WebTransport session. upstream_request_ must already have headers.
  bool installUpstreamEcho() {
    absl::Notification done;
    bool ok = false;
    fake_upstreams_[0]->runOnDispatcherThread([&]() {
      OptRef<Http::WebTransportSession> session = upstream_request_->webTransportSession();
      if (session.has_value() && session->rawWebTransportSession() != nullptr) {
        session->setWebTransportVisitor(
            std::make_unique<EchoSessionVisitor>(session->rawWebTransportSession()));
        ok = true;
      }
      done.Notify();
    });
    EXPECT_TRUE(done.WaitForNotificationWithTimeout(absl::Seconds(15)));
    return ok;
  }
};

INSTANTIATE_TEST_SUITE_P(Protocols, WebTransportIntegrationTest,
                         testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParams(
                             {Http::CodecType::HTTP3}, {Http::CodecType::HTTP3})),
                         HttpProtocolIntegrationTest::protocolTestParamsToString);

// A downstream WebTransport CONNECT is forwarded to the upstream, and once both sides answer, the
// bridge has a live WebTransport session on each end.
TEST_P(WebTransportIntegrationTest, BridgesWebTransportSessions) {
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto encoder_decoder = codec_client_->startRequest(webTransportHeaders());
  request_encoder_ = &encoder_decoder.first;
  auto response = std::move(encoder_decoder.second);

  // The CONNECT is forwarded upstream as an extended CONNECT.
  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForHeadersComplete());
  EXPECT_EQ(upstream_request_->headers().getUpgradeValue(), "webtransport");

  // The upstream accepts the WebTransport session and installs the echo application on it.
  ASSERT_TRUE(installUpstreamEcho());

  // Respond 2xx so the bridge is installed at the downstream client side too.
  upstream_request_->encodeHeaders(default_response_headers_, false);
  response->waitForHeaders();

  // The downstream client now has a live WebTransport session.
  OptRef<Http::WebTransportSession> client_session =
      request_encoder_->getStream().webTransportSession();
  ASSERT_TRUE(client_session.has_value());
  ASSERT_NE(client_session->rawWebTransportSession(), nullptr);

  cleanupUpstreamAndDownstream();
}

// Opens a bidirectional WebTransport stream on the downstream client, writes data, and verifies the
// bytes are forwarded through Envoy's bridge to the upstream echo application and back.
TEST_P(WebTransportIntegrationTest, EchoesBidirectionalStreamThroughBridge) {
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto encoder_decoder = codec_client_->startRequest(webTransportHeaders());
  request_encoder_ = &encoder_decoder.first;
  auto response = std::move(encoder_decoder.second);

  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForHeadersComplete());

  ASSERT_TRUE(installUpstreamEcho());

  upstream_request_->encodeHeaders(default_response_headers_, false);
  response->waitForHeaders();

  // Open a bidirectional stream on the client session and write a payload.
  webtransport::Session* client_session =
      request_encoder_->getStream().webTransportSession()->rawWebTransportSession();
  ASSERT_NE(client_session, nullptr);
  webtransport::Stream* client_stream = client_session->OpenOutgoingBidirectionalStream();
  ASSERT_NE(client_stream, nullptr);
  client_stream->SetVisitor(std::make_unique<CaptureStreamVisitor>(client_stream));
  auto* capture_ptr = static_cast<CaptureStreamVisitor*>(client_stream->visitor());

  const std::string payload = "hello webtransport";
  quiche::QuicheMemSlice slice(
      quiche::QuicheBuffer::Copy(quiche::SimpleBufferAllocator::Get(), payload));
  absl::Span<quiche::QuicheMemSlice> span(&slice, 1);
  ASSERT_TRUE(client_stream->Writev(span, webtransport::StreamWriteOptions()).ok());

  // Pump the client event loop until the echo arrives (the bytes traverse client -> Envoy bridge ->
  // upstream echo -> back).
  Event::TestTimeSystem& time_system = timeSystem();
  bool echoed = false;
  for (int i = 0; i < 200 && !echoed; i++) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
    echoed = capture_ptr->received() == payload;
    if (!echoed) {
      time_system.advanceTimeWait(std::chrono::milliseconds(10));
    }
  }
  EXPECT_TRUE(echoed) << "received: '" << capture_ptr->received() << "'";

  cleanupUpstreamAndDownstream();
}

// Sends a WebTransport datagram from the downstream client and verifies it is forwarded through
// Envoy's bridge to the upstream echo application and back.
TEST_P(WebTransportIntegrationTest, EchoesDatagramThroughBridge) {
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto encoder_decoder = codec_client_->startRequest(webTransportHeaders());
  request_encoder_ = &encoder_decoder.first;
  auto response = std::move(encoder_decoder.second);

  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForHeadersComplete());

  ASSERT_TRUE(installUpstreamEcho());

  upstream_request_->encodeHeaders(default_response_headers_, false);
  response->waitForHeaders();

  // Install a session visitor on the client session to capture echoed datagrams.
  OptRef<Http::WebTransportSession> client = request_encoder_->getStream().webTransportSession();
  ASSERT_TRUE(client.has_value());
  ASSERT_NE(client->rawWebTransportSession(), nullptr);
  auto capture = std::make_unique<CaptureSessionVisitor>();
  auto* capture_ptr = capture.get();
  client->setWebTransportVisitor(std::move(capture));
  webtransport::Session* client_session = client->rawWebTransportSession();

  // Datagrams are unreliable, so resend each round until the echo is observed (or we give up).
  // Early datagrams may be dropped before the session/bridge is fully ready.
  const std::string payload = "hello datagram";
  bool echoed = false;
  for (int i = 0; i < 200 && !echoed; i++) {
    client_session->SendOrQueueDatagram(payload);
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
    echoed = capture_ptr->received(payload);
    if (!echoed) {
      timeSystem().advanceTimeWait(std::chrono::milliseconds(10));
    }
  }
  EXPECT_TRUE(echoed);

  cleanupUpstreamAndDownstream();
}

} // namespace
} // namespace Envoy
