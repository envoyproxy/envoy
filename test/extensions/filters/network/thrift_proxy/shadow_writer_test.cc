#include <memory>

#include "envoy/extensions/filters/network/thrift_proxy/v3/thrift_proxy.pb.h"
#include "envoy/tcp/conn_pool.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/extensions/filters/network/thrift_proxy/router/shadow_writer_impl.h"

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
using testing::ReturnRef;

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

    host_ = std::make_shared<NiceMock<Upstream::MockHost>>();
  }

  NiceMock<Upstream::MockClusterManager> cm_;
  NiceMock<Server::Configuration::MockFactoryContext> context_;
  NiceMock<Event::MockDispatcher> dispatcher_;
  Envoy::ConnectionPool::MockCancellable cancellable_;
  std::shared_ptr<ShadowWriterImpl> shadow_writer_;
  MessageMetadataSharedPtr metadata_;
  NiceMock<Tcp::ConnectionPool::MockInstance> conn_pool_;
  std::shared_ptr<NiceMock<Upstream::MockHost>> host_;
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

  EXPECT_EQ(
      1UL,
      cluster->cluster_.info_->statsScope().counterFromString("thrift.upstream_rq_call").value());
}

TEST_F(ShadowWriterTest, ShadowRequestPoolReady) {
  std::shared_ptr<Upstream::MockThreadLocalCluster> cluster =
      std::make_shared<NiceMock<Upstream::MockThreadLocalCluster>>();
  Upstream::ClusterInfoConstSharedPtr cluster_info = cluster->info();
  Upstream::TcpPoolData conn_pool_data([]() {}, &conn_pool_);
  NiceMock<Network::MockClientConnection> connection;

  auto request_ptr =
      std::make_unique<ShadowRequest>(*shadow_writer_, std::move(cluster_info), conn_pool_data,
                                      metadata_, TransportType::Framed, ProtocolType::Binary);

  EXPECT_CALL(conn_pool_, newConnection(_))
      .WillOnce(Invoke([&](Tcp::ConnectionPool::Callbacks&) -> Tcp::ConnectionPool::Cancellable* {
        auto data = std::make_unique<NiceMock<Envoy::Tcp::ConnectionPool::MockConnectionData>>();
        EXPECT_CALL(*data, connection()).WillOnce(ReturnRef(connection));
        request_ptr->onPoolReady(std::move(data), host_);
        return nullptr;
      }));

  request_ptr->start();

  EXPECT_CALL(connection, write(_, false));

  Buffer::OwnedImpl buffer;
  buffer.add("hello");
  request_ptr->tryWriteRequest(buffer);
}

TEST_F(ShadowWriterTest, ShadowRequestPoolFailure) {
  std::shared_ptr<Upstream::MockThreadLocalCluster> cluster =
      std::make_shared<NiceMock<Upstream::MockThreadLocalCluster>>();
  Upstream::ClusterInfoConstSharedPtr cluster_info = cluster->info();
  Upstream::TcpPoolData conn_pool_data([]() {}, &conn_pool_);
  NiceMock<Network::MockClientConnection> connection;

  auto request_ptr =
      std::make_unique<ShadowRequest>(*shadow_writer_, std::move(cluster_info), conn_pool_data,
                                      metadata_, TransportType::Framed, ProtocolType::Binary);

  EXPECT_CALL(conn_pool_, newConnection(_))
      .WillOnce(Invoke([&](Tcp::ConnectionPool::Callbacks&) -> Tcp::ConnectionPool::Cancellable* {
        auto data = std::make_unique<NiceMock<Envoy::Tcp::ConnectionPool::MockConnectionData>>();
        EXPECT_CALL(*data, connection()).Times(0);
        request_ptr->onPoolFailure(ConnectionPool::PoolFailureReason::Overflow, nullptr);
        return nullptr;
      }));

  request_ptr->start();

  EXPECT_CALL(connection, write(_, false)).Times(0);
  Buffer::OwnedImpl buffer;
  request_ptr->tryWriteRequest(buffer);
}

struct MockNullResponseDecoder : public NullResponseDecoder {
  MockNullResponseDecoder(Transport& transport, Protocol& protocol)
      : NullResponseDecoder(transport, protocol) {}

  MOCK_METHOD(bool, onData, (Buffer::Instance & data), ());
};

TEST_F(ShadowWriterTest, ShadowRequestOnUpstreamData) {
  std::shared_ptr<Upstream::MockThreadLocalCluster> cluster =
      std::make_shared<NiceMock<Upstream::MockThreadLocalCluster>>();
  Upstream::ClusterInfoConstSharedPtr cluster_info = cluster->info();
  Upstream::TcpPoolData conn_pool_data([]() {}, &conn_pool_);
  NiceMock<Network::MockClientConnection> connection;

  auto request_ptr =
      std::make_unique<ShadowRequest>(*shadow_writer_, std::move(cluster_info), conn_pool_data,
                                      metadata_, TransportType::Framed, ProtocolType::Binary);

  EXPECT_CALL(conn_pool_, newConnection(_))
      .WillOnce(Invoke([&](Tcp::ConnectionPool::Callbacks&) -> Tcp::ConnectionPool::Cancellable* {
        auto data = std::make_unique<NiceMock<Envoy::Tcp::ConnectionPool::MockConnectionData>>();
        EXPECT_CALL(*data, connection()).WillOnce(ReturnRef(connection));
        request_ptr->onPoolReady(std::move(data), host_);
        return nullptr;
      }));

  request_ptr->start();

  EXPECT_CALL(connection, write(_, false));

  Buffer::OwnedImpl buffer;
  buffer.add("hello");
  request_ptr->tryWriteRequest(buffer);

  // Prepare response metadata & data processing.
  MessageMetadataSharedPtr response_metadata = std::make_shared<MessageMetadata>();
  response_metadata->setMessageType(MessageType::Reply);
  response_metadata->setSequenceId(1);

  auto transport_ptr =
      NamedTransportConfigFactory::getFactory(TransportType::Framed).createTransport();
  auto protocol_ptr = NamedProtocolConfigFactory::getFactory(ProtocolType::Binary).createProtocol();
  auto decoder_ptr = std::make_unique<MockNullResponseDecoder>(*transport_ptr, *protocol_ptr);
  decoder_ptr->messageBegin(response_metadata);
  decoder_ptr->success_ = true;
  EXPECT_CALL(*decoder_ptr, onData(_)).WillOnce(Return(true));
  request_ptr->setResponseDecoder(std::move(decoder_ptr));

  Buffer::OwnedImpl response_buffer;
  request_ptr->onUpstreamData(response_buffer, false);

  // Check stats.
  EXPECT_EQ(1UL, cluster->cluster_.info_->statsScope()
                     .counterFromString("thrift.upstream_resp_reply")
                     .value());
  EXPECT_EQ(1UL, cluster->cluster_.info_->statsScope()
                     .counterFromString("thrift.upstream_resp_success")
                     .value());
}

} // namespace Router
} // namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
