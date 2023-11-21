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
#include "source/common/network/socket_option_factory.h"
#include "source/common/network/transport_socket_options_impl.h"
#include "source/common/network/upstream_server_name.h"
#include "source/common/network/upstream_socket_options_filter_state.h"
#include "source/common/network/win32_redirect_records_option_impl.h"
#include "source/common/router/metadatamatchcriteria_impl.h"
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
  using TcpProxyTestBase::setup;
  void setup(uint32_t connections, bool set_redirect_records,
             const envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy& config) override {
    if (config.has_on_demand()) {
      EXPECT_CALL(factory_context_.server_factory_context_.cluster_manager_,
                  allocateOdCdsApi(_, _, _))
          .WillOnce(
              Invoke([this]() { return Upstream::MockOdCdsApiHandlePtr(mock_odcds_api_handle_); }));
    }

    configure(config);
    mock_access_logger_ = std::make_shared<NiceMock<AccessLog::MockInstance>>();
    const_cast<std::vector<AccessLog::InstanceSharedPtr>&>(config_->accessLogs())
        .push_back(mock_access_logger_);
    upstream_local_address_ = Network::Utility::resolveUrl("tcp://2.2.2.2:50000");
    upstream_remote_address_ = Network::Utility::resolveUrl("tcp://127.0.0.1:80");
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
                    tcpConnPool(_, _))
            .WillOnce(Return(Upstream::TcpPoolData([]() {}, &conn_pool_)))
            .RetiresOnSaturation();
        EXPECT_CALL(conn_pool_, newConnection(_))
            .WillOnce(Invoke(
                [=](Tcp::ConnectionPool::Callbacks& cb) -> Tcp::ConnectionPool::Cancellable* {
                  conn_pool_callbacks_.push_back(&cb);
                  return onNewConnection(conn_pool_handles_.at(i).get());
                }))
            .RetiresOnSaturation();
      }
      EXPECT_CALL(factory_context_.server_factory_context_.cluster_manager_.thread_local_cluster_,
                  tcpConnPool(_, _))
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
      filter_ = std::make_unique<Filter>(config_,
                                         factory_context_.server_factory_context_.cluster_manager_);
      EXPECT_CALL(filter_callbacks_.connection_, enableHalfClose(true));
      EXPECT_CALL(filter_callbacks_.connection_, readDisable(true));
      filter_->initializeReadFilterCallbacks(filter_callbacks_);
      filter_callbacks_.connection_.stream_info_.downstream_connection_info_provider_
          ->setSslConnection(filter_callbacks_.connection_.ssl());
    }

    if (connections > 0) {
      EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onNewConnection());
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

TEST_F(TcpProxyTest, ExplicitCluster) {
  configure(defaultConfig());

  NiceMock<Network::MockConnection> connection;
  EXPECT_EQ(std::string("fake_cluster"), config_->getRouteFromEntries(connection)->clusterName());
}

// Tests that half-closes are proxied and don't themselves cause any connection to be closed.
TEST_F(TcpProxyTest, HalfCloseProxy) {
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
TEST_F(TcpProxyTest, ExplicitFactory) {
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
TEST_F(TcpProxyTest, BadFactory) {
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
TEST_F(TcpProxyTest, UpstreamLocalDisconnect) {
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
TEST_F(TcpProxyTest, UpstreamRemoteDisconnect) {
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

// Test that reconnect is attempted after a local connect failure
TEST_F(TcpProxyTest, ConnectAttemptsUpstreamLocalFail) {
  envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy config = defaultConfig();
  config.mutable_max_connect_attempts()->set_value(2);

  setup(2, config);

  timeSystem().advanceTimeWait(std::chrono::microseconds(10));
  raiseEventUpstreamConnectFailed(0, ConnectionPool::PoolFailureReason::LocalConnectionFailure);
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
}

// Make sure that the tcp proxy code handles reentrant calls to onPoolFailure.
TEST_F(TcpProxyTest, ConnectAttemptsUpstreamLocalFailReentrant) {
  envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy config = defaultConfig();
  config.mutable_max_connect_attempts()->set_value(2);

  // Set up a call to onPoolFailure from inside the first newConnection call.
  // This simulates a connection failure from under the stack of newStream.
  new_connection_functions_.push_back(
      [&](Tcp::ConnectionPool::Cancellable*) -> Tcp::ConnectionPool::Cancellable* {
        raiseEventUpstreamConnectFailed(0,
                                        ConnectionPool::PoolFailureReason::LocalConnectionFailure);
        return nullptr;
      });

  setup(2, config);

  // Make sure the last connection pool to be created is the one which gets the
  // cancellation call.
  EXPECT_CALL(*conn_pool_handles_.at(0), cancel(Tcp::ConnectionPool::CancelPolicy::CloseExcess))
      .Times(0);
  EXPECT_CALL(*conn_pool_handles_.at(1), cancel(Tcp::ConnectionPool::CancelPolicy::CloseExcess));
  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
}

// Test that reconnect is attempted after a remote connect failure
TEST_F(TcpProxyTest, ConnectAttemptsUpstreamRemoteFail) {
  envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy config = defaultConfig();
  config.mutable_max_connect_attempts()->set_value(2);
  setup(2, config);

  raiseEventUpstreamConnectFailed(0, ConnectionPool::PoolFailureReason::RemoteConnectionFailure);
  raiseEventUpstreamConnected(1);

  EXPECT_EQ(0U, factory_context_.server_factory_context_.cluster_manager_.thread_local_cluster_
                    .cluster_.info_->stats_store_.counter("upstream_cx_connect_attempts_exceeded")
                    .value());
}

// Test that reconnect is attempted after a connect timeout.
TEST_F(TcpProxyTest, ConnectAttemptsUpstreamTimeout) {
  envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy config = defaultConfig();
  config.mutable_max_connect_attempts()->set_value(2);
  setup(2, config);

  raiseEventUpstreamConnectFailed(0, ConnectionPool::PoolFailureReason::Timeout);
  raiseEventUpstreamConnected(1);

  EXPECT_EQ(0U, factory_context_.server_factory_context_.cluster_manager_.thread_local_cluster_
                    .cluster_.info_->stats_store_.counter("upstream_cx_connect_attempts_exceeded")
                    .value());
}

// Test that only the configured number of connect attempts occur
TEST_F(TcpProxyTest, ConnectAttemptsLimit) {
  envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy config =
      accessLogConfig("%RESPONSE_FLAGS%");
  config.mutable_max_connect_attempts()->set_value(3);
  setup(3, config);

  EXPECT_CALL(upstream_hosts_.at(0)->outlier_detector_,
              putResult(Upstream::Outlier::Result::LocalOriginTimeout, _));
  EXPECT_CALL(upstream_hosts_.at(1)->outlier_detector_,
              putResult(Upstream::Outlier::Result::LocalOriginConnectFailed, _));
  EXPECT_CALL(upstream_hosts_.at(2)->outlier_detector_,
              putResult(Upstream::Outlier::Result::LocalOriginConnectFailed, _));

  EXPECT_CALL(filter_callbacks_.connection_, close(Network::ConnectionCloseType::NoFlush, _));

  // Try both failure modes
  raiseEventUpstreamConnectFailed(0, ConnectionPool::PoolFailureReason::Timeout);
  timeSystem().advanceTimeWait(std::chrono::microseconds(10));
  raiseEventUpstreamConnectFailed(1, ConnectionPool::PoolFailureReason::RemoteConnectionFailure);
  timeSystem().advanceTimeWait(std::chrono::microseconds(15));
  raiseEventUpstreamConnectFailed(2, ConnectionPool::PoolFailureReason::RemoteConnectionFailure);

  const absl::optional<std::chrono::nanoseconds> upstream_connection_establishment_latency =
      filter_->getStreamInfo().upstreamInfo()->upstreamTiming().connectionPoolCallbackLatency();
  ASSERT_TRUE(upstream_connection_establishment_latency.has_value());
  EXPECT_EQ(std::chrono::microseconds(25), upstream_connection_establishment_latency.value());

  filter_.reset();
  EXPECT_EQ(access_log_data_, "UF,URX");
}

TEST_F(TcpProxyTest, ConnectedNoOp) {
  setup(1);
  raiseEventUpstreamConnected(0);

  upstream_callbacks_->onEvent(Network::ConnectionEvent::Connected);

  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
}

// Test that the tcp proxy sends the correct notifications to the outlier detector
TEST_F(TcpProxyTest, OutlierDetection) {
  envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy config = defaultConfig();
  config.mutable_max_connect_attempts()->set_value(3);
  setup(3, config);

  EXPECT_CALL(upstream_hosts_.at(0)->outlier_detector_,
              putResult(Upstream::Outlier::Result::LocalOriginTimeout, _));
  raiseEventUpstreamConnectFailed(0, ConnectionPool::PoolFailureReason::Timeout);

  EXPECT_CALL(upstream_hosts_.at(1)->outlier_detector_,
              putResult(Upstream::Outlier::Result::LocalOriginConnectFailed, _));
  raiseEventUpstreamConnectFailed(1, ConnectionPool::PoolFailureReason::RemoteConnectionFailure);

  EXPECT_CALL(upstream_hosts_.at(2)->outlier_detector_,
              putResult(Upstream::Outlier::Result::LocalOriginConnectSuccessFinal, _));
  raiseEventUpstreamConnected(2);
}

TEST_F(TcpProxyTest, UpstreamDisconnectDownstreamFlowControl) {
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

TEST_F(TcpProxyTest, DownstreamDisconnectRemote) {
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

TEST_F(TcpProxyTest, DownstreamDisconnectLocal) {
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

TEST_F(TcpProxyTest, UpstreamConnectTimeout) {
  setup(1, accessLogConfig("%RESPONSE_FLAGS%"));

  EXPECT_CALL(filter_callbacks_.connection_, close(Network::ConnectionCloseType::NoFlush, _));
  raiseEventUpstreamConnectFailed(0, ConnectionPool::PoolFailureReason::Timeout);

  filter_.reset();
  EXPECT_EQ(access_log_data_, "UF,URX");
}

TEST_F(TcpProxyTest, UpstreamClusterNotFound) {
  setup(0, accessLogConfig("%RESPONSE_FLAGS%"));

  EXPECT_CALL(factory_context_.server_factory_context_.cluster_manager_, getThreadLocalCluster(_))
      .WillRepeatedly(Return(nullptr));
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onNewConnection());

  filter_.reset();
  EXPECT_EQ(access_log_data_.value(), "NC");
}

TEST_F(TcpProxyTest, NoHost) {
  EXPECT_CALL(filter_callbacks_.connection_, close(Network::ConnectionCloseType::NoFlush, _));
  setup(0, accessLogConfig("%RESPONSE_FLAGS%"));
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onNewConnection());
  filter_.reset();
  EXPECT_EQ(access_log_data_, "UH");
}

TEST_F(TcpProxyTest, RouteWithMetadataMatch) {
  auto v1 = ProtobufWkt::Value();
  v1.set_string_value("v1");
  auto v2 = ProtobufWkt::Value();
  v2.set_number_value(2.0);
  auto v3 = ProtobufWkt::Value();
  v3.set_bool_value(true);

  std::vector<Router::MetadataMatchCriterionImpl> criteria = {{"a", v1}, {"b", v2}, {"c", v3}};

  auto metadata_struct = ProtobufWkt::Struct();
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
TEST_F(TcpProxyTest, WeightedClusterWithMetadataMatch) {
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

  ProtobufWkt::Value v0, v1, v2;
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
                tcpConnPool(_, _))
        .WillOnce(DoAll(SaveArg<1>(&context), Return(absl::nullopt)));
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
                tcpConnPool(_, _))
        .WillOnce(DoAll(SaveArg<1>(&context), Return(absl::nullopt)));
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
TEST_F(TcpProxyTest, StreamInfoDynamicMetadata) {
  configure(defaultConfig());

  ProtobufWkt::Value val;
  val.set_string_value("val");

  envoy::config::core::v3::Metadata metadata;
  ProtobufWkt::Struct& map =
      (*metadata.mutable_filter_metadata())[Envoy::Config::MetadataFilters::get().ENVOY_LB];
  (*map.mutable_fields())["test"] = val;
  EXPECT_CALL(filter_callbacks_.connection_.stream_info_, dynamicMetadata())
      .WillRepeatedly(ReturnRef(metadata));

  filter_ =
      std::make_unique<Filter>(config_, factory_context_.server_factory_context_.cluster_manager_);
  filter_->initializeReadFilterCallbacks(filter_callbacks_);

  Upstream::LoadBalancerContext* context;

  EXPECT_CALL(factory_context_.server_factory_context_.cluster_manager_.thread_local_cluster_,
              tcpConnPool(_, _))
      .WillOnce(DoAll(SaveArg<1>(&context), Return(absl::nullopt)));
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
TEST_F(TcpProxyTest, StreamInfoDynamicMetadataAndConfigMerged) {
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

  ProtobufWkt::Value v0, v1, v2;
  v0.set_string_value("v0");
  v1.set_string_value("from_streaminfo"); // 'v1' is overridden with this value by streamInfo.
  v2.set_string_value("v2");
  HashedValue hv0(v0), hv1(v1), hv2(v2);

  envoy::config::core::v3::Metadata metadata;
  ProtobufWkt::Struct& map =
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
              tcpConnPool(_, _))
      .WillOnce(DoAll(SaveArg<1>(&context), Return(absl::nullopt)));
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

TEST_F(TcpProxyTest, DisconnectBeforeData) {
  configure(defaultConfig());
  filter_ =
      std::make_unique<Filter>(config_, factory_context_.server_factory_context_.cluster_manager_);
  filter_->initializeReadFilterCallbacks(filter_callbacks_);

  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
}

// Test that if the downstream connection is closed before the upstream connection
// is established, the upstream connection is cancelled.
TEST_F(TcpProxyTest, RemoteClosedBeforeUpstreamConnected) {
  setup(1);
  EXPECT_CALL(*conn_pool_handles_.at(0), cancel(Tcp::ConnectionPool::CancelPolicy::CloseExcess));
  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
}

// Test that if the downstream connection is closed before the upstream connection
// is established, the upstream connection is cancelled.
TEST_F(TcpProxyTest, LocalClosedBeforeUpstreamConnected) {
  setup(1);
  EXPECT_CALL(*conn_pool_handles_.at(0), cancel(Tcp::ConnectionPool::CancelPolicy::CloseExcess));
  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::LocalClose);
}

TEST_F(TcpProxyTest, UpstreamConnectFailure) {
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

TEST_F(TcpProxyTest, UpstreamConnectionLimit) {
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

// Tests that the idle timer closes both connections, and gets updated when either
// connection has activity.
TEST_F(TcpProxyTest, IdleTimeout) {
  envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy config = defaultConfig();
  config.mutable_idle_timeout()->set_seconds(1);
  setup(1, config);

  Event::MockTimer* idle_timer = new Event::MockTimer(&filter_callbacks_.connection_.dispatcher_);
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
TEST_F(TcpProxyTest, IdleTimerDisabledDownstreamClose) {
  envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy config = defaultConfig();
  config.mutable_idle_timeout()->set_seconds(1);
  setup(1, config);

  Event::MockTimer* idle_timer = new Event::MockTimer(&filter_callbacks_.connection_.dispatcher_);
  EXPECT_CALL(*idle_timer, enableTimer(std::chrono::milliseconds(1000), _));
  raiseEventUpstreamConnected(0);

  EXPECT_CALL(*idle_timer, disableTimer());
  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
}

// Tests that the idle timer is disabled when the upstream connection is closed.
TEST_F(TcpProxyTest, IdleTimerDisabledUpstreamClose) {
  envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy config = defaultConfig();
  config.mutable_idle_timeout()->set_seconds(1);
  setup(1, config);

  Event::MockTimer* idle_timer = new Event::MockTimer(&filter_callbacks_.connection_.dispatcher_);
  EXPECT_CALL(*idle_timer, enableTimer(std::chrono::milliseconds(1000), _));
  raiseEventUpstreamConnected(0);

  EXPECT_CALL(*idle_timer, disableTimer());
  upstream_callbacks_->onEvent(Network::ConnectionEvent::RemoteClose);
}

// Tests that flushing data during an idle timeout doesn't cause problems.
TEST_F(TcpProxyTest, IdleTimeoutWithOutstandingDataFlushed) {
  envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy config = defaultConfig();
  config.mutable_idle_timeout()->set_seconds(1);
  setup(1, config);

  Event::MockTimer* idle_timer = new Event::MockTimer(&filter_callbacks_.connection_.dispatcher_);
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
TEST_F(TcpProxyTest, AccessLogBytesMeterData) {
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
TEST_F(TcpProxyTest, AccessLogUpstreamHost) {
  setup(1, accessLogConfig("%UPSTREAM_HOST% %UPSTREAM_CLUSTER%"));
  raiseEventUpstreamConnected(0);
  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
  filter_.reset();
  EXPECT_EQ(access_log_data_, "127.0.0.1:80 observability_name");
}

// Test that access log field %UPSTREAM_LOCAL_ADDRESS% is correctly logged.
TEST_F(TcpProxyTest, AccessLogUpstreamLocalAddress) {
  setup(1, accessLogConfig("%UPSTREAM_LOCAL_ADDRESS%"));
  raiseEventUpstreamConnected(0);
  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
  filter_.reset();
  EXPECT_EQ(access_log_data_, "2.2.2.2:50000");
}

// Test that access log fields %DOWNSTREAM_PEER_URI_SAN% is correctly logged.
TEST_F(TcpProxyTest, AccessLogPeerUriSan) {
  filter_callbacks_.connection_.stream_info_.downstream_connection_info_provider_->setLocalAddress(
      Network::Utility::resolveUrl("tcp://1.1.1.2:20000"));
  filter_callbacks_.connection_.stream_info_.downstream_connection_info_provider_->setRemoteAddress(
      Network::Utility::resolveUrl("tcp://1.1.1.1:40000"));

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
TEST_F(TcpProxyTest, AccessLogTlsSessionId) {
  filter_callbacks_.connection_.stream_info_.downstream_connection_info_provider_->setLocalAddress(
      Network::Utility::resolveUrl("tcp://1.1.1.2:20000"));
  filter_callbacks_.connection_.stream_info_.downstream_connection_info_provider_->setRemoteAddress(
      Network::Utility::resolveUrl("tcp://1.1.1.1:40000"));

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
TEST_F(TcpProxyTest, AccessLogDownstreamAddress) {
  filter_callbacks_.connection_.stream_info_.downstream_connection_info_provider_->setLocalAddress(
      Network::Utility::resolveUrl("tcp://1.1.1.2:20000"));
  filter_callbacks_.connection_.stream_info_.downstream_connection_info_provider_->setRemoteAddress(
      Network::Utility::resolveUrl("tcp://1.1.1.1:40000"));
  setup(1, accessLogConfig("%DOWNSTREAM_REMOTE_ADDRESS_WITHOUT_PORT% %DOWNSTREAM_LOCAL_ADDRESS%"));
  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
  filter_.reset();
  EXPECT_EQ(access_log_data_, "1.1.1.1 1.1.1.2:20000");
}

// Test that intermediate log entry by field %ACCESS_LOG_TYPE%.
TEST_F(TcpProxyTest, IntermediateLogEntry) {
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
      .WillOnce(Invoke([](const Formatter::HttpFormatterContext& log_context,
                          const StreamInfo::StreamInfo& stream_info) {
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
      .WillOnce(Invoke([](const Formatter::HttpFormatterContext& log_context,
                          const StreamInfo::StreamInfo& stream_info) {
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
      .WillOnce(Invoke(
          [](const Formatter::HttpFormatterContext& log_context, const StreamInfo::StreamInfo&) {
            EXPECT_EQ(log_context.accessLogType(), AccessLog::AccessLogType::TcpConnectionEnd);
          }));

  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
  filter_.reset();

  EXPECT_EQ(access_log_data_.value(),
            AccessLogType_Name(AccessLog::AccessLogType::TcpConnectionEnd));
}

TEST_F(TcpProxyTest, TestAccessLogOnUpstreamConnected) {
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

TEST_F(TcpProxyTest, AccessLogUpstreamSSLConnection) {
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

// Tests that upstream flush works properly with no idle timeout configured.
TEST_F(TcpProxyTest, UpstreamFlushNoTimeout) {
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
TEST_F(TcpProxyTest, UpstreamFlushTimeoutConfigured) {
  envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy config = defaultConfig();
  config.mutable_idle_timeout()->set_seconds(1);
  setup(1, config);

  NiceMock<Event::MockTimer>* idle_timer =
      new NiceMock<Event::MockTimer>(&filter_callbacks_.connection_.dispatcher_);
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
TEST_F(TcpProxyTest, UpstreamFlushTimeoutExpired) {
  envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy config = defaultConfig();
  config.mutable_idle_timeout()->set_seconds(1);
  setup(1, config);

  NiceMock<Event::MockTimer>* idle_timer =
      new NiceMock<Event::MockTimer>(&filter_callbacks_.connection_.dispatcher_);
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
  idle_timer->invokeCallback();
  EXPECT_EQ(1U, config_->stats().upstream_flush_total_.value());
  EXPECT_EQ(0U, config_->stats().upstream_flush_active_.value());
  EXPECT_EQ(1U, config_->stats().idle_timeout_.value());
}

// Tests that upstream flush will close a connection if it reads data from the upstream
// connection after the downstream connection is closed (nowhere to send it).
TEST_F(TcpProxyTest, UpstreamFlushReceiveUpstreamData) {
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

TEST_F(TcpProxyTest, UpstreamSocketOptionsReturnedEmpty) {
  setup(1);
  auto options = filter_->upstreamSocketOptions();
  EXPECT_EQ(options, nullptr);
}

TEST_F(TcpProxyTest, TcpProxySetRedirectRecordsToUpstream) {
  setup(1, true);
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
TEST_F(TcpProxyTest, ShareFilterState) {
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
TEST_F(TcpProxyTest, AccessDownstreamAndUpstreamProperties) {
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

TEST_F(TcpProxyTest, PickClusterOnUpstreamFailure) {
  auto config = defaultConfig();
  set2Cluster(config);
  config.mutable_max_connect_attempts()->set_value(2);

  // The random number lead into picking the first one in the weighted clusters.
  EXPECT_CALL(factory_context_.server_factory_context_.api_.random_, random()).WillOnce(Return(0));
  EXPECT_CALL(factory_context_.server_factory_context_.cluster_manager_,
              getThreadLocalCluster("fake_cluster_0"))
      .WillOnce(
          Return(&factory_context_.server_factory_context_.cluster_manager_.thread_local_cluster_));

  setup(1, config);

  // The random number lead into picking the second cluster.
  EXPECT_CALL(factory_context_.server_factory_context_.api_.random_, random()).WillOnce(Return(1));

  EXPECT_CALL(factory_context_.server_factory_context_.cluster_manager_,
              getThreadLocalCluster("fake_cluster_1"))
      .WillOnce(
          Return(&factory_context_.server_factory_context_.cluster_manager_.thread_local_cluster_));

  raiseEventUpstreamConnectFailed(0, ConnectionPool::PoolFailureReason::LocalConnectionFailure);
  EXPECT_EQ(0U, factory_context_.server_factory_context_.cluster_manager_.thread_local_cluster_
                    .cluster_.info_->stats_store_.counter("upstream_cx_connect_attempts_exceeded")
                    .value());
}

// Verify that odcds callback does not re-pick cluster. Upstream connect failure does.
TEST_F(TcpProxyTest, OnDemandCallbackStickToTheSelectedCluster) {
  auto config = onDemandConfig();
  set2Cluster(config);
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

  setup(1, config);

  // When the on-demand look up callback is invoked, the target cluster should not change.
  // The behavior is verified by checking the random() which is used during cluster re-pick.
  EXPECT_CALL(factory_context_.server_factory_context_.api_.random_, random()).Times(0);
  std::invoke(*cluster_discovery_callback, Upstream::ClusterDiscoveryStatus::Available);

  // Start to raise connect failure.

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

  raiseEventUpstreamConnectFailed(0, ConnectionPool::PoolFailureReason::LocalConnectionFailure);
  EXPECT_EQ(0U, factory_context_.server_factory_context_.cluster_manager_.thread_local_cluster_
                    .cluster_.info_->stats_store_.counter("upstream_cx_connect_attempts_exceeded")
                    .value());

  EXPECT_CALL(filter_callbacks_.connection_, close(_, _));
  std::invoke(*cluster_discovery_callback, Upstream::ClusterDiscoveryStatus::Missing);
}

// Verify the on demand api is not invoked when the target thread local cluster is present.
TEST_F(TcpProxyTest, OdcdsIsIgnoredIfClusterExists) {
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
TEST_F(TcpProxyTest, OdcdsCancelIfConnectionClose) {
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
TEST_F(TcpProxyTest, OdcdsBasicDownstreamLocalClose) {
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
TEST_F(TcpProxyTest, OdcdsClusterMissingCauseConnectionClose) {
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
TEST_F(TcpProxyTest, UpstreamConnectFailureStreamInfoAccessLog) {
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
TEST_F(TcpProxyTest, UpstreamStartSecureTransport) {
  envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy config = defaultConfig();

  setup(1, config);
  raiseEventUpstreamConnected(0);
  EXPECT_CALL(*upstream_connections_.at(0), startSecureTransport);
  filter_->startUpstreamSecureTransport();
}

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

} // namespace
} // namespace TcpProxy
} // namespace Envoy
