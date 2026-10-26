#include "envoy/extensions/upstreams/http/generic/v3/generic_connection_pool.pb.h"
#include "envoy/extensions/upstreams/tcp/generic/v3/generic_connection_pool.pb.h"

#include "source/common/stream_info/stream_info_impl.h"

#include "test/mocks/http/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/router/mocks.h"
#include "test/mocks/router/router_filter_interface.h"
#include "test/mocks/tcp/mocks.h"
#include "test/mocks/upstream/load_balancer_context.h"
#include "test/mocks/upstream/thread_local_cluster.h"
#include "test/test_common/registry.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/utility.h"

#include "contrib/istio/filters/network/hbone/source/pool.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Istio {
namespace Hbone {
namespace {

using testing::_;
using testing::Return;
using testing::ReturnRef;
using testing::SaveArg;
using testing::StrictMock;

constexpr absl::string_view Rejection = "istio_hbone_connect_rejected:403";

class HttpInnerFactory : public Router::GenericConnPoolFactory {
public:
  std::string category() const override { return "envoy.upstreams"; }
  std::string name() const override { return "envoy.filters.connection_pools.http.generic"; }
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<
        envoy::extensions::upstreams::http::generic::v3::GenericConnectionPoolProto>();
  }
  Router::GenericConnPoolPtr
  createGenericConnPool(Upstream::HostConstSharedPtr, Upstream::ThreadLocalCluster&,
                        UpstreamProtocol, Upstream::ResourcePriority, std::optional<Http::Protocol>,
                        Upstream::LoadBalancerContext*, const Protobuf::Message&) const override {
    return std::move(pool_);
  }
  mutable Router::GenericConnPoolPtr pool_;
};

class MockRouterContext : public Router::MockRouterFilterInterface,
                          public Upstream::MockLoadBalancerContext {};

class HboneHttpPoolTest : public testing::Test {
protected:
  HboneHttpPoolTest()
      : api_(Api::createApiForTest(time_system_)), dispatcher_(api_->allocateDispatcher("test")) {
    EXPECT_CALL(router_, callbacks()).WillRepeatedly(Return(&downstream_));
    EXPECT_CALL(downstream_, dispatcher()).WillRepeatedly(ReturnRef(*dispatcher_));
    EXPECT_CALL(downstream_, streamInfo()).WillRepeatedly(ReturnRef(info_));
    info_.setUpstreamInfo(std::make_shared<StreamInfo::UpstreamInfoImpl>());
    inner_factory_.pool_ = std::make_unique<StrictMock<Router::MockGenericConnPool>>();
    inner_ = static_cast<StrictMock<Router::MockGenericConnPool>*>(inner_factory_.pool_.get());
    EXPECT_CALL(*inner_, cancelAnyPendingStream()).WillRepeatedly(Return(false));
  }

  Router::GenericConnPoolPtr create(Upstream::LoadBalancerContext* context) {
    return factory_.createGenericConnPool(
        nullptr, cluster_, Router::GenericConnPoolFactory::UpstreamProtocol::HTTP,
        Upstream::ResourcePriority::Default, Http::Protocol::Http11, context, PoolConfig{});
  }

  void initialize() {
    pool_ = create(&router_);
    ASSERT_NE(nullptr, pool_);
    EXPECT_CALL(*inner_, newStream(_)).WillOnce(SaveArg<0>(&adapter_callbacks_));
    pool_->newStream(&callbacks_);
    ASSERT_NE(nullptr, adapter_callbacks_);
  }

  void reject() {
    // The adapter must retain the reason after this temporary string is destroyed.
    const std::string reason(Rejection);
    adapter_callbacks_->onPoolFailure(ConnectionPool::PoolFailureReason::RemoteConnectionFailure,
                                      reason, host_);
  }

  void run() { dispatcher_->run(Event::Dispatcher::RunType::NonBlock); }

  Event::SimulatedTimeSystem time_system_;
  Api::ApiPtr api_;
  Event::DispatcherPtr dispatcher_;
  StreamInfo::StreamInfoImpl info_{time_system_, nullptr,
                                   StreamInfo::FilterState::LifeSpan::Request};
  StrictMock<Upstream::MockThreadLocalCluster> cluster_;
  StrictMock<MockRouterContext> router_;
  StrictMock<Http::MockStreamDecoderFilterCallbacks> downstream_;
  StrictMock<Router::MockGenericConnectionPoolCallbacks> callbacks_;
  Upstream::HostDescriptionConstSharedPtr host_{
      std::make_shared<StrictMock<Upstream::MockHostDescription>>()};
  HttpInnerFactory inner_factory_;
  Registry::InjectFactory<Router::GenericConnPoolFactory> registration_{inner_factory_};
  HttpPoolFactory factory_;
  StrictMock<Router::MockGenericConnPool>* inner_{};
  Router::GenericConnectionPoolCallbacks* adapter_callbacks_{};
  Router::GenericConnPoolPtr pool_;
};

TEST_F(HboneHttpPoolTest, NullInnerPoolDoesNotRequireRouterContext) {
  inner_factory_.pool_.reset();
  EXPECT_EQ(nullptr, create(nullptr));
}

TEST_F(HboneHttpPoolTest, UnexpectedContextReportsBugAndReturnsInnerPool) {
  StrictMock<Upstream::MockLoadBalancerContext> context;
  EXPECT_ENVOY_BUG(EXPECT_EQ(inner_, create(&context).get()),
                   "HBONE HTTP pool requires a router context");
}

TEST_F(HboneHttpPoolTest, NullContextReportsBugAndReturnsInnerPool) {
  EXPECT_ENVOY_BUG(EXPECT_EQ(inner_, create(nullptr).get()),
                   "HBONE HTTP pool requires a router context");
}

TEST_F(HboneHttpPoolTest, TransientFailureUsesNormalCallbacks) {
  initialize();
  EXPECT_CALL(callbacks_,
              onPoolFailure(ConnectionPool::PoolFailureReason::Timeout, "TLS error", host_));
  adapter_callbacks_->onPoolFailure(ConnectionPool::PoolFailureReason::Timeout, "TLS error", host_);
  EXPECT_FALSE(info_.hasAnyResponseFlag());
  EXPECT_FALSE(pool_->cancelAnyPendingStream());
  run();
}

TEST_F(HboneHttpPoolTest, ReadyUsesNormalCallbacks) {
  initialize();
  StrictMock<Network::MockConnection> connection;
  EXPECT_CALL(connection, connectionInfoProvider());
  EXPECT_CALL(callbacks_, onPoolReady(_, host_, _, _, testing::Optional(Http::Protocol::Http11)));
  adapter_callbacks_->onPoolReady(nullptr, host_, connection.connectionInfoProvider(), info_,
                                  Http::Protocol::Http11);
  EXPECT_FALSE(info_.hasAnyResponseFlag());
  run();
}

TEST_F(HboneHttpPoolTest, RejectionIsDeferredAndLocalReplyCanDestroyPool) {
  initialize();
  reject();
  EXPECT_TRUE(info_.hasResponseFlag(StreamInfo::CoreResponseFlag::UpstreamConnectionFailure));
  EXPECT_EQ(Rejection, info_.responseCodeDetails());
  EXPECT_EQ(host_, info_.upstreamInfo()->upstreamHost());
  EXPECT_EQ(Rejection, info_.upstreamInfo()->upstreamTransportFailureReason());
  // StrictMock rejects a synchronous reply before this expectation is installed.
  EXPECT_CALL(downstream_, sendLocalReply(Http::Code::Forbidden, _, _, _, Rejection))
      .WillOnce([this](auto, auto, auto, auto, absl::string_view details) {
        pool_.reset();
        EXPECT_EQ(Rejection, details);
      });
  run();
  EXPECT_EQ(nullptr, pool_);
}

TEST_F(HboneHttpPoolTest, CancellationRemovesAlreadyScheduledRejection) {
  initialize();
  reject();
  // No inner stream is pending: cancellation must still report the deferred reply.
  EXPECT_TRUE(pool_->cancelAnyPendingStream());
  EXPECT_FALSE(pool_->cancelAnyPendingStream());
  run();
}

TEST_F(HboneHttpPoolTest, DestructionRemovesAlreadyScheduledRejection) {
  initialize();
  reject();
  pool_.reset();
  run();
}

class MockTcpPool : public TcpProxy::GenericConnPool {
public:
  MOCK_METHOD(void, newStream, (TcpProxy::GenericConnectionPoolCallbacks&), (override));
};

class MockTcpPoolCallbacks : public TcpProxy::GenericConnectionPoolCallbacks {
public:
  MOCK_METHOD(void, onGenericPoolReady,
              (StreamInfo::StreamInfo*, std::unique_ptr<TcpProxy::GenericUpstream>&&,
               Upstream::HostDescriptionConstSharedPtr&, const Network::ConnectionInfoProvider&,
               Ssl::ConnectionInfoConstSharedPtr),
              (override));
  MOCK_METHOD(void, onGenericPoolFailure,
              (ConnectionPool::PoolFailureReason, absl::string_view,
               Upstream::HostDescriptionConstSharedPtr),
              (override));
};

class TcpInnerFactory : public TcpProxy::GenericConnPoolFactory {
public:
  std::string category() const override { return "envoy.upstreams"; }
  std::string name() const override { return "envoy.filters.connection_pools.tcp.generic"; }
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<
        envoy::extensions::upstreams::tcp::generic::v3::GenericConnectionPoolProto>();
  }
  TcpProxy::GenericConnPoolPtr createGenericConnPool(Upstream::HostConstSharedPtr,
                                                     Upstream::ThreadLocalCluster&,
                                                     TcpProxy::TunnelingConfigHelperOptConstRef,
                                                     Upstream::LoadBalancerContext*,
                                                     Tcp::ConnectionPool::UpstreamCallbacks&,
                                                     Http::StreamDecoderFilterCallbacks&,
                                                     StreamInfo::StreamInfo&) const override {
    return std::move(pool_);
  }
  mutable TcpProxy::GenericConnPoolPtr pool_;
};

class HboneTcpPoolTest : public testing::Test {
protected:
  HboneTcpPoolTest()
      : api_(Api::createApiForTest(time_system_)), dispatcher_(api_->allocateDispatcher("test")) {
    EXPECT_CALL(context_, downstreamConnection()).WillRepeatedly(Return(&downstream_));
    EXPECT_CALL(downstream_, dispatcher()).WillRepeatedly(ReturnRef(*dispatcher_));
    inner_factory_.pool_ = std::make_unique<StrictMock<MockTcpPool>>();
    inner_ = static_cast<StrictMock<MockTcpPool>*>(inner_factory_.pool_.get());
  }

  TcpProxy::GenericConnPoolPtr create(Upstream::LoadBalancerContext* context) {
    return factory_.createGenericConnPool(nullptr, cluster_, {}, context, upstream_callbacks_,
                                          decoder_callbacks_, info_);
  }

  void initialize() {
    pool_ = create(&context_);
    ASSERT_NE(nullptr, pool_);
    EXPECT_CALL(*inner_, newStream(_)).WillOnce([this](auto& callbacks) {
      adapter_callbacks_ = &callbacks;
    });
    pool_->newStream(callbacks_);
    ASSERT_NE(nullptr, adapter_callbacks_);
  }

  void reject() {
    const std::string reason(Rejection);
    adapter_callbacks_->onGenericPoolFailure(
        ConnectionPool::PoolFailureReason::RemoteConnectionFailure, reason, nullptr);
  }

  void run() { dispatcher_->run(Event::Dispatcher::RunType::NonBlock); }

  Event::SimulatedTimeSystem time_system_;
  Api::ApiPtr api_;
  Event::DispatcherPtr dispatcher_;
  StreamInfo::StreamInfoImpl info_{time_system_, nullptr,
                                   StreamInfo::FilterState::LifeSpan::Connection};
  StrictMock<Upstream::MockThreadLocalCluster> cluster_;
  StrictMock<Upstream::MockLoadBalancerContext> context_;
  StrictMock<Network::MockConnection> downstream_;
  StrictMock<Tcp::ConnectionPool::MockUpstreamCallbacks> upstream_callbacks_;
  StrictMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks_;
  StrictMock<MockTcpPoolCallbacks> callbacks_;
  TcpInnerFactory inner_factory_;
  Registry::InjectFactory<TcpProxy::GenericConnPoolFactory> registration_{inner_factory_};
  TcpPoolFactory factory_;
  StrictMock<MockTcpPool>* inner_{};
  TcpProxy::GenericConnectionPoolCallbacks* adapter_callbacks_{};
  TcpProxy::GenericConnPoolPtr pool_;
};

TEST_F(HboneTcpPoolTest, NullInnerPoolDoesNotRequireDownstream) {
  inner_factory_.pool_.reset();
  EXPECT_EQ(nullptr, create(nullptr));
}

TEST_F(HboneTcpPoolTest, NullContextReportsBugAndReturnsInnerPool) {
  EXPECT_ENVOY_BUG(EXPECT_EQ(inner_, create(nullptr).get()),
                   "HBONE TCP pool requires a downstream connection");
}

TEST_F(HboneTcpPoolTest, MissingDownstreamReportsBugAndReturnsInnerPool) {
  EXPECT_CALL(context_, downstreamConnection()).WillRepeatedly(Return(nullptr));
  EXPECT_ENVOY_BUG(EXPECT_EQ(inner_, create(&context_).get()),
                   "HBONE TCP pool requires a downstream connection");
}

TEST_F(HboneTcpPoolTest, TransientFailureUsesNormalCallbacks) {
  initialize();
  EXPECT_CALL(callbacks_, onGenericPoolFailure(ConnectionPool::PoolFailureReason::Timeout,
                                               "TLS error", nullptr));
  adapter_callbacks_->onGenericPoolFailure(ConnectionPool::PoolFailureReason::Timeout, "TLS error",
                                           nullptr);
  EXPECT_FALSE(info_.hasAnyResponseFlag());
  run();
}

TEST_F(HboneTcpPoolTest, ReadyUsesNormalCallbacks) {
  initialize();
  Upstream::HostDescriptionConstSharedPtr host;
  EXPECT_CALL(downstream_, connectionInfoProvider());
  EXPECT_CALL(callbacks_, onGenericPoolReady(&info_, _, _, _, nullptr));
  adapter_callbacks_->onGenericPoolReady(&info_, nullptr, host,
                                         downstream_.connectionInfoProvider(), nullptr);
  EXPECT_FALSE(info_.hasAnyResponseFlag());
  run();
}

TEST_F(HboneTcpPoolTest, RejectionIsDeferredAndCloseCanDestroyPool) {
  initialize();
  reject();
  EXPECT_TRUE(info_.hasResponseFlag(StreamInfo::CoreResponseFlag::UpstreamConnectionFailure));
  EXPECT_EQ(Rejection, info_.responseCodeDetails());
  EXPECT_CALL(downstream_, close(Network::ConnectionCloseType::NoFlush, Rejection))
      .WillOnce([this](auto, absl::string_view details) {
        pool_.reset();
        EXPECT_EQ(Rejection, details);
      });
  run();
  EXPECT_EQ(nullptr, pool_);
}

TEST_F(HboneTcpPoolTest, DestructionRemovesAlreadyScheduledRejection) {
  initialize();
  reject();
  pool_.reset();
  run();
}

} // namespace
} // namespace Hbone
} // namespace Istio
} // namespace Extensions
} // namespace Envoy
