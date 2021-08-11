#include <memory>

#include "source/extensions/filters/network/dubbo_proxy/app_exception.h"
#include "source/extensions/filters/network/dubbo_proxy/dubbo_hessian2_serializer_impl.h"
#include "source/extensions/filters/network/dubbo_proxy/message_impl.h"
#include "source/extensions/filters/network/dubbo_proxy/protocol.h"
#include "source/extensions/filters/network/dubbo_proxy/router/router_impl.h"

#include "test/extensions/filters/network/dubbo_proxy/mocks.h"
#include "test/extensions/filters/network/dubbo_proxy/utility.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/server/factory_context.h"
#include "test/test_common/printers.h"
#include "test/test_common/registry.h"

#include "gtest/gtest.h"

using testing::_;
using testing::ContainsRegex;
using testing::Eq;
using testing::Invoke;
using testing::NiceMock;
using testing::Ref;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace DubboProxy {
namespace Router {

namespace {

class TestNamedSerializerConfigFactory : public NamedSerializerConfigFactory {
public:
  TestNamedSerializerConfigFactory(std::function<MockSerializer*()> f) : f_(f) {}

  SerializerPtr createSerializer() override { return SerializerPtr{f_()}; }
  std::string name() const override {
    return SerializerNames::get().fromType(SerializationType::Hessian2);
  }

  std::function<MockSerializer*()> f_;
};

class TestNamedProtocolConfigFactory : public NamedProtocolConfigFactory {
public:
  TestNamedProtocolConfigFactory(std::function<MockProtocol*()> f) : f_(f) {}

  ProtocolPtr createProtocol(SerializationType serialization_type) override {
    auto protocol = ProtocolPtr{f_()};
    protocol->initSerializer(serialization_type);
    return protocol;
  }
  std::string name() const override { return ProtocolNames::get().fromType(ProtocolType::Dubbo); }

  std::function<MockProtocol*()> f_;
};

void writeRequest(Buffer::Instance& buffer) {
  buffer.add(std::string({'\xda', '\xbb', 0x42, 20})); // Header
  addInt64(buffer, 1);
  addInt32(buffer, 5);

  buffer.add(std::string({
      0x04,
      't',
      'e',
      's',
      't',
  })); // Body
}

} // namespace

class DubboRouterTestBase {
public:
  DubboRouterTestBase()
      : serializer_factory_([&]() -> MockSerializer* {
          ASSERT(serializer_ == nullptr);
          serializer_ = new NiceMock<MockSerializer>();
          if (mock_serializer_cb_) {
            mock_serializer_cb_(serializer_);
          }
          return serializer_;
        }),
        protocol_factory_([&]() -> MockProtocol* {
          ASSERT(protocol_ == nullptr);
          protocol_ = new NiceMock<MockProtocol>();
          if (mock_protocol_cb_) {
            mock_protocol_cb_(protocol_);
          }
          return protocol_;
        }),
        serializer_register_(serializer_factory_), protocol_register_(protocol_factory_) {
    context_.cluster_manager_.initializeThreadLocalClusters({"cluster"});
  }

  void initializeRouter() {
    route_ = new NiceMock<MockRoute>();
    route_ptr_.reset(route_);

    router_ = std::make_unique<Router>(context_.clusterManager());

    EXPECT_EQ(nullptr, router_->downstreamConnection());

    router_->setDecoderFilterCallbacks(callbacks_);
    router_->setEncoderFilterCallbacks(encoder_callbacks_);
  }

  void initializeMetadata(MessageType msg_type) {
    if (metadata_ != nullptr) {
      return;
    }

    msg_type_ = msg_type;

    metadata_ = std::make_shared<MessageMetadata>();
    metadata_->setMessageType(msg_type_);
    metadata_->setRequestId(1);

    auto invo = std::make_shared<RpcInvocationImpl>();
    metadata_->setInvocationInfo(invo);
    invo->setMethodName("test");

    message_context_ = std::make_shared<ContextImpl>();
  }

  void startRequest(MessageType msg_type) {
    initializeMetadata(msg_type);

    EXPECT_CALL(callbacks_, route()).WillOnce(Return(route_ptr_));
    EXPECT_CALL(*route_, routeEntry()).WillOnce(Return(&route_entry_));
    EXPECT_CALL(route_entry_, clusterName()).WillRepeatedly(ReturnRef(cluster_name_));

    EXPECT_CALL(callbacks_, serializationType()).WillOnce(Return(SerializationType::Hessian2));
    EXPECT_CALL(callbacks_, protocolType()).WillOnce(Return(ProtocolType::Dubbo));

    EXPECT_EQ(FilterStatus::StopIteration, router_->onMessageDecoded(metadata_, message_context_));

    EXPECT_CALL(callbacks_, connection()).WillRepeatedly(Return(&connection_));
    EXPECT_EQ(&connection_, router_->downstreamConnection());

    // Not yet implemented:
    EXPECT_EQ(absl::optional<uint64_t>(), router_->computeHashKey());
    EXPECT_EQ(nullptr, router_->metadataMatchCriteria());
    EXPECT_EQ(nullptr, router_->downstreamHeaders());
  }

  void connectUpstream() {
    EXPECT_CALL(*context_.cluster_manager_.thread_local_cluster_.tcp_conn_pool_.connection_data_,
                addUpstreamCallbacks(_))
        .WillOnce(Invoke([&](Tcp::ConnectionPool::UpstreamCallbacks& cb) -> void {
          upstream_callbacks_ = &cb;
        }));

    conn_state_.reset();
    EXPECT_CALL(*context_.cluster_manager_.thread_local_cluster_.tcp_conn_pool_.connection_data_,
                connectionState())
        .WillRepeatedly(
            Invoke([&]() -> Tcp::ConnectionPool::ConnectionState* { return conn_state_.get(); }));

    EXPECT_CALL(callbacks_, continueDecoding());
    context_.cluster_manager_.thread_local_cluster_.tcp_conn_pool_.poolReady(upstream_connection_);

    EXPECT_NE(nullptr, upstream_callbacks_);
  }

  void startRequestWithExistingConnection(MessageType msg_type) {
    EXPECT_CALL(callbacks_, route()).WillOnce(Return(route_ptr_));
    EXPECT_CALL(*route_, routeEntry()).WillOnce(Return(&route_entry_));
    EXPECT_CALL(route_entry_, clusterName()).WillRepeatedly(ReturnRef(cluster_name_));

    initializeMetadata(msg_type);

    EXPECT_CALL(*context_.cluster_manager_.thread_local_cluster_.tcp_conn_pool_.connection_data_,
                addUpstreamCallbacks(_))
        .WillOnce(Invoke([&](Tcp::ConnectionPool::UpstreamCallbacks& cb) -> void {
          upstream_callbacks_ = &cb;
        }));

    EXPECT_CALL(callbacks_, connection()).WillRepeatedly(Return(&connection_));
    EXPECT_EQ(&connection_, router_->downstreamConnection());

    // Not yet implemented:
    EXPECT_EQ(absl::optional<uint64_t>(), router_->computeHashKey());
    EXPECT_EQ(nullptr, router_->metadataMatchCriteria());
    EXPECT_EQ(nullptr, router_->downstreamHeaders());

    EXPECT_CALL(callbacks_, serializationType()).WillOnce(Return(SerializationType::Hessian2));
    EXPECT_CALL(callbacks_, protocolType()).WillOnce(Return(ProtocolType::Dubbo));

    EXPECT_CALL(callbacks_, continueDecoding()).Times(0);
    EXPECT_CALL(context_.cluster_manager_.thread_local_cluster_.tcp_conn_pool_, newConnection(_))
        .WillOnce(
            Invoke([&](Tcp::ConnectionPool::Callbacks& cb) -> Tcp::ConnectionPool::Cancellable* {
              context_.cluster_manager_.thread_local_cluster_.tcp_conn_pool_.newConnectionImpl(cb);
              context_.cluster_manager_.thread_local_cluster_.tcp_conn_pool_.poolReady(
                  upstream_connection_);
              return nullptr;
            }));
  }

  void returnResponse() {
    Buffer::OwnedImpl buffer;

    EXPECT_CALL(callbacks_, startUpstreamResponse());

    EXPECT_CALL(callbacks_, upstreamData(Ref(buffer)))
        .WillOnce(Return(DubboFilters::UpstreamResponseStatus::MoreData));
    upstream_callbacks_->onUpstreamData(buffer, false);

    // Nothing to do.
    upstream_callbacks_->onAboveWriteBufferHighWatermark();
    upstream_callbacks_->onBelowWriteBufferLowWatermark();

    EXPECT_CALL(callbacks_, upstreamData(Ref(buffer)))
        .WillOnce(Return(DubboFilters::UpstreamResponseStatus::Complete));
    EXPECT_CALL(context_.cluster_manager_.thread_local_cluster_.tcp_conn_pool_,
                released(Ref(upstream_connection_)));
    upstream_callbacks_->onUpstreamData(buffer, false);
  }

  void destroyRouter() {
    router_->onDestroy();
    router_.reset();
  }

  TestNamedSerializerConfigFactory serializer_factory_;
  TestNamedProtocolConfigFactory protocol_factory_;
  Registry::InjectFactory<NamedSerializerConfigFactory> serializer_register_;
  Registry::InjectFactory<NamedProtocolConfigFactory> protocol_register_;

  std::function<void(MockSerializer*)> mock_serializer_cb_{};
  std::function<void(MockProtocol*)> mock_protocol_cb_{};

  NiceMock<Server::Configuration::MockFactoryContext> context_;
  NiceMock<Network::MockClientConnection> connection_;
  NiceMock<DubboFilters::MockDecoderFilterCallbacks> callbacks_;
  NiceMock<DubboFilters::MockEncoderFilterCallbacks> encoder_callbacks_;
  NiceMock<MockSerializer>* serializer_{};
  NiceMock<MockProtocol>* protocol_{};
  NiceMock<MockRoute>* route_{};
  NiceMock<MockRouteEntry> route_entry_;
  NiceMock<Upstream::MockHostDescription>* host_{};
  Tcp::ConnectionPool::ConnectionStatePtr conn_state_;

  RouteConstSharedPtr route_ptr_;
  std::unique_ptr<Router> router_;

  std::string cluster_name_{"cluster"};

  MessageType msg_type_{MessageType::Request};
  MessageMetadataSharedPtr metadata_;
  ContextSharedPtr message_context_;

  Tcp::ConnectionPool::UpstreamCallbacks* upstream_callbacks_{};
  NiceMock<Network::MockClientConnection> upstream_connection_;
};

class DubboRouterTest : public DubboRouterTestBase, public testing::Test {};

TEST_F(DubboRouterTest, PoolRemoteConnectionFailure) {
  initializeRouter();

  EXPECT_CALL(callbacks_, sendLocalReply(_, _))
      .WillOnce(Invoke([&](const DubboFilters::DirectResponse& response, bool end_stream) -> void {
        auto& app_ex = dynamic_cast<const AppException&>(response);
        EXPECT_EQ(ResponseStatus::ServerError, app_ex.status_);
        EXPECT_THAT(app_ex.what(), ContainsRegex(".*connection failure.*"));
        EXPECT_FALSE(end_stream);
      }));
  startRequest(MessageType::Request);

  context_.cluster_manager_.thread_local_cluster_.tcp_conn_pool_.poolFailure(
      ConnectionPool::PoolFailureReason::RemoteConnectionFailure);
}

TEST_F(DubboRouterTest, PoolTimeout) {
  initializeRouter();

  EXPECT_CALL(callbacks_, sendLocalReply(_, _))
      .WillOnce(Invoke([&](const DubboFilters::DirectResponse& response, bool end_stream) -> void {
        auto& app_ex = dynamic_cast<const AppException&>(response);
        EXPECT_EQ(ResponseStatus::ServerError, app_ex.status_);
        EXPECT_THAT(app_ex.what(), ContainsRegex(".*connection failure.*"));
        EXPECT_FALSE(end_stream);
      }));
  startRequest(MessageType::Request);

  context_.cluster_manager_.thread_local_cluster_.tcp_conn_pool_.poolFailure(
      ConnectionPool::PoolFailureReason::Timeout);
}

TEST_F(DubboRouterTest, PoolOverflowFailure) {
  initializeRouter();

  EXPECT_CALL(callbacks_, sendLocalReply(_, _))
      .WillOnce(Invoke([&](const DubboFilters::DirectResponse& response, bool end_stream) -> void {
        auto& app_ex = dynamic_cast<const AppException&>(response);
        EXPECT_EQ(ResponseStatus::ServerError, app_ex.status_);
        EXPECT_THAT(app_ex.what(), ContainsRegex(".*too many connections.*"));
        EXPECT_FALSE(end_stream);
      }));
  startRequest(MessageType::Request);

  context_.cluster_manager_.thread_local_cluster_.tcp_conn_pool_.poolFailure(
      ConnectionPool::PoolFailureReason::Overflow);
}

TEST_F(DubboRouterTest, ClusterMaintenanceMode) {
  initializeRouter();
  initializeMetadata(MessageType::Request);

  EXPECT_CALL(callbacks_, route()).WillOnce(Return(route_ptr_));
  EXPECT_CALL(*route_, routeEntry()).WillOnce(Return(&route_entry_));
  EXPECT_CALL(route_entry_, clusterName()).WillRepeatedly(ReturnRef(cluster_name_));
  EXPECT_CALL(*context_.cluster_manager_.thread_local_cluster_.cluster_.info_, maintenanceMode())
      .WillOnce(Return(true));

  EXPECT_CALL(callbacks_, sendLocalReply(_, _))
      .WillOnce(Invoke([&](const DubboFilters::DirectResponse& response, bool end_stream) -> void {
        auto& app_ex = dynamic_cast<const AppException&>(response);
        EXPECT_EQ(ResponseStatus::ServerError, app_ex.status_);
        EXPECT_THAT(app_ex.what(), ContainsRegex(".*maintenance mode.*"));
        EXPECT_FALSE(end_stream);
      }));
  EXPECT_EQ(FilterStatus::StopIteration, router_->onMessageDecoded(metadata_, message_context_));
}

TEST_F(DubboRouterTest, NoHealthyHosts) {
  initializeRouter();
  initializeMetadata(MessageType::Request);

  EXPECT_CALL(callbacks_, route()).WillOnce(Return(route_ptr_));
  EXPECT_CALL(*route_, routeEntry()).WillOnce(Return(&route_entry_));
  EXPECT_CALL(route_entry_, clusterName()).WillRepeatedly(ReturnRef(cluster_name_));
  EXPECT_CALL(context_.cluster_manager_.thread_local_cluster_, tcpConnPool(_, _))
      .WillOnce(Return(absl::nullopt));

  EXPECT_CALL(callbacks_, sendLocalReply(_, _))
      .WillOnce(Invoke([&](const DubboFilters::DirectResponse& response, bool end_stream) -> void {
        auto& app_ex = dynamic_cast<const AppException&>(response);
        EXPECT_EQ(ResponseStatus::ServerError, app_ex.status_);
        EXPECT_THAT(app_ex.what(), ContainsRegex(".*no healthy upstream.*"));
        EXPECT_FALSE(end_stream);
      }));

  EXPECT_EQ(FilterStatus::StopIteration, router_->onMessageDecoded(metadata_, message_context_));
}

TEST_F(DubboRouterTest, PoolConnectionFailureWithOnewayMessage) {
  context_.cluster_manager_.initializeThreadLocalClusters({"fake_cluster"});
  initializeRouter();
  initializeMetadata(MessageType::Oneway);

  EXPECT_CALL(callbacks_, protocolType()).WillOnce(Return(ProtocolType::Dubbo));
  EXPECT_CALL(callbacks_, serializationType()).WillOnce(Return(SerializationType::Hessian2));
  EXPECT_CALL(callbacks_, sendLocalReply(_, _)).Times(0);
  EXPECT_CALL(callbacks_, resetStream());
  EXPECT_EQ(FilterStatus::StopIteration, router_->onMessageDecoded(metadata_, message_context_));

  context_.cluster_manager_.thread_local_cluster_.tcp_conn_pool_.poolFailure(
      ConnectionPool::PoolFailureReason::RemoteConnectionFailure);

  destroyRouter();
}

TEST_F(DubboRouterTest, NoRoute) {
  initializeRouter();
  initializeMetadata(MessageType::Request);

  EXPECT_CALL(callbacks_, route()).WillOnce(Return(nullptr));
  EXPECT_CALL(callbacks_, sendLocalReply(_, _))
      .WillOnce(Invoke([&](const DubboFilters::DirectResponse& response, bool end_stream) -> void {
        auto& app_ex = dynamic_cast<const AppException&>(response);
        EXPECT_EQ(ResponseStatus::ServiceNotFound, app_ex.status_);
        EXPECT_THAT(app_ex.what(), ContainsRegex(".*no route.*"));
        EXPECT_FALSE(end_stream);
      }));
  EXPECT_EQ(FilterStatus::StopIteration, router_->onMessageDecoded(metadata_, message_context_));
}

TEST_F(DubboRouterTest, NoCluster) {
  initializeRouter();
  initializeMetadata(MessageType::Request);

  EXPECT_CALL(callbacks_, route()).WillOnce(Return(route_ptr_));
  EXPECT_CALL(*route_, routeEntry()).WillOnce(Return(&route_entry_));
  EXPECT_CALL(route_entry_, clusterName()).WillRepeatedly(ReturnRef(cluster_name_));
  EXPECT_CALL(context_.cluster_manager_, getThreadLocalCluster(Eq(cluster_name_)))
      .WillOnce(Return(nullptr));
  EXPECT_CALL(callbacks_, sendLocalReply(_, _))
      .WillOnce(Invoke([&](const DubboFilters::DirectResponse& response, bool end_stream) -> void {
        auto& app_ex = dynamic_cast<const AppException&>(response);
        EXPECT_EQ(ResponseStatus::ServerError, app_ex.status_);
        EXPECT_THAT(app_ex.what(), ContainsRegex(".*unknown cluster.*"));
        EXPECT_FALSE(end_stream);
      }));
  EXPECT_EQ(FilterStatus::StopIteration, router_->onMessageDecoded(metadata_, message_context_));
}

TEST_F(DubboRouterTest, UnexpectedRouterDestroy) {
  initializeRouter();
  initializeMetadata(MessageType::Request);
  EXPECT_CALL(upstream_connection_, close(Network::ConnectionCloseType::NoFlush));

  Buffer::OwnedImpl buffer;
  buffer.add(std::string({'\xda', '\xbb', 0x42, 20})); // Header
  buffer.add("test");                                  // Body

  auto ctx = static_cast<ContextImpl*>(message_context_.get());
  ctx->originMessage().move(buffer, buffer.length());
  startRequest(MessageType::Request);
  connectUpstream();
  destroyRouter();
}

TEST_F(DubboRouterTest, UpstreamRemoteCloseNoRequest) {
  initializeRouter();

  startRequest(MessageType::Request);
  connectUpstream();
  returnResponse();

  upstream_callbacks_->onEvent(Network::ConnectionEvent::RemoteClose);
  destroyRouter();
}

TEST_F(DubboRouterTest, UpstreamLocalCloseAndRequestReset) {
  initializeRouter();

  startRequest(MessageType::Request);
  connectUpstream();

  Buffer::OwnedImpl buffer;

  EXPECT_CALL(callbacks_, startUpstreamResponse());

  EXPECT_CALL(callbacks_, upstreamData(Ref(buffer)))
      .WillOnce(Return(DubboFilters::UpstreamResponseStatus::Reset));

  upstream_callbacks_->onUpstreamData(buffer, false);

  upstream_callbacks_->onEvent(Network::ConnectionEvent::LocalClose);
  destroyRouter();
}

TEST_F(DubboRouterTest, UpstreamRemoteCloseMidResponse) {
  initializeRouter();

  EXPECT_CALL(callbacks_, sendLocalReply(_, _))
      .WillOnce(Invoke([&](const DubboFilters::DirectResponse& response, bool end_stream) -> void {
        auto& app_ex = dynamic_cast<const AppException&>(response);
        EXPECT_EQ(ResponseStatus::ServerError, app_ex.status_);
        EXPECT_THAT(app_ex.what(), ContainsRegex(".*connection failure.*"));
        EXPECT_FALSE(end_stream);
      }));
  startRequest(MessageType::Request);
  connectUpstream();
  upstream_callbacks_->onEvent(Network::ConnectionEvent::RemoteClose);
  destroyRouter();
}

TEST_F(DubboRouterTest, UpstreamLocalCloseMidResponse) {
  initializeRouter();
  startRequest(MessageType::Request);
  connectUpstream();

  upstream_callbacks_->onEvent(Network::ConnectionEvent::LocalClose);
  destroyRouter();
}

TEST_F(DubboRouterTest, OneWay) {
  initializeRouter();
  initializeMetadata(MessageType::Oneway);

  EXPECT_CALL(context_.cluster_manager_.thread_local_cluster_.tcp_conn_pool_,
              released(Ref(upstream_connection_)));

  startRequest(MessageType::Oneway);
  connectUpstream();
  destroyRouter();
}

TEST_F(DubboRouterTest, Call) {
  initializeRouter();
  initializeMetadata(MessageType::Request);

  EXPECT_CALL(upstream_connection_, write(_, false));

  startRequest(MessageType::Request);
  connectUpstream();
  returnResponse();
  destroyRouter();
}

// Test the attachment being updated.
TEST_F(DubboRouterTest, AttachmentUpdated) {
  initializeRouter();
  initializeMetadata(MessageType::Request);

  auto* invo = const_cast<RpcInvocationImpl*>(
      dynamic_cast<const RpcInvocationImpl*>(&metadata_->invocationInfo()));

  EXPECT_CALL(upstream_connection_, write(_, false));

  writeRequest(message_context_->originMessage());
  dynamic_cast<ContextImpl*>(message_context_.get())->setHeaderSize(16);

  const size_t origin_message_size = message_context_->originMessage().length();

  invo->setParametersLazyCallback([]() -> RpcInvocationImpl::ParametersPtr {
    return std::make_unique<RpcInvocationImpl::Parameters>();
  });

  invo->setAttachmentLazyCallback([origin_message_size]() -> RpcInvocationImpl::AttachmentPtr {
    auto map = std::make_unique<RpcInvocationImpl::Attachment::Map>();
    return std::make_unique<RpcInvocationImpl::Attachment>(std::move(map), origin_message_size);
  });

  invo->mutableAttachment()->insert("fake_attach_key", "fake_attach_value");

  startRequest(MessageType::Request);

  auto& upstream_request_buffer = router_->upstreamRequestBufferForTest();

  // Verify that the attachment is properly serialized.
  Hessian2::Decoder decoder(
      std::make_unique<BufferReader>(upstream_request_buffer, origin_message_size));
  EXPECT_EQ("fake_attach_value",
            *(decoder.decode<Hessian2::Object>()
                  ->toUntypedMap()
                  .value()
                  ->at(std::make_unique<Hessian2::StringObject>("fake_attach_key"))
                  ->toString()
                  .value()));

  // Check new body size value.
  EXPECT_EQ(upstream_request_buffer.peekBEInt<int32_t>(12), upstream_request_buffer.length() - 16);

  connectUpstream();
  returnResponse();
  destroyRouter();
}

TEST_F(DubboRouterTest, DecoderFilterCallbacks) {
  initializeRouter();
  initializeMetadata(MessageType::Request);

  EXPECT_CALL(upstream_connection_, write(_, false));
  EXPECT_CALL(callbacks_, startUpstreamResponse());
  EXPECT_CALL(callbacks_, upstreamData(_));

  startRequest(MessageType::Request);
  connectUpstream();

  Buffer::OwnedImpl buffer;
  buffer.add(std::string("This is the test data"));
  router_->onUpstreamData(buffer, true);

  destroyRouter();
}

TEST_F(DubboRouterTest, UpstreamDataReset) {
  initializeRouter();
  initializeMetadata(MessageType::Request);

  EXPECT_CALL(callbacks_, startUpstreamResponse());
  EXPECT_CALL(callbacks_, upstreamData(_))
      .WillOnce(Return(DubboFilters::UpstreamResponseStatus::Reset));
  EXPECT_CALL(upstream_connection_, close(Network::ConnectionCloseType::NoFlush));

  startRequest(MessageType::Request);
  connectUpstream();

  Buffer::OwnedImpl buffer;
  buffer.add(std::string("This is the test data"));
  router_->onUpstreamData(buffer, false);

  destroyRouter();
}

TEST_F(DubboRouterTest, StartRequestWithExistingConnection) {
  initializeRouter();
  startRequestWithExistingConnection(MessageType::Request);

  EXPECT_EQ(FilterStatus::Continue, router_->onMessageDecoded(metadata_, message_context_));

  destroyRouter();
}

TEST_F(DubboRouterTest, DestroyWhileConnecting) {
  initializeRouter();
  initializeMetadata(MessageType::Request);

  NiceMock<Envoy::ConnectionPool::MockCancellable> conn_pool_handle;
  EXPECT_CALL(context_.cluster_manager_.thread_local_cluster_.tcp_conn_pool_, newConnection(_))
      .WillOnce(Invoke([&](Tcp::ConnectionPool::Callbacks&) -> Tcp::ConnectionPool::Cancellable* {
        return &conn_pool_handle;
      }));

  EXPECT_CALL(conn_pool_handle, cancel(Tcp::ConnectionPool::CancelPolicy::Default));

  startRequest(MessageType::Request);
  router_->onDestroy();

  destroyRouter();
}

TEST_F(DubboRouterTest, LocalClosedWhileResponseComplete) {
  initializeRouter();
  initializeMetadata(MessageType::Request);

  EXPECT_CALL(callbacks_, startUpstreamResponse());
  EXPECT_CALL(callbacks_, upstreamData(_))
      .WillOnce(Return(DubboFilters::UpstreamResponseStatus::Complete));
  EXPECT_CALL(callbacks_, sendLocalReply(_, _)).Times(0);

  startRequest(MessageType::Request);
  connectUpstream();

  Buffer::OwnedImpl buffer;
  buffer.add(std::string("This is the test data"));
  router_->onUpstreamData(buffer, false);

  upstream_connection_.close(Network::ConnectionCloseType::NoFlush);

  destroyRouter();
}

TEST_F(DubboRouterTest, ResponseOk) {
  initializeRouter();
  startRequest(MessageType::Request);
  connectUpstream();

  auto response_meta = std::make_shared<MessageMetadata>();
  response_meta->setMessageType(MessageType::Response);
  response_meta->setResponseStatus(ResponseStatus::Ok);

  EXPECT_CALL(
      context_.cluster_manager_.thread_local_cluster_.tcp_conn_pool_.host_->outlier_detector_,
      putResult(Upstream::Outlier::Result::ExtOriginRequestSuccess, _));
  EXPECT_EQ(FilterStatus::Continue, router_->onMessageEncoded(response_meta, message_context_));

  destroyRouter();
}

TEST_F(DubboRouterTest, ResponseException) {
  initializeRouter();
  startRequest(MessageType::Request);
  connectUpstream();

  auto response_meta = std::make_shared<MessageMetadata>();
  response_meta->setMessageType(MessageType::Exception);
  response_meta->setResponseStatus(ResponseStatus::Ok);

  EXPECT_CALL(
      context_.cluster_manager_.thread_local_cluster_.tcp_conn_pool_.host_->outlier_detector_,
      putResult(Upstream::Outlier::Result::ExtOriginRequestFailed, _));
  EXPECT_EQ(FilterStatus::Continue, router_->onMessageEncoded(response_meta, message_context_));

  destroyRouter();
}

TEST_F(DubboRouterTest, ResponseServerTimeout) {
  initializeRouter();
  startRequest(MessageType::Request);
  connectUpstream();

  auto response_meta = std::make_shared<MessageMetadata>();
  response_meta->setMessageType(MessageType::Response);
  response_meta->setResponseStatus(ResponseStatus::ServerTimeout);

  EXPECT_CALL(
      context_.cluster_manager_.thread_local_cluster_.tcp_conn_pool_.host_->outlier_detector_,
      putResult(Upstream::Outlier::Result::LocalOriginTimeout, _));
  EXPECT_EQ(FilterStatus::Continue, router_->onMessageEncoded(response_meta, message_context_));

  destroyRouter();
}

TEST_F(DubboRouterTest, ResponseServerError) {
  initializeRouter();
  startRequest(MessageType::Request);
  connectUpstream();

  auto response_meta = std::make_shared<MessageMetadata>();
  response_meta->setMessageType(MessageType::Response);
  response_meta->setResponseStatus(ResponseStatus::ServiceError);

  EXPECT_CALL(
      context_.cluster_manager_.thread_local_cluster_.tcp_conn_pool_.host_->outlier_detector_,
      putResult(Upstream::Outlier::Result::ExtOriginRequestFailed, _));
  EXPECT_EQ(FilterStatus::Continue, router_->onMessageEncoded(response_meta, message_context_));

  destroyRouter();
}

} // namespace Router
} // namespace DubboProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
