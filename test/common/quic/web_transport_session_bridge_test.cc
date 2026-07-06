#include <string>

#include "source/common/quic/web_transport_session_bridge.h"

#include "test/mocks/http/webtransport.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Quic {
namespace {

using ::testing::_;
using ::testing::A;
using ::testing::NiceMock;
using ::testing::Return;

// The WebTransport mocks are shared via test/mocks/http so other tests can reuse them.
using Http::MockRawWebTransportSession;
using Http::MockWebTransportSession;
using Http::MockWebTransportStream;

// Bundles the two mock streams and the bridge under test. A plain struct (not a gtest fixture) so
// the tests stay simple TEST() cases. `local`/`peer` mirror the bridge's own naming: the
// LocalVisitor forwards local->peer and the PeerVisitor forwards peer->local.
struct StreamBridgeHarness {
  StreamBridgeHarness()
      : bridge(std::make_shared<WebTransportStreamBridge>(&local_stream, &peer_stream)) {
    // By default both write sides can accept data; backpressure tests override this.
    ON_CALL(local_stream, CanWrite()).WillByDefault(Return(true));
    ON_CALL(peer_stream, CanWrite()).WillByDefault(Return(true));
    // By default a stream has no more data to read after the test's seeded read.
    ON_CALL(local_stream, Read(testing::A<absl::Span<char>>()))
        .WillByDefault(Return(webtransport::Stream::ReadResult{0, false}));
    ON_CALL(peer_stream, Read(testing::A<absl::Span<char>>()))
        .WillByDefault(Return(webtransport::Stream::ReadResult{0, false}));
  }
  testing::NiceMock<MockWebTransportStream> local_stream;
  testing::NiceMock<MockWebTransportStream> peer_stream;
  WebTransportStreamBridgeSharedPtr bridge;
};

// Returns a lambda usable as a Read() action that copies `data` into the caller's buffer and
// reports the given fin bit.
auto readReturning(std::string data, bool fin) {
  return
      [data = std::move(data), fin](absl::Span<char> buffer) -> webtransport::Stream::ReadResult {
        std::copy(data.begin(), data.end(), buffer.data());
        return {data.size(), fin};
      };
}

TEST(WebTransportStreamBridgeTest, LocalVisitorForwardsDataToPeer) {
  StreamBridgeHarness h;
  auto visitor = h.bridge->makeLocalVisitor();

  // Local returns some data on the first read, then nothing (drains the loop).
  EXPECT_CALL(h.local_stream, Read(testing::A<absl::Span<char>>()))
      .WillOnce(readReturning("hello", false))
      .WillRepeatedly(Return(webtransport::Stream::ReadResult{0, false}));
  EXPECT_CALL(h.peer_stream, Writev(_, _)).WillOnce(Return(absl::OkStatus()));

  visitor->OnCanRead();
}

TEST(WebTransportStreamBridgeTest, PeerVisitorForwardsDataToLocal) {
  StreamBridgeHarness h;
  auto visitor = h.bridge->makePeerVisitor();

  EXPECT_CALL(h.peer_stream, Read(testing::A<absl::Span<char>>()))
      .WillOnce(readReturning("world", false))
      .WillRepeatedly(Return(webtransport::Stream::ReadResult{0, false}));
  EXPECT_CALL(h.local_stream, Writev(_, _)).WillOnce(Return(absl::OkStatus()));

  visitor->OnCanRead();
}

// Multiple readable chunks in one OnCanRead are each forwarded until the source drains.
TEST(WebTransportStreamBridgeTest, ForwardsMultipleChunksUntilDrained) {
  StreamBridgeHarness h;
  auto visitor = h.bridge->makeLocalVisitor();

  EXPECT_CALL(h.local_stream, Read(testing::A<absl::Span<char>>()))
      .WillOnce(readReturning("chunk1", false))
      .WillOnce(readReturning("chunk2", false))
      .WillRepeatedly(Return(webtransport::Stream::ReadResult{0, false}));
  EXPECT_CALL(h.peer_stream, Writev(_, _)).Times(2).WillRepeatedly(Return(absl::OkStatus()));

  visitor->OnCanRead();
}

TEST(WebTransportStreamBridgeTest, LocalResetForwardedToPeer) {
  StreamBridgeHarness h;
  auto visitor = h.bridge->makeLocalVisitor();

  EXPECT_CALL(h.peer_stream, ResetWithUserCode(42));
  visitor->OnResetStreamReceived(42);
}

TEST(WebTransportStreamBridgeTest, PeerResetForwardedToLocal) {
  StreamBridgeHarness h;
  auto visitor = h.bridge->makePeerVisitor();

  EXPECT_CALL(h.local_stream, ResetWithUserCode(99));
  visitor->OnResetStreamReceived(99);
}

TEST(WebTransportStreamBridgeTest, LocalStopSendingForwardedToPeer) {
  StreamBridgeHarness h;
  auto visitor = h.bridge->makeLocalVisitor();

  EXPECT_CALL(h.peer_stream, SendStopSending(7));
  visitor->OnStopSendingReceived(7);
}

TEST(WebTransportStreamBridgeTest, PeerStopSendingForwardedToLocal) {
  StreamBridgeHarness h;
  auto visitor = h.bridge->makePeerVisitor();

  EXPECT_CALL(h.local_stream, SendStopSending(8));
  visitor->OnStopSendingReceived(8);
}

TEST(WebTransportStreamBridgeTest, FinWithDataForwardedOnRead) {
  StreamBridgeHarness h;
  auto visitor = h.bridge->makeLocalVisitor();

  // Local returns FIN together with data; a single write carries both.
  EXPECT_CALL(h.local_stream, Read(testing::A<absl::Span<char>>()))
      .WillOnce(readReturning("bye", true));
  EXPECT_CALL(h.peer_stream, Writev(_, _))
      .WillOnce(
          [](absl::Span<quiche::QuicheMemSlice>, const webtransport::StreamWriteOptions& options) {
            EXPECT_TRUE(options.send_fin());
            return absl::OkStatus();
          });

  visitor->OnCanRead();
}

// A FIN with no data still propagates as an empty, fin-bearing write so the destination half-close
// is forwarded.
TEST(WebTransportStreamBridgeTest, EmptyFinForwardedOnRead) {
  StreamBridgeHarness h;
  auto visitor = h.bridge->makeLocalVisitor();

  EXPECT_CALL(h.local_stream, Read(testing::A<absl::Span<char>>()))
      .WillOnce(Return(webtransport::Stream::ReadResult{0, true}));
  EXPECT_CALL(h.peer_stream, Writev(_, _))
      .WillOnce([](absl::Span<quiche::QuicheMemSlice> data,
                   const webtransport::StreamWriteOptions& options) {
        EXPECT_TRUE(data.empty());
        EXPECT_TRUE(options.send_fin());
        return absl::OkStatus();
      });

  visitor->OnCanRead();
}

// A write failure that is not flow-control backpressure (CanWrite() was true) is terminal for the
// direction: the bridge stops rather than spinning, so the source is read only once.
TEST(WebTransportStreamBridgeTest, WriteFailureIsTerminalAndDoesNotSpin) {
  StreamBridgeHarness h;
  auto visitor = h.bridge->makeLocalVisitor();

  EXPECT_CALL(h.local_stream, Read(testing::A<absl::Span<char>>()))
      .WillOnce(readReturning("data", false));
  EXPECT_CALL(h.peer_stream, Writev(_, _)).WillOnce(Return(absl::InternalError("write failed")));

  visitor->OnCanRead();
}

// OnCanWrite is a no-op when the opposite direction was not paused: nothing is read or written.
TEST(WebTransportStreamBridgeTest, OnCanWriteIsNoOpWhenNotBlocked) {
  StreamBridgeHarness h;
  auto visitor = h.bridge->makeLocalVisitor();

  EXPECT_CALL(h.peer_stream, Read(testing::A<absl::Span<char>>())).Times(0);
  EXPECT_CALL(h.local_stream, Writev(_, _)).Times(0);

  visitor->OnCanWrite();
}

// A blocked destination must not consume data from the source (no data loss): the bytes stay in
// the source stream until the destination can accept a write.
TEST(WebTransportStreamBridgeTest, BlockedDestinationDoesNotConsumeSource) {
  StreamBridgeHarness h;
  auto visitor = h.bridge->makeLocalVisitor();

  // Peer (the destination for local->peer) cannot accept writes.
  EXPECT_CALL(h.peer_stream, CanWrite()).WillRepeatedly(Return(false));
  EXPECT_CALL(h.local_stream, Read(testing::A<absl::Span<char>>())).Times(0);
  EXPECT_CALL(h.peer_stream, Writev(_, _)).Times(0);

  visitor->OnCanRead();
}

// When the destination unblocks, the paused direction resumes and forwards the pending data.
TEST(WebTransportStreamBridgeTest, ResumesForwardingWhenDestinationUnblocks) {
  StreamBridgeHarness h;
  auto local_visitor = h.bridge->makeLocalVisitor();
  auto peer_visitor = h.bridge->makePeerVisitor();

  bool peer_writable = false;
  EXPECT_CALL(h.peer_stream, CanWrite()).WillRepeatedly([&] { return peer_writable; });

  // The data is only read once the write side is available, so seed it up front; it must not be
  // consumed while the peer is blocked.
  EXPECT_CALL(h.local_stream, Read(testing::A<absl::Span<char>>()))
      .WillOnce(readReturning("resumed", false))
      .WillRepeatedly(Return(webtransport::Stream::ReadResult{0, false}));
  EXPECT_CALL(h.peer_stream, Writev(_, _)).WillOnce(Return(absl::OkStatus()));

  // Peer blocked: OnCanRead records the block and reads nothing.
  local_visitor->OnCanRead();

  // Peer becomes writable; its OnCanWrite resumes the local->peer direction.
  peer_writable = true;
  peer_visitor->OnCanWrite();
}

// Symmetric to the above: when the local write side unblocks, LocalVisitor::OnCanWrite resumes the
// paused peer->local direction.
TEST(WebTransportStreamBridgeTest, LocalOnCanWriteResumesPeerDirection) {
  StreamBridgeHarness h;
  auto local_visitor = h.bridge->makeLocalVisitor();
  auto peer_visitor = h.bridge->makePeerVisitor();

  bool local_writable = false;
  EXPECT_CALL(h.local_stream, CanWrite()).WillRepeatedly([&] { return local_writable; });

  EXPECT_CALL(h.peer_stream, Read(A<absl::Span<char>>()))
      .WillOnce(readReturning("resumed", false))
      .WillRepeatedly(Return(webtransport::Stream::ReadResult{0, false}));
  EXPECT_CALL(h.local_stream, Writev(_, _)).WillOnce(Return(absl::OkStatus()));

  // Local blocked: peer's OnCanRead records the block and reads nothing.
  peer_visitor->OnCanRead();

  // Local becomes writable; its OnCanWrite resumes the peer->local direction.
  local_writable = true;
  local_visitor->OnCanWrite();
}

// start() pumps both directions once so data already buffered before the visitors were installed is
// not stranded (QUICHE does not replay OnCanRead for it).
TEST(WebTransportStreamBridgeTest, StartPumpsBothDirections) {
  StreamBridgeHarness h;
  // Visitors must exist (they are normally installed before start()), though start() reads the
  // bridge's own stream pointers directly.
  auto local_visitor = h.bridge->makeLocalVisitor();
  auto peer_visitor = h.bridge->makePeerVisitor();

  EXPECT_CALL(h.local_stream, Read(A<absl::Span<char>>()))
      .WillOnce(readReturning("pre-buffered-local", false))
      .WillRepeatedly(Return(webtransport::Stream::ReadResult{0, false}));
  EXPECT_CALL(h.peer_stream, Read(A<absl::Span<char>>()))
      .WillOnce(readReturning("pre-buffered-peer", false))
      .WillRepeatedly(Return(webtransport::Stream::ReadResult{0, false}));
  EXPECT_CALL(h.peer_stream, Writev(_, _)).WillOnce(Return(absl::OkStatus()));
  EXPECT_CALL(h.local_stream, Writev(_, _)).WillOnce(Return(absl::OkStatus()));

  h.bridge->start();
}

// start() records a per-direction block when a destination cannot accept the pre-buffered data.
TEST(WebTransportStreamBridgeTest, StartRecordsBlockWhenDestinationFull) {
  StreamBridgeHarness h;
  auto local_visitor = h.bridge->makeLocalVisitor();
  auto peer_visitor = h.bridge->makePeerVisitor();

  // Neither destination can accept writes, so start() reads nothing and marks both directions
  // blocked. No Read/Writev happens (a blocked destination must not consume the source).
  EXPECT_CALL(h.local_stream, CanWrite()).WillRepeatedly(Return(false));
  EXPECT_CALL(h.peer_stream, CanWrite()).WillRepeatedly(Return(false));
  EXPECT_CALL(h.local_stream, Read(A<absl::Span<char>>())).Times(0);
  EXPECT_CALL(h.peer_stream, Read(A<absl::Span<char>>())).Times(0);

  h.bridge->start();
}

// start() is a no-op once a stream has gone away (its visitor was destroyed, nulling the pointer).
TEST(WebTransportStreamBridgeTest, StartNoOpWhenStreamMissing) {
  StreamBridgeHarness h;
  {
    // Destroying the local visitor nulls the bridge's local_ pointer.
    auto local_visitor = h.bridge->makeLocalVisitor();
  }
  EXPECT_CALL(h.local_stream, Read(A<absl::Span<char>>())).Times(0);
  EXPECT_CALL(h.peer_stream, Read(A<absl::Span<char>>())).Times(0);

  h.bridge->start();
}

// After the peer stream is gone (its visitor destroyed), the surviving local visitor's callbacks
// must short-circuit instead of dereferencing the freed peer.
TEST(WebTransportStreamBridgeTest, LocalVisitorShortCircuitsAfterPeerGone) {
  StreamBridgeHarness h;
  auto local_visitor = h.bridge->makeLocalVisitor();
  { auto peer_visitor = h.bridge->makePeerVisitor(); } // Destroyed here; nulls bridge->peer_.

  EXPECT_CALL(h.local_stream, Read(A<absl::Span<char>>())).Times(0);
  EXPECT_CALL(h.peer_stream, ResetWithUserCode(_)).Times(0);
  EXPECT_CALL(h.peer_stream, SendStopSending(_)).Times(0);

  local_visitor->OnCanRead();
  local_visitor->OnCanWrite();
  // Reset/stop-sending forwarding must not touch the gone peer.
  local_visitor->OnResetStreamReceived(1);
  local_visitor->OnStopSendingReceived(1);
}

// Symmetric: after the local stream is gone, the surviving peer visitor's callbacks short-circuit.
TEST(WebTransportStreamBridgeTest, PeerVisitorShortCircuitsAfterLocalGone) {
  StreamBridgeHarness h;
  auto peer_visitor = h.bridge->makePeerVisitor();
  { auto local_visitor = h.bridge->makeLocalVisitor(); } // Destroyed here; nulls bridge->local_.

  EXPECT_CALL(h.peer_stream, Read(A<absl::Span<char>>())).Times(0);
  EXPECT_CALL(h.local_stream, ResetWithUserCode(_)).Times(0);
  EXPECT_CALL(h.local_stream, SendStopSending(_)).Times(0);

  peer_visitor->OnCanRead();
  peer_visitor->OnCanWrite();
  peer_visitor->OnResetStreamReceived(1);
  peer_visitor->OnStopSendingReceived(1);
}

// --- WebTransportSessionState ---

TEST(WebTransportSessionStateTest, StoresAndRetrievesSessions) {
  WebTransportSessionState state;

  EXPECT_EQ(state.upstreamSession(), nullptr);
  EXPECT_EQ(state.downstreamSession(), nullptr);

  // Use sentinel pointers for testing (never dereferenced).
  auto* fake_upstream = reinterpret_cast<webtransport::Session*>(0x1234);
  auto* fake_downstream = reinterpret_cast<webtransport::Session*>(0x5678);

  state.setUpstreamSession(fake_upstream);
  state.setDownstreamSession(fake_downstream);

  EXPECT_EQ(state.upstreamSession(), fake_upstream);
  EXPECT_EQ(state.downstreamSession(), fake_downstream);
}

TEST(WebTransportSessionStateTest, LivenessFollowsSessionPointers) {
  WebTransportSessionState state;

  // Liveness is derived from whether the session pointer is set.
  EXPECT_FALSE(state.upstreamAlive());
  EXPECT_FALSE(state.downstreamAlive());

  auto* fake_upstream = reinterpret_cast<webtransport::Session*>(0x1234);
  auto* fake_downstream = reinterpret_cast<webtransport::Session*>(0x5678);
  state.setUpstreamSession(fake_upstream);
  state.setDownstreamSession(fake_downstream);
  EXPECT_TRUE(state.upstreamAlive());
  EXPECT_TRUE(state.downstreamAlive());

  // Clearing a session pointer (done by the bridge on that side when its session is torn down)
  // marks that side dead.
  state.setUpstreamSession(nullptr);
  EXPECT_FALSE(state.upstreamAlive());
  EXPECT_TRUE(state.downstreamAlive());
}

// --- WebTransportSessionBridge ---
//
// Since installBridge() now takes the Http::WebTransportSession interface, the session-level
// forwarding happy path is unit-testable with mocks (see InstallBridgeWiresBothSessionsAndForwards
// below): the Envoy-level Http::MockWebTransportSession captures the installed visitor and returns
// a MockRawWebTransportSession from rawWebTransportSession() for the forwarding side. The tests
// that drive the bridge state directly (selection, closed/dead-peer guards) use sentinel session
// pointers, which are never dereferenced because every path under test short-circuits.

// Sentinel session pointers; never dereferenced because every path under test short-circuits.
webtransport::Session* fakeDownstream() { return reinterpret_cast<webtransport::Session*>(0x1234); }
webtransport::Session* fakeUpstream() { return reinterpret_cast<webtransport::Session*>(0x5678); }

TEST(WebTransportSessionBridgeTest, LocalAndPeerSelectionDependsOnSide) {
  auto state = std::make_shared<WebTransportSessionState>();
  state->setDownstreamSession(fakeDownstream());
  state->setUpstreamSession(fakeUpstream());

  // From the downstream side, "local" is the downstream session and "peer" is the upstream session.
  WebTransportSessionBridge downstream_bridge(state, /*is_downstream=*/true);
  EXPECT_EQ(downstream_bridge.localSession(), fakeDownstream());
  EXPECT_EQ(downstream_bridge.peerSession(), fakeUpstream());

  // From the upstream side, the roles are flipped.
  WebTransportSessionBridge upstream_bridge(state, /*is_downstream=*/false);
  EXPECT_EQ(upstream_bridge.localSession(), fakeUpstream());
  EXPECT_EQ(upstream_bridge.peerSession(), fakeDownstream());
}

// Destroying a bridge (which happens when its WebTransportHttp3 session is torn down, since the
// session owns the visitor) clears only that side's session pointer in the shared state, so the
// surviving peer bridge stops forwarding to the freed session.
TEST(WebTransportSessionBridgeTest, DestructorClearsOwnSide) {
  auto state = std::make_shared<WebTransportSessionState>();
  state->setDownstreamSession(fakeDownstream());
  state->setUpstreamSession(fakeUpstream());

  {
    WebTransportSessionBridge downstream_bridge(state, /*is_downstream=*/true);
    EXPECT_TRUE(state->downstreamAlive());
  }
  // The downstream bridge's destructor cleared the downstream side; the upstream side is untouched.
  EXPECT_FALSE(state->downstreamAlive());
  EXPECT_TRUE(state->upstreamAlive());

  {
    WebTransportSessionBridge upstream_bridge(state, /*is_downstream=*/false);
    EXPECT_TRUE(state->upstreamAlive());
  }
  EXPECT_FALSE(state->upstreamAlive());
}

// OnSessionReady is a no-op log. OnSessionClosed clears this bridge's own side (so the peer stops
// forwarding into the closed session) but leaves the peer side intact, and is idempotent.
TEST(WebTransportSessionBridgeTest, ReadyAndClosedCallbacksAreSafe) {
  auto state = std::make_shared<WebTransportSessionState>();
  state->setDownstreamSession(fakeDownstream());
  state->setUpstreamSession(fakeUpstream());
  WebTransportSessionBridge bridge(state, /*is_downstream=*/true);

  bridge.OnSessionReady();
  EXPECT_TRUE(state->downstreamAlive());

  // Closing the downstream session clears the downstream side, not the upstream side.
  bridge.OnSessionClosed(0, "");
  EXPECT_FALSE(state->downstreamAlive());
  EXPECT_TRUE(state->upstreamAlive());

  // Idempotent: a second close is a no-op.
  bridge.OnSessionClosed(1, "again");
  EXPECT_FALSE(state->downstreamAlive());
  EXPECT_TRUE(state->upstreamAlive());
}

// After OnSessionClosed, incoming-stream and datagram callbacks must short-circuit instead of
// accepting/dereferencing the (sentinel) sessions: the close clears this side and sets the closed_
// guard, both of which gate the handlers.
TEST(WebTransportSessionBridgeTest, IncomingStreamsIgnoredAfterClose) {
  auto state = std::make_shared<WebTransportSessionState>();
  state->setDownstreamSession(fakeDownstream());
  state->setUpstreamSession(fakeUpstream());
  WebTransportSessionBridge bridge(state, /*is_downstream=*/true);

  bridge.OnSessionClosed(0, "");

  // Would dereference the sentinel sessions if the closed_ guard were missing.
  bridge.OnIncomingBidirectionalStreamAvailable();
  bridge.OnIncomingUnidirectionalStreamAvailable();
  bridge.OnDatagramReceived("payload");
}

// When one side has gone away (its session pointer was cleared on stream close), incoming-stream
// and datagram callbacks must short-circuit instead of dereferencing the dead session.
TEST(WebTransportSessionBridgeTest, IncomingStreamsIgnoredWhenPeerIsDead) {
  auto state = std::make_shared<WebTransportSessionState>();
  // Downstream is live, upstream has closed.
  state->setDownstreamSession(fakeDownstream());
  state->setUpstreamSession(nullptr);
  WebTransportSessionBridge bridge(state, /*is_downstream=*/true);

  bridge.OnIncomingBidirectionalStreamAvailable();
  bridge.OnIncomingUnidirectionalStreamAvailable();
  bridge.OnDatagramReceived("payload");
}

// installBridge is a no-op when either side has no underlying webtransport::Session
// (rawWebTransportSession() returns null, e.g. the session has already gone away): no visitor is
// installed on either side.
TEST(WebTransportSessionBridgeTest, InstallBridgeNoOpWhenASideIsMissing) {
  NiceMock<MockRawWebTransportSession> raw;

  // Upstream missing.
  {
    NiceMock<Http::MockWebTransportSession> downstream;
    NiceMock<Http::MockWebTransportSession> upstream;
    ON_CALL(downstream, rawWebTransportSession()).WillByDefault(Return(&raw));
    ON_CALL(upstream, rawWebTransportSession()).WillByDefault(Return(nullptr));
    EXPECT_CALL(downstream, setWebTransportVisitor(_)).Times(0);
    EXPECT_CALL(upstream, setWebTransportVisitor(_)).Times(0);
    WebTransportSessionBridge::installBridge(downstream, upstream);
  }

  // Downstream missing.
  {
    NiceMock<Http::MockWebTransportSession> downstream;
    NiceMock<Http::MockWebTransportSession> upstream;
    ON_CALL(downstream, rawWebTransportSession()).WillByDefault(Return(nullptr));
    ON_CALL(upstream, rawWebTransportSession()).WillByDefault(Return(&raw));
    EXPECT_CALL(downstream, setWebTransportVisitor(_)).Times(0);
    EXPECT_CALL(upstream, setWebTransportVisitor(_)).Times(0);
    WebTransportSessionBridge::installBridge(downstream, upstream);
  }
}

// installBridge wires both sessions: it reads each side's raw webtransport::Session and installs a
// SessionVisitor (the per-side WebTransportSessionBridge) on each via setWebTransportVisitor. The
// downstream visitor it installs, when driven with an incoming datagram, forwards it to the
// *upstream* raw session -- proving the two bridges share state and forward across sides.
TEST(WebTransportSessionBridgeTest, InstallBridgeWiresBothSessionsAndForwards) {
  NiceMock<Http::MockWebTransportSession> downstream;
  NiceMock<Http::MockWebTransportSession> upstream;
  NiceMock<MockRawWebTransportSession> downstream_raw;
  NiceMock<MockRawWebTransportSession> upstream_raw;
  ON_CALL(downstream, rawWebTransportSession()).WillByDefault(Return(&downstream_raw));
  ON_CALL(upstream, rawWebTransportSession()).WillByDefault(Return(&upstream_raw));

  // Capture the visitor installed on each side so the forwarding can be driven directly.
  std::unique_ptr<webtransport::SessionVisitor> downstream_visitor;
  std::unique_ptr<webtransport::SessionVisitor> upstream_visitor;
  EXPECT_CALL(downstream, setWebTransportVisitor(_))
      .WillOnce([&](std::unique_ptr<webtransport::SessionVisitor> v) {
        downstream_visitor = std::move(v);
      });
  EXPECT_CALL(upstream, setWebTransportVisitor(_))
      .WillOnce([&](std::unique_ptr<webtransport::SessionVisitor> v) {
        upstream_visitor = std::move(v);
      });

  WebTransportSessionBridge::installBridge(downstream, upstream);
  ASSERT_NE(downstream_visitor, nullptr);
  ASSERT_NE(upstream_visitor, nullptr);

  // A datagram received on the downstream session is forwarded to the upstream's raw session.
  EXPECT_CALL(upstream_raw, SendOrQueueDatagram(absl::string_view("payload")))
      .WillOnce(
          Return(webtransport::DatagramStatus{webtransport::DatagramStatusCode::kSuccess, ""}));
  downstream_visitor->OnDatagramReceived("payload");
}

// Builds a downstream-side session bridge wired to two mock sessions. Forwarding tests drive the
// incoming-stream/datagram callbacks and assert the call reaches the peer session.
struct SessionBridgeHarness {
  SessionBridgeHarness()
      : state(std::make_shared<WebTransportSessionState>()), bridge(state, /*is_downstream=*/true) {
    state->setDownstreamSession(&local_session);
    state->setUpstreamSession(&peer_session);
  }
  // Configures a freshly-accepted/opened stream pair so bridgeIncomingStream's start() pump drains
  // cleanly (no data, write side open).
  void primeStreamPair(MockWebTransportStream& local, MockWebTransportStream& peer) {
    ON_CALL(local, CanWrite()).WillByDefault(Return(true));
    ON_CALL(peer, CanWrite()).WillByDefault(Return(true));
    ON_CALL(local, Read(A<absl::Span<char>>()))
        .WillByDefault(Return(webtransport::Stream::ReadResult{0, false}));
    ON_CALL(peer, Read(A<absl::Span<char>>()))
        .WillByDefault(Return(webtransport::Stream::ReadResult{0, false}));
  }
  NiceMock<MockRawWebTransportSession> local_session;
  NiceMock<MockRawWebTransportSession> peer_session;
  WebTransportSessionStateSharedPtr state;
  WebTransportSessionBridge bridge;
};

// An incoming bidirectional stream on the local session is accepted, a matching stream is opened on
// the peer session, and a stream bridge is wired between them (a visitor installed on each).
TEST(WebTransportSessionBridgeTest, ForwardsIncomingBidirectionalStream) {
  SessionBridgeHarness h;
  NiceMock<MockWebTransportStream> local_stream;
  NiceMock<MockWebTransportStream> peer_stream;
  h.primeStreamPair(local_stream, peer_stream);

  // One stream available, then the queue drains (nullptr ends the accept loop).
  EXPECT_CALL(h.local_session, AcceptIncomingBidirectionalStream())
      .WillOnce(Return(&local_stream))
      .WillOnce(Return(nullptr));
  EXPECT_CALL(h.peer_session, OpenOutgoingBidirectionalStream()).WillOnce(Return(&peer_stream));
  EXPECT_CALL(local_stream, SetVisitor(_));
  EXPECT_CALL(peer_stream, SetVisitor(_));

  h.bridge.OnIncomingBidirectionalStreamAvailable();
}

TEST(WebTransportSessionBridgeTest, ForwardsIncomingUnidirectionalStream) {
  SessionBridgeHarness h;
  NiceMock<MockWebTransportStream> local_stream;
  NiceMock<MockWebTransportStream> peer_stream;
  h.primeStreamPair(local_stream, peer_stream);

  EXPECT_CALL(h.local_session, AcceptIncomingUnidirectionalStream())
      .WillOnce(Return(&local_stream))
      .WillOnce(Return(nullptr));
  EXPECT_CALL(h.peer_session, OpenOutgoingUnidirectionalStream()).WillOnce(Return(&peer_stream));
  EXPECT_CALL(local_stream, SetVisitor(_));
  EXPECT_CALL(peer_stream, SetVisitor(_));

  h.bridge.OnIncomingUnidirectionalStreamAvailable();
}

// If the peer session cannot open a matching stream, the accepted local stream is reset (and no
// bridge/visitor is wired).
TEST(WebTransportSessionBridgeTest, ResetsLocalStreamWhenPeerCannotOpen) {
  SessionBridgeHarness h;
  NiceMock<MockWebTransportStream> local_stream;

  EXPECT_CALL(h.local_session, AcceptIncomingBidirectionalStream())
      .WillOnce(Return(&local_stream))
      .WillOnce(Return(nullptr));
  EXPECT_CALL(h.peer_session, OpenOutgoingBidirectionalStream()).WillOnce(Return(nullptr));
  EXPECT_CALL(local_stream, ResetWithUserCode(0));
  EXPECT_CALL(local_stream, SetVisitor(_)).Times(0);

  h.bridge.OnIncomingBidirectionalStreamAvailable();
}

// A datagram received on the local session is forwarded to the peer session.
TEST(WebTransportSessionBridgeTest, ForwardsDatagram) {
  SessionBridgeHarness h;
  EXPECT_CALL(h.peer_session, SendOrQueueDatagram(absl::string_view("payload")))
      .WillOnce(
          Return(webtransport::DatagramStatus{webtransport::DatagramStatusCode::kSuccess, ""}));

  h.bridge.OnDatagramReceived("payload");
}

} // namespace
} // namespace Quic
} // namespace Envoy
