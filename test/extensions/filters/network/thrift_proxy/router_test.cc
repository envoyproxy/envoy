#include <memory>

#include "envoy/config/filter/thrift/router/v2alpha1/router.pb.h"
#include "envoy/config/filter/thrift/router/v2alpha1/router.pb.validate.h"
#include "envoy/tcp/conn_pool.h"

#include "common/buffer/buffer_impl.h"

#include "extensions/filters/network/thrift_proxy/app_exception_impl.h"
#include "extensions/filters/network/thrift_proxy/config.h"
#include "extensions/filters/network/thrift_proxy/router/config.h"
#include "extensions/filters/network/thrift_proxy/router/router_impl.h"

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
using testing::AtLeast;
using testing::Combine;
using testing::ContainsRegex;
using testing::Eq;
using testing::Invoke;
using testing::NiceMock;
using testing::Ref;
using testing::Return;
using testing::ReturnRef;
using ::testing::TestParamInfo;
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
  std::string name() const override { return TransportNames::get().FRAMED; }

  std::function<MockTransport*()> f_;
};

class TestNamedProtocolConfigFactory : public NamedProtocolConfigFactory {
public:
  TestNamedProtocolConfigFactory(std::function<MockProtocol*()> f) : f_(f) {}

  ProtocolPtr createProtocol() override { return ProtocolPtr{f_()}; }
  std::string name() const override { return ProtocolNames::get().BINARY; }

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
        transport_register_(transport_factory_), protocol_register_(protocol_factory_) {
    context_.cluster_manager_.initializeThreadLocalClusters({"cluster"});
  }

  void initializeRouter() {
    route_ = new NiceMock<MockRoute>();
    route_ptr_.reset(route_);

    router_ = std::make_unique<Router>(context_.clusterManager(), "test", context_.scope());

    EXPECT_EQ(nullptr, router_->downstreamConnection());

    router_->setDecoderFilterCallbacks(callbacks_);
  }

  void initializeMetadata(MessageType msg_type, std::string method = "method") {
    msg_type_ = msg_type;

    metadata_ = std::make_shared<MessageMetadata>();
    metadata_->setMethodName(method);
    metadata_->setMessageType(msg_type_);
    metadata_->setSequenceId(1);
  }

  void startRequest(MessageType msg_type, std::string method = "method",
                    const bool strip_service_name = false,
                    const TransportType transport_type = TransportType::Framed,
                    const ProtocolType protocol_type = ProtocolType::Binary) {
    EXPECT_EQ(FilterStatus::Continue, router_->transportBegin(metadata_));

    EXPECT_CALL(callbacks_, route()).WillOnce(Return(route_ptr_));
    EXPECT_CALL(*route_, routeEntry()).WillOnce(Return(&route_entry_));
    EXPECT_CALL(route_entry_, clusterName()).WillRepeatedly(ReturnRef(cluster_name_));

    if (strip_service_name) {
      EXPECT_CALL(route_entry_, stripServiceName()).WillOnce(Return(true));
    }

    initializeMetadata(msg_type, method);

    EXPECT_CALL(callbacks_, downstreamTransportType())
        .Times(AtLeast(1))
        .WillRepeatedly(Return(transport_type));
    EXPECT_CALL(callbacks_, downstreamProtocolType())
        .Times(AtLeast(1))
        .WillRepeatedly(Return(protocol_type));
    EXPECT_EQ(FilterStatus::StopIteration, router_->messageBegin(metadata_));

    EXPECT_CALL(callbacks_, connection()).WillRepeatedly(Return(&connection_));
    EXPECT_CALL(callbacks_, dispatcher()).WillRepeatedly(ReturnRef(dispatcher_));
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
    EXPECT_CALL(*context_.cluster_manager_.thread_local_cluster_.tcp_conn_pool_.connection_data_,
                setConnectionState_(_))
        .WillOnce(Invoke(
            [&](Tcp::ConnectionPool::ConnectionStatePtr& cs) -> void { conn_state_.swap(cs); }));

    EXPECT_CALL(*protocol_, writeMessageBegin(_, _))
        .WillOnce(Invoke([&](Buffer::Instance&, const MessageMetadata& metadata) -> void {
          EXPECT_EQ(metadata_->methodName(), metadata.methodName());
          EXPECT_EQ(metadata_->messageType(), metadata.messageType());
          EXPECT_EQ(metadata_->sequenceId(), metadata.sequenceId());
        }));

    EXPECT_CALL(callbacks_, continueDecoding());
    context_.cluster_manager_.thread_local_cluster_.tcp_conn_pool_.poolReady(upstream_connection_);

    EXPECT_NE(nullptr, upstream_callbacks_);
  }

  void startRequestWithExistingConnection(MessageType msg_type) {
    EXPECT_EQ(FilterStatus::Continue, router_->transportBegin({}));

    EXPECT_CALL(callbacks_, route()).WillOnce(Return(route_ptr_));
    EXPECT_CALL(*route_, routeEntry()).WillOnce(Return(&route_entry_));
    EXPECT_CALL(route_entry_, clusterName()).WillRepeatedly(ReturnRef(cluster_name_));

    initializeMetadata(msg_type);

    EXPECT_CALL(*context_.cluster_manager_.thread_local_cluster_.tcp_conn_pool_.connection_data_,
                addUpstreamCallbacks(_))
        .WillOnce(Invoke([&](Tcp::ConnectionPool::UpstreamCallbacks& cb) -> void {
          upstream_callbacks_ = &cb;
        }));

    if (!conn_state_) {
      conn_state_ = std::make_unique<ThriftConnectionState>();
    }
    EXPECT_CALL(*context_.cluster_manager_.thread_local_cluster_.tcp_conn_pool_.connection_data_,
                connectionState())
        .WillRepeatedly(
            Invoke([&]() -> Tcp::ConnectionPool::ConnectionState* { return conn_state_.get(); }));

    EXPECT_CALL(callbacks_, connection()).WillRepeatedly(Return(&connection_));
    EXPECT_CALL(callbacks_, dispatcher()).WillRepeatedly(ReturnRef(dispatcher_));
    EXPECT_EQ(&connection_, router_->downstreamConnection());

    // Not yet implemented:
    EXPECT_EQ(absl::optional<uint64_t>(), router_->computeHashKey());
    EXPECT_EQ(nullptr, router_->metadataMatchCriteria());
    EXPECT_EQ(nullptr, router_->downstreamHeaders());

    EXPECT_CALL(callbacks_, downstreamTransportType())
        .Times(2)
        .WillRepeatedly(Return(TransportType::Framed));
    EXPECT_CALL(callbacks_, downstreamProtocolType())
        .Times(2)
        .WillRepeatedly(Return(ProtocolType::Binary));

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
    EXPECT_CALL(context_.cluster_manager_.thread_local_cluster_.tcp_conn_pool_, newConnection(_))
        .WillOnce(
            Invoke([&](Tcp::ConnectionPool::Callbacks& cb) -> Tcp::ConnectionPool::Cancellable* {
              context_.cluster_manager_.thread_local_cluster_.tcp_conn_pool_.newConnectionImpl(cb);
              context_.cluster_manager_.thread_local_cluster_.tcp_conn_pool_.poolReady(
                  upstream_connection_);
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
      EXPECT_CALL(context_.cluster_manager_.thread_local_cluster_.tcp_conn_pool_,
                  released(Ref(upstream_connection_)));
    }

    EXPECT_EQ(FilterStatus::Continue, router_->messageEnd());
    EXPECT_EQ(FilterStatus::Continue, router_->transportEnd());
  }

  void returnResponse(MessageType msg_type = MessageType::Reply, bool is_success = true) {
    Buffer::OwnedImpl buffer;

    EXPECT_CALL(callbacks_, startUpstreamResponse(_, _));

    auto metadata = std::make_shared<MessageMetadata>();
    metadata->setMessageType(msg_type);
    metadata->setSequenceId(1);
    ON_CALL(callbacks_, responseMetadata()).WillByDefault(Return(metadata));
    ON_CALL(callbacks_, responseSuccess()).WillByDefault(Return(is_success));

    EXPECT_CALL(callbacks_, upstreamData(Ref(buffer)))
        .WillOnce(Return(ThriftFilters::ResponseStatus::MoreData));
    upstream_callbacks_->onUpstreamData(buffer, false);

    EXPECT_CALL(callbacks_, upstreamData(Ref(buffer)))
        .WillOnce(Return(ThriftFilters::ResponseStatus::Complete));
    EXPECT_CALL(context_.cluster_manager_.thread_local_cluster_.tcp_conn_pool_,
                released(Ref(upstream_connection_)));
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
  NiceMock<Event::MockDispatcher> dispatcher_;
  NiceMock<MockTimeSystem> time_source_;
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

class ThriftRouterTest : public testing::Test, public ThriftRouterTestBase {
public:
};

class ThriftRouterFieldTypeTest : public testing::TestWithParam<FieldType>,
                                  public ThriftRouterTestBase {
public:
};

INSTANTIATE_TEST_SUITE_P(PrimitiveFieldTypes, ThriftRouterFieldTypeTest,
                         Values(FieldType::Bool, FieldType::Byte, FieldType::I16, FieldType::I32,
                                FieldType::I64, FieldType::Double, FieldType::String),
                         fieldTypeParamToString);

class ThriftRouterContainerTest : public testing::TestWithParam<FieldType>,
                                  public ThriftRouterTestBase {
public:
};

INSTANTIATE_TEST_SUITE_P(ContainerFieldTypes, ThriftRouterContainerTest,
                         Values(FieldType::Map, FieldType::List, FieldType::Set),
                         fieldTypeParamToString);

class ThriftRouterPassthroughTest
    : public testing::TestWithParam<
          std::tuple<TransportType, ProtocolType, TransportType, ProtocolType>>,
      public ThriftRouterTestBase {
public:
};

static std::string downstreamUpstreamTypesToString(
    const TestParamInfo<std::tuple<TransportType, ProtocolType, TransportType, ProtocolType>>&
        params) {
  TransportType downstream_transport_type;
  ProtocolType downstream_protocol_type;
  TransportType upstream_transport_type;
  ProtocolType upstream_protocol_type;

  std::tie(downstream_transport_type, downstream_protocol_type, upstream_transport_type,
           upstream_protocol_type) = params.param;

  return fmt::format("{}{}{}{}", TransportNames::get().fromType(downstream_transport_type),
                     ProtocolNames::get().fromType(downstream_protocol_type),
                     TransportNames::get().fromType(upstream_transport_type),
                     ProtocolNames::get().fromType(upstream_protocol_type));
}

INSTANTIATE_TEST_SUITE_P(DownstreamUpstreamTypes, ThriftRouterPassthroughTest,
                         Combine(Values(TransportType::Framed, TransportType::Unframed),
                                 Values(ProtocolType::Binary, ProtocolType::Twitter),
                                 Values(TransportType::Framed, TransportType::Unframed),
                                 Values(ProtocolType::Binary, ProtocolType::Twitter)),
                         downstreamUpstreamTypesToString);

TEST_F(ThriftRouterTest, PoolRemoteConnectionFailure) {
  initializeRouter();

  startRequest(MessageType::Call);

  EXPECT_EQ(1UL, context_.cluster_manager_.thread_local_cluster_.cluster_.info_->statsScope()
                     .counterFromString("thrift.upstream_rq_call")
                     .value());

  EXPECT_CALL(callbacks_, sendLocalReply(_, _))
      .WillOnce(Invoke([&](const DirectResponse& response, bool end_stream) -> void {
        auto& app_ex = dynamic_cast<const AppException&>(response);
        EXPECT_EQ(AppExceptionType::InternalError, app_ex.type_);
        EXPECT_THAT(app_ex.what(), ContainsRegex(".*connection failure.*"));
        EXPECT_TRUE(end_stream);
      }));
  context_.cluster_manager_.thread_local_cluster_.tcp_conn_pool_.poolFailure(
      ConnectionPool::PoolFailureReason::RemoteConnectionFailure);
}

TEST_F(ThriftRouterTest, PoolLocalConnectionFailure) {
  initializeRouter();

  startRequest(MessageType::Call);

  EXPECT_EQ(1UL, context_.cluster_manager_.thread_local_cluster_.cluster_.info_->statsScope()
                     .counterFromString("thrift.upstream_rq_call")
                     .value());

  context_.cluster_manager_.thread_local_cluster_.tcp_conn_pool_.poolFailure(
      ConnectionPool::PoolFailureReason::LocalConnectionFailure);
}

TEST_F(ThriftRouterTest, PoolTimeout) {
  initializeRouter();

  startRequest(MessageType::Call);

  EXPECT_EQ(1UL, context_.cluster_manager_.thread_local_cluster_.cluster_.info_->statsScope()
                     .counterFromString("thrift.upstream_rq_call")
                     .value());

  EXPECT_CALL(callbacks_, sendLocalReply(_, _))
      .WillOnce(Invoke([&](const DirectResponse& response, bool end_stream) -> void {
        auto& app_ex = dynamic_cast<const AppException&>(response);
        EXPECT_EQ(AppExceptionType::InternalError, app_ex.type_);
        EXPECT_THAT(app_ex.what(), ContainsRegex(".*connection failure.*"));
        EXPECT_TRUE(end_stream);
      }));
  context_.cluster_manager_.thread_local_cluster_.tcp_conn_pool_.poolFailure(
      ConnectionPool::PoolFailureReason::Timeout);
}

TEST_F(ThriftRouterTest, PoolOverflowFailure) {
  initializeRouter();

  startRequest(MessageType::Call);

  EXPECT_EQ(1UL, context_.cluster_manager_.thread_local_cluster_.cluster_.info_->statsScope()
                     .counterFromString("thrift.upstream_rq_call")
                     .value());

  EXPECT_CALL(callbacks_, sendLocalReply(_, _))
      .WillOnce(Invoke([&](const DirectResponse& response, bool end_stream) -> void {
        auto& app_ex = dynamic_cast<const AppException&>(response);
        EXPECT_EQ(AppExceptionType::InternalError, app_ex.type_);
        EXPECT_THAT(app_ex.what(), ContainsRegex(".*too many connections.*"));
        EXPECT_TRUE(end_stream);
      }));
  context_.cluster_manager_.thread_local_cluster_.tcp_conn_pool_.poolFailure(
      ConnectionPool::PoolFailureReason::Overflow, true);
}

TEST_F(ThriftRouterTest, PoolConnectionFailureWithOnewayMessage) {
  initializeRouter();
  startRequest(MessageType::Oneway);

  EXPECT_EQ(1UL, context_.cluster_manager_.thread_local_cluster_.cluster_.info_->statsScope()
                     .counterFromString("thrift.upstream_rq_oneway")
                     .value());

  EXPECT_CALL(callbacks_, sendLocalReply(_, _)).Times(0);
  EXPECT_CALL(callbacks_, resetDownstreamConnection());
  context_.cluster_manager_.thread_local_cluster_.tcp_conn_pool_.poolFailure(
      ConnectionPool::PoolFailureReason::RemoteConnectionFailure);

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
  EXPECT_EQ(1U, context_.scope().counterFromString("test.route_missing").value());
}

TEST_F(ThriftRouterTest, NoCluster) {
  initializeRouter();
  initializeMetadata(MessageType::Call);

  EXPECT_CALL(callbacks_, route()).WillOnce(Return(route_ptr_));
  EXPECT_CALL(*route_, routeEntry()).WillOnce(Return(&route_entry_));
  EXPECT_CALL(route_entry_, clusterName()).WillRepeatedly(ReturnRef(cluster_name_));
  EXPECT_CALL(context_.cluster_manager_, getThreadLocalCluster(Eq(cluster_name_)))
      .WillOnce(Return(nullptr));
  EXPECT_CALL(callbacks_, sendLocalReply(_, _))
      .WillOnce(Invoke([&](const DirectResponse& response, bool end_stream) -> void {
        auto& app_ex = dynamic_cast<const AppException&>(response);
        EXPECT_EQ(AppExceptionType::InternalError, app_ex.type_);
        EXPECT_THAT(app_ex.what(), ContainsRegex(".*unknown cluster.*"));
        EXPECT_TRUE(end_stream);
      }));
  EXPECT_EQ(FilterStatus::StopIteration, router_->messageBegin(metadata_));
  EXPECT_EQ(1U, context_.scope().counterFromString("test.unknown_cluster").value());
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
  EXPECT_EQ(1U, context_.scope().counterFromString("test.upstream_rq_maintenance_mode").value());
  EXPECT_EQ(1UL, context_.cluster_manager_.thread_local_cluster_.cluster_.info_->statsScope()
                     .counterFromString("thrift.upstream_rq_call")
                     .value());
}

TEST_F(ThriftRouterTest, NoHealthyHosts) {
  initializeRouter();
  initializeMetadata(MessageType::Call);

  EXPECT_CALL(callbacks_, route()).WillOnce(Return(route_ptr_));
  EXPECT_CALL(*route_, routeEntry()).WillOnce(Return(&route_entry_));
  EXPECT_CALL(route_entry_, clusterName()).WillRepeatedly(ReturnRef(cluster_name_));
  EXPECT_CALL(context_.cluster_manager_.thread_local_cluster_, tcpConnPool(_, _))
      .WillOnce(Return(nullptr));

  EXPECT_CALL(callbacks_, sendLocalReply(_, _))
      .WillOnce(Invoke([&](const DirectResponse& response, bool end_stream) -> void {
        auto& app_ex = dynamic_cast<const AppException&>(response);
        EXPECT_EQ(AppExceptionType::InternalError, app_ex.type_);
        EXPECT_THAT(app_ex.what(), ContainsRegex(".*no healthy upstream.*"));
        EXPECT_TRUE(end_stream);
      }));

  EXPECT_EQ(FilterStatus::StopIteration, router_->messageBegin(metadata_));
  EXPECT_EQ(1U, context_.scope().counterFromString("test.no_healthy_upstream").value());
  EXPECT_EQ(1UL, context_.cluster_manager_.thread_local_cluster_.cluster_.info_->statsScope()
                     .counterFromString("thrift.upstream_rq_call")
                     .value());
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
  EXPECT_CALL(context_.cluster_manager_.thread_local_cluster_.tcp_conn_pool_,
              released(Ref(upstream_connection_)));
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

// Regression test for https://github.com/envoyproxy/envoy/issues/9037.
TEST_F(ThriftRouterTest, DontCloseConnectionTwice) {
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

  // Connection close shouldn't happen in onDestroy(), since it's been handled.
  EXPECT_CALL(upstream_connection_, close(Network::ConnectionCloseType::NoFlush)).Times(0);
  destroyRouter();
}

TEST_F(ThriftRouterTest, UnexpectedRouterDestroyBeforeUpstreamConnect) {
  initializeRouter();
  startRequest(MessageType::Call);

  EXPECT_EQ(1, context_.cluster_manager_.thread_local_cluster_.tcp_conn_pool_.handles_.size());
  EXPECT_CALL(context_.cluster_manager_.thread_local_cluster_.tcp_conn_pool_.handles_.front(),
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

  EXPECT_CALL(*context_.cluster_manager_.thread_local_cluster_.tcp_conn_pool_.connection_data_,
              addUpstreamCallbacks(_))
      .WillOnce(Invoke(
          [&](Tcp::ConnectionPool::UpstreamCallbacks& cb) -> void { upstream_callbacks_ = &cb; }));

  conn_state_.reset();
  EXPECT_CALL(*context_.cluster_manager_.thread_local_cluster_.tcp_conn_pool_.connection_data_,
              connectionState())
      .WillRepeatedly(
          Invoke([&]() -> Tcp::ConnectionPool::ConnectionState* { return conn_state_.get(); }));
  EXPECT_CALL(*context_.cluster_manager_.thread_local_cluster_.tcp_conn_pool_.connection_data_,
              setConnectionState_(_))
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

  context_.cluster_manager_.thread_local_cluster_.tcp_conn_pool_.poolReady(upstream_connection_);
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

  EXPECT_EQ(1UL, context_.cluster_manager_.thread_local_cluster_.cluster_.info_->statsScope()
                     .counterFromString("thrift.upstream_rq_call")
                     .value());
  EXPECT_EQ(1UL, context_.cluster_manager_.thread_local_cluster_.cluster_.info_->statsScope()
                     .counterFromString("thrift.upstream_resp_reply")
                     .value());
  EXPECT_EQ(1UL, context_.cluster_manager_.thread_local_cluster_.cluster_.info_->statsScope()
                     .counterFromString("thrift.upstream_resp_success")
                     .value());
}

// Test the case where an upgrade will occur, but the conn pool
// returns immediately with a valid, but never, used connection.
TEST_F(ThriftRouterTest, ProtocolUpgradeOnExistingUnusedConnection) {
  initializeRouter();

  EXPECT_CALL(*context_.cluster_manager_.thread_local_cluster_.tcp_conn_pool_.connection_data_,
              addUpstreamCallbacks(_))
      .WillOnce(Invoke(
          [&](Tcp::ConnectionPool::UpstreamCallbacks& cb) -> void { upstream_callbacks_ = &cb; }));

  conn_state_.reset();
  EXPECT_CALL(*context_.cluster_manager_.thread_local_cluster_.tcp_conn_pool_.connection_data_,
              connectionState())
      .WillRepeatedly(
          Invoke([&]() -> Tcp::ConnectionPool::ConnectionState* { return conn_state_.get(); }));
  EXPECT_CALL(*context_.cluster_manager_.thread_local_cluster_.tcp_conn_pool_.connection_data_,
              setConnectionState_(_))
      .WillOnce(Invoke(
          [&](Tcp::ConnectionPool::ConnectionStatePtr& cs) -> void { conn_state_.swap(cs); }));

  MockThriftObject* upgrade_response = new NiceMock<MockThriftObject>();

  EXPECT_CALL(upstream_connection_, write(_, false))
      .WillOnce(Invoke([&](Buffer::Instance& buffer, bool) -> void {
        EXPECT_EQ("upgrade request", buffer.toString());
      }));

  // Simulate an existing connection that's never been used.
  EXPECT_CALL(context_.cluster_manager_.thread_local_cluster_.tcp_conn_pool_, newConnection(_))
      .WillOnce(
          Invoke([&](Tcp::ConnectionPool::Callbacks& cb) -> Tcp::ConnectionPool::Cancellable* {
            context_.cluster_manager_.thread_local_cluster_.tcp_conn_pool_.newConnectionImpl(cb);

            EXPECT_CALL(*protocol_, supportsUpgrade()).WillOnce(Return(true));

            EXPECT_CALL(*protocol_, attemptUpgrade(_, _, _))
                .WillOnce(Invoke([&](Transport&, ThriftConnectionState&,
                                     Buffer::Instance& buffer) -> ThriftObjectPtr {
                  buffer.add("upgrade request");
                  return ThriftObjectPtr{upgrade_response};
                }));

            context_.cluster_manager_.thread_local_cluster_.tcp_conn_pool_.poolReady(
                upstream_connection_);
            return nullptr;
          }));

  startRequest(MessageType::Call);

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

  EXPECT_EQ(1UL, context_.cluster_manager_.thread_local_cluster_.cluster_.info_->statsScope()
                     .counterFromString("thrift.upstream_rq_call")
                     .value());
  EXPECT_EQ(1UL, context_.cluster_manager_.thread_local_cluster_.cluster_.info_->statsScope()
                     .counterFromString("thrift.upstream_resp_reply")
                     .value());
  EXPECT_EQ(1UL, context_.cluster_manager_.thread_local_cluster_.cluster_.info_->statsScope()
                     .counterFromString("thrift.upstream_resp_success")
                     .value());
}

TEST_F(ThriftRouterTest, ProtocolUpgradeSkippedOnExistingConnection) {
  initializeRouter();
  startRequest(MessageType::Call);

  EXPECT_CALL(*context_.cluster_manager_.thread_local_cluster_.tcp_conn_pool_.connection_data_,
              addUpstreamCallbacks(_))
      .WillOnce(Invoke(
          [&](Tcp::ConnectionPool::UpstreamCallbacks& cb) -> void { upstream_callbacks_ = &cb; }));

  conn_state_ = std::make_unique<ThriftConnectionState>();
  EXPECT_CALL(*context_.cluster_manager_.thread_local_cluster_.tcp_conn_pool_.connection_data_,
              connectionState())
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

  context_.cluster_manager_.thread_local_cluster_.tcp_conn_pool_.poolReady(upstream_connection_);
  EXPECT_NE(nullptr, upstream_callbacks_);

  // Then the actual request...
  sendTrivialStruct(FieldType::String);
  completeRequest();
  returnResponse();
  destroyRouter();

  EXPECT_EQ(1UL, context_.cluster_manager_.thread_local_cluster_.cluster_.info_->statsScope()
                     .counterFromString("thrift.upstream_rq_call")
                     .value());
  EXPECT_EQ(1UL, context_.cluster_manager_.thread_local_cluster_.cluster_.info_->statsScope()
                     .counterFromString("thrift.upstream_resp_reply")
                     .value());
  EXPECT_EQ(1UL, context_.cluster_manager_.thread_local_cluster_.cluster_.info_->statsScope()
                     .counterFromString("thrift.upstream_resp_success")
                     .value());
}

TEST_F(ThriftRouterTest, PoolTimeoutUpstreamTimeMeasurement) {
  initializeRouter();

  Stats::MockStore cluster_scope;
  ON_CALL(*context_.cluster_manager_.thread_local_cluster_.cluster_.info_, statsScope())
      .WillByDefault(ReturnRef(cluster_scope));
  EXPECT_CALL(cluster_scope, counter("thrift.upstream_rq_call"));

  startRequest(MessageType::Call);

  dispatcher_.time_system_.advanceTimeWait(std::chrono::milliseconds(500));
  EXPECT_CALL(cluster_scope,
              histogram("thrift.upstream_rq_time", Stats::Histogram::Unit::Milliseconds));
  EXPECT_CALL(cluster_scope,
              deliverHistogramToSinks(
                  testing::Property(&Stats::Metric::name, "thrift.upstream_rq_time"), 500));
  EXPECT_CALL(callbacks_, sendLocalReply(_, _))
      .WillOnce(Invoke([&](const DirectResponse& response, bool end_stream) -> void {
        auto& app_ex = dynamic_cast<const AppException&>(response);
        EXPECT_EQ(AppExceptionType::InternalError, app_ex.type_);
        EXPECT_THAT(app_ex.what(), ContainsRegex(".*connection failure.*"));
        EXPECT_TRUE(end_stream);
      }));
  context_.cluster_manager_.thread_local_cluster_.tcp_conn_pool_.poolFailure(
      ConnectionPool::PoolFailureReason::Timeout);
}

TEST_P(ThriftRouterFieldTypeTest, OneWay) {
  FieldType field_type = GetParam();

  initializeRouter();
  startRequest(MessageType::Oneway);
  connectUpstream();
  sendTrivialStruct(field_type);
  completeRequest();
  destroyRouter();

  EXPECT_EQ(1UL, context_.cluster_manager_.thread_local_cluster_.cluster_.info_->statsScope()
                     .counterFromString("thrift.upstream_rq_oneway")
                     .value());
  EXPECT_EQ(0UL, context_.cluster_manager_.thread_local_cluster_.cluster_.info_->statsScope()
                     .counterFromString("thrift.upstream_resp_reply")
                     .value());
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

  EXPECT_EQ(1UL, context_.cluster_manager_.thread_local_cluster_.cluster_.info_->statsScope()
                     .counterFromString("thrift.upstream_rq_call")
                     .value());
  EXPECT_EQ(1UL, context_.cluster_manager_.thread_local_cluster_.cluster_.info_->statsScope()
                     .counterFromString("thrift.upstream_resp_reply")
                     .value());
  EXPECT_EQ(1UL, context_.cluster_manager_.thread_local_cluster_.cluster_.info_->statsScope()
                     .counterFromString("thrift.upstream_resp_success")
                     .value());
}

TEST_P(ThriftRouterFieldTypeTest, CallWithUpstreamRqTime) {
  FieldType field_type = GetParam();

  initializeRouter();

  Stats::MockStore cluster_scope;
  ON_CALL(*context_.cluster_manager_.thread_local_cluster_.cluster_.info_, statsScope())
      .WillByDefault(ReturnRef(cluster_scope));
  EXPECT_CALL(cluster_scope, counter("thrift.upstream_rq_call"));
  EXPECT_CALL(cluster_scope, counter("thrift.upstream_resp_reply"));
  EXPECT_CALL(cluster_scope, counter("thrift.upstream_resp_success"));

  startRequest(MessageType::Call);
  connectUpstream();
  sendTrivialStruct(field_type);
  completeRequest();

  dispatcher_.time_system_.advanceTimeWait(std::chrono::milliseconds(500));
  EXPECT_CALL(cluster_scope,
              histogram("thrift.upstream_rq_time", Stats::Histogram::Unit::Milliseconds));
  EXPECT_CALL(cluster_scope,
              deliverHistogramToSinks(
                  testing::Property(&Stats::Metric::name, "thrift.upstream_rq_time"), 500));
  returnResponse();
  destroyRouter();
}

TEST_P(ThriftRouterFieldTypeTest, Call_Error) {
  FieldType field_type = GetParam();

  initializeRouter();
  startRequest(MessageType::Call);
  connectUpstream();
  sendTrivialStruct(field_type);
  completeRequest();
  returnResponse(MessageType::Reply, false);
  destroyRouter();

  EXPECT_EQ(1UL, context_.cluster_manager_.thread_local_cluster_.cluster_.info_->statsScope()
                     .counterFromString("thrift.upstream_rq_call")
                     .value());
  EXPECT_EQ(1UL, context_.cluster_manager_.thread_local_cluster_.cluster_.info_->statsScope()
                     .counterFromString("thrift.upstream_resp_reply")
                     .value());
  EXPECT_EQ(0UL, context_.cluster_manager_.thread_local_cluster_.cluster_.info_->statsScope()
                     .counterFromString("thrift.upstream_resp_success")
                     .value());
  EXPECT_EQ(1UL, context_.cluster_manager_.thread_local_cluster_.cluster_.info_->statsScope()
                     .counterFromString("thrift.upstream_resp_error")
                     .value());
}

TEST_P(ThriftRouterFieldTypeTest, Exception) {
  FieldType field_type = GetParam();

  initializeRouter();
  startRequest(MessageType::Call);
  connectUpstream();
  sendTrivialStruct(field_type);
  completeRequest();
  returnResponse(MessageType::Exception);
  destroyRouter();

  EXPECT_EQ(1UL, context_.cluster_manager_.thread_local_cluster_.cluster_.info_->statsScope()
                     .counterFromString("thrift.upstream_rq_call")
                     .value());
  EXPECT_EQ(1UL, context_.cluster_manager_.thread_local_cluster_.cluster_.info_->statsScope()
                     .counterFromString("thrift.upstream_resp_exception")
                     .value());
}

TEST_P(ThriftRouterFieldTypeTest, UnknownMessageTypes) {
  FieldType field_type = GetParam();

  initializeRouter();
  startRequest(MessageType::Exception);
  connectUpstream();
  sendTrivialStruct(field_type);
  completeRequest();
  returnResponse(MessageType::Call);
  destroyRouter();

  EXPECT_EQ(1UL, context_.cluster_manager_.thread_local_cluster_.cluster_.info_->statsScope()
                     .counterFromString("thrift.upstream_rq_invalid_type")
                     .value());
  EXPECT_EQ(1UL, context_.cluster_manager_.thread_local_cluster_.cluster_.info_->statsScope()
                     .counterFromString("thrift.upstream_resp_invalid_type")
                     .value());
}

// Ensure the service name gets stripped when strip_service_name = true.
TEST_P(ThriftRouterFieldTypeTest, StripServiceNameEnabled) {
  FieldType field_type = GetParam();

  initializeRouter();
  startRequest(MessageType::Call, "Service:method", true);
  connectUpstream();
  sendTrivialStruct(field_type);
  completeRequest();

  EXPECT_EQ("method", metadata_->methodName());

  returnResponse();
  destroyRouter();

  EXPECT_EQ(1UL, context_.cluster_manager_.thread_local_cluster_.cluster_.info_->statsScope()
                     .counterFromString("thrift.upstream_rq_call")
                     .value());
  EXPECT_EQ(1UL, context_.cluster_manager_.thread_local_cluster_.cluster_.info_->statsScope()
                     .counterFromString("thrift.upstream_resp_reply")
                     .value());
  EXPECT_EQ(1UL, context_.cluster_manager_.thread_local_cluster_.cluster_.info_->statsScope()
                     .counterFromString("thrift.upstream_resp_success")
                     .value());
}

// Ensure the service name prefix isn't stripped when strip_service_name = false.
TEST_P(ThriftRouterFieldTypeTest, StripServiceNameDisabled) {
  FieldType field_type = GetParam();

  initializeRouter();
  startRequest(MessageType::Call, "Service:method", false);
  connectUpstream();
  sendTrivialStruct(field_type);
  completeRequest();

  EXPECT_EQ("Service:method", metadata_->methodName());

  returnResponse();
  destroyRouter();

  EXPECT_EQ(1UL, context_.cluster_manager_.thread_local_cluster_.cluster_.info_->statsScope()
                     .counterFromString("thrift.upstream_rq_call")
                     .value());
  EXPECT_EQ(1UL, context_.cluster_manager_.thread_local_cluster_.cluster_.info_->statsScope()
                     .counterFromString("thrift.upstream_resp_reply")
                     .value());
  EXPECT_EQ(1UL, context_.cluster_manager_.thread_local_cluster_.cluster_.info_->statsScope()
                     .counterFromString("thrift.upstream_resp_success")
                     .value());
}

TEST_F(ThriftRouterTest, CallWithExistingConnection) {
  initializeRouter();

  // Simulate previous sequence id usage.
  conn_state_ = std::make_unique<ThriftConnectionState>(3);

  startRequestWithExistingConnection(MessageType::Call);
  sendTrivialStruct(FieldType::I32);
  completeRequest();

  EXPECT_EQ(3, metadata_->sequenceId());

  returnResponse();
  destroyRouter();

  EXPECT_EQ(1UL, context_.cluster_manager_.thread_local_cluster_.cluster_.info_->statsScope()
                     .counterFromString("thrift.upstream_rq_call")
                     .value());
  EXPECT_EQ(1UL, context_.cluster_manager_.thread_local_cluster_.cluster_.info_->statsScope()
                     .counterFromString("thrift.upstream_resp_reply")
                     .value());
  EXPECT_EQ(1UL, context_.cluster_manager_.thread_local_cluster_.cluster_.info_->statsScope()
                     .counterFromString("thrift.upstream_resp_success")
                     .value());
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

TEST_P(ThriftRouterPassthroughTest, PassthroughEnable) {
  TransportType downstream_transport_type;
  ProtocolType downstream_protocol_type;
  TransportType upstream_transport_type;
  ProtocolType upstream_protocol_type;

  std::tie(downstream_transport_type, downstream_protocol_type, upstream_transport_type,
           upstream_protocol_type) = GetParam();

  const std::string yaml_string = R"EOF(
  transport: {}
  protocol: {}
  )EOF";

  envoy::extensions::filters::network::thrift_proxy::v3::ThriftProtocolOptions configuration;
  TestUtility::loadFromYaml(fmt::format(yaml_string,
                                        TransportNames::get().fromType(upstream_transport_type),
                                        ProtocolNames::get().fromType(upstream_protocol_type)),
                            configuration);

  const auto protocol_option = std::make_shared<ProtocolOptionsConfigImpl>(configuration);
  EXPECT_CALL(*context_.cluster_manager_.thread_local_cluster_.cluster_.info_,
              extensionProtocolOptions(_))
      .WillRepeatedly(Return(protocol_option));

  initializeRouter();
  startRequest(MessageType::Call, "method", false, downstream_transport_type,
               downstream_protocol_type);

  bool passthroughSupported = false;
  if (downstream_transport_type == upstream_transport_type &&
      downstream_transport_type == TransportType::Framed &&
      downstream_protocol_type == upstream_protocol_type &&
      downstream_protocol_type != ProtocolType::Twitter) {
    passthroughSupported = true;
  }
  ASSERT_EQ(passthroughSupported, router_->passthroughSupported());

  EXPECT_CALL(callbacks_, sendLocalReply(_, _))
      .WillOnce(Invoke([&](const DirectResponse& response, bool end_stream) -> void {
        auto& app_ex = dynamic_cast<const AppException&>(response);
        EXPECT_EQ(AppExceptionType::InternalError, app_ex.type_);
        EXPECT_THAT(app_ex.what(), ContainsRegex(".*connection failure.*"));
        EXPECT_TRUE(end_stream);
      }));
  context_.cluster_manager_.thread_local_cluster_.tcp_conn_pool_.poolFailure(
      ConnectionPool::PoolFailureReason::RemoteConnectionFailure);
}

} // namespace Router
} // namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
