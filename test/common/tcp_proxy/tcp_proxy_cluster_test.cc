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
#include "envoy/upstream/thread_local_cluster.h"

#include "common/buffer/buffer_impl.h"
#include "common/network/address_impl.h"
#include "common/network/application_protocol.h"
#include "common/network/transport_socket_options_impl.h"
#include "common/network/upstream_server_name.h"
#include "common/router/metadatamatchcriteria_impl.h"
#include "common/tcp_proxy/tcp_proxy.h"
#include "common/upstream/cluster_manager_impl.h"
#include "common/upstream/upstream_impl.h"

#include "extensions/access_loggers/well_known_names.h"

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
#include "test/mocks/upstream/host.h"
#include "test/test_common/test_runtime.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace TcpProxy {

namespace {

using ::Envoy::Network::UpstreamServerName;
using ::testing::_;
using ::testing::DoAll;
using ::testing::Invoke;
using ::testing::InvokeWithoutArgs;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::ReturnPointee;
using ::testing::ReturnRef;
using ::testing::SaveArg;

class TcpProxyFutureClusterTest : public TcpProxyTestBase {
public:
  using TcpProxyTestBase::setup;
  void setup(uint32_t connections,
             const envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy& config) override {
    FANCY_LOG(info, "lambdai: in TcpProxyFutureClusterTest:setup/2");
    configure(config);
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
      upstream_connections_.at(i)->stream_info_.downstream_address_provider_->setLocalAddress(
          upstream_local_address_);
      EXPECT_CALL(*upstream_connections_.at(i), dispatcher())
          .WillRepeatedly(ReturnRef(filter_callbacks_.connection_.dispatcher_));
    }

    {
      testing::InSequence sequence;
      for (uint32_t i = 0; i < connections; i++) {
        EXPECT_CALL(factory_context_.cluster_manager_, futureThreadLocalCluster(_))
            .WillOnce(testing::WithArg<0>(Invoke([this, i](absl::string_view name) {
              future_clusters_.push_back(std::make_shared<Upstream::DelayedFutureCluster>(
                  name, factory_context_.cluster_manager_, per_cluster_ready_flags_[i]));
              return future_clusters_.back();
            })));
        EXPECT_CALL(factory_context_.cluster_manager_.thread_local_cluster_, tcpConnPool(_, _))
            .WillOnce(Return(&conn_pool_))
            .RetiresOnSaturation();
        EXPECT_CALL(conn_pool_, newConnection(_))
            .WillOnce(Invoke(
                [=](Tcp::ConnectionPool::Callbacks& cb) -> Tcp::ConnectionPool::Cancellable* {
                  conn_pool_callbacks_.push_back(&cb);

                  return onNewConnection(conn_pool_handles_.at(i).get());
                }))
            .RetiresOnSaturation();
      }
      EXPECT_CALL(factory_context_.cluster_manager_.thread_local_cluster_, tcpConnPool(_, _))
          .WillRepeatedly(Return(nullptr));
    }

    {
      filter_ = std::make_unique<Filter>(config_, factory_context_.cluster_manager_);
      EXPECT_CALL(filter_callbacks_.connection_, enableHalfClose(true));
      EXPECT_CALL(filter_callbacks_.connection_, readDisable(true));
      filter_->initializeReadFilterCallbacks(filter_callbacks_);
      filter_callbacks_.connection_.streamInfo().setDownstreamSslConnection(
          filter_callbacks_.connection_.ssl());
      FANCY_LOG(info, "lambdai: pre onNewConnection");
      EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onNewConnection());

      EXPECT_EQ(absl::optional<uint64_t>(), filter_->computeHashKey());
      EXPECT_EQ(&filter_callbacks_.connection_, filter_->downstreamConnection());
      EXPECT_EQ(nullptr, filter_->metadataMatchCriteria());
    }
  }
  // Preallocate 16 bool elements to accommodate multiple upstream connection tests. vector<bool>
  // cannot be used because we cannot get reference to any bool element in vector<bool>.
  std::array<bool, 16> per_cluster_ready_flags_{false};
  std::vector<std::shared_ptr<Upstream::DelayedFutureCluster>> future_clusters_;
};

// Test that if the downstream connection is closed before the upstream connection
// is established, the upstream connection is cancelled.
TEST_F(TcpProxyFutureClusterTest, LocalClosedBeforeClusterIsReady) {
  auto future = std::make_shared<Upstream::DelayedFutureCluster>(
      "fake_cluster", factory_context_.cluster_manager_, per_cluster_ready_flags_[0]);
  EXPECT_CALL(factory_context_.cluster_manager_, futureThreadLocalCluster(_))
      .WillOnce(Return(future));
  setup(0);
  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::LocalClose);
}

TEST_F(TcpProxyFutureClusterTest, ClusterIsNotReady) {
  auto future = std::make_shared<Upstream::DelayedFutureCluster>(
      "fake_cluster", factory_context_.cluster_manager_, per_cluster_ready_flags_[0]);
  EXPECT_CALL(factory_context_.cluster_manager_, futureThreadLocalCluster(_))
      .WillOnce(Return(future));
  setup(0, accessLogConfig("%RESPONSE_FLAGS%"));
  filter_.reset();
  EXPECT_EQ(access_log_data_, "UH");
}

// Test that downstream is closed after an upstream LocalClose.
TEST_F(TcpProxyFutureClusterTest, UpstreamLocalDisconnect) {
  setup(1);
  per_cluster_ready_flags_[0] = true;
  future_clusters_[0]->readyCallback();

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
TEST_F(TcpProxyFutureClusterTest, UpstreamRemoteDisconnect) {
  setup(1);
  per_cluster_ready_flags_[0] = true;
  future_clusters_[0]->readyCallback();

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

} // namespace
} // namespace TcpProxy
} // namespace Envoy