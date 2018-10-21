#include "envoy/config/filter/network/thrift_proxy/v2alpha1/route.pb.h"
#include "envoy/config/filter/network/thrift_proxy/v2alpha1/route.pb.validate.h"
#include "envoy/tcp/conn_pool.h"

#include "common/buffer/buffer_impl.h"

#include "extensions/filters/network/thrift_proxy/app_exception_impl.h"
#include "extensions/filters/network/thrift_proxy/router/config.h"
#include "extensions/filters/network/thrift_proxy/router/router_impl.h"

#include "test/extensions/filters/network/thrift_proxy/mocks.h"
#include "test/extensions/filters/network/thrift_proxy/utility.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/server/mocks.h"
#include "test/mocks/upstream/mocks.h"
#include "test/test_common/printers.h"
#include "test/test_common/registry.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::ContainsRegex;
using testing::InSequence;
using testing::Invoke;
using testing::NiceMock;
using testing::Ref;
using testing::Return;
using testing::ReturnRef;
using testing::Test;
using testing::TestWithParam;
using testing::Values;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ThriftProxy {
namespace Router {

namespace {

class TestNamedTransportConfigFactory : public NamedTransportConfigFactory {
public:
  TestNamedTransportConfigFactory(std::function<MockTransport*()> f) : f_(f) {}

  TransportPtr createTransport() override { return TransportPtr{f_()}; }
  std::string name() override { return TransportNames::get().FRAMED; }

  std::function<MockTransport*()> f_;
};

class TestNamedProtocolConfigFactory : public NamedProtocolConfigFactory {
public:
  TestNamedProtocolConfigFactory(std::function<MockProtocol*()> f) : f_(f) {}

  ProtocolPtr createProtocol() override { return ProtocolPtr{f_()}; }
  std::string name() override { return ProtocolNames::get().BINARY; }

  std::function<MockProtocol*()> f_;
};

} // namespace

class ThriftRouterTestBase {
public:
  ThriftRouterTestBase()
      : transport_factory_([&]() -> MockTransport* {
          ASSERT(transport_ == nullptr);
          transport_ = new NiceMock<MockTransport>();
          if (mock_transport_cb_) {
            mock_transport_cb_(transport_);
          }
          return transport_;
        }),
        protocol_factory_([&]() -> MockProtocol* {
          ASSERT(protocol_ == nullptr);
          protocol_ = new NiceMock<MockProtocol>();
          if (mock_protocol_cb_) {
            mock_protocol_cb_(protocol_);
          }
          return protocol_;
        }),
        transport_register_(transport_factory_), protocol_register_(protocol_factory_) {}

  void initializeRouter() {
    route_ = new NiceMock<MockRoute>();
    route_ptr_.reset(route_);

    router_.reset(new Router(context_.clusterManager()));

    EXPECT_EQ(nullptr, router_->downstreamConnection());

    router_->setDecoderFilterCallbacks(callbacks_);
  }

  void initializeMetadata(MessageType msg_type) {
    msg_type_ = msg_type;

    metadata_.reset(new MessageMetadata());
    metadata_->setMethodName("method");
    metadata_->setMessageType(msg_type_);
    metadata_->setSequenceId(1);
  }

  void startRequest(MessageType msg_type) {
    EXPECT_EQ(FilterStatus::Continue, router_->transportBegin(metadata_));

    EXPECT_CALL(callbacks_, route()).WillOnce(Return(route_ptr_));
    EXPECT_CALL(*route_, routeEntry()).WillOnce(Return(&route_entry_));
    EXPECT_CALL(route_entry_, clusterName()).WillRepeatedly(ReturnRef(cluster_name_));

    initializeMetadata(msg_type);

    EXPECT_CALL(callbacks_, downstreamTransportType()).WillOnce(Return(TransportType::Framed));
    EXPECT_CALL(callbacks_, downstreamProtocolType()).WillOnce(Return(ProtocolType::Binary));
    EXPECT_EQ(FilterStatus::StopIteration, router_->messageBegin(metadata_));

    EXPECT_CALL(callbacks_, connection()).WillRepeatedly(Return(&connection_));
    EXPECT_EQ(&connection_, router_->downstreamConnection());

    // Not yet implemented:
    EXPECT_EQ(absl::optional<uint64_t>(), router_->computeHashKey());
    EXPECT_EQ(nullptr, router_->metadataMatchCriteria());
    EXPECT_EQ(nullptr, router_->downstreamHeaders());
  }

  void connectUpstream() {
    EXPECT_CALL(*context_.cluster_manager_.tcp_conn_pool_.connection_data_, addUpstreamCallbacks(_))
        .WillOnce(Invoke([&](Tcp::ConnectionPool::UpstreamCallbacks& cb) -> void {
          upstream_callbacks_ = &cb;
        }));

    conn_state_.reset();
    EXPECT_CALL(*context_.cluster_manager_.tcp_conn_pool_.connection_data_, connectionState())
        .WillRepeatedly(
            Invoke([&]() -> Tcp::ConnectionPool::ConnectionState* { return conn_state_.get(); }));
    EXPECT_CALL(*context_.cluster_manager_.tcp_conn_pool_.connection_data_, setConnectionState_(_))
        .WillOnce(Invoke(
            [&](Tcp::ConnectionPool::ConnectionStatePtr& cs) -> void { conn_state_.swap(cs); }));

    EXPECT_CALL(*protocol_, writeMessageBegin(_, _))
        .WillOnce(Invoke([&](Buffer::Instance&, const MessageMetadata& metadata) -> void {
          EXPECT_EQ(metadata_->methodName(), metadata.methodName());
          EXPECT_EQ(metadata_->messageType(), metadata.messageType());
          EXPECT_EQ(metadata_->sequenceId(), metadata.sequenceId());
        }));

    EXPECT_CALL(callbacks_, continueDecoding());
    context_.cluster_manager_.tcp_conn_pool_.poolReady(upstream_connection_);

    EXPECT_NE(nullptr, upstream_callbacks_);
  }

  void startRequestWithExistingConnection(MessageType msg_type) {
    EXPECT_EQ(FilterStatus::Continue, router_->transportBegin({}));

    EXPECT_CALL(callbacks_, route()).WillOnce(Return(route_ptr_));
    EXPECT_CALL(*route_, routeEntry()).WillOnce(Return(&route_entry_));
    EXPECT_CALL(route_entry_, clusterName()).WillRepeatedly(ReturnRef(cluster_name_));

    initializeMetadata(msg_type);

    EXPECT_CALL(*context_.cluster_manager_.tcp_conn_pool_.connection_data_, addUpstreamCallbacks(_))
        .WillOnce(Invoke([&](Tcp::ConnectionPool::UpstreamCallbacks& cb) -> void {
          upstream_callbacks_ = &cb;
        }));

    if (!conn_state_) {
      conn_state_.reset(new ThriftConnectionState());
    }
    EXPECT_CALL(*context_.cluster_manager_.tcp_conn_pool_.connection_data_, connectionState())
        .WillRepeatedly(
            Invoke([&]() -> Tcp::ConnectionPool::ConnectionState* { return conn_state_.get(); }));

    EXPECT_CALL(callbacks_, connection()).WillRepeatedly(Return(&connection_));
    EXPECT_EQ(&connection_, router_->downstreamConnection());

    // Not yet implemented:
    EXPECT_EQ(absl::optional<uint64_t>(), router_->computeHashKey());
    EXPECT_EQ(nullptr, router_->metadataMatchCriteria());
    EXPECT_EQ(nullptr, router_->downstreamHeaders());

    EXPECT_CALL(callbacks_, downstreamTransportType()).WillOnce(Return(TransportType::Framed));
    EXPECT_CALL(callbacks_, downstreamProtocolType()).WillOnce(Return(ProtocolType::Binary));

    mock_protocol_cb_ = [&](MockProtocol* protocol) -> void {
      ON_CALL(*protocol, type()).WillByDefault(Return(ProtocolType::Binary));
      EXPECT_CALL(*protocol, writeMessageBegin(_, _))
          .WillOnce(Invoke([&](Buffer::Instance&, const MessageMetadata& metadata) -> void {
            EXPECT_EQ(metadata_->methodName(), metadata.methodName());
            EXPECT_EQ(metadata_->messageType(), metadata.messageType());
            EXPECT_EQ(metadata_->sequenceId(), metadata.sequenceId());
          }));
    };
    EXPECT_CALL(callbacks_, continueDecoding()).Times(0);
    EXPECT_CALL(context_.cluster_manager_.tcp_conn_pool_, newConnection(_))
        .WillOnce(
            Invoke([&](Tcp::ConnectionPool::Callbacks& cb) -> Tcp::ConnectionPool::Cancellable* {
              context_.cluster_manager_.tcp_conn_pool_.newConnectionImpl(cb);
              context_.cluster_manager_.tcp_conn_pool_.poolReady(upstream_connection_);
              return nullptr;
            }));

    EXPECT_EQ(FilterStatus::Continue, router_->messageBegin(metadata_));
    EXPECT_NE(nullptr, upstream_callbacks_);
  }

  void sendTrivialStruct(FieldType field_type) {
    EXPECT_CALL(*protocol_, writeStructBegin(_, ""));
    EXPECT_EQ(FilterStatus::Continue, router_->structBegin({}));

    int16_t id = 1;
    EXPECT_CALL(*protocol_, writeFieldBegin(_, "", field_type, id));
    EXPECT_EQ(FilterStatus::Continue, router_->fieldBegin({}, field_type, id));

    sendTrivialValue(field_type);

    EXPECT_CALL(*protocol_, writeFieldEnd(_));
    EXPECT_EQ(FilterStatus::Continue, router_->fieldEnd());

    EXPECT_CALL(*protocol_, writeFieldBegin(_, "", FieldType::Stop, 0));
    EXPECT_CALL(*protocol_, writeStructEnd(_));
    EXPECT_EQ(FilterStatus::Continue, router_->structEnd());
  }

  void sendTrivialValue(FieldType field_type) {
    switch (field_type) {
    case FieldType::Bool: {
      bool v = true;
      EXPECT_CALL(*protocol_, writeBool(_, v));
      EXPECT_EQ(FilterStatus::Continue, router_->boolValue(v));
    } break;
    case FieldType::Byte: {
      uint8_t v = 2;
      EXPECT_CALL(*protocol_, writeByte(_, v));
      EXPECT_EQ(FilterStatus::Continue, router_->byteValue(v));
    } break;
    case FieldType::I16: {
      int16_t v = 3;
      EXPECT_CALL(*protocol_, writeInt16(_, v));
      EXPECT_EQ(FilterStatus::Continue, router_->int16Value(v));
    } break;
    case FieldType::I32: {
      int32_t v = 4;
      EXPECT_CALL(*protocol_, writeInt32(_, v));
      EXPECT_EQ(FilterStatus::Continue, router_->int32Value(v));
    } break;
    case FieldType::I64: {
      int64_t v = 5;
      EXPECT_CALL(*protocol_, writeInt64(_, v));
      EXPECT_EQ(FilterStatus::Continue, router_->int64Value(v));
    } break;
    case FieldType::Double: {
      double v = 6.0;
      EXPECT_CALL(*protocol_, writeDouble(_, v));
      EXPECT_EQ(FilterStatus::Continue, router_->doubleValue(v));
    } break;
    case FieldType::String: {
      std::string v = "seven";
      EXPECT_CALL(*protocol_, writeString(_, v));
      EXPECT_EQ(FilterStatus::Continue, router_->stringValue(v));
    } break;
    default:
      NOT_REACHED_GCOVR_EXCL_LINE;
    }
  }

  void completeRequest() {
    EXPECT_CALL(*protocol_, writeMessageEnd(_));
    EXPECT_CALL(*transport_, encodeFrame(_, _, _));
    EXPECT_CALL(upstream_connection_, write(_, false));

    if (msg_type_ == MessageType::Oneway) {
      EXPECT_CALL(context_.cluster_manager_.tcp_conn_pool_, released(Ref(upstream_connection_)));
    }

    EXPECT_EQ(FilterStatus::Continue, router_->messageEnd());
    EXPECT_EQ(FilterStatus::Continue, router_->transportEnd());
  }

  void returnResponse() {
    Buffer::OwnedImpl buffer;

    EXPECT_CALL(callbacks_, startUpstreamResponse(_, _));

    EXPECT_CALL(callbacks_, upstreamData(Ref(buffer)))
        .WillOnce(Return(ThriftFilters::ResponseStatus::MoreData));
    upstream_callbacks_->onUpstreamData(buffer, false);

    EXPECT_CALL(callbacks_, upstreamData(Ref(buffer)))
        .WillOnce(Return(ThriftFilters::ResponseStatus::Complete));
    EXPECT_CALL(context_.cluster_manager_.tcp_conn_pool_, released(Ref(upstream_connection_)));
    upstream_callbacks_->onUpstreamData(buffer, false);
  }

  void destroyRouter() {
    router_->onDestroy();
    router_.reset();
  }

  TestNamedTransportConfigFactory transport_factory_;
  TestNamedProtocolConfigFactory protocol_factory_;
  Registry::InjectFactory<NamedTransportConfigFactory> transport_register_;
  Registry::InjectFactory<NamedProtocolConfigFactory> protocol_register_;

  std::function<void(MockTransport*)> mock_transport_cb_{};
  std::function<void(MockProtocol*)> mock_protocol_cb_{};

  NiceMock<Server::Configuration::MockFactoryContext> context_;
  NiceMock<Network::MockClientConnection> connection_;
  NiceMock<ThriftFilters::MockDecoderFilterCallbacks> callbacks_;
  NiceMock<MockTransport>* transport_{};
  NiceMock<MockProtocol>* protocol_{};
  NiceMock<MockRoute>* route_{};
  NiceMock<MockRouteEntry> route_entry_;
  NiceMock<Upstream::MockHostDescription>* host_{};
  Tcp::ConnectionPool::ConnectionStatePtr conn_state_;

  RouteConstSharedPtr route_ptr_;
  std::unique_ptr<Router> router_;

  std::string cluster_name_{"cluster"};

  MessageType msg_type_{MessageType::Call};
  MessageMetadataSharedPtr metadata_;

  Tcp::ConnectionPool::UpstreamCallbacks* upstream_callbacks_{};
  NiceMock<Network::MockClientConnection> upstream_connection_;
};

class ThriftRouterTest : public ThriftRouterTestBase, public Test {
public:
  ThriftRouterTest() {}
};

class ThriftRouterFieldTypeTest : public ThriftRouterTestBase, public TestWithParam<FieldType> {
public:
  ThriftRouterFieldTypeTest() {}
};

INSTANTIATE_TEST_CASE_P(PrimitiveFieldTypes, ThriftRouterFieldTypeTest,
                        Values(FieldType::Bool, FieldType::Byte, FieldType::I16, FieldType::I32,
                               FieldType::I64, FieldType::Double, FieldType::String),
                        fieldTypeParamToString);

class ThriftRouterContainerTest : public ThriftRouterTestBase, public TestWithParam<FieldType> {
public:
  ThriftRouterContainerTest() {}
};

INSTANTIATE_TEST_CASE_P(ContainerFieldTypes, ThriftRouterContainerTest,
                        Values(FieldType::Map, FieldType::List, FieldType::Set),
                        fieldTypeParamToString);

TEST_F(ThriftRouterTest, PoolRemoteConnectionFailure) {
  initializeRouter();

  startRequest(MessageType::Call);

  EXPECT_CALL(callbacks_, sendLocalReply(_, _))
      .WillOnce(Invoke([&](const DirectResponse& response, bool end_stream) -> void {
        auto& app_ex = dynamic_cast<const AppException&>(response);
        EXPECT_EQ(AppExceptionType::InternalError, app_ex.type_);
        EXPECT_THAT(app_ex.what(), ContainsRegex(".*connection failure.*"));
        EXPECT_TRUE(end_stream);
      }));
  context_.cluster_manager_.tcp_conn_pool_.poolFailure(
      Tcp::ConnectionPool::PoolFailureReason::RemoteConnectionFailure);
}

TEST_F(ThriftRouterTest, PoolLocalConnectionFailure) {
  initializeRouter();

  startRequest(MessageType::Call);

  context_.cluster_manager_.tcp_conn_pool_.poolFailure(
      Tcp::ConnectionPool::PoolFailureReason::LocalConnectionFailure);
}

TEST_F(ThriftRouterTest, PoolTimeout) {
  initializeRouter();

  startRequest(MessageType::Call);

  EXPECT_CALL(callbacks_, sendLocalReply(_, _))
      .WillOnce(Invoke([&](const DirectResponse& response, bool end_stream) -> void {
        auto& app_ex = dynamic_cast<const AppException&>(response);
        EXPECT_EQ(AppExceptionType::InternalError, app_ex.type_);
        EXPECT_THAT(app_ex.what(), ContainsRegex(".*connection failure.*"));
        EXPECT_TRUE(end_stream);
      }));
  context_.cluster_manager_.tcp_conn_pool_.poolFailure(
      Tcp::ConnectionPool::PoolFailureReason::Timeout);
}

TEST_F(ThriftRouterTest, PoolOverflowFailure) {
  initializeRouter();

  startRequest(MessageType::Call);

  EXPECT_CALL(callbacks_, sendLocalReply(_, _))
      .WillOnce(Invoke([&](const DirectResponse& response, bool end_stream) -> void {
        auto& app_ex = dynamic_cast<const AppException&>(response);
        EXPECT_EQ(AppExceptionType::InternalError, app_ex.type_);
        EXPECT_THAT(app_ex.what(), ContainsRegex(".*too many connections.*"));
        EXPECT_TRUE(end_stream);
      }));
  context_.cluster_manager_.tcp_conn_pool_.poolFailure(
      Tcp::ConnectionPool::PoolFailureReason::Overflow);
}

TEST_F(ThriftRouterTest, PoolConnectionFailureWithOnewayMessage) {
  initializeRouter();
  startRequest(MessageType::Oneway);

  EXPECT_CALL(callbacks_, sendLocalReply(_, _)).Times(0);
  EXPECT_CALL(callbacks_, resetDownstreamConnection());
  context_.cluster_manager_.tcp_conn_pool_.poolFailure(
      Tcp::ConnectionPool::PoolFailureReason::RemoteConnectionFailure);

  destroyRouter();
}

TEST_F(ThriftRouterTest, NoRoute) {
  initializeRouter();
  initializeMetadata(MessageType::Call);

  EXPECT_CALL(callbacks_, route()).WillOnce(Return(nullptr));
  EXPECT_CALL(callbacks_, sendLocalReply(_, _))
      .WillOnce(Invoke([&](const DirectResponse& response, bool end_stream) -> void {
        auto& app_ex = dynamic_cast<const AppException&>(response);
        EXPECT_EQ(AppExceptionType::UnknownMethod, app_ex.type_);
        EXPECT_THAT(app_ex.what(), ContainsRegex(".*no route.*"));
        EXPECT_TRUE(end_stream);
      }));
  EXPECT_EQ(FilterStatus::StopIteration, router_->messageBegin(metadata_));
}

TEST_F(ThriftRouterTest, NoCluster) {
  initializeRouter();
  initializeMetadata(MessageType::Call);

  EXPECT_CALL(callbacks_, route()).WillOnce(Return(route_ptr_));
  EXPECT_CALL(*route_, routeEntry()).WillOnce(Return(&route_entry_));
  EXPECT_CALL(route_entry_, clusterName()).WillRepeatedly(ReturnRef(cluster_name_));
  EXPECT_CALL(context_.cluster_manager_, get(cluster_name_)).WillOnce(Return(nullptr));
  EXPECT_CALL(callbacks_, sendLocalReply(_, _))
      .WillOnce(Invoke([&](const DirectResponse& response, bool end_stream) -> void {
        auto& app_ex = dynamic_cast<const AppException&>(response);
        EXPECT_EQ(AppExceptionType::InternalError, app_ex.type_);
        EXPECT_THAT(app_ex.what(), ContainsRegex(".*unknown cluster.*"));
        EXPECT_TRUE(end_stream);
      }));
  EXPECT_EQ(FilterStatus::StopIteration, router_->messageBegin(metadata_));
}

TEST_F(ThriftRouterTest, ClusterMaintenanceMode) {
  initializeRouter();
  initializeMetadata(MessageType::Call);

  EXPECT_CALL(callbacks_, route()).WillOnce(Return(route_ptr_));
  EXPECT_CALL(*route_, routeEntry()).WillOnce(Return(&route_entry_));
  EXPECT_CALL(route_entry_, clusterName()).WillRepeatedly(ReturnRef(cluster_name_));
  EXPECT_CALL(*context_.cluster_manager_.thread_local_cluster_.cluster_.info_, maintenanceMode())
      .WillOnce(Return(true));

  EXPECT_CALL(callbacks_, sendLocalReply(_, _))
      .WillOnce(Invoke([&](const DirectResponse& response, bool end_stream) -> void {
        auto& app_ex = dynamic_cast<const AppException&>(response);
        EXPECT_EQ(AppExceptionType::InternalError, app_ex.type_);
        EXPECT_THAT(app_ex.what(), ContainsRegex(".*maintenance mode.*"));
        EXPECT_TRUE(end_stream);
      }));
  EXPECT_EQ(FilterStatus::StopIteration, router_->messageBegin(metadata_));
}

TEST_F(ThriftRouterTest, NoHealthyHosts) {
  initializeRouter();
  initializeMetadata(MessageType::Call);

  EXPECT_CALL(callbacks_, route()).WillOnce(Return(route_ptr_));
  EXPECT_CALL(*route_, routeEntry()).WillOnce(Return(&route_entry_));
  EXPECT_CALL(route_entry_, clusterName()).WillRepeatedly(ReturnRef(cluster_name_));
  EXPECT_CALL(context_.cluster_manager_, tcpConnPoolForCluster(cluster_name_, _, _))
      .WillOnce(Return(nullptr));

  EXPECT_CALL(callbacks_, sendLocalReply(_, _))
      .WillOnce(Invoke([&](const DirectResponse& response, bool end_stream) -> void {
        auto& app_ex = dynamic_cast<const AppException&>(response);
        EXPECT_EQ(AppExceptionType::InternalError, app_ex.type_);
        EXPECT_THAT(app_ex.what(), ContainsRegex(".*no healthy upstream.*"));
        EXPECT_TRUE(end_stream);
      }));

  EXPECT_EQ(FilterStatus::StopIteration, router_->messageBegin(metadata_));
}

TEST_F(ThriftRouterTest, TruncatedResponse) {
  initializeRouter();
  startRequest(MessageType::Call);
  connectUpstream();
  sendTrivialStruct(FieldType::String);
  completeRequest();

  Buffer::OwnedImpl buffer;

  EXPECT_CALL(callbacks_, startUpstreamResponse(_, _));
  EXPECT_CALL(callbacks_, upstreamData(Ref(buffer)))
      .WillOnce(Return(ThriftFilters::ResponseStatus::MoreData));
  EXPECT_CALL(context_.cluster_manager_.tcp_conn_pool_, released(Ref(upstream_connection_)));
  EXPECT_CALL(callbacks_, resetDownstreamConnection());

  upstream_callbacks_->onUpstreamData(buffer, true);
  destroyRouter();
}

TEST_F(ThriftRouterTest, UpstreamRemoteCloseMidResponse) {
  initializeRouter();
  startRequest(MessageType::Call);
  connectUpstream();

  EXPECT_CALL(callbacks_, sendLocalReply(_, _))
      .WillOnce(Invoke([&](const DirectResponse& response, bool end_stream) -> void {
        auto& app_ex = dynamic_cast<const AppException&>(response);
        EXPECT_EQ(AppExceptionType::InternalError, app_ex.type_);
        EXPECT_THAT(app_ex.what(), ContainsRegex(".*connection failure.*"));
        EXPECT_TRUE(end_stream);
      }));
  upstream_callbacks_->onEvent(Network::ConnectionEvent::RemoteClose);
  destroyRouter();
}

TEST_F(ThriftRouterTest, UpstreamLocalCloseMidResponse) {
  initializeRouter();
  startRequest(MessageType::Call);
  connectUpstream();

  upstream_callbacks_->onEvent(Network::ConnectionEvent::LocalClose);
  destroyRouter();
}

TEST_F(ThriftRouterTest, UpstreamCloseAfterResponse) {
  initializeRouter();
  startRequest(MessageType::Call);
  connectUpstream();
  sendTrivialStruct(FieldType::String);
  completeRequest();

  upstream_callbacks_->onEvent(Network::ConnectionEvent::LocalClose);
  destroyRouter();
}

TEST_F(ThriftRouterTest, UpstreamDataTriggersReset) {
  initializeRouter();
  startRequest(MessageType::Call);
  connectUpstream();
  sendTrivialStruct(FieldType::String);
  completeRequest();

  Buffer::OwnedImpl buffer;

  EXPECT_CALL(callbacks_, startUpstreamResponse(_, _));
  EXPECT_CALL(callbacks_, upstreamData(Ref(buffer)))
      .WillOnce(Return(ThriftFilters::ResponseStatus::Reset));
  EXPECT_CALL(upstream_connection_, close(Network::ConnectionCloseType::NoFlush));

  upstream_callbacks_->onUpstreamData(buffer, true);
  destroyRouter();
}

TEST_F(ThriftRouterTest, UnexpectedUpstreamRemoteClose) {
  initializeRouter();
  startRequest(MessageType::Call);
  connectUpstream();
  sendTrivialStruct(FieldType::String);

  EXPECT_CALL(callbacks_, sendLocalReply(_, _))
      .WillOnce(Invoke([&](const DirectResponse& response, bool end_stream) -> void {
        auto& app_ex = dynamic_cast<const AppException&>(response);
        EXPECT_EQ(AppExceptionType::InternalError, app_ex.type_);
        EXPECT_THAT(app_ex.what(), ContainsRegex(".*connection failure.*"));
        EXPECT_TRUE(end_stream);
      }));
  router_->onEvent(Network::ConnectionEvent::RemoteClose);
}

TEST_F(ThriftRouterTest, UnexpectedUpstreamLocalClose) {
  initializeRouter();
  startRequest(MessageType::Call);
  connectUpstream();
  sendTrivialStruct(FieldType::String);

  EXPECT_CALL(callbacks_, sendLocalReply(_, _))
      .WillOnce(Invoke([&](const DirectResponse& response, bool end_stream) -> void {
        auto& app_ex = dynamic_cast<const AppException&>(response);
        EXPECT_EQ(AppExceptionType::InternalError, app_ex.type_);
        EXPECT_THAT(app_ex.what(), ContainsRegex(".*connection failure.*"));
        EXPECT_TRUE(end_stream);
      }));
  router_->onEvent(Network::ConnectionEvent::RemoteClose);
}

TEST_F(ThriftRouterTest, UnexpectedRouterDestroyBeforeUpstreamConnect) {
  initializeRouter();
  startRequest(MessageType::Call);

  EXPECT_EQ(1, context_.cluster_manager_.tcp_conn_pool_.handles_.size());
  EXPECT_CALL(context_.cluster_manager_.tcp_conn_pool_.handles_.front(),
              cancel(Tcp::ConnectionPool::CancelPolicy::Default));
  destroyRouter();
}

TEST_F(ThriftRouterTest, UnexpectedRouterDestroy) {
  initializeRouter();
  startRequest(MessageType::Call);
  connectUpstream();
  EXPECT_CALL(upstream_connection_, close(Network::ConnectionCloseType::NoFlush));
  destroyRouter();
}

TEST_F(ThriftRouterTest, ProtocolUpgrade) {
  initializeRouter();
  startRequest(MessageType::Call);

  EXPECT_CALL(*context_.cluster_manager_.tcp_conn_pool_.connection_data_, addUpstreamCallbacks(_))
      .WillOnce(Invoke(
          [&](Tcp::ConnectionPool::UpstreamCallbacks& cb) -> void { upstream_callbacks_ = &cb; }));

  conn_state_.reset();
  EXPECT_CALL(*context_.cluster_manager_.tcp_conn_pool_.connection_data_, connectionState())
      .WillRepeatedly(
          Invoke([&]() -> Tcp::ConnectionPool::ConnectionState* { return conn_state_.get(); }));
  EXPECT_CALL(*context_.cluster_manager_.tcp_conn_pool_.connection_data_, setConnectionState_(_))
      .WillOnce(Invoke(
          [&](Tcp::ConnectionPool::ConnectionStatePtr& cs) -> void { conn_state_.swap(cs); }));

  EXPECT_CALL(*protocol_, supportsUpgrade()).WillOnce(Return(true));

  MockThriftObject* upgrade_response = new NiceMock<MockThriftObject>();

  EXPECT_CALL(*protocol_, attemptUpgrade(_, _, _))
      .WillOnce(Invoke(
          [&](Transport&, ThriftConnectionState&, Buffer::Instance& buffer) -> ThriftObjectPtr {
            buffer.add("upgrade request");
            return ThriftObjectPtr{upgrade_response};
          }));
  EXPECT_CALL(upstream_connection_, write(_, false))
      .WillOnce(Invoke([&](Buffer::Instance& buffer, bool) -> void {
        EXPECT_EQ("upgrade request", buffer.toString());
      }));

  context_.cluster_manager_.tcp_conn_pool_.poolReady(upstream_connection_);
  EXPECT_NE(nullptr, upstream_callbacks_);

  Buffer::OwnedImpl buffer;
  EXPECT_CALL(*upgrade_response, onData(Ref(buffer))).WillOnce(Return(false));
  upstream_callbacks_->onUpstreamData(buffer, false);

  EXPECT_CALL(*upgrade_response, onData(Ref(buffer))).WillOnce(Return(true));
  EXPECT_CALL(*protocol_, completeUpgrade(_, Ref(*upgrade_response)));
  EXPECT_CALL(callbacks_, continueDecoding());
  EXPECT_CALL(*protocol_, writeMessageBegin(_, _))
      .WillOnce(Invoke([&](Buffer::Instance&, const MessageMetadata& metadata) -> void {
        EXPECT_EQ(metadata_->methodName(), metadata.methodName());
        EXPECT_EQ(metadata_->messageType(), metadata.messageType());
        EXPECT_EQ(metadata_->sequenceId(), metadata.sequenceId());
      }));
  upstream_callbacks_->onUpstreamData(buffer, false);

  // Then the actual request...
  sendTrivialStruct(FieldType::String);
  completeRequest();
  returnResponse();
  destroyRouter();
}

TEST_F(ThriftRouterTest, ProtocolUpgradeSkippedOnExistingConnection) {
  initializeRouter();
  startRequest(MessageType::Call);

  EXPECT_CALL(*context_.cluster_manager_.tcp_conn_pool_.connection_data_, addUpstreamCallbacks(_))
      .WillOnce(Invoke(
          [&](Tcp::ConnectionPool::UpstreamCallbacks& cb) -> void { upstream_callbacks_ = &cb; }));

  conn_state_ = std::make_unique<ThriftConnectionState>();
  EXPECT_CALL(*context_.cluster_manager_.tcp_conn_pool_.connection_data_, connectionState())
      .WillRepeatedly(
          Invoke([&]() -> Tcp::ConnectionPool::ConnectionState* { return conn_state_.get(); }));

  EXPECT_CALL(*protocol_, supportsUpgrade()).WillOnce(Return(true));

  // Protocol determines that connection state shows upgrade already occurred
  EXPECT_CALL(*protocol_, attemptUpgrade(_, _, _))
      .WillOnce(Invoke([&](Transport&, ThriftConnectionState&,
                           Buffer::Instance&) -> ThriftObjectPtr { return nullptr; }));

  EXPECT_CALL(*protocol_, writeMessageBegin(_, _))
      .WillOnce(Invoke([&](Buffer::Instance&, const MessageMetadata& metadata) -> void {
        EXPECT_EQ(metadata_->methodName(), metadata.methodName());
        EXPECT_EQ(metadata_->messageType(), metadata.messageType());
        EXPECT_EQ(metadata_->sequenceId(), metadata.sequenceId());
      }));
  EXPECT_CALL(callbacks_, continueDecoding());

  context_.cluster_manager_.tcp_conn_pool_.poolReady(upstream_connection_);
  EXPECT_NE(nullptr, upstream_callbacks_);

  // Then the actual request...
  sendTrivialStruct(FieldType::String);
  completeRequest();
  returnResponse();
  destroyRouter();
}

TEST_P(ThriftRouterFieldTypeTest, OneWay) {
  FieldType field_type = GetParam();

  initializeRouter();
  startRequest(MessageType::Oneway);
  connectUpstream();
  sendTrivialStruct(field_type);
  completeRequest();
  destroyRouter();
}

TEST_P(ThriftRouterFieldTypeTest, Call) {
  FieldType field_type = GetParam();

  initializeRouter();
  startRequest(MessageType::Call);
  connectUpstream();
  sendTrivialStruct(field_type);
  completeRequest();
  returnResponse();
  destroyRouter();
}

TEST_F(ThriftRouterTest, CallWithExistingConnection) {
  initializeRouter();

  // Simulate previous sequence id usage.
  conn_state_.reset(new ThriftConnectionState(3));

  startRequestWithExistingConnection(MessageType::Call);
  sendTrivialStruct(FieldType::I32);
  completeRequest();

  EXPECT_EQ(3, metadata_->sequenceId());

  returnResponse();
  destroyRouter();
}

TEST_P(ThriftRouterContainerTest, DecoderFilterCallbacks) {
  FieldType field_type = GetParam();
  int16_t field_id = 1;

  initializeRouter();

  startRequest(MessageType::Oneway);
  connectUpstream();

  EXPECT_CALL(*protocol_, writeStructBegin(_, ""));
  EXPECT_EQ(FilterStatus::Continue, router_->structBegin({}));

  EXPECT_CALL(*protocol_, writeFieldBegin(_, "", field_type, field_id));
  EXPECT_EQ(FilterStatus::Continue, router_->fieldBegin({}, field_type, field_id));

  FieldType container_type = FieldType::I32;
  uint32_t size{};

  switch (field_type) {
  case FieldType::Map:
    size = 2;
    EXPECT_CALL(*protocol_, writeMapBegin(_, container_type, container_type, size));
    EXPECT_EQ(FilterStatus::Continue, router_->mapBegin(container_type, container_type, size));
    for (int i = 0; i < 2; i++) {
      EXPECT_CALL(*protocol_, writeInt32(_, i));
      EXPECT_EQ(FilterStatus::Continue, router_->int32Value(i));
      int j = i + 100;
      EXPECT_CALL(*protocol_, writeInt32(_, j));
      EXPECT_EQ(FilterStatus::Continue, router_->int32Value(j));
    }
    EXPECT_CALL(*protocol_, writeMapEnd(_));
    EXPECT_EQ(FilterStatus::Continue, router_->mapEnd());
    break;
  case FieldType::List:
    size = 3;
    EXPECT_CALL(*protocol_, writeListBegin(_, container_type, size));
    EXPECT_EQ(FilterStatus::Continue, router_->listBegin(container_type, size));
    for (int i = 0; i < 3; i++) {
      EXPECT_CALL(*protocol_, writeInt32(_, i));
      EXPECT_EQ(FilterStatus::Continue, router_->int32Value(i));
    }
    EXPECT_CALL(*protocol_, writeListEnd(_));
    EXPECT_EQ(FilterStatus::Continue, router_->listEnd());
    break;
  case FieldType::Set:
    size = 4;
    EXPECT_CALL(*protocol_, writeSetBegin(_, container_type, size));
    EXPECT_EQ(FilterStatus::Continue, router_->setBegin(container_type, size));
    for (int i = 0; i < 4; i++) {
      EXPECT_CALL(*protocol_, writeInt32(_, i));
      EXPECT_EQ(FilterStatus::Continue, router_->int32Value(i));
    }
    EXPECT_CALL(*protocol_, writeSetEnd(_));
    EXPECT_EQ(FilterStatus::Continue, router_->setEnd());
    break;
  default:
    NOT_REACHED_GCOVR_EXCL_LINE;
  }

  EXPECT_CALL(*protocol_, writeFieldEnd(_));
  EXPECT_EQ(FilterStatus::Continue, router_->fieldEnd());

  EXPECT_CALL(*protocol_, writeFieldBegin(_, _, FieldType::Stop, 0));
  EXPECT_CALL(*protocol_, writeStructEnd(_));
  EXPECT_EQ(FilterStatus::Continue, router_->structEnd());

  completeRequest();
  destroyRouter();
}

} // namespace Router
} // namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
