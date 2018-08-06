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

using testing::ContainsRegex;
using testing::Invoke;
using testing::NiceMock;
using testing::Ref;
using testing::Return;
using testing::ReturnRef;
using testing::Test;
using testing::TestWithParam;
using testing::Values;
using testing::_;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ThriftProxy {
namespace Router {

namespace {

envoy::config::filter::network::thrift_proxy::v2alpha1::RouteConfiguration
parseRouteConfigurationFromV2Yaml(const std::string& yaml) {
  envoy::config::filter::network::thrift_proxy::v2alpha1::RouteConfiguration route_config;
  MessageUtil::loadFromYaml(yaml, route_config);
  MessageUtil::validate(route_config);
  return route_config;
}

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
      : transport_factory_([&]() -> MockTransport* { return transport_; }),
        protocol_factory_([&]() -> MockProtocol* { return protocol_; }),
        transport_register_(transport_factory_), protocol_register_(protocol_factory_) {}

  void initializeRouter() {
    route_ = new NiceMock<MockRoute>();
    route_ptr_.reset(route_);

    host_ = new NiceMock<Upstream::MockHostDescription>();
    host_ptr_.reset(host_);

    router_.reset(new Router(context_.clusterManager()));

    EXPECT_EQ(nullptr, router_->downstreamConnection());

    router_->setDecoderFilterCallbacks(callbacks_);
  }

  void startRequest(MessageType msg_type) {
    msg_type_ = msg_type;

    EXPECT_EQ(ThriftFilters::FilterStatus::Continue, router_->transportBegin({}));

    EXPECT_CALL(callbacks_, route()).WillOnce(Return(route_ptr_));
    EXPECT_CALL(*route_, routeEntry()).WillOnce(Return(&route_entry_));
    EXPECT_CALL(route_entry_, clusterName()).WillRepeatedly(ReturnRef(cluster_name_));

    EXPECT_CALL(context_.cluster_manager_.tcp_conn_pool_, newConnection(_))
        .WillOnce(
            Invoke([&](Tcp::ConnectionPool::Callbacks& cb) -> Tcp::ConnectionPool::Cancellable* {
              conn_pool_callbacks_ = &cb;
              return &handle_;
            }));

    EXPECT_EQ(ThriftFilters::FilterStatus::StopIteration,
              router_->messageBegin(method_name_, msg_type_, seq_id_));
    EXPECT_NE(nullptr, conn_pool_callbacks_);

    NiceMock<Network::MockClientConnection> connection;
    EXPECT_CALL(callbacks_, connection()).WillRepeatedly(Return(&connection));
    EXPECT_EQ(&connection, router_->downstreamConnection());

    // Not yet implemented:
    EXPECT_EQ(absl::optional<uint64_t>(), router_->computeHashKey());
    EXPECT_EQ(nullptr, router_->metadataMatchCriteria());
    EXPECT_EQ(nullptr, router_->downstreamHeaders());
  }

  void connectUpstream() {
    EXPECT_CALL(conn_data_, addUpstreamCallbacks(_))
        .WillOnce(Invoke([&](Tcp::ConnectionPool::UpstreamCallbacks& cb) -> void {
          upstream_callbacks_ = &cb;
        }));

    EXPECT_CALL(callbacks_, downstreamTransportType()).WillOnce(Return(TransportType::Framed));
    transport_ = new NiceMock<MockTransport>();
    ON_CALL(*transport_, type()).WillByDefault(Return(TransportType::Framed));

    EXPECT_CALL(callbacks_, downstreamProtocolType()).WillOnce(Return(ProtocolType::Binary));
    protocol_ = new NiceMock<MockProtocol>();
    ON_CALL(*protocol_, type()).WillByDefault(Return(ProtocolType::Binary));
    EXPECT_CALL(*protocol_, writeMessageBegin(_, method_name_, msg_type_, seq_id_));

    EXPECT_CALL(callbacks_, continueDecoding());
    conn_pool_callbacks_->onPoolReady(conn_data_, host_ptr_);
    EXPECT_NE(nullptr, upstream_callbacks_);
  }

  void sendTrivialStruct(FieldType field_type) {
    EXPECT_CALL(*protocol_, writeStructBegin(_, ""));
    EXPECT_EQ(ThriftFilters::FilterStatus::Continue, router_->structBegin({}));

    EXPECT_CALL(*protocol_, writeFieldBegin(_, "", field_type, 1));
    EXPECT_EQ(ThriftFilters::FilterStatus::Continue, router_->fieldBegin({}, field_type, 1));

    sendTrivialValue(field_type);

    EXPECT_CALL(*protocol_, writeFieldEnd(_));
    EXPECT_EQ(ThriftFilters::FilterStatus::Continue, router_->fieldEnd());

    EXPECT_CALL(*protocol_, writeFieldBegin(_, "", FieldType::Stop, 0));
    EXPECT_CALL(*protocol_, writeStructEnd(_));
    EXPECT_EQ(ThriftFilters::FilterStatus::Continue, router_->structEnd());
  }

  void sendTrivialValue(FieldType field_type) {
    switch (field_type) {
    case FieldType::Bool:
      EXPECT_CALL(*protocol_, writeBool(_, true));
      EXPECT_EQ(ThriftFilters::FilterStatus::Continue, router_->boolValue(true));
      break;
    case FieldType::Byte:
      EXPECT_CALL(*protocol_, writeByte(_, 2));
      EXPECT_EQ(ThriftFilters::FilterStatus::Continue, router_->byteValue(2));
      break;
    case FieldType::I16:
      EXPECT_CALL(*protocol_, writeInt16(_, 3));
      EXPECT_EQ(ThriftFilters::FilterStatus::Continue, router_->int16Value(3));
      break;
    case FieldType::I32:
      EXPECT_CALL(*protocol_, writeInt32(_, 4));
      EXPECT_EQ(ThriftFilters::FilterStatus::Continue, router_->int32Value(4));
      break;
    case FieldType::I64:
      EXPECT_CALL(*protocol_, writeInt64(_, 5));
      EXPECT_EQ(ThriftFilters::FilterStatus::Continue, router_->int64Value(5));
      break;
    case FieldType::Double:
      EXPECT_CALL(*protocol_, writeDouble(_, 6.0));
      EXPECT_EQ(ThriftFilters::FilterStatus::Continue, router_->doubleValue(6.0));
      break;
    case FieldType::String:
      EXPECT_CALL(*protocol_, writeString(_, "seven"));
      EXPECT_EQ(ThriftFilters::FilterStatus::Continue, router_->stringValue("seven"));
      break;
    default:
      NOT_REACHED_GCOVR_EXCL_LINE;
    }
  }

  void completeRequest() {
    EXPECT_CALL(*protocol_, writeMessageEnd(_));
    EXPECT_CALL(*transport_, encodeFrame(_, _));
    EXPECT_CALL(conn_data_.connection_, write(_, false));

    if (msg_type_ == MessageType::Oneway) {
      EXPECT_CALL(conn_data_, release());
    }

    EXPECT_EQ(ThriftFilters::FilterStatus::Continue, router_->messageEnd());
    EXPECT_EQ(ThriftFilters::FilterStatus::Continue, router_->transportEnd());
  }

  void returnResponse() {
    Buffer::OwnedImpl buffer;

    EXPECT_CALL(callbacks_, startUpstreamResponse(TransportType::Framed, ProtocolType::Binary));

    EXPECT_CALL(callbacks_, upstreamData(Ref(buffer))).WillOnce(Return(false));
    upstream_callbacks_->onUpstreamData(buffer, false);

    EXPECT_CALL(callbacks_, upstreamData(Ref(buffer))).WillOnce(Return(true));
    EXPECT_CALL(conn_data_, release());
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

  NiceMock<Server::Configuration::MockFactoryContext> context_;
  NiceMock<ThriftFilters::MockDecoderFilterCallbacks> callbacks_;
  NiceMock<MockTransport>* transport_{};
  NiceMock<MockProtocol>* protocol_{};
  NiceMock<MockRoute>* route_{};
  NiceMock<MockRouteEntry> route_entry_;
  NiceMock<Upstream::MockHostDescription>* host_{};

  RouteConstSharedPtr route_ptr_;
  Upstream::HostDescriptionConstSharedPtr host_ptr_;

  std::unique_ptr<Router> router_;

  std::string cluster_name_{"cluster"};

  std::string method_name_{"method"};
  MessageType msg_type_{MessageType::Call};
  int32_t seq_id_{1};

  NiceMock<Tcp::ConnectionPool::MockCancellable> handle_;
  NiceMock<Tcp::ConnectionPool::MockConnectionData> conn_data_;
  Tcp::ConnectionPool::Callbacks* conn_pool_callbacks_{};
  Tcp::ConnectionPool::UpstreamCallbacks* upstream_callbacks_{};
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

  EXPECT_CALL(callbacks_, sendLocalReply_(_))
      .WillOnce(Invoke([&](ThriftFilters::DirectResponsePtr& response) -> void {
        auto* app_ex = dynamic_cast<AppException*>(response.get());
        EXPECT_NE(nullptr, app_ex);
        EXPECT_EQ(method_name_, app_ex->method_name_);
        EXPECT_EQ(seq_id_, app_ex->seq_id_);
        EXPECT_EQ(AppExceptionType::InternalError, app_ex->type_);
        EXPECT_THAT(app_ex->error_message_, ContainsRegex(".*connection failure.*"));
      }));
  conn_pool_callbacks_->onPoolFailure(
      Tcp::ConnectionPool::PoolFailureReason::RemoteConnectionFailure, host_ptr_);
}

TEST_F(ThriftRouterTest, PoolLocalConnectionFailure) {
  initializeRouter();

  startRequest(MessageType::Call);

  EXPECT_CALL(callbacks_, sendLocalReply_(_))
      .WillOnce(Invoke([&](ThriftFilters::DirectResponsePtr& response) -> void {
        auto* app_ex = dynamic_cast<AppException*>(response.get());
        EXPECT_NE(nullptr, app_ex);
        EXPECT_EQ(method_name_, app_ex->method_name_);
        EXPECT_EQ(seq_id_, app_ex->seq_id_);
        EXPECT_EQ(AppExceptionType::InternalError, app_ex->type_);
        EXPECT_THAT(app_ex->error_message_, ContainsRegex(".*connection failure.*"));
      }));
  conn_pool_callbacks_->onPoolFailure(
      Tcp::ConnectionPool::PoolFailureReason::LocalConnectionFailure, host_ptr_);
}

TEST_F(ThriftRouterTest, PoolTimeout) {
  initializeRouter();

  startRequest(MessageType::Call);

  EXPECT_CALL(callbacks_, sendLocalReply_(_))
      .WillOnce(Invoke([&](ThriftFilters::DirectResponsePtr& response) -> void {
        auto* app_ex = dynamic_cast<AppException*>(response.get());
        EXPECT_NE(nullptr, app_ex);
        EXPECT_EQ(method_name_, app_ex->method_name_);
        EXPECT_EQ(seq_id_, app_ex->seq_id_);
        EXPECT_EQ(AppExceptionType::InternalError, app_ex->type_);
        EXPECT_THAT(app_ex->error_message_, ContainsRegex(".*connection failure.*"));
      }));
  conn_pool_callbacks_->onPoolFailure(Tcp::ConnectionPool::PoolFailureReason::Timeout, host_ptr_);
}

TEST_F(ThriftRouterTest, PoolOverflowFailure) {
  initializeRouter();

  startRequest(MessageType::Call);

  EXPECT_CALL(callbacks_, sendLocalReply_(_))
      .WillOnce(Invoke([&](ThriftFilters::DirectResponsePtr& response) -> void {
        auto* app_ex = dynamic_cast<AppException*>(response.get());
        EXPECT_NE(nullptr, app_ex);
        EXPECT_EQ(method_name_, app_ex->method_name_);
        EXPECT_EQ(seq_id_, app_ex->seq_id_);
        EXPECT_EQ(AppExceptionType::InternalError, app_ex->type_);
        EXPECT_THAT(app_ex->error_message_, ContainsRegex(".*too many connections.*"));
      }));
  conn_pool_callbacks_->onPoolFailure(Tcp::ConnectionPool::PoolFailureReason::Overflow, host_ptr_);
}

TEST_F(ThriftRouterTest, NoRoute) {
  initializeRouter();

  EXPECT_CALL(callbacks_, route()).WillOnce(Return(nullptr));
  EXPECT_CALL(callbacks_, sendLocalReply_(_))
      .WillOnce(Invoke([&](ThriftFilters::DirectResponsePtr& response) -> void {
        auto* app_ex = dynamic_cast<AppException*>(response.get());
        EXPECT_NE(nullptr, app_ex);
        if (app_ex != nullptr) {
          EXPECT_EQ(method_name_, app_ex->method_name_);
          EXPECT_EQ(seq_id_, app_ex->seq_id_);
          EXPECT_EQ(AppExceptionType::UnknownMethod, app_ex->type_);
          EXPECT_THAT(app_ex->error_message_, ContainsRegex(".*no route.*"));
        }
      }));
  EXPECT_EQ(ThriftFilters::FilterStatus::StopIteration,
            router_->messageBegin(method_name_, MessageType::Call, seq_id_));
}

TEST_F(ThriftRouterTest, NoCluster) {
  initializeRouter();

  EXPECT_CALL(callbacks_, route()).WillOnce(Return(route_ptr_));
  EXPECT_CALL(*route_, routeEntry()).WillOnce(Return(&route_entry_));
  EXPECT_CALL(route_entry_, clusterName()).WillRepeatedly(ReturnRef(cluster_name_));
  EXPECT_CALL(context_.cluster_manager_, get(cluster_name_)).WillOnce(Return(nullptr));
  EXPECT_CALL(callbacks_, sendLocalReply_(_))
      .WillOnce(Invoke([&](ThriftFilters::DirectResponsePtr& response) -> void {
        auto* app_ex = dynamic_cast<AppException*>(response.get());
        EXPECT_NE(nullptr, app_ex);
        EXPECT_EQ(method_name_, app_ex->method_name_);
        EXPECT_EQ(seq_id_, app_ex->seq_id_);
        EXPECT_EQ(AppExceptionType::InternalError, app_ex->type_);
        EXPECT_THAT(app_ex->error_message_, ContainsRegex(".*unknown cluster.*"));
      }));
  EXPECT_EQ(ThriftFilters::FilterStatus::StopIteration,
            router_->messageBegin(method_name_, MessageType::Call, seq_id_));
}

TEST_F(ThriftRouterTest, ClusterMaintenanceMode) {
  initializeRouter();

  EXPECT_CALL(callbacks_, route()).WillOnce(Return(route_ptr_));
  EXPECT_CALL(*route_, routeEntry()).WillOnce(Return(&route_entry_));
  EXPECT_CALL(route_entry_, clusterName()).WillRepeatedly(ReturnRef(cluster_name_));
  EXPECT_CALL(*context_.cluster_manager_.thread_local_cluster_.cluster_.info_, maintenanceMode())
      .WillOnce(Return(true));

  EXPECT_CALL(callbacks_, sendLocalReply_(_))
      .WillOnce(Invoke([&](ThriftFilters::DirectResponsePtr& response) -> void {
        auto* app_ex = dynamic_cast<AppException*>(response.get());
        EXPECT_NE(nullptr, app_ex);
        EXPECT_EQ(method_name_, app_ex->method_name_);
        EXPECT_EQ(seq_id_, app_ex->seq_id_);
        EXPECT_EQ(AppExceptionType::InternalError, app_ex->type_);
        EXPECT_THAT(app_ex->error_message_, ContainsRegex(".*maintenance mode.*"));
      }));
  EXPECT_EQ(ThriftFilters::FilterStatus::StopIteration,
            router_->messageBegin(method_name_, MessageType::Call, seq_id_));
}

TEST_F(ThriftRouterTest, NoHealthyHosts) {
  initializeRouter();

  EXPECT_CALL(callbacks_, route()).WillOnce(Return(route_ptr_));
  EXPECT_CALL(*route_, routeEntry()).WillOnce(Return(&route_entry_));
  EXPECT_CALL(route_entry_, clusterName()).WillRepeatedly(ReturnRef(cluster_name_));
  EXPECT_CALL(context_.cluster_manager_, tcpConnPoolForCluster(cluster_name_, _, _))
      .WillOnce(Return(nullptr));

  EXPECT_CALL(callbacks_, sendLocalReply_(_))
      .WillOnce(Invoke([&](ThriftFilters::DirectResponsePtr& response) -> void {
        auto* app_ex = dynamic_cast<AppException*>(response.get());
        EXPECT_NE(nullptr, app_ex);
        EXPECT_EQ(method_name_, app_ex->method_name_);
        EXPECT_EQ(seq_id_, app_ex->seq_id_);
        EXPECT_EQ(AppExceptionType::InternalError, app_ex->type_);
        EXPECT_THAT(app_ex->error_message_, ContainsRegex(".*no healthy upstream.*"));
      }));
  EXPECT_EQ(ThriftFilters::FilterStatus::StopIteration,
            router_->messageBegin(method_name_, MessageType::Call, seq_id_));
}

TEST_F(ThriftRouterTest, TruncatedResponse) {
  initializeRouter();
  startRequest(MessageType::Call);
  connectUpstream();
  sendTrivialStruct(FieldType::String);
  completeRequest();

  Buffer::OwnedImpl buffer;

  EXPECT_CALL(callbacks_, startUpstreamResponse(TransportType::Framed, ProtocolType::Binary));
  EXPECT_CALL(callbacks_, upstreamData(Ref(buffer))).WillOnce(Return(false));
  EXPECT_CALL(conn_data_, release());
  EXPECT_CALL(callbacks_, resetDownstreamConnection());

  upstream_callbacks_->onUpstreamData(buffer, true);
  destroyRouter();
}

TEST_F(ThriftRouterTest, UpstreamDataTriggersReset) {
  initializeRouter();
  startRequest(MessageType::Call);
  connectUpstream();
  sendTrivialStruct(FieldType::String);
  completeRequest();

  Buffer::OwnedImpl buffer;

  EXPECT_CALL(callbacks_, startUpstreamResponse(TransportType::Framed, ProtocolType::Binary));
  EXPECT_CALL(callbacks_, upstreamData(Ref(buffer)))
      .WillOnce(Invoke([&](Buffer::Instance&) -> bool {
        router_->resetUpstreamConnection();
        return true;
      }));
  EXPECT_CALL(conn_data_.connection_, close(Network::ConnectionCloseType::NoFlush));

  upstream_callbacks_->onUpstreamData(buffer, true);
  destroyRouter();
}

TEST_F(ThriftRouterTest, UnexpectedRouterDestroyBeforeUpstreamConnect) {
  initializeRouter();
  startRequest(MessageType::Call);
  destroyRouter();
}

TEST_F(ThriftRouterTest, UnexpectedRouterDestroy) {
  initializeRouter();
  startRequest(MessageType::Call);
  connectUpstream();
  EXPECT_CALL(conn_data_.connection_, close(Network::ConnectionCloseType::NoFlush));
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

TEST_P(ThriftRouterContainerTest, DecoderFilterCallbacks) {
  FieldType field_type = GetParam();

  initializeRouter();

  startRequest(MessageType::Oneway);
  connectUpstream();

  EXPECT_CALL(*protocol_, writeStructBegin(_, ""));
  EXPECT_EQ(ThriftFilters::FilterStatus::Continue, router_->structBegin({}));

  EXPECT_CALL(*protocol_, writeFieldBegin(_, "", field_type, 1));
  EXPECT_EQ(ThriftFilters::FilterStatus::Continue, router_->fieldBegin({}, field_type, 1));

  switch (field_type) {
  case FieldType::Map:
    EXPECT_CALL(*protocol_, writeMapBegin(_, FieldType::I32, FieldType::I32, 2));
    EXPECT_EQ(ThriftFilters::FilterStatus::Continue,
              router_->mapBegin(FieldType::I32, FieldType::I32, 2));
    for (int i = 0; i < 2; i++) {
      EXPECT_CALL(*protocol_, writeInt32(_, i));
      EXPECT_EQ(ThriftFilters::FilterStatus::Continue, router_->int32Value(i));
      EXPECT_CALL(*protocol_, writeInt32(_, i + 100));
      EXPECT_EQ(ThriftFilters::FilterStatus::Continue, router_->int32Value(i + 100));
    }
    EXPECT_CALL(*protocol_, writeMapEnd(_));
    EXPECT_EQ(ThriftFilters::FilterStatus::Continue, router_->mapEnd());
    break;
  case FieldType::List:
    EXPECT_CALL(*protocol_, writeListBegin(_, FieldType::I32, 3));
    EXPECT_EQ(ThriftFilters::FilterStatus::Continue, router_->listBegin(FieldType::I32, 3));
    for (int i = 0; i < 3; i++) {
      EXPECT_CALL(*protocol_, writeInt32(_, i));
      EXPECT_EQ(ThriftFilters::FilterStatus::Continue, router_->int32Value(i));
    }
    EXPECT_CALL(*protocol_, writeListEnd(_));
    EXPECT_EQ(ThriftFilters::FilterStatus::Continue, router_->listEnd());
    break;
  case FieldType::Set:
    EXPECT_CALL(*protocol_, writeSetBegin(_, FieldType::I32, 4));
    EXPECT_EQ(ThriftFilters::FilterStatus::Continue, router_->setBegin(FieldType::I32, 4));
    for (int i = 0; i < 4; i++) {
      EXPECT_CALL(*protocol_, writeInt32(_, i));
      EXPECT_EQ(ThriftFilters::FilterStatus::Continue, router_->int32Value(i));
    }
    EXPECT_CALL(*protocol_, writeSetEnd(_));
    EXPECT_EQ(ThriftFilters::FilterStatus::Continue, router_->setEnd());
    break;
  default:
    NOT_REACHED_GCOVR_EXCL_LINE;
  }

  EXPECT_CALL(*protocol_, writeFieldEnd(_));
  EXPECT_EQ(ThriftFilters::FilterStatus::Continue, router_->fieldEnd());

  EXPECT_CALL(*protocol_, writeFieldBegin(_, _, FieldType::Stop, 0));
  EXPECT_CALL(*protocol_, writeStructEnd(_));
  EXPECT_EQ(ThriftFilters::FilterStatus::Continue, router_->structEnd());

  completeRequest();
  destroyRouter();
}

TEST(RouteMatcherTest, Route) {
  const std::string yaml = R"EOF(
name: config
routes:
  - match:
      method: "method1"
    route:
      cluster: "cluster1"
  - match:
      method: "method2"
    route:
      cluster: "cluster2"
)EOF";

  envoy::config::filter::network::thrift_proxy::v2alpha1::RouteConfiguration config =
      parseRouteConfigurationFromV2Yaml(yaml);

  RouteMatcher matcher(config);
  EXPECT_EQ(nullptr, matcher.route("unknown"));
  EXPECT_EQ(nullptr, matcher.route("METHOD1"));

  RouteConstSharedPtr route = matcher.route("method1");
  EXPECT_NE(nullptr, route);
  EXPECT_EQ("cluster1", route->routeEntry()->clusterName());

  RouteConstSharedPtr route2 = matcher.route("method2");
  EXPECT_NE(nullptr, route2);
  EXPECT_EQ("cluster2", route2->routeEntry()->clusterName());
}

TEST(RouteMatcherTest, RouteMatchAny) {
  const std::string yaml = R"EOF(
name: config
routes:
  - match:
      method: "method1"
    route:
      cluster: "cluster1"
  - match: {}
    route:
      cluster: "cluster2"
)EOF";

  envoy::config::filter::network::thrift_proxy::v2alpha1::RouteConfiguration config =
      parseRouteConfigurationFromV2Yaml(yaml);

  RouteMatcher matcher(config);
  RouteConstSharedPtr route = matcher.route("method1");
  EXPECT_NE(nullptr, route);
  EXPECT_EQ("cluster1", route->routeEntry()->clusterName());

  RouteConstSharedPtr route2 = matcher.route("anything");
  EXPECT_NE(nullptr, route2);
  EXPECT_EQ("cluster2", route2->routeEntry()->clusterName());
}

} // namespace Router
} // namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
