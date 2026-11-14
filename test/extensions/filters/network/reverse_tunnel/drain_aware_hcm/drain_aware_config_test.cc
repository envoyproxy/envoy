#include "source/common/stats/isolated_store_impl.h"
#include "source/extensions/bootstrap/reverse_tunnel/downstream_socket_interface/downstream_reverse_connection_io_handle.h"
#include "source/extensions/bootstrap/reverse_tunnel/downstream_socket_interface/reverse_connection_io_handle.h"
#include "source/extensions/filters/network/reverse_tunnel/drain_aware_hcm/drain_aware_config.h"

#include "test/mocks/event/mocks.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/server/factory_context.h"
#include "test/mocks/server/overload_manager.h"
#include "test/mocks/upstream/cluster_manager.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/status_utility.h"
#include "test/test_common/test_runtime.h"
#include "test/test_common/utility.h"

#include "absl/strings/str_cat.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using ::Envoy::StatusHelpers::HasStatus;
using ::Envoy::StatusHelpers::IsOkAndHolds;
using testing::_;
using testing::HasSubstr;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ReverseTunnel {
namespace {

// Minimal valid DrainAwareHttpConnectionManager proto YAML.
// The router filter must be registered (via the BUILD dep on
// //source/extensions/filters/http/router:config).
constexpr absl::string_view kMinimalConfig = R"EOF(
hcm_config:
  stat_prefix: test
  route_config:
    virtual_hosts:
    - name: local
      domains: ["*"]
      routes:
      - match:
          prefix: "/"
        direct_response:
          status: 200
  http_filters:
  - name: envoy.filters.http.router
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.http.router.v3.Router
)EOF";

class DrainAwareConfigTest : public Event::TestUsingSimulatedTime, public testing::Test {
protected:
  DrainAwareConfigTest() {
    ON_CALL(context_, listenerInfo()).WillByDefault(testing::ReturnRef(listener_info_));
  }

  NiceMock<Network::MockListenerInfo> listener_info_;
  NiceMock<Server::Configuration::MockFactoryContext> context_;

  envoy::extensions::filters::network::reverse_tunnel::v3::DrainAwareHttpConnectionManager
  parseConfig(absl::string_view yaml) {
    envoy::extensions::filters::network::reverse_tunnel::v3::DrainAwareHttpConnectionManager proto;
    TestUtility::loadFromYaml(std::string(yaml), proto);
    return proto;
  }

  // Builds a drain-aware HCM config (or a test subclass of it) the way the factory does. The
  // singletons are kept on the fixture because the config holds references into them.
  template <class ConfigT> std::shared_ptr<ConfigT> makeConfig(const DrainAwareHcm& proto_config) {
    singletons_ = HttpConnectionManager::Utility::createSingletons(context_);
    absl::Status creation_status = absl::OkStatus();
    auto config = std::make_shared<ConfigT>(
        proto_config, context_, *singletons_.date_provider_,
        *singletons_.route_config_provider_manager_,
        singletons_.scoped_routes_config_provider_manager_.get(), *singletons_.tracer_manager_,
        *singletons_.filter_config_provider_manager_, creation_status);
    EXPECT_TRUE(creation_status.ok()) << creation_status;
    return config;
  }

  HttpConnectionManager::Utility::Singletons singletons_;
};

TEST_F(DrainAwareConfigTest, FactoryName) {
  DrainAwareHttpConnectionManagerFilterConfigFactory factory;
  EXPECT_EQ("envoy.filters.network.reverse_tunnel_drain_aware_http_connection_manager",
            factory.name());
}

TEST_F(DrainAwareConfigTest, CreateEmptyConfigProto) {
  DrainAwareHttpConnectionManagerFilterConfigFactory factory;
  auto proto = factory.createEmptyConfigProto();
  ASSERT_NE(nullptr, proto);
  EXPECT_EQ("envoy.extensions.filters.network.reverse_tunnel.v3.DrainAwareHttpConnectionManager",
            proto->GetTypeName());
}

TEST_F(DrainAwareConfigTest, CreateFilterFactoryFromValidConfig) {
  DrainAwareHttpConnectionManagerFilterConfigFactory factory;
  auto proto_config = parseConfig(kMinimalConfig);
  auto result = factory.createFilterFactoryFromProto(proto_config, context_);
  ASSERT_THAT(result, IsOkAndHolds(::testing::NotNull()));
}

TEST_F(DrainAwareConfigTest, FilterFactoryCallbackIsNonNull) {
  DrainAwareHttpConnectionManagerFilterConfigFactory factory;
  auto proto_config = parseConfig(kMinimalConfig);
  auto result = factory.createFilterFactoryFromProto(proto_config, context_);
  // Verify a callable callback was produced. The actual ConnectionManagerImpl
  // installation path is exercised end-to-end in integration_test.cc.
  ASSERT_THAT(result, IsOkAndHolds(::testing::NotNull()));
}

// Subclass that overrides createBaseCodec to return nullptr, exercising the defensive
// nullptr check in createCodec().
class NullCodecDrainAwareConfig : public DrainAwareHttpConnectionManagerConfig {
public:
  using DrainAwareHttpConnectionManagerConfig::DrainAwareHttpConnectionManagerConfig;

  Http::ServerConnectionPtr createBaseCodec(Network::Connection&, const Buffer::Instance&,
                                            Http::ServerConnectionCallbacks&,
                                            Server::OverloadManager&) override {
    return nullptr;
  }
};

TEST_F(DrainAwareConfigTest, CreateCodecReturnsNullptrWhenBaseReturnsNullptr) {
  auto config = makeConfig<NullCodecDrainAwareConfig>(parseConfig(kMinimalConfig));

  NiceMock<Network::MockConnection> connection;
  NiceMock<Http::MockServerConnectionCallbacks> callbacks;
  NiceMock<Server::MockOverloadManager> overload_manager;
  Buffer::OwnedImpl data;

  auto codec = config->createCodec(connection, data, callbacks, overload_manager);
  EXPECT_EQ(nullptr, codec);
}

// Same defensive nullptr check, but with drain_with_goaway enabled: createCodec runs the re-dial
// wiring block first (here a plain socket, so no wiring), then returns nullptr when the base codec
// declines.
TEST_F(DrainAwareConfigTest, CreateCodecReturnsNullptrWhenBaseReturnsNullptrDrainEnabled) {
  auto proto_config = parseConfig(kMinimalConfig);
  proto_config.set_enable_drain_with_goaway(true);
  auto config = makeConfig<NullCodecDrainAwareConfig>(proto_config);

  NiceMock<Network::MockConnection> connection;
  Network::ConnectionSocketPtr socket = std::make_unique<NiceMock<Network::MockConnectionSocket>>();
  ON_CALL(connection, getSocket()).WillByDefault(ReturnRef(socket));
  NiceMock<Http::MockServerConnectionCallbacks> callbacks;
  NiceMock<Server::MockOverloadManager> overload_manager;
  Buffer::OwnedImpl data;

  auto codec = config->createCodec(connection, data, callbacks, overload_manager);
  EXPECT_EQ(nullptr, codec);
}

// Exercises the real createBaseCodec (parent HCM codec construction) rather than a test override,
// building an HTTP/1 server codec and wrapping it in the drain-aware connection.
TEST_F(DrainAwareConfigTest, CreateCodecBuildsRealHttp1BaseCodec) {
  constexpr absl::string_view kHttp1Config = R"EOF(
hcm_config:
  stat_prefix: test
  codec_type: HTTP1
  route_config:
    virtual_hosts:
    - name: local
      domains: ["*"]
      routes:
      - match:
          prefix: "/"
        direct_response:
          status: 200
  http_filters:
  - name: envoy.filters.http.router
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.http.router.v3.Router
)EOF";
  auto config = makeConfig<DrainAwareHttpConnectionManagerConfig>(parseConfig(kHttp1Config));

  NiceMock<Network::MockConnection> connection;
  NiceMock<Http::MockServerConnectionCallbacks> callbacks;
  NiceMock<Server::MockOverloadManager> overload_manager;
  Buffer::OwnedImpl data;

  auto codec = config->createCodec(connection, data, callbacks, overload_manager);
  ASSERT_NE(nullptr, codec);
  EXPECT_EQ(Http::Protocol::Http11, codec->protocol());
}

// Subclass that returns a real (mock) codec from createBaseCodec, so createCodec() exercises the
// drain-aware wrapping path rather than the defensive nullptr branch.
class FakeCodecDrainAwareConfig : public DrainAwareHttpConnectionManagerConfig {
public:
  using DrainAwareHttpConnectionManagerConfig::DrainAwareHttpConnectionManagerConfig;

  Http::ServerConnectionPtr createBaseCodec(Network::Connection&, const Buffer::Instance&,
                                            Http::ServerConnectionCallbacks& callbacks,
                                            Server::OverloadManager&) override {
    last_callbacks_ = &callbacks;
    auto codec = std::make_unique<NiceMock<Http::MockServerConnection>>();
    ON_CALL(*codec, protocol()).WillByDefault(Return(Http::Protocol::Http2));
    return codec;
  }

  Http::ServerConnectionCallbacks* last_callbacks_{nullptr};
};

// With drain_with_goaway enabled but a plain (non reverse-tunnel) socket, the typed cast yields no
// initiator handle, so no re-dial wiring is installed; the drain-aware wrapper is still produced
// and the codec callbacks are passed through unchanged (no GOAWAY-observing wrapper).
TEST_F(DrainAwareConfigTest, CreateCodecDrainEnabledNonReverseSocket) {
  auto proto_config = parseConfig(kMinimalConfig);
  proto_config.set_enable_drain_with_goaway(true);
  auto config = makeConfig<FakeCodecDrainAwareConfig>(proto_config);

  NiceMock<Network::MockConnection> connection;
  // Default MockConnectionSocket exposes a stock IoSocketHandleImpl, which the typed cast rejects.
  Network::ConnectionSocketPtr socket = std::make_unique<NiceMock<Network::MockConnectionSocket>>();
  ON_CALL(connection, getSocket()).WillByDefault(ReturnRef(socket));

  NiceMock<Http::MockServerConnectionCallbacks> callbacks;
  NiceMock<Server::MockOverloadManager> overload_manager;
  Buffer::OwnedImpl data;

  auto codec = config->createCodec(connection, data, callbacks, overload_manager);
  EXPECT_NE(nullptr, codec);
  // No peer-GOAWAY wrapper: the HCM callbacks are forwarded to the base codec directly.
  EXPECT_EQ(&callbacks, config->last_callbacks_);
}

// With drain_with_goaway enabled and a reverse-tunnel socket whose IoHandle has a live parent
// initiator, createCodec() installs both the local-drain re-dial closure and a GOAWAY-observing
// callbacks wrapper interposed in front of the HCM callbacks.
TEST_F(DrainAwareConfigTest, CreateCodecDrainEnabledReverseTunnelWiresRedial) {
  auto proto_config = parseConfig(kMinimalConfig);
  proto_config.set_enable_drain_with_goaway(true);
  auto config = makeConfig<FakeCodecDrainAwareConfig>(proto_config);

  // Build a real initiator IoHandle to act as the tunnel's parent. The extension is unused during
  // codec construction (the re-dial closure that would reach it is created but not invoked here).
  NiceMock<Upstream::MockClusterManager> cluster_manager;
  Stats::IsolatedStoreImpl stats_store;
  Bootstrap::ReverseConnection::ReverseConnectionSocketConfig rc_config;
  rc_config.src_node_id = "node";
  rc_config.src_cluster_id = "cluster";
  auto parent = std::make_unique<Bootstrap::ReverseConnection::ReverseConnectionIOHandle>(
      ::socket(AF_INET, SOCK_STREAM, 0), rc_config, cluster_manager, /*extension=*/nullptr,
      *stats_store.rootScope());

  // The accepted socket's IoHandle is the reverse-tunnel handle owning an (unused) inner socket.
  auto tunnel_handle =
      std::make_unique<Bootstrap::ReverseConnection::DownstreamReverseConnectionIOHandle>(
          std::make_unique<NiceMock<Network::MockConnectionSocket>>(), parent.get(),
          "10.0.0.1:5000", /*connection_id=*/0);
  auto outer_mock = std::make_unique<NiceMock<Network::MockConnectionSocket>>();
  ON_CALL(*outer_mock, ioHandle()).WillByDefault(ReturnRef(*tunnel_handle));
  ON_CALL(testing::Const(*outer_mock), ioHandle()).WillByDefault(ReturnRef(*tunnel_handle));
  Network::ConnectionSocketPtr socket = std::move(outer_mock);

  NiceMock<Network::MockConnection> connection;
  ON_CALL(connection, getSocket()).WillByDefault(ReturnRef(socket));

  NiceMock<Http::MockServerConnectionCallbacks> callbacks;
  NiceMock<Server::MockOverloadManager> overload_manager;
  Buffer::OwnedImpl data;

  auto codec = config->createCodec(connection, data, callbacks, overload_manager);
  EXPECT_NE(nullptr, codec);
  // A GOAWAY-observing wrapper is interposed, so the base codec sees the wrapper, not the raw
  // HCM callbacks.
  EXPECT_NE(&callbacks, config->last_callbacks_);

  // Firing the local drain runs the re-dial closure, which asks the (real) initiator IOHandle to
  // dial a replacement tunnel. With no tunnels tracked this is a safe no-op, but it exercises the
  // wired closure.
  codec->shutdownNotice();
}

TEST_F(DrainAwareConfigTest, DelaysRequireEnableDrainWithGoaway) {
  DrainAwareHttpConnectionManagerFilterConfigFactory factory;
  auto proto_config = parseConfig(absl::StrCat(kMinimalConfig, R"EOF(
shutdown_notice_delay: 2s
)EOF"));
  EXPECT_THAT(factory.createFilterFactoryFromProto(proto_config, context_),
              HasStatus(absl::StatusCode::kInvalidArgument,
                        HasSubstr("requires enable_drain_with_goaway")));
}

TEST_F(DrainAwareConfigTest, MetadataDelayRequiresAllowMetadata) {
  DrainAwareHttpConnectionManagerFilterConfigFactory factory;
  auto proto_config = parseConfig(absl::StrCat(kMinimalConfig, R"EOF(
enable_drain_with_goaway: true
metadata_delay: 2s
)EOF"));
  EXPECT_THAT(factory.createFilterFactoryFromProto(proto_config, context_),
              HasStatus(absl::StatusCode::kInvalidArgument, HasSubstr("allow_metadata")));
}

TEST_F(DrainAwareConfigTest, MetadataDelayMustBeLessThanDrainTimeout) {
  DrainAwareHttpConnectionManagerFilterConfigFactory factory;
  auto proto_config = parseConfig(absl::StrCat(kMinimalConfig, R"EOF(
enable_drain_with_goaway: true
metadata_delay: 5s
)EOF"));
  proto_config.mutable_hcm_config()->mutable_http2_protocol_options()->set_allow_metadata(true);
  EXPECT_THAT(factory.createFilterFactoryFromProto(proto_config, context_),
              HasStatus(absl::StatusCode::kInvalidArgument,
                        HasSubstr("metadata_delay must be less than hcm_config.drain_timeout")));
}

TEST_F(DrainAwareConfigTest, MetadataDelayMustBeLessThanShutdownNoticeDelay) {
  DrainAwareHttpConnectionManagerFilterConfigFactory factory;
  auto proto_config = parseConfig(absl::StrCat(kMinimalConfig, R"EOF(
enable_drain_with_goaway: true
metadata_delay: 3s
shutdown_notice_delay: 3s
)EOF"));
  proto_config.mutable_hcm_config()->mutable_http2_protocol_options()->set_allow_metadata(true);
  EXPECT_THAT(factory.createFilterFactoryFromProto(proto_config, context_),
              HasStatus(absl::StatusCode::kInvalidArgument,
                        HasSubstr("must be less than shutdown_notice_delay")));
}

// hcm_config.drain_timeout is unset here, so this also checks that validation uses the HCM's own
// 5s default for drain_timeout.
TEST_F(DrainAwareConfigTest, ShutdownNoticeDelayMustBeLessThanDrainTimeout) {
  DrainAwareHttpConnectionManagerFilterConfigFactory factory;
  auto proto_config = parseConfig(absl::StrCat(kMinimalConfig, R"EOF(
enable_drain_with_goaway: true
shutdown_notice_delay: 5s
)EOF"));
  EXPECT_THAT(
      factory.createFilterFactoryFromProto(proto_config, context_),
      HasStatus(absl::StatusCode::kInvalidArgument,
                HasSubstr("shutdown_notice_delay must be less than hcm_config.drain_timeout")));
}

// An hcm_config the base HCM rejects surfaces the HCM's own error. The drain delays are invalid as
// well (shutdown_notice_delay is not below the 5s default drain_timeout), but they are only
// validated once the base config is known to be valid.
TEST_F(DrainAwareConfigTest, InvalidBaseHcmConfigErrorTakesPrecedence) {
  DrainAwareHttpConnectionManagerFilterConfigFactory factory;
  auto proto_config = parseConfig(absl::StrCat(kMinimalConfig, R"EOF(
enable_drain_with_goaway: true
shutdown_notice_delay: 5s
)EOF"));
  proto_config.mutable_hcm_config()->mutable_max_request_headers_kb()->set_value(64);
  proto_config.mutable_hcm_config()
      ->mutable_http2_protocol_options()
      ->mutable_max_header_field_size_kb()
      ->set_value(128);
  EXPECT_THAT(
      factory.createFilterFactoryFromProto(proto_config, context_),
      HasStatus(absl::StatusCode::kInvalidArgument,
                HasSubstr("max_header_field_size_kb must not exceed max_request_headers_kb")));
}

TEST_F(DrainAwareConfigTest, ZeroDrainTimeoutWithoutDelaysIsValid) {
  DrainAwareHttpConnectionManagerFilterConfigFactory factory;
  auto proto_config = parseConfig(kMinimalConfig);
  proto_config.mutable_hcm_config()->mutable_drain_timeout()->set_seconds(0);
  EXPECT_THAT(factory.createFilterFactoryFromProto(proto_config, context_),
              IsOkAndHolds(::testing::NotNull()));
}

TEST_F(DrainAwareConfigTest, DelaysValidConfig) {
  DrainAwareHttpConnectionManagerFilterConfigFactory factory;
  auto proto_config = parseConfig(absl::StrCat(kMinimalConfig, R"EOF(
enable_drain_with_goaway: true
metadata_delay: 2s
shutdown_notice_delay: 3s
)EOF"));
  proto_config.mutable_hcm_config()->mutable_http2_protocol_options()->set_allow_metadata(true);
  EXPECT_THAT(factory.createFilterFactoryFromProto(proto_config, context_),
              IsOkAndHolds(::testing::NotNull()));
}

// Delays are validated against the unjittered drain_timeout: a 2s drain_timeout with 100% jitter,
// and a random value that would stretch it to 3.999s. The outcome must not depend on the jitter
// draw.
DrainAwareHcm jitteredDrainConfig(DrainAwareHcm proto_config,
                                  std::chrono::milliseconds shutdown_notice_delay) {
  proto_config.set_enable_drain_with_goaway(true);
  proto_config.mutable_hcm_config()->mutable_drain_timeout()->set_seconds(2);
  proto_config.mutable_hcm_config()->mutable_drain_timeout_jitter()->set_value(100.0);
  *proto_config.mutable_shutdown_notice_delay() =
      Protobuf::util::TimeUtil::MillisecondsToDuration(shutdown_notice_delay.count());
  return proto_config;
}

TEST_F(DrainAwareConfigTest, DelayEqualToUnjitteredDrainTimeoutIsRejected) {
  ON_CALL(context_.server_factory_context_.api_.random_, random()).WillByDefault(Return(1999));
  DrainAwareHttpConnectionManagerFilterConfigFactory factory;
  EXPECT_THAT(
      factory.createFilterFactoryFromProto(
          jitteredDrainConfig(parseConfig(kMinimalConfig), std::chrono::milliseconds(2000)),
          context_),
      HasStatus(absl::StatusCode::kInvalidArgument,
                HasSubstr("shutdown_notice_delay must be less than hcm_config.drain_timeout")));
}

TEST_F(DrainAwareConfigTest, DelayBelowUnjitteredDrainTimeoutIsAccepted) {
  ON_CALL(context_.server_factory_context_.api_.random_, random()).WillByDefault(Return(1999));
  DrainAwareHttpConnectionManagerFilterConfigFactory factory;
  EXPECT_THAT(factory.createFilterFactoryFromProto(
                  jitteredDrainConfig(parseConfig(kMinimalConfig), std::chrono::milliseconds(1999)),
                  context_),
              IsOkAndHolds(::testing::NotNull()));
}

// createCodec() draws the jittered drain_timeout for each connection, and the wrapper arms the
// final GOAWAY with it when it detects a listener drain. 2s with 50% jitter gives up to 1000ms of
// jitter; a random value of 500 gives exactly 2500ms.
TEST_F(DrainAwareConfigTest, CreateCodecArmsFinalGoAwayWithJitteredDrainTimeout) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues({{"envoy.reloadable_features.use_connection_event_drain", "false"}});
  ON_CALL(context_.server_factory_context_.api_.random_, random()).WillByDefault(Return(500));
  ON_CALL(context_.drain_manager_, drainClose(_)).WillByDefault(Return(true));

  auto proto_config = parseConfig(kMinimalConfig);
  proto_config.mutable_hcm_config()->mutable_drain_timeout()->set_seconds(2);
  proto_config.mutable_hcm_config()->mutable_drain_timeout_jitter()->set_value(50.0);
  auto config = makeConfig<FakeCodecDrainAwareConfig>(proto_config);

  // The wrapper creates the drain-check timer and then the final GOAWAY timer. gmock matches the
  // most recent expectation first, so register them in reverse.
  NiceMock<Network::MockConnection> connection;
  auto* goaway_timer = new Event::MockTimer(&connection.dispatcher_);
  auto* drain_check_timer = new Event::MockTimer(&connection.dispatcher_);
  EXPECT_CALL(*drain_check_timer, enableTimer(std::chrono::milliseconds(100), _));

  NiceMock<Http::MockServerConnectionCallbacks> callbacks;
  NiceMock<Server::MockOverloadManager> overload_manager;
  Buffer::OwnedImpl data;
  auto codec = config->createCodec(connection, data, callbacks, overload_manager);
  ASSERT_NE(nullptr, codec);

  EXPECT_CALL(*goaway_timer, enableTimer(std::chrono::milliseconds(2500), _));
  drain_check_timer->invokeCallback();

  EXPECT_CALL(*drain_check_timer, disableTimer());
  codec.reset();
}

} // namespace
} // namespace ReverseTunnel
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
