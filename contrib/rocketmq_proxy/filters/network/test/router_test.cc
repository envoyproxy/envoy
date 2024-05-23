#include "test/mocks/server/factory_context.h"

#include "contrib/rocketmq_proxy/filters/network/source/config.h"
#include "contrib/rocketmq_proxy/filters/network/source/conn_manager.h"
#include "contrib/rocketmq_proxy/filters/network/source/constant.h"
#include "contrib/rocketmq_proxy/filters/network/source/router/router.h"
#include "contrib/rocketmq_proxy/filters/network/test/mocks.h"
#include "contrib/rocketmq_proxy/filters/network/test/utility.h"
#include "gtest/gtest.h"

using testing::_;
using testing::ContainsRegex;
using testing::Return;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace RocketmqProxy {
namespace Router {

class RocketmqRouterTestBase {
public:
  RocketmqRouterTestBase()
      : config_(std::make_shared<ConfigImpl>(rocketmq_proxy_config_, context_)),
        cluster_info_(std::make_shared<Upstream::MockClusterInfo>()) {
    context_.server_factory_context_.cluster_manager_.initializeThreadLocalClusters(
        {"fake_cluster"});
    conn_manager_ = std::make_unique<ConnectionManager>(
        config_, context_.server_factory_context_.mainThreadDispatcher().timeSource());
    conn_manager_->initializeReadFilterCallbacks(filter_callbacks_);
  }

  ~RocketmqRouterTestBase() { filter_callbacks_.connection_.dispatcher_.clearDeferredDeleteList(); }

  void initializeRouter() {
    router_ = std::make_unique<RouterImpl>(context_.server_factory_context_.clusterManager());
    EXPECT_EQ(nullptr, router_->downstreamConnection());
  }

  void initSendMessageRequest(std::string topic_name = "test_topic", bool is_oneway = false) {
    RemotingCommandPtr request = std::make_unique<RemotingCommand>();
    request->code(static_cast<int>(RequestCode::SendMessageV2));
    if (is_oneway) {
      request->flag(2);
    }
    SendMessageRequestHeader* header = new SendMessageRequestHeader();
    absl::string_view t = topic_name;
    header->topic(t);
    CommandCustomHeaderPtr custom_header(header);
    request->customHeader(custom_header);
    active_message_ =
        std::make_unique<NiceMock<MockActiveMessage>>(*conn_manager_, std::move(request));

    // Not yet implemented:
    EXPECT_EQ(nullptr, router_->metadataMatchCriteria());
  }

  void initPopMessageRequest() {
    Buffer::OwnedImpl buffer;
    BufferUtility::fillRequestBuffer(buffer, RequestCode::PopMessage);

    bool underflow = false;
    bool has_error = false;

    RemotingCommandPtr request = Decoder::decode(buffer, underflow, has_error);

    active_message_ =
        std::make_unique<NiceMock<MockActiveMessage>>(*conn_manager_, std::move(request));
  }

  void initAckMessageRequest() {
    Buffer::OwnedImpl buffer;
    BufferUtility::fillRequestBuffer(buffer, RequestCode::AckMessage);

    bool underflow = false;
    bool has_error = false;

    RemotingCommandPtr request = Decoder::decode(buffer, underflow, has_error);

    active_message_ =
        std::make_unique<NiceMock<MockActiveMessage>>(*conn_manager_, std::move(request));
  }

  void initOneWayAckMessageRequest() {
    RemotingCommandPtr request = std::make_unique<RemotingCommand>();
    request->code(static_cast<int>(RequestCode::AckMessage));
    request->flag(2);
    std::unique_ptr<AckMessageRequestHeader> header = std::make_unique<AckMessageRequestHeader>();
    header->consumerGroup("test_cg");
    header->topic("test_topic");
    header->queueId(0);
    header->extraInfo("test_extra");
    header->offset(1);
    CommandCustomHeaderPtr ptr(header.release());
    request->customHeader(ptr);
    active_message_ =
        std::make_unique<NiceMock<MockActiveMessage>>(*conn_manager_, std::move(request));
  }

  void startRequest() { router_->sendRequestToUpstream(*active_message_); }

  void connectUpstream() {
    context_.server_factory_context_.cluster_manager_.thread_local_cluster_.tcp_conn_pool_
        .poolReady(upstream_connection_);
  }

  void startRequestWithExistingConnection() {
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
    router_->sendRequestToUpstream(*active_message_);
  }

  void receiveEmptyResponse() {
    Buffer::OwnedImpl buffer;
    router_->onAboveWriteBufferHighWatermark();
    router_->onBelowWriteBufferLowWatermark();
    router_->onUpstreamData(buffer, false);
  }

  void receiveSendMessageResponse(bool end_stream) {
    Buffer::OwnedImpl buffer;
    BufferUtility::fillResponseBuffer(buffer, RequestCode::SendMessageV2, ResponseCode::Success);
    router_->onUpstreamData(buffer, end_stream);
  }

  void receivePopMessageResponse() {
    Buffer::OwnedImpl buffer;
    BufferUtility::fillResponseBuffer(buffer, RequestCode::PopMessage, ResponseCode::Success);
    router_->onUpstreamData(buffer, false);
  }

  void receiveAckMessageResponse() {
    Buffer::OwnedImpl buffer;
    BufferUtility::fillResponseBuffer(buffer, RequestCode::AckMessage, ResponseCode::Success);
    router_->onUpstreamData(buffer, false);
  }

  NiceMock<Network::MockReadFilterCallbacks> filter_callbacks_;
  NiceMock<Server::Configuration::MockFactoryContext> context_;
  ConfigImpl::RocketmqProxyConfig rocketmq_proxy_config_;
  std::shared_ptr<ConfigImpl> config_;
  std::unique_ptr<ConnectionManager> conn_manager_;

  std::unique_ptr<Router> router_;

  std::unique_ptr<NiceMock<MockActiveMessage>> active_message_;
  NiceMock<Network::MockClientConnection> upstream_connection_;

  std::shared_ptr<Upstream::MockClusterInfo> cluster_info_;
  NiceMock<Upstream::MockThreadLocalCluster> thread_local_cluster_;
};

class RocketmqRouterTest : public RocketmqRouterTestBase, public testing::Test {};

TEST_F(RocketmqRouterTest, PoolRemoteConnectionFailure) {
  initializeRouter();
  initSendMessageRequest();

  EXPECT_CALL(*active_message_, onError(_))
      .Times(1)
      .WillOnce(Invoke([&](absl::string_view error_message) -> void {
        EXPECT_THAT(error_message, ContainsRegex(".*remote connection failure*."));
      }));

  startRequest();
  context_.server_factory_context_.cluster_manager_.thread_local_cluster_.tcp_conn_pool_
      .poolFailure(Tcp::ConnectionPool::PoolFailureReason::RemoteConnectionFailure);
}

TEST_F(RocketmqRouterTest, PoolTimeout) {
  initializeRouter();
  initSendMessageRequest();

  EXPECT_CALL(*active_message_, onError(_))
      .Times(1)
      .WillOnce(Invoke([&](absl::string_view error_message) -> void {
        EXPECT_THAT(error_message, ContainsRegex(".*timeout*."));
      }));
  EXPECT_CALL(*active_message_, onReset());

  startRequest();
  context_.server_factory_context_.cluster_manager_.thread_local_cluster_.tcp_conn_pool_
      .poolFailure(Tcp::ConnectionPool::PoolFailureReason::Timeout);
}

TEST_F(RocketmqRouterTest, PoolLocalConnectionFailure) {
  initializeRouter();
  initSendMessageRequest();

  EXPECT_CALL(*active_message_, onError(_))
      .Times(1)
      .WillOnce(Invoke([&](absl::string_view error_message) -> void {
        EXPECT_THAT(error_message, ContainsRegex(".*local connection failure*."));
      }));
  EXPECT_CALL(*active_message_, onReset());

  startRequest();
  context_.server_factory_context_.cluster_manager_.thread_local_cluster_.tcp_conn_pool_
      .poolFailure(Tcp::ConnectionPool::PoolFailureReason::LocalConnectionFailure);
}

TEST_F(RocketmqRouterTest, PoolOverflowFailure) {
  initializeRouter();
  initSendMessageRequest();

  EXPECT_CALL(*active_message_, onError(_))
      .Times(1)
      .WillOnce(Invoke([&](absl::string_view error_message) -> void {
        EXPECT_THAT(error_message, ContainsRegex(".*overflow*."));
      }));
  EXPECT_CALL(*active_message_, onReset());

  startRequest();
  context_.server_factory_context_.cluster_manager_.thread_local_cluster_.tcp_conn_pool_
      .poolFailure(Tcp::ConnectionPool::PoolFailureReason::Overflow);
}

TEST_F(RocketmqRouterTest, ClusterMaintenanceMode) {
  initializeRouter();
  initSendMessageRequest();

  EXPECT_CALL(*active_message_, onError(_))
      .Times(1)
      .WillOnce(Invoke([&](absl::string_view error_message) -> void {
        EXPECT_THAT(error_message, ContainsRegex(".*Cluster under maintenance*."));
      }));
  EXPECT_CALL(
      *context_.server_factory_context_.cluster_manager_.thread_local_cluster_.cluster_.info_,
      maintenanceMode())
      .WillOnce(Return(true));
  EXPECT_CALL(*active_message_, onReset());

  startRequest();
}

TEST_F(RocketmqRouterTest, NoHealthyHosts) {
  initializeRouter();
  initSendMessageRequest();

  EXPECT_CALL(*active_message_, onError(_))
      .Times(1)
      .WillOnce(Invoke([&](absl::string_view error_message) -> void {
        EXPECT_THAT(error_message, ContainsRegex(".*No host available*."));
      }));
  EXPECT_CALL(context_.server_factory_context_.cluster_manager_.thread_local_cluster_,
              tcpConnPool(_, _))
      .WillOnce(Return(absl::nullopt));
  EXPECT_CALL(*active_message_, onReset());

  startRequest();
}

TEST_F(RocketmqRouterTest, NoRouteForRequest) {
  initializeRouter();
  initSendMessageRequest();

  EXPECT_CALL(*active_message_, onError(_))
      .Times(1)
      .WillOnce(Invoke([&](absl::string_view error_message) -> void {
        EXPECT_THAT(error_message, ContainsRegex(".*No route for current request*."));
      }));
  EXPECT_CALL(*active_message_, route()).WillRepeatedly(Return(nullptr));
  EXPECT_CALL(*active_message_, onReset());

  startRequest();
}

TEST_F(RocketmqRouterTest, NoCluster) {
  initializeRouter();
  initSendMessageRequest();

  EXPECT_CALL(*active_message_, onReset());
  EXPECT_CALL(context_.server_factory_context_.cluster_manager_, getThreadLocalCluster(_))
      .WillRepeatedly(Return(nullptr));

  startRequest();
}

TEST_F(RocketmqRouterTest, CallWithEmptyResponse) {
  initializeRouter();
  initSendMessageRequest();

  startRequest();
  connectUpstream();

  EXPECT_CALL(*active_message_, sendResponseToDownstream()).Times(0);
  EXPECT_CALL(*active_message_, onReset()).Times(0);

  receiveEmptyResponse();
}

TEST_F(RocketmqRouterTest, OneWayRequest) {
  initializeRouter();
  initSendMessageRequest("test_topic", true);
  startRequest();

  EXPECT_CALL(*active_message_, onReset());

  connectUpstream();

  EXPECT_TRUE(active_message_->metadata()->isOneWay());
}

TEST_F(RocketmqRouterTest, ReceiveSendMessageResponse) {
  initializeRouter();
  initSendMessageRequest();

  startRequest();
  connectUpstream();

  EXPECT_CALL(*active_message_, sendResponseToDownstream());
  EXPECT_CALL(*active_message_, onReset());

  receiveSendMessageResponse(false);
}

TEST_F(RocketmqRouterTest, ReceivePopMessageResponse) {
  initializeRouter();
  initPopMessageRequest();

  startRequest();
  connectUpstream();

  EXPECT_CALL(*active_message_, sendResponseToDownstream());
  EXPECT_CALL(*active_message_, onReset());

  receivePopMessageResponse();
}

TEST_F(RocketmqRouterTest, ReceiveAckMessageResponse) {
  initializeRouter();
  initAckMessageRequest();

  startRequest();
  connectUpstream();

  EXPECT_CALL(*active_message_, sendResponseToDownstream());
  EXPECT_CALL(*active_message_, onReset());

  receiveAckMessageResponse();
}

TEST_F(RocketmqRouterTest, OneWayAckMessage) {
  initializeRouter();
  initOneWayAckMessageRequest();

  startRequest();

  EXPECT_CALL(*active_message_, onReset());

  connectUpstream();
}

TEST_F(RocketmqRouterTest, ReceivedSendMessageResponseWithDecodeError) {
  initializeRouter();
  initSendMessageRequest();

  EXPECT_CALL(*active_message_, onError(_))
      .Times(1)
      .WillOnce(Invoke([&](absl::string_view error_message) -> void {
        EXPECT_THAT(error_message, ContainsRegex(".*Failed to decode response*."));
      }));

  EXPECT_CALL(upstream_connection_, close(Network::ConnectionCloseType::NoFlush));

  startRequest();
  connectUpstream();
  std::string json = R"EOF(
  {
    "language": "JAVA",
    "version": 2,
    "opaque": 1,
    "flag": 1,
    "serializeTypeCurrentRPC": "JSON"
  }
  )EOF";
  Buffer::OwnedImpl buffer;
  buffer.writeBEInt<int32_t>(4 + 4 + json.size());
  buffer.writeBEInt<int32_t>(json.size());
  buffer.add(json);

  EXPECT_CALL(*active_message_, onReset()).WillRepeatedly(Invoke([&]() -> void {
    conn_manager_->deferredDelete(**conn_manager_->activeMessageList().begin());
  }));
  EXPECT_CALL(*active_message_, onReset());

  LinkedList::moveIntoList(std::move(active_message_), conn_manager_->activeMessageList());
  router_->onUpstreamData(buffer, false);
}

TEST_F(RocketmqRouterTest, ReceivedSendMessageResponseWithStreamEnd) {
  initializeRouter();
  initSendMessageRequest();

  EXPECT_CALL(upstream_connection_, close(Network::ConnectionCloseType::NoFlush));

  startRequest();
  connectUpstream();

  EXPECT_CALL(*active_message_, sendResponseToDownstream());
  EXPECT_CALL(*active_message_, onReset());

  receiveSendMessageResponse(true);
}

TEST_F(RocketmqRouterTest, UpstreamRemoteCloseMidResponse) {
  initializeRouter();
  initSendMessageRequest();

  EXPECT_CALL(*active_message_, onError(_))
      .Times(1)
      .WillOnce(Invoke([&](absl::string_view error_message) -> void {
        EXPECT_THAT(error_message, ContainsRegex(".*Connection to upstream is closed*."));
      }));

  startRequest();
  connectUpstream();

  EXPECT_CALL(*active_message_, sendResponseToDownstream()).Times(0);
  EXPECT_CALL(*active_message_, onReset());

  router_->onEvent(Network::ConnectionEvent::RemoteClose);
}

TEST_F(RocketmqRouterTest, UpstreamLocalCloseMidResponse) {
  initializeRouter();
  initSendMessageRequest();

  EXPECT_CALL(*active_message_, onError(_))
      .Times(1)
      .WillOnce(Invoke([&](absl::string_view error_message) -> void {
        EXPECT_THAT(error_message, ContainsRegex(".*Connection to upstream has been closed*."));
      }));

  startRequest();
  connectUpstream();

  EXPECT_CALL(*active_message_, sendResponseToDownstream()).Times(0);
  EXPECT_CALL(*active_message_, onReset());

  router_->onEvent(Network::ConnectionEvent::LocalClose);
}

TEST_F(RocketmqRouterTest, UpstreamConnected) {
  initializeRouter();
  initSendMessageRequest();

  startRequest();
  connectUpstream();

  EXPECT_CALL(*active_message_, sendResponseToDownstream()).Times(0);
  EXPECT_CALL(*active_message_, onReset()).Times(0);

  router_->onEvent(Network::ConnectionEvent::Connected);
}

TEST_F(RocketmqRouterTest, StartRequestWithExistingConnection) {
  initializeRouter();
  initSendMessageRequest();

  EXPECT_CALL(*active_message_, onError(_)).Times(0);
  EXPECT_CALL(*active_message_, onReset()).Times(0);

  startRequestWithExistingConnection();
}

} // namespace Router
} // namespace RocketmqProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
