#include <memory>
#include <utility>

#include "envoy/extensions/bootstrap/reverse_tunnel/upstream_socket_interface/v3/upstream_reverse_connection_socket_interface.pb.h"
#include "envoy/extensions/upstreams/http/reverse_tunnel/v3/reverse_tunnel_codec.pb.h"
#include "envoy/http/client_codec_factory.h"
#include "envoy/server/admin.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/assert.h"
#include "source/common/http/http2/codec_impl.h"
#include "source/common/http/utility.h"
#include "source/common/network/socket_interface.h"
#include "source/common/stats/isolated_store_impl.h"
#include "source/extensions/bootstrap/reverse_tunnel/upstream_socket_interface/reverse_tunnel_acceptor.h"
#include "source/extensions/bootstrap/reverse_tunnel/upstream_socket_interface/reverse_tunnel_acceptor_extension.h"
#include "source/extensions/bootstrap/reverse_tunnel/upstream_socket_interface/upstream_socket_manager.h"
#include "source/extensions/upstreams/http/reverse_tunnel/config.h"
#include "source/extensions/upstreams/http/reverse_tunnel/drain_aware_client_connection.h"
#include "source/extensions/upstreams/http/reverse_tunnel/drain_registry.h"
#include "source/extensions/upstreams/http/reverse_tunnel/reverse_tunnel_codec_stats.h"

#include "test/common/http/http2/http2_frame.h"
#include "test/mocks/common.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/reverse_tunnel_reporting_service/reporter.h"
#include "test/mocks/server/admin_stream.h"
#include "test/mocks/server/server_factory_context.h"
#include "test/mocks/thread_local/mocks.h"
#include "test/mocks/upstream/cluster_info.h"
#include "test/test_common/registry.h"
#include "test/test_common/status_utility.h"
#include "test/test_common/utility.h"

#include "absl/strings/match.h"
#include "absl/strings/string_view.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Upstreams {
namespace Http {
namespace ReverseTunnel {
namespace {

using ::Envoy::StatusHelpers::IsOkAndHolds;
using ::testing::_;
using ::testing::Eq;
using ::testing::Invoke;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::ReturnRef;

using Extensions::Bootstrap::ReverseConnection::MOCK_REPORTER;
using Extensions::Bootstrap::ReverseConnection::MockReporterFactory;
using Extensions::Bootstrap::ReverseConnection::MockReverseTunnelReporter;
using Extensions::Bootstrap::ReverseConnection::ReverseTunnelAcceptor;
using Extensions::Bootstrap::ReverseConnection::ReverseTunnelAcceptorExtension;
using Extensions::Bootstrap::ReverseConnection::ReverseTunnelReporter;
using Extensions::Bootstrap::ReverseConnection::ReverseTunnelReporterFactory;
using Extensions::Bootstrap::ReverseConnection::UpstreamSocketManager;
using Extensions::Bootstrap::ReverseConnection::UpstreamSocketThreadLocal;

envoy::extensions::upstreams::http::reverse_tunnel::v3::ReverseTunnelUpstreamCodecOptions
makeProto(bool enable) {
  envoy::extensions::upstreams::http::reverse_tunnel::v3::ReverseTunnelUpstreamCodecOptions proto;
  proto.set_enable_drain_with_goaway(enable);
  return proto;
}

// Same as makeProto, but also sets the connection-metadata key the upstream client codec watches
// for a peer drain signal.
envoy::extensions::upstreams::http::reverse_tunnel::v3::ReverseTunnelUpstreamCodecOptions
makeProtoWithMetadataKey(bool enable, absl::string_view metadata_key) {
  auto proto = makeProto(enable);
  proto.set_metadata_key(std::string(metadata_key));
  return proto;
}

constexpr int kTestSocketFd = 42;

class ReverseTunnelUpstreamCodecTest : public testing::Test {
protected:
  ReverseTunnelUpstreamCodecTest() : dispatcher_("worker_0") {
    tls_.setDispatcher(&dispatcher_);
    EXPECT_CALL(dispatcher_, createTimer_(_))
        .WillRepeatedly(testing::ReturnNew<NiceMock<Event::MockTimer>>());
    EXPECT_CALL(dispatcher_, createFileEvent_(_, _, _, _))
        .WillRepeatedly(testing::ReturnNew<NiceMock<Event::MockFileEvent>>());
  }

  void SetUp() override {
    const Network::SocketInterface* socket_if =
        Network::socketInterface("envoy.bootstrap.reverse_tunnel.upstream_socket_interface");
    RELEASE_ASSERT(socket_if != nullptr, "upstream reverse tunnel socket interface not registered");
  }

  void TearDown() override {
    // Drop the TLS registry first so ~UpstreamSocketManager still has a live extension_.
    socket_manager_ = nullptr;
    thread_local_registry_.reset();
    tls_slot_.reset();
    if (extension_ != nullptr) {
      const Network::SocketInterface* socket_if =
          Network::socketInterface("envoy.bootstrap.reverse_tunnel.upstream_socket_interface");
      if (socket_if != nullptr) {
        auto* acceptor =
            dynamic_cast<ReverseTunnelAcceptor*>(const_cast<Network::SocketInterface*>(socket_if));
        if (acceptor != nullptr && acceptor->extension_ == extension_.get()) {
          acceptor->extension_ = nullptr;
        }
      }
    }
    extension_.reset();
  }

  Envoy::Http::ClientCodecFactory::Context makeContext(Envoy::Http::CodecType type) {
    return Envoy::Http::ClientCodecFactory::Context{
        type, connection_, callbacks_, cluster_, random_, transport_socket_options_};
  }

  NiceMock<MockReverseTunnelReporter>* makeReporter() {
    auto* reporter = new NiceMock<MockReverseTunnelReporter>();
    EXPECT_CALL(reporter_factory_, createReporter()).WillOnce(Invoke([reporter]() {
      return std::unique_ptr<ReverseTunnelReporter>(reporter);
    }));
    return reporter;
  }

  std::unique_ptr<ReverseTunnelAcceptorExtension>
  makeExtension(absl::string_view node_id, absl::string_view cluster_id, int fd) {
    envoy::extensions::bootstrap::reverse_tunnel::upstream_socket_interface::v3::
        UpstreamReverseConnectionSocketInterface config;
    config.set_stat_prefix("reverse_tunnel_upstream_codec_test");
    auto* reporter_cfg = config.mutable_reporter_config();
    reporter_cfg->set_name(MOCK_REPORTER);
    Protobuf::StringValue noop_config;
    std::ignore = reporter_cfg->mutable_typed_config()->PackFrom(noop_config);

    EXPECT_CALL(context_, threadLocal()).WillRepeatedly(ReturnRef(tls_));
    EXPECT_CALL(context_, scope()).WillRepeatedly(ReturnRef(*stats_scope_));
    EXPECT_CALL(context_, messageValidationVisitor())
        .WillRepeatedly(ReturnRef(ProtobufMessage::getStrictValidationVisitor()));

    const Network::SocketInterface* socket_if =
        Network::socketInterface("envoy.bootstrap.reverse_tunnel.upstream_socket_interface");
    RELEASE_ASSERT(socket_if != nullptr, "upstream reverse tunnel socket interface not registered");
    auto* acceptor =
        dynamic_cast<ReverseTunnelAcceptor*>(const_cast<Network::SocketInterface*>(socket_if));
    RELEASE_ASSERT(acceptor != nullptr,
                   "registered socket interface is not a ReverseTunnelAcceptor");

    auto extension = std::make_unique<ReverseTunnelAcceptorExtension>(*acceptor, context_, config);
    acceptor->extension_ = extension.get();
    wireTlsRegistry(*extension);
    socket_manager_->addConnectionSocket(std::string(node_id), std::string(cluster_id),
                                         createSeedSocket(fd), std::chrono::seconds(30),
                                         /*rebalanced=*/false);
    return extension;
  }

  void wireTlsRegistry(ReverseTunnelAcceptorExtension& extension) {
    thread_local_registry_ = std::make_shared<UpstreamSocketThreadLocal>(dispatcher_, &extension);
    socket_manager_ = thread_local_registry_->socketManager();
    RELEASE_ASSERT(socket_manager_ != nullptr, "socket manager not created");

    tls_slot_ = ThreadLocal::TypedSlot<UpstreamSocketThreadLocal>::makeUnique(tls_);
    tls_slot_->set([registry = thread_local_registry_](Event::Dispatcher&)
                       -> std::shared_ptr<UpstreamSocketThreadLocal> { return registry; });
    extension.setTestOnlyTLSRegistry(std::move(tls_slot_));
  }

  Network::ConnectionSocketPtr createSeedSocket(int fd) {
    auto socket = std::make_unique<NiceMock<Network::MockConnectionSocket>>();
    auto io_handle = std::make_unique<NiceMock<Network::MockIoHandle>>();
    auto* io_handle_raw = io_handle.get();
    EXPECT_CALL(*io_handle_raw, fdDoNotUse()).WillRepeatedly(Return(fd));
    EXPECT_CALL(*socket, ioHandle()).WillRepeatedly(ReturnRef(*io_handle_raw));
    socket->io_handle_ = std::move(io_handle);
    return socket;
  }

  NiceMock<Server::Configuration::MockServerFactoryContext> context_;
  // store_ must be declared before stats_ (stats_ is generated from its scope).
  Stats::IsolatedStoreImpl store_;
  Stats::ScopeSharedPtr stats_scope_{store_.createScope("test_scope.")};
  NiceMock<MockReporterFactory> reporter_factory_;
  Registry::InjectFactory<ReverseTunnelReporterFactory> reporter_injector_{reporter_factory_};
  std::unique_ptr<ReverseTunnelAcceptorExtension> extension_;
  std::shared_ptr<UpstreamSocketThreadLocal> thread_local_registry_;
  std::unique_ptr<ThreadLocal::TypedSlot<UpstreamSocketThreadLocal>> tls_slot_;
  UpstreamSocketManager* socket_manager_{nullptr};
  ReverseTunnelUpstreamCodecStats stats_{
      ReverseTunnelUpstreamCodecStats::generate(*store_.rootScope())};
  NiceMock<Network::MockConnection> connection_;
  NiceMock<Network::MockConnectionSocket>* socket_raw_{
      new NiceMock<Network::MockConnectionSocket>()};
  Network::ConnectionSocketPtr socket_{socket_raw_};
  NiceMock<Network::MockIoHandle> io_handle_;
  NiceMock<Envoy::Http::MockConnectionCallbacks> callbacks_;
  NiceMock<Upstream::MockClusterInfo> cluster_;
  NiceMock<Random::MockRandomGenerator> random_;
  std::shared_ptr<const Network::TransportSocketOptions> transport_socket_options_;
  NiceMock<Event::MockDispatcher> dispatcher_;
  NiceMock<ThreadLocal::MockInstance> tls_;
};

// The options object surfaces a per-cluster upstream codec factory via the ProtocolOptionsConfig
// hook (how ClusterInfoImpl discovers it).
TEST_F(ReverseTunnelUpstreamCodecTest, OptionsExposesCodecFactory) {
  Upstream::ProtocolOptionsConfigConstSharedPtr opts =
      std::make_shared<ReverseTunnelUpstreamCodecOptions>(makeProto(true), stats_, nullptr);
  EXPECT_TRUE(opts->upstreamHttpClientCodecFactory().has_value());
}

// A received GOAWAY is observed (logged + counted) and forwarded to the real callbacks so the
// pool's normal drain handling still runs.
TEST_F(ReverseTunnelUpstreamCodecTest, CallbacksObserveAndForwardGoaway) {
  NiceMock<Envoy::Http::MockConnectionCallbacks> inner;
  DrainAwareClientCallbacks wrapper(inner, kTestSocketFd);

  EXPECT_CALL(inner, onGoAway(Envoy::Http::GoAwayErrorCode::NoError));
  wrapper.onGoAway(Envoy::Http::GoAwayErrorCode::NoError);
}

// Peer GOAWAY on the drain-aware callbacks reaches the socket manager and reports to the
// configured reverse-tunnel reporter with the node, cluster, and fd for the seeded socket.
TEST_F(ReverseTunnelUpstreamCodecTest, PeerGoAwayReportsToReporter) {
  NiceMock<MockReverseTunnelReporter>* reporter = makeReporter();

  const std::string node_id = "node-1";
  const std::string cluster_id = "cluster-1";
  extension_ = makeExtension(node_id, cluster_id, kTestSocketFd);

  NiceMock<Envoy::Http::MockConnectionCallbacks> inner;
  DrainAwareClientCallbacks wrapper(inner, kTestSocketFd);

  EXPECT_CALL(*reporter, reportGoAwayEvent(Eq(node_id), Eq(cluster_id), Eq(kTestSocketFd)));
  EXPECT_CALL(inner, onGoAway(Envoy::Http::GoAwayErrorCode::NoError));
  wrapper.onGoAway(Envoy::Http::GoAwayErrorCode::NoError);
}

TEST_F(ReverseTunnelUpstreamCodecTest, PeerGoAwayNoReportWithoutTls) {
  auto* reporter = new NiceMock<MockReverseTunnelReporter>();

  const Network::SocketInterface* socket_if =
      Network::socketInterface("envoy.bootstrap.reverse_tunnel.upstream_socket_interface");
  ASSERT_NE(socket_if, nullptr);
  auto* acceptor =
      dynamic_cast<ReverseTunnelAcceptor*>(const_cast<Network::SocketInterface*>(socket_if));
  ASSERT_NE(acceptor, nullptr);
  acceptor->extension_ = nullptr;

  NiceMock<Envoy::Http::MockConnectionCallbacks> inner;
  DrainAwareClientCallbacks wrapper(inner, kTestSocketFd);

  EXPECT_CALL(*reporter, reportGoAwayEvent(_, _, _)).Times(0);
  EXPECT_CALL(inner, onGoAway(Envoy::Http::GoAwayErrorCode::NoError));
  wrapper.onGoAway(Envoy::Http::GoAwayErrorCode::NoError);

  delete reporter;
}

TEST_F(ReverseTunnelUpstreamCodecTest, CreateClientCodecPeerGoAwayReportsToReporter) {
  NiceMock<MockReverseTunnelReporter>* reporter = makeReporter();

  const std::string node_id = "node-1";
  const std::string cluster_id = "cluster-1";
  extension_ = makeExtension(node_id, cluster_id, kTestSocketFd);

  ReverseTunnelUpstreamCodecOptions opts(makeProto(true), stats_, nullptr);
  envoy::config::cluster::v3::Cluster::CustomClusterType custom_type;
  custom_type.set_name("envoy.clusters.reverse_connection");
  ON_CALL(cluster_, clusterType()).WillByDefault(Return(makeOptRef(std::as_const(custom_type))));
  ON_CALL(cluster_, maxResponseHeadersCount()).WillByDefault(Return(100));

  EXPECT_CALL(connection_, getSocket()).WillOnce(ReturnRef(socket_));
  EXPECT_CALL(*socket_raw_, ioHandle()).WillOnce(ReturnRef(io_handle_));
  EXPECT_CALL(io_handle_, fdDoNotUse()).WillOnce(Return(kTestSocketFd));

  auto codec = opts.createClientCodec(makeContext(Envoy::Http::CodecType::HTTP2));
  ASSERT_NE(codec, nullptr);

  EXPECT_CALL(*reporter, reportGoAwayEvent(Eq(node_id), Eq(cluster_id), Eq(kTestSocketFd)));
  EXPECT_CALL(callbacks_, onGoAway(Envoy::Http::GoAwayErrorCode::NoError));

  Buffer::OwnedImpl buffer;
  buffer.add(std::string(Envoy::Http::Http2::Http2Frame::makeEmptySettingsFrame()));
  buffer.add(std::string(Envoy::Http::Http2::Http2Frame::makeEmptyGoAwayFrame(
      0, Envoy::Http::Http2::Http2Frame::ErrorCode::NoError)));
  EXPECT_TRUE(codec->dispatch(buffer).ok());
}

// End-to-end on a real HTTP/2 client codec: a stream-0 METADATA frame carrying the configured
// drain key is dispatched, which drives DrainAwareClientCallbacks::onMetadata. The wrapper reports
// a GOAWAY to the reverse-tunnel reporter (so the initiator dials a replacement), erases the
// drain key, and forwards the remaining map to the inner callbacks. Requires the reverse-connection
// cluster type, HTTP/2, enable_drain_with_goaway, http2_protocol_options.allow_metadata, and a
// configured metadata_key.
TEST_F(ReverseTunnelUpstreamCodecTest, CreateClientCodecConnectionMetadataDispatched) {
  NiceMock<MockReverseTunnelReporter>* reporter = makeReporter();
  const std::string node_id = "node-1";
  const std::string cluster_id = "cluster-1";
  extension_ = makeExtension(node_id, cluster_id, kTestSocketFd);

  ReverseTunnelUpstreamCodecOptions opts(makeProtoWithMetadataKey(true, "drain_reverse_tunnel"),
                                         stats_, nullptr);
  envoy::config::cluster::v3::Cluster::CustomClusterType custom_type;
  custom_type.set_name("envoy.clusters.reverse_connection");
  ON_CALL(cluster_, clusterType()).WillByDefault(Return(makeOptRef(std::as_const(custom_type))));
  ON_CALL(cluster_, maxResponseHeadersCount()).WillByDefault(Return(100));
  cluster_.http2_options_.set_allow_metadata(true);

  EXPECT_CALL(connection_, getSocket()).WillOnce(ReturnRef(socket_));
  EXPECT_CALL(*socket_raw_, ioHandle()).WillOnce(ReturnRef(io_handle_));
  EXPECT_CALL(io_handle_, fdDoNotUse()).WillOnce(Return(kTestSocketFd));

  auto codec = opts.createClientCodec(makeContext(Envoy::Http::CodecType::HTTP2));
  ASSERT_NE(codec, nullptr);

  // The drain key is reported as a GOAWAY to the reporter for this fd's node/cluster.
  EXPECT_CALL(*reporter, reportGoAwayEvent(Eq(node_id), Eq(cluster_id), Eq(kTestSocketFd)));
  // The drain key is erased before the map is forwarded to the inner callbacks, so the inner
  // callbacks see the remaining (non-drain) entries only.
  EXPECT_CALL(callbacks_, onMetadata(_))
      .WillOnce(Invoke([](Envoy::Http::MetadataMapPtr&& metadata_map_ptr) {
        EXPECT_EQ(metadata_map_ptr->end(), metadata_map_ptr->find("drain_reverse_tunnel"));
      }));

  const Envoy::Http::MetadataMap metadata_map = {{"drain_reverse_tunnel", "true"},
                                                 {"other", "keep"}};
  const auto metadata_frame = Envoy::Http::Http2::Http2Frame::makeMetadataFrameFromMetadataMap(
      0, metadata_map, Envoy::Http::Http2::Http2Frame::MetadataFlags::EndMetadata);

  Buffer::OwnedImpl buffer;
  buffer.add(std::string(Envoy::Http::Http2::Http2Frame::makeEmptySettingsFrame()));
  buffer.add(std::string(metadata_frame));
  EXPECT_TRUE(codec->dispatch(buffer).ok());
}

// A stream-0 METADATA frame that does NOT carry the configured drain key is forwarded to the inner
// callbacks unchanged and does NOT report a GOAWAY (no replacement dial). Verifies the wrapper
// only treats the configured key+value as a drain signal.
TEST_F(ReverseTunnelUpstreamCodecTest, CreateClientCodecConnectionMetadataWithoutDrainKeyNoReport) {
  NiceMock<MockReverseTunnelReporter>* reporter = makeReporter();
  const std::string node_id = "node-1";
  const std::string cluster_id = "cluster-1";
  extension_ = makeExtension(node_id, cluster_id, kTestSocketFd);

  ReverseTunnelUpstreamCodecOptions opts(makeProtoWithMetadataKey(true, "drain_reverse_tunnel"),
                                         stats_, nullptr);
  envoy::config::cluster::v3::Cluster::CustomClusterType custom_type;
  custom_type.set_name("envoy.clusters.reverse_connection");
  ON_CALL(cluster_, clusterType()).WillByDefault(Return(makeOptRef(std::as_const(custom_type))));
  ON_CALL(cluster_, maxResponseHeadersCount()).WillByDefault(Return(100));
  cluster_.http2_options_.set_allow_metadata(true);

  EXPECT_CALL(connection_, getSocket()).WillOnce(ReturnRef(socket_));
  EXPECT_CALL(*socket_raw_, ioHandle()).WillOnce(ReturnRef(io_handle_));
  EXPECT_CALL(io_handle_, fdDoNotUse()).WillOnce(Return(kTestSocketFd));

  auto codec = opts.createClientCodec(makeContext(Envoy::Http::CodecType::HTTP2));
  ASSERT_NE(codec, nullptr);

  EXPECT_CALL(*reporter, reportGoAwayEvent(_, _, _)).Times(0);
  EXPECT_CALL(callbacks_, onMetadata(_))
      .WillOnce(Invoke([](Envoy::Http::MetadataMapPtr&& metadata_map_ptr) {
        // Unrelated key is forwarded unchanged.
        EXPECT_EQ("keep", (*metadata_map_ptr)["other"]);
      }));

  const Envoy::Http::MetadataMap metadata_map = {{"other", "keep"}};
  const auto metadata_frame = Envoy::Http::Http2::Http2Frame::makeMetadataFrameFromMetadataMap(
      0, metadata_map, Envoy::Http::Http2::Http2Frame::MetadataFlags::EndMetadata);

  Buffer::OwnedImpl buffer;
  buffer.add(std::string(Envoy::Http::Http2::Http2Frame::makeEmptySettingsFrame()));
  buffer.add(std::string(metadata_frame));
  EXPECT_TRUE(codec->dispatch(buffer).ok());
}

TEST_F(ReverseTunnelUpstreamCodecTest, LocalGracefulDrainDoesNotReportGoAway) {
  NiceMock<MockReverseTunnelReporter>* reporter = makeReporter();
  extension_ = makeExtension("node-1", "cluster-1", kTestSocketFd);

  auto inner = std::make_unique<NiceMock<Envoy::Http::MockClientConnection>>();
  auto* inner_raw = inner.get();
  auto callbacks = std::make_unique<DrainAwareClientCallbacks>(callbacks_, kTestSocketFd);
  auto* drain_timer = new NiceMock<Event::MockTimer>(&dispatcher_);
  DrainAwareClientConnection codec(std::move(inner), std::move(callbacks), stats_, dispatcher_,
                                   /*registry=*/nullptr, /*cluster=*/"", /*metadata_key=*/nullptr);

  EXPECT_CALL(*reporter, reportGoAwayEvent(_, _, _)).Times(0);
  // No metadata key: Phase 1 (sendMetadata) is a no-op; only the deferred final GOAWAY fires.
  EXPECT_CALL(*drain_timer, enableTimer(std::chrono::milliseconds(100), _));
  codec.startGracefulDrain(std::chrono::milliseconds(100));

  EXPECT_CALL(*inner_raw, goAway());
  EXPECT_CALL(callbacks_, onGoAway(Envoy::Http::GoAwayErrorCode::NoError));
  drain_timer->invokeCallback();
}

// The decorator forwards ClientConnection calls and counts a sent GOAWAY.
TEST_F(ReverseTunnelUpstreamCodecTest, ConnectionForwardsAndCountsGoawaySent) {
  auto inner = std::make_unique<NiceMock<Envoy::Http::MockClientConnection>>();
  auto* inner_raw = inner.get();
  auto callbacks = std::make_unique<DrainAwareClientCallbacks>(callbacks_, kTestSocketFd);
  DrainAwareClientConnection codec(std::move(inner), std::move(callbacks), stats_, dispatcher_,
                                   /*registry=*/nullptr, /*cluster=*/"", /*metadata_key=*/nullptr);

  EXPECT_CALL(*inner_raw, goAway());
  codec.goAway();
  EXPECT_EQ(1, stats_.goaway_sent_.value());

  EXPECT_CALL(*inner_raw, shutdownNotice());
  codec.shutdownNotice();
}

// Graceful drain sends a shutdown notice immediately, then after drain_time sends the
// final GOAWAY to the peer AND gracefully drains the local pool connection (drives onGoAway into
// the pool's active client) instead of hard-closing it. The pool drain is what lets in-flight
// requests finish and routes new requests onto the replacement tunnel; a hard close would abort
// in-flight.
TEST_F(ReverseTunnelUpstreamCodecTest, StartGracefulDrainTwoPhase) {
  auto inner = std::make_unique<NiceMock<Envoy::Http::MockClientConnection>>();
  auto* inner_raw = inner.get();
  auto callbacks = std::make_unique<DrainAwareClientCallbacks>(callbacks_, kTestSocketFd);
  // MockTimer registers with the dispatcher and captures the timer callback for invokeCallback().
  auto* drain_timer = new NiceMock<Event::MockTimer>(&dispatcher_);
  DrainAwareClientConnection codec(std::move(inner), std::move(callbacks), stats_, dispatcher_,
                                   /*registry=*/nullptr, /*cluster=*/"", /*metadata_key=*/nullptr);

  // Phase 1: no metadata key, so sendMetadata() is a no-op (no immediate wire signal); GOAWAY is
  // deferred (no goaway_sent yet).
  EXPECT_CALL(*drain_timer, enableTimer(std::chrono::milliseconds(5000), _));
  codec.startGracefulDrain(std::chrono::milliseconds(5000));
  EXPECT_EQ(0, stats_.goaway_sent_.value());

  // Phase 2: timer fires -> final GOAWAY to the peer, plus a graceful pool drain driven into the
  // local pool's active client (onGoAway on the wrapped callbacks). goaway_received_ stays 0
  // because this is a locally-initiated drain, not an observed peer GOAWAY.
  EXPECT_CALL(*inner_raw, goAway());
  EXPECT_CALL(callbacks_, onGoAway(Envoy::Http::GoAwayErrorCode::NoError));
  drain_timer->invokeCallback();
  EXPECT_EQ(1, stats_.goaway_sent_.value());

  // Idempotent: a second call does nothing.
  codec.startGracefulDrain(std::chrono::milliseconds(5000));
}

// With a metadata key configured, startGracefulDrain sends a connection-level METADATA frame
// immediately (Phase 1, via encodeMetadata on the wrapped codec), then after drain_time sends the
// final GOAWAY and gracefully drains the local pool connection (Phase 2). No soft GOAWAY is sent:
// the METADATA frame is the only immediate peer signal. This is the metadata-driven drain path.
TEST_F(ReverseTunnelUpstreamCodecTest, StartGracefulDrainSendsMetadataThenGoAway) {
  auto inner = std::make_unique<NiceMock<Envoy::Http::MockClientConnection>>();
  auto* inner_raw = inner.get();
  auto callbacks = std::make_unique<DrainAwareClientCallbacks>(callbacks_, kTestSocketFd);
  auto* drain_timer = new NiceMock<Event::MockTimer>(&dispatcher_);
  auto metadata_key = std::make_shared<std::string>("drain_reverse_tunnel");
  DrainAwareClientConnection codec(std::move(inner), std::move(callbacks), stats_, dispatcher_,
                                   /*registry=*/nullptr, /*cluster=*/"", metadata_key);

  // Phase 1: METADATA encoded now (drain key -> peer redials). MockClientConnection does not mock
  // encodeMetadata, so the call hits the base no-op; the body of sendMetadata() still runs (builds
  // the map and calls encodeMetadata), which is what we want for coverage. No GOAWAY yet.
  EXPECT_CALL(*drain_timer, enableTimer(std::chrono::milliseconds(5000), _));
  codec.startGracefulDrain(std::chrono::milliseconds(5000));
  EXPECT_EQ(0, stats_.goaway_sent_.value());

  // Phase 2: timer fires -> final GOAWAY + graceful pool drain (onGoAway into the local pool).
  EXPECT_CALL(*inner_raw, goAway());
  EXPECT_CALL(callbacks_, onGoAway(Envoy::Http::GoAwayErrorCode::NoError));
  drain_timer->invokeCallback();
  EXPECT_EQ(1, stats_.goaway_sent_.value());
}

// The registry fans a drain out to a registered codec for the matching cluster, which triggers
// the two-phase graceful drain (shutdownNotice now, GOAWAY after drain_time).
TEST_F(ReverseTunnelUpstreamCodecTest, RegistryDrainsRegisteredCluster) {
  auto registry = std::make_shared<UpstreamCodecDrainRegistry>(tls_);

  auto inner = std::make_unique<NiceMock<Envoy::Http::MockClientConnection>>();
  auto* inner_raw = inner.get();
  auto callbacks = std::make_unique<DrainAwareClientCallbacks>(callbacks_, kTestSocketFd);
  auto* drain_timer = new NiceMock<Event::MockTimer>(&dispatcher_);
  // Constructing with the registry auto-registers under "cluster_a".
  DrainAwareClientConnection codec(std::move(inner), std::move(callbacks), stats_, dispatcher_,
                                   registry, "cluster_a", /*metadata_key=*/nullptr);

  // Draining a different cluster is a no-op for this codec.
  registry->drainCluster("other", std::chrono::milliseconds(1000));

  // Draining the matching cluster reaches the codec. No metadata key, so Phase 1 is a no-op; only
  // the deferred final GOAWAY fires after drain_time.
  registry->drainCluster("cluster_a", std::chrono::milliseconds(1000));

  // Timer fires -> final GOAWAY + graceful pool drain (onGoAway into the local pool), not a close.
  EXPECT_CALL(*inner_raw, goAway());
  EXPECT_CALL(callbacks_, onGoAway(Envoy::Http::GoAwayErrorCode::NoError));
  drain_timer->invokeCallback();
  EXPECT_EQ(1, stats_.goaway_sent_.value());
}

// An empty cluster key drains every registered codec (the "drain all" admin path).
TEST_F(ReverseTunnelUpstreamCodecTest, RegistryDrainsAllClusters) {
  auto registry = std::make_shared<UpstreamCodecDrainRegistry>(tls_);

  auto inner = std::make_unique<NiceMock<Envoy::Http::MockClientConnection>>();
  auto* inner_raw = inner.get();
  auto callbacks = std::make_unique<DrainAwareClientCallbacks>(callbacks_, kTestSocketFd);
  auto* drain_timer = new NiceMock<Event::MockTimer>(&dispatcher_);
  DrainAwareClientConnection codec(std::move(inner), std::move(callbacks), stats_, dispatcher_,
                                   registry, "cluster_a", /*metadata_key=*/nullptr);

  // Empty key fans the drain out across all registered clusters. No metadata key, so Phase 1 is a
  // no-op; only the deferred final GOAWAY fires after drain_time.
  registry->drainCluster("", std::chrono::milliseconds(1000));

  EXPECT_CALL(*inner_raw, goAway());
  EXPECT_CALL(callbacks_, onGoAway(Envoy::Http::GoAwayErrorCode::NoError));
  drain_timer->invokeCallback();
}

// The factory produces empty protos for both the protocol-options and config message hooks.
TEST_F(ReverseTunnelUpstreamCodecTest, FactoryCreatesEmptyProtos) {
  ReverseTunnelUpstreamCodecFactory factory;
  auto options_proto = factory.createEmptyProtocolOptionsProto();
  ASSERT_NE(nullptr, options_proto);
  EXPECT_EQ("envoy.extensions.upstreams.http.reverse_tunnel.v3.ReverseTunnelUpstreamCodecOptions",
            options_proto->GetTypeName());
  EXPECT_NE(nullptr, factory.createEmptyConfigProto());
}

// When disabled, the factory declines (returns nullptr) so CodecClientProd uses the stock codec.
TEST_F(ReverseTunnelUpstreamCodecTest, PassThroughWhenDisabled) {
  ReverseTunnelUpstreamCodecOptions opts(makeProto(false), stats_, nullptr);
  EXPECT_EQ(opts.createClientCodec(makeContext(Envoy::Http::CodecType::HTTP2)), nullptr);
}

// Non-HTTP/2 codecs are not customized even when enabled; the factory declines.
TEST_F(ReverseTunnelUpstreamCodecTest, PassThroughForHttp1) {
  ReverseTunnelUpstreamCodecOptions opts(makeProto(true), stats_, nullptr);
  EXPECT_EQ(opts.createClientCodec(makeContext(Envoy::Http::CodecType::HTTP1)), nullptr);
}

// Even enabled + HTTP/2, the codec is only customized for reverse-connection clusters. A cluster
// whose type is not envoy.clusters.reverse_connection (here: the default mock returns no custom
// cluster type) declines, falling through to the stock codec.
TEST_F(ReverseTunnelUpstreamCodecTest, PassThroughForNonReverseConnectionCluster) {
  ReverseTunnelUpstreamCodecOptions opts(makeProto(true), stats_, nullptr);

  // MockClusterInfo::clusterType() returns an empty OptRef by default (a built-in cluster type),
  // so the reverse-connection guard does not match.
  ON_CALL(cluster_, clusterType())
      .WillByDefault(
          testing::Return(OptRef<const envoy::config::cluster::v3::Cluster::CustomClusterType>{}));

  EXPECT_EQ(opts.createClientCodec(makeContext(Envoy::Http::CodecType::HTTP2)), nullptr);
}

// Enabled + HTTP/2 + a reverse-connection cluster: the factory builds a drain-aware HTTP/2 client
// codec (wrapping the stock codec with the GOAWAY-observing callbacks).
TEST_F(ReverseTunnelUpstreamCodecTest, CreatesDrainAwareCodecForReverseConnectionHttp2) {
  ReverseTunnelUpstreamCodecOptions opts(makeProto(true), stats_, nullptr);

  envoy::config::cluster::v3::Cluster::CustomClusterType custom_type;
  custom_type.set_name("envoy.clusters.reverse_connection");
  ON_CALL(cluster_, clusterType()).WillByDefault(Return(makeOptRef(std::as_const(custom_type))));
  ON_CALL(cluster_, maxResponseHeadersCount()).WillByDefault(Return(100));

  EXPECT_CALL(connection_, getSocket()).WillOnce(ReturnRef(socket_));
  EXPECT_CALL(*socket_raw_, ioHandle()).WillOnce(ReturnRef(io_handle_));
  EXPECT_CALL(io_handle_, fdDoNotUse()).WillOnce(Return(kTestSocketFd));

  auto codec = opts.createClientCodec(makeContext(Envoy::Http::CodecType::HTTP2));
  ASSERT_NE(codec, nullptr);
  EXPECT_EQ(Envoy::Http::Protocol::Http2, codec->protocol());
}

// End-to-end on a real HTTP/2 client codec: startGracefulDrain sends a connection-level METADATA
// frame immediately (Phase 1, via the stock codec's encodeMetadata), then the final GOAWAY and a
// graceful pool drain after drain_time (Phase 2). Exercises the real-codec encodeMetadata path
// that the mock-inner tests cannot reach, and requires http2_protocol_options.allow_metadata so
// the codec emits the stream-0 METADATA frame.
TEST_F(ReverseTunnelUpstreamCodecTest, GracefulDrainSendsMetadataOnRealHttp2Codec) {
  ON_CALL(cluster_, maxResponseHeadersCount()).WillByDefault(Return(100));
  cluster_.http2_options_.set_allow_metadata(true);

  auto callbacks = std::make_unique<DrainAwareClientCallbacks>(callbacks_, kTestSocketFd);
  auto& callbacks_ref = *callbacks;
  auto h2 = std::make_unique<Envoy::Http::Http2::ClientConnectionImpl>(
      connection_, callbacks_ref, cluster_.http2CodecStats(), random_,
      cluster_.httpProtocolOptions().http2Options(),
      cluster_.maxResponseHeadersKb().value_or(Envoy::Http::DEFAULT_MAX_REQUEST_HEADERS_KB),
      cluster_.maxResponseHeadersCount(), Envoy::Http::Http2::ProdNghttp2SessionFactory::get());
  auto metadata_key = std::make_shared<std::string>("drain_reverse_tunnel");
  // Registers with dispatcher_; the next createTimer() returns it.
  auto* drain_timer = new NiceMock<Event::MockTimer>(&dispatcher_);
  DrainAwareClientConnection codec(std::move(h2), std::move(callbacks), stats_, dispatcher_,
                                   /*registry=*/nullptr, /*cluster=*/"", metadata_key);

  // Phase 1: METADATA encoded now on the real codec (goaway_sent stays 0; no soft GOAWAY).
  EXPECT_CALL(*drain_timer, enableTimer(std::chrono::milliseconds(5000), _));
  codec.startGracefulDrain(std::chrono::milliseconds(5000));
  EXPECT_EQ(0, stats_.goaway_sent_.value());

  // Phase 2: timer fires -> final GOAWAY on the real codec (goaway_sent -> 1) + graceful pool
  // drain (onGoAway into the wrapped callbacks).
  EXPECT_CALL(callbacks_, onGoAway(Envoy::Http::GoAwayErrorCode::NoError));
  drain_timer->invokeCallback();
  EXPECT_EQ(1, stats_.goaway_sent_.value());
}

// The DrainAwareClientCallbacks wrapper forwards the non-GOAWAY connection callbacks unchanged.
TEST_F(ReverseTunnelUpstreamCodecTest, CallbacksForwardSettingsAndMaxStreams) {
  NiceMock<Envoy::Http::MockConnectionCallbacks> inner;
  DrainAwareClientCallbacks wrapper(inner, kTestSocketFd);

  NiceMock<Envoy::Http::MockReceivedSettings> settings;
  EXPECT_CALL(inner, onSettings(_));
  wrapper.onSettings(settings);

  // onMaxStreamsChanged has a default (non-pure) implementation, so it is not a gmock method;
  // exercising the forwarding path is enough for coverage.
  wrapper.onMaxStreamsChanged(42);
}

// onMetadata with no configured metadata_key forwards the map to the inner callbacks unchanged and
// does NOT report a GOAWAY (the wrapper is metadata-transparent in this mode).
TEST_F(ReverseTunnelUpstreamCodecTest, OnMetadataForwardsWhenNoKey) {
  NiceMock<Envoy::Http::MockConnectionCallbacks> inner;
  DrainAwareClientCallbacks wrapper(inner, kTestSocketFd, /*metadata_key=*/nullptr);

  auto metadata_map =
      std::make_unique<Envoy::Http::MetadataMap>(Envoy::Http::MetadataMap{{"key", "value"}});
  EXPECT_CALL(inner, onMetadata(_))
      .WillOnce(Invoke([](Envoy::Http::MetadataMapPtr&& metadata_map_ptr) {
        EXPECT_EQ(1, metadata_map_ptr->size());
        EXPECT_EQ("value", (*metadata_map_ptr)["key"]);
      }));
  wrapper.onMetadata(std::move(metadata_map));
}

// onMetadata with the configured key present and value "true" reports a GOAWAY to the reporter,
// erases the drain key, and forwards the remaining map to the inner callbacks.
TEST_F(ReverseTunnelUpstreamCodecTest, OnMetadataMatchingKeyReportsAndErases) {
  NiceMock<MockReverseTunnelReporter>* reporter = makeReporter();
  const std::string node_id = "node-1";
  const std::string cluster_id = "cluster-1";
  extension_ = makeExtension(node_id, cluster_id, kTestSocketFd);

  NiceMock<Envoy::Http::MockConnectionCallbacks> inner;
  auto metadata_key = std::make_shared<std::string>("drain_reverse_tunnel");
  DrainAwareClientCallbacks wrapper(inner, kTestSocketFd, metadata_key);

  EXPECT_CALL(*reporter, reportGoAwayEvent(Eq(node_id), Eq(cluster_id), Eq(kTestSocketFd)));
  EXPECT_CALL(inner, onMetadata(_))
      .WillOnce(Invoke([](Envoy::Http::MetadataMapPtr&& metadata_map_ptr) {
        // The drain key was erased; only the unrelated entry remains.
        EXPECT_EQ(metadata_map_ptr->end(), metadata_map_ptr->find("drain_reverse_tunnel"));
        EXPECT_EQ("keep", (*metadata_map_ptr)["other"]);
      }));

  auto metadata_map = std::make_unique<Envoy::Http::MetadataMap>(
      Envoy::Http::MetadataMap{{"drain_reverse_tunnel", "true"}, {"other", "keep"}});
  wrapper.onMetadata(std::move(metadata_map));
}

// onMetadata with the configured key absent forwards the map unchanged and does NOT report.
TEST_F(ReverseTunnelUpstreamCodecTest, OnMetadataKeyAbsentForwardsNoReport) {
  NiceMock<MockReverseTunnelReporter>* reporter = makeReporter();
  extension_ = makeExtension("node-1", "cluster-1", kTestSocketFd);

  NiceMock<Envoy::Http::MockConnectionCallbacks> inner;
  auto metadata_key = std::make_shared<std::string>("drain_reverse_tunnel");
  DrainAwareClientCallbacks wrapper(inner, kTestSocketFd, metadata_key);

  EXPECT_CALL(*reporter, reportGoAwayEvent(_, _, _)).Times(0);
  EXPECT_CALL(inner, onMetadata(_))
      .WillOnce(Invoke([](Envoy::Http::MetadataMapPtr&& metadata_map_ptr) {
        EXPECT_EQ("keep", (*metadata_map_ptr)["other"]);
      }));

  auto metadata_map =
      std::make_unique<Envoy::Http::MetadataMap>(Envoy::Http::MetadataMap{{"other", "keep"}});
  wrapper.onMetadata(std::move(metadata_map));
}

// onMetadata with the configured key present but value != "true" forwards the map unchanged and
// does NOT report: only the exact key+value ("true") is treated as a drain signal.
TEST_F(ReverseTunnelUpstreamCodecTest, OnMetadataWrongValueForwardsNoReport) {
  NiceMock<MockReverseTunnelReporter>* reporter = makeReporter();
  extension_ = makeExtension("node-1", "cluster-1", kTestSocketFd);

  NiceMock<Envoy::Http::MockConnectionCallbacks> inner;
  auto metadata_key = std::make_shared<std::string>("drain_reverse_tunnel");
  DrainAwareClientCallbacks wrapper(inner, kTestSocketFd, metadata_key);

  EXPECT_CALL(*reporter, reportGoAwayEvent(_, _, _)).Times(0);
  EXPECT_CALL(inner, onMetadata(_))
      .WillOnce(Invoke([](Envoy::Http::MetadataMapPtr&& metadata_map_ptr) {
        // Key present with a non-"true" value is forwarded unchanged.
        EXPECT_EQ("false", (*metadata_map_ptr)["drain_reverse_tunnel"]);
      }));

  auto metadata_map = std::make_unique<Envoy::Http::MetadataMap>(
      Envoy::Http::MetadataMap{{"drain_reverse_tunnel", "false"}});
  wrapper.onMetadata(std::move(metadata_map));
}

// The reporter is notified at most once per connection: a matching metadata frame reports, and a
// subsequent peer GOAWAY on the same connection does NOT report again (the once-guard suppresses
// it). The GOAWAY is still forwarded to the inner callbacks.
TEST_F(ReverseTunnelUpstreamCodecTest, NotifyReporterFiresAtMostOnce) {
  NiceMock<MockReverseTunnelReporter>* reporter = makeReporter();
  const std::string node_id = "node-1";
  const std::string cluster_id = "cluster-1";
  extension_ = makeExtension(node_id, cluster_id, kTestSocketFd);

  NiceMock<Envoy::Http::MockConnectionCallbacks> inner;
  auto metadata_key = std::make_shared<std::string>("drain_reverse_tunnel");
  DrainAwareClientCallbacks wrapper(inner, kTestSocketFd, metadata_key);

  // First trigger (matching metadata) reports exactly once.
  EXPECT_CALL(*reporter, reportGoAwayEvent(Eq(node_id), Eq(cluster_id), Eq(kTestSocketFd)));
  auto metadata_map = std::make_unique<Envoy::Http::MetadataMap>(
      Envoy::Http::MetadataMap{{"drain_reverse_tunnel", "true"}});
  wrapper.onMetadata(std::move(metadata_map));

  // Second trigger (peer GOAWAY) must NOT report again, but still forwards to the inner callbacks.
  EXPECT_CALL(*reporter, reportGoAwayEvent(_, _, _)).Times(0);
  EXPECT_CALL(inner, onGoAway(Envoy::Http::GoAwayErrorCode::NoError));
  wrapper.onGoAway(Envoy::Http::GoAwayErrorCode::NoError);
}

// Symmetric to the above but in reverse order: a peer GOAWAY reports first, and a subsequent
// matching metadata frame on the same connection does NOT report again (it still forwards the
// map, with the drain key erased, to the inner callbacks).
TEST_F(ReverseTunnelUpstreamCodecTest, NotifyReporterFiresAtMostOnceGoAwayFirst) {
  NiceMock<MockReverseTunnelReporter>* reporter = makeReporter();
  extension_ = makeExtension("node-1", "cluster-1", kTestSocketFd);

  NiceMock<Envoy::Http::MockConnectionCallbacks> inner;
  auto metadata_key = std::make_shared<std::string>("drain_reverse_tunnel");
  DrainAwareClientCallbacks wrapper(inner, kTestSocketFd, metadata_key);

  // First trigger (peer GOAWAY) reports exactly once and forwards to the inner callbacks.
  EXPECT_CALL(*reporter, reportGoAwayEvent(_, _, _));
  EXPECT_CALL(inner, onGoAway(Envoy::Http::GoAwayErrorCode::NoError));
  wrapper.onGoAway(Envoy::Http::GoAwayErrorCode::NoError);

  // Second trigger (matching metadata) must NOT report again, but still forwards (key erased).
  EXPECT_CALL(*reporter, reportGoAwayEvent(_, _, _)).Times(0);
  EXPECT_CALL(inner, onMetadata(_))
      .WillOnce(Invoke([](Envoy::Http::MetadataMapPtr&& metadata_map_ptr) {
        EXPECT_EQ(metadata_map_ptr->end(), metadata_map_ptr->find("drain_reverse_tunnel"));
      }));
  auto metadata_map = std::make_unique<Envoy::Http::MetadataMap>(
      Envoy::Http::MetadataMap{{"drain_reverse_tunnel", "true"}});
  wrapper.onMetadata(std::move(metadata_map));
}

// The decorator forwards the remaining ClientConnection surface to the wrapped codec.
TEST_F(ReverseTunnelUpstreamCodecTest, ConnectionForwardsRemainingCalls) {
  auto inner = std::make_unique<NiceMock<Envoy::Http::MockClientConnection>>();
  auto* inner_raw = inner.get();
  auto callbacks = std::make_unique<DrainAwareClientCallbacks>(callbacks_, kTestSocketFd);
  DrainAwareClientConnection codec(std::move(inner), std::move(callbacks), stats_, dispatcher_,
                                   /*registry=*/nullptr, /*cluster=*/"", /*metadata_key=*/nullptr);

  NiceMock<Envoy::Http::MockResponseDecoder> decoder;
  NiceMock<Envoy::Http::MockRequestEncoder> encoder;
  EXPECT_CALL(*inner_raw, newStream(_)).WillOnce(ReturnRef(encoder));
  codec.newStream(decoder);

  Buffer::OwnedImpl data;
  EXPECT_CALL(*inner_raw, dispatch(_)).WillOnce(Return(Envoy::Http::okStatus()));
  EXPECT_OK(codec.dispatch(data));

  EXPECT_CALL(*inner_raw, protocol()).WillOnce(Return(Envoy::Http::Protocol::Http2));
  EXPECT_EQ(Envoy::Http::Protocol::Http2, codec.protocol());

  EXPECT_CALL(*inner_raw, wantsToWrite()).WillOnce(Return(true));
  EXPECT_TRUE(codec.wantsToWrite());

  EXPECT_CALL(*inner_raw, onUnderlyingConnectionAboveWriteBufferHighWatermark());
  codec.onUnderlyingConnectionAboveWriteBufferHighWatermark();

  EXPECT_CALL(*inner_raw, onUnderlyingConnectionBelowWriteBufferLowWatermark());
  codec.onUnderlyingConnectionBelowWriteBufferLowWatermark();
}

// The factory builds the options object, lazily creates the shared drain registry, and registers
// the admin drain trigger. Invoking that trigger fans a drain out via the registry.
TEST_F(ReverseTunnelUpstreamCodecTest, FactoryCreatesOptionsAndRegistersAdminHandler) {
  NiceMock<Server::Configuration::MockTransportSocketFactoryContext> factory_context;
  auto& admin = factory_context.server_context_.admin_;
  ON_CALL(factory_context, serverFactoryContext())
      .WillByDefault(ReturnRef(factory_context.server_context_));
  ON_CALL(factory_context.server_context_, admin())
      .WillByDefault(Return(OptRef<Server::Admin>{admin}));
  // The handler defaults the rotation grace window to the server's drain time.
  ON_CALL(factory_context.server_context_.options_, drainTime())
      .WillByDefault(Return(std::chrono::seconds(30)));

  Server::Admin::HandlerCb captured_handler;
  EXPECT_CALL(admin, addHandler("/reverse_tunnel/drain_clusters", _, _, false, true, _))
      .WillOnce([&captured_handler](const std::string&, const std::string&,
                                    Server::Admin::HandlerCb cb, bool, bool,
                                    const Server::Admin::ParamDescriptorVec&) {
        captured_handler = std::move(cb);
        return true;
      });

  ReverseTunnelUpstreamCodecFactory factory;
  auto proto = makeProto(true);
  auto result = factory.createProtocolOptionsConfig(proto, factory_context);
  ASSERT_THAT(result, IsOkAndHolds(::testing::NotNull()));
  EXPECT_TRUE(result.value()->upstreamHttpClientCodecFactory().has_value());

  // Drive the admin handler with a drain_time_ms query param to exercise its body.
  ASSERT_TRUE(captured_handler != nullptr);
  NiceMock<Server::MockAdminStream> admin_stream;
  Envoy::Http::Utility::QueryParamsMulti params;
  params.add("cluster", "cluster_a");
  params.add("drain_time_ms", "1000");
  ON_CALL(admin_stream, queryParams()).WillByDefault(Return(params));
  Envoy::Http::TestResponseHeaderMapImpl response_headers;
  Buffer::OwnedImpl response;
  EXPECT_EQ(Envoy::Http::Code::OK, captured_handler(response_headers, response, admin_stream));
  EXPECT_TRUE(absl::StrContains(response.toString(), "cluster_a"));
  EXPECT_TRUE(absl::StrContains(response.toString(), "drain_time_ms=1000"));

  // Without an explicit drain_time_ms, the handler falls back to the server drain time (30s).
  Envoy::Http::Utility::QueryParamsMulti default_params;
  ON_CALL(admin_stream, queryParams()).WillByDefault(Return(default_params));
  Buffer::OwnedImpl default_response;
  EXPECT_EQ(Envoy::Http::Code::OK,
            captured_handler(response_headers, default_response, admin_stream));
  EXPECT_TRUE(absl::StrContains(default_response.toString(), "drain_time_ms=30000"));

  // A non-numeric drain_time_ms is rejected with 400 Bad Request.
  Envoy::Http::Utility::QueryParamsMulti bad_params;
  bad_params.add("drain_time_ms", "not-a-number");
  ON_CALL(admin_stream, queryParams()).WillByDefault(Return(bad_params));
  Buffer::OwnedImpl bad_response;
  EXPECT_EQ(Envoy::Http::Code::BadRequest,
            captured_handler(response_headers, bad_response, admin_stream));
}

// With the feature disabled the factory neither creates the drain registry nor registers the admin
// trigger, but still returns a usable options object.
TEST_F(ReverseTunnelUpstreamCodecTest, FactoryDoesNotRegisterAdminHandlerWhenDisabled) {
  NiceMock<Server::Configuration::MockTransportSocketFactoryContext> factory_context;
  auto& admin = factory_context.server_context_.admin_;
  ON_CALL(factory_context, serverFactoryContext())
      .WillByDefault(ReturnRef(factory_context.server_context_));
  ON_CALL(factory_context.server_context_, admin())
      .WillByDefault(Return(OptRef<Server::Admin>{admin}));

  EXPECT_CALL(admin, addHandler(_, _, _, _, _, _)).Times(0);

  ReverseTunnelUpstreamCodecFactory factory;
  auto proto = makeProto(false);
  auto result = factory.createProtocolOptionsConfig(proto, factory_context);
  ASSERT_THAT(result, IsOkAndHolds(::testing::NotNull()));
  EXPECT_TRUE(result.value()->upstreamHttpClientCodecFactory().has_value());
}

} // namespace
} // namespace ReverseTunnel
} // namespace Http
} // namespace Upstreams
} // namespace Extensions
} // namespace Envoy
