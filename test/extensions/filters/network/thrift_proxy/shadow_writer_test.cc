#include <memory>

#include "envoy/extensions/filters/network/thrift_proxy/v3/thrift_proxy.pb.h"
#include "envoy/tcp/conn_pool.h"

#include "common/buffer/buffer_impl.h"

#include "extensions/filters/network/thrift_proxy/router/shadow_writer_impl.h"

#include "test/extensions/filters/network/thrift_proxy/mocks.h"
#include "test/extensions/filters/network/thrift_proxy/utility.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/server/factory_context.h"
#include "test/mocks/upstream/host.h"
#include "test/test_common/printers.h"
#include "test/test_common/registry.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::Return;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ThriftProxy {
namespace Router {

class ShadowWriterTest : public testing::Test {
public:
  ShadowWriterTest() {
    shadow_writer_ = std::make_shared<ShadowWriterImpl>(cm_, context_.scope(), dispatcher_);
    metadata_ = std::make_shared<MessageMetadata>();
    metadata_->setMethodName("ping");
    metadata_->setMessageType(MessageType::Call);
    metadata_->setSequenceId(1);
  }

  NiceMock<Upstream::MockClusterManager> cm_;
  NiceMock<Server::Configuration::MockFactoryContext> context_;
  NiceMock<Event::MockDispatcher> dispatcher_;
  Envoy::ConnectionPool::MockCancellable cancellable_;
  std::shared_ptr<ShadowWriterImpl> shadow_writer_;
  MessageMetadataSharedPtr metadata_;
  NiceMock<Tcp::ConnectionPool::MockInstance> conn_pool_;
};

TEST_F(ShadowWriterTest, SubmitClusterNotFound) {
  EXPECT_CALL(cm_, getThreadLocalCluster(_)).WillOnce(Return(nullptr));
  auto request_handle = shadow_writer_->submit("shadow_cluster", metadata_, TransportType::Framed,
                                               ProtocolType::Binary);
  EXPECT_EQ(absl::nullopt, request_handle);
}

TEST_F(ShadowWriterTest, SubmitClusterInMaintenance) {
  std::shared_ptr<Upstream::MockThreadLocalCluster> cluster =
      std::make_shared<NiceMock<Upstream::MockThreadLocalCluster>>();
  EXPECT_CALL(cm_, getThreadLocalCluster(_)).WillOnce(Return(cluster.get()));
  EXPECT_CALL(*cluster->cluster_.info_, maintenanceMode()).WillOnce(Return(true));
  auto request_handle = shadow_writer_->submit("shadow_cluster", metadata_, TransportType::Framed,
                                               ProtocolType::Binary);
  EXPECT_EQ(absl::nullopt, request_handle);
}

TEST_F(ShadowWriterTest, SubmitNoHealthyUpstream) {
  std::shared_ptr<Upstream::MockThreadLocalCluster> cluster =
      std::make_shared<NiceMock<Upstream::MockThreadLocalCluster>>();
  EXPECT_CALL(cm_, getThreadLocalCluster(_)).WillOnce(Return(cluster.get()));
  EXPECT_CALL(*cluster->cluster_.info_, maintenanceMode()).WillOnce(Return(false));
  EXPECT_CALL(*cluster, tcpConnPool(_, _)).WillOnce(Return(absl::nullopt));
  auto request_handle = shadow_writer_->submit("shadow_cluster", metadata_, TransportType::Framed,
                                               ProtocolType::Binary);
  EXPECT_EQ(absl::nullopt, request_handle);
}

TEST_F(ShadowWriterTest, SubmitConnectionNotReady) {
  std::shared_ptr<Upstream::MockThreadLocalCluster> cluster =
      std::make_shared<NiceMock<Upstream::MockThreadLocalCluster>>();
  EXPECT_CALL(cm_, getThreadLocalCluster(_)).WillOnce(Return(cluster.get()));
  EXPECT_CALL(*cluster->cluster_.info_, maintenanceMode()).WillOnce(Return(false));
  EXPECT_CALL(*cluster, tcpConnPool(_, _))
      .WillOnce(Return(Upstream::TcpPoolData([]() {}, &conn_pool_)));
  EXPECT_CALL(cancellable_, cancel(_));
  EXPECT_CALL(conn_pool_, newConnection(_))
      .WillOnce(Invoke([&](Tcp::ConnectionPool::Callbacks&) -> Tcp::ConnectionPool::Cancellable* {
        return &cancellable_;
      }));

  auto request_handle = shadow_writer_->submit("shadow_cluster", metadata_, TransportType::Framed,
                                               ProtocolType::Binary);
  EXPECT_NE(absl::nullopt, request_handle);
  EXPECT_TRUE(request_handle.value().get().waitingForConnection());
}

} // namespace Router
} // namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
