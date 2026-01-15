#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "envoy/config/accesslog/v3/accesslog.pb.h"
#include "envoy/extensions/access_loggers/file/v3/file.pb.h"
#include "envoy/extensions/filters/network/tcp_proxy/v3/tcp_proxy.pb.h"
#include "envoy/extensions/filters/network/tcp_proxy/v3/tcp_proxy.pb.validate.h"
#include "envoy/extensions/upstreams/http/generic/v3/generic_connection_pool.pb.h"
#include "envoy/extensions/upstreams/tcp/generic/v3/generic_connection_pool.pb.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/network/address_impl.h"
#include "source/common/network/application_protocol.h"
#include "source/common/network/proxy_protocol_filter_state.h"
#include "source/common/network/socket_option_factory.h"
#include "source/common/network/transport_socket_options_impl.h"
#include "source/common/network/upstream_server_name.h"
#include "source/common/network/upstream_socket_options_filter_state.h"
#include "source/common/network/win32_redirect_records_option_impl.h"
#include "source/common/router/metadatamatchcriteria_impl.h"
#include "source/common/router/string_accessor_impl.h"
#include "source/common/stream_info/bool_accessor_impl.h"
#include "source/common/stream_info/uint64_accessor_impl.h"
#include "source/common/tcp_proxy/tcp_proxy.h"
#include "source/common/upstream/upstream_impl.h"

#include "test/common/tcp_proxy/tcp_proxy_test_base.h"
#include "test/common/upstream/utility.h"
#include "test/mocks/buffer/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/server/factory_context.h"
#include "test/mocks/server/instance.h"
#include "test/mocks/ssl/mocks.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/mocks/tcp/mocks.h"
#include "test/mocks/upstream/cluster_discovery_callback_handle.h"
#include "test/mocks/upstream/host.h"
#include "test/mocks/upstream/od_cds_api_handle.h"
#include "test/test_common/test_runtime.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace TcpProxy {

namespace {

using ::testing::_;
using ::testing::DoAll;
using ::testing::Invoke;
using ::testing::InvokeWithoutArgs;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::ReturnPointee;
using ::testing::ReturnRef;
using ::testing::SaveArg;

class TcpProxyTest : public TcpProxyTestBase {
public:
  TcpProxyTest() {
    EXPECT_CALL(factory_context_.server_factory_context_.cluster_manager_.thread_local_cluster_,
                chooseHost(_))
        .WillRepeatedly(Invoke([this] {
          return Upstream::HostSelectionResponse{
              factory_context_.server_factory_context_.cluster_manager_.thread_local_cluster_.lb_
                  .host_};
        }));
  }
  using TcpProxyTestBase::setup;

  // Helper to set up filter for ON_DOWNSTREAM_DATA mode tests without calling setup().
  // This avoids conflicting expectations for IMMEDIATE mode.
  void setupOnDownstreamDataMode(
      const envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy& config,
      bool receive_before_connect = true) {
    configure(config);
    mock_access_logger_ = std::make_shared<NiceMock<AccessLog::MockInstance>>();
    const_cast<AccessLog::InstanceSharedPtrVector&>(config_->accessLogs())
        .push_back(mock_access_logger_);

    // Set up upstream connection data.
    upstream_connections_.push_back(std::make_unique<NiceMock<Network::MockClientConnection>>());
    upstream_connection_data_.push_back(
        std::make_unique<NiceMock<Tcp::ConnectionPool::MockConnectionData>>());
    ON_CALL(*upstream_connection_data_.back(), connection())
        .WillByDefault(ReturnRef(*upstream_connections_.back()));
    upstream_hosts_.push_back(std::make_shared<NiceMock<Upstream::MockHost>>());
    conn_pool_handles_.push_back(
        std::make_unique<NiceMock<Envoy::ConnectionPool::MockCancellable>>());
    ON_CALL(*upstream_hosts_.at(0), address())
        .WillByDefault(Return(*Network::Utility::resolveUrl("tcp://127.0.0.1:80")));
    EXPECT_CALL(*upstream_connections_.at(0), dispatcher())
        .WillRepeatedly(ReturnRef(filter_callbacks_.connection_.dispatcher_));

    // Set receive_before_connect filter state.
    filter_callbacks_.connection().streamInfo().filterState()->setData(
        TcpProxy::ReceiveBeforeConnectKey,
        std::make_unique<StreamInfo::BoolAccessorImpl>(receive_before_connect),
        StreamInfo::FilterState::StateType::ReadOnly,
        StreamInfo::FilterState::LifeSpan::Connection);

    // Create and initialize filter.
    filter_ = std::make_unique<Filter>(config_,
                                       factory_context_.server_factory_context_.cluster_manager_);
    EXPECT_CALL(filter_callbacks_.connection_, enableHalfClose(true));
    // For ON_DOWNSTREAM_DATA mode with receive_before_connect, readDisable is not called initially.
    filter_->initializeReadFilterCallbacks(filter_callbacks_);
    filter_callbacks_.connection_.stream_info_.downstream_connection_info_provider_
        ->setSslConnection(filter_callbacks_.connection_.ssl());
  }
  void setup(uint32_t connections, bool set_redirect_records, bool receive_before_connect,
             const envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy& config) override {
    if (config.has_on_demand()) {
      EXPECT_CALL(factory_context_.server_factory_context_.cluster_manager_,
                  allocateOdCdsApi(_, _, _, _))
          .WillOnce(
              Invoke([this]() { return Upstream::MockOdCdsApiHandlePtr(mock_odcds_api_handle_); }));
    }

    configure(config);
    mock_access_logger_ = std::make_shared<NiceMock<AccessLog::MockInstance>>();
    const_cast<AccessLog::InstanceSharedPtrVector&>(config_->accessLogs())
        .push_back(mock_access_logger_);
    upstream_local_address_ = *Network::Utility::resolveUrl("tcp://2.2.2.2:50000");
    upstream_remote_address_ = *Network::Utility::resolveUrl("tcp://127.0.0.1:80");
    for (uint32_t i = 0; i < connections; i++) {
      upstream_connections_.push_back(std::make_unique<NiceMock<Network::MockClientConnection>>());
      upstream_connection_data_.push_back(
          std::make_unique<NiceMock<Tcp::ConnectionPool::MockConnectionData>>());
      ON_CALL(*upstream_connection_data_.back(), connection())
          .WillByDefault(ReturnRef(*upstream_connections_.back()));
      upstream_hosts_.push_back(std::make_shared<NiceMock<Upstream::MockHost>>());
      conn_pool_handles_.push_back(
          std::make_unique<NiceMock<Envoy::ConnectionPool::MockCancellable>>());
      ON_CALL(*upstream_hosts_.at(i), address()).WillByDefault(Return(upstream_remote_address_));
      upstream_connections_.at(i)
          ->stream_info_.downstream_connection_info_provider_->setLocalAddress(
              upstream_local_address_);
      EXPECT_CALL(*upstream_connections_.at(i), dispatcher())
          .WillRepeatedly(ReturnRef(filter_callbacks_.connection_.dispatcher_));
    }

    {
      testing::InSequence sequence;
      for (uint32_t i = 0; i < connections; i++) {
        EXPECT_CALL(factory_context_.server_factory_context_.cluster_manager_.thread_local_cluster_,
                    tcpConnPool(_, _, _))
            .WillOnce(Return(Upstream::TcpPoolData([]() {}, &conn_pool_)))
            .RetiresOnSaturation();
        EXPECT_CALL(conn_pool_, newConnection(_))
            .WillOnce(Invoke(
                [=, this](Tcp::ConnectionPool::Callbacks& cb) -> Tcp::ConnectionPool::Cancellable* {
                  conn_pool_callbacks_.push_back(&cb);
                  return onNewConnection(conn_pool_handles_.at(i).get());
                }))
            .RetiresOnSaturation();
      }
      EXPECT_CALL(factory_context_.server_factory_context_.cluster_manager_.thread_local_cluster_,
                  tcpConnPool(_, _, _))
          .WillRepeatedly(Return(absl::nullopt));
    }

    {
      if (set_redirect_records) {
        auto redirect_records = std::make_shared<Network::Win32RedirectRecords>();
        memcpy(redirect_records->buf_, reinterpret_cast<void*>(redirect_records_data_.data()),
               redirect_records_data_.size());
        redirect_records->buf_size_ = redirect_records_data_.size();

        filter_callbacks_.connection_.streamInfo().filterState()->setData(
            Network::UpstreamSocketOptionsFilterState::key(),
            std::make_unique<Network::UpstreamSocketOptionsFilterState>(),
            StreamInfo::FilterState::StateType::Mutable,
            StreamInfo::FilterState::LifeSpan::Connection);
        filter_callbacks_.connection_.streamInfo()
            .filterState()
            ->getDataMutable<Network::UpstreamSocketOptionsFilterState>(
                Network::UpstreamSocketOptionsFilterState::key())
            ->addOption(
                Network::SocketOptionFactory::buildWFPRedirectRecordsOptions(*redirect_records));
      }

      filter_callbacks_.connection().streamInfo().filterState()->setData(
          TcpProxy::ReceiveBeforeConnectKey,
          std::make_unique<StreamInfo::BoolAccessorImpl>(receive_before_connect),
          StreamInfo::FilterState::StateType::ReadOnly,
          StreamInfo::FilterState::LifeSpan::Connection);

      filter_ = std::make_unique<Filter>(config_,
                                         factory_context_.server_factory_context_.cluster_manager_);
      EXPECT_CALL(filter_callbacks_.connection_, enableHalfClose(true));

      if (!receive_before_connect) {
        EXPECT_CALL(filter_callbacks_.connection_, readDisable(true));
      }

      filter_->initializeReadFilterCallbacks(filter_callbacks_);
      filter_callbacks_.connection_.stream_info_.downstream_connection_info_provider_
          ->setSslConnection(filter_callbacks_.connection_.ssl());
    }

    if (connections > 0) {
      auto expected_status_on_new_connection = receive_before_connect
                                                   ? Network::FilterStatus::Continue
                                                   : Network::FilterStatus::StopIteration;
      EXPECT_EQ(expected_status_on_new_connection, filter_->onNewConnection());
      EXPECT_EQ(absl::optional<uint64_t>(), filter_->computeHashKey());
      EXPECT_EQ(&filter_callbacks_.connection_, filter_->downstreamConnection());
      EXPECT_EQ(nullptr, filter_->metadataMatchCriteria());
    }
  }

  void set2Cluster(envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy& config) {
    auto* new_cluster = config.mutable_weighted_clusters()->add_clusters();
    *new_cluster->mutable_name() = "fake_cluster_0";
    new_cluster->set_weight(1);
    new_cluster = config.mutable_weighted_clusters()->add_clusters();
    *new_cluster->mutable_name() = "fake_cluster_1";
    new_cluster->set_weight(1);
  }

  envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy onDemandConfig() {
    auto config = defaultConfig();
    config.mutable_on_demand()->mutable_odcds_config();
    return config;
  }

  // Saved api handle pointer. The pointer is assigned in setup() in most of the on demand cases.
  // In these cases, the mocked allocateOdCdsApi() takes the ownership.
  Upstream::MockOdCdsApiHandle* mock_odcds_api_handle_{};
  std::shared_ptr<NiceMock<AccessLog::MockInstance>> mock_access_logger_;
};

TEST_P(TcpProxyTest, ExplicitCluster) {
  configure(defaultConfig());

  NiceMock<Network::MockConnection> connection;
  EXPECT_EQ(std::string("fake_cluster"), config_->getRouteFromEntries(connection)->clusterName());
}

// Tests that half-closes are proxied and don't themselves cause any connection to be closed.
TEST_P(TcpProxyTest, HalfCloseProxy) {
  setup(1);

  EXPECT_CALL(filter_callbacks_.connection_, close(_)).Times(0);
  EXPECT_CALL(*upstream_connections_.at(0), close(_)).Times(0);

  raiseEventUpstreamConnected(0);

  Buffer::OwnedImpl buffer("hello");
  EXPECT_CALL(*upstream_connections_.at(0), write(BufferEqual(&buffer), true));
  filter_->onData(buffer, true);

  Buffer::OwnedImpl response("world");
  EXPECT_CALL(filter_callbacks_.connection_, write(BufferEqual(&response), true));
  upstream_callbacks_->onUpstreamData(response, true);

  EXPECT_CALL(filter_callbacks_.connection_, close(_));
  upstream_callbacks_->onEvent(Network::ConnectionEvent::RemoteClose);
}

// Test with an explicitly configured upstream.
TEST_P(TcpProxyTest, ExplicitFactory) {
  // Explicitly configure an HTTP upstream, to test factory creation.
  auto& info = factory_context_.server_factory_context_.cluster_manager_.thread_local_cluster_
                   .cluster_.info_;
  info->upstream_config_ = std::make_unique<envoy::config::core::v3::TypedExtensionConfig>();
  envoy::extensions::upstreams::tcp::generic::v3::GenericConnectionPoolProto generic_config;
  info->upstream_config_->mutable_typed_config()->PackFrom(generic_config);
  setup(1);

  raiseEventUpstreamConnected(0);

  Buffer::OwnedImpl buffer("hello");
  EXPECT_CALL(*upstream_connections_.at(0), write(BufferEqual(&buffer), false));
  filter_->onData(buffer, false);

  Buffer::OwnedImpl response("world");
  EXPECT_CALL(filter_callbacks_.connection_, write(BufferEqual(&response), _));
  upstream_callbacks_->onUpstreamData(response, false);

  EXPECT_CALL(filter_callbacks_.connection_, close(_));
  upstream_callbacks_->onEvent(Network::ConnectionEvent::LocalClose);
}

// Test nothing bad happens if an invalid factory is configured.
TEST_P(TcpProxyTest, BadFactory) {
  auto& info = factory_context_.server_factory_context_.cluster_manager_.thread_local_cluster_
                   .cluster_.info_;
  info->upstream_config_ = std::make_unique<envoy::config::core::v3::TypedExtensionConfig>();
  // The HTTP Generic connection pool is not a valid type for TCP upstreams.
  envoy::extensions::upstreams::http::generic::v3::GenericConnectionPoolProto generic_config;
  info->upstream_config_->mutable_typed_config()->PackFrom(generic_config);

  envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy config = defaultConfig();

  configure(config);

  upstream_connections_.push_back(std::make_unique<NiceMock<Network::MockClientConnection>>());
  upstream_connection_data_.push_back(
      std::make_unique<NiceMock<Tcp::ConnectionPool::MockConnectionData>>());
  ON_CALL(*upstream_connection_data_.back(), connection())
      .WillByDefault(ReturnRef(*upstream_connections_.back()));
  upstream_hosts_.push_back(std::make_shared<NiceMock<Upstream::MockHost>>());
  conn_pool_handles_.push_back(
      std::make_unique<NiceMock<Envoy::ConnectionPool::MockCancellable>>());

  ON_CALL(*upstream_hosts_.at(0), cluster())
      .WillByDefault(ReturnPointee(factory_context_.server_factory_context_.cluster_manager_
                                       .thread_local_cluster_.cluster_.info_));
  EXPECT_CALL(*upstream_connections_.at(0), dispatcher())
      .WillRepeatedly(ReturnRef(filter_callbacks_.connection_.dispatcher_));

  filter_ =
      std::make_unique<Filter>(config_, factory_context_.server_factory_context_.cluster_manager_);
  EXPECT_CALL(filter_callbacks_.connection_, enableHalfClose(true));
  EXPECT_CALL(filter_callbacks_.connection_, readDisable(true));
  filter_->initializeReadFilterCallbacks(filter_callbacks_);
  filter_callbacks_.connection_.stream_info_.downstream_connection_info_provider_->setSslConnection(
      filter_callbacks_.connection_.ssl());
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onNewConnection());
}

// Test that downstream is closed after an upstream LocalClose.
TEST_P(TcpProxyTest, UpstreamLocalDisconnect) {
  setup(1);

  raiseEventUpstreamConnected(0);

  Buffer::OwnedImpl buffer("hello");
  EXPECT_CALL(*upstream_connections_.at(0), write(BufferEqual(&buffer), false));
  filter_->onData(buffer, false);

  Buffer::OwnedImpl response("world");
  EXPECT_CALL(filter_callbacks_.connection_, write(BufferEqual(&response), _));
  upstream_callbacks_->onUpstreamData(response, false);

  EXPECT_CALL(filter_callbacks_.connection_, close(_));
  upstream_callbacks_->onEvent(Network::ConnectionEvent::LocalClose);
}

// Test that downstream is closed after an upstream RemoteClose.
TEST_P(TcpProxyTest, UpstreamRemoteDisconnect) {
  setup(1);

  timeSystem().advanceTimeWait(std::chrono::microseconds(20));
  raiseEventUpstreamConnected(0);

  const absl::optional<std::chrono::nanoseconds> upstream_connection_establishment_latency =
      filter_->getStreamInfo().upstreamInfo()->upstreamTiming().connectionPoolCallbackLatency();
  ASSERT_TRUE(upstream_connection_establishment_latency.has_value());
  EXPECT_EQ(std::chrono::microseconds(20), upstream_connection_establishment_latency.value());

  Buffer::OwnedImpl buffer("hello");
  EXPECT_CALL(*upstream_connections_.at(0), write(BufferEqual(&buffer), false));
  filter_->onData(buffer, false);

  Buffer::OwnedImpl response("world");
  EXPECT_CALL(filter_callbacks_.connection_, write(BufferEqual(&response), _));
  upstream_callbacks_->onUpstreamData(response, false);

  EXPECT_CALL(filter_callbacks_.connection_, close(Network::ConnectionCloseType::FlushWrite));
  upstream_callbacks_->onEvent(Network::ConnectionEvent::RemoteClose);
}

// Test that reconnect is attempted after a local connect failure, backoff options not configured.
TEST_P(TcpProxyTest, ConnectAttemptsUpstreamLocalFailNoBackoffOptions) {
  envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy config = defaultConfig();
  config.mutable_idle_timeout()->set_seconds(0); // Disable idle timeout.
  config.mutable_max_connect_attempts()->set_value(2);

  Event::MockTimer* retry_timer = new Event::MockTimer(&filter_callbacks_.connection_.dispatcher_);
  setup(2, config);

  EXPECT_CALL(factory_context_.server_factory_context_.api_.random_, random()).Times(0);
  EXPECT_CALL(*retry_timer, enableTimer(std::chrono::milliseconds(0), _));

  timeSystem().advanceTimeWait(std::chrono::microseconds(10));
  raiseEventUpstreamConnectFailed(0, ConnectionPool::PoolFailureReason::LocalConnectionFailure);
  retry_timer->invokeCallback();

  timeSystem().advanceTimeWait(std::chrono::microseconds(40));
  raiseEventUpstreamConnected(1);

  EXPECT_EQ(0U, factory_context_.server_factory_context_.cluster_manager_.thread_local_cluster_
                    .cluster_.info_->stats_store_.counter("upstream_cx_connect_attempts_exceeded")
                    .value());
  EXPECT_EQ(2U, filter_->getStreamInfo().attemptCount().value());
  const absl::optional<std::chrono::nanoseconds> upstream_connection_establishment_latency =
      filter_->getStreamInfo().upstreamInfo()->upstreamTiming().connectionPoolCallbackLatency();
  ASSERT_TRUE(upstream_connection_establishment_latency.has_value());
  EXPECT_EQ(std::chrono::microseconds(50), upstream_connection_establishment_latency.value());

  EXPECT_CALL(*retry_timer, disableTimer());
}

// Test that reconnect is attempted after a local connect failure, backoff options configured.
TEST_P(TcpProxyTest, ConnectAttemptsUpstreamLocalFailWithBackoffOptions) {
  envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy config = defaultConfig();
  config.mutable_idle_timeout()->set_seconds(0); // Disable idle timeout.
  config.mutable_backoff_options()->mutable_base_interval()->set_seconds(1);
  config.mutable_max_connect_attempts()->set_value(2);

  Event::MockTimer* retry_timer = new Event::MockTimer(&filter_callbacks_.connection_.dispatcher_);
  setup(2, config);

  EXPECT_CALL(factory_context_.server_factory_context_.api_.random_, random())
      .WillOnce(Return(100));
  EXPECT_CALL(*retry_timer, enableTimer(std::chrono::milliseconds(100), _));

  timeSystem().advanceTimeWait(std::chrono::microseconds(10));
  raiseEventUpstreamConnectFailed(0, ConnectionPool::PoolFailureReason::LocalConnectionFailure);
  retry_timer->invokeCallback();

  timeSystem().advanceTimeWait(std::chrono::microseconds(40));
  raiseEventUpstreamConnected(1);

  EXPECT_EQ(0U, factory_context_.server_factory_context_.cluster_manager_.thread_local_cluster_
                    .cluster_.info_->stats_store_.counter("upstream_cx_connect_attempts_exceeded")
                    .value());
  EXPECT_EQ(2U, filter_->getStreamInfo().attemptCount().value());
  const absl::optional<std::chrono::nanoseconds> upstream_connection_establishment_latency =
      filter_->getStreamInfo().upstreamInfo()->upstreamTiming().connectionPoolCallbackLatency();
  ASSERT_TRUE(upstream_connection_establishment_latency.has_value());
  EXPECT_EQ(std::chrono::microseconds(50), upstream_connection_establishment_latency.value());

  EXPECT_CALL(*retry_timer, disableTimer());
}

// Make sure that the tcp proxy code handles reentrant calls to onPoolFailure, backoff options not
// configured.
TEST_P(TcpProxyTest, ConnectAttemptsUpstreamLocalFailReentrantNoBackoffOptions) {
  envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy config = defaultConfig();
  config.mutable_idle_timeout()->set_seconds(0); // Disable idle timeout.
  config.mutable_max_connect_attempts()->set_value(2);

  // Set up a call to onPoolFailure from inside the first newConnection call.
  // This simulates a connection failure from under the stack of newStream.
  new_connection_functions_.push_back(
      [&](Tcp::ConnectionPool::Cancellable*) -> Tcp::ConnectionPool::Cancellable* {
        raiseEventUpstreamConnectFailed(0,
                                        ConnectionPool::PoolFailureReason::LocalConnectionFailure);
        return nullptr;
      });

  Event::MockTimer* retry_timer = new Event::MockTimer(&filter_callbacks_.connection_.dispatcher_);

  EXPECT_CALL(factory_context_.server_factory_context_.api_.random_, random()).Times(0);
  EXPECT_CALL(*retry_timer, enableTimer(std::chrono::milliseconds(0), _));

  setup(2, config);

  // Make sure the last connection pool to be created is the one which gets the
  // cancellation call.
  EXPECT_CALL(*conn_pool_handles_.at(0), cancel(Tcp::ConnectionPool::CancelPolicy::CloseExcess))
      .Times(0);
  EXPECT_CALL(*conn_pool_handles_.at(1), cancel(Tcp::ConnectionPool::CancelPolicy::CloseExcess));

  retry_timer->invokeCallback();
  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);

  EXPECT_CALL(*retry_timer, disableTimer());
}

// Make sure that the tcp proxy code handles reentrant calls to onPoolFailure, backoff options
// configured.
TEST_P(TcpProxyTest, ConnectAttemptsUpstreamLocalFailReentrantWithBackoffOptions) {
  envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy config = defaultConfig();
  config.mutable_idle_timeout()->set_seconds(0); // Disable idle timeout.
  config.mutable_backoff_options()->mutable_base_interval()->set_seconds(1);
  config.mutable_max_connect_attempts()->set_value(2);

  // Set up a call to onPoolFailure from inside the first newConnection call.
  // This simulates a connection failure from under the stack of newStream.
  new_connection_functions_.push_back(
      [&](Tcp::ConnectionPool::Cancellable*) -> Tcp::ConnectionPool::Cancellable* {
        raiseEventUpstreamConnectFailed(0,
                                        ConnectionPool::PoolFailureReason::LocalConnectionFailure);
        return nullptr;
      });

  Event::MockTimer* retry_timer = new Event::MockTimer(&filter_callbacks_.connection_.dispatcher_);

  EXPECT_CALL(factory_context_.server_factory_context_.api_.random_, random())
      .WillOnce(Return(100));
  EXPECT_CALL(*retry_timer, enableTimer(std::chrono::milliseconds(100), _));

  setup(2, config);

  // Make sure the last connection pool to be created is the one which gets the
  // cancellation call.
  EXPECT_CALL(*conn_pool_handles_.at(0), cancel(Tcp::ConnectionPool::CancelPolicy::CloseExcess))
      .Times(0);
  EXPECT_CALL(*conn_pool_handles_.at(1), cancel(Tcp::ConnectionPool::CancelPolicy::CloseExcess));

  retry_timer->invokeCallback();
  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);

  EXPECT_CALL(*retry_timer, disableTimer());
}

// Test that reconnect is attempted after a remote connect failure, backoff options not configured.
TEST_P(TcpProxyTest, ConnectAttemptsUpstreamRemoteFailNoBackoffOptions) {
  envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy config = defaultConfig();
  config.mutable_idle_timeout()->set_seconds(0); // Disable idle timeout.
  config.mutable_max_connect_attempts()->set_value(2);

  Event::MockTimer* retry_timer = new Event::MockTimer(&filter_callbacks_.connection_.dispatcher_);
  setup(2, config);

  EXPECT_CALL(factory_context_.server_factory_context_.api_.random_, random()).Times(0);
  EXPECT_CALL(*retry_timer, enableTimer(std::chrono::milliseconds(0), _));

  raiseEventUpstreamConnectFailed(0, ConnectionPool::PoolFailureReason::RemoteConnectionFailure);
  retry_timer->invokeCallback();
  raiseEventUpstreamConnected(1);

  EXPECT_EQ(0U, factory_context_.server_factory_context_.cluster_manager_.thread_local_cluster_
                    .cluster_.info_->stats_store_.counter("upstream_cx_connect_attempts_exceeded")
                    .value());

  EXPECT_CALL(*retry_timer, disableTimer());
}

// Test that reconnect is attempted after a remote connect failure, backoff options configured.
TEST_P(TcpProxyTest, ConnectAttemptsUpstreamRemoteFailWithBackoffOptions) {
  envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy config = defaultConfig();
  config.mutable_idle_timeout()->set_seconds(0); // Disable idle timeout.
  config.mutable_backoff_options()->mutable_base_interval()->set_seconds(1);
  config.mutable_max_connect_attempts()->set_value(2);

  Event::MockTimer* retry_timer = new Event::MockTimer(&filter_callbacks_.connection_.dispatcher_);
  setup(2, config);

  EXPECT_CALL(factory_context_.server_factory_context_.api_.random_, random())
      .WillOnce(Return(100));
  EXPECT_CALL(*retry_timer, enableTimer(std::chrono::milliseconds(100), _));

  raiseEventUpstreamConnectFailed(0, ConnectionPool::PoolFailureReason::RemoteConnectionFailure);
  retry_timer->invokeCallback();
  raiseEventUpstreamConnected(1);

  EXPECT_EQ(0U, factory_context_.server_factory_context_.cluster_manager_.thread_local_cluster_
                    .cluster_.info_->stats_store_.counter("upstream_cx_connect_attempts_exceeded")
                    .value());

  EXPECT_CALL(*retry_timer, disableTimer());
}

// Test that reconnect is attempted after a connect timeout, backoff options not configured.
TEST_P(TcpProxyTest, ConnectAttemptsUpstreamTimeoutNoBackoffOptions) {
  envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy config = defaultConfig();
  config.mutable_idle_timeout()->set_seconds(0); // Disable idle timeout.
  config.mutable_max_connect_attempts()->set_value(2);

  Event::MockTimer* retry_timer = new Event::MockTimer(&filter_callbacks_.connection_.dispatcher_);
  setup(2, config);

  EXPECT_CALL(factory_context_.server_factory_context_.api_.random_, random()).Times(0);
  EXPECT_CALL(*retry_timer, enableTimer(std::chrono::milliseconds(0), _));

  raiseEventUpstreamConnectFailed(0, ConnectionPool::PoolFailureReason::Timeout);
  retry_timer->invokeCallback();
  raiseEventUpstreamConnected(1);

  EXPECT_EQ(0U, factory_context_.server_factory_context_.cluster_manager_.thread_local_cluster_
                    .cluster_.info_->stats_store_.counter("upstream_cx_connect_attempts_exceeded")
                    .value());

  EXPECT_CALL(*retry_timer, disableTimer());
}

// Test that reconnect is attempted after a connect timeout, backoff options configured.
TEST_P(TcpProxyTest, ConnectAttemptsUpstreamTimeoutWithBackoffOptions) {
  envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy config = defaultConfig();
  config.mutable_idle_timeout()->set_seconds(0); // Disable idle timeout.
  config.mutable_backoff_options()->mutable_base_interval()->set_seconds(1);
  config.mutable_max_connect_attempts()->set_value(2);

  Event::MockTimer* retry_timer = new Event::MockTimer(&filter_callbacks_.connection_.dispatcher_);
  setup(2, config);

  EXPECT_CALL(factory_context_.server_factory_context_.api_.random_, random())
      .WillOnce(Return(100));
  EXPECT_CALL(*retry_timer, enableTimer(std::chrono::milliseconds(100), _));

  raiseEventUpstreamConnectFailed(0, ConnectionPool::PoolFailureReason::Timeout);
  retry_timer->invokeCallback();
  raiseEventUpstreamConnected(1);

  EXPECT_EQ(0U, factory_context_.server_factory_context_.cluster_manager_.thread_local_cluster_
                    .cluster_.info_->stats_store_.counter("upstream_cx_connect_attempts_exceeded")
                    .value());

  EXPECT_CALL(*retry_timer, disableTimer());
}

// Test that only the configured number of connect attempts occur, backoff options not configured.
TEST_P(TcpProxyTest, ConnectAttemptsLimitNoBackoffOptions) {
  envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy config =
      accessLogConfig("%RESPONSE_FLAGS%");
  config.mutable_idle_timeout()->set_seconds(0); // Disable idle timeout.
  config.mutable_max_connect_attempts()->set_value(3);

  Event::MockTimer* retry_timer = new Event::MockTimer(&filter_callbacks_.connection_.dispatcher_);

  setup(3, config);

  EXPECT_CALL(upstream_hosts_.at(0)->outlier_detector_,
              putResult(Upstream::Outlier::Result::LocalOriginTimeout, _));
  EXPECT_CALL(upstream_hosts_.at(1)->outlier_detector_,
              putResult(Upstream::Outlier::Result::LocalOriginConnectFailed, _));
  EXPECT_CALL(upstream_hosts_.at(2)->outlier_detector_,
              putResult(Upstream::Outlier::Result::LocalOriginConnectFailed, _));

  EXPECT_CALL(filter_callbacks_.connection_, close(Network::ConnectionCloseType::NoFlush, _));

  // Try both failure modes
  EXPECT_CALL(factory_context_.server_factory_context_.api_.random_, random()).Times(0);
  EXPECT_CALL(*retry_timer, enableTimer(std::chrono::milliseconds(0), _));
  raiseEventUpstreamConnectFailed(0, ConnectionPool::PoolFailureReason::Timeout);
  retry_timer->invokeCallback();

  EXPECT_CALL(factory_context_.server_factory_context_.api_.random_, random()).Times(0);
  EXPECT_CALL(*retry_timer, enableTimer(std::chrono::milliseconds(0), _));
  timeSystem().advanceTimeWait(std::chrono::microseconds(10));
  raiseEventUpstreamConnectFailed(1, ConnectionPool::PoolFailureReason::RemoteConnectionFailure);
  retry_timer->invokeCallback();

  // This one should not enable the retry timer.
  timeSystem().advanceTimeWait(std::chrono::microseconds(15));
  raiseEventUpstreamConnectFailed(2, ConnectionPool::PoolFailureReason::RemoteConnectionFailure);

  const absl::optional<std::chrono::nanoseconds> upstream_connection_establishment_latency =
      filter_->getStreamInfo().upstreamInfo()->upstreamTiming().connectionPoolCallbackLatency();
  ASSERT_TRUE(upstream_connection_establishment_latency.has_value());
  EXPECT_EQ(std::chrono::microseconds(25), upstream_connection_establishment_latency.value());

  EXPECT_CALL(*retry_timer, disableTimer());
  filter_.reset();
  EXPECT_EQ(access_log_data_, "UF,URX");
}

// Test that only the configured number of connect attempts occur, backoff options configured.
TEST_P(TcpProxyTest, ConnectAttemptsLimitWithBackoffOptions) {
  envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy config =
      accessLogConfig("%RESPONSE_FLAGS%");
  config.mutable_idle_timeout()->set_seconds(0); // Disable idle timeout.
  config.mutable_backoff_options()->mutable_base_interval()->set_seconds(1);
  config.mutable_max_connect_attempts()->set_value(3);

  Event::MockTimer* retry_timer = new Event::MockTimer(&filter_callbacks_.connection_.dispatcher_);

  setup(3, config);

  EXPECT_CALL(upstream_hosts_.at(0)->outlier_detector_,
              putResult(Upstream::Outlier::Result::LocalOriginTimeout, _));
  EXPECT_CALL(upstream_hosts_.at(1)->outlier_detector_,
              putResult(Upstream::Outlier::Result::LocalOriginConnectFailed, _));
  EXPECT_CALL(upstream_hosts_.at(2)->outlier_detector_,
              putResult(Upstream::Outlier::Result::LocalOriginConnectFailed, _));

  EXPECT_CALL(filter_callbacks_.connection_, close(Network::ConnectionCloseType::NoFlush, _));

  // Try both failure modes
  EXPECT_CALL(factory_context_.server_factory_context_.api_.random_, random())
      .WillOnce(Return(100));
  EXPECT_CALL(*retry_timer, enableTimer(std::chrono::milliseconds(100), _));
  raiseEventUpstreamConnectFailed(0, ConnectionPool::PoolFailureReason::Timeout);
  retry_timer->invokeCallback();

  EXPECT_CALL(factory_context_.server_factory_context_.api_.random_, random())
      .WillOnce(Return(200));
  EXPECT_CALL(*retry_timer, enableTimer(std::chrono::milliseconds(200), _));
  timeSystem().advanceTimeWait(std::chrono::microseconds(10));
  raiseEventUpstreamConnectFailed(1, ConnectionPool::PoolFailureReason::RemoteConnectionFailure);
  retry_timer->invokeCallback();

  // This one should not enable the retry timer.
  timeSystem().advanceTimeWait(std::chrono::microseconds(15));
  raiseEventUpstreamConnectFailed(2, ConnectionPool::PoolFailureReason::RemoteConnectionFailure);

  const absl::optional<std::chrono::nanoseconds> upstream_connection_establishment_latency =
      filter_->getStreamInfo().upstreamInfo()->upstreamTiming().connectionPoolCallbackLatency();
  ASSERT_TRUE(upstream_connection_establishment_latency.has_value());
  EXPECT_EQ(std::chrono::microseconds(25), upstream_connection_establishment_latency.value());

  EXPECT_CALL(*retry_timer, disableTimer());
  filter_.reset();
  EXPECT_EQ(access_log_data_, "UF,URX");
}

TEST_P(TcpProxyTest, ConnectedNoOp) {
  setup(1);
  raiseEventUpstreamConnected(0);

  upstream_callbacks_->onEvent(Network::ConnectionEvent::Connected);

  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
}

// Test that the tcp proxy sends the correct notifications to the outlier detector, backoff options
// not configured.
TEST_P(TcpProxyTest, OutlierDetectionNoBackoffOptions) {
  envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy config = defaultConfig();
  config.mutable_idle_timeout()->set_seconds(0); // Disable idle timeout.
  config.mutable_max_connect_attempts()->set_value(3);

  Event::MockTimer* retry_timer = new Event::MockTimer(&filter_callbacks_.connection_.dispatcher_);
  setup(3, config);

  EXPECT_CALL(factory_context_.server_factory_context_.api_.random_, random()).Times(0);
  EXPECT_CALL(*retry_timer, enableTimer(std::chrono::milliseconds(0), _));
  EXPECT_CALL(upstream_hosts_.at(0)->outlier_detector_,
              putResult(Upstream::Outlier::Result::LocalOriginTimeout, _));
  raiseEventUpstreamConnectFailed(0, ConnectionPool::PoolFailureReason::Timeout);
  retry_timer->invokeCallback();

  EXPECT_CALL(factory_context_.server_factory_context_.api_.random_, random()).Times(0);
  EXPECT_CALL(*retry_timer, enableTimer(std::chrono::milliseconds(0), _));
  EXPECT_CALL(upstream_hosts_.at(1)->outlier_detector_,
              putResult(Upstream::Outlier::Result::LocalOriginConnectFailed, _));
  raiseEventUpstreamConnectFailed(1, ConnectionPool::PoolFailureReason::RemoteConnectionFailure);
  retry_timer->invokeCallback();

  EXPECT_CALL(upstream_hosts_.at(2)->outlier_detector_,
              putResult(Upstream::Outlier::Result::LocalOriginConnectSuccessFinal, _));
  raiseEventUpstreamConnected(2);

  EXPECT_CALL(*retry_timer, disableTimer());
}

// Test that the tcp proxy sends the correct notifications to the outlier detector, backoff options
// configured.
TEST_P(TcpProxyTest, OutlierDetectionWithBackoffOptions) {
  envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy config = defaultConfig();
  config.mutable_idle_timeout()->set_seconds(0); // Disable idle timeout.
  config.mutable_backoff_options()->mutable_base_interval()->set_seconds(1);
  config.mutable_max_connect_attempts()->set_value(3);

  Event::MockTimer* retry_timer = new Event::MockTimer(&filter_callbacks_.connection_.dispatcher_);
  setup(3, config);

  EXPECT_CALL(factory_context_.server_factory_context_.api_.random_, random())
      .WillOnce(Return(100));
  EXPECT_CALL(*retry_timer, enableTimer(std::chrono::milliseconds(100), _));
  EXPECT_CALL(upstream_hosts_.at(0)->outlier_detector_,
              putResult(Upstream::Outlier::Result::LocalOriginTimeout, _));
  raiseEventUpstreamConnectFailed(0, ConnectionPool::PoolFailureReason::Timeout);
  retry_timer->invokeCallback();

  EXPECT_CALL(factory_context_.server_factory_context_.api_.random_, random())
      .WillOnce(Return(200));
  EXPECT_CALL(*retry_timer, enableTimer(std::chrono::milliseconds(200), _));
  EXPECT_CALL(upstream_hosts_.at(1)->outlier_detector_,
              putResult(Upstream::Outlier::Result::LocalOriginConnectFailed, _));
  raiseEventUpstreamConnectFailed(1, ConnectionPool::PoolFailureReason::RemoteConnectionFailure);
  retry_timer->invokeCallback();

  EXPECT_CALL(upstream_hosts_.at(2)->outlier_detector_,
              putResult(Upstream::Outlier::Result::LocalOriginConnectSuccessFinal, _));
  raiseEventUpstreamConnected(2);

  EXPECT_CALL(*retry_timer, disableTimer());
}

TEST_P(TcpProxyTest, UpstreamDisconnectDownstreamFlowControl) {
  setup(1);

  raiseEventUpstreamConnected(0);

  Buffer::OwnedImpl buffer("hello");
  EXPECT_CALL(*upstream_connections_.at(0), write(BufferEqual(&buffer), _));
  filter_->onData(buffer, false);

  Buffer::OwnedImpl response("world");
  EXPECT_CALL(filter_callbacks_.connection_, write(BufferEqual(&response), _));
  upstream_callbacks_->onUpstreamData(response, false);

  EXPECT_CALL(*upstream_connections_.at(0), readDisable(true));
  filter_callbacks_.connection_.runHighWatermarkCallbacks();

  EXPECT_CALL(filter_callbacks_.connection_, close(Network::ConnectionCloseType::FlushWrite));
  upstream_callbacks_->onEvent(Network::ConnectionEvent::RemoteClose);

  filter_callbacks_.connection_.runLowWatermarkCallbacks();
}

TEST_P(TcpProxyTest, ReceiveBeforeConnectBuffersOnEarlyData) {
  setup(/*connections=*/1, /*set_redirect_records=*/false, /*receive_before_connect=*/true);
  std::string early_data("early data");
  Buffer::OwnedImpl early_data_buffer(early_data);

  // Check that the early data is buffered and flushed to upstream when connection is established.
  // Also check that downstream connection is read disabled.
  EXPECT_CALL(*upstream_connections_.at(0), write(_, _)).Times(0);
  EXPECT_CALL(filter_callbacks_.connection_, readDisable(true));
  filter_->onData(early_data_buffer, /*end_stream=*/false);

  // Now when upstream connection is established, early buffer will be sent.
  EXPECT_CALL(*upstream_connections_.at(0), write(BufferStringEqual(early_data), false));
  raiseEventUpstreamConnected(/*conn_index=*/0);

  // Any further communications between client and server can resume normally.
  Buffer::OwnedImpl buffer("hello");
  EXPECT_CALL(*upstream_connections_.at(0), write(BufferEqual(&buffer), _));
  filter_->onData(buffer, false);

  Buffer::OwnedImpl response("world");
  EXPECT_CALL(filter_callbacks_.connection_, write(BufferEqual(&response), _));
  upstream_callbacks_->onUpstreamData(response, false);
}

TEST_P(TcpProxyTest, ReceiveBeforeConnectEarlyDataWithEndStream) {
  setup(/*connections=*/1, /*set_redirect_records=*/false, /*receive_before_connect=*/true);
  std::string early_data("early data");
  Buffer::OwnedImpl early_data_buffer(early_data);

  // Early data is sent and downstream connection has indicated end of stream.
  EXPECT_CALL(*upstream_connections_.at(0), write(_, _)).Times(0);
  EXPECT_CALL(filter_callbacks_.connection_, readDisable(true));
  filter_->onData(early_data_buffer, /*end_stream=*/true);

  // Now when upstream connection is established, early buffer will be sent.
  EXPECT_CALL(*upstream_connections_.at(0),
              write(BufferStringEqual(early_data), /*end_stream*/ true));
  raiseEventUpstreamConnected(/*conn_index=*/0);

  // Any further communications between client and server can resume normally.
  Buffer::OwnedImpl response("hello");
  EXPECT_CALL(filter_callbacks_.connection_, write(BufferEqual(&response), _));
  upstream_callbacks_->onUpstreamData(response, false);
}

// Test that when downstream closes without sending any data before upstream connection is
// established, the end_stream signal is properly propagated to upstream.
// This prevents upstream connection leaks.
TEST_P(TcpProxyTest, ReceiveBeforeConnectDownstreamClosesWithoutData) {
  setup(/*connections=*/1, /*set_redirect_records=*/false, /*receive_before_connect=*/true);

  // Downstream closes without sending any data.
  Buffer::OwnedImpl empty_buffer;
  EXPECT_CALL(*upstream_connections_.at(0), write(_, _)).Times(0);
  filter_->onData(empty_buffer, /*end_stream=*/true);

  // When upstream connection is established, the end_stream signal should be sent even though
  // the buffer is empty. This ensures the upstream connection is properly closed.
  EXPECT_CALL(*upstream_connections_.at(0), write(BufferStringEqual(""), /*end_stream*/ true));
  raiseEventUpstreamConnected(/*conn_index=*/0);
}

// Test that when downstream sends empty buffer with end_stream before upstream is connected,
// the end_stream is properly handled.
TEST_P(TcpProxyTest, ReceiveBeforeConnectEmptyBufferWithEndStream) {
  setup(/*connections=*/1, /*set_redirect_records=*/false, /*receive_before_connect=*/true);

  // Downstream sends empty data with end_stream set.
  Buffer::OwnedImpl empty_buffer;
  EXPECT_CALL(*upstream_connections_.at(0), write(_, _)).Times(0);
  filter_->onData(empty_buffer, /*end_stream=*/true);

  // When upstream connection is established, end_stream should be propagated.
  EXPECT_CALL(*upstream_connections_.at(0), write(BufferStringEqual(""), /*end_stream*/ true));
  raiseEventUpstreamConnected(/*conn_index=*/0);

  // Upstream can still send data back.
  Buffer::OwnedImpl response("response data");
  EXPECT_CALL(filter_callbacks_.connection_, write(BufferEqual(&response), _));
  upstream_callbacks_->onUpstreamData(response, false);
}

TEST_P(TcpProxyTest, ReceiveBeforeConnectNoEarlyData) {
  setup(1, /*set_redirect_records=*/false, /*receive_before_connect=*/true);
  raiseEventUpstreamConnected(/*conn_index=*/0, /*expect_read_enable=*/false);

  // Any data sent after upstream connection is established is flushed directly to upstream,
  // and downstream connection is not read disabled.
  Buffer::OwnedImpl buffer("hello");
  EXPECT_CALL(filter_callbacks_.connection_, readDisable(_)).Times(0);
  EXPECT_CALL(*upstream_connections_.at(0), write(BufferEqual(&buffer), _));
  filter_->onData(buffer, /*end_stream=*/false);

  Buffer::OwnedImpl response("world");
  EXPECT_CALL(filter_callbacks_.connection_, write(BufferEqual(&response), _));
  upstream_callbacks_->onUpstreamData(response, false);
}

TEST_P(TcpProxyTest, ReceiveBeforeConnectSetToFalse) {
  setup(1, /*set_redirect_records=*/false, /*receive_before_connect=*/false);
  raiseEventUpstreamConnected(/*conn_index=*/0, /*expect_read_enable=*/true);

  // Any further communications between client and server can resume normally.
  Buffer::OwnedImpl buffer("hello");
  EXPECT_CALL(*upstream_connections_.at(0), write(BufferEqual(&buffer), _));
  filter_->onData(buffer, false);

  Buffer::OwnedImpl response("world");
  EXPECT_CALL(filter_callbacks_.connection_, write(BufferEqual(&response), _));
  upstream_callbacks_->onUpstreamData(response, false);
}

TEST_P(TcpProxyTest, DownstreamDisconnectRemote) {
  setup(1);

  raiseEventUpstreamConnected(0);

  Buffer::OwnedImpl buffer("hello");
  EXPECT_CALL(*upstream_connections_.at(0), write(BufferEqual(&buffer), _));
  filter_->onData(buffer, false);

  Buffer::OwnedImpl response("world");
  EXPECT_CALL(filter_callbacks_.connection_, write(BufferEqual(&response), _));
  upstream_callbacks_->onUpstreamData(response, false);

  EXPECT_CALL(*upstream_connections_.at(0), close(Network::ConnectionCloseType::FlushWrite, _));
  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
}

TEST_P(TcpProxyTest, DownstreamDisconnectLocal) {
  setup(1);

  raiseEventUpstreamConnected(0);

  Buffer::OwnedImpl buffer("hello");
  EXPECT_CALL(*upstream_connections_.at(0), write(BufferEqual(&buffer), _));
  filter_->onData(buffer, false);

  Buffer::OwnedImpl response("world");
  EXPECT_CALL(filter_callbacks_.connection_, write(BufferEqual(&response), _));
  upstream_callbacks_->onUpstreamData(response, false);

  EXPECT_CALL(*upstream_connections_.at(0), close(Network::ConnectionCloseType::NoFlush, _));
  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::LocalClose);
}

TEST_P(TcpProxyTest, UpstreamConnectTimeout) {
  setup(1, accessLogConfig("%RESPONSE_FLAGS%"));

  EXPECT_CALL(filter_callbacks_.connection_, close(Network::ConnectionCloseType::NoFlush, _));
  raiseEventUpstreamConnectFailed(0, ConnectionPool::PoolFailureReason::Timeout);

  filter_.reset();
  EXPECT_EQ(access_log_data_, "UF,URX");
}

TEST_P(TcpProxyTest, UpstreamClusterNotFound) {
  setup(0, accessLogConfig("%RESPONSE_FLAGS%"));

  EXPECT_CALL(factory_context_.server_factory_context_.cluster_manager_, getThreadLocalCluster(_))
      .WillRepeatedly(Return(nullptr));
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onNewConnection());

  filter_.reset();
  EXPECT_EQ(access_log_data_.value(), "NC");
}

TEST_P(TcpProxyTest, NoHost) {
  EXPECT_CALL(filter_callbacks_.connection_, close(Network::ConnectionCloseType::NoFlush, _));
  setup(0, accessLogConfig("%RESPONSE_FLAGS%"));
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onNewConnection());
  filter_.reset();
  EXPECT_EQ(access_log_data_, "UH");
}

// Tests StreamDecoderFilterCallbacks interface implementation
TEST_P(TcpProxyTest, StreamDecoderFilterCallbacks) {
  envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy config =
      accessLogConfig("%RESPONSE_FLAGS%");
  config.mutable_tunneling_config()->set_hostname("www.example.com");
  configure(config);
  NiceMock<Upstream::MockThreadLocalCluster> thread_local_cluster_;
  auto cluster_info = std::make_shared<NiceMock<Upstream::MockClusterInfo>>();
  // EXPECT_CALL(factory_context_.serverFactoryContext().clusterManager(), getThreadLocalCluster(_))
  //     .WillRepeatedly(Return(&thread_local_cluster_));
  EXPECT_CALL(thread_local_cluster_, info()).WillRepeatedly(Return(cluster_info));
  filter_ =
      std::make_unique<Filter>(config_, factory_context_.serverFactoryContext().clusterManager());
  filter_->initializeReadFilterCallbacks(filter_callbacks_);
  auto stream_decoder_callbacks = Filter::HttpStreamDecoderFilterCallbacks(filter_.get());
  EXPECT_NO_THROW(stream_decoder_callbacks.streamId());
  EXPECT_NO_THROW(stream_decoder_callbacks.connection());
  EXPECT_NO_THROW(stream_decoder_callbacks.dispatcher());
  EXPECT_ENVOY_BUG(
      { stream_decoder_callbacks.resetStream(Http::StreamResetReason::RemoteReset, ""); },
      "Not implemented");
  EXPECT_NO_THROW(stream_decoder_callbacks.streamInfo());
  EXPECT_NO_THROW(stream_decoder_callbacks.scope());
  EXPECT_NO_THROW(stream_decoder_callbacks.route());
  EXPECT_NO_THROW(stream_decoder_callbacks.continueDecoding());
  EXPECT_NO_THROW(stream_decoder_callbacks.requestHeaders());
  EXPECT_NO_THROW(stream_decoder_callbacks.requestTrailers());
  EXPECT_NO_THROW(stream_decoder_callbacks.responseHeaders());
  EXPECT_NO_THROW(stream_decoder_callbacks.responseTrailers());
  EXPECT_NO_THROW(stream_decoder_callbacks.encodeMetadata(nullptr));
  EXPECT_NO_THROW(stream_decoder_callbacks.onDecoderFilterAboveWriteBufferHighWatermark());
  EXPECT_NO_THROW(stream_decoder_callbacks.onDecoderFilterBelowWriteBufferLowWatermark());
  EXPECT_NO_THROW(stream_decoder_callbacks.setBufferLimit(uint32_t{0}));
  EXPECT_NO_THROW(stream_decoder_callbacks.bufferLimit());
  EXPECT_NO_THROW(stream_decoder_callbacks.recreateStream(nullptr));
  EXPECT_NO_THROW(stream_decoder_callbacks.getUpstreamSocketOptions());
  Network::Socket::OptionsSharedPtr sock_options =
      Network::SocketOptionFactory::buildIpTransparentOptions();
  EXPECT_NO_THROW(stream_decoder_callbacks.addUpstreamSocketOptions(sock_options));
  EXPECT_NO_THROW(stream_decoder_callbacks.mostSpecificPerFilterConfig());
  EXPECT_NO_THROW(stream_decoder_callbacks.account());
  EXPECT_NO_THROW(stream_decoder_callbacks.setUpstreamOverrideHost(
      Upstream::LoadBalancerContext::OverrideHost(std::make_pair("foo", true))));
  EXPECT_NO_THROW(stream_decoder_callbacks.http1StreamEncoderOptions());
  EXPECT_NO_THROW(stream_decoder_callbacks.downstreamCallbacks());
  EXPECT_NO_THROW(stream_decoder_callbacks.upstreamCallbacks());
  EXPECT_NO_THROW(stream_decoder_callbacks.upstreamOverrideHost());
  EXPECT_NO_THROW(stream_decoder_callbacks.resetIdleTimer());
  EXPECT_NO_THROW(stream_decoder_callbacks.filterConfigName());
  EXPECT_NO_THROW(stream_decoder_callbacks.activeSpan());
  EXPECT_NO_THROW(stream_decoder_callbacks.tracingConfig());
  Buffer::OwnedImpl inject_data;
  EXPECT_NO_THROW(stream_decoder_callbacks.addDecodedData(inject_data, false));
  EXPECT_NO_THROW(stream_decoder_callbacks.injectDecodedDataToFilterChain(inject_data, false));
  EXPECT_NO_THROW(stream_decoder_callbacks.addDecodedData(inject_data, false));
  EXPECT_NO_THROW(stream_decoder_callbacks.addDecodedTrailers());
  EXPECT_NO_THROW(stream_decoder_callbacks.addDecodedMetadata());
  EXPECT_NO_THROW(stream_decoder_callbacks.decodingBuffer());
  auto func = [](Buffer::Instance&) {};
  EXPECT_NO_THROW(stream_decoder_callbacks.modifyDecodingBuffer(func));
  EXPECT_NO_THROW(stream_decoder_callbacks.encode1xxHeaders(nullptr));
  EXPECT_NO_THROW(stream_decoder_callbacks.informationalHeaders());
  EXPECT_NO_THROW(stream_decoder_callbacks.encodeHeaders(nullptr, false, ""));
  EXPECT_NO_THROW(stream_decoder_callbacks.encodeData(inject_data, false));
  EXPECT_NO_THROW(stream_decoder_callbacks.encodeTrailers(nullptr));
  EXPECT_NO_THROW(stream_decoder_callbacks.setBufferLimit(0));
  std::array<char, 256> buffer;
  OutputBufferStream ostream{buffer.data(), buffer.size()};
  EXPECT_NO_THROW(stream_decoder_callbacks.dumpState(ostream, 0));

  // Release filter explicitly. Filter destructor tries to use access logger, so we want filter
  // to be destroyed before the access logger to avoid accessing released memory.
  filter_.reset();
}

TEST_P(TcpProxyTest, RouteWithMetadataMatch) {
  auto v1 = Protobuf::Value();
  v1.set_string_value("v1");
  auto v2 = Protobuf::Value();
  v2.set_number_value(2.0);
  auto v3 = Protobuf::Value();
  v3.set_bool_value(true);

  std::vector<Router::MetadataMatchCriterionImpl> criteria = {{"a", v1}, {"b", v2}, {"c", v3}};

  auto metadata_struct = Protobuf::Struct();
  auto mutable_fields = metadata_struct.mutable_fields();

  for (const auto& criterion : criteria) {
    mutable_fields->insert({criterion.name(), criterion.value().value()});
  }

  envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy config = defaultConfig();
  config.mutable_metadata_match()->mutable_filter_metadata()->insert(
      {Envoy::Config::MetadataFilters::get().ENVOY_LB, metadata_struct});

  configure(config);
  filter_ =
      std::make_unique<Filter>(config_, factory_context_.server_factory_context_.cluster_manager_);
  filter_->initializeReadFilterCallbacks(filter_callbacks_);
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onNewConnection());

  const auto effective_criteria = filter_->metadataMatchCriteria();
  EXPECT_NE(nullptr, effective_criteria);

  const auto& effective_criterions = effective_criteria->metadataMatchCriteria();
  EXPECT_EQ(effective_criterions.size(), criteria.size());
  for (size_t i = 0; i < criteria.size(); ++i) {
    EXPECT_EQ(effective_criterions[i]->name(), criteria[i].name());
    EXPECT_EQ(effective_criterions[i]->value(), criteria[i].value());
  }
}

// Tests that the endpoint selector of a weighted cluster gets included into the
// LoadBalancerContext.
TEST_P(TcpProxyTest, WeightedClusterWithMetadataMatch) {
  const std::string yaml = R"EOF(
  stat_prefix: name
  weighted_clusters:
    clusters:
    - name: cluster1
      weight: 1
      metadata_match:
        filter_metadata:
          envoy.lb:
            k1: v1
    - name: cluster2
      weight: 2
      metadata_match:
        filter_metadata:
          envoy.lb:
            k2: v2
  metadata_match:
    filter_metadata:
      envoy.lb:
        k0: v0
)EOF";

  factory_context_.server_factory_context_.cluster_manager_.initializeThreadLocalClusters(
      {"cluster1", "cluster2"});
  config_ = std::make_shared<Config>(constructConfigFromYaml(yaml, factory_context_));

  Protobuf::Value v0, v1, v2;
  v0.set_string_value("v0");
  v1.set_string_value("v1");
  v2.set_string_value("v2");
  HashedValue hv0(v0), hv1(v1), hv2(v2);

  // Expect filter to try to open a connection to cluster1.
  {
    NiceMock<Network::MockReadFilterCallbacks> filter_callbacks;
    filter_ = std::make_unique<Filter>(config_,
                                       factory_context_.server_factory_context_.cluster_manager_);
    filter_->initializeReadFilterCallbacks(filter_callbacks);

    Upstream::LoadBalancerContext* context;

    EXPECT_CALL(factory_context_.server_factory_context_.api_.random_, random())
        .WillOnce(Return(0));
    EXPECT_CALL(factory_context_.server_factory_context_.cluster_manager_.thread_local_cluster_,
                tcpConnPool(_, _, _))
        .WillOnce(DoAll(SaveArg<2>(&context), Return(absl::nullopt)));
    EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onNewConnection());

    EXPECT_NE(nullptr, context);

    const auto effective_criteria = context->metadataMatchCriteria();
    EXPECT_NE(nullptr, effective_criteria);

    const auto& effective_criterions = effective_criteria->metadataMatchCriteria();
    EXPECT_EQ(2, effective_criterions.size());

    EXPECT_EQ("k0", effective_criterions[0]->name());
    EXPECT_EQ(hv0, effective_criterions[0]->value());

    EXPECT_EQ("k1", effective_criterions[1]->name());
    EXPECT_EQ(hv1, effective_criterions[1]->value());
  }

  // Expect filter to try to open a connection to cluster2.
  {
    NiceMock<Network::MockReadFilterCallbacks> filter_callbacks;
    filter_ = std::make_unique<Filter>(config_,
                                       factory_context_.server_factory_context_.cluster_manager_);
    filter_->initializeReadFilterCallbacks(filter_callbacks);

    Upstream::LoadBalancerContext* context;

    EXPECT_CALL(factory_context_.server_factory_context_.api_.random_, random())
        .WillOnce(Return(2));
    EXPECT_CALL(factory_context_.server_factory_context_.cluster_manager_.thread_local_cluster_,
                tcpConnPool(_, _, _))
        .WillOnce(DoAll(SaveArg<2>(&context), Return(absl::nullopt)));
    EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onNewConnection());

    EXPECT_NE(nullptr, context);

    const auto effective_criteria = context->metadataMatchCriteria();
    EXPECT_NE(nullptr, effective_criteria);

    const auto& effective_criterions = effective_criteria->metadataMatchCriteria();
    EXPECT_EQ(2, effective_criterions.size());

    EXPECT_EQ("k0", effective_criterions[0]->name());
    EXPECT_EQ(hv0, effective_criterions[0]->value());

    EXPECT_EQ("k2", effective_criterions[1]->name());
    EXPECT_EQ(hv2, effective_criterions[1]->value());
  }
}

// Test that metadata match criteria provided on the StreamInfo is used.
TEST_P(TcpProxyTest, StreamInfoDynamicMetadata) {
  configure(defaultConfig());

  Protobuf::Value val;
  val.set_string_value("val");

  envoy::config::core::v3::Metadata metadata;
  Protobuf::Struct& map =
      (*metadata.mutable_filter_metadata())[Envoy::Config::MetadataFilters::get().ENVOY_LB];
  (*map.mutable_fields())["test"] = val;
  EXPECT_CALL(filter_callbacks_.connection_.stream_info_, dynamicMetadata())
      .WillRepeatedly(ReturnRef(metadata));

  filter_ =
      std::make_unique<Filter>(config_, factory_context_.server_factory_context_.cluster_manager_);
  filter_->initializeReadFilterCallbacks(filter_callbacks_);

  Upstream::LoadBalancerContext* context;

  EXPECT_CALL(factory_context_.server_factory_context_.cluster_manager_.thread_local_cluster_,
              tcpConnPool(_, _, _))
      .WillOnce(DoAll(SaveArg<2>(&context), Return(absl::nullopt)));
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onNewConnection());

  EXPECT_NE(nullptr, context);

  const auto effective_criteria = context->metadataMatchCriteria();
  EXPECT_NE(nullptr, effective_criteria);

  const auto& effective_criterions = effective_criteria->metadataMatchCriteria();
  EXPECT_EQ(1, effective_criterions.size());

  EXPECT_EQ("test", effective_criterions[0]->name());
  EXPECT_EQ(HashedValue(val), effective_criterions[0]->value());
}

// Test that if both streamInfo and configuration add metadata match criteria, they
// are merged.
TEST_P(TcpProxyTest, StreamInfoDynamicMetadataAndConfigMerged) {
  const std::string yaml = R"EOF(
  stat_prefix: name
  weighted_clusters:
    clusters:
    - name: cluster1
      weight: 1
      metadata_match:
        filter_metadata:
          envoy.lb:
            k0: v0
            k1: from_config
)EOF";

  factory_context_.server_factory_context_.cluster_manager_.initializeThreadLocalClusters(
      {"cluster1"});
  config_ = std::make_shared<Config>(constructConfigFromYaml(yaml, factory_context_));

  Protobuf::Value v0, v1, v2;
  v0.set_string_value("v0");
  v1.set_string_value("from_streaminfo"); // 'v1' is overridden with this value by streamInfo.
  v2.set_string_value("v2");
  HashedValue hv0(v0), hv1(v1), hv2(v2);

  envoy::config::core::v3::Metadata metadata;
  Protobuf::Struct& map =
      (*metadata.mutable_filter_metadata())[Envoy::Config::MetadataFilters::get().ENVOY_LB];
  (*map.mutable_fields())["k1"] = v1;
  (*map.mutable_fields())["k2"] = v2;
  EXPECT_CALL(filter_callbacks_.connection_.stream_info_, dynamicMetadata())
      .WillRepeatedly(ReturnRef(metadata));

  filter_ =
      std::make_unique<Filter>(config_, factory_context_.server_factory_context_.cluster_manager_);
  filter_->initializeReadFilterCallbacks(filter_callbacks_);

  Upstream::LoadBalancerContext* context;

  EXPECT_CALL(factory_context_.server_factory_context_.cluster_manager_.thread_local_cluster_,
              tcpConnPool(_, _, _))
      .WillOnce(DoAll(SaveArg<2>(&context), Return(absl::nullopt)));
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onNewConnection());

  EXPECT_NE(nullptr, context);

  const auto effective_criteria = context->metadataMatchCriteria();
  EXPECT_NE(nullptr, effective_criteria);

  const auto& effective_criterions = effective_criteria->metadataMatchCriteria();
  EXPECT_EQ(3, effective_criterions.size());

  EXPECT_EQ("k0", effective_criterions[0]->name());
  EXPECT_EQ(hv0, effective_criterions[0]->value());

  EXPECT_EQ("k1", effective_criterions[1]->name());
  EXPECT_EQ(hv1, effective_criterions[1]->value());

  EXPECT_EQ("k2", effective_criterions[2]->name());
  EXPECT_EQ(hv2, effective_criterions[2]->value());
}

TEST_P(TcpProxyTest, DisconnectBeforeData) {
  configure(defaultConfig());
  filter_ =
      std::make_unique<Filter>(config_, factory_context_.server_factory_context_.cluster_manager_);
  filter_->initializeReadFilterCallbacks(filter_callbacks_);

  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
}

// Test that if the downstream connection is closed before the upstream connection
// is established, the upstream connection is cancelled.
TEST_P(TcpProxyTest, RemoteClosedBeforeUpstreamConnected) {
  setup(1);
  EXPECT_CALL(*conn_pool_handles_.at(0), cancel(Tcp::ConnectionPool::CancelPolicy::CloseExcess));
  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
}

// Test that if the downstream connection is closed before the upstream connection
// is established, the upstream connection is cancelled.
TEST_P(TcpProxyTest, LocalClosedBeforeUpstreamConnected) {
  setup(1);
  EXPECT_CALL(*conn_pool_handles_.at(0), cancel(Tcp::ConnectionPool::CancelPolicy::CloseExcess));
  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::LocalClose);
}

TEST_P(TcpProxyTest, UpstreamConnectFailure) {
  setup(1, accessLogConfig("%RESPONSE_FLAGS%"));

  EXPECT_CALL(filter_callbacks_.connection_, close(Network::ConnectionCloseType::NoFlush, _));
  timeSystem().advanceTimeWait(std::chrono::microseconds(20));
  raiseEventUpstreamConnectFailed(0, ConnectionPool::PoolFailureReason::RemoteConnectionFailure);

  const absl::optional<std::chrono::nanoseconds> upstream_connection_establishment_latency =
      filter_->getStreamInfo().upstreamInfo()->upstreamTiming().connectionPoolCallbackLatency();
  ASSERT_TRUE(upstream_connection_establishment_latency.has_value());
  EXPECT_EQ(std::chrono::microseconds(20), upstream_connection_establishment_latency.value());

  filter_.reset();
  EXPECT_EQ(access_log_data_, "UF,URX");
}

TEST_P(TcpProxyTest, UpstreamConnectionLimit) {
  configure(accessLogConfig("%RESPONSE_FLAGS%"));
  factory_context_.server_factory_context_.cluster_manager_.thread_local_cluster_.cluster_.info_
      ->resetResourceManager(0, 0, 0, 0, 0);

  // setup sets up expectation for tcpConnForCluster but this test is expected to NOT call that
  filter_ =
      std::make_unique<Filter>(config_, factory_context_.server_factory_context_.cluster_manager_);
  // The downstream connection closes if the proxy can't make an upstream connection.
  EXPECT_CALL(filter_callbacks_.connection_, close(Network::ConnectionCloseType::NoFlush, _));
  filter_->initializeReadFilterCallbacks(filter_callbacks_);
  filter_->onNewConnection();

  filter_.reset();
  EXPECT_EQ(access_log_data_, "UO");
}

TEST_P(TcpProxyTest, IdleTimeoutObjectFactory) {
  const std::string name = "envoy.tcp_proxy.per_connection_idle_timeout_ms";
  auto* factory =
      Registry::FactoryRegistry<StreamInfo::FilterState::ObjectFactory>::getFactory(name);
  ASSERT_NE(nullptr, factory);
  EXPECT_EQ(name, factory->name());
  const std::string duration_in_milliseconds = std::to_string(1234);
  auto object = factory->createFromBytes(duration_in_milliseconds);
  ASSERT_NE(nullptr, object);
  EXPECT_EQ(duration_in_milliseconds, object->serializeAsString());
}

TEST_P(TcpProxyTest, InvalidIdleTimeoutObjectFactory) {
  const std::string name = "envoy.tcp_proxy.per_connection_idle_timeout_ms";
  auto* factory =
      Registry::FactoryRegistry<StreamInfo::FilterState::ObjectFactory>::getFactory(name);
  ASSERT_NE(nullptr, factory);
  EXPECT_EQ(name, factory->name());
  ASSERT_EQ(nullptr, factory->createFromBytes("not_a_number"));
}

TEST_P(TcpProxyTest, IdleTimeoutWithFilterStateOverride) {
  envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy config = defaultConfig();
  config.mutable_idle_timeout()->set_seconds(1);

  uint64_t idle_timeout_override = 5000;

  // Although the configured idle timeout is 1 second, overriding the value through filter state
  // so the expected idle timeout is 5 seconds instead.
  filter_callbacks_.connection_.streamInfo().filterState()->setData(
      TcpProxy::PerConnectionIdleTimeoutMs,
      std::make_unique<StreamInfo::UInt64AccessorImpl>(idle_timeout_override),
      StreamInfo::FilterState::StateType::ReadOnly, StreamInfo::FilterState::LifeSpan::Connection);

  Event::MockTimer* idle_timer = new Event::MockTimer(&filter_callbacks_.connection_.dispatcher_);
  EXPECT_CALL(*idle_timer, enableTimer(std::chrono::milliseconds(idle_timeout_override), _));
  setup(1, config);

  EXPECT_CALL(*idle_timer, enableTimer(std::chrono::milliseconds(idle_timeout_override), _));
  raiseEventUpstreamConnected(0);

  Buffer::OwnedImpl buffer("hello");
  EXPECT_CALL(*idle_timer, enableTimer(std::chrono::milliseconds(idle_timeout_override), _));
  filter_->onData(buffer, false);

  buffer.add("hello2");
  EXPECT_CALL(*idle_timer, enableTimer(std::chrono::milliseconds(idle_timeout_override), _));
  upstream_callbacks_->onUpstreamData(buffer, false);

  EXPECT_CALL(*idle_timer, enableTimer(std::chrono::milliseconds(idle_timeout_override), _));
  filter_callbacks_.connection_.raiseBytesSentCallbacks(1);

  EXPECT_CALL(*idle_timer, enableTimer(std::chrono::milliseconds(idle_timeout_override), _));
  upstream_connections_.at(0)->raiseBytesSentCallbacks(2);

  EXPECT_CALL(*upstream_connections_.at(0), close(Network::ConnectionCloseType::NoFlush, _));
  EXPECT_CALL(filter_callbacks_.connection_, close(Network::ConnectionCloseType::NoFlush, _));
  EXPECT_CALL(*idle_timer, disableTimer());
  idle_timer->invokeCallback();
}

// Tests that the idle timer closes both connections, and gets updated when either
// connection has activity.
TEST_P(TcpProxyTest, IdleTimeout) {
  envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy config = defaultConfig();
  config.mutable_idle_timeout()->set_seconds(1);
  Event::MockTimer* idle_timer = new Event::MockTimer(&filter_callbacks_.connection_.dispatcher_);
  EXPECT_CALL(*idle_timer, enableTimer(std::chrono::milliseconds(1000), _));
  setup(1, config);

  EXPECT_CALL(*idle_timer, enableTimer(std::chrono::milliseconds(1000), _));
  raiseEventUpstreamConnected(0);

  Buffer::OwnedImpl buffer("hello");
  EXPECT_CALL(*idle_timer, enableTimer(std::chrono::milliseconds(1000), _));
  filter_->onData(buffer, false);

  buffer.add("hello2");
  EXPECT_CALL(*idle_timer, enableTimer(std::chrono::milliseconds(1000), _));
  upstream_callbacks_->onUpstreamData(buffer, false);

  EXPECT_CALL(*idle_timer, enableTimer(std::chrono::milliseconds(1000), _));
  filter_callbacks_.connection_.raiseBytesSentCallbacks(1);

  EXPECT_CALL(*idle_timer, enableTimer(std::chrono::milliseconds(1000), _));
  upstream_connections_.at(0)->raiseBytesSentCallbacks(2);

  EXPECT_CALL(*upstream_connections_.at(0), close(Network::ConnectionCloseType::NoFlush, _));
  EXPECT_CALL(filter_callbacks_.connection_, close(Network::ConnectionCloseType::NoFlush, _));
  EXPECT_CALL(*idle_timer, disableTimer());
  idle_timer->invokeCallback();
}

// Tests that the idle timer is disabled when the downstream connection is closed.
TEST_P(TcpProxyTest, IdleTimerDisabledDownstreamClose) {
  envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy config = defaultConfig();
  config.mutable_idle_timeout()->set_seconds(1);
  Event::MockTimer* idle_timer = new Event::MockTimer(&filter_callbacks_.connection_.dispatcher_);
  EXPECT_CALL(*idle_timer, enableTimer(std::chrono::milliseconds(1000), _));
  setup(1, config);

  EXPECT_CALL(*idle_timer, enableTimer(std::chrono::milliseconds(1000), _));
  raiseEventUpstreamConnected(0);

  EXPECT_CALL(*idle_timer, disableTimer());
  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
}

// Tests that the idle timer is disabled when the upstream connection is closed.
TEST_P(TcpProxyTest, IdleTimerDisabledUpstreamClose) {
  envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy config = defaultConfig();
  config.mutable_idle_timeout()->set_seconds(1);
  Event::MockTimer* idle_timer = new Event::MockTimer(&filter_callbacks_.connection_.dispatcher_);
  EXPECT_CALL(*idle_timer, enableTimer(std::chrono::milliseconds(1000), _));
  setup(1, config);

  EXPECT_CALL(*idle_timer, enableTimer(std::chrono::milliseconds(1000), _));
  raiseEventUpstreamConnected(0);

  EXPECT_CALL(*idle_timer, disableTimer());
  upstream_callbacks_->onEvent(Network::ConnectionEvent::RemoteClose);
}

// Tests that flushing data during an idle timeout doesn't cause problems.
TEST_P(TcpProxyTest, IdleTimeoutWithOutstandingDataFlushed) {
  envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy config = defaultConfig();
  config.mutable_idle_timeout()->set_seconds(1);
  Event::MockTimer* idle_timer = new Event::MockTimer(&filter_callbacks_.connection_.dispatcher_);
  EXPECT_CALL(*idle_timer, enableTimer(std::chrono::milliseconds(1000), _));
  setup(1, config);

  EXPECT_CALL(*idle_timer, enableTimer(std::chrono::milliseconds(1000), _));
  raiseEventUpstreamConnected(0);

  Buffer::OwnedImpl buffer("hello");
  EXPECT_CALL(*idle_timer, enableTimer(std::chrono::milliseconds(1000), _));
  filter_->onData(buffer, false);

  buffer.add("hello2");
  EXPECT_CALL(*idle_timer, enableTimer(std::chrono::milliseconds(1000), _));
  upstream_callbacks_->onUpstreamData(buffer, false);

  EXPECT_CALL(*idle_timer, enableTimer(std::chrono::milliseconds(1000), _));
  filter_callbacks_.connection_.raiseBytesSentCallbacks(1);

  EXPECT_CALL(*idle_timer, enableTimer(std::chrono::milliseconds(1000), _));
  upstream_connections_.at(0)->raiseBytesSentCallbacks(2);

  // Mark the upstream connection as blocked.
  // This should read-disable the downstream connection.
  EXPECT_CALL(filter_callbacks_.connection_, readDisable(_));
  upstream_connections_.at(0)->runHighWatermarkCallbacks();

  // When Envoy has an idle timeout, the following happens.
  // Envoy closes the downstream connection
  // Envoy closes the upstream connection.
  // When closing the upstream connection with ConnectionCloseType::NoFlush,
  // if there is data in the buffer, Envoy does a best-effort flush.
  // If the write succeeds, Envoy may go under the flow control limit and start
  // the callbacks to read-enable the already-closed downstream connection.
  //
  // In this case we expect readDisable to not be called on the already closed
  // connection.
  EXPECT_CALL(filter_callbacks_.connection_, readDisable(true)).Times(0);
  EXPECT_CALL(*upstream_connections_.at(0), close(Network::ConnectionCloseType::NoFlush, _))
      .WillOnce(InvokeWithoutArgs(
          [&]() -> void { upstream_connections_.at(0)->runLowWatermarkCallbacks(); }));

  EXPECT_CALL(filter_callbacks_.connection_, close(Network::ConnectionCloseType::NoFlush, _));
  EXPECT_CALL(*idle_timer, disableTimer());
  idle_timer->invokeCallback();
}

// Test that Upstream and Downstream Bytes are metered.
// Checks that %UPSTREAM_WIRE_BYTES_SENT%, %UPSTREAM_WIRE_BYTES_RECEIVED%,
//  %DOWNSTREAM_WIRE_BYTES_SENT%, and %DOWNSTREAM_WIRE_BYTES_RECEIVED% are
//  correctly logged.
TEST_P(TcpProxyTest, AccessLogBytesMeterData) {
  setup(1, accessLogConfig("%UPSTREAM_WIRE_BYTES_SENT% %UPSTREAM_WIRE_BYTES_RECEIVED% "
                           "%DOWNSTREAM_WIRE_BYTES_SENT% %DOWNSTREAM_WIRE_BYTES_RECEIVED%"));
  raiseEventUpstreamConnected(0);
  Buffer::OwnedImpl upData("bye");
  upstream_callbacks_->onUpstreamData(upData, false);
  Buffer::OwnedImpl downData("hiya");
  filter_->onData(downData, false);
  Buffer::OwnedImpl noneData("");
  filter_->onData(noneData, false);

  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
  filter_.reset();
  EXPECT_EQ(access_log_data_, "4 3 3 4");
}

// Test that access log fields %UPSTREAM_HOST% and %UPSTREAM_CLUSTER% are correctly logged with the
// observability name.
TEST_P(TcpProxyTest, AccessLogUpstreamHost) {
  setup(1, accessLogConfig("%UPSTREAM_HOST% %UPSTREAM_CLUSTER%"));
  raiseEventUpstreamConnected(0);
  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
  filter_.reset();
  EXPECT_EQ(access_log_data_, "127.0.0.1:80 observability_name");
}

// Test that access log field %UPSTREAM_LOCAL_ADDRESS% is correctly logged.
TEST_P(TcpProxyTest, AccessLogUpstreamLocalAddress) {
  setup(1, accessLogConfig("%UPSTREAM_LOCAL_ADDRESS%"));
  raiseEventUpstreamConnected(0);
  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
  filter_.reset();
  EXPECT_EQ(access_log_data_, "2.2.2.2:50000");
}

// Test that access log fields %DOWNSTREAM_PEER_URI_SAN% is correctly logged.
TEST_P(TcpProxyTest, AccessLogPeerUriSan) {
  filter_callbacks_.connection_.stream_info_.downstream_connection_info_provider_->setLocalAddress(
      *Network::Utility::resolveUrl("tcp://1.1.1.2:20000"));
  filter_callbacks_.connection_.stream_info_.downstream_connection_info_provider_->setRemoteAddress(
      *Network::Utility::resolveUrl("tcp://1.1.1.1:40000"));

  const std::vector<std::string> uriSan{"someSan"};
  auto mockConnectionInfo = std::make_shared<Ssl::MockConnectionInfo>();
  EXPECT_CALL(*mockConnectionInfo, uriSanPeerCertificate()).WillOnce(Return(uriSan));
  EXPECT_CALL(filter_callbacks_.connection_, ssl()).WillRepeatedly(Return(mockConnectionInfo));

  setup(1, accessLogConfig("%DOWNSTREAM_PEER_URI_SAN%"));
  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
  filter_.reset();
  EXPECT_EQ(access_log_data_, "someSan");
}

// Test that access log fields %DOWNSTREAM_TLS_SESSION_ID% is correctly logged.
TEST_P(TcpProxyTest, AccessLogTlsSessionId) {
  filter_callbacks_.connection_.stream_info_.downstream_connection_info_provider_->setLocalAddress(
      *Network::Utility::resolveUrl("tcp://1.1.1.2:20000"));
  filter_callbacks_.connection_.stream_info_.downstream_connection_info_provider_->setRemoteAddress(
      *Network::Utility::resolveUrl("tcp://1.1.1.1:40000"));

  const std::string tlsSessionId{
      "D62A523A65695219D46FE1FFE285A4C371425ACE421B110B5B8D11D3EB4D5F0B"};
  auto mockConnectionInfo = std::make_shared<Ssl::MockConnectionInfo>();
  EXPECT_CALL(*mockConnectionInfo, sessionId()).WillOnce(ReturnRef(tlsSessionId));
  EXPECT_CALL(filter_callbacks_.connection_, ssl()).WillRepeatedly(Return(mockConnectionInfo));

  setup(1, accessLogConfig("%DOWNSTREAM_TLS_SESSION_ID%"));
  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
  filter_.reset();
  EXPECT_EQ(access_log_data_, "D62A523A65695219D46FE1FFE285A4C371425ACE421B110B5B8D11D3EB4D5F0B");
}

// Test that access log fields %DOWNSTREAM_REMOTE_ADDRESS_WITHOUT_PORT% and
// %DOWNSTREAM_LOCAL_ADDRESS% are correctly logged.
TEST_P(TcpProxyTest, AccessLogDownstreamAddress) {
  filter_callbacks_.connection_.stream_info_.downstream_connection_info_provider_->setLocalAddress(
      *Network::Utility::resolveUrl("tcp://1.1.1.2:20000"));
  filter_callbacks_.connection_.stream_info_.downstream_connection_info_provider_->setRemoteAddress(
      *Network::Utility::resolveUrl("tcp://1.1.1.1:40000"));
  setup(1, accessLogConfig("%DOWNSTREAM_REMOTE_ADDRESS_WITHOUT_PORT% %DOWNSTREAM_LOCAL_ADDRESS%"));
  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
  filter_.reset();
  EXPECT_EQ(access_log_data_, "1.1.1.1 1.1.1.2:20000");
}

// Test that access log fields %DOWNSTREAM_LOCAL_ADDRESS_ENDPOINT_ID% is correctly logged.
TEST_P(TcpProxyTest, AccessLogDownstreamEndpointId) {
  auto downstream_local_address = Network::Address::InstanceConstSharedPtr{
      new Network::Address::EnvoyInternalInstance("downstream", "1234567890")};
  filter_callbacks_.connection_.stream_info_.downstream_connection_info_provider_->setLocalAddress(
      downstream_local_address);
  setup(1, accessLogConfig("%DOWNSTREAM_LOCAL_ADDRESS_ENDPOINT_ID%"));
  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
  filter_.reset();
  EXPECT_EQ(access_log_data_, "1234567890");
}

// Test that intermediate log entry by field %ACCESS_LOG_TYPE%.
TEST_P(TcpProxyTest, IntermediateLogEntry) {
  auto config = accessLogConfig("%ACCESS_LOG_TYPE%");
  config.mutable_access_log_options()->mutable_access_log_flush_interval()->set_seconds(1);
  config.mutable_idle_timeout()->set_seconds(0);

  auto* flush_timer = new NiceMock<Event::MockTimer>(&filter_callbacks_.connection_.dispatcher_);
  EXPECT_CALL(*flush_timer, enableTimer(std::chrono::milliseconds(1000), _));

  setup(1, config);
  raiseEventUpstreamConnected(0);

  // The timer will be enabled cyclically.
  EXPECT_CALL(*flush_timer, enableTimer(std::chrono::milliseconds(1000), _));
  filter_callbacks_.connection_.stream_info_.downstream_bytes_meter_->addWireBytesReceived(10);
  EXPECT_CALL(*mock_access_logger_, log(_, _))
      .WillOnce(Invoke(
          [](const Formatter::Context& log_context, const StreamInfo::StreamInfo& stream_info) {
            EXPECT_EQ(log_context.accessLogType(), AccessLog::AccessLogType::TcpPeriodic);

            EXPECT_EQ(stream_info.getDownstreamBytesMeter()->wireBytesReceived(), 10);
            EXPECT_THAT(stream_info.getDownstreamBytesMeter()->bytesAtLastDownstreamPeriodicLog(),
                        testing::IsNull());
          }));
  flush_timer->invokeCallback();

  // No valid duration until the connection is closed.
  EXPECT_EQ(access_log_data_.value(), AccessLogType_Name(AccessLog::AccessLogType::TcpPeriodic));

  filter_callbacks_.connection_.stream_info_.downstream_bytes_meter_->addWireBytesReceived(9);
  EXPECT_CALL(*mock_access_logger_, log(_, _))
      .WillOnce(Invoke(
          [](const Formatter::Context& log_context, const StreamInfo::StreamInfo& stream_info) {
            EXPECT_EQ(log_context.accessLogType(), AccessLog::AccessLogType::TcpPeriodic);

            EXPECT_EQ(stream_info.getDownstreamBytesMeter()->wireBytesReceived(), 19);
            EXPECT_EQ(stream_info.getDownstreamBytesMeter()
                          ->bytesAtLastDownstreamPeriodicLog()
                          ->wire_bytes_received,
                      10);
          }));
  EXPECT_CALL(*flush_timer, enableTimer(std::chrono::milliseconds(1000), _));
  flush_timer->invokeCallback();

  EXPECT_CALL(*mock_access_logger_, log(_, _))
      .WillOnce(Invoke([](const Formatter::Context& log_context, const StreamInfo::StreamInfo&) {
        EXPECT_EQ(log_context.accessLogType(), AccessLog::AccessLogType::TcpConnectionEnd);
      }));

  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
  filter_.reset();

  EXPECT_EQ(access_log_data_.value(),
            AccessLogType_Name(AccessLog::AccessLogType::TcpConnectionEnd));
}

TEST_P(TcpProxyTest, TestAccessLogOnUpstreamConnected) {
  auto config = accessLogConfig("%UPSTREAM_HOST% %ACCESS_LOG_TYPE%");
  config.mutable_access_log_options()->set_flush_access_log_on_connected(true);

  setup(1, config);
  raiseEventUpstreamConnected(0);

  // Default access log will only be flushed after the stream is closed.
  // Passing the following check makes sure that the access log was flushed
  // before the stream was closed.
  EXPECT_EQ(access_log_data_.value(),
            absl::StrCat("127.0.0.1:80 ",
                         AccessLogType_Name(AccessLog::AccessLogType::TcpUpstreamConnected)));

  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
  filter_.reset();

  EXPECT_EQ(access_log_data_.value(),
            absl::StrCat("127.0.0.1:80 ",
                         AccessLogType_Name(AccessLog::AccessLogType::TcpConnectionEnd)));
}

TEST_P(TcpProxyTest, AccessLogUpstreamSSLConnection) {
  setup(1);

  NiceMock<StreamInfo::MockStreamInfo> stream_info;
  const std::string session_id = "D62A523A65695219D46FE1FFE285A4C371425ACE421B110B5B8D11D3EB4D5F0B";
  auto ssl_info = std::make_shared<Ssl::MockConnectionInfo>();
  EXPECT_CALL(*ssl_info, sessionId()).WillRepeatedly(ReturnRef(session_id));
  stream_info.downstream_connection_info_provider_->setSslConnection(ssl_info);
  EXPECT_CALL(*upstream_connections_.at(0), streamInfo()).WillRepeatedly(ReturnRef(stream_info));

  raiseEventUpstreamConnected(0);
  ASSERT_NE(nullptr, filter_->getStreamInfo().upstreamInfo()->upstreamSslConnection());
  EXPECT_EQ(session_id,
            filter_->getStreamInfo().upstreamInfo()->upstreamSslConnection()->sessionId());
}

TEST_P(TcpProxyTest, AccessLogUpstreamConnectionId) {
  int connection_id = 20;
  setup(1, accessLogConfig("%UPSTREAM_CONNECTION_ID%"));

  EXPECT_CALL(*upstream_connections_.at(0), id()).WillRepeatedly(Return(connection_id));
  raiseEventUpstreamConnected(0);

  EXPECT_EQ(connection_id, filter_->getStreamInfo().upstreamInfo()->upstreamConnectionId());
  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
  filter_.reset();

  EXPECT_EQ(access_log_data_.value(), std::to_string(connection_id));
}

// Tests that upstream flush works properly with no idle timeout configured.
TEST_P(TcpProxyTest, UpstreamFlushNoTimeout) {
  setup(1);
  raiseEventUpstreamConnected(0);

  EXPECT_CALL(*upstream_connections_.at(0), close(Network::ConnectionCloseType::FlushWrite, _))
      .WillOnce(Return()); // Cancel default action of raising LocalClose
  EXPECT_CALL(*upstream_connections_.at(0), state())
      .WillOnce(Return(Network::Connection::State::Closing));
  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
  filter_.reset();

  EXPECT_EQ(1U, config_->stats().upstream_flush_active_.value());

  // Send some bytes; no timeout configured so this should be a no-op (not a crash).
  upstream_connections_.at(0)->raiseBytesSentCallbacks(1);

  // Simulate flush complete.
  upstream_callbacks_->onEvent(Network::ConnectionEvent::LocalClose);
  EXPECT_EQ(1U, config_->stats().upstream_flush_total_.value());
  EXPECT_EQ(0U, config_->stats().upstream_flush_active_.value());
}

// Tests that upstream flush works with an idle timeout configured, but the connection
// finishes draining before the timer expires.
TEST_P(TcpProxyTest, UpstreamFlushTimeoutConfigured) {
  envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy config = defaultConfig();
  config.mutable_idle_timeout()->set_seconds(1);
  Event::MockTimer* idle_timer = new Event::MockTimer(&filter_callbacks_.connection_.dispatcher_);
  EXPECT_CALL(*idle_timer, enableTimer(_, _));
  setup(1, config);

  EXPECT_CALL(*idle_timer, enableTimer(_, _));
  raiseEventUpstreamConnected(0);

  EXPECT_CALL(*upstream_connections_.at(0), close(Network::ConnectionCloseType::FlushWrite, _))
      .WillOnce(Return()); // Cancel default action of raising LocalClose
  EXPECT_CALL(*upstream_connections_.at(0), state())
      .WillOnce(Return(Network::Connection::State::Closing));
  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);

  filter_.reset();
  EXPECT_EQ(1U, config_->stats().upstream_flush_active_.value());

  EXPECT_CALL(*idle_timer, enableTimer(std::chrono::milliseconds(1000), _));
  upstream_connections_.at(0)->raiseBytesSentCallbacks(1);

  // Simulate flush complete.
  EXPECT_CALL(*idle_timer, disableTimer());
  upstream_callbacks_->onEvent(Network::ConnectionEvent::LocalClose);
  EXPECT_EQ(1U, config_->stats().upstream_flush_total_.value());
  EXPECT_EQ(0U, config_->stats().upstream_flush_active_.value());
  EXPECT_EQ(0U, config_->stats().idle_timeout_.value());
}

// Tests that upstream flush closes the connection when the idle timeout fires.
TEST_P(TcpProxyTest, UpstreamFlushTimeoutExpired) {
  envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy config = defaultConfig();
  config.mutable_idle_timeout()->set_seconds(1);
  Event::MockTimer* idle_timer = new Event::MockTimer(&filter_callbacks_.connection_.dispatcher_);
  EXPECT_CALL(*idle_timer, enableTimer(_, _));
  setup(1, config);

  EXPECT_CALL(*idle_timer, enableTimer(_, _));
  raiseEventUpstreamConnected(0);

  EXPECT_CALL(*upstream_connections_.at(0), close(Network::ConnectionCloseType::FlushWrite, _))
      .WillOnce(Return()); // Cancel default action of raising LocalClose
  EXPECT_CALL(*upstream_connections_.at(0), state())
      .WillOnce(Return(Network::Connection::State::Closing));
  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);

  filter_.reset();
  EXPECT_EQ(1U, config_->stats().upstream_flush_active_.value());

  EXPECT_CALL(*upstream_connections_.at(0), close(Network::ConnectionCloseType::NoFlush));
  EXPECT_CALL(*idle_timer, disableTimer());
  idle_timer->invokeCallback();
  EXPECT_EQ(1U, config_->stats().upstream_flush_total_.value());
  EXPECT_EQ(0U, config_->stats().upstream_flush_active_.value());
  EXPECT_EQ(1U, config_->stats().idle_timeout_.value());
}

// Tests that upstream flush will close a connection if it reads data from the upstream
// connection after the downstream connection is closed (nowhere to send it).
TEST_P(TcpProxyTest, UpstreamFlushReceiveUpstreamData) {
  setup(1);
  raiseEventUpstreamConnected(0);

  EXPECT_CALL(*upstream_connections_.at(0), close(Network::ConnectionCloseType::FlushWrite, _))
      .WillOnce(Return()); // Cancel default action of raising LocalClose
  EXPECT_CALL(*upstream_connections_.at(0), state())
      .WillOnce(Return(Network::Connection::State::Closing));
  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
  filter_.reset();

  EXPECT_EQ(1U, config_->stats().upstream_flush_active_.value());

  // Send some bytes; no timeout configured so this should be a no-op (not a crash).
  Buffer::OwnedImpl buffer("a");
  EXPECT_CALL(*upstream_connections_.at(0), close(Network::ConnectionCloseType::NoFlush));
  upstream_callbacks_->onUpstreamData(buffer, false);
}

TEST_P(TcpProxyTest, UpstreamSocketOptionsReturnedEmpty) {
  setup(1);
  auto options = filter_->upstreamSocketOptions();
  EXPECT_EQ(options, nullptr);
}

TEST_P(TcpProxyTest, TcpProxySetRedirectRecordsToUpstream) {
  setup(/*connections=*/1, /*set_redirect_records=*/true, /*receive_before_connect=*/false);
  EXPECT_TRUE(filter_->upstreamSocketOptions());
  auto iterator = std::find_if(
      filter_->upstreamSocketOptions()->begin(), filter_->upstreamSocketOptions()->end(),
      [this](std::shared_ptr<const Network::Socket::Option> opt) {
        NiceMock<Network::MockConnectionSocket> dummy_socket;
        bool has_value = opt->getOptionDetails(dummy_socket,
                                               envoy::config::core::v3::SocketOption::STATE_PREBIND)
                             .has_value();
        return has_value &&
               opt->getOptionDetails(dummy_socket,
                                     envoy::config::core::v3::SocketOption::STATE_PREBIND)
                       .value()
                       .value_ == redirect_records_data_;
      });
  EXPECT_TRUE(iterator != filter_->upstreamSocketOptions()->end());
}

// Tests that downstream connection can access upstream connections filter state.
TEST_P(TcpProxyTest, ShareFilterState) {
  setup(1);

  upstream_connections_.at(0)->streamInfo().filterState()->setData(
      "envoy.tcp_proxy.cluster", std::make_unique<PerConnectionCluster>("filter_state_cluster"),
      StreamInfo::FilterState::StateType::Mutable, StreamInfo::FilterState::LifeSpan::Connection);
  raiseEventUpstreamConnected(0);
  EXPECT_EQ("filter_state_cluster",
            filter_callbacks_.connection_.streamInfo()
                .upstreamInfo()
                ->upstreamFilterState()
                ->getDataReadOnly<PerConnectionCluster>("envoy.tcp_proxy.cluster")
                ->value());
}

// Tests that filter callback can access downstream and upstream address and ssl properties.
TEST_P(TcpProxyTest, AccessDownstreamAndUpstreamProperties) {
  setup(1);

  raiseEventUpstreamConnected(0);
  EXPECT_EQ(filter_callbacks_.connection().streamInfo().downstreamAddressProvider().sslConnection(),
            filter_callbacks_.connection().ssl());
  EXPECT_EQ(
      filter_callbacks_.connection().streamInfo().upstreamInfo()->upstreamLocalAddress().get(),
      upstream_connections_.at(0)->streamInfo().downstreamAddressProvider().localAddress().get());
  EXPECT_EQ(filter_callbacks_.connection().streamInfo().upstreamInfo()->upstreamSslConnection(),
            upstream_connections_.at(0)->streamInfo().downstreamAddressProvider().sslConnection());
}

TEST_P(TcpProxyTest, PickClusterOnUpstreamFailureNoBackoffOptions) {
  auto config = defaultConfig();
  set2Cluster(config);
  config.mutable_idle_timeout()->set_seconds(0); // Disable idle timeout.
  config.mutable_max_connect_attempts()->set_value(2);

  Event::MockTimer* retry_timer = new Event::MockTimer(&filter_callbacks_.connection_.dispatcher_);

  // The random number lead into picking the first one in the weighted clusters.
  EXPECT_CALL(factory_context_.server_factory_context_.api_.random_, random()).WillOnce(Return(0));
  EXPECT_CALL(factory_context_.server_factory_context_.cluster_manager_,
              getThreadLocalCluster("fake_cluster_0"))
      .WillOnce(
          Return(&factory_context_.server_factory_context_.cluster_manager_.thread_local_cluster_));

  setup(1, config);

  EXPECT_CALL(factory_context_.server_factory_context_.api_.random_, random()).Times(0);
  EXPECT_CALL(*retry_timer, enableTimer(std::chrono::milliseconds(0), _));

  raiseEventUpstreamConnectFailed(0, ConnectionPool::PoolFailureReason::LocalConnectionFailure);

  // The random number lead into picking the second cluster.
  EXPECT_CALL(factory_context_.server_factory_context_.api_.random_, random()).WillOnce(Return(1));
  EXPECT_CALL(factory_context_.server_factory_context_.cluster_manager_,
              getThreadLocalCluster("fake_cluster_1"))
      .WillOnce(
          Return(&factory_context_.server_factory_context_.cluster_manager_.thread_local_cluster_));

  retry_timer->invokeCallback();

  EXPECT_EQ(0U, factory_context_.server_factory_context_.cluster_manager_.thread_local_cluster_
                    .cluster_.info_->stats_store_.counter("upstream_cx_connect_attempts_exceeded")
                    .value());
  EXPECT_CALL(*retry_timer, disableTimer());
}

TEST_P(TcpProxyTest, PickClusterOnUpstreamFailureWithBackoffOptions) {
  auto config = defaultConfig();
  set2Cluster(config);
  config.mutable_idle_timeout()->set_seconds(0); // Disable idle timeout.
  config.mutable_backoff_options()->mutable_base_interval()->set_seconds(1);
  config.mutable_max_connect_attempts()->set_value(2);

  Event::MockTimer* retry_timer = new Event::MockTimer(&filter_callbacks_.connection_.dispatcher_);

  // The random number lead into picking the first one in the weighted clusters.
  EXPECT_CALL(factory_context_.server_factory_context_.api_.random_, random()).WillOnce(Return(0));
  EXPECT_CALL(factory_context_.server_factory_context_.cluster_manager_,
              getThreadLocalCluster("fake_cluster_0"))
      .WillOnce(
          Return(&factory_context_.server_factory_context_.cluster_manager_.thread_local_cluster_));

  setup(1, config);

  EXPECT_CALL(factory_context_.server_factory_context_.api_.random_, random())
      .WillOnce(Return(100));
  EXPECT_CALL(*retry_timer, enableTimer(std::chrono::milliseconds(100), _));

  raiseEventUpstreamConnectFailed(0, ConnectionPool::PoolFailureReason::LocalConnectionFailure);

  // The random number lead into picking the second cluster.
  EXPECT_CALL(factory_context_.server_factory_context_.api_.random_, random()).WillOnce(Return(1));
  EXPECT_CALL(factory_context_.server_factory_context_.cluster_manager_,
              getThreadLocalCluster("fake_cluster_1"))
      .WillOnce(
          Return(&factory_context_.server_factory_context_.cluster_manager_.thread_local_cluster_));

  retry_timer->invokeCallback();

  EXPECT_EQ(0U, factory_context_.server_factory_context_.cluster_manager_.thread_local_cluster_
                    .cluster_.info_->stats_store_.counter("upstream_cx_connect_attempts_exceeded")
                    .value());
  EXPECT_CALL(*retry_timer, disableTimer());
}

// Verify that odcds callback does not re-pick cluster. Upstream connect failure does, backoff
// options not configured.
TEST_P(TcpProxyTest, OnDemandCallbackStickToTheSelectedClusterNoBackoffOptions) {
  auto config = onDemandConfig();
  set2Cluster(config);
  config.mutable_idle_timeout()->set_seconds(0); // Disable idle timeout.
  config.mutable_max_connect_attempts()->set_value(2);
  mock_odcds_api_handle_ = Upstream::MockOdCdsApiHandle::create().release();

  // The random number lead to select the first one in the weighted clusters.
  EXPECT_CALL(factory_context_.server_factory_context_.api_.random_, random()).WillOnce(Return(0));

  // The first cluster is requested 2 times.
  EXPECT_CALL(factory_context_.server_factory_context_.cluster_manager_,
              getThreadLocalCluster("fake_cluster_0"))
      // Invoked on new connection. Null is returned which would trigger on demand.
      .WillOnce(Return(nullptr))
      // Invoked in the callback of on demand look up. The cluster is ready upon callback.
      .WillOnce(
          Return(&factory_context_.server_factory_context_.cluster_manager_.thread_local_cluster_))
      .RetiresOnSaturation();

  Upstream::ClusterDiscoveryCallbackPtr cluster_discovery_callback;
  EXPECT_CALL(*mock_odcds_api_handle_, requestOnDemandClusterDiscovery("fake_cluster_0", _, _))
      .WillOnce(Invoke([&](auto&&, auto&& cb, auto&&) {
        cluster_discovery_callback = std::move(cb);
        return std::make_unique<Upstream::MockClusterDiscoveryCallbackHandle>();
      }));

  Event::MockTimer* retry_timer = new Event::MockTimer(&filter_callbacks_.connection_.dispatcher_);
  setup(1, config);

  // When the on-demand look up callback is invoked, the target cluster should not change.
  // The behavior is verified by checking the random() which is used during cluster re-pick.
  EXPECT_CALL(factory_context_.server_factory_context_.api_.random_, random()).Times(0);
  std::invoke(*cluster_discovery_callback, Upstream::ClusterDiscoveryStatus::Available);

  // Start to raise connect failure.

  EXPECT_CALL(factory_context_.server_factory_context_.api_.random_, random()).Times(0);
  EXPECT_CALL(*retry_timer, enableTimer(std::chrono::milliseconds(0), _));

  raiseEventUpstreamConnectFailed(0, ConnectionPool::PoolFailureReason::LocalConnectionFailure);

  // random() is raised in the cluster pick. `fake_cluster_1` will be picked.
  EXPECT_CALL(factory_context_.server_factory_context_.api_.random_, random()).WillOnce(Return(1));

  EXPECT_CALL(factory_context_.server_factory_context_.cluster_manager_,
              getThreadLocalCluster("fake_cluster_1"))
      // Invoked on connect attempt. Null is returned which would trigger on demand.
      .WillOnce(Return(nullptr))
      .RetiresOnSaturation();

  EXPECT_CALL(*mock_odcds_api_handle_, requestOnDemandClusterDiscovery("fake_cluster_1", _, _))
      .WillOnce(Invoke([&](auto&&, auto&&, auto&&) {
        return std::make_unique<Upstream::MockClusterDiscoveryCallbackHandle>();
      }));

  retry_timer->invokeCallback();
  EXPECT_EQ(0U, factory_context_.server_factory_context_.cluster_manager_.thread_local_cluster_
                    .cluster_.info_->stats_store_.counter("upstream_cx_connect_attempts_exceeded")
                    .value());

  EXPECT_CALL(filter_callbacks_.connection_, close(_, _));
  EXPECT_CALL(*retry_timer, disableTimer());
  std::invoke(*cluster_discovery_callback, Upstream::ClusterDiscoveryStatus::Missing);
}

// Verify that odcds callback does not re-pick cluster. Upstream connect failure does, backoff
// options configured.
TEST_P(TcpProxyTest, OnDemandCallbackStickToTheSelectedClusterWithBackoffOptions) {
  auto config = onDemandConfig();
  set2Cluster(config);
  config.mutable_idle_timeout()->set_seconds(0); // Disable idle timeout.
  config.mutable_backoff_options()->mutable_base_interval()->set_seconds(1);
  config.mutable_max_connect_attempts()->set_value(2);
  mock_odcds_api_handle_ = Upstream::MockOdCdsApiHandle::create().release();

  // The random number lead to select the first one in the weighted clusters.
  EXPECT_CALL(factory_context_.server_factory_context_.api_.random_, random()).WillOnce(Return(0));

  // The first cluster is requested 2 times.
  EXPECT_CALL(factory_context_.server_factory_context_.cluster_manager_,
              getThreadLocalCluster("fake_cluster_0"))
      // Invoked on new connection. Null is returned which would trigger on demand.
      .WillOnce(Return(nullptr))
      // Invoked in the callback of on demand look up. The cluster is ready upon callback.
      .WillOnce(
          Return(&factory_context_.server_factory_context_.cluster_manager_.thread_local_cluster_))
      .RetiresOnSaturation();

  Upstream::ClusterDiscoveryCallbackPtr cluster_discovery_callback;
  EXPECT_CALL(*mock_odcds_api_handle_, requestOnDemandClusterDiscovery("fake_cluster_0", _, _))
      .WillOnce(Invoke([&](auto&&, auto&& cb, auto&&) {
        cluster_discovery_callback = std::move(cb);
        return std::make_unique<Upstream::MockClusterDiscoveryCallbackHandle>();
      }));

  Event::MockTimer* retry_timer = new Event::MockTimer(&filter_callbacks_.connection_.dispatcher_);
  setup(1, config);

  // When the on-demand look up callback is invoked, the target cluster should not change.
  // The behavior is verified by checking the random() which is used during cluster re-pick.
  EXPECT_CALL(factory_context_.server_factory_context_.api_.random_, random()).Times(0);
  std::invoke(*cluster_discovery_callback, Upstream::ClusterDiscoveryStatus::Available);

  // Start to raise connect failure.

  EXPECT_CALL(factory_context_.server_factory_context_.api_.random_, random())
      .WillOnce(Return(100));
  EXPECT_CALL(*retry_timer, enableTimer(std::chrono::milliseconds(100), _));

  raiseEventUpstreamConnectFailed(0, ConnectionPool::PoolFailureReason::LocalConnectionFailure);

  // random() is raised in the cluster pick. `fake_cluster_1` will be picked.
  EXPECT_CALL(factory_context_.server_factory_context_.api_.random_, random()).WillOnce(Return(1));

  EXPECT_CALL(factory_context_.server_factory_context_.cluster_manager_,
              getThreadLocalCluster("fake_cluster_1"))
      // Invoked on connect attempt. Null is returned which would trigger on demand.
      .WillOnce(Return(nullptr))
      .RetiresOnSaturation();

  EXPECT_CALL(*mock_odcds_api_handle_, requestOnDemandClusterDiscovery("fake_cluster_1", _, _))
      .WillOnce(Invoke([&](auto&&, auto&&, auto&&) {
        return std::make_unique<Upstream::MockClusterDiscoveryCallbackHandle>();
      }));

  retry_timer->invokeCallback();
  EXPECT_EQ(0U, factory_context_.server_factory_context_.cluster_manager_.thread_local_cluster_
                    .cluster_.info_->stats_store_.counter("upstream_cx_connect_attempts_exceeded")
                    .value());

  EXPECT_CALL(filter_callbacks_.connection_, close(_, _));
  EXPECT_CALL(*retry_timer, disableTimer());
  std::invoke(*cluster_discovery_callback, Upstream::ClusterDiscoveryStatus::Missing);
}

// Verify the on demand api is not invoked when the target thread local cluster is present.
TEST_P(TcpProxyTest, OdcdsIsIgnoredIfClusterExists) {
  auto config = onDemandConfig();

  setup(1, config);
  raiseEventUpstreamConnected(0);

  Buffer::OwnedImpl buffer("hello");
  EXPECT_CALL(*upstream_connections_.at(0), write(BufferEqual(&buffer), false));
  filter_->onData(buffer, false);

  Buffer::OwnedImpl response("world");
  EXPECT_CALL(filter_callbacks_.connection_, write(BufferEqual(&response), _));
  upstream_callbacks_->onUpstreamData(response, false);

  EXPECT_CALL(filter_callbacks_.connection_, close(Network::ConnectionCloseType::FlushWrite));
  upstream_callbacks_->onEvent(Network::ConnectionEvent::RemoteClose);
}

// Verify the on demand request is cancelled if the tcp downstream connection is closed.
TEST_P(TcpProxyTest, OdcdsCancelIfConnectionClose) {
  auto config = onDemandConfig();
  mock_odcds_api_handle_ = Upstream::MockOdCdsApiHandle::create().release();

  // To trigger the on demand request, we enforce the first call to getThreadLocalCluster returning
  // no cluster.
  EXPECT_CALL(factory_context_.server_factory_context_.cluster_manager_,
              getThreadLocalCluster("fake_cluster"))
      .WillOnce(Return(nullptr))
      .RetiresOnSaturation();

  EXPECT_CALL(*mock_odcds_api_handle_, requestOnDemandClusterDiscovery("fake_cluster", _, _))
      .WillOnce(Invoke([&](auto&&, auto&&, auto&&) {
        return std::make_unique<Upstream::MockClusterDiscoveryCallbackHandle>();
      }));
  setup(0, config);
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onNewConnection());

  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::LocalClose);
}

// Verify a request can be served after a successful on demand cluster request.
TEST_P(TcpProxyTest, OdcdsBasicDownstreamLocalClose) {
  auto config = onDemandConfig();
  mock_odcds_api_handle_ = Upstream::MockOdCdsApiHandle::create().release();

  // To trigger the on demand request, we enforce the first call to getThreadLocalCluster returning
  // no cluster.
  EXPECT_CALL(factory_context_.server_factory_context_.cluster_manager_,
              getThreadLocalCluster("fake_cluster"))
      .WillOnce(Return(nullptr))
      .WillOnce(
          Return(&factory_context_.server_factory_context_.cluster_manager_.thread_local_cluster_))
      .RetiresOnSaturation();

  timeSystem().advanceTimeWait(std::chrono::microseconds(20));
  Upstream::ClusterDiscoveryCallbackPtr cluster_discovery_callback;
  EXPECT_CALL(*mock_odcds_api_handle_, requestOnDemandClusterDiscovery("fake_cluster", _, _))
      .WillOnce(Invoke([&](auto&&, auto&& cb, auto&&) {
        cluster_discovery_callback = std::move(cb);
        return std::make_unique<Upstream::MockClusterDiscoveryCallbackHandle>();
      }));

  setup(1, config);
  std::invoke(*cluster_discovery_callback, Upstream::ClusterDiscoveryStatus::Available);
  timeSystem().advanceTimeWait(std::chrono::microseconds(10));

  raiseEventUpstreamConnected(0);
  const absl::optional<std::chrono::nanoseconds> upstream_connection_establishment_latency =
      filter_->getStreamInfo().upstreamInfo()->upstreamTiming().connectionPoolCallbackLatency();
  ASSERT_TRUE(upstream_connection_establishment_latency.has_value());
  // OdCds resolution time isn't included in time to connect to upstream.
  EXPECT_EQ(std::chrono::microseconds(10), upstream_connection_establishment_latency.value());

  Buffer::OwnedImpl buffer("hello");
  EXPECT_CALL(*upstream_connections_.at(0), write(BufferEqual(&buffer), _));
  filter_->onData(buffer, false);

  Buffer::OwnedImpl response("world");
  EXPECT_CALL(filter_callbacks_.connection_, write(BufferEqual(&response), _));
  upstream_callbacks_->onUpstreamData(response, false);

  EXPECT_CALL(*upstream_connections_.at(0), close(Network::ConnectionCloseType::NoFlush, _));
  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::LocalClose);
}

// Verify the connection is closed after the cluster missing callback is triggered.
TEST_P(TcpProxyTest, OdcdsClusterMissingCauseConnectionClose) {
  auto config = onDemandConfig();
  mock_odcds_api_handle_ = Upstream::MockOdCdsApiHandle::create().release();

  EXPECT_CALL(factory_context_.server_factory_context_.cluster_manager_,
              getThreadLocalCluster("fake_cluster"))
      .WillOnce(Return(nullptr))
      .RetiresOnSaturation();

  Upstream::ClusterDiscoveryCallbackPtr cluster_discovery_callback;
  EXPECT_CALL(*mock_odcds_api_handle_, requestOnDemandClusterDiscovery("fake_cluster", _, _))
      .WillOnce(Invoke([&](auto&&, auto&& cb, auto&&) {
        cluster_discovery_callback = std::move(cb);
        return std::make_unique<Upstream::MockClusterDiscoveryCallbackHandle>();
      }));

  setup(0, config);
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onNewConnection());

  EXPECT_CALL(filter_callbacks_.connection_, close(_, _));
  std::invoke(*cluster_discovery_callback, Upstream::ClusterDiscoveryStatus::Missing);

  // No upstream connection was attempted, so no latency should be recorded.
  const absl::optional<std::chrono::nanoseconds> upstream_connection_establishment_latency =
      filter_->getStreamInfo().upstreamInfo()->upstreamTiming().connectionPoolCallbackLatency();
  ASSERT_FALSE(upstream_connection_establishment_latency.has_value());
}

// Test that upstream transport failure message is reflected in access logs.
TEST_P(TcpProxyTest, UpstreamConnectFailureStreamInfoAccessLog) {
  envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy config = defaultConfig();

  setup(1, accessLogConfig("%UPSTREAM_TRANSPORT_FAILURE_REASON%"));

  raiseEventUpstreamConnectFailed(0, ConnectionPool::PoolFailureReason::LocalConnectionFailure,
                                  "test_transport_failure");

  EXPECT_EQ(filter_->getStreamInfo().upstreamInfo()->upstreamTransportFailureReason(),
            "test_transport_failure");

  filter_.reset();
  EXPECT_EQ(access_log_data_, "test_transport_failure");
}

// Test that call to tcp_proxy filter's startUpstreamSecureTransport results
// in upstream's startUpstreamSecureTransport call.
TEST_P(TcpProxyTest, UpstreamStartSecureTransport) {
  envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy config = defaultConfig();

  setup(1, config);
  raiseEventUpstreamConnected(0);
  EXPECT_CALL(*upstream_connections_.at(0), startSecureTransport);
  filter_->startUpstreamSecureTransport();
}

// Verify the filter uses the value returned by
// Config::calculateMaxDownstreamConnectionDurationWithJitter.
TEST_P(TcpProxyTest, MaxDownstreamConnectionDurationWithJitterPercentage) {
  envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy config = defaultConfig();
  config.mutable_max_downstream_connection_duration()->set_seconds(10);
  config.mutable_max_downstream_connection_duration_jitter_percentage()->set_value(50.0);

  // Idle timeout also uses createTimer, clear it to avoid mixing up timers in tests.
  config.mutable_idle_timeout()->clear_seconds();

  EXPECT_CALL(factory_context_.server_factory_context_.api_.random_, random())
      .WillRepeatedly(Return(2500));

  setup(1, config);

  // Calculation of expected value is verified in config test. Here we just verify that the filter
  // uses the value returned by the config.
  const auto expected = config_->calculateMaxDownstreamConnectionDurationWithJitter();
  ASSERT_TRUE(expected.has_value());

  Event::MockTimer* connection_duration_timer =
      new Event::MockTimer(&filter_callbacks_.connection_.dispatcher_);

  EXPECT_CALL(*connection_duration_timer, enableTimer(expected.value(), _));
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onNewConnection());
}

// Test that the proxy protocol TLV with static value is set.
TEST_P(TcpProxyTest, SetTLV) {
  envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy config = defaultConfig();
  auto* tlv = config.add_proxy_protocol_tlvs();
  tlv->set_type(0xF1);
  tlv->set_value("tst");

  setup(1, config);
  raiseEventUpstreamConnected(0);

  // Verify the downstream TLV is set.
  auto& downstream_info = filter_callbacks_.connection_.streamInfo();
  auto header =
      downstream_info.filterState()->getDataReadOnly<Envoy::Network::ProxyProtocolFilterState>(
          Envoy::Network::ProxyProtocolFilterState::key());
  ASSERT_TRUE(header != nullptr);
  auto& tlvs = header->value().tlv_vector_;
  ASSERT_EQ(1, tlvs.size());
  EXPECT_EQ(0xF1, tlvs[0].type);
  EXPECT_EQ("tst", std::string(tlvs[0].value.begin(), tlvs[0].value.end()));

  // Verify the upstream TLV is set.
  const auto upstream_header = filter_->upstreamTransportSocketOptions()->proxyProtocolOptions();
  ASSERT_TRUE(upstream_header.has_value());
  const auto& upstream_tlvs = upstream_header->tlv_vector_;
  ASSERT_EQ(1, upstream_tlvs.size());
  EXPECT_EQ(0xF1, upstream_tlvs[0].type);
  EXPECT_EQ("tst", std::string(upstream_tlvs[0].value.begin(), upstream_tlvs[0].value.end()));
}

// Test that the proxy protocol TLV with dynamic format string is evaluated correctly.
TEST_P(TcpProxyTest, SetDynamicTLVWithMetadata) {
  envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy config = defaultConfig();
  auto* tlv = config.add_proxy_protocol_tlvs();
  tlv->set_type(0xF2);
  tlv->mutable_format_string()->mutable_text_format_source()->set_inline_string(
      "%DYNAMIC_METADATA(envoy.test:key)%");

  // Set dynamic metadata on the connection streamInfo directly.
  filter_callbacks_.connection_.stream_info_.metadata_.mutable_filter_metadata()->insert(
      {"envoy.test", Protobuf::Struct()});
  auto& test_struct = (*filter_callbacks_.connection_.stream_info_.metadata_
                            .mutable_filter_metadata())["envoy.test"];
  (*test_struct.mutable_fields())["key"].set_string_value("test_value");

  setup(1, config);
  raiseEventUpstreamConnected(0);

  // Verify the downstream TLV is set with the formatted value.
  auto& downstream_info = filter_callbacks_.connection_.streamInfo();
  auto header =
      downstream_info.filterState()->getDataReadOnly<Envoy::Network::ProxyProtocolFilterState>(
          Envoy::Network::ProxyProtocolFilterState::key());
  ASSERT_TRUE(header != nullptr);
  auto& tlvs = header->value().tlv_vector_;
  ASSERT_EQ(1, tlvs.size());
  EXPECT_EQ(0xF2, tlvs[0].type);

  // The formatter outputs "test_value" directly as bytes.
  EXPECT_EQ("test_value", std::string(tlvs[0].value.begin(), tlvs[0].value.end()));

  // Verify the upstream TLV is set.
  const auto upstream_header = filter_->upstreamTransportSocketOptions()->proxyProtocolOptions();
  ASSERT_TRUE(upstream_header.has_value());
  const auto& upstream_tlvs = upstream_header->tlv_vector_;
  ASSERT_EQ(1, upstream_tlvs.size());
  EXPECT_EQ(0xF2, upstream_tlvs[0].type);
  EXPECT_EQ("test_value",
            std::string(upstream_tlvs[0].value.begin(), upstream_tlvs[0].value.end()));
}

// Test that the proxy protocol TLV with dynamic format string for downstream address works.
TEST_P(TcpProxyTest, SetDynamicTLVWithDownstreamAddress) {
  filter_callbacks_.connection_.stream_info_.downstream_connection_info_provider_->setLocalAddress(
      *Network::Utility::resolveUrl("tcp://1.1.1.2:20000"));
  filter_callbacks_.connection_.stream_info_.downstream_connection_info_provider_->setRemoteAddress(
      *Network::Utility::resolveUrl("tcp://1.1.1.1:40000"));

  envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy config = defaultConfig();
  auto* tlv = config.add_proxy_protocol_tlvs();
  tlv->set_type(0xF3);
  tlv->mutable_format_string()->mutable_text_format_source()->set_inline_string(
      "%DOWNSTREAM_REMOTE_ADDRESS_WITHOUT_PORT%");

  setup(1, config);
  raiseEventUpstreamConnected(0);

  // Verify the downstream TLV is set with the formatted value.
  auto& downstream_info = filter_callbacks_.connection_.streamInfo();
  auto header =
      downstream_info.filterState()->getDataReadOnly<Envoy::Network::ProxyProtocolFilterState>(
          Envoy::Network::ProxyProtocolFilterState::key());
  ASSERT_TRUE(header != nullptr);
  auto& tlvs = header->value().tlv_vector_;
  ASSERT_EQ(1, tlvs.size());
  EXPECT_EQ(0xF3, tlvs[0].type);

  // The formatter outputs "1.1.1.1" directly as bytes.
  EXPECT_EQ("1.1.1.1", std::string(tlvs[0].value.begin(), tlvs[0].value.end()));
}

// Test that both static and dynamic TLVs can be combined.
TEST_P(TcpProxyTest, SetMixedStaticAndDynamicTLVs) {
  envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy config = defaultConfig();

  // Add a static TLV.
  auto* static_tlv = config.add_proxy_protocol_tlvs();
  static_tlv->set_type(0xF1);
  static_tlv->set_value("static_value");

  // Add a dynamic TLV.
  auto* dynamic_tlv = config.add_proxy_protocol_tlvs();
  dynamic_tlv->set_type(0xF2);
  dynamic_tlv->mutable_format_string()->mutable_text_format_source()->set_inline_string(
      "dynamic_value");

  setup(1, config);
  raiseEventUpstreamConnected(0);

  // Verify both TLVs are set.
  auto& downstream_info = filter_callbacks_.connection_.streamInfo();
  auto header =
      downstream_info.filterState()->getDataReadOnly<Envoy::Network::ProxyProtocolFilterState>(
          Envoy::Network::ProxyProtocolFilterState::key());
  ASSERT_TRUE(header != nullptr);
  auto& tlvs = header->value().tlv_vector_;
  ASSERT_EQ(2, tlvs.size());

  // Static TLV is first.
  EXPECT_EQ(0xF1, tlvs[0].type);
  EXPECT_EQ("static_value", std::string(tlvs[0].value.begin(), tlvs[0].value.end()));

  // Dynamic TLV is second with formatted value.
  EXPECT_EQ(0xF2, tlvs[1].type);
  EXPECT_EQ("dynamic_value", std::string(tlvs[1].value.begin(), tlvs[1].value.end()));
}

// Test that setting both value and format_string throws an error.
TEST_P(TcpProxyTest, SetTLVWithBothValueAndFormatStringFails) {
  envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy config = defaultConfig();
  auto* tlv = config.add_proxy_protocol_tlvs();
  tlv->set_type(0xF1);
  tlv->set_value("test");
  tlv->mutable_format_string()->mutable_text_format_source()->set_inline_string("test");

  EXPECT_THROW_WITH_MESSAGE(
      configure(config), EnvoyException,
      "Invalid TLV configuration: only one of 'value' or 'format_string' may be set.");
}

// Test that setting neither value nor format_string throws an error.
TEST_P(TcpProxyTest, SetTLVWithNeitherValueNorFormatStringFails) {
  envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy config = defaultConfig();
  auto* tlv = config.add_proxy_protocol_tlvs();
  tlv->set_type(0xF1);

  EXPECT_THROW_WITH_MESSAGE(
      configure(config), EnvoyException,
      "Invalid TLV configuration: one of 'value' or 'format_string' must be set.");
}

// Test that an invalid format string throws an error.
TEST_P(TcpProxyTest, SetTLVWithInvalidFormatStringFails) {
  envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy config = defaultConfig();
  auto* tlv = config.add_proxy_protocol_tlvs();
  tlv->set_type(0xF1);
  tlv->mutable_format_string()->mutable_text_format_source()->set_inline_string(
      "%INVALID_COMMAND%");

  EXPECT_THROW_WITH_REGEX(configure(config), EnvoyException,
                          "Failed to parse TLV format string:.*");
}

// Test that the proxy protocol TLV can use filter state values.
TEST_P(TcpProxyTest, SetDynamicTLVWithFilterState) {
  envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy config = defaultConfig();
  auto* tlv = config.add_proxy_protocol_tlvs();
  tlv->set_type(0xF4);
  tlv->mutable_format_string()->mutable_text_format_source()->set_inline_string(
      "%FILTER_STATE(test.key:PLAIN)%");

  // Set filter state on the connection streamInfo.
  filter_callbacks_.connection_.stream_info_.filter_state_->setData(
      "test.key", std::make_unique<Router::StringAccessorImpl>("filter_state_value"),
      StreamInfo::FilterState::StateType::ReadOnly, StreamInfo::FilterState::LifeSpan::Connection);

  setup(1, config);
  raiseEventUpstreamConnected(0);

  // Verify the downstream TLV is set with the filter state value.
  auto& downstream_info = filter_callbacks_.connection_.streamInfo();
  auto header =
      downstream_info.filterState()->getDataReadOnly<Envoy::Network::ProxyProtocolFilterState>(
          Envoy::Network::ProxyProtocolFilterState::key());
  ASSERT_TRUE(header != nullptr);
  auto& tlvs = header->value().tlv_vector_;
  ASSERT_EQ(1, tlvs.size());
  EXPECT_EQ(0xF4, tlvs[0].type);
  EXPECT_EQ("filter_state_value", std::string(tlvs[0].value.begin(), tlvs[0].value.end()));

  // Verify the upstream TLV is set.
  const auto upstream_header = filter_->upstreamTransportSocketOptions()->proxyProtocolOptions();
  ASSERT_TRUE(upstream_header.has_value());
  const auto& upstream_tlvs = upstream_header->tlv_vector_;
  ASSERT_EQ(1, upstream_tlvs.size());
  EXPECT_EQ(0xF4, upstream_tlvs[0].type);
  EXPECT_EQ("filter_state_value",
            std::string(upstream_tlvs[0].value.begin(), upstream_tlvs[0].value.end()));
}

// Test that START_TIME formatter works correctly.
TEST_P(TcpProxyTest, SetDynamicTLVWithStartTime) {
  envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy config = defaultConfig();
  auto* tlv = config.add_proxy_protocol_tlvs();
  tlv->set_type(0xF5);
  tlv->mutable_format_string()->mutable_text_format_source()->set_inline_string("%START_TIME%");

  setup(1, config);
  raiseEventUpstreamConnected(0);

  // Verify the TLV is set with a timestamp value.
  auto& downstream_info = filter_callbacks_.connection_.streamInfo();
  auto header =
      downstream_info.filterState()->getDataReadOnly<Envoy::Network::ProxyProtocolFilterState>(
          Envoy::Network::ProxyProtocolFilterState::key());
  ASSERT_TRUE(header != nullptr);
  auto& tlvs = header->value().tlv_vector_;
  ASSERT_EQ(1, tlvs.size());
  EXPECT_EQ(0xF5, tlvs[0].type);

  // The value should be a timestamp string (we just verify it's not empty).
  const std::string timestamp_value(tlvs[0].value.begin(), tlvs[0].value.end());
  EXPECT_FALSE(timestamp_value.empty());
  // Should contain date-like characters.
  EXPECT_TRUE(timestamp_value.find("-") != std::string::npos ||
              timestamp_value.find(":") != std::string::npos);
}

// Test buffer overflow behavior - should only readDisable, not re-trigger connection.
TEST_P(TcpProxyTest, BufferOverflowOnlyReadDisables) {
  // Configure with small buffer to test overflow.
  envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy config = defaultConfig();
  config.set_upstream_connect_mode(
      envoy::extensions::filters::network::tcp_proxy::v3::ON_DOWNSTREAM_DATA);
  config.mutable_max_early_data_bytes()->set_value(10);

  // Set up filter for ON_DOWNSTREAM_DATA mode without calling setup() to avoid conflicting
  // expectations.
  setupOnDownstreamDataMode(config, true);

  // Initial onNewConnection should return Continue for ON_DOWNSTREAM_DATA
  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onNewConnection());

  // Set up expectations for connection establishment when onData() triggers it.
  EXPECT_CALL(factory_context_.server_factory_context_.cluster_manager_.thread_local_cluster_,
              tcpConnPool(_, _, _))
      .WillOnce(Return(Upstream::TcpPoolData([]() {}, &conn_pool_)));
  EXPECT_CALL(conn_pool_, newConnection(_))
      .WillOnce(
          Invoke([&](Tcp::ConnectionPool::Callbacks& cb) -> Tcp::ConnectionPool::Cancellable* {
            conn_pool_callbacks_.push_back(&cb);
            return conn_pool_handles_
                .emplace_back(std::make_unique<NiceMock<Envoy::ConnectionPool::MockCancellable>>())
                .get();
          }));

  // First data chunk - should trigger connection.
  // Since buffer (5 bytes) < max (10 bytes), we don't read-disable yet.
  Buffer::OwnedImpl initial_data("hello");
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(initial_data, false));

  // Connection should be triggered exactly once.
  EXPECT_EQ(conn_pool_callbacks_.size(), 1);

  // Second data chunk that exceeds buffer - should NOT trigger connection again, but should
  // read-disable.
  Buffer::OwnedImpl overflow_data("world!!!"); // 8 bytes, total = 13 > 10 max
  EXPECT_CALL(filter_callbacks_.connection_, readDisable(true));
  // No new connection establishment call expected.
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(overflow_data, false));

  // Connection should still be triggered exactly once (not twice).
  EXPECT_EQ(conn_pool_callbacks_.size(), 1);
}

// Test that connection is triggered exactly once with multiple data chunks.
TEST_P(TcpProxyTest, SingleConnectionTriggerWithMultipleDataChunks) {
  envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy config = defaultConfig();
  config.set_upstream_connect_mode(
      envoy::extensions::filters::network::tcp_proxy::v3::ON_DOWNSTREAM_DATA);
  config.mutable_max_early_data_bytes()->set_value(1024);

  // Set up filter for ON_DOWNSTREAM_DATA mode without calling setup() to avoid conflicting
  // expectations.
  setupOnDownstreamDataMode(config, true);

  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onNewConnection());

  int connection_attempts = 0;
  EXPECT_CALL(factory_context_.server_factory_context_.cluster_manager_.thread_local_cluster_,
              tcpConnPool(_, _, _))
      .WillRepeatedly(Invoke([&](auto, auto, auto) {
        connection_attempts++;
        return Upstream::TcpPoolData([]() {}, &conn_pool_);
      }));
  EXPECT_CALL(conn_pool_, newConnection(_))
      .WillRepeatedly(
          Invoke([&](Tcp::ConnectionPool::Callbacks& cb) -> Tcp::ConnectionPool::Cancellable* {
            conn_pool_callbacks_.push_back(&cb);
            return conn_pool_handles_
                .emplace_back(std::make_unique<NiceMock<Envoy::ConnectionPool::MockCancellable>>())
                .get();
          }));

  // For new API with max_early_data_bytes=1024, readDisable(true) is only called when buffer
  // exceeds limit. Each chunk is 6 bytes, so we won't exceed 1024, so readDisable shouldn't be
  // called.

  // First data chunk.
  Buffer::OwnedImpl data1("chunk1");
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(data1, false));
  EXPECT_EQ(connection_attempts, 1);

  // Second data chunk - should NOT trigger another connection.
  Buffer::OwnedImpl data2("chunk2");
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(data2, false));
  EXPECT_EQ(connection_attempts, 1);

  // Third data chunk - should NOT trigger another connection.
  Buffer::OwnedImpl data3("chunk3");
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(data3, false));
  EXPECT_EQ(connection_attempts, 1);

  // Verify connection was triggered exactly once.
  EXPECT_EQ(connection_attempts, 1);
}

// Test empty data with end_stream doesn't trigger connection in ON_DOWNSTREAM_DATA mode.
TEST_P(TcpProxyTest, EmptyDataWithEndStreamDoesNotTriggerConnection) {
  envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy config = defaultConfig();
  config.set_upstream_connect_mode(
      envoy::extensions::filters::network::tcp_proxy::v3::ON_DOWNSTREAM_DATA);
  config.mutable_max_early_data_bytes()->set_value(1024);

  // Set up filter for ON_DOWNSTREAM_DATA mode without calling setup() to avoid conflicting
  // expectations.
  setupOnDownstreamDataMode(config, true);

  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onNewConnection());

  // Empty data with end_stream should NOT trigger connection.
  Buffer::OwnedImpl empty_data;
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(empty_data, true));

  // No connection should be established.
  EXPECT_TRUE(conn_pool_callbacks_.empty());
}

// Test that StopIteration in ON_DOWNSTREAM_DATA mode still allows reading.
TEST_P(TcpProxyTest, StopIterationAllowsReading) {
  envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy config = defaultConfig();
  config.set_upstream_connect_mode(
      envoy::extensions::filters::network::tcp_proxy::v3::ON_DOWNSTREAM_DATA);
  config.mutable_max_early_data_bytes()->set_value(1024);

  // Set up filter for ON_DOWNSTREAM_DATA mode without calling setup() to avoid conflicting
  // expectations.
  setupOnDownstreamDataMode(config, true);

  // onNewConnection returns Continue for ON_DOWNSTREAM_DATA mode with receive_before_connect
  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onNewConnection());

  // Setup expectations for when data arrives.
  EXPECT_CALL(factory_context_.server_factory_context_.cluster_manager_.thread_local_cluster_,
              tcpConnPool(_, _, _))
      .WillOnce(Return(Upstream::TcpPoolData([]() {}, &conn_pool_)));
  EXPECT_CALL(conn_pool_, newConnection(_))
      .WillOnce(
          Invoke([&](Tcp::ConnectionPool::Callbacks& cb) -> Tcp::ConnectionPool::Cancellable* {
            conn_pool_callbacks_.push_back(&cb);
            return conn_pool_handles_
                .emplace_back(std::make_unique<NiceMock<Envoy::ConnectionPool::MockCancellable>>())
                .get();
          }));

  // For new API with max_early_data_bytes=1024, readDisable(true) is only called when buffer
  // exceeds limit. "test_data" is 9 bytes, so we won't exceed 1024, so readDisable shouldn't be
  // called.

  // Data can still be received after StopIteration.
  Buffer::OwnedImpl data("test_data");
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(data, false));

  // Connection should be established.
  EXPECT_FALSE(conn_pool_callbacks_.empty());
}

// Test large buffer scenario with ON_DOWNSTREAM_DATA mode.
TEST_P(TcpProxyTest, LargeBufferScenario) {
  envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy config = defaultConfig();
  config.set_upstream_connect_mode(
      envoy::extensions::filters::network::tcp_proxy::v3::ON_DOWNSTREAM_DATA);
  config.mutable_max_early_data_bytes()->set_value(65536);

  // Use setupOnDownstreamDataMode to avoid conflicting expectations from base setup.
  setupOnDownstreamDataMode(config, false /* receive_before_connect */);

  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onNewConnection());

  EXPECT_CALL(factory_context_.server_factory_context_.cluster_manager_.thread_local_cluster_,
              tcpConnPool(_, _, _))
      .WillOnce(Return(Upstream::TcpPoolData([]() {}, &conn_pool_)));
  EXPECT_CALL(conn_pool_, newConnection(_))
      .WillOnce(
          Invoke([&](Tcp::ConnectionPool::Callbacks& cb) -> Tcp::ConnectionPool::Cancellable* {
            conn_pool_callbacks_.push_back(&cb);
            return conn_pool_handles_
                .emplace_back(std::make_unique<NiceMock<Envoy::ConnectionPool::MockCancellable>>())
                .get();
          }));

  // With new API, readDisable is only called when buffer exceeds limit, not when it reaches it.
  // So with 65536 bytes and limit 65536, readDisable should NOT be called.
  // But we need to send more than 65536 to trigger readDisable.
  // Actually, let's send 65537 bytes to exceed the limit.
  std::string large_data(65537, 'A');
  Buffer::OwnedImpl data(large_data);
  EXPECT_CALL(filter_callbacks_.connection_, readDisable(true));
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(data, false));

  // Connection should be established.
  EXPECT_FALSE(conn_pool_callbacks_.empty());
}

INSTANTIATE_TEST_SUITE_P(WithOrWithoutUpstream, TcpProxyTest, ::testing::Bool());

TEST(PerConnectionCluster, ObjectFactory) {
  const std::string name = "envoy.tcp_proxy.cluster";
  auto* factory =
      Registry::FactoryRegistry<StreamInfo::FilterState::ObjectFactory>::getFactory(name);
  ASSERT_NE(nullptr, factory);
  EXPECT_EQ(name, factory->name());
  const std::string cluster = "per_connection_cluster";
  auto object = factory->createFromBytes(cluster);
  ASSERT_NE(nullptr, object);
  EXPECT_EQ(cluster, object->serializeAsString());
}

// Test configuration parsing for UpstreamConnectMode.
TEST(TcpProxyConfigTest, UpstreamConnectModeImmediateConfig) {
  const std::string yaml = R"EOF(
    stat_prefix: name
    cluster: fake_cluster
    upstream_connect_mode: IMMEDIATE
  )EOF";

  NiceMock<Server::Configuration::MockFactoryContext> factory_context;
  envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy tcp_proxy;
  TestUtility::loadFromYamlAndValidate(yaml, tcp_proxy);

  Config config(tcp_proxy, factory_context);

  EXPECT_EQ(config.upstreamConnectMode(),
            envoy::extensions::filters::network::tcp_proxy::v3::IMMEDIATE);
}

TEST(TcpProxyConfigTest, UpstreamConnectModeOnDownstreamDataConfig) {
  const std::string yaml = R"EOF(
    stat_prefix: name
    cluster: fake_cluster
    upstream_connect_mode: ON_DOWNSTREAM_DATA
    max_early_data_bytes: 1024
  )EOF";

  NiceMock<Server::Configuration::MockFactoryContext> factory_context;
  envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy tcp_proxy;
  TestUtility::loadFromYamlAndValidate(yaml, tcp_proxy);

  Config config(tcp_proxy, factory_context);

  EXPECT_EQ(config.upstreamConnectMode(),
            envoy::extensions::filters::network::tcp_proxy::v3::ON_DOWNSTREAM_DATA);
  EXPECT_TRUE(config.maxEarlyDataBytes().has_value());
  EXPECT_EQ(config.maxEarlyDataBytes().value(), 1024);
}

TEST(TcpProxyConfigTest, UpstreamConnectModeOnDownstreamTlsHandshakeConfig) {
  const std::string yaml = R"EOF(
    stat_prefix: name
    cluster: fake_cluster
    upstream_connect_mode: ON_DOWNSTREAM_TLS_HANDSHAKE
  )EOF";

  NiceMock<Server::Configuration::MockFactoryContext> factory_context;
  envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy tcp_proxy;
  TestUtility::loadFromYamlAndValidate(yaml, tcp_proxy);

  Config config(tcp_proxy, factory_context);

  EXPECT_EQ(config.upstreamConnectMode(),
            envoy::extensions::filters::network::tcp_proxy::v3::ON_DOWNSTREAM_TLS_HANDSHAKE);
}

TEST(TcpProxyConfigTest, UpstreamConnectModeDefaultConfig) {
  const std::string yaml = R"EOF(
    stat_prefix: name
    cluster: fake_cluster
  )EOF";

  NiceMock<Server::Configuration::MockFactoryContext> factory_context;
  envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy tcp_proxy;
  TestUtility::loadFromYamlAndValidate(yaml, tcp_proxy);

  Config config(tcp_proxy, factory_context);

  // Should default to IMMEDIATE mode.
  EXPECT_EQ(config.upstreamConnectMode(),
            envoy::extensions::filters::network::tcp_proxy::v3::IMMEDIATE);
  EXPECT_FALSE(config.maxEarlyDataBytes().has_value());
}

TEST(TcpProxyConfigTest, UpstreamConnectModeWithEarlyDataBuffering) {
  const std::string yaml = R"EOF(
    stat_prefix: name
    cluster: fake_cluster
    upstream_connect_mode: IMMEDIATE
    max_early_data_bytes: 4096
  )EOF";

  NiceMock<Server::Configuration::MockFactoryContext> factory_context;
  envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy tcp_proxy;
  TestUtility::loadFromYamlAndValidate(yaml, tcp_proxy);

  Config config(tcp_proxy, factory_context);

  // IMMEDIATE mode with early data buffering enabled.
  EXPECT_EQ(config.upstreamConnectMode(),
            envoy::extensions::filters::network::tcp_proxy::v3::IMMEDIATE);
  EXPECT_TRUE(config.maxEarlyDataBytes().has_value());
  EXPECT_EQ(config.maxEarlyDataBytes().value(), 4096);
}

// Test that ON_DOWNSTREAM_DATA mode requires max_early_data_bytes.
TEST(TcpProxyConfigTest, UpstreamConnectModeOnDownstreamDataRequiresEarlyDataBytes) {
  const std::string yaml = R"EOF(
    stat_prefix: name
    cluster: fake_cluster
    upstream_connect_mode: ON_DOWNSTREAM_DATA
  )EOF";

  NiceMock<Server::Configuration::MockFactoryContext> factory_context;
  envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy tcp_proxy;
  TestUtility::loadFromYamlAndValidate(yaml, tcp_proxy);

  // Should throw exception because max_early_data_bytes is not set.
  EXPECT_THROW_WITH_MESSAGE(
      Config config(tcp_proxy, factory_context), EnvoyException,
      "max_early_data_bytes must be set when upstream_connect_mode is ON_DOWNSTREAM_DATA");
}

// Test TLS handshake completion detection for ON_DOWNSTREAM_TLS_HANDSHAKE mode.
// Use parameterized testing like the base class.
class TcpProxyTlsHandshakeTest : public TcpProxyTest {
public:
  void setupFilter(const std::string& yaml, bool receive_before_connect = false,
                   bool expect_initial_read_disable = true) {
    envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy tcp_proxy;
    TestUtility::loadFromYamlAndValidate(yaml, tcp_proxy);

    // Configure without calling the base setup to avoid conflicting expectations.
    configure(tcp_proxy);
    mock_access_logger_ = std::make_shared<NiceMock<AccessLog::MockInstance>>();
    const_cast<AccessLog::InstanceSharedPtrVector&>(config_->accessLogs())
        .push_back(mock_access_logger_);

    // Set up upstream connection data.
    upstream_connections_.push_back(std::make_unique<NiceMock<Network::MockClientConnection>>());
    upstream_connection_data_.push_back(
        std::make_unique<NiceMock<Tcp::ConnectionPool::MockConnectionData>>());
    ON_CALL(*upstream_connection_data_.back(), connection())
        .WillByDefault(ReturnRef(*upstream_connections_.back()));
    upstream_hosts_.push_back(std::make_shared<NiceMock<Upstream::MockHost>>());
    conn_pool_handles_.push_back(
        std::make_unique<NiceMock<Envoy::ConnectionPool::MockCancellable>>());

    // Set receive_before_connect filter state.
    filter_callbacks_.connection().streamInfo().filterState()->setData(
        TcpProxy::ReceiveBeforeConnectKey,
        std::make_unique<StreamInfo::BoolAccessorImpl>(receive_before_connect),
        StreamInfo::FilterState::StateType::ReadOnly,
        StreamInfo::FilterState::LifeSpan::Connection);

    // Create and initialize filter.
    filter_ = std::make_unique<Filter>(config_,
                                       factory_context_.server_factory_context_.cluster_manager_);
    EXPECT_CALL(filter_callbacks_.connection_, enableHalfClose(true));

    // For TLS modes with TLS connections, the filter will call readDisable(true)
    // when waiting for TLS handshake, regardless of receive_before_connect.
    if (expect_initial_read_disable) {
      EXPECT_CALL(filter_callbacks_.connection_, readDisable(true));
    }

    filter_->initializeReadFilterCallbacks(filter_callbacks_);
    filter_callbacks_.connection_.stream_info_.downstream_connection_info_provider_
        ->setSslConnection(filter_callbacks_.connection_.ssl());
  }

  void setupTlsMode() {
    const std::string yaml = R"EOF(
      stat_prefix: name
      cluster: fake_cluster
      upstream_connect_mode: ON_DOWNSTREAM_TLS_HANDSHAKE
    )EOF";

    // receive_before_connect=false, expect_initial_read_disable=true
    setupFilter(yaml, false, true);
  }
};

TEST_P(TcpProxyTlsHandshakeTest, TlsHandshakeMode_WithTlsConnection_WaitsForHandshake) {
  // Setup SSL connection before initializing the filter.
  auto ssl_connection = std::make_shared<NiceMock<Ssl::MockConnectionInfo>>();
  EXPECT_CALL(filter_callbacks_.connection_, ssl()).WillRepeatedly(Return(ssl_connection));

  setupTlsMode();

  // Call onNewConnection() to initialize the filter.
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onNewConnection());

  // Set up connection pool expectations for when TLS handshake completes.
  EXPECT_CALL(factory_context_.server_factory_context_.cluster_manager_.thread_local_cluster_,
              tcpConnPool(_, _, _))
      .WillOnce(Return(Upstream::TcpPoolData([]() {}, &conn_pool_)));
  EXPECT_CALL(conn_pool_, newConnection(_))
      .WillOnce(
          Invoke([&](Tcp::ConnectionPool::Callbacks& cb) -> Tcp::ConnectionPool::Cancellable* {
            conn_pool_callbacks_.push_back(&cb);
            return conn_pool_handles_
                .emplace_back(std::make_unique<NiceMock<Envoy::ConnectionPool::MockCancellable>>())
                .get();
          }));

  // Simulate TLS handshake completion.
  filter_->onDownstreamTlsHandshakeComplete();

  // Verify connection establishment was triggered.
  EXPECT_FALSE(conn_pool_callbacks_.empty());
}

TEST_P(TcpProxyTlsHandshakeTest, TlsHandshakeMode_WithNonTlsConnection_ImmediateConnect) {
  // No SSL connection.
  EXPECT_CALL(filter_callbacks_.connection_, ssl()).WillRepeatedly(Return(nullptr));

  // Set up connection pool expectations - should be called immediately for non-TLS.
  EXPECT_CALL(factory_context_.server_factory_context_.cluster_manager_.thread_local_cluster_,
              tcpConnPool(_, _, _))
      .WillOnce(Return(Upstream::TcpPoolData([]() {}, &conn_pool_)));
  EXPECT_CALL(conn_pool_, newConnection(_))
      .WillOnce(
          Invoke([&](Tcp::ConnectionPool::Callbacks& cb) -> Tcp::ConnectionPool::Cancellable* {
            conn_pool_callbacks_.push_back(&cb);
            return conn_pool_handles_
                .emplace_back(std::make_unique<NiceMock<Envoy::ConnectionPool::MockCancellable>>())
                .get();
          }));

  // readDisable(true) is already expected in setupFilter.

  setupTlsMode();

  // Call onNewConnection() to initialize the filter.
  // Since it's non-TLS, it should connect immediately.
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onNewConnection());

  // Connection should be established immediately.
  EXPECT_FALSE(conn_pool_callbacks_.empty());
}

TEST_P(TcpProxyTlsHandshakeTest, EarlyDataBufferExceedsMaxSize) {
  // Setup SSL connection before initializing the filter.
  auto ssl_connection = std::make_shared<NiceMock<Ssl::MockConnectionInfo>>();
  EXPECT_CALL(filter_callbacks_.connection_, ssl()).WillRepeatedly(Return(ssl_connection));

  // Configure with small max_buffered_bytes to trigger the overflow.
  setupFilter(R"EOF(
    stat_prefix: name
    cluster: fake_cluster
    upstream_connect_mode: ON_DOWNSTREAM_DATA
    max_early_data_bytes: 10
  )EOF",
              true,   // receive_before_connect
              false); // expect_initial_read_disable

  // Call onNewConnection() to initialize the filter.
  // For ON_DOWNSTREAM_DATA mode, it should return Continue to allow data to flow.
  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onNewConnection());

  // Set up connection pool expectations. Connection should be triggered when initial data is
  // received.
  EXPECT_CALL(factory_context_.server_factory_context_.cluster_manager_.thread_local_cluster_,
              tcpConnPool(_, _, _))
      .WillOnce(Return(Upstream::TcpPoolData([]() {}, &conn_pool_)));
  EXPECT_CALL(conn_pool_, newConnection(_))
      .WillOnce(
          Invoke([&](Tcp::ConnectionPool::Callbacks& cb) -> Tcp::ConnectionPool::Cancellable* {
            conn_pool_callbacks_.push_back(&cb);
            return conn_pool_handles_
                .emplace_back(std::make_unique<NiceMock<Envoy::ConnectionPool::MockCancellable>>())
                .get();
          }));

  // First, send small amount of data. We should buffer it and trigger connection.
  // Since buffer (5 bytes) < max (10 bytes), we don't read-disable yet.
  Buffer::OwnedImpl small_data("hello");
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(small_data, false));

  // Connection should be triggered by initial data.
  EXPECT_FALSE(conn_pool_callbacks_.empty());

  // Now send more data that will exceed max_buffered_bytes (10 bytes total).
  // Current buffer has 5 bytes, adding 10 more = 15 bytes > 10 max.
  // This should trigger readDisable(true) to prevent further buffering.
  Buffer::OwnedImpl more_data("more data!");
  EXPECT_CALL(filter_callbacks_.connection_, readDisable(true));
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(more_data, false));
}

TEST_P(TcpProxyTlsHandshakeTest, TlsHandshakeViaConnectedEvent) {
  // Setup SSL connection before initializing the filter.
  auto ssl_connection = std::make_shared<NiceMock<Ssl::MockConnectionInfo>>();
  EXPECT_CALL(filter_callbacks_.connection_, ssl()).WillRepeatedly(Return(ssl_connection));

  setupTlsMode();

  // Call onNewConnection() to initialize the filter.
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onNewConnection());

  // Set up connection pool expectations for when TLS handshake completes.
  EXPECT_CALL(factory_context_.server_factory_context_.cluster_manager_.thread_local_cluster_,
              tcpConnPool(_, _, _))
      .WillOnce(Return(Upstream::TcpPoolData([]() {}, &conn_pool_)));
  EXPECT_CALL(conn_pool_, newConnection(_))
      .WillOnce(
          Invoke([&](Tcp::ConnectionPool::Callbacks& cb) -> Tcp::ConnectionPool::Cancellable* {
            conn_pool_callbacks_.push_back(&cb);
            return conn_pool_handles_
                .emplace_back(std::make_unique<NiceMock<Envoy::ConnectionPool::MockCancellable>>())
                .get();
          }));

  // Simulate TLS handshake completion via Connected event.
  // This triggers the DownstreamCallbacks which calls onDownstreamEvent internally.
  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::Connected);

  // Verify connection establishment was triggered.
  EXPECT_FALSE(conn_pool_callbacks_.empty());
}

// Instantiate parameterized tests with both values of the runtime feature flag.
INSTANTIATE_TEST_SUITE_P(TcpProxyTlsHandshakeTestParams, TcpProxyTlsHandshakeTest,
                         testing::Values(false, true));

// Test that IMMEDIATE mode can be combined with max_early_data_bytes.
TEST(TcpProxyConfigTest, OrthogonalityImmediateModeWithEarlyData) {
  const std::string yaml = R"EOF(
    stat_prefix: name
    cluster: fake_cluster
    upstream_connect_mode: IMMEDIATE
    max_early_data_bytes: 4096
  )EOF";

  NiceMock<Server::Configuration::MockFactoryContext> factory_context;
  envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy tcp_proxy;
  TestUtility::loadFromYamlAndValidate(yaml, tcp_proxy);

  Config config(tcp_proxy, factory_context);

  // Both fields should be set independently.
  EXPECT_EQ(config.upstreamConnectMode(),
            envoy::extensions::filters::network::tcp_proxy::v3::IMMEDIATE);
  EXPECT_TRUE(config.maxEarlyDataBytes().has_value());
  EXPECT_EQ(config.maxEarlyDataBytes().value(), 4096);
}

// Test that ON_DOWNSTREAM_TLS_HANDSHAKE can be combined with max_early_data_bytes.
TEST(TcpProxyConfigTest, OrthogonalityTlsHandshakeModeWithEarlyData) {
  const std::string yaml = R"EOF(
    stat_prefix: name
    cluster: fake_cluster
    upstream_connect_mode: ON_DOWNSTREAM_TLS_HANDSHAKE
    max_early_data_bytes: 8192
  )EOF";

  NiceMock<Server::Configuration::MockFactoryContext> factory_context;
  envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy tcp_proxy;
  TestUtility::loadFromYamlAndValidate(yaml, tcp_proxy);

  Config config(tcp_proxy, factory_context);

  EXPECT_EQ(config.upstreamConnectMode(),
            envoy::extensions::filters::network::tcp_proxy::v3::ON_DOWNSTREAM_TLS_HANDSHAKE);
  EXPECT_TRUE(config.maxEarlyDataBytes().has_value());
  EXPECT_EQ(config.maxEarlyDataBytes().value(), 8192);
}

// Test that ON_DOWNSTREAM_TLS_HANDSHAKE without max_early_data_bytes works.
TEST(TcpProxyConfigTest, OrthogonalityTlsHandshakeModeWithoutEarlyData) {
  const std::string yaml = R"EOF(
    stat_prefix: name
    cluster: fake_cluster
    upstream_connect_mode: ON_DOWNSTREAM_TLS_HANDSHAKE
  )EOF";

  NiceMock<Server::Configuration::MockFactoryContext> factory_context;
  envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy tcp_proxy;
  TestUtility::loadFromYamlAndValidate(yaml, tcp_proxy);

  Config config(tcp_proxy, factory_context);

  EXPECT_EQ(config.upstreamConnectMode(),
            envoy::extensions::filters::network::tcp_proxy::v3::ON_DOWNSTREAM_TLS_HANDSHAKE);
  EXPECT_FALSE(config.maxEarlyDataBytes().has_value());
}

// Test that buffer exactly at limit does NOT trigger readDisable (only > limit does).
TEST_P(TcpProxyTest, BufferExactlyAtLimitDoesNotReadDisable) {
  envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy config = defaultConfig();
  config.set_upstream_connect_mode(
      envoy::extensions::filters::network::tcp_proxy::v3::ON_DOWNSTREAM_DATA);
  config.mutable_max_early_data_bytes()->set_value(5); // Exactly 5 bytes

  // Use setupOnDownstreamDataMode to avoid conflicting expectations from base setup.
  setupOnDownstreamDataMode(config, false /* receive_before_connect */);
  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onNewConnection());

  EXPECT_CALL(factory_context_.server_factory_context_.cluster_manager_.thread_local_cluster_,
              tcpConnPool(_, _, _))
      .WillOnce(Return(Upstream::TcpPoolData([]() {}, &conn_pool_)));
  EXPECT_CALL(conn_pool_, newConnection(_))
      .WillOnce(
          Invoke([&](Tcp::ConnectionPool::Callbacks& cb) -> Tcp::ConnectionPool::Cancellable* {
            conn_pool_callbacks_.push_back(&cb);
            return conn_pool_handles_
                .emplace_back(std::make_unique<NiceMock<Envoy::ConnectionPool::MockCancellable>>())
                .get();
          }));

  // Send exactly 5 bytes. It should trigger connection but NOT readDisable.
  Buffer::OwnedImpl exact_data("hello"); // 5 bytes exactly
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(exact_data, false));

  // Connection should be established.
  EXPECT_FALSE(conn_pool_callbacks_.empty());
}

// Test that ON_DOWNSTREAM_TLS_HANDSHAKE returns StopIteration.
TEST_P(TcpProxyTest, TlsHandshakeModeReturnsStopIteration) {
  envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy config = defaultConfig();
  config.set_upstream_connect_mode(
      envoy::extensions::filters::network::tcp_proxy::v3::ON_DOWNSTREAM_TLS_HANDSHAKE);

  setup(1, false, false, config);

  // ON_DOWNSTREAM_TLS_HANDSHAKE should return StopIteration, not Continue.
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onNewConnection());
}

// Test that multiple tiny data chunks correctly set initial_data_received_ only once.
TEST_P(TcpProxyTest, MultipleTinyChunksSetInitialDataReceivedOnce) {
  envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy config = defaultConfig();
  config.set_upstream_connect_mode(
      envoy::extensions::filters::network::tcp_proxy::v3::ON_DOWNSTREAM_DATA);
  config.mutable_max_early_data_bytes()->set_value(1024);

  // Use setupOnDownstreamDataMode to avoid conflicting expectations from base setup.
  setupOnDownstreamDataMode(config, false /* receive_before_connect */);
  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onNewConnection());

  EXPECT_CALL(factory_context_.server_factory_context_.cluster_manager_.thread_local_cluster_,
              tcpConnPool(_, _, _))
      .WillOnce(Return(Upstream::TcpPoolData([]() {}, &conn_pool_)));
  EXPECT_CALL(conn_pool_, newConnection(_))
      .WillOnce(
          Invoke([&](Tcp::ConnectionPool::Callbacks& cb) -> Tcp::ConnectionPool::Cancellable* {
            conn_pool_callbacks_.push_back(&cb);
            return conn_pool_handles_
                .emplace_back(std::make_unique<NiceMock<Envoy::ConnectionPool::MockCancellable>>())
                .get();
          }));

  // First tiny chunk. It should trigger connection but NOT readDisable.
  Buffer::OwnedImpl chunk1("h");
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(chunk1, false));
  EXPECT_EQ(conn_pool_callbacks_.size(), 1);

  // Second tiny chunk. It should NOT trigger connection again.
  Buffer::OwnedImpl chunk2("e");
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(chunk2, false));
  EXPECT_EQ(conn_pool_callbacks_.size(), 1); // Still only one connection attempt.
}

// Test that data with max_buffered_bytes=0 triggers connection and readDisable.
TEST_P(TcpProxyTest, ZeroBufferTriggersReadDisable) {
  envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy config = defaultConfig();
  config.set_upstream_connect_mode(
      envoy::extensions::filters::network::tcp_proxy::v3::ON_DOWNSTREAM_DATA);
  config.mutable_max_early_data_bytes()->set_value(0);

  // Use setupOnDownstreamDataMode to avoid conflicting expectations from base setup.
  setupOnDownstreamDataMode(config, false /* receive_before_connect */);
  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onNewConnection());

  // Expect connection to be triggered with data.
  EXPECT_CALL(factory_context_.server_factory_context_.cluster_manager_.thread_local_cluster_,
              tcpConnPool(_, _, _))
      .WillOnce(Return(Upstream::TcpPoolData([]() {}, &conn_pool_)));
  EXPECT_CALL(conn_pool_, newConnection(_))
      .WillOnce(
          Invoke([&](Tcp::ConnectionPool::Callbacks& cb) -> Tcp::ConnectionPool::Cancellable* {
            conn_pool_callbacks_.push_back(&cb);
            return conn_pool_handles_
                .emplace_back(std::make_unique<NiceMock<Envoy::ConnectionPool::MockCancellable>>())
                .get();
          }));

  // Data should trigger connection and readDisable (since buffer will be at limit).
  // With max_buffered_bytes=0, any data will cause readDisable.
  EXPECT_CALL(filter_callbacks_.connection_, readDisable(true));
  Buffer::OwnedImpl data("a");
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(data, false));
  EXPECT_FALSE(conn_pool_callbacks_.empty());
}

// Test that readDisable works correctly with buffer overflow.
TEST_P(TcpProxyTest, ReadDisableFalseOnlyWhenActuallyDisabled) {
  envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy config = defaultConfig();
  config.set_upstream_connect_mode(
      envoy::extensions::filters::network::tcp_proxy::v3::ON_DOWNSTREAM_DATA);
  config.mutable_max_early_data_bytes()->set_value(10); // Small buffer to trigger overflow

  // Use setupOnDownstreamDataMode to avoid conflicting expectations from base setup.
  setupOnDownstreamDataMode(config, false /* receive_before_connect */);
  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onNewConnection());

  EXPECT_CALL(factory_context_.server_factory_context_.cluster_manager_.thread_local_cluster_,
              tcpConnPool(_, _, _))
      .WillOnce(Return(Upstream::TcpPoolData([]() {}, &conn_pool_)));
  EXPECT_CALL(conn_pool_, newConnection(_))
      .WillOnce(
          Invoke([&](Tcp::ConnectionPool::Callbacks& cb) -> Tcp::ConnectionPool::Cancellable* {
            conn_pool_callbacks_.push_back(&cb);
            return conn_pool_handles_
                .emplace_back(std::make_unique<NiceMock<Envoy::ConnectionPool::MockCancellable>>())
                .get();
          }));

  // Send data that exceeds buffer (11 bytes > 10 byte limit). readDisable(true) should be called.
  Buffer::OwnedImpl data("hello world"); // 11 bytes
  EXPECT_CALL(filter_callbacks_.connection_, readDisable(true));
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(data, false));

  // Connection should be established.
  EXPECT_FALSE(conn_pool_callbacks_.empty());
}

// Test that legacy filter state receive_before_connect works correctly.
TEST_P(TcpProxyTest, LegacyFilterStateWithNewApi) {
  envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy config = defaultConfig();
  // Use default mode (IMMEDIATE) without max_early_data_bytes to test legacy behavior.
  // Don't set max_early_data_bytes, but set legacy filter state.

  setup(1, false, true, config); // receive_before_connect=true via filter state

  // Legacy filter state should work. With receive_before_connect and IMMEDIATE mode,
  // the base setup() will have already established a connection, so we can just verify
  // the filter was created successfully.
  EXPECT_NE(nullptr, filter_.get());
}

} // namespace
} // namespace TcpProxy
} // namespace Envoy
