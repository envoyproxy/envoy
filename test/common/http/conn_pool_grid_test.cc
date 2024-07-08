#include <memory>

#include "envoy/http/http_server_properties_cache.h"

#include "source/common/http/conn_pool_grid.h"
#include "source/common/http/http_server_properties_cache_impl.h"

#include "test/common/http/common.h"
#include "test/common/upstream/utility.h"
#include "test/mocks/common.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/http/conn_pool.h"
#include "test/mocks/http/stream_decoder.h"
#include "test/mocks/http/stream_encoder.h"
#include "test/mocks/network/connection.h"
#include "test/mocks/server/transport_socket_factory_context.h"
#include "test/mocks/ssl/mocks.h"
#include "test/mocks/upstream/cluster_info.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/threadsafe_singleton_injector.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using Envoy::Event::MockTimer;
using testing::_;
using testing::AnyNumber;
using testing::Return;
using testing::StrictMock;

namespace Envoy {
namespace Http {

class ConnectivityGridForTest : public ConnectivityGrid {
public:
  using ConnectivityGrid::ConnectivityGrid;
  using ConnectivityGrid::getOrCreateHttp2Pool;
  using ConnectivityGrid::getOrCreateHttp3Pool;

  static bool hasHttp3FailedRecently(const ConnectivityGrid& grid) {
    return grid.getHttp3StatusTracker().hasHttp3FailedRecently();
  }

  // Helper method to expose getOrCreateHttp3Pool() for non-test grids
  static ConnectionPool::Instance* forceGetOrCreateHttp3Pool(ConnectivityGrid& grid) {
    return grid.getOrCreateHttp3Pool();
  }
  // Helper method to expose getOrCreateHttp2Pool() for non-test grids
  static ConnectionPool::Instance* forceGetOrCreateHttp2Pool(ConnectivityGrid& grid) {
    return grid.getOrCreateHttp2Pool();
  }

  ConnectionPool::InstancePtr createHttp3Pool(bool alternate) override {
    if (!alternate) {
      return createMockPool("http3");
    }
    ASSERT(!http3_alternate_pool_);
    ConnectionPool::InstancePtr ret = createMockPool("alternate");
    ON_CALL(*static_cast<ConnectionPool::MockInstance*>(ret.get()), newStream(_, _, _))
        .WillByDefault(Invoke(
            [&](Http::ResponseDecoder&, ConnectionPool::Callbacks& callbacks,
                const ConnectionPool::Instance::StreamOptions&) -> ConnectionPool::Cancellable* {
              if (!alternate_immediate_) {
                callbacks_.push_back(&callbacks);
                return cancel_;
              }
              if (alternate_failure_) {
                callbacks.onPoolFailure(ConnectionPool::PoolFailureReason::LocalConnectionFailure,
                                        "reason", host());
              } else {
                callbacks.onPoolReady(*encoder_, host(), *info_, absl::nullopt);
              }
              return nullptr;
            }));
    return ret;
  }
  ConnectionPool::InstancePtr createHttp2Pool() override { return createMockPool("http2"); }

  void createHttp3AlternatePool() {
    ConnectionPool::MockInstance* instance = new NiceMock<ConnectionPool::MockInstance>();
    setupPool(*instance);
    http3_alternate_pool_.reset(instance);
    pools_.push_back(instance);
    EXPECT_CALL(*instance, protocolDescription())
        .Times(AnyNumber())
        .WillRepeatedly(Return("alternate"));
  }

  ConnectionPool::InstancePtr createMockPool(absl::string_view type) {
    ConnectionPool::MockInstance* instance = new NiceMock<ConnectionPool::MockInstance>();
    ON_CALL(*instance, newStream(_, _, _))
        .WillByDefault(
            Invoke([&, &grid = *this](Http::ResponseDecoder&, ConnectionPool::Callbacks& callbacks,
                                      const ConnectionPool::Instance::StreamOptions& options)
                       -> ConnectionPool::Cancellable* {
              if (ConnectivityGridForTest::hasHttp3FailedRecently(grid)) {
                EXPECT_FALSE(options.can_send_early_data_);
              }
              if (immediate_success_) {
                callbacks.onPoolReady(*encoder_, host(), *info_, absl::nullopt);
                return nullptr;
              }
              if (immediate_failure_) {
                callbacks.onPoolFailure(ConnectionPool::PoolFailureReason::LocalConnectionFailure,
                                        "reason", host());
                return nullptr;
              }
              if (second_pool_immediate_success_) {
                second_pool_immediate_success_ = false;
                immediate_success_ = true;
              }
              callbacks_.push_back(&callbacks);
              return cancel_;
            }));
    EXPECT_CALL(*instance, protocolDescription()).Times(AnyNumber()).WillRepeatedly(Return(type));
    return absl::WrapUnique(instance);
  }

  ConnectionPool::MockInstance* http3Pool() {
    return static_cast<ConnectionPool::MockInstance*>(http3_pool_.get());
  }
  ConnectionPool::MockInstance* http2Pool() {
    return static_cast<ConnectionPool::MockInstance*>(http2_pool_.get());
  }
  ConnectionPool::MockInstance* alternate() {
    return static_cast<ConnectionPool::MockInstance*>(http3_alternate_pool_.get());
  }

  ConnectionPool::Callbacks* callbacks(int index = 0) { return callbacks_[index]; }

  bool isHttp3Confirmed() const {
    ASSERT(host_->address()->type() == Network::Address::Type::Ip);
    HttpServerPropertiesCache::Origin origin{"https", host_->hostname(),
                                             host_->address()->ip()->port()};
    HttpServerPropertiesCache::Http3StatusTracker& http3_status_tracker =
        alternate_protocols_->getOrCreateHttp3StatusTracker(origin);
    return http3_status_tracker.isHttp3Confirmed();
  }

  StreamInfo::MockStreamInfo* info_;
  NiceMock<MockRequestEncoder>* encoder_;
  void setDestroying() { destroying_ = true; }
  std::vector<ConnectionPool::Callbacks*> callbacks_;
  NiceMock<Envoy::ConnectionPool::MockCancellable>* cancel_;
  bool immediate_success_{};
  bool immediate_failure_{};
  bool second_pool_immediate_success_{};

  bool alternate_immediate_{true};
  bool alternate_failure_{true};
};

namespace {

class ConnectivityGridTest : public Event::TestUsingSimulatedTime, public testing::Test {
public:
  ConnectivityGridTest()
      : transport_socket_options_(
            std::make_shared<Network::TransportSocketOptionsImpl>("hostname")),
        options_({Http::Protocol::Http11, Http::Protocol::Http2, Http::Protocol::Http3}),
        alternate_protocols_(std::make_shared<HttpServerPropertiesCacheImpl>(
            dispatcher_, std::vector<std::string>(), nullptr, 10)),
        quic_stat_names_(store_.symbolTable()) {
    ON_CALL(factory_context_.server_context_, threadLocal())
        .WillByDefault(ReturnRef(thread_local_));
    // Make sure we test happy eyeballs code.
    address_list_ = {*Network::Utility::resolveUrl("tcp://127.0.0.1:9000"),
                     *Network::Utility::resolveUrl("tcp://[::]:9000")};
  }

  void initialize() {
    quic_connection_persistent_info_ =
#ifdef ENVOY_ENABLE_QUIC
        std::make_unique<Quic::PersistentQuicInfoImpl>(dispatcher_, 0);
#else
        std::make_unique<PersistentQuicInfo>();
#endif
    host_ = std::make_shared<Upstream::HostImpl>(
        cluster_, "hostname", *Network::Utility::resolveUrl("tcp://127.0.0.1:9000"), nullptr,
        nullptr, 1, envoy::config::core::v3::Locality(),
        envoy::config::endpoint::v3::Endpoint::HealthCheckConfig::default_instance(), 0,
        envoy::config::core::v3::UNKNOWN, simTime(), address_list_);

    grid_ = std::make_unique<ConnectivityGridForTest>(
        dispatcher_, random_, host_, Upstream::ResourcePriority::Default, socket_options_,
        transport_socket_options_, state_, simTime(), alternate_protocols_, options_,
        quic_stat_names_, *store_.rootScope(), *quic_connection_persistent_info_);
    grid_->cancel_ = &cancel_;
    grid_->info_ = &info_;
    grid_->encoder_ = &encoder_;
  }

  HttpServerPropertiesCacheSharedPtr
  maybeCreateHttpServerPropertiesCacheImpl(bool use_alternate_protocols) {
    HttpServerPropertiesCacheSharedPtr cache;
    if (!use_alternate_protocols) {
      return nullptr;
    }
    return std::make_shared<HttpServerPropertiesCacheImpl>(dispatcher_, std::vector<std::string>(),
                                                           nullptr, 10);
  }

  void addHttp3AlternateProtocol(absl::optional<std::chrono::microseconds> rtt = {}) {
    std::vector<HttpServerPropertiesCacheImpl::AlternateProtocol> protocols = {
        {"h3", "", origin_.port_, dispatcher_.timeSource().monotonicTime() + Seconds(5)}};
    alternate_protocols_->setAlternatives(origin_, protocols);
    if (rtt.has_value()) {
      alternate_protocols_->setSrtt(origin_, rtt.value());
    }
    alternate_protocols_->getOrCreateHttp3StatusTracker(origin_);
  }

  HttpServerPropertiesCacheImpl::Origin origin_{"https", "hostname", 9000};
  const Network::ConnectionSocket::OptionsSharedPtr socket_options_;
  const Network::TransportSocketOptionsConstSharedPtr transport_socket_options_;
  ConnectivityGrid::ConnectivityOptions options_;
  Upstream::ClusterConnectivityState state_;
  std::shared_ptr<Upstream::MockClusterInfo> cluster_{new NiceMock<Upstream::MockClusterInfo>()};
  NiceMock<Random::MockRandomGenerator> random_;
  HttpServerPropertiesCacheSharedPtr alternate_protocols_;
  Stats::IsolatedStoreImpl store_;
  Quic::QuicStatNames quic_stat_names_;
  PersistentQuicInfoPtr quic_connection_persistent_info_;
  NiceMock<Envoy::ConnectionPool::MockCancellable> cancel_;
  std::shared_ptr<Upstream::HostImpl> host_;
  Upstream::HostDescriptionImpl::AddressVector address_list_{};

  NiceMock<ConnPoolCallbacks> callbacks_;
  NiceMock<MockResponseDecoder> decoder_;

  StreamInfo::MockStreamInfo info_;
  NiceMock<MockRequestEncoder> encoder_;

  NiceMock<Server::Configuration::MockTransportSocketFactoryContext> factory_context_;
  testing::NiceMock<ThreadLocal::MockInstance> thread_local_;
  NiceMock<Event::MockDispatcher> dispatcher_;
  std::unique_ptr<ConnectivityGridForTest> grid_;
};

// Test the first pool successfully connecting.
TEST_F(ConnectivityGridTest, Success) {
  initialize();
  addHttp3AlternateProtocol();
  EXPECT_EQ(grid_->http3Pool(), nullptr);
  EXPECT_NE(grid_->newStream(decoder_, callbacks_,
                             {/*can_send_early_data_=*/false,
                              /*can_use_http3_=*/true}),
            nullptr);
  EXPECT_NE(grid_->http3Pool(), nullptr);
  EXPECT_EQ(grid_->http2Pool(), nullptr);

  // onPoolReady should be passed from the pool back to the original caller.
  ASSERT_NE(grid_->callbacks(), nullptr);
  grid_->onHandshakeComplete();
  EXPECT_TRUE(grid_->isHttp3Confirmed());
  EXPECT_CALL(callbacks_.pool_ready_, ready());
  grid_->callbacks()->onPoolReady(encoder_, host_, info_, absl::nullopt);
}

// Test the first pool successfully connecting under the stack of newStream.
TEST_F(ConnectivityGridTest, ImmediateSuccess) {
  initialize();
  addHttp3AlternateProtocol();
  grid_->immediate_success_ = true;

  EXPECT_CALL(callbacks_.pool_ready_, ready());
  EXPECT_EQ(grid_->newStream(decoder_, callbacks_,
                             {/*can_send_early_data=*/false,
                              /*can_use_http3_=*/true}),
            nullptr);
  EXPECT_NE(grid_->http3Pool(), nullptr);
  EXPECT_FALSE(grid_->isHttp3Broken());
  EXPECT_FALSE(grid_->isHttp3Confirmed());
}

// Test the first pool failing and the second connecting.
TEST_F(ConnectivityGridTest, DoubleFailureThenSuccessSerial) {
  initialize();
  addHttp3AlternateProtocol();
  EXPECT_EQ(grid_->http3Pool(), nullptr);

  EXPECT_LOG_CONTAINS("trace", "http3 pool attempting to create a new stream to host 'hostname'",
                      grid_->newStream(decoder_, callbacks_,
                                       {/*can_send_early_data=*/false,
                                        /*can_use_http3_=*/true}));

  EXPECT_NE(grid_->http3Pool(), nullptr);

  // onPoolFailure should not be passed up the first time. Instead the grid
  // should fail over to the second pool.
  EXPECT_CALL(callbacks_.pool_failure_, ready()).Times(0);

  EXPECT_LOG_CONTAINS_ALL_OF(
      Envoy::ExpectedLogMessages(
          {{"trace", "http3 pool failed to create connection to host 'hostname'"},
           {"trace", "alternate pool failed to create connection to host 'hostname'"},
           {"trace", "http2 pool attempting to create a new stream to host 'hostname'"}}),
      grid_->callbacks()->onPoolFailure(ConnectionPool::PoolFailureReason::LocalConnectionFailure,
                                        "reason", host_));
  ASSERT_NE(grid_->http2Pool(), nullptr);

  // onPoolReady should be passed from the pool back to the original caller.
  ASSERT_NE(grid_->callbacks(), nullptr);
  EXPECT_CALL(callbacks_.pool_ready_, ready());
  EXPECT_LOG_CONTAINS("trace", "http2 pool successfully connected to host 'hostname'",
                      grid_->callbacks(1)->onPoolReady(encoder_, host_, info_, absl::nullopt));
  EXPECT_TRUE(grid_->isHttp3Broken());
}

// Test all three connections in parallel, H3 failing and TCP connecting.
TEST_F(ConnectivityGridTest, ParallelConnectionsTcpConnects) {
  initialize();
  grid_->alternate_immediate_ = false;
  addHttp3AlternateProtocol();
  EXPECT_EQ(grid_->http3Pool(), nullptr);

  // This timer will be returned and armed as the grid creates the wrapper's failover timer.
  Event::MockTimer* failover_timer = new StrictMock<MockTimer>(&dispatcher_);
  EXPECT_CALL(*failover_timer, enableTimer(std::chrono::milliseconds(300), nullptr)).Times(2);
  EXPECT_CALL(*failover_timer, enabled()).WillRepeatedly(Return(false));

  EXPECT_LOG_CONTAINS("trace", "http3 pool attempting to create a new stream to host 'hostname'",
                      grid_->newStream(decoder_, callbacks_,
                                       {/*can_send_early_data=*/false,
                                        /*can_use_http3_=*/true}));

  EXPECT_NE(grid_->http3Pool(), nullptr);
  EXPECT_EQ(grid_->http2Pool(), nullptr);
  EXPECT_EQ(grid_->alternate(), nullptr);

  // The failover timer should kick off H3 alternate and H2
  failover_timer->invokeCallback();
  EXPECT_NE(grid_->http2Pool(), nullptr);
  EXPECT_NE(grid_->alternate(), nullptr);

  EXPECT_CALL(callbacks_.pool_failure_, ready()).Times(0);
  // Fail the alternate pool. H3 should not be broken.
  grid_->callbacks(2)->onPoolFailure(ConnectionPool::PoolFailureReason::LocalConnectionFailure,
                                     "reason", host_);
  EXPECT_FALSE(grid_->isHttp3Broken());

  // Fail the H3 pool. H3 should still not be broken as TCP has not connected.
  grid_->callbacks()->onPoolFailure(ConnectionPool::PoolFailureReason::LocalConnectionFailure,
                                    "reason", host_);
  EXPECT_FALSE(grid_->isHttp3Broken());

  // Now TCP connects. H3 should be marked broken.
  // onPoolReady should be passed from the pool back to the original caller.
  ASSERT_NE(grid_->callbacks(), nullptr);
  EXPECT_CALL(callbacks_.pool_ready_, ready());
  EXPECT_LOG_CONTAINS("trace", "http2 pool successfully connected to host 'hostname'",
                      grid_->callbacks(1)->onPoolReady(encoder_, host_, info_, absl::nullopt));
  EXPECT_TRUE(grid_->isHttp3Broken());
}

// Test the first pool failing inline but http/3 happy eyeballs succeeding inline
TEST_F(ConnectivityGridTest, H3HappyEyeballsMeansNoH2Pool) {
  // The alternate H3 pool will succeed inline.
  initialize();
  grid_->alternate_failure_ = false;
  grid_->immediate_failure_ = true;
  addHttp3AlternateProtocol();
  EXPECT_EQ(grid_->http3Pool(), nullptr);

  EXPECT_CALL(callbacks_.pool_ready_, ready());
  EXPECT_EQ(nullptr, grid_->newStream(decoder_, callbacks_,
                                      {/*can_send_early_data=*/false,
                                       /*can_use_http3_=*/true}));

  EXPECT_NE(grid_->http3Pool(), nullptr);
  EXPECT_NE(grid_->alternate(), nullptr);
  EXPECT_EQ(grid_->http2Pool(), nullptr);
}

// Test both connections happening in parallel and the second connecting.
TEST_F(ConnectivityGridTest, TimeoutThenSuccessParallelH2Connects) {
  initialize();
  addHttp3AlternateProtocol();
  EXPECT_EQ(grid_->http3Pool(), nullptr);

  // This timer will be returned and armed as the grid creates the wrapper's failover timer.
  Event::MockTimer* failover_timer = new StrictMock<MockTimer>(&dispatcher_);
  EXPECT_CALL(*failover_timer, enableTimer(std::chrono::milliseconds(300), nullptr)).Times(2);
  EXPECT_CALL(*failover_timer, enabled()).WillRepeatedly(Return(false));

  grid_->newStream(decoder_, callbacks_,
                   {/*can_send_early_data_=*/false,
                    /*can_use_http3_=*/true});
  EXPECT_NE(grid_->http3Pool(), nullptr);
  EXPECT_TRUE(failover_timer->enabled_);

  // Kick off the second connection.
  failover_timer->invokeCallback();
  EXPECT_NE(grid_->http2Pool(), nullptr);
  // QUIC happy eyeballs fails inline by default but should be tried.
  EXPECT_NE(grid_->alternate(), nullptr);

  // onPoolFailure should not be passed up the first time. Instead the grid
  // should wait on the second pool.
  EXPECT_CALL(callbacks_.pool_failure_, ready()).Times(0);
  grid_->callbacks()->onPoolFailure(ConnectionPool::PoolFailureReason::LocalConnectionFailure,
                                    "reason", host_);

  // onPoolReady should be passed from the pool back to the original caller.
  EXPECT_NE(grid_->callbacks(), nullptr);
  EXPECT_CALL(callbacks_.pool_ready_, ready());
  grid_->callbacks(1)->onPoolReady(encoder_, host_, info_, absl::nullopt);
  EXPECT_TRUE(grid_->isHttp3Broken());
}

// Test both connections happening in parallel and the second connecting.
TEST_F(ConnectivityGridTest, TimeoutThenSuccessParallelH2ConnectsNoHE) {
  address_list_ = {*Network::Utility::resolveUrl("tcp://127.0.0.1:9000"),
                   *Network::Utility::resolveUrl("tcp://127.0.0.1:9001")};
  initialize();
  addHttp3AlternateProtocol();
  EXPECT_EQ(grid_->http3Pool(), nullptr);

  // This timer will be returned and armed as the grid creates the wrapper's failover timer.
  Event::MockTimer* failover_timer = new StrictMock<MockTimer>(&dispatcher_);
  EXPECT_CALL(*failover_timer, enableTimer(std::chrono::milliseconds(300), nullptr)).Times(2);
  EXPECT_CALL(*failover_timer, enabled()).WillRepeatedly(Return(false));

  grid_->newStream(decoder_, callbacks_,
                   {/*can_send_early_data_=*/false,
                    /*can_use_http3_=*/true});
  EXPECT_NE(grid_->http3Pool(), nullptr);
  EXPECT_TRUE(failover_timer->enabled_);

  // Kick off the second connection.
  failover_timer->invokeCallback();
  EXPECT_NE(grid_->http2Pool(), nullptr);
  // Unlike the test above, QUIC happy eyeballs should not be tried because
  // there's only one address family.
  EXPECT_EQ(grid_->alternate(), nullptr);

  // onPoolFailure should not be passed up the first time. Instead the grid
  // should wait on the second pool.
  EXPECT_CALL(callbacks_.pool_failure_, ready()).Times(0);
  grid_->callbacks()->onPoolFailure(ConnectionPool::PoolFailureReason::LocalConnectionFailure,
                                    "reason", host_);

  // onPoolReady should be passed from the pool back to the original caller.
  EXPECT_NE(grid_->callbacks(), nullptr);
  EXPECT_CALL(callbacks_.pool_ready_, ready());
  grid_->callbacks(1)->onPoolReady(encoder_, host_, info_, absl::nullopt);
  EXPECT_TRUE(grid_->isHttp3Broken());
}

// Test timer is affected by prior rtt.
TEST_F(ConnectivityGridTest, SrttMatters) {
  addHttp3AlternateProtocol(std::chrono::microseconds(2000));
  initialize();
  EXPECT_EQ(grid_->http3Pool(), nullptr);

  // This timer will be returned and armed based on prior rtt.
  Event::MockTimer* failover_timer = new StrictMock<MockTimer>(&dispatcher_);
  EXPECT_CALL(*failover_timer, enableTimer(std::chrono::milliseconds(4), nullptr));
  EXPECT_CALL(*failover_timer, enabled()).WillRepeatedly(Return(false));

  auto cancel = grid_->newStream(decoder_, callbacks_,
                                 {/*can_send_early_data_=*/false,
                                  /*can_use_http3_=*/true});
  EXPECT_NE(grid_->http3Pool(), nullptr);
  EXPECT_TRUE(failover_timer->enabled_);

  // Clean up.
  cancel->cancel(Envoy::ConnectionPool::CancelPolicy::CloseExcess);
}

// Test multiple connections happening in parallel and the first connecting.
TEST_F(ConnectivityGridTest, TimeoutThenSuccessParallelFirstConnects) {
  initialize();
  addHttp3AlternateProtocol();
  EXPECT_EQ(grid_->http3Pool(), nullptr);
  grid_->alternate_immediate_ = false;

  // This timer will be returned and armed as the grid creates the wrapper's failover timer.
  Event::MockTimer* failover_timer = new NiceMock<MockTimer>(&dispatcher_);

  grid_->newStream(decoder_, callbacks_,
                   {/*can_send_early_data_=*/false,
                    /*can_use_http3_=*/true});
  EXPECT_NE(grid_->http3Pool(), nullptr);

  EXPECT_TRUE(failover_timer->enabled_);

  // Kick off the second and third connections.
  failover_timer->invokeCallback();
  EXPECT_NE(grid_->http2Pool(), nullptr);
  EXPECT_NE(grid_->alternate(), nullptr);

  // onPoolFailure should not be passed up the first time. Instead the grid
  // should wait on the other pools
  EXPECT_CALL(callbacks_.pool_failure_, ready()).Times(0);
  grid_->callbacks(2)->onPoolFailure(ConnectionPool::PoolFailureReason::LocalConnectionFailure,
                                     "reason", host_);

  EXPECT_CALL(callbacks_.pool_failure_, ready()).Times(0);
  grid_->callbacks(1)->onPoolFailure(ConnectionPool::PoolFailureReason::LocalConnectionFailure,
                                     "reason", host_);

  // onPoolReady should be passed from the pool back to the original caller.
  EXPECT_NE(grid_->callbacks(0), nullptr);
  EXPECT_CALL(callbacks_.pool_ready_, ready());
  grid_->callbacks(0)->onPoolReady(encoder_, host_, info_, absl::nullopt);
  EXPECT_FALSE(grid_->isHttp3Broken());
}

// Test two connections happening in parallel and the second connecting before
// the first eventually fails.
TEST_F(ConnectivityGridTest, TimeoutThenSuccessParallelSecondConnectsFirstFail) {
  initialize();
  addHttp3AlternateProtocol();
  EXPECT_EQ(grid_->http3Pool(), nullptr);

  // This timer will be returned and armed as the grid creates the wrapper's failover timer.
  Event::MockTimer* failover_timer = new NiceMock<MockTimer>(&dispatcher_);

  grid_->newStream(decoder_, callbacks_,
                   {/*can_send_early_data_=*/false,
                    /*can_use_http3_=*/true});
  EXPECT_NE(grid_->http3Pool(), nullptr);
  EXPECT_TRUE(failover_timer->enabled_);

  // Kick off the second connection.
  failover_timer->invokeCallback();
  EXPECT_NE(grid_->http2Pool(), nullptr);

  // onPoolReady should be passed from the pool back to the original caller.
  EXPECT_NE(grid_->callbacks(1), nullptr);
  EXPECT_CALL(callbacks_.pool_ready_, ready());
  grid_->callbacks(1)->onPoolReady(encoder_, host_, info_, absl::nullopt);
  EXPECT_FALSE(grid_->isHttp3Broken());

  // onPoolFailure should not be passed up the first time. Instead the grid
  // should wait on the other pool
  EXPECT_NE(grid_->callbacks(0), nullptr);
  EXPECT_CALL(callbacks_.pool_failure_, ready()).Times(0);
  grid_->callbacks(0)->onPoolFailure(ConnectionPool::PoolFailureReason::LocalConnectionFailure,
                                     "reason", host_);
  EXPECT_TRUE(grid_->isHttp3Broken());
}

// Test both connections happening in parallel and the second connecting before
// the first eventually fails.
TEST_F(ConnectivityGridTest, Http3BrokenWithExpiredHttpServerPropertiesCacheEntry) {
  initialize();
  addHttp3AlternateProtocol();
  EXPECT_EQ(grid_->http3Pool(), nullptr);

  // This timer will be returned and armed as the grid creates the wrapper's failover timer.
  Event::MockTimer* failover_timer = new NiceMock<MockTimer>(&dispatcher_);

  grid_->newStream(decoder_, callbacks_,
                   {/*can_send_early_data_=*/false,
                    /*can_use_http3_=*/true});
  EXPECT_NE(grid_->http3Pool(), nullptr);
  EXPECT_TRUE(failover_timer->enabled_);

  // Kick off the second connection.
  failover_timer->invokeCallback();
  EXPECT_NE(grid_->http2Pool(), nullptr);

  // onPoolReady should be passed from the pool back to the original caller.
  EXPECT_NE(grid_->callbacks(1), nullptr);
  EXPECT_CALL(callbacks_.pool_ready_, ready());
  grid_->callbacks(1)->onPoolReady(encoder_, host_, info_, absl::nullopt);
  EXPECT_FALSE(grid_->isHttp3Broken());

  // Advance time so that the cache entry expires and alternatives removed by a following fetching.
  simTime().setMonotonicTime(simTime().monotonicTime() + Seconds(10));
  EXPECT_FALSE(alternate_protocols_->findAlternatives(origin_).has_value());

  // onPoolFailure should not be passed up the first time. Instead the grid
  // should wait on the other pool
  EXPECT_NE(grid_->callbacks(0), nullptr);
  EXPECT_CALL(callbacks_.pool_failure_, ready()).Times(0);
  grid_->callbacks(0)->onPoolFailure(ConnectionPool::PoolFailureReason::LocalConnectionFailure,
                                     "reason", host_);
  // The Broken state should be cached.
  EXPECT_TRUE(grid_->isHttp3Broken());
  EXPECT_FALSE(alternate_protocols_->findAlternatives(origin_).has_value());

  // Updating the alternatives of the same origin shouldn't change its HTTP/3 status.
  std::vector<HttpServerPropertiesCacheImpl::AlternateProtocol> protocols = {
      {"h3-29", "", origin_.port_, simTime().monotonicTime() + Seconds(5)}};
  alternate_protocols_->setAlternatives(origin_, protocols);
  EXPECT_TRUE(grid_->isHttp3Broken());
}

// Test that newStream() with HTTP/3 disabled.
TEST_F(ConnectivityGridTest, NewStreamWithHttp3Disabled) {
  addHttp3AlternateProtocol();
  initialize();
  grid_->immediate_success_ = true;

  EXPECT_CALL(callbacks_.pool_ready_, ready());
  EXPECT_EQ(grid_->newStream(decoder_, callbacks_,
                             {/*can_send_early_data_=*/false,
                              /*can_use_http3_=*/false}),
            nullptr);
  EXPECT_NE(grid_->http3Pool(), nullptr);
  EXPECT_NE(grid_->http2Pool(), nullptr);
  EXPECT_TRUE(grid_->isHttp3Broken());
}

// Test that newStream() with alternate protocols disabled and TCP connection also fails.
TEST_F(ConnectivityGridTest, NewStreamWithAltSvcDisabledFail) {
  addHttp3AlternateProtocol();
  initialize();
  grid_->immediate_failure_ = true;

  EXPECT_CALL(callbacks_.pool_failure_, ready());
  EXPECT_EQ(grid_->newStream(decoder_, callbacks_,
                             {/*can_send_early_data_=*/false,
                              /*can_use_http3_=*/false}),
            nullptr);
  EXPECT_NE(grid_->http3Pool(), nullptr);
  EXPECT_NE(grid_->http2Pool(), nullptr);
  EXPECT_FALSE(grid_->isHttp3Broken());
}

// Test that after the first pool fails, subsequent connections will
// successfully fail over to the second pool (the iterators work as intended)
TEST_F(ConnectivityGridTest, FailureThenSuccessForMultipleConnectionsSerial) {
  initialize();
  addHttp3AlternateProtocol();
  NiceMock<ConnPoolCallbacks> callbacks2;
  NiceMock<MockResponseDecoder> decoder2;
  // Kick off two new streams.
  auto* cancel1 = grid_->newStream(decoder_, callbacks_,
                                   {/*can_send_early_data_=*/false,
                                    /*can_use_http3_=*/true});
  auto* cancel2 = grid_->newStream(decoder2, callbacks2,
                                   {/*can_send_early_data_=*/false,
                                    /*can_use_http3_=*/true});

  // Fail the first connection and verify the second pool is created.
  EXPECT_CALL(callbacks_.pool_failure_, ready()).Times(0);
  grid_->callbacks()->onPoolFailure(ConnectionPool::PoolFailureReason::LocalConnectionFailure,
                                    "reason", host_);
  ASSERT_NE(grid_->http2Pool(), nullptr);

  // Fail the second connection, and verify the second pool gets another newStream call.
  EXPECT_CALL(callbacks_.pool_failure_, ready()).Times(0);
  EXPECT_CALL(*grid_->http2Pool(), newStream(_, _, _));
  grid_->callbacks(1)->onPoolFailure(ConnectionPool::PoolFailureReason::LocalConnectionFailure,
                                     "reason", host_);

  // Clean up.
  cancel1->cancel(Envoy::ConnectionPool::CancelPolicy::CloseExcess);
  cancel2->cancel(Envoy::ConnectionPool::CancelPolicy::CloseExcess);
}

// Test triple failure under the stack of newStream.
TEST_F(ConnectivityGridTest, ImmediateDoubleFailure) {
  initialize();
  addHttp3AlternateProtocol();
  grid_->immediate_failure_ = true;
  EXPECT_CALL(callbacks_.pool_failure_, ready());
  EXPECT_EQ(grid_->newStream(decoder_, callbacks_,
                             {/*can_send_early_data_=*/false,
                              /*can_use_http3_=*/true}),
            nullptr);
  EXPECT_FALSE(grid_->isHttp3Broken());
}

// Test both connections happening in parallel and both failing.
TEST_F(ConnectivityGridTest, TimeoutDoubleFailureParallel) {
  initialize();
  addHttp3AlternateProtocol();
  EXPECT_EQ(grid_->http3Pool(), nullptr);

  // This timer will be returned and armed as the grid creates the wrapper's failover timer.
  Event::MockTimer* failover_timer = new NiceMock<MockTimer>(&dispatcher_);

  grid_->newStream(decoder_, callbacks_,
                   {/*can_send_early_data_=*/false,
                    /*can_use_http3_=*/true});
  EXPECT_NE(grid_->http3Pool(), nullptr);
  EXPECT_TRUE(failover_timer->enabled_);

  // Kick off the second connection.
  failover_timer->invokeCallback();
  EXPECT_NE(grid_->http2Pool(), nullptr);

  // onPoolFailure should not be passed up the first time. Instead the grid
  // should wait on the second pool.
  EXPECT_CALL(callbacks_.pool_failure_, ready()).Times(0);
  grid_->callbacks()->onPoolFailure(ConnectionPool::PoolFailureReason::LocalConnectionFailure,
                                    "reason", host_);

  // Failure should be passed from the pool back to the original caller.
  EXPECT_CALL(callbacks_.pool_failure_, ready());
  grid_->callbacks(1)->onPoolFailure(ConnectionPool::PoolFailureReason::LocalConnectionFailure,
                                     "reason", host_);
  EXPECT_FALSE(grid_->isHttp3Broken());
}

// Test cancellation
TEST_F(ConnectivityGridTest, TestCancel) {
  initialize();
  addHttp3AlternateProtocol();
  EXPECT_EQ(grid_->http3Pool(), nullptr);

  auto cancel = grid_->newStream(decoder_, callbacks_,
                                 {/*can_send_early_data_=*/false,
                                  /*can_use_http3_=*/true});
  EXPECT_NE(grid_->http3Pool(), nullptr);

  // cancel should be passed through the WrapperCallbacks to the connection pool.
  EXPECT_CALL(*grid_->cancel_, cancel(_));
  cancel->cancel(Envoy::ConnectionPool::CancelPolicy::CloseExcess);
}

// Test tearing down the grid with active connections.
TEST_F(ConnectivityGridTest, TestTeardown) {
  initialize();
  addHttp3AlternateProtocol();
  EXPECT_EQ(grid_->http3Pool(), nullptr);

  grid_->newStream(decoder_, callbacks_,
                   {/*can_send_early_data_=*/false,
                    /*can_use_http3_=*/true});
  EXPECT_NE(grid_->http3Pool(), nullptr);

  // When the grid is reset, pool failure should be called.
  EXPECT_CALL(callbacks_.pool_failure_, ready());
  grid_.reset();
}

// Make sure drains get sent to all active pools.
TEST_F(ConnectivityGridTest, Drain) {
  initialize();
  addHttp3AlternateProtocol();
  grid_->drainConnections(Envoy::ConnectionPool::DrainBehavior::DrainExistingConnections);

  // Synthetically create a pool.
  grid_->getOrCreateHttp3Pool();
  {
    EXPECT_CALL(*grid_->http3Pool(),
                drainConnections(Envoy::ConnectionPool::DrainBehavior::DrainExistingConnections));
    grid_->drainConnections(Envoy::ConnectionPool::DrainBehavior::DrainExistingConnections);
  }

  grid_->getOrCreateHttp2Pool();
  {
    EXPECT_CALL(*grid_->http3Pool(),
                drainConnections(Envoy::ConnectionPool::DrainBehavior::DrainExistingConnections));
    EXPECT_CALL(*grid_->http2Pool(),
                drainConnections(Envoy::ConnectionPool::DrainBehavior::DrainExistingConnections));
    grid_->drainConnections(Envoy::ConnectionPool::DrainBehavior::DrainExistingConnections);
  }
}

// Make sure drain callbacks work as expected.
TEST_F(ConnectivityGridTest, DrainCallbacks) {
  initialize();
  addHttp3AlternateProtocol();
  // Synthetically create both pools.
  grid_->getOrCreateHttp3Pool();
  grid_->getOrCreateHttp2Pool();

  bool drain_received = false;

  grid_->addIdleCallback([&]() { drain_received = true; });

  // The first time a drain is started, both pools should start draining.
  {
    EXPECT_CALL(*grid_->http3Pool(),
                drainConnections(Envoy::ConnectionPool::DrainBehavior::DrainExistingConnections));
    EXPECT_CALL(*grid_->http2Pool(),
                drainConnections(Envoy::ConnectionPool::DrainBehavior::DrainExistingConnections));
    grid_->drainConnections(Envoy::ConnectionPool::DrainBehavior::DrainExistingConnections);
  }

  grid_->createHttp3AlternatePool();
  // The second time a drain is started, both pools should still be notified.
  {
    EXPECT_CALL(*grid_->http3Pool(),
                drainConnections(Envoy::ConnectionPool::DrainBehavior::DrainAndDelete));
    EXPECT_CALL(*grid_->http2Pool(),
                drainConnections(Envoy::ConnectionPool::DrainBehavior::DrainAndDelete));
    EXPECT_CALL(*grid_->alternate(),
                drainConnections(Envoy::ConnectionPool::DrainBehavior::DrainAndDelete));
    grid_->drainConnections(Envoy::ConnectionPool::DrainBehavior::DrainAndDelete);
  }
  {
    // Notify the grid the second pool has been drained. This should not be
    // passed up to the original callers.
    EXPECT_FALSE(drain_received);
    EXPECT_CALL(*grid_->http2Pool(), isIdle()).WillRepeatedly(Return(true));
    grid_->http2Pool()->idle_cb_();
    EXPECT_FALSE(drain_received);
  }

  {
    // Notify the grid that the remaining pools have been drained. Now that all pools are
    // drained, the original callers should be informed.
    EXPECT_FALSE(drain_received);
    EXPECT_CALL(*grid_->http3Pool(), isIdle()).WillRepeatedly(Return(true));
    EXPECT_CALL(*grid_->alternate(), isIdle()).WillRepeatedly(Return(true));
    grid_->http3Pool()->idle_cb_();
    EXPECT_TRUE(drain_received);
  }
}

// Make sure idle callbacks work as expected.
TEST_F(ConnectivityGridTest, IdleCallbacks) {
  initialize();
  addHttp3AlternateProtocol();
  // Synthetically create both pools.
  grid_->getOrCreateHttp3Pool();
  grid_->getOrCreateHttp2Pool();

  bool idle_received = false;

  grid_->addIdleCallback([&]() { idle_received = true; });
  EXPECT_FALSE(idle_received);

  // Notify the grid the second pool is idle. This should not be
  // passed up to the original callers.
  EXPECT_CALL(*grid_->http2Pool(), isIdle()).WillOnce(Return(true));
  EXPECT_CALL(*grid_->http3Pool(), isIdle()).WillOnce(Return(false));
  grid_->http2Pool()->idle_cb_();
  EXPECT_FALSE(idle_received);

  // Notify the grid that the first pool is idle, the but second no longer is.
  EXPECT_CALL(*grid_->http3Pool(), isIdle()).WillOnce(Return(true));
  EXPECT_CALL(*grid_->http2Pool(), isIdle()).WillOnce(Return(false));
  grid_->http3Pool()->idle_cb_();
  EXPECT_FALSE(idle_received);

  // Notify the grid that both are now idle. This should be passed up
  // to the original caller.
  EXPECT_CALL(*grid_->http3Pool(), isIdle()).WillOnce(Return(true));
  EXPECT_CALL(*grid_->http2Pool(), isIdle()).WillOnce(Return(true));
  grid_->http3Pool()->idle_cb_();
  EXPECT_TRUE(idle_received);
}

// Ensure drain callbacks aren't called during grid teardown.
TEST_F(ConnectivityGridTest, NoDrainOnTeardown) {
  initialize();
  addHttp3AlternateProtocol();
  grid_->getOrCreateHttp3Pool();

  bool drain_received = false;

  {
    grid_->addIdleCallback([&drain_received]() -> void { drain_received = true; });
    grid_->drainConnections(Envoy::ConnectionPool::DrainBehavior::DrainAndDelete);
  }

  grid_->setDestroying(); // Fake being in the destructor.
  grid_->http3Pool()->idle_cb_();
  EXPECT_FALSE(drain_received);
}

// Test that when HTTP/3 is broken then the HTTP/3 pool is skipped.
TEST_F(ConnectivityGridTest, SuccessAfterBroken) {
  initialize();
  addHttp3AlternateProtocol();
  alternate_protocols_
      ->getOrCreateHttp3StatusTracker(HttpServerPropertiesCache::Origin("https", "hostname", 9000))
      .markHttp3Broken();
  EXPECT_EQ(grid_->http3Pool(), nullptr);

  EXPECT_LOG_CONTAINS("trace", "HTTP/3 is broken to host 'hostname', skipping.",
                      EXPECT_NE(grid_->newStream(decoder_, callbacks_,
                                                 {/*can_send_early_data_=*/false,
                                                  /*can_use_http3_=*/true}),
                                nullptr));
  EXPECT_NE(grid_->http3Pool(), nullptr);
  EXPECT_NE(grid_->http2Pool(), nullptr);

  // onPoolReady should be passed from the pool back to the original caller.
  ASSERT_NE(grid_->callbacks(), nullptr);
  EXPECT_CALL(callbacks_.pool_ready_, ready());
  grid_->callbacks()->onPoolReady(encoder_, host_, info_, absl::nullopt);
  EXPECT_TRUE(grid_->isHttp3Broken());
}

// Test the HTTP/3 pool successfully connecting when HTTP/3 is available.
TEST_F(ConnectivityGridTest, SuccessWithAltSvc) {
  initialize();
  addHttp3AlternateProtocol();
  EXPECT_EQ(grid_->http3Pool(), nullptr);

  EXPECT_NE(grid_->newStream(decoder_, callbacks_,
                             {/*can_send_early_data_=*/false,
                              /*can_use_http3_=*/true}),
            nullptr);
  EXPECT_NE(grid_->http3Pool(), nullptr);
  EXPECT_EQ(grid_->http2Pool(), nullptr);

  // onPoolReady should be passed from the pool back to the original caller.
  ASSERT_NE(grid_->callbacks(), nullptr);
  EXPECT_CALL(callbacks_.pool_ready_, ready());
  grid_->callbacks()->onPoolReady(encoder_, host_, info_, absl::nullopt);
  EXPECT_FALSE(grid_->isHttp3Broken());
}

// Test that when HTTP/3 is not available then the HTTP/3 pool is skipped.
TEST_F(ConnectivityGridTest, SuccessWithoutHttp3) {
  initialize();
  EXPECT_EQ(grid_->http3Pool(), nullptr);

  EXPECT_LOG_CONTAINS("trace",
                      "No alternate protocols available for host 'hostname', skipping HTTP/3.",
                      EXPECT_NE(grid_->newStream(decoder_, callbacks_,
                                                 {/*can_send_early_data_=*/false,
                                                  /*can_use_http3_=*/true}),
                                nullptr));
  EXPECT_NE(grid_->http3Pool(), nullptr);
  EXPECT_NE(grid_->http2Pool(), nullptr);

  // onPoolReady should be passed from the pool back to the original caller.
  ASSERT_NE(grid_->callbacks(), nullptr);
  EXPECT_CALL(callbacks_.pool_ready_, ready());
  grid_->callbacks()->onPoolReady(encoder_, host_, info_, absl::nullopt);
}

// Test that when HTTP/3 is not available then the HTTP/3 pool is skipped.
TEST_F(ConnectivityGridTest, SuccessWithExpiredHttp3) {
  initialize();
  std::vector<HttpServerPropertiesCacheImpl::AlternateProtocol> protocols = {
      {"h3-29", "", origin_.port_, simTime().monotonicTime() + Seconds(5)}};
  alternate_protocols_->setAlternatives(origin_, protocols);
  simTime().setMonotonicTime(simTime().monotonicTime() + Seconds(10));

  EXPECT_EQ(grid_->http3Pool(), nullptr);

  EXPECT_LOG_CONTAINS("trace",
                      "No alternate protocols available for host 'hostname', skipping HTTP/3.",
                      EXPECT_NE(grid_->newStream(decoder_, callbacks_,
                                                 {/*can_send_early_data_=*/false,
                                                  /*can_use_http3_=*/true}),
                                nullptr));
  EXPECT_NE(grid_->http3Pool(), nullptr);
  EXPECT_NE(grid_->http2Pool(), nullptr);

  // onPoolReady should be passed from the pool back to the original caller.
  ASSERT_NE(grid_->callbacks(), nullptr);
  EXPECT_CALL(callbacks_.pool_ready_, ready());
  grid_->callbacks()->onPoolReady(encoder_, host_, info_, absl::nullopt);
}

// Test that when the alternate protocol specifies a different host, then the HTTP/3 pool is
// skipped.
TEST_F(ConnectivityGridTest, SuccessWithoutHttp3NoMatchingHostname) {
  initialize();
  std::vector<HttpServerPropertiesCacheImpl::AlternateProtocol> protocols = {
      {"h3-29", "otherhostname", origin_.port_, simTime().monotonicTime() + Seconds(5)}};
  alternate_protocols_->setAlternatives(origin_, protocols);

  EXPECT_EQ(grid_->http3Pool(), nullptr);

  EXPECT_LOG_CONTAINS("trace", "HTTP/3 is not available to host 'hostname', skipping.",
                      EXPECT_NE(grid_->newStream(decoder_, callbacks_,
                                                 {/*can_send_early_data_=*/false,
                                                  /*can_use_http3_=*/true}),
                                nullptr));
  EXPECT_NE(grid_->http3Pool(), nullptr);
  EXPECT_NE(grid_->http2Pool(), nullptr);

  // onPoolReady should be passed from the pool back to the original caller.
  ASSERT_NE(grid_->callbacks(), nullptr);
  EXPECT_CALL(callbacks_.pool_ready_, ready());
  grid_->callbacks()->onPoolReady(encoder_, host_, info_, absl::nullopt);
}

// Test that when the alternate protocol specifies a different port, then the HTTP/3 pool is
// skipped.
TEST_F(ConnectivityGridTest, SuccessWithoutHttp3NoMatchingPort) {
  initialize();
  std::vector<HttpServerPropertiesCacheImpl::AlternateProtocol> protocols = {
      {"h3-29", "", origin_.port_ + 1, simTime().monotonicTime() + Seconds(5)}};
  alternate_protocols_->setAlternatives(origin_, protocols);

  EXPECT_EQ(grid_->http3Pool(), nullptr);

  EXPECT_LOG_CONTAINS("trace", "HTTP/3 is not available to host 'hostname', skipping.",
                      EXPECT_NE(grid_->newStream(decoder_, callbacks_,
                                                 {/*can_send_early_data_=*/false,
                                                  /*can_use_http3_=*/true}),
                                nullptr));
  EXPECT_NE(grid_->http3Pool(), nullptr);
  EXPECT_NE(grid_->http2Pool(), nullptr);

  // onPoolReady should be passed from the pool back to the original caller.
  ASSERT_NE(grid_->callbacks(), nullptr);
  EXPECT_CALL(callbacks_.pool_ready_, ready());
  grid_->callbacks()->onPoolReady(encoder_, host_, info_, absl::nullopt);
}

// Test that when the alternate protocol specifies an invalid ALPN, then the HTTP/3 pool is skipped.
TEST_F(ConnectivityGridTest, SuccessWithoutHttp3NoMatchingAlpn) {
  initialize();
  std::vector<HttpServerPropertiesCacheImpl::AlternateProtocol> protocols = {
      {"http/2", "", origin_.port_, simTime().monotonicTime() + Seconds(5)}};
  alternate_protocols_->setAlternatives(origin_, protocols);

  EXPECT_EQ(grid_->http3Pool(), nullptr);

  EXPECT_LOG_CONTAINS("trace", "HTTP/3 is not available to host 'hostname', skipping.",
                      EXPECT_NE(grid_->newStream(decoder_, callbacks_,
                                                 {/*can_send_early_data_=*/false,
                                                  /*can_use_http3_=*/true}),
                                nullptr));
  EXPECT_NE(grid_->http3Pool(), nullptr);
  EXPECT_NE(grid_->http2Pool(), nullptr);

  // onPoolReady should be passed from the pool back to the original caller.
  ASSERT_NE(grid_->callbacks(), nullptr);
  EXPECT_CALL(callbacks_.pool_ready_, ready());
  grid_->callbacks()->onPoolReady(encoder_, host_, info_, absl::nullopt);
}

// Test the TCP pool will be immediately attempted if HTTP/3 has failed before.
TEST_F(ConnectivityGridTest, Http3FailedRecentlyThenSucceeds) {
  initialize();
  addHttp3AlternateProtocol();
  grid_->onZeroRttHandshakeFailed();
  EXPECT_TRUE(ConnectivityGridForTest::hasHttp3FailedRecently(*grid_));
  EXPECT_EQ(grid_->http3Pool(), nullptr);

  // This timer will be returned and armed as the grid creates the wrapper's failover timer.
  Event::MockTimer* failover_timer = new StrictMock<MockTimer>(&dispatcher_);
  EXPECT_CALL(*failover_timer, enabled()).WillOnce(Return(false)).WillOnce(Return(true));
  EXPECT_CALL(*failover_timer, enableTimer(_, _));
  EXPECT_NE(grid_->newStream(decoder_, callbacks_,
                             {/*can_send_early_data_=*/true,
                              /*can_use_http3_=*/true}),
            nullptr);
  EXPECT_NE(grid_->http3Pool(), nullptr);
  // The 2nd pool should be TCP pool and it should have been created together with h3 pool.
  EXPECT_NE(grid_->http2Pool(), nullptr);
  EXPECT_EQ(2u, grid_->callbacks_.size());

  // If failover timer expires, as no more pool to try on.
  failover_timer->invokeCallback();
  EXPECT_EQ(2u, grid_->callbacks_.size());

  // onPoolReady should be passed from the pool back to the original caller.
  ASSERT_NE(grid_->callbacks(0), nullptr);
  ASSERT_NE(grid_->callbacks(1), nullptr);
  EXPECT_CALL(callbacks_.pool_ready_, ready());
  EXPECT_CALL(*grid_->cancel_, cancel(_));
  grid_->callbacks(0)->onPoolReady(encoder_, host_, info_, absl::nullopt);
  // Getting onPoolReady() from HTTP/3 pool doesn't change H3 status.
  EXPECT_TRUE(ConnectivityGridForTest::hasHttp3FailedRecently(*grid_));
}

// Test the TCP pool will be immediately attempted if HTTP/3 has failed before.
TEST_F(ConnectivityGridTest, Http3FailedRecentlyThenFailsAgain) {
  initialize();
  addHttp3AlternateProtocol();
  grid_->onZeroRttHandshakeFailed();
  EXPECT_TRUE(ConnectivityGridForTest::hasHttp3FailedRecently(*grid_));
  EXPECT_EQ(grid_->http3Pool(), nullptr);

  EXPECT_NE(grid_->newStream(decoder_, callbacks_,
                             {/*can_send_early_data_=*/true,
                              /*can_use_http3_=*/true}),
            nullptr);
  EXPECT_NE(grid_->http3Pool(), nullptr);
  EXPECT_NE(grid_->http2Pool(), nullptr);

  // onPoolReady should be passed from the pool back to the original caller.
  ASSERT_NE(grid_->callbacks(0), nullptr);
  ASSERT_NE(grid_->callbacks(1), nullptr);
  EXPECT_CALL(callbacks_.pool_ready_, ready());
  grid_->callbacks(1)->onPoolReady(encoder_, host_, info_, absl::nullopt);
  // Getting onPoolReady() from TCP pool alone doesn't change H3 status.
  EXPECT_TRUE(ConnectivityGridForTest::hasHttp3FailedRecently(*grid_));
  // Getting onPoolFailure() from Http3 pool later should mark H3 broken.
  grid_->callbacks(0)->onPoolFailure(ConnectionPool::PoolFailureReason::LocalConnectionFailure,
                                     "reason", host_);
  EXPECT_TRUE(grid_->isHttp3Broken());
}

TEST_F(ConnectivityGridTest, Http3FailedH2SuceedsInline) {
  initialize();
  addHttp3AlternateProtocol();
  grid_->onZeroRttHandshakeFailed();
  EXPECT_TRUE(ConnectivityGridForTest::hasHttp3FailedRecently(*grid_));
  EXPECT_EQ(grid_->http3Pool(), nullptr);

  // Force H3 to have no immediate change, but H2 to immediately succeed.
  grid_->second_pool_immediate_success_ = true;
  // Because the second pool immediately succeeded, newStream should return nullptr.
  EXPECT_EQ(grid_->newStream(decoder_, callbacks_,
                             {/*can_send_early_data_=*/true,
                              /*can_use_http3_=*/true}),
            nullptr);
}

#ifdef ENVOY_ENABLE_QUIC

} // namespace
} // namespace Http
} // namespace Envoy

#include "source/common/quic/quic_client_transport_socket_factory.h"
namespace Envoy {
namespace Http {
namespace {

TEST_F(ConnectivityGridTest, RealGrid) {
  initialize();
  testing::InSequence s;
  dispatcher_.allow_null_callback_ = true;
  // Set the cluster up to have a quic transport socket.
  Envoy::Ssl::ClientContextConfigPtr config(new NiceMock<Ssl::MockClientContextConfig>());
  auto factory =
      *Quic::QuicClientTransportSocketFactory::create(std::move(config), factory_context_);
  factory->initialize();
  auto& matcher =
      static_cast<Upstream::MockTransportSocketMatcher&>(*cluster_->transport_socket_matcher_);
  EXPECT_CALL(matcher, resolve(_, _))
      .WillRepeatedly(
          Return(Upstream::TransportSocketMatcher::MatchData(*factory, matcher.stats_, "test")));

  ConnectivityGrid grid(
      dispatcher_, random_, Upstream::makeTestHost(cluster_, "tcp://127.0.0.1:9000", simTime()),
      Upstream::ResourcePriority::Default, socket_options_, transport_socket_options_, state_,
      simTime(), alternate_protocols_, options_, quic_stat_names_, *store_.rootScope(),
      *quic_connection_persistent_info_);
  EXPECT_EQ("connection grid", grid.protocolDescription());
  EXPECT_FALSE(grid.hasActiveConnections());

  // Create the HTTP/3 pool.
  auto pool1 = ConnectivityGridForTest::forceGetOrCreateHttp3Pool(grid);
  ASSERT_TRUE(pool1 != nullptr);
  EXPECT_EQ("HTTP/3", pool1->protocolDescription());
  EXPECT_FALSE(grid.hasActiveConnections());

  // Create the mixed pool.
  auto pool2 = ConnectivityGridForTest::forceGetOrCreateHttp2Pool(grid);
  ASSERT_TRUE(pool2 != nullptr);
  EXPECT_EQ("HTTP/1 HTTP/2 ALPN", pool2->protocolDescription());
}

TEST_F(ConnectivityGridTest, ConnectionCloseDuringAysnConnect) {
  initialize();
  EXPECT_CALL(*cluster_, connectTimeout()).WillRepeatedly(Return(std::chrono::seconds(10)));

  testing::InSequence s;
  dispatcher_.allow_null_callback_ = true;
  // Set the cluster up to have a quic transport socket.
  Envoy::Ssl::ClientContextConfigPtr config(new NiceMock<Ssl::MockClientContextConfig>());
  Ssl::ClientContextSharedPtr ssl_context(new Ssl::MockClientContext());
  EXPECT_CALL(factory_context_.context_manager_, createSslClientContext(_, _))
      .WillOnce(Return(ssl_context));
  auto factory =
      *Quic::QuicClientTransportSocketFactory::create(std::move(config), factory_context_);
  factory->initialize();
  auto& matcher =
      static_cast<Upstream::MockTransportSocketMatcher&>(*cluster_->transport_socket_matcher_);
  EXPECT_CALL(matcher, resolve(_, _))
      .WillRepeatedly(
          Return(Upstream::TransportSocketMatcher::MatchData(*factory, matcher.stats_, "test")));

  ConnectivityGrid grid(
      dispatcher_, random_, Upstream::makeTestHost(cluster_, "tcp://127.0.0.1:9000", simTime()),
      Upstream::ResourcePriority::Default, socket_options_, transport_socket_options_, state_,
      simTime(), alternate_protocols_, options_, quic_stat_names_, *store_.rootScope(),
      *quic_connection_persistent_info_);

  // Create the HTTP/3 pool.
  auto pool = ConnectivityGridForTest::forceGetOrCreateHttp3Pool(grid);
  ASSERT_TRUE(pool != nullptr);
  EXPECT_EQ("HTTP/3", pool->protocolDescription());

  const bool supports_getifaddrs = Api::OsSysCallsSingleton::get().supportsGetifaddrs();
  Api::InterfaceAddressVector interfaces{};
  if (supports_getifaddrs) {
    ASSERT_EQ(0, Api::OsSysCallsSingleton::get().getifaddrs(interfaces).return_value_);
  }

  NiceMock<Api::MockOsSysCalls> os_sys_calls;
  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls(&os_sys_calls);
  EXPECT_CALL(os_sys_calls, supportsGetifaddrs()).WillOnce(Return(supports_getifaddrs));
  if (supports_getifaddrs) {
    EXPECT_CALL(os_sys_calls, getifaddrs(_))
        .WillOnce(
            Invoke([&](Api::InterfaceAddressVector& interface_vector) -> Api::SysCallIntResult {
              interface_vector.insert(interface_vector.begin(), interfaces.begin(),
                                      interfaces.end());
              return {0, 0};
            }));
  }
  EXPECT_CALL(os_sys_calls, socket(_, _, _)).WillOnce(Return(Api::SysCallSocketResult{1, 0}));
#if defined(__APPLE__) || defined(WIN32)
  EXPECT_CALL(os_sys_calls, setsocketblocking(1, false))
      .WillOnce(Return(Api::SysCallIntResult{1, 0}));
#endif
  EXPECT_CALL(os_sys_calls, setsockopt_(_, _, _, _, _))
      .Times(testing::AtLeast(0u))
      .WillRepeatedly(Return(0));
  EXPECT_CALL(os_sys_calls, bind(_, _, _)).WillOnce(Return(Api::SysCallIntResult{1, 0}));
  EXPECT_CALL(os_sys_calls, setsockopt_(_, _, _, _, _)).WillRepeatedly(Return(0));
  auto* async_connect_callback = new NiceMock<Event::MockSchedulableCallback>(&dispatcher_);

  ConnectionPool::Cancellable* cancel = pool->newStream(decoder_, callbacks_,
                                                        {/*can_send_early_data_=*/false,
                                                         /*can_use_http3_=*/true});
  EXPECT_NE(nullptr, cancel);

  EXPECT_CALL(os_sys_calls, sendmsg(_, _, _)).WillOnce(Return(Api::SysCallSizeResult{-1, 101}));
  EXPECT_CALL(callbacks_.pool_failure_, ready());
  async_connect_callback->invokeCallback();
}

#endif

} // namespace
} // namespace Http
} // namespace Envoy
