#include <string>

#include "envoy/registry/registry.h"

#include "source/common/network/filter_state_dst_address.h"

#include "test/mocks/server/factory_context.h"
#include "test/mocks/thread/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

#include "absl/strings/str_format.h"
#include "contrib/golang/common/dso/test/mocks.h"
#include "contrib/golang/filters/network/source/upstream.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Golang {
namespace {

class UpstreamConnTest : public testing::Test {
public:
  UpstreamConnTest() { ENVOY_LOG_MISC(info, "test"); }
  ~UpstreamConnTest() override = default;

  void initialize() {
    EXPECT_CALL(slot_allocator_, allocateSlot())
        .WillRepeatedly(Invoke(&slot_allocator_, &ThreadLocal::MockInstance::allocateSlotMock));
    context_.server_factory_context_.cluster_manager_.initializeClusters({"plainText"}, {});
    context_.server_factory_context_.cluster_manager_.initializeThreadLocalClusters({"plainText"});
    ON_CALL(context_.server_factory_context_.api_, threadFactory())
        .WillByDefault(ReturnRef(thread_factory_));
    UpstreamConn::initThreadLocalStorage(context_, slot_allocator_);
    dso_ = std::make_shared<Dso::MockNetworkFilterDsoImpl>();
    upConn_ = std::make_shared<UpstreamConn>(addr_, dso_, 0, &dispatcher_);
  }

  ThreadLocal::MockInstance slot_allocator_;
  NiceMock<Thread::MockThreadFactory> thread_factory_;
  NiceMock<Server::Configuration::MockFactoryContext> context_;
  std::shared_ptr<Dso::MockNetworkFilterDsoImpl> dso_;
  NiceMock<Event::MockDispatcher> dispatcher_;
  const std::string addr_{"127.0.0.1:8080"};
  UpstreamConnPtr upConn_;

  NiceMock<Network::MockClientConnection> upstream_connection_;
};

TEST_F(UpstreamConnTest, ConnectUpstream) {
  initialize();

  const auto* dst_addr =
      upConn_->requestStreamInfo()->filterState().getDataReadOnly<Network::AddressObject>(
          "envoy.network.transport_socket.original_dst_address");
  EXPECT_EQ(dst_addr->address()->asString(), addr_);

  EXPECT_CALL(
      context_.server_factory_context_.cluster_manager_.thread_local_cluster_.tcp_conn_pool_,
      newConnection(_))
      .WillOnce(
          Invoke([&](Tcp::ConnectionPool::Callbacks& cb) -> Tcp::ConnectionPool::Cancellable* {
            context_.server_factory_context_.cluster_manager_.thread_local_cluster_.tcp_conn_pool_
                .newConnectionImpl(cb);
            context_.server_factory_context_.cluster_manager_.thread_local_cluster_.tcp_conn_pool_
                .poolReady(upstream_connection_);
            return nullptr;
          }));
  EXPECT_CALL(*dso_.get(), envoyGoFilterOnUpstreamConnectionReady(_, _));
  upConn_->connect();

  EXPECT_CALL(
      context_.server_factory_context_.cluster_manager_.thread_local_cluster_.tcp_conn_pool_,
      newConnection(_))
      .WillOnce(
          Invoke([&](Tcp::ConnectionPool::Callbacks& cb) -> Tcp::ConnectionPool::Cancellable* {
            context_.server_factory_context_.cluster_manager_.thread_local_cluster_.tcp_conn_pool_
                .newConnectionImpl(cb);
            context_.server_factory_context_.cluster_manager_.thread_local_cluster_.tcp_conn_pool_
                .poolFailure(ConnectionPool::PoolFailureReason::RemoteConnectionFailure, true);
            return nullptr;
          }));
  EXPECT_CALL(*dso_.get(),
              envoyGoFilterOnUpstreamConnectionFailure(
                  _, GoInt(ConnectionPool::PoolFailureReason::RemoteConnectionFailure), _));
  upConn_->connect();
}

TEST_F(UpstreamConnTest, InvokeDsoOnEventOrData) {
  initialize();
  EXPECT_CALL(*dso_.get(),
              envoyGoFilterOnUpstreamEvent(_, GoInt(Network::ConnectionEvent::Connected)));
  upConn_->onEvent(Network::ConnectionEvent::Connected);

  Buffer::OwnedImpl someData("123");
  EXPECT_CALL(*dso_.get(), envoyGoFilterOnUpstreamData(_, someData.length(), _, _, _));
  upConn_->onUpstreamData(someData, false);
}

TEST_F(UpstreamConnTest, WriteAndClose) {
  initialize();

  EXPECT_CALL(*dso_.get(), envoyGoFilterOnUpstreamConnectionReady(_, _));
  auto data = std::make_unique<NiceMock<Envoy::Tcp::ConnectionPool::MockConnectionData>>();
  EXPECT_CALL(*data, connection()).WillRepeatedly(ReturnRef(upstream_connection_));
  upConn_->onPoolReady(std::move(data), nullptr);

  EXPECT_CALL(upstream_connection_, enableHalfClose(true));
  upConn_->enableHalfClose(true);

  Buffer::OwnedImpl someData("123");
  EXPECT_CALL(upstream_connection_, write(_, false));
  upConn_->write(someData, false);

  EXPECT_CALL(upstream_connection_, close(_, "go_upstream_close"));
  EXPECT_CALL(*dso_.get(), envoyGoFilterOnUpstreamEvent(_, _));
  upConn_->close(Network::ConnectionCloseType::NoFlush);
  upConn_->onEvent(Network::ConnectionEvent::RemoteClose);

  // once upstream conn got closed, should not write any more
  EXPECT_CALL(upstream_connection_, write(_, _)).Times(0);
  upConn_->write(someData, false);

  // once upstream conn got closed, should not close any more
  EXPECT_CALL(upstream_connection_, close(_)).Times(0);
  upConn_->close(Network::ConnectionCloseType::NoFlush);
}

} // namespace
} // namespace Golang
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
