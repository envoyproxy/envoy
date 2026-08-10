#include <chrono>
#include <memory>
#include <string>

#include "envoy/extensions/filters/network/redis_proxy/v3/redis_proxy.pb.h"

#include "source/common/protobuf/protobuf.h"
#include "source/extensions/filters/network/redis_proxy/command_splitter_impl.h"
#include "source/extensions/filters/network/redis_proxy/config.h"
#include "source/extensions/filters/network/redis_proxy/proxy_filter.h"
#include "source/extensions/filters/network/well_known_names.h"

#include "test/common/stats/stat_test_utility.h"
#include "test/extensions/common/dynamic_forward_proxy/mocks.h"
#include "test/extensions/filters/network/common/redis/mocks.h"
#include "test/extensions/filters/network/redis_proxy/mocks.h"
#include "test/extensions/filters/network/redis_proxy/pubsub_test_utils.h"
#include "test/mocks/api/mocks.h"
#include "test/mocks/common.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/server/factory_context.h"
#include "test/mocks/upstream/cluster_manager.h"
#include "test/mocks/upstream/thread_local_cluster.h"
#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::ByRef;
using testing::DoAll;
using testing::Eq;
using testing::InSequence;
using testing::Invoke;
using testing::NiceMock;
using testing::Ref;
using testing::Return;
using testing::WithArg;
using testing::WithArgs;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace RedisProxy {

envoy::extensions::filters::network::redis_proxy::v3::RedisProxy
parseProtoFromYaml(const std::string& yaml_string) {
  envoy::extensions::filters::network::redis_proxy::v3::RedisProxy config;
  TestUtility::loadFromYaml(yaml_string, config);
  return config;
}

class RedisProxyFilterConfigTest
    : public testing::Test,
      public Extensions::Common::DynamicForwardProxy::DnsCacheManagerFactory {
public:
  Extensions::Common::DynamicForwardProxy::DnsCacheManagerSharedPtr get() override {
    return dns_cache_manager_;
  }

  std::shared_ptr<Extensions::Common::DynamicForwardProxy::MockDnsCacheManager> dns_cache_manager_{
      new Extensions::Common::DynamicForwardProxy::MockDnsCacheManager()};
  Stats::TestUtil::TestStore store_;
  Network::MockDrainDecision drain_decision_;
  Runtime::MockLoader runtime_;
  NiceMock<Api::MockApi> api_;
  Event::SimulatedTimeSystem time_source_;
};

TEST_F(RedisProxyFilterConfigTest, Normal) {
  const std::string yaml_string = R"EOF(
  prefix_routes:
    catch_all_route:
      cluster: fake_cluster
  stat_prefix: foo
  settings:
    op_timeout: 0.01s
  )EOF";

  envoy::extensions::filters::network::redis_proxy::v3::RedisProxy proto_config =
      parseProtoFromYaml(yaml_string);
  ProxyFilterConfig config(proto_config, *store_.rootScope(), drain_decision_, runtime_, api_,
                           time_source_, *this);
  EXPECT_EQ("redis.foo.", config.stat_prefix_);
  EXPECT_TRUE(config.downstream_auth_username_.empty());
  EXPECT_TRUE(config.downstream_auth_passwords_.empty());
}

TEST_F(RedisProxyFilterConfigTest, BadRedisProxyConfig) {
  const std::string yaml_string = R"EOF(
  cluster_name: fake_cluster
  cluster: fake_cluster
  )EOF";

  EXPECT_THROW(parseProtoFromYaml(yaml_string), EnvoyException);
}

TEST_F(RedisProxyFilterConfigTest, DownstreamAuthPasswordSet) {
  const std::string yaml_string = R"EOF(
  prefix_routes:
    catch_all_route:
      cluster: fake_cluster
  stat_prefix: foo
  settings:
    op_timeout: 0.01s
  downstream_auth_password:
    inline_string: somepassword
  )EOF";

  envoy::extensions::filters::network::redis_proxy::v3::RedisProxy proto_config =
      parseProtoFromYaml(yaml_string);
  ProxyFilterConfig config(proto_config, *store_.rootScope(), drain_decision_, runtime_, api_,
                           time_source_, *this);
  EXPECT_EQ(config.downstream_auth_passwords_.size(), 1);
  EXPECT_EQ(config.downstream_auth_passwords_[0], "somepassword");
}

TEST_F(RedisProxyFilterConfigTest, DownstreamMultipleAuthPasswordsSet) {
  const std::string yaml_string = R"EOF(
  prefix_routes:
    catch_all_route:
      cluster: fake_cluster
  stat_prefix: foo
  settings:
    op_timeout: 0.01s
  downstream_auth_password:
    inline_string: somepassword
  downstream_auth_passwords:
  - inline_string: newpassword1
  - inline_string: newpassword2
  )EOF";

  envoy::extensions::filters::network::redis_proxy::v3::RedisProxy proto_config =
      parseProtoFromYaml(yaml_string);
  ProxyFilterConfig config(proto_config, *store_.rootScope(), drain_decision_, runtime_, api_,
                           time_source_, *this);
  EXPECT_EQ(config.downstream_auth_passwords_.size(), 3);
  EXPECT_EQ(config.downstream_auth_passwords_[0], "somepassword");
  EXPECT_EQ(config.downstream_auth_passwords_[1], "newpassword1");
  EXPECT_EQ(config.downstream_auth_passwords_[2], "newpassword2");
}

TEST_F(RedisProxyFilterConfigTest, DownstreamOnlyExraAuthPasswordsSet) {
  const std::string yaml_string = R"EOF(
  prefix_routes:
    catch_all_route:
      cluster: fake_cluster
  stat_prefix: foo
  settings:
    op_timeout: 0.01s
  downstream_auth_passwords:
  - inline_string: newpassword1
  - inline_string: newpassword2
  )EOF";

  envoy::extensions::filters::network::redis_proxy::v3::RedisProxy proto_config =
      parseProtoFromYaml(yaml_string);
  ProxyFilterConfig config(proto_config, *store_.rootScope(), drain_decision_, runtime_, api_,
                           time_source_, *this);
  EXPECT_EQ(config.downstream_auth_passwords_.size(), 2);
  EXPECT_EQ(config.downstream_auth_passwords_[0], "newpassword1");
  EXPECT_EQ(config.downstream_auth_passwords_[1], "newpassword2");
}

TEST_F(RedisProxyFilterConfigTest, DownstreamAuthAclSet) {
  const std::string yaml_string = R"EOF(
  prefix_routes:
    catch_all_route:
      cluster: fake_cluster
  stat_prefix: foo
  settings:
    op_timeout: 0.01s
  downstream_auth_username:
    inline_string: someusername
  downstream_auth_password:
    inline_string: somepassword
  )EOF";

  envoy::extensions::filters::network::redis_proxy::v3::RedisProxy proto_config =
      parseProtoFromYaml(yaml_string);
  ProxyFilterConfig config(proto_config, *store_.rootScope(), drain_decision_, runtime_, api_,
                           time_source_, *this);
  EXPECT_EQ(config.downstream_auth_username_, "someusername");
  EXPECT_EQ(config.downstream_auth_passwords_.size(), 1);
  EXPECT_EQ(config.downstream_auth_passwords_[0], "somepassword");
}

TEST_F(RedisProxyFilterConfigTest, DownstreamAuthAclSetWithMultiplePasswords) {
  const std::string yaml_string = R"EOF(
  prefix_routes:
    catch_all_route:
      cluster: fake_cluster
  stat_prefix: foo
  settings:
    op_timeout: 0.01s
  downstream_auth_username:
    inline_string: someusername
  downstream_auth_password:
    inline_string: somepassword
  downstream_auth_passwords:
  - inline_string: newpassword1
  - inline_string: newpassword2
  )EOF";

  envoy::extensions::filters::network::redis_proxy::v3::RedisProxy proto_config =
      parseProtoFromYaml(yaml_string);
  ProxyFilterConfig config(proto_config, *store_.rootScope(), drain_decision_, runtime_, api_,
                           time_source_, *this);
  EXPECT_EQ(config.downstream_auth_username_, "someusername");
  EXPECT_EQ(config.downstream_auth_passwords_.size(), 3);
  EXPECT_EQ(config.downstream_auth_passwords_[0], "somepassword");
  EXPECT_EQ(config.downstream_auth_passwords_[1], "newpassword1");
  EXPECT_EQ(config.downstream_auth_passwords_[2], "newpassword2");
}

TEST_F(RedisProxyFilterConfigTest, DownstreamAuthAclSetWithOnlyExtraPasswords) {
  const std::string yaml_string = R"EOF(
  prefix_routes:
    catch_all_route:
      cluster: fake_cluster
  stat_prefix: foo
  settings:
    op_timeout: 0.01s
  downstream_auth_username:
    inline_string: someusername
  downstream_auth_passwords:
  - inline_string: newpassword1
  - inline_string: newpassword2
  )EOF";

  envoy::extensions::filters::network::redis_proxy::v3::RedisProxy proto_config =
      parseProtoFromYaml(yaml_string);
  ProxyFilterConfig config(proto_config, *store_.rootScope(), drain_decision_, runtime_, api_,
                           time_source_, *this);
  EXPECT_EQ(config.downstream_auth_username_, "someusername");
  EXPECT_EQ(config.downstream_auth_passwords_.size(), 2);
  EXPECT_EQ(config.downstream_auth_passwords_[0], "newpassword1");
  EXPECT_EQ(config.downstream_auth_passwords_[1], "newpassword2");
}

TEST_F(RedisProxyFilterConfigTest, ExternalAuthBasic) {
  const std::string yaml_string = R"EOF(
  prefix_routes:
    catch_all_route:
      cluster: fake_cluster
  stat_prefix: foo
  settings:
    op_timeout: 0.01s
  external_auth_provider:
    grpc_service:
      envoy_grpc:
        cluster_name: fake_cluster
  )EOF";

  envoy::extensions::filters::network::redis_proxy::v3::RedisProxy proto_config =
      parseProtoFromYaml(yaml_string);
  ProxyFilterConfig config(proto_config, *store_.rootScope(), drain_decision_, runtime_, api_,
                           time_source_, *this);
  EXPECT_TRUE(config.external_auth_enabled_);
  EXPECT_FALSE(config.external_auth_expiration_enabled_);
}

TEST_F(RedisProxyFilterConfigTest, ExternalAuthWithExpiration) {
  const std::string yaml_string = R"EOF(
  prefix_routes:
    catch_all_route:
      cluster: fake_cluster
  stat_prefix: foo
  settings:
    op_timeout: 0.01s
  external_auth_provider:
    grpc_service:
      envoy_grpc:
        cluster_name: fake_cluster
    enable_auth_expiration: true
  )EOF";

  envoy::extensions::filters::network::redis_proxy::v3::RedisProxy proto_config =
      parseProtoFromYaml(yaml_string);
  ProxyFilterConfig config(proto_config, *store_.rootScope(), drain_decision_, runtime_, api_,
                           time_source_, *this);
  EXPECT_TRUE(config.external_auth_enabled_);
  EXPECT_TRUE(config.external_auth_expiration_enabled_);
}

// Listener-level RESP version tests. The ``protocol_version`` is fixed at
// filter config load from the static ``RedisProxy.protocol_version`` field;
// there is no live cluster lookup and no floor computation. Cluster manager
// state cannot change it.
TEST_F(RedisProxyFilterConfigTest, ProtocolVersionDefaultsToResp2) {
  const std::string yaml_string = R"EOF(
  prefix_routes:
    catch_all_route:
      cluster: fake_cluster
  stat_prefix: foo
  settings:
    op_timeout: 0.01s
  )EOF";
  envoy::extensions::filters::network::redis_proxy::v3::RedisProxy proto_config =
      parseProtoFromYaml(yaml_string);
  ProxyFilterConfig config(proto_config, *store_.rootScope(), drain_decision_, runtime_, api_,
                           time_source_, *this);
  EXPECT_EQ(Common::Redis::RespProtocolVersion::Resp2, config.protocolVersion());
}

// ``protocol_version: RESP3`` is read into ProxyFilterConfig::protocolVersion(), which
// config.cc forwards verbatim to ConnPool::InstanceImpl (and on to ClientFactory — see
// conn_pool_impl_test). Pins the proto → conn-pool wiring at its source.
TEST_F(RedisProxyFilterConfigTest, ProtocolVersionResp3IsHonored) {
  const std::string yaml_string = R"EOF(
  prefix_routes:
    catch_all_route:
      cluster: fake_cluster
  stat_prefix: foo
  protocol_version: RESP3
  settings:
    op_timeout: 0.01s
  )EOF";
  envoy::extensions::filters::network::redis_proxy::v3::RedisProxy proto_config =
      parseProtoFromYaml(yaml_string);
  ProxyFilterConfig config(proto_config, *store_.rootScope(), drain_decision_, runtime_, api_,
                           time_source_, *this);
  EXPECT_EQ(Common::Redis::RespProtocolVersion::Resp3, config.protocolVersion());
}

class RedisProxyFilterTest
    : public testing::Test,
      public Common::Redis::DecoderFactory,
      public Extensions::Common::DynamicForwardProxy::DnsCacheManagerFactory {
public:
  Extensions::Common::DynamicForwardProxy::DnsCacheManagerSharedPtr get() override {
    return dns_cache_manager_;
  }

  std::shared_ptr<Extensions::Common::DynamicForwardProxy::MockDnsCacheManager> dns_cache_manager_{
      new Extensions::Common::DynamicForwardProxy::MockDnsCacheManager()};

  static constexpr const char* DefaultConfig = R"EOF(
  prefix_routes:
    catch_all_route:
      cluster: fake_cluster
  stat_prefix: foo
  settings:
    op_timeout: 0.01s
  )EOF";

  RedisProxyFilterTest(const std::string& yaml_string) {
    envoy::extensions::filters::network::redis_proxy::v3::RedisProxy proto_config =
        parseProtoFromYaml(yaml_string);
    config_ = std::make_shared<ProxyFilterConfig>(
        proto_config, *store_.rootScope(), drain_decision_, runtime_, api_, time_source_, *this);
    time_source_.setSystemTime(std::chrono::seconds(0));
    buildFilter();
  }

  RedisProxyFilterTest() : RedisProxyFilterTest(DefaultConfig) {}

  // Construct the ProxyFilter using the previously-built config_. Split out of the ctor so
  // subclasses can build the filter after they have wired their own fixture state.
  void buildFilter() {
    if (config_->external_auth_enabled_) {
      external_auth_client_ = new ExternalAuth::MockExternalAuthClient();
      filter_ = std::make_unique<ProxyFilter>(
          *this, Common::Redis::EncoderPtr{encoder_}, splitter_, config_,
          ExternalAuth::ExternalAuthClientPtr{external_auth_client_});
    } else {
      filter_ = std::make_unique<ProxyFilter>(*this, Common::Redis::EncoderPtr{encoder_}, splitter_,
                                              config_, nullptr);
    }

    filter_->initializeReadFilterCallbacks(filter_callbacks_);
    EXPECT_EQ(Network::FilterStatus::Continue, filter_->onNewConnection());
    EXPECT_EQ(1UL, config_->stats_.downstream_cx_total_.value());
    EXPECT_EQ(1UL, config_->stats_.downstream_cx_active_.value());

    // No subscriber attached yet, so the high-watermark callback is a no-op here.
    filter_->onAboveWriteBufferHighWatermark();
    filter_->onBelowWriteBufferLowWatermark();
  }

  // The fixture is a friend of ProxyFilter (friendship is not inherited, so the TEST_F-generated
  // subclass cannot touch private members directly); attach a subscriber here for the
  // slow-subscriber backpressure test without driving the whole subscribe path.
  void attachSubscriberForTest() {
    filter_->subscriber_ = std::make_shared<DownstreamSubscriber>(filter_callbacks_.connection_,
                                                                  testDownstreamSubscriberStats());
  }

  // Simulate active pub/sub work (parked message-push bytes) on the filter, so the slow-subscriber
  // watermark path treats the attached subscriber as an active consumer (see the slow-subscriber
  // guard).
  void setHeldPushBytesForTest(uint64_t n) { filter_->held_push_bytes_ = n; }

  // Create a subscriber through the production path (getOrCreateSubscriber), which also wires the
  // filter's push-ordering sink so deliverMessage routes back through
  // ProxyFilter::enqueueOrderedPush (FIFO ordering + backpressure tests). Friendship is not
  // inherited, so the TEST_F body cannot call the private getOrCreateSubscriber itself — this
  // fixture helper does.
  DownstreamSubscriberPtr getWiredSubscriberForTest() { return filter_->getOrCreateSubscriber(); }

  // Friendship is not inherited, so these fixture helpers expose the private close handler and the
  // filter's subscriber_ to TEST_F bodies (G-1: subscriber survives an upstream transaction-client
  // close, which routes to handleConnectionEvent with is_downstream=false).
  void driveConnectionEvent(Network::ConnectionEvent event, bool is_downstream) {
    filter_->handleConnectionEvent(event, is_downstream);
  }
  bool filterHoldsSubscriber() { return filter_->subscriber_ != nullptr; }

  ~RedisProxyFilterTest() override {
    filter_.reset();
    for (const Stats::GaugeSharedPtr& gauge : store_.gauges()) {
      EXPECT_EQ(0U, gauge->value());
    }
  }

  // Common::Redis::DecoderFactory
  Common::Redis::DecoderPtr create(Common::Redis::DecoderCallbacks& callbacks) override {
    decoder_callbacks_ = &callbacks;
    return Common::Redis::DecoderPtr{decoder_};
  }

  // NiceMock suppresses the "uninteresting call" warning for tests that drive request/response
  // flows without explicitly asserting on encoder calls — every successful HELLO emits a Map
  // reply through ``encode``, and any reply forwarded by ``onResponse`` likewise hits the
  // encoder, so without NiceMock those tests would flood with uninteresting-call warnings.
  NiceMock<Common::Redis::MockEncoder>* encoder_{new NiceMock<Common::Redis::MockEncoder>()};
  Common::Redis::MockDecoder* decoder_{new Common::Redis::MockDecoder()};
  Common::Redis::DecoderCallbacks* decoder_callbacks_{};
  CommandSplitter::MockInstance splitter_;
  Stats::TestUtil::TestStore store_;
  NiceMock<Network::MockDrainDecision> drain_decision_;
  NiceMock<Runtime::MockLoader> runtime_;
  ProxyFilterConfigSharedPtr config_;
  std::unique_ptr<ProxyFilter> filter_;
  NiceMock<Network::MockReadFilterCallbacks> filter_callbacks_;
  NiceMock<Api::MockApi> api_;
  ExternalAuth::MockExternalAuthClient* external_auth_client_;
  Event::SimulatedTimeSystem time_source_;
};

class RedisProxyFilterTestWithTwoCallbacks : public RedisProxyFilterTest {
public:
  CommandSplitter::MockSplitRequest* request_handle1_{new CommandSplitter::MockSplitRequest()};
  CommandSplitter::MockSplitRequest* request_handle2_{new CommandSplitter::MockSplitRequest()};
  CommandSplitter::SplitCallbacks* request_callbacks1_;
  CommandSplitter::SplitCallbacks* request_callbacks2_;

  void decodeHelper(Buffer::Instance&) {
    Common::Redis::RespValuePtr request1(new Common::Redis::RespValue());
    EXPECT_CALL(splitter_, makeRequest_(Ref(*request1), _, _, _))
        .WillOnce(
            DoAll(WithArg<1>(SaveArgAddress(&request_callbacks1_)), Return(request_handle1_)));
    decoder_callbacks_->onRespValue(std::move(request1));

    Common::Redis::RespValuePtr request2(new Common::Redis::RespValue());
    EXPECT_CALL(splitter_, makeRequest_(Ref(*request2), _, _, _))
        .WillOnce(
            DoAll(WithArg<1>(SaveArgAddress(&request_callbacks2_)), Return(request_handle2_)));
    decoder_callbacks_->onRespValue(std::move(request2));
  }
};

TEST_F(RedisProxyFilterTestWithTwoCallbacks, OutOfOrderResponseWithDrainClose) {
  InSequence s;

  Buffer::OwnedImpl fake_data;
  EXPECT_CALL(*decoder_, decode(Ref(fake_data)))
      .WillOnce(Invoke(this, &RedisProxyFilterTestWithTwoCallbacks::decodeHelper));
  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(fake_data, false));

  EXPECT_EQ(2UL, config_->stats_.downstream_rq_total_.value());
  EXPECT_EQ(2UL, config_->stats_.downstream_rq_active_.value());

  Common::Redis::RespValuePtr response2(new Common::Redis::RespValue());
  Common::Redis::RespValue* response2_ptr = response2.get();
  request_callbacks2_->onResponse(std::move(response2));

  Common::Redis::RespValuePtr response1(new Common::Redis::RespValue());
  EXPECT_CALL(*encoder_, encode(Ref(*response1), _));
  EXPECT_CALL(*encoder_, encode(Ref(*response2_ptr), _));
  EXPECT_CALL(filter_callbacks_.connection_, write(_, _));
  EXPECT_CALL(drain_decision_, drainClose(Network::DrainDirection::All)).WillOnce(Return(true));
  EXPECT_CALL(runtime_.snapshot_, featureEnabled("redis.drain_close_enabled", 100))
      .WillOnce(Return(true));
  EXPECT_CALL(filter_callbacks_.connection_, close(Network::ConnectionCloseType::FlushWrite));
  request_callbacks1_->onResponse(std::move(response1));

  EXPECT_EQ(1UL, config_->stats_.downstream_cx_drain_close_.value());
}

// Within a SINGLE
// decode() loop, an earlier frame can synchronously close the downstream connection — a reply/ack
// write tripping the slow-subscriber high-watermark (onAboveWriteBufferHighWatermark →
// closeSlowSubscriber), or a splitter-driven error close — which runs onEvent(LocalClose) inline
// and cancels every pending request. The decoder keeps looping over the buffer, so a LATER frame in
// the same loop MUST be dropped at the Open-state guard: turning it into a PendingRequest would
// leak a live upstream handle no future close can cancel (~PendingRequest ASSERT in debug /
// freed-callback UAF in release), and a SUBSCRIBE would re-create a subscriber on the closed
// connection (zombie upstream subscription + leaked gauge). This reproduces the
// two-pipelined-frame case: frame 1 processed → connection closed → frame 2 dropped before the
// splitter.
// The filter IS the connection-scoped PubsubSession; exercise the owner-map surfaces directly
// through that interface: registry tracking with expired-weak pruning, per-channel bind /
// lookup / unbind (including the expired-owner lazy erase), the owner-first and fallback-sweep
// unsubscribe paths, and the subscription-change counters.
TEST_F(RedisProxyFilterTest, PubsubSessionOwnerMapMaintenance) {
  InSequence s;
  CommandSplitter::PubsubSession& session = *filter_;

  NiceMock<MockUpstreamSubscriptionCallbacks> ucb;
  NiceMock<Random::MockRandomGenerator> rng;
  NiceMock<Event::MockDispatcher> session_dispatcher;
  auto reg1 = std::make_shared<SubscriptionRegistry>(ucb, rng, session_dispatcher);
  auto reg2 = std::make_shared<SubscriptionRegistry>(ucb, rng, session_dispatcher);

  session.setSubscriptionRegistry(reg1);
  session.setSubscriptionRegistry(reg1); // already tracked — no duplicate
  {
    auto ephemeral = std::make_shared<SubscriptionRegistry>(ucb, rng, session_dispatcher);
    session.setSubscriptionRegistry(ephemeral);
  } // expires -> next mutation prunes the dead weak ref
  session.setSubscriptionRegistry(reg2);

  session.bindSubscriptionRegistryForChannel("ch", reg1);
  EXPECT_EQ(reg1, session.subscriptionRegistryForChannel("ch"));
  EXPECT_EQ(nullptr, session.subscriptionRegistryForChannel("unbound"));
  {
    auto ephemeral = std::make_shared<SubscriptionRegistry>(ucb, rng, session_dispatcher);
    session.bindSubscriptionRegistryForChannel("dead", ephemeral);
  }
  // Owner expired without unbind: lookup prunes the dead entry and reports no owner.
  EXPECT_EQ(nullptr, session.subscriptionRegistryForChannel("dead"));
  session.unbindSubscriptionRegistryForChannel("ch");
  EXPECT_EQ(nullptr, session.subscriptionRegistryForChannel("ch"));

  auto subscriber = session.downstreamSubscriber();
  ASSERT_NE(nullptr, subscriber);
  std::vector<Common::Redis::RespValue> preserved;
  // Owner-first path: bound registry unsubscribes (a no-op here) and the binding drops since the
  // subscriber never held the channel.
  session.bindSubscriptionRegistryForChannel("owned", reg1);
  session.unsubscribeChannelAcrossRegistries("owned", subscriber, preserved);
  EXPECT_EQ(nullptr, session.subscriptionRegistryForChannel("owned"));
  // Fallback sweep: no binding — every tracked registry (live and expired) is walked.
  session.unsubscribeChannelAcrossRegistries("unowned", subscriber, preserved);
  EXPECT_TRUE(preserved.empty());

  session.onPubsubSubscriptionChange(2);
  session.onPubsubSubscriptionChange(-2);
  session.onPubsubSubscriptionChange(0);
  EXPECT_EQ(2UL, config_->stats_.pubsub_subscribe_total_.value());
  EXPECT_EQ(2UL, config_->stats_.pubsub_unsubscribe_total_.value());
}

TEST_F(RedisProxyFilterTest, H1DropsFrameDispatchedAfterConnectionClosedMidDecode) {
  // The connection is Open until frame 1's processing closes it; a mutable flag lets state() flip
  // mid-decode without fighting gmock expectation ordering.
  bool conn_open = true;
  ON_CALL(filter_callbacks_.connection_, state()).WillByDefault(Invoke([&conn_open]() {
    return conn_open ? Network::Connection::State::Open : Network::Connection::State::Closed;
  }));

  auto* handle1 = new CommandSplitter::MockSplitRequest();

  Buffer::OwnedImpl fake_data;
  EXPECT_CALL(*decoder_, decode(Ref(fake_data))).WillOnce(Invoke([&](Buffer::Instance&) {
    // Frame 1 arrives while Open: normal makeRequest, counted active.
    Common::Redis::RespValuePtr request1(new Common::Redis::RespValue());
    EXPECT_CALL(splitter_, makeRequest_(Ref(*request1), _, _, _)).WillOnce(Return(handle1));
    decoder_callbacks_->onRespValue(std::move(request1));

    // Frame 1's processing synchronously closed the connection: onEvent(LocalClose) runs inline,
    // cancelling the in-flight request and leaving the connection non-Open.
    conn_open = false;
    EXPECT_CALL(*handle1, cancel());
    filter_->onEvent(Network::ConnectionEvent::LocalClose);

    // Frame 2, still dispatched by the same decode loop, must be dropped BEFORE the splitter: no
    // second makeRequest, no PendingRequest, no subscriber created on the closed connection.
    Common::Redis::RespValuePtr request2(new Common::Redis::RespValue());
    EXPECT_CALL(splitter_, makeRequest_(Ref(*request2), _, _, _)).Times(0);
    decoder_callbacks_->onRespValue(std::move(request2));
  }));
  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(fake_data, false));

  // Exactly one request was ever created (frame 1); the close cancelled it and frame 2 never became
  // a request. pending_requests_ is empty, so the ~ProxyFilter ASSERT and the fixture's gauge-zero
  // destructor check both hold.
  EXPECT_EQ(1UL, config_->stats_.downstream_rq_total_.value());
  EXPECT_EQ(0UL, config_->stats_.downstream_rq_active_.value());
}

// Covers the setProtocolVersion(Resp2) true branch of the onResponse re-stamp: a reply produced
// for a request that was created while the connection was RESP2 must be encoded in RESP2 even
// after the connection later upgrades to RESP3, then the encoder must be restored to RESP3.
TEST_F(RedisProxyFilterTestWithTwoCallbacks, ResponseReStampedToCreationVersionAcrossHello3) {
  Buffer::OwnedImpl fake_data;
  EXPECT_CALL(*decoder_, decode(Ref(fake_data)))
      .WillOnce(Invoke(this, &RedisProxyFilterTestWithTwoCallbacks::decodeHelper));
  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(fake_data, false));

  // Both requests were created while downstream was RESP2, so each carries
  // resp_version_at_creation_ == 2. request2 now negotiates HELLO 3: the connection flips to RESP3
  // and request2 is re-stamped to 3, but request1 (created earlier) stays at 2. The encoder flip
  // performed here is swallowed by the NiceMock — it is asserted in the HELLO tests, not this one.
  request_callbacks2_->setDownstreamRespVersion(3);

  // request1's reply was produced for a RESP2 client. Although the connection is now RESP3, that
  // reply must be encoded in RESP2 (request1's creation version), and the encoder restored to RESP3
  // immediately afterward for the live connection.
  Common::Redis::RespValuePtr response1(new Common::Redis::RespValue());
  {
    InSequence s;
    EXPECT_CALL(*encoder_, setProtocolVersion(Common::Redis::RespProtocolVersion::Resp2));
    EXPECT_CALL(*encoder_, encode(Ref(*response1), _));
    EXPECT_CALL(*encoder_, setProtocolVersion(Common::Redis::RespProtocolVersion::Resp3));
    EXPECT_CALL(filter_callbacks_.connection_, write(_, _));
  }
  request_callbacks1_->onResponse(std::move(response1));

  // request2's own reply was created at RESP3 (== the current downstream version), so onResponse
  // encodes it directly with no version flip.
  Common::Redis::RespValuePtr response2(new Common::Redis::RespValue());
  Common::Redis::RespValue* response2_ptr = response2.get();
  EXPECT_CALL(*encoder_, encode(Ref(*response2_ptr), _));
  EXPECT_CALL(filter_callbacks_.connection_, write(_, _));
  request_callbacks2_->onResponse(std::move(response2));
}

TEST_F(RedisProxyFilterTestWithTwoCallbacks, OutOfOrderResponseDownstreamDisconnectBeforeFlush) {
  InSequence s;

  Buffer::OwnedImpl fake_data;
  EXPECT_CALL(*decoder_, decode(Ref(fake_data)))
      .WillOnce(Invoke(this, &RedisProxyFilterTestWithTwoCallbacks::decodeHelper));
  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(fake_data, false));

  EXPECT_EQ(2UL, config_->stats_.downstream_rq_total_.value());
  EXPECT_EQ(2UL, config_->stats_.downstream_rq_active_.value());

  Common::Redis::RespValuePtr response2(new Common::Redis::RespValue());
  request_callbacks2_->onResponse(std::move(response2));
  EXPECT_CALL(*request_handle1_, cancel());

  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
}

TEST_F(RedisProxyFilterTest, DownstreamDisconnectWithActive) {
  InSequence s;

  Buffer::OwnedImpl fake_data;
  CommandSplitter::MockSplitRequest* request_handle1 = new CommandSplitter::MockSplitRequest();
  CommandSplitter::SplitCallbacks* request_callbacks1;
  EXPECT_CALL(*decoder_, decode(Ref(fake_data))).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    Common::Redis::RespValuePtr request1(new Common::Redis::RespValue());
    EXPECT_CALL(splitter_, makeRequest_(Ref(*request1), _, _, _))
        .WillOnce(DoAll(WithArg<1>(SaveArgAddress(&request_callbacks1)), Return(request_handle1)));
    decoder_callbacks_->onRespValue(std::move(request1));
  }));
  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(fake_data, false));

  EXPECT_CALL(*request_handle1, cancel());
  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
}

TEST_F(RedisProxyFilterTest, ImmediateResponse) {
  InSequence s;

  Buffer::OwnedImpl fake_data;
  Common::Redis::RespValuePtr request1(new Common::Redis::RespValue());
  EXPECT_CALL(*decoder_, decode(Ref(fake_data))).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    decoder_callbacks_->onRespValue(std::move(request1));
  }));
  EXPECT_CALL(splitter_, makeRequest_(Ref(*request1), _, _, _))
      .WillOnce(Invoke([&](const Common::Redis::RespValue&,
                           CommandSplitter::SplitCallbacks& callbacks, Event::Dispatcher&,
                           const StreamInfo::StreamInfo&) -> CommandSplitter::SplitRequest* {
        Common::Redis::RespValuePtr error(new Common::Redis::RespValue());
        error->type(Common::Redis::RespType::Error);
        error->asString() = "no healthy upstream";
        EXPECT_CALL(*encoder_, encode(Eq(ByRef(*error)), _));
        EXPECT_CALL(filter_callbacks_.connection_, write(_, _));
        callbacks.onResponse(std::move(error));
        return nullptr;
      }));

  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(fake_data, false));
  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
}

// Pipelined GET (still pending upstream) + SUBSCRIBE that fails locally must serialize on the
// wire: GET reply, then SUBSCRIBE error. Local pub/sub error frames are RESP Errors (not
// Push) and must traverse the FIFO. The simulated splitter mirrors the real splitter's terminal
// respond(): the SUBSCRIBE handler's frame batch is handed over in one call, buffered on the
// SUBSCRIBE FIFO entry, and only flushed once the earlier GET entry completes — so the SUBSCRIBE
// error can never overtake the GET reply nor be dropped when the GET entry is popped.
TEST_F(RedisProxyFilterTest, PipelinedGetThenSubscribeErrorPreservesFifo) {
  InSequence s;

  Buffer::OwnedImpl fake_data;
  CommandSplitter::MockSplitRequest* get_handle = new CommandSplitter::MockSplitRequest();
  CommandSplitter::SplitCallbacks* get_callbacks{};
  CommandSplitter::SplitCallbacks* sub_callbacks{};

  // Decode two pipelined commands in one onData pass: GET (returns a live SplitRequest) +
  // SUBSCRIBE (synchronously runs the splitter's terminal respond() with its accumulated frame
  // batch, the same shape the real SubscriptionRequest::create now uses).
  EXPECT_CALL(*decoder_, decode(Ref(fake_data))).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    Common::Redis::RespValuePtr get_req(new Common::Redis::RespValue());
    EXPECT_CALL(splitter_, makeRequest_(Ref(*get_req), _, _, _))
        .WillOnce(DoAll(WithArg<1>(SaveArgAddress(&get_callbacks)), Return(get_handle)));
    decoder_callbacks_->onRespValue(std::move(get_req));

    Common::Redis::RespValuePtr sub_req(new Common::Redis::RespValue());
    EXPECT_CALL(splitter_, makeRequest_(Ref(*sub_req), _, _, _))
        .WillOnce(Invoke([&](const Common::Redis::RespValue&, CommandSplitter::SplitCallbacks& cbs,
                             Event::Dispatcher&,
                             const StreamInfo::StreamInfo&) -> CommandSplitter::SplitRequest* {
          sub_callbacks = &cbs;
          Common::Redis::RespValuePtr err(new Common::Redis::RespValue());
          err->type(Common::Redis::RespType::Error);
          err->asString() = "ERR no route for pub/sub target 'chat'";
          // Terminal: hand the per-channel -ERR to respond() as a one-frame batch, which buffers
          // it and marks the SUBSCRIBE FIFO entry done. Front-of-FIFO is still the in-flight GET,
          // so flushReadyResponses must NOT pop or write anything yet — pin that with the explicit
          // Times(0) on encode/write below being satisfied during this onData pass.
          CommandSplitter::RespValueFrames frames;
          frames.push_back(std::move(err));
          cbs.respond(std::move(frames));
          return nullptr;
        }));
    decoder_callbacks_->onRespValue(std::move(sub_req));
  }));
  // No encode and no connection.write during the decode pass — both must wait for the GET
  // upstream reply to land below.
  EXPECT_CALL(*encoder_, encode(_, _)).Times(0);
  EXPECT_CALL(filter_callbacks_.connection_, write(_, _)).Times(0);
  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(fake_data, false));
  testing::Mock::VerifyAndClearExpectations(encoder_);
  testing::Mock::VerifyAndClearExpectations(&filter_callbacks_.connection_);

  // Now drive the GET reply. The flush should encode GET reply first, then drain the queued
  // SUBSCRIBE error from the next FIFO entry — both in the same connection.write batch.
  Common::Redis::RespValue get_reply;
  get_reply.type(Common::Redis::RespType::SimpleString);
  get_reply.asString() = "OK";
  Common::Redis::RespValue sub_err;
  sub_err.type(Common::Redis::RespType::Error);
  sub_err.asString() = "ERR no route for pub/sub target 'chat'";
  testing::Sequence write_seq;
  EXPECT_CALL(*encoder_, encode(Eq(ByRef(get_reply)), _)).InSequence(write_seq);
  EXPECT_CALL(*encoder_, encode(Eq(ByRef(sub_err)), _)).InSequence(write_seq);
  EXPECT_CALL(filter_callbacks_.connection_, write(_, _));
  Common::Redis::RespValuePtr get_reply_ptr(new Common::Redis::RespValue(get_reply));
  get_callbacks->onResponse(std::move(get_reply_ptr));

  // gMock's leak detector occasionally incorrectly attributes get_handle's destruction (it
  // is owned by PendingRequest::request_handle_ and reset on respond above).
  // Allow-leak so the test fails only on actual FIFO assertions.
  testing::Mock::AllowLeak(get_handle);
  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
}

TEST_F(RedisProxyFilterTest, ProtocolError) {
  InSequence s;

  Buffer::OwnedImpl fake_data;
  EXPECT_CALL(*decoder_, decode(Ref(fake_data))).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    throw Common::Redis::ProtocolError("error");
  }));

  Common::Redis::RespValue error;
  error.type(Common::Redis::RespType::Error);
  error.asString() = "downstream protocol error";
  EXPECT_CALL(*encoder_, encode(Eq(ByRef(error)), _));
  EXPECT_CALL(filter_callbacks_.connection_, write(_, _));
  EXPECT_CALL(filter_callbacks_.connection_, close(Network::ConnectionCloseType::NoFlush));
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(fake_data, false));

  EXPECT_EQ(1UL, store_.counter("redis.foo.downstream_cx_protocol_error").value());
}

TEST_F(RedisProxyFilterTest, AuthWhenNotRequired) {
  InSequence s;

  Buffer::OwnedImpl fake_data;
  Common::Redis::RespValuePtr request(new Common::Redis::RespValue());
  EXPECT_CALL(*decoder_, decode(Ref(fake_data))).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    decoder_callbacks_->onRespValue(std::move(request));
  }));
  EXPECT_CALL(splitter_, makeRequest_(Ref(*request), _, _, _))
      .WillOnce(Invoke([&](const Common::Redis::RespValue&,
                           CommandSplitter::SplitCallbacks& callbacks, Event::Dispatcher&,
                           const StreamInfo::StreamInfo&) -> CommandSplitter::SplitRequest* {
        EXPECT_TRUE(callbacks.connectionAllowed());
        Common::Redis::RespValuePtr error(new Common::Redis::RespValue());
        error->type(Common::Redis::RespType::Error);
        error->asString() = "ERR Client sent AUTH, but no password is set";
        EXPECT_CALL(*encoder_, encode(Eq(ByRef(*error)), _));
        EXPECT_CALL(filter_callbacks_.connection_, write(_, _));
        callbacks.onAuth("foo");
        // callbacks cannot be accessed now.
        EXPECT_TRUE(filter_->connectionAllowed());
        return nullptr;
      }));

  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(fake_data, false));
}

TEST_F(RedisProxyFilterTest, AuthAclWhenNotRequired) {
  InSequence s;

  Buffer::OwnedImpl fake_data;
  Common::Redis::RespValuePtr request(new Common::Redis::RespValue());
  EXPECT_CALL(*decoder_, decode(Ref(fake_data))).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    decoder_callbacks_->onRespValue(std::move(request));
  }));
  EXPECT_CALL(splitter_, makeRequest_(Ref(*request), _, _, _))
      .WillOnce(Invoke([&](const Common::Redis::RespValue&,
                           CommandSplitter::SplitCallbacks& callbacks, Event::Dispatcher&,
                           const StreamInfo::StreamInfo&) -> CommandSplitter::SplitRequest* {
        EXPECT_TRUE(callbacks.connectionAllowed());
        Common::Redis::RespValuePtr error(new Common::Redis::RespValue());
        error->type(Common::Redis::RespType::Error);
        error->asString() = "ERR Client sent AUTH, but no username-password pair is set";
        EXPECT_CALL(*encoder_, encode(Eq(ByRef(*error)), _));
        EXPECT_CALL(filter_callbacks_.connection_, write(_, _));
        callbacks.onAuth("foo", "bar");
        // callbacks cannot be accessed now.
        EXPECT_TRUE(filter_->connectionAllowed());
        return nullptr;
      }));

  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(fake_data, false));
}

// First-non-HELLO gate on a RESP3 listener (ProxyFilter::processRespValue).

const std::string resp3_listener_config = R"EOF(
prefix_routes:
  catch_all_route:
      cluster: fake_cluster
stat_prefix: foo
protocol_version: RESP3
settings:
  op_timeout: 0.01s
)EOF";

const std::string resp3_listener_with_auth_config = R"EOF(
prefix_routes:
  catch_all_route:
      cluster: fake_cluster
stat_prefix: foo
protocol_version: RESP3
settings:
  op_timeout: 0.01s
downstream_auth_password:
  inline_string: somepassword
)EOF";

class RedisProxyFilterResp3Test : public RedisProxyFilterTest {
public:
  RedisProxyFilterResp3Test() : RedisProxyFilterTest(resp3_listener_config) {}

  // Build a RespValue Array of BulkString command tokens (e.g. {"get", "foo"}).
  static Common::Redis::RespValuePtr makeCommand(const std::vector<std::string>& tokens) {
    auto value = std::make_unique<Common::Redis::RespValue>();
    value->type(Common::Redis::RespType::Array);
    std::vector<Common::Redis::RespValue> elements(tokens.size());
    for (size_t i = 0; i < tokens.size(); ++i) {
      elements[i].type(Common::Redis::RespType::BulkString);
      elements[i].asString() = tokens[i];
    }
    value->asArray().swap(elements);
    return value;
  }
};

class RedisProxyFilterResp3WithAuthTest : public RedisProxyFilterTest {
public:
  RedisProxyFilterResp3WithAuthTest() : RedisProxyFilterTest(resp3_listener_with_auth_config) {}

  static Common::Redis::RespValuePtr makeCommand(const std::vector<std::string>& tokens) {
    return RedisProxyFilterResp3Test::makeCommand(tokens);
  }
};

// RESP3 listener + fresh connection + ``GET foo`` → ``-NOPROTO``. The splitter must not be
// invoked at all: the gate runs before ``splitter_.makeRequest`` so a strict ``EXPECT_CALL(0)``
// pins that property.
TEST_F(RedisProxyFilterResp3Test, GetBeforeHelloRejectedNoproto) {
  InSequence s;
  Buffer::OwnedImpl fake_data;
  Common::Redis::RespValuePtr request = makeCommand({"get", "foo"});
  EXPECT_CALL(*decoder_, decode(Ref(fake_data))).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    decoder_callbacks_->onRespValue(std::move(request));
  }));
  EXPECT_CALL(splitter_, makeRequest_(_, _, _, _)).Times(0);

  Common::Redis::RespValue expected_err;
  expected_err.type(Common::Redis::RespType::Error);
  expected_err.asString() = "NOPROTO unsupported protocol version";
  EXPECT_CALL(*encoder_, encode(Eq(ByRef(expected_err)), _));
  EXPECT_CALL(filter_callbacks_.connection_, write(_, _));

  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(fake_data, false));
  // The pre-HELLO gate bumps the operational counter.
  EXPECT_EQ(1UL, config_->stats_.downstream_rq_noproto_.value());
}

// RESP3 listener + ``BOGUSCMD`` before HELLO → ``-NOPROTO`` (NOT the splitter's
// unsupported-command error). The protocol gate takes precedence over the splitter's
// ``ERR unknown command`` path, so the operator-facing error always reads "client did not
// handshake" rather than "client sent a typo".
TEST_F(RedisProxyFilterResp3Test, UnknownCommandBeforeHelloReturnsNoproto) {
  InSequence s;
  Buffer::OwnedImpl fake_data;
  Common::Redis::RespValuePtr request = makeCommand({"boguscmd"});
  EXPECT_CALL(*decoder_, decode(Ref(fake_data))).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    decoder_callbacks_->onRespValue(std::move(request));
  }));
  EXPECT_CALL(splitter_, makeRequest_(_, _, _, _)).Times(0);

  Common::Redis::RespValue expected_err;
  expected_err.type(Common::Redis::RespType::Error);
  expected_err.asString() = "NOPROTO unsupported protocol version";
  EXPECT_CALL(*encoder_, encode(Eq(ByRef(expected_err)), _));
  EXPECT_CALL(filter_callbacks_.connection_, write(_, _));

  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(fake_data, false));
}

// RESP3 listener: AUTH pre-HELLO is permitted; a following GET is still gated -NOPROTO
// (AUTH does not promote the connection's RESP version).
TEST_F(RedisProxyFilterResp3Test, AuthBeforeHelloAllowedThenGetStillNoproto) {
  Buffer::OwnedImpl fake_data;
  Common::Redis::RespValuePtr auth_request = makeCommand({"auth", "secret"});
  Common::Redis::RespValuePtr get_request = makeCommand({"get", "foo"});
  Common::Redis::RespValue* auth_ptr = auth_request.get();

  Common::Redis::RespValue auth_reply;
  auth_reply.type(Common::Redis::RespType::SimpleString);
  auth_reply.asString() = "OK";

  Common::Redis::RespValue expected_err;
  expected_err.type(Common::Redis::RespType::Error);
  expected_err.asString() = "NOPROTO unsupported protocol version";

  EXPECT_CALL(*decoder_, decode(Ref(fake_data))).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    decoder_callbacks_->onRespValue(std::move(auth_request));
    decoder_callbacks_->onRespValue(std::move(get_request));
  }));

  // AUTH flows through the splitter (allowlisted by the gate); the splitter mock fabricates a
  // ``+OK`` reply.
  EXPECT_CALL(splitter_, makeRequest_(Ref(*auth_ptr), _, _, _))
      .WillOnce(Invoke([&](const Common::Redis::RespValue&,
                           CommandSplitter::SplitCallbacks& callbacks, Event::Dispatcher&,
                           const StreamInfo::StreamInfo&) -> CommandSplitter::SplitRequest* {
        Common::Redis::RespValuePtr reply(new Common::Redis::RespValue(auth_reply));
        callbacks.onResponse(std::move(reply));
        return nullptr;
      }));

  EXPECT_CALL(*encoder_, encode(Eq(ByRef(auth_reply)), _));
  EXPECT_CALL(*encoder_, encode(Eq(ByRef(expected_err)), _));
  EXPECT_CALL(filter_callbacks_.connection_, write(_, _)).Times(2);

  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(fake_data, false));
}

// Auth-required RESP3 listener + ``GET foo`` before any HELLO → ``-NOPROTO``, NOT ``-NOAUTH``.
// The gate runs ahead of the splitter, which is where the auth-required check lives, so the
// gate precedence pins "operator sees handshake-missing, not bad-credentials".
TEST_F(RedisProxyFilterResp3WithAuthTest, UnauthedGetBeforeHelloRejectedNoprotoNotNoauth) {
  InSequence s;
  Buffer::OwnedImpl fake_data;
  Common::Redis::RespValuePtr request = makeCommand({"get", "foo"});
  EXPECT_CALL(*decoder_, decode(Ref(fake_data))).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    decoder_callbacks_->onRespValue(std::move(request));
  }));
  EXPECT_CALL(splitter_, makeRequest_(_, _, _, _)).Times(0);

  Common::Redis::RespValue expected_err;
  expected_err.type(Common::Redis::RespType::Error);
  expected_err.asString() = "NOPROTO unsupported protocol version";
  EXPECT_CALL(*encoder_, encode(Eq(ByRef(expected_err)), _));
  EXPECT_CALL(filter_callbacks_.connection_, write(_, _));

  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(fake_data, false));
}

// RESP2 listener + ``GET foo`` with no prior HELLO → normal splitter dispatch. The gate must
// not engage on a RESP2 listener (no implicit HELLO 3 requirement). Lock the default behavior
// against accidental gate-on-Resp2 regressions. The mock splitter responds immediately so the
// filter's ``pending_requests_`` drains cleanly through ``onResponse`` (otherwise
// ``ProxyFilter::~ProxyFilter`` asserts on a leaked PendingRequest).
TEST_F(RedisProxyFilterTest, GetBeforeHelloOnResp2ListenerDispatchesNormally) {
  InSequence s;
  Buffer::OwnedImpl fake_data;
  Common::Redis::RespValuePtr request = RedisProxyFilterResp3Test::makeCommand({"get", "foo"});
  Common::Redis::RespValue* request_ptr = request.get();
  EXPECT_CALL(*decoder_, decode(Ref(fake_data))).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    decoder_callbacks_->onRespValue(std::move(request));
  }));
  EXPECT_CALL(splitter_, makeRequest_(Ref(*request_ptr), _, _, _))
      .WillOnce(Invoke([&](const Common::Redis::RespValue&,
                           CommandSplitter::SplitCallbacks& callbacks, Event::Dispatcher&,
                           const StreamInfo::StreamInfo&) -> CommandSplitter::SplitRequest* {
        Common::Redis::RespValuePtr reply(new Common::Redis::RespValue());
        reply->type(Common::Redis::RespType::SimpleString);
        reply->asString() = "OK";
        EXPECT_CALL(*encoder_, encode(Eq(ByRef(*reply)), _));
        EXPECT_CALL(filter_callbacks_.connection_, write(_, _));
        callbacks.onResponse(std::move(reply));
        return nullptr;
      }));

  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(fake_data, false));
}

const std::string downstream_auth_password_config = R"EOF(
prefix_routes:
  catch_all_route:
      cluster: fake_cluster
stat_prefix: foo
settings:
  op_timeout: 0.01s
downstream_auth_password:
  inline_string: somepassword
)EOF";

class RedisProxyFilterWithAuthPasswordTest : public RedisProxyFilterTest {
public:
  RedisProxyFilterWithAuthPasswordTest() : RedisProxyFilterTest(downstream_auth_password_config) {}
};

TEST_F(RedisProxyFilterWithAuthPasswordTest, AuthPasswordCorrect) {
  InSequence s;

  Buffer::OwnedImpl fake_data;
  Common::Redis::RespValuePtr request(new Common::Redis::RespValue());
  EXPECT_CALL(*decoder_, decode(Ref(fake_data))).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    decoder_callbacks_->onRespValue(std::move(request));
  }));
  EXPECT_CALL(splitter_, makeRequest_(Ref(*request), _, _, _))
      .WillOnce(Invoke([&](const Common::Redis::RespValue&,
                           CommandSplitter::SplitCallbacks& callbacks, Event::Dispatcher&,
                           const StreamInfo::StreamInfo&) -> CommandSplitter::SplitRequest* {
        EXPECT_FALSE(callbacks.connectionAllowed());
        Common::Redis::RespValuePtr reply(new Common::Redis::RespValue());
        reply->type(Common::Redis::RespType::SimpleString);
        reply->asString() = "OK";
        EXPECT_CALL(*encoder_, encode(Eq(ByRef(*reply)), _));
        EXPECT_CALL(filter_callbacks_.connection_, write(_, _));
        callbacks.onAuth("somepassword");
        // callbacks cannot be accessed now.
        EXPECT_TRUE(filter_->connectionAllowed());
        return nullptr;
      }));

  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(fake_data, false));
}

TEST_F(RedisProxyFilterWithAuthPasswordTest, AuthPasswordIncorrect) {
  InSequence s;

  Buffer::OwnedImpl fake_data;
  Common::Redis::RespValuePtr request(new Common::Redis::RespValue());
  EXPECT_CALL(*decoder_, decode(Ref(fake_data))).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    decoder_callbacks_->onRespValue(std::move(request));
  }));
  EXPECT_CALL(splitter_, makeRequest_(Ref(*request), _, _, _))
      .WillOnce(Invoke([&](const Common::Redis::RespValue&,
                           CommandSplitter::SplitCallbacks& callbacks, Event::Dispatcher&,
                           const StreamInfo::StreamInfo&) -> CommandSplitter::SplitRequest* {
        EXPECT_FALSE(callbacks.connectionAllowed());
        Common::Redis::RespValuePtr reply(new Common::Redis::RespValue());
        reply->type(Common::Redis::RespType::Error);
        reply->asString() = "ERR invalid password";
        EXPECT_CALL(*encoder_, encode(Eq(ByRef(*reply)), _));
        EXPECT_CALL(filter_callbacks_.connection_, write(_, _));
        callbacks.onAuth("wrongpassword");
        // callbacks cannot be accessed now.
        EXPECT_FALSE(filter_->connectionAllowed());
        return nullptr;
      }));

  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(fake_data, false));
}

// ``AUTH default <password>`` succeeds against a ``downstream_auth_password``-only listener.
// Exercises the ``default`` username synonym branch in ``onAuth(username, password)``: Redis 6
// ACLs treat an empty configured username + the literal ``default`` supplied by the client as
// a match.
TEST_F(RedisProxyFilterWithAuthPasswordTest, AuthCommandWithDefaultUsernameAcceptsCorrectPassword) {
  InSequence s;

  Buffer::OwnedImpl fake_data;
  Common::Redis::RespValuePtr request(new Common::Redis::RespValue());
  EXPECT_CALL(*decoder_, decode(Ref(fake_data))).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    decoder_callbacks_->onRespValue(std::move(request));
  }));
  EXPECT_CALL(splitter_, makeRequest_(Ref(*request), _, _, _))
      .WillOnce(Invoke([&](const Common::Redis::RespValue&,
                           CommandSplitter::SplitCallbacks& callbacks, Event::Dispatcher&,
                           const StreamInfo::StreamInfo&) -> CommandSplitter::SplitRequest* {
        EXPECT_FALSE(callbacks.connectionAllowed());
        Common::Redis::RespValuePtr reply(new Common::Redis::RespValue());
        reply->type(Common::Redis::RespType::SimpleString);
        reply->asString() = "OK";
        EXPECT_CALL(*encoder_, encode(Eq(ByRef(*reply)), _));
        EXPECT_CALL(filter_callbacks_.connection_, write(_, _));
        callbacks.onAuth("default", "somepassword");
        EXPECT_TRUE(filter_->connectionAllowed());
        return nullptr;
      }));

  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(fake_data, false));
}

// ``HELLO N AUTH default <password>`` against a ``downstream_auth_password``-only listener
// reaches the local-credentials match path of ``attemptDownstreamAuthInline`` and returns
// ``Allowed`` after flipping ``connection_allowed_`` to true. The trailing onResponse call
// mimics what the production splitter does after the Allowed return (emit the HELLO Map),
// which drains the in-flight PendingRequest so the filter dtor's empty-queue ASSERT holds.
TEST_F(RedisProxyFilterWithAuthPasswordTest, AttemptInlineAuthAllowedWithLocalPassword) {
  InSequence s;

  Buffer::OwnedImpl fake_data;
  Common::Redis::RespValuePtr request(new Common::Redis::RespValue());
  EXPECT_CALL(*decoder_, decode(Ref(fake_data))).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    decoder_callbacks_->onRespValue(std::move(request));
  }));
  EXPECT_CALL(splitter_, makeRequest_(Ref(*request), _, _, _))
      .WillOnce(Invoke([&](const Common::Redis::RespValue&,
                           CommandSplitter::SplitCallbacks& callbacks, Event::Dispatcher&,
                           const StreamInfo::StreamInfo&) -> CommandSplitter::SplitRequest* {
        EXPECT_FALSE(callbacks.connectionAllowed());
        // ``default`` is the Redis 6 ACL synonym for the empty configured username.
        EXPECT_EQ(CommandSplitter::SplitCallbacks::AuthAttempt::Allowed,
                  callbacks.attemptDownstreamAuthInline("default", "somepassword", 3));
        EXPECT_TRUE(filter_->connectionAllowed());
        Common::Redis::RespValuePtr reply(new Common::Redis::RespValue());
        reply->type(Common::Redis::RespType::SimpleString);
        reply->asString() = "OK";
        EXPECT_CALL(*encoder_, encode(Eq(ByRef(*reply)), _));
        EXPECT_CALL(filter_callbacks_.connection_, write(_, _));
        callbacks.onResponse(std::move(reply));
        return nullptr;
      }));

  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(fake_data, false));
}

// ``HELLO N AUTH user wrong`` against a ``downstream_auth_password``-only listener reaches
// the local-credentials no-match arm of ``attemptDownstreamAuthInline`` and returns
// ``Denied`` while leaving ``connection_allowed_`` false. Same code region as the test
// above; the trailing onResponse mimics the splitter emitting WRONGPASS after a Denied
// return, which drains the PendingRequest.
TEST_F(RedisProxyFilterWithAuthPasswordTest, AttemptInlineAuthDeniedWithWrongLocalPassword) {
  InSequence s;

  Buffer::OwnedImpl fake_data;
  Common::Redis::RespValuePtr request(new Common::Redis::RespValue());
  EXPECT_CALL(*decoder_, decode(Ref(fake_data))).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    decoder_callbacks_->onRespValue(std::move(request));
  }));
  EXPECT_CALL(splitter_, makeRequest_(Ref(*request), _, _, _))
      .WillOnce(Invoke([&](const Common::Redis::RespValue&,
                           CommandSplitter::SplitCallbacks& callbacks, Event::Dispatcher&,
                           const StreamInfo::StreamInfo&) -> CommandSplitter::SplitRequest* {
        EXPECT_FALSE(callbacks.connectionAllowed());
        EXPECT_EQ(CommandSplitter::SplitCallbacks::AuthAttempt::Denied,
                  callbacks.attemptDownstreamAuthInline("default", "wrong", 3));
        EXPECT_FALSE(filter_->connectionAllowed());
        Common::Redis::RespValuePtr reply(new Common::Redis::RespValue());
        reply->type(Common::Redis::RespType::Error);
        reply->asString() = "WRONGPASS invalid username-password pair";
        EXPECT_CALL(*encoder_, encode(Eq(ByRef(*reply)), _));
        EXPECT_CALL(filter_callbacks_.connection_, write(_, _));
        callbacks.onResponse(std::move(reply));
        return nullptr;
      }));

  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(fake_data, false));
}

// ``HELLO N AUTH ...`` against a listener with NO downstream credentials configured emits the
// same "no username-password pair is set" ERR as a username+password AUTH command and returns
// ``ImplOwnsResponse`` (the implementation owns the response). Default fixture (no auth
// configured).
TEST_F(RedisProxyFilterTest, AttemptInlineAuthNoConfiguredCredsEmitsNoUsernamePasswordPairError) {
  InSequence s;

  Buffer::OwnedImpl fake_data;
  Common::Redis::RespValuePtr request(new Common::Redis::RespValue());
  EXPECT_CALL(*decoder_, decode(Ref(fake_data))).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    decoder_callbacks_->onRespValue(std::move(request));
  }));
  EXPECT_CALL(splitter_, makeRequest_(Ref(*request), _, _, _))
      .WillOnce(Invoke([&](const Common::Redis::RespValue&,
                           CommandSplitter::SplitCallbacks& callbacks, Event::Dispatcher&,
                           const StreamInfo::StreamInfo&) -> CommandSplitter::SplitRequest* {
        Common::Redis::RespValue reply;
        reply.type(Common::Redis::RespType::Error);
        reply.asString() = "ERR Client sent AUTH, but no username-password pair is set";
        EXPECT_CALL(*encoder_, encode(Eq(ByRef(reply)), _));
        EXPECT_CALL(filter_callbacks_.connection_, write(_, _));
        // The impl emits the error itself and returns ImplOwnsResponse; the splitter emits nothing.
        // connectionAllowed() stays true throughout — this fixture configures no downstream auth,
        // so the connection was never gated; the HELLO AUTH is rejected because no downstream
        // username/password credentials are configured to validate it against. Assert the state
        // before and after to pin that the no-credentials path does not flip connection_allowed_.
        // Query the filter, not the PendingRequest: attemptDownstreamAuthInline emits the error via
        // onResponse, which pops and destroys this request, so ``callbacks`` dangles afterward.
        EXPECT_TRUE(filter_->connectionAllowed());
        EXPECT_EQ(CommandSplitter::SplitCallbacks::AuthAttempt::ImplOwnsResponse,
                  callbacks.attemptDownstreamAuthInline("alice", "secret", 3));
        EXPECT_TRUE(filter_->connectionAllowed());
        return nullptr;
      }));

  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(fake_data, false));
}

const std::string downstream_multiple_auth_passwords_config = R"EOF(
prefix_routes:
  catch_all_route:
      cluster: fake_cluster
stat_prefix: foo
settings:
  op_timeout: 0.01s
downstream_auth_password:
  inline_string: somepassword
downstream_auth_passwords:
-  inline_string: newpassword1
-  inline_string: newpassword2
)EOF";

class RedisProxyFilterWithMultipleAuthPasswordsTest : public RedisProxyFilterTest {
public:
  RedisProxyFilterWithMultipleAuthPasswordsTest()
      : RedisProxyFilterTest(downstream_multiple_auth_passwords_config) {}
};

TEST_F(RedisProxyFilterWithMultipleAuthPasswordsTest, AuthPasswordCorrect) {
  InSequence s;

  Buffer::OwnedImpl fake_data;
  Common::Redis::RespValuePtr request(new Common::Redis::RespValue());
  EXPECT_CALL(*decoder_, decode(Ref(fake_data))).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    decoder_callbacks_->onRespValue(std::move(request));
  }));
  EXPECT_CALL(splitter_, makeRequest_(Ref(*request), _, _, _))
      .WillOnce(Invoke([&](const Common::Redis::RespValue&,
                           CommandSplitter::SplitCallbacks& callbacks, Event::Dispatcher&,
                           const StreamInfo::StreamInfo&) -> CommandSplitter::SplitRequest* {
        EXPECT_FALSE(callbacks.connectionAllowed());
        Common::Redis::RespValuePtr reply(new Common::Redis::RespValue());
        reply->type(Common::Redis::RespType::SimpleString);
        reply->asString() = "OK";
        EXPECT_CALL(*encoder_, encode(Eq(ByRef(*reply)), _));
        EXPECT_CALL(filter_callbacks_.connection_, write(_, _));
        callbacks.onAuth("somepassword");
        // callbacks cannot be accessed now.
        EXPECT_TRUE(filter_->connectionAllowed());
        return nullptr;
      }));

  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(fake_data, false));
}

TEST_F(RedisProxyFilterWithMultipleAuthPasswordsTest, AuthNewPassword1Correct) {
  InSequence s;

  Buffer::OwnedImpl fake_data;
  Common::Redis::RespValuePtr request(new Common::Redis::RespValue());
  EXPECT_CALL(*decoder_, decode(Ref(fake_data))).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    decoder_callbacks_->onRespValue(std::move(request));
  }));
  EXPECT_CALL(splitter_, makeRequest_(Ref(*request), _, _, _))
      .WillOnce(Invoke([&](const Common::Redis::RespValue&,
                           CommandSplitter::SplitCallbacks& callbacks, Event::Dispatcher&,
                           const StreamInfo::StreamInfo&) -> CommandSplitter::SplitRequest* {
        EXPECT_FALSE(callbacks.connectionAllowed());
        Common::Redis::RespValuePtr reply(new Common::Redis::RespValue());
        reply->type(Common::Redis::RespType::SimpleString);
        reply->asString() = "OK";
        EXPECT_CALL(*encoder_, encode(Eq(ByRef(*reply)), _));
        EXPECT_CALL(filter_callbacks_.connection_, write(_, _));
        callbacks.onAuth("newpassword1");
        // callbacks cannot be accessed now.
        EXPECT_TRUE(filter_->connectionAllowed());
        return nullptr;
      }));

  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(fake_data, false));
}

TEST_F(RedisProxyFilterWithMultipleAuthPasswordsTest, AuthNewPassword2Correct) {
  InSequence s;

  Buffer::OwnedImpl fake_data;
  Common::Redis::RespValuePtr request(new Common::Redis::RespValue());
  EXPECT_CALL(*decoder_, decode(Ref(fake_data))).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    decoder_callbacks_->onRespValue(std::move(request));
  }));
  EXPECT_CALL(splitter_, makeRequest_(Ref(*request), _, _, _))
      .WillOnce(Invoke([&](const Common::Redis::RespValue&,
                           CommandSplitter::SplitCallbacks& callbacks, Event::Dispatcher&,
                           const StreamInfo::StreamInfo&) -> CommandSplitter::SplitRequest* {
        EXPECT_FALSE(callbacks.connectionAllowed());
        Common::Redis::RespValuePtr reply(new Common::Redis::RespValue());
        reply->type(Common::Redis::RespType::SimpleString);
        reply->asString() = "OK";
        EXPECT_CALL(*encoder_, encode(Eq(ByRef(*reply)), _));
        EXPECT_CALL(filter_callbacks_.connection_, write(_, _));
        callbacks.onAuth("newpassword2");
        // callbacks cannot be accessed now.
        EXPECT_TRUE(filter_->connectionAllowed());
        return nullptr;
      }));

  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(fake_data, false));
}

TEST_F(RedisProxyFilterWithMultipleAuthPasswordsTest, AuthPasswordIncorrect) {
  InSequence s;

  Buffer::OwnedImpl fake_data;
  Common::Redis::RespValuePtr request(new Common::Redis::RespValue());
  EXPECT_CALL(*decoder_, decode(Ref(fake_data))).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    decoder_callbacks_->onRespValue(std::move(request));
  }));
  EXPECT_CALL(splitter_, makeRequest_(Ref(*request), _, _, _))
      .WillOnce(Invoke([&](const Common::Redis::RespValue&,
                           CommandSplitter::SplitCallbacks& callbacks, Event::Dispatcher&,
                           const StreamInfo::StreamInfo&) -> CommandSplitter::SplitRequest* {
        EXPECT_FALSE(callbacks.connectionAllowed());
        Common::Redis::RespValuePtr reply(new Common::Redis::RespValue());
        reply->type(Common::Redis::RespType::Error);
        reply->asString() = "ERR invalid password";
        EXPECT_CALL(*encoder_, encode(Eq(ByRef(*reply)), _));
        EXPECT_CALL(filter_callbacks_.connection_, write(_, _));
        callbacks.onAuth("wrongpassword");
        // callbacks cannot be accessed now.
        EXPECT_FALSE(filter_->connectionAllowed());
        return nullptr;
      }));

  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(fake_data, false));
}

const std::string downstream_auth_acl_config = R"EOF(
prefix_routes:
  catch_all_route:
      cluster: fake_cluster
stat_prefix: foo
settings:
  op_timeout: 0.01s
downstream_auth_username:
  inline_string: someusername
downstream_auth_password:
  inline_string: somepassword
)EOF";

class RedisProxyFilterWithAuthAclTest : public RedisProxyFilterTest {
public:
  RedisProxyFilterWithAuthAclTest() : RedisProxyFilterTest(downstream_auth_acl_config) {}
};

TEST_F(RedisProxyFilterWithAuthAclTest, AuthAclCorrect) {
  InSequence s;

  Buffer::OwnedImpl fake_data;
  Common::Redis::RespValuePtr request(new Common::Redis::RespValue());
  EXPECT_CALL(*decoder_, decode(Ref(fake_data))).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    decoder_callbacks_->onRespValue(std::move(request));
  }));
  EXPECT_CALL(splitter_, makeRequest_(Ref(*request), _, _, _))
      .WillOnce(Invoke([&](const Common::Redis::RespValue&,
                           CommandSplitter::SplitCallbacks& callbacks, Event::Dispatcher&,
                           const StreamInfo::StreamInfo&) -> CommandSplitter::SplitRequest* {
        EXPECT_FALSE(callbacks.connectionAllowed());
        Common::Redis::RespValuePtr reply(new Common::Redis::RespValue());
        reply->type(Common::Redis::RespType::SimpleString);
        reply->asString() = "OK";
        EXPECT_CALL(*encoder_, encode(Eq(ByRef(*reply)), _));
        EXPECT_CALL(filter_callbacks_.connection_, write(_, _));
        callbacks.onAuth("someusername", "somepassword");
        // callbacks cannot be accessed now.
        EXPECT_TRUE(filter_->connectionAllowed());
        return nullptr;
      }));

  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(fake_data, false));
}

TEST_F(RedisProxyFilterWithAuthAclTest, AuthAclUsernameIncorrect) {
  InSequence s;

  Buffer::OwnedImpl fake_data;
  Common::Redis::RespValuePtr request(new Common::Redis::RespValue());
  EXPECT_CALL(*decoder_, decode(Ref(fake_data))).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    decoder_callbacks_->onRespValue(std::move(request));
  }));
  EXPECT_CALL(splitter_, makeRequest_(Ref(*request), _, _, _))
      .WillOnce(Invoke([&](const Common::Redis::RespValue&,
                           CommandSplitter::SplitCallbacks& callbacks, Event::Dispatcher&,
                           const StreamInfo::StreamInfo&) -> CommandSplitter::SplitRequest* {
        EXPECT_FALSE(callbacks.connectionAllowed());
        Common::Redis::RespValuePtr reply(new Common::Redis::RespValue());
        reply->type(Common::Redis::RespType::Error);
        reply->asString() = "WRONGPASS invalid username-password pair";
        EXPECT_CALL(*encoder_, encode(Eq(ByRef(*reply)), _));
        EXPECT_CALL(filter_callbacks_.connection_, write(_, _));
        callbacks.onAuth("wrongusername", "somepassword");
        // callbacks cannot be accessed now.
        EXPECT_FALSE(filter_->connectionAllowed());
        return nullptr;
      }));

  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(fake_data, false));
}

TEST_F(RedisProxyFilterWithAuthAclTest, AuthAclPasswordIncorrect) {
  InSequence s;

  Buffer::OwnedImpl fake_data;
  Common::Redis::RespValuePtr request(new Common::Redis::RespValue());
  EXPECT_CALL(*decoder_, decode(Ref(fake_data))).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    decoder_callbacks_->onRespValue(std::move(request));
  }));
  EXPECT_CALL(splitter_, makeRequest_(Ref(*request), _, _, _))
      .WillOnce(Invoke([&](const Common::Redis::RespValue&,
                           CommandSplitter::SplitCallbacks& callbacks, Event::Dispatcher&,
                           const StreamInfo::StreamInfo&) -> CommandSplitter::SplitRequest* {
        EXPECT_FALSE(callbacks.connectionAllowed());
        Common::Redis::RespValuePtr reply(new Common::Redis::RespValue());
        reply->type(Common::Redis::RespType::Error);
        reply->asString() = "WRONGPASS invalid username-password pair";
        EXPECT_CALL(*encoder_, encode(Eq(ByRef(*reply)), _));
        EXPECT_CALL(filter_callbacks_.connection_, write(_, _));
        callbacks.onAuth("someusername", "wrongpassword");
        // callbacks cannot be accessed now.
        EXPECT_FALSE(filter_->connectionAllowed());
        return nullptr;
      }));

  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(fake_data, false));
}

// ``HELLO N AUTH <user> <pass>`` against a listener with a configured username+password
// reaches the local-credentials check in ``attemptDownstreamAuthInline``: matching creds →
// Allowed + connection_allowed_.
TEST_F(RedisProxyFilterWithAuthAclTest, AttemptInlineAuthConfiguredUsernameCorrect) {
  InSequence s;

  Buffer::OwnedImpl fake_data;
  Common::Redis::RespValuePtr request(new Common::Redis::RespValue());
  EXPECT_CALL(*decoder_, decode(Ref(fake_data))).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    decoder_callbacks_->onRespValue(std::move(request));
  }));
  EXPECT_CALL(splitter_, makeRequest_(Ref(*request), _, _, _))
      .WillOnce(Invoke([&](const Common::Redis::RespValue&,
                           CommandSplitter::SplitCallbacks& callbacks, Event::Dispatcher&,
                           const StreamInfo::StreamInfo&) -> CommandSplitter::SplitRequest* {
        EXPECT_FALSE(callbacks.connectionAllowed());
        EXPECT_EQ(CommandSplitter::SplitCallbacks::AuthAttempt::Allowed,
                  callbacks.attemptDownstreamAuthInline("someusername", "somepassword", 3));
        EXPECT_TRUE(filter_->connectionAllowed());
        Common::Redis::RespValuePtr reply(new Common::Redis::RespValue());
        reply->type(Common::Redis::RespType::SimpleString);
        reply->asString() = "OK";
        EXPECT_CALL(*encoder_, encode(Eq(ByRef(*reply)), _));
        EXPECT_CALL(filter_callbacks_.connection_, write(_, _));
        callbacks.onResponse(std::move(reply));
        return nullptr;
      }));

  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(fake_data, false));
}

// Same fixture, wrong password → Denied, connection stays disallowed.
TEST_F(RedisProxyFilterWithAuthAclTest, AttemptInlineAuthConfiguredUsernameWrongPassword) {
  InSequence s;

  Buffer::OwnedImpl fake_data;
  Common::Redis::RespValuePtr request(new Common::Redis::RespValue());
  EXPECT_CALL(*decoder_, decode(Ref(fake_data))).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    decoder_callbacks_->onRespValue(std::move(request));
  }));
  EXPECT_CALL(splitter_, makeRequest_(Ref(*request), _, _, _))
      .WillOnce(Invoke([&](const Common::Redis::RespValue&,
                           CommandSplitter::SplitCallbacks& callbacks, Event::Dispatcher&,
                           const StreamInfo::StreamInfo&) -> CommandSplitter::SplitRequest* {
        EXPECT_EQ(CommandSplitter::SplitCallbacks::AuthAttempt::Denied,
                  callbacks.attemptDownstreamAuthInline("someusername", "wrongpassword", 3));
        EXPECT_FALSE(filter_->connectionAllowed());
        Common::Redis::RespValuePtr reply(new Common::Redis::RespValue());
        reply->type(Common::Redis::RespType::Error);
        reply->asString() = "WRONGPASS invalid username-password pair";
        EXPECT_CALL(*encoder_, encode(Eq(ByRef(*reply)), _));
        EXPECT_CALL(filter_callbacks_.connection_, write(_, _));
        callbacks.onResponse(std::move(reply));
        return nullptr;
      }));

  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(fake_data, false));
}

// Same fixture, configured username but the client supplies a DIFFERENT username → Denied. The
// ``username == configured`` comparison short-circuits before the password is examined, so a
// correct password does not rescue a wrong username. Covers the false branch of the
// configured-username comparison.
TEST_F(RedisProxyFilterWithAuthAclTest, AttemptInlineAuthConfiguredUsernameWrongUsername) {
  InSequence s;

  Buffer::OwnedImpl fake_data;
  Common::Redis::RespValuePtr request(new Common::Redis::RespValue());
  EXPECT_CALL(*decoder_, decode(Ref(fake_data))).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    decoder_callbacks_->onRespValue(std::move(request));
  }));
  EXPECT_CALL(splitter_, makeRequest_(Ref(*request), _, _, _))
      .WillOnce(Invoke([&](const Common::Redis::RespValue&,
                           CommandSplitter::SplitCallbacks& callbacks, Event::Dispatcher&,
                           const StreamInfo::StreamInfo&) -> CommandSplitter::SplitRequest* {
        // Correct password, wrong username: still Denied.
        EXPECT_EQ(CommandSplitter::SplitCallbacks::AuthAttempt::Denied,
                  callbacks.attemptDownstreamAuthInline("wronguser", "somepassword", 3));
        EXPECT_FALSE(filter_->connectionAllowed());
        Common::Redis::RespValuePtr reply(new Common::Redis::RespValue());
        reply->type(Common::Redis::RespType::Error);
        reply->asString() = "WRONGPASS invalid username-password pair";
        EXPECT_CALL(*encoder_, encode(Eq(ByRef(*reply)), _));
        EXPECT_CALL(filter_callbacks_.connection_, write(_, _));
        callbacks.onResponse(std::move(reply));
        return nullptr;
      }));

  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(fake_data, false));
}

// Same fixture, configured username but the client supplies an EMPTY username → Denied. With a
// username configured, an empty inline-AUTH username is NOT promoted to the ``default`` synonym
// (that promotion only applies when NO username is configured), so ``"" == "someusername"`` is
// false and the connection is refused even with the correct password. Pins the empty-username
// sub-case of the configured-username comparison.
TEST_F(RedisProxyFilterWithAuthAclTest, AttemptInlineAuthConfiguredUsernameEmptyUsername) {
  InSequence s;

  Buffer::OwnedImpl fake_data;
  Common::Redis::RespValuePtr request(new Common::Redis::RespValue());
  EXPECT_CALL(*decoder_, decode(Ref(fake_data))).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    decoder_callbacks_->onRespValue(std::move(request));
  }));
  EXPECT_CALL(splitter_, makeRequest_(Ref(*request), _, _, _))
      .WillOnce(Invoke([&](const Common::Redis::RespValue&,
                           CommandSplitter::SplitCallbacks& callbacks, Event::Dispatcher&,
                           const StreamInfo::StreamInfo&) -> CommandSplitter::SplitRequest* {
        EXPECT_EQ(CommandSplitter::SplitCallbacks::AuthAttempt::Denied,
                  callbacks.attemptDownstreamAuthInline("", "somepassword", 3));
        EXPECT_FALSE(filter_->connectionAllowed());
        Common::Redis::RespValuePtr reply(new Common::Redis::RespValue());
        reply->type(Common::Redis::RespType::Error);
        reply->asString() = "WRONGPASS invalid username-password pair";
        EXPECT_CALL(*encoder_, encode(Eq(ByRef(*reply)), _));
        EXPECT_CALL(filter_callbacks_.connection_, write(_, _));
        callbacks.onResponse(std::move(reply));
        return nullptr;
      }));

  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(fake_data, false));
}

const std::string downstream_auth_acl_multiple_passwords_config = R"EOF(
prefix_routes:
  catch_all_route:
      cluster: fake_cluster
stat_prefix: foo
settings:
  op_timeout: 0.01s
downstream_auth_username:
  inline_string: someusername
downstream_auth_password:
  inline_string: somepassword
downstream_auth_passwords:
-  inline_string: newpassword1
-  inline_string: newpassword2
)EOF";

class RedisProxyFilterWithAuthAclMultiplePasswordsTest : public RedisProxyFilterTest {
public:
  RedisProxyFilterWithAuthAclMultiplePasswordsTest()
      : RedisProxyFilterTest(downstream_auth_acl_multiple_passwords_config) {}
};

TEST_F(RedisProxyFilterWithAuthAclMultiplePasswordsTest, AuthAclCorrect) {
  InSequence s;

  Buffer::OwnedImpl fake_data;
  Common::Redis::RespValuePtr request(new Common::Redis::RespValue());
  EXPECT_CALL(*decoder_, decode(Ref(fake_data))).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    decoder_callbacks_->onRespValue(std::move(request));
  }));
  EXPECT_CALL(splitter_, makeRequest_(Ref(*request), _, _, _))
      .WillOnce(Invoke([&](const Common::Redis::RespValue&,
                           CommandSplitter::SplitCallbacks& callbacks, Event::Dispatcher&,
                           const StreamInfo::StreamInfo&) -> CommandSplitter::SplitRequest* {
        EXPECT_FALSE(callbacks.connectionAllowed());
        Common::Redis::RespValuePtr reply(new Common::Redis::RespValue());
        reply->type(Common::Redis::RespType::SimpleString);
        reply->asString() = "OK";
        EXPECT_CALL(*encoder_, encode(Eq(ByRef(*reply)), _));
        EXPECT_CALL(filter_callbacks_.connection_, write(_, _));
        callbacks.onAuth("someusername", "somepassword");
        // callbacks cannot be accessed now.
        EXPECT_TRUE(filter_->connectionAllowed());
        return nullptr;
      }));

  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(fake_data, false));
}

TEST_F(RedisProxyFilterWithAuthAclMultiplePasswordsTest, AuthAclCorrect1) {
  InSequence s;

  Buffer::OwnedImpl fake_data;
  Common::Redis::RespValuePtr request(new Common::Redis::RespValue());
  EXPECT_CALL(*decoder_, decode(Ref(fake_data))).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    decoder_callbacks_->onRespValue(std::move(request));
  }));
  EXPECT_CALL(splitter_, makeRequest_(Ref(*request), _, _, _))
      .WillOnce(Invoke([&](const Common::Redis::RespValue&,
                           CommandSplitter::SplitCallbacks& callbacks, Event::Dispatcher&,
                           const StreamInfo::StreamInfo&) -> CommandSplitter::SplitRequest* {
        EXPECT_FALSE(callbacks.connectionAllowed());
        Common::Redis::RespValuePtr reply(new Common::Redis::RespValue());
        reply->type(Common::Redis::RespType::SimpleString);
        reply->asString() = "OK";
        EXPECT_CALL(*encoder_, encode(Eq(ByRef(*reply)), _));
        EXPECT_CALL(filter_callbacks_.connection_, write(_, _));
        callbacks.onAuth("someusername", "newpassword1");
        EXPECT_TRUE(filter_->connectionAllowed());
        return nullptr;
      }));

  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(fake_data, false));
}

TEST_F(RedisProxyFilterWithAuthAclMultiplePasswordsTest, AuthAclCorrect2) {
  InSequence s;

  Buffer::OwnedImpl fake_data;
  Common::Redis::RespValuePtr request(new Common::Redis::RespValue());
  EXPECT_CALL(*decoder_, decode(Ref(fake_data))).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    decoder_callbacks_->onRespValue(std::move(request));
  }));
  EXPECT_CALL(splitter_, makeRequest_(Ref(*request), _, _, _))
      .WillOnce(Invoke([&](const Common::Redis::RespValue&,
                           CommandSplitter::SplitCallbacks& callbacks, Event::Dispatcher&,
                           const StreamInfo::StreamInfo&) -> CommandSplitter::SplitRequest* {
        EXPECT_FALSE(callbacks.connectionAllowed());
        Common::Redis::RespValuePtr reply(new Common::Redis::RespValue());
        reply->type(Common::Redis::RespType::SimpleString);
        reply->asString() = "OK";
        EXPECT_CALL(*encoder_, encode(Eq(ByRef(*reply)), _));
        EXPECT_CALL(filter_callbacks_.connection_, write(_, _));
        callbacks.onAuth("someusername", "newpassword2");
        EXPECT_TRUE(filter_->connectionAllowed());
        return nullptr;
      }));

  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(fake_data, false));
}

TEST_F(RedisProxyFilterWithAuthAclMultiplePasswordsTest, AuthAclUsernameIncorrect) {
  InSequence s;

  Buffer::OwnedImpl fake_data;
  Common::Redis::RespValuePtr request(new Common::Redis::RespValue());
  EXPECT_CALL(*decoder_, decode(Ref(fake_data))).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    decoder_callbacks_->onRespValue(std::move(request));
  }));
  EXPECT_CALL(splitter_, makeRequest_(Ref(*request), _, _, _))
      .WillOnce(Invoke([&](const Common::Redis::RespValue&,
                           CommandSplitter::SplitCallbacks& callbacks, Event::Dispatcher&,
                           const StreamInfo::StreamInfo&) -> CommandSplitter::SplitRequest* {
        EXPECT_FALSE(callbacks.connectionAllowed());
        Common::Redis::RespValuePtr reply(new Common::Redis::RespValue());
        reply->type(Common::Redis::RespType::Error);
        reply->asString() = "WRONGPASS invalid username-password pair";
        EXPECT_CALL(*encoder_, encode(Eq(ByRef(*reply)), _));
        EXPECT_CALL(filter_callbacks_.connection_, write(_, _));
        callbacks.onAuth("wrongusername", "somepassword");
        // callbacks cannot be accessed now.
        EXPECT_FALSE(filter_->connectionAllowed());
        return nullptr;
      }));

  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(fake_data, false));
}

TEST_F(RedisProxyFilterWithAuthAclMultiplePasswordsTest, AuthAclPasswordIncorrect) {
  InSequence s;

  Buffer::OwnedImpl fake_data;
  Common::Redis::RespValuePtr request(new Common::Redis::RespValue());
  EXPECT_CALL(*decoder_, decode(Ref(fake_data))).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    decoder_callbacks_->onRespValue(std::move(request));
  }));
  EXPECT_CALL(splitter_, makeRequest_(Ref(*request), _, _, _))
      .WillOnce(Invoke([&](const Common::Redis::RespValue&,
                           CommandSplitter::SplitCallbacks& callbacks, Event::Dispatcher&,
                           const StreamInfo::StreamInfo&) -> CommandSplitter::SplitRequest* {
        EXPECT_FALSE(callbacks.connectionAllowed());
        Common::Redis::RespValuePtr reply(new Common::Redis::RespValue());
        reply->type(Common::Redis::RespType::Error);
        reply->asString() = "WRONGPASS invalid username-password pair";
        EXPECT_CALL(*encoder_, encode(Eq(ByRef(*reply)), _));
        EXPECT_CALL(filter_callbacks_.connection_, write(_, _));
        callbacks.onAuth("someusername", "wrongpassword");
        // callbacks cannot be accessed now.
        EXPECT_FALSE(filter_->connectionAllowed());
        return nullptr;
      }));

  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(fake_data, false));
}

const std::string external_auth_expiration_config = R"EOF(
prefix_routes:
  catch_all_route:
      cluster: fake_cluster
stat_prefix: foo
settings:
  op_timeout: 0.01s
external_auth_provider:
  grpc_service:
    envoy_grpc:
      cluster_name: fake_cluster
  enable_auth_expiration: true
)EOF";

const std::string resp3_listener_external_auth_config = R"EOF(
prefix_routes:
  catch_all_route:
      cluster: fake_cluster
stat_prefix: foo
protocol_version: RESP3
settings:
  op_timeout: 0.01s
external_auth_provider:
  grpc_service:
    envoy_grpc:
      cluster_name: fake_cluster
)EOF";

class RedisProxyFilterWithExternalAuthAndExpiration : public RedisProxyFilterTest {
public:
  RedisProxyFilterWithExternalAuthAndExpiration()
      : RedisProxyFilterTest(external_auth_expiration_config) {}
};

class RedisProxyFilterResp3WithExternalAuth : public RedisProxyFilterTest {
public:
  RedisProxyFilterResp3WithExternalAuth()
      : RedisProxyFilterTest(resp3_listener_external_auth_config) {}
};

TEST_F(RedisProxyFilterWithExternalAuthAndExpiration, ExternalAuthPasswordWrong) {
  InSequence s;

  Buffer::OwnedImpl fake_data;
  Common::Redis::RespValuePtr request(new Common::Redis::RespValue());
  EXPECT_CALL(*decoder_, decode(Ref(fake_data))).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    decoder_callbacks_->onRespValue(std::move(request));
  }));
  EXPECT_CALL(splitter_, makeRequest_(Ref(*request), _, _, _))
      .WillOnce(Invoke([&](const Common::Redis::RespValue&,
                           CommandSplitter::SplitCallbacks& callbacks, Event::Dispatcher&,
                           const StreamInfo::StreamInfo&) -> CommandSplitter::SplitRequest* {
        EXPECT_FALSE(callbacks.connectionAllowed());
        EXPECT_CALL(*external_auth_client_,
                    authenticateExternal(_, _, _, EMPTY_STRING, "wrongpassword"))
            .WillOnce(WithArgs<0, 1>(
                Invoke([&](ExternalAuth::AuthenticateCallback& callback,
                           CommandSplitter::SplitCallbacks& pending_request) -> void {
                  ExternalAuth::AuthenticateResponsePtr auth_response =
                      std::make_unique<ExternalAuth::AuthenticateResponse>(
                          ExternalAuth::AuthenticateResponse{});
                  auth_response->status = ExternalAuth::AuthenticationRequestStatus::Unauthorized;
                  auth_response->message = "sorry, invalid password";
                  callback.onAuthenticateExternal(pending_request, std::move(auth_response));
                })));
        Common::Redis::RespValuePtr reply(new Common::Redis::RespValue());
        reply->type(Common::Redis::RespType::Error);
        reply->asString() = "ERR sorry, invalid password";
        EXPECT_CALL(*encoder_, encode(Eq(ByRef(*reply)), _));
        EXPECT_CALL(filter_callbacks_.connection_, write(_, _));
        callbacks.onAuth("wrongpassword");
        // callbacks cannot be accessed now.
        EXPECT_FALSE(filter_->connectionAllowed());
        return nullptr;
      }));

  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(fake_data, false));
}

TEST_F(RedisProxyFilterWithExternalAuthAndExpiration, ExternalAuthUsernamePasswordWrong) {
  InSequence s;

  Buffer::OwnedImpl fake_data;
  Common::Redis::RespValuePtr request(new Common::Redis::RespValue());
  EXPECT_CALL(*decoder_, decode(Ref(fake_data))).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    decoder_callbacks_->onRespValue(std::move(request));
  }));
  EXPECT_CALL(splitter_, makeRequest_(Ref(*request), _, _, _))
      .WillOnce(Invoke([&](const Common::Redis::RespValue&,
                           CommandSplitter::SplitCallbacks& callbacks, Event::Dispatcher&,
                           const StreamInfo::StreamInfo&) -> CommandSplitter::SplitRequest* {
        EXPECT_FALSE(callbacks.connectionAllowed());
        EXPECT_CALL(*external_auth_client_,
                    authenticateExternal(_, _, _, "wrongusername", "wrongpassword"))
            .WillOnce(WithArgs<0, 1>(
                Invoke([&](ExternalAuth::AuthenticateCallback& callback,
                           CommandSplitter::SplitCallbacks& pending_request) -> void {
                  ExternalAuth::AuthenticateResponsePtr auth_response =
                      std::make_unique<ExternalAuth::AuthenticateResponse>(
                          ExternalAuth::AuthenticateResponse{});
                  auth_response->status = ExternalAuth::AuthenticationRequestStatus::Unauthorized;
                  auth_response->message = "ooops, invalid password and username";
                  callback.onAuthenticateExternal(pending_request, std::move(auth_response));
                })));
        Common::Redis::RespValuePtr reply(new Common::Redis::RespValue());
        reply->type(Common::Redis::RespType::Error);
        reply->asString() = "ERR ooops, invalid password and username";
        EXPECT_CALL(*encoder_, encode(Eq(ByRef(*reply)), _));
        EXPECT_CALL(filter_callbacks_.connection_, write(_, _));
        callbacks.onAuth("wrongusername", "wrongpassword");
        // callbacks cannot be accessed now.
        EXPECT_FALSE(filter_->connectionAllowed());
        return nullptr;
      }));

  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(fake_data, false));
}

TEST_F(RedisProxyFilterWithExternalAuthAndExpiration, ExternalAuthUsernamePasswordCorrect) {
  InSequence s;

  Buffer::OwnedImpl fake_data;
  Common::Redis::RespValuePtr request(new Common::Redis::RespValue());
  EXPECT_CALL(*decoder_, decode(Ref(fake_data))).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    decoder_callbacks_->onRespValue(std::move(request));
  }));
  EXPECT_CALL(splitter_, makeRequest_(Ref(*request), _, _, _))
      .WillOnce(Invoke([&](const Common::Redis::RespValue&,
                           CommandSplitter::SplitCallbacks& callbacks, Event::Dispatcher&,
                           const StreamInfo::StreamInfo&) -> CommandSplitter::SplitRequest* {
        EXPECT_FALSE(callbacks.connectionAllowed());
        EXPECT_CALL(*external_auth_client_, authenticateExternal(_, _, _, "username", "password"))
            .WillOnce(WithArgs<0, 1>(
                Invoke([&](ExternalAuth::AuthenticateCallback& callback,
                           CommandSplitter::SplitCallbacks& pending_request) -> void {
                  ExternalAuth::AuthenticateResponsePtr auth_response =
                      std::make_unique<ExternalAuth::AuthenticateResponse>(
                          ExternalAuth::AuthenticateResponse{});
                  auth_response->status = ExternalAuth::AuthenticationRequestStatus::Authorized;
                  auto time = time_source_.systemTime() + std::chrono::hours(12);

                  auth_response->expiration.set_seconds(
                      duration_cast<std::chrono::seconds>(time.time_since_epoch()).count());
                  callback.onAuthenticateExternal(pending_request, std::move(auth_response));
                })));
        Common::Redis::RespValuePtr reply(new Common::Redis::RespValue());
        reply->type(Common::Redis::RespType::SimpleString);
        reply->asString() = "OK";
        EXPECT_CALL(*encoder_, encode(Eq(ByRef(*reply)), _));
        EXPECT_CALL(filter_callbacks_.connection_, write(_, _));
        callbacks.onAuth("username", "password");
        // callbacks can be accessed now.
        EXPECT_TRUE(filter_->connectionAllowed());
        return nullptr;
      }));

  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(fake_data, false));
}

TEST_F(RedisProxyFilterWithExternalAuthAndExpiration, ExternalAuthPasswordCorrect) {
  InSequence s;

  Buffer::OwnedImpl fake_data;
  Common::Redis::RespValuePtr request(new Common::Redis::RespValue());
  EXPECT_CALL(*decoder_, decode(Ref(fake_data))).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    decoder_callbacks_->onRespValue(std::move(request));
  }));
  EXPECT_CALL(splitter_, makeRequest_(Ref(*request), _, _, _))
      .WillOnce(Invoke([&](const Common::Redis::RespValue&,
                           CommandSplitter::SplitCallbacks& callbacks, Event::Dispatcher&,
                           const StreamInfo::StreamInfo&) -> CommandSplitter::SplitRequest* {
        EXPECT_FALSE(callbacks.connectionAllowed());
        EXPECT_CALL(*external_auth_client_, authenticateExternal(_, _, _, EMPTY_STRING, "password"))
            .WillOnce(WithArgs<0, 1>(
                Invoke([&](ExternalAuth::AuthenticateCallback& callback,
                           CommandSplitter::SplitCallbacks& pending_request) -> void {
                  ExternalAuth::AuthenticateResponsePtr auth_response =
                      std::make_unique<ExternalAuth::AuthenticateResponse>(
                          ExternalAuth::AuthenticateResponse{});
                  auth_response->status = ExternalAuth::AuthenticationRequestStatus::Authorized;
                  auto time = time_source_.systemTime() + std::chrono::hours(12);
                  auth_response->expiration.set_seconds(
                      duration_cast<std::chrono::seconds>(time.time_since_epoch()).count());
                  callback.onAuthenticateExternal(pending_request, std::move(auth_response));
                })));
        Common::Redis::RespValuePtr reply(new Common::Redis::RespValue());
        reply->type(Common::Redis::RespType::SimpleString);
        reply->asString() = "OK";
        EXPECT_CALL(*encoder_, encode(Eq(ByRef(*reply)), _));
        EXPECT_CALL(filter_callbacks_.connection_, write(_, _));
        callbacks.onAuth("password");
        // callbacks can be accessed now.
        EXPECT_TRUE(filter_->connectionAllowed());
        return nullptr;
      }));

  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(fake_data, false));
}

TEST_F(RedisProxyFilterWithExternalAuthAndExpiration, ExternalAuthWithPipelining) {
  InSequence s;

  Buffer::OwnedImpl fake_data;
  Common::Redis::RespValuePtr auth_request(new Common::Redis::RespValue());
  EXPECT_CALL(*decoder_, decode(Ref(fake_data))).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    decoder_callbacks_->onRespValue(std::move(auth_request));
  }));

  // AUTH expectation
  EXPECT_CALL(splitter_, makeRequest_(Ref(*auth_request), _, _, _))
      .WillOnce(Invoke([&](const Common::Redis::RespValue&,
                           CommandSplitter::SplitCallbacks& callbacks, Event::Dispatcher&,
                           const StreamInfo::StreamInfo&) -> CommandSplitter::SplitRequest* {
        // External auth expectation.
        EXPECT_CALL(*external_auth_client_, authenticateExternal(_, _, _, EMPTY_STRING, "password"))
            .WillOnce(WithArgs<0, 1>(
                Invoke([&](ExternalAuth::AuthenticateCallback& callback,
                           CommandSplitter::SplitCallbacks& pending_request) -> void {
                  ExternalAuth::AuthenticateResponsePtr auth_response =
                      std::make_unique<ExternalAuth::AuthenticateResponse>(
                          ExternalAuth::AuthenticateResponse{});
                  auth_response->status = ExternalAuth::AuthenticationRequestStatus::Authorized;
                  auto time = time_source_.systemTime() + std::chrono::hours(12);
                  auth_response->expiration.set_seconds(
                      duration_cast<std::chrono::seconds>(time.time_since_epoch()).count());

                  // Expect that we receive OK from AUTH first
                  Common::Redis::RespValuePtr reply(new Common::Redis::RespValue());
                  reply->type(Common::Redis::RespType::SimpleString);
                  reply->asString() = "OK";
                  EXPECT_CALL(*encoder_, encode(Eq(ByRef(*reply)), _));
                  EXPECT_CALL(filter_callbacks_.connection_, write(_, _));

                  // PING expectation, will be processed after AUTH.
                  Common::Redis::RespValuePtr ping_request(new Common::Redis::RespValue());
                  EXPECT_CALL(splitter_, makeRequest_(Ref(*ping_request), _, _, _))
                      .WillOnce(Invoke(
                          [&](const Common::Redis::RespValue&,
                              CommandSplitter::SplitCallbacks& callbacks, Event::Dispatcher&,
                              const StreamInfo::StreamInfo&) -> CommandSplitter::SplitRequest* {
                            // Having connection allowed proves pipelining was respected.
                            EXPECT_TRUE(filter_->connectionAllowed());

                            Common::Redis::RespValuePtr reply_ping(new Common::Redis::RespValue());
                            reply_ping->type(Common::Redis::RespType::SimpleString);
                            reply_ping->asString() = "PONG";
                            EXPECT_CALL(*encoder_, encode(Eq(ByRef(*reply_ping)), _));
                            EXPECT_CALL(filter_callbacks_.connection_, write(_, _));

                            callbacks.onResponse(std::move(reply_ping));
                            return nullptr;
                          }));

                  // Simulate that before the auth response is received, another command is
                  // pipelined.
                  decoder_callbacks_->onRespValue(std::move(ping_request));

                  callback.onAuthenticateExternal(pending_request, std::move(auth_response));
                })));

        callbacks.onAuth("password");
        return nullptr;
      }));

  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(fake_data, false));
}

// Pipelining stamp pin for ``AUTH ; HELLO 3 ; cmd`` with external auth: when HELLO 3 replays
// after auth resolves, setDownstreamRespVersion must re-stamp the queued cmd or onResponse
// would emit it as RESP2 (encoder flip on stamp-vs-current mismatch).
TEST_F(RedisProxyFilterResp3WithExternalAuth,
       AuthThenHello3ThenCommandKeepsResp3StampForQueuedCommand) {

  Common::Redis::RespValuePtr auth_request = RedisProxyFilterResp3Test::makeCommand({"auth", "p"});
  Common::Redis::RespValuePtr hello_request =
      RedisProxyFilterResp3Test::makeCommand({"hello", "3"});
  Common::Redis::RespValuePtr cmd_request =
      RedisProxyFilterResp3Test::makeCommand({"hgetall", "k"});
  Common::Redis::RespValue* hello_ptr = hello_request.get();
  Common::Redis::RespValue* cmd_ptr = cmd_request.get();

  Buffer::OwnedImpl fake_data;
  EXPECT_CALL(*decoder_, decode(Ref(fake_data))).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    decoder_callbacks_->onRespValue(std::move(auth_request));
  }));

  CommandSplitter::SplitCallbacks* cmd_callbacks_capture = nullptr;

  EXPECT_CALL(splitter_, makeRequest_(_, _, _, _))
      .WillOnce(Invoke([&](const Common::Redis::RespValue&,
                           CommandSplitter::SplitCallbacks& callbacks, Event::Dispatcher&,
                           const StreamInfo::StreamInfo&) -> CommandSplitter::SplitRequest* {
        EXPECT_CALL(*external_auth_client_, authenticateExternal(_, _, _, EMPTY_STRING, "p"))
            .WillOnce(
                WithArgs<0, 1>(Invoke([&](ExternalAuth::AuthenticateCallback& callback,
                                          CommandSplitter::SplitCallbacks& auth_pending) -> void {
                  // HELLO 3 + ``HGETALL`` arrive while ``external_auth_call_status_`` is Pending —
                  // they get stashed in pending_request_value_ with pre-HELLO stamp=2.
                  decoder_callbacks_->onRespValue(std::move(hello_request));
                  decoder_callbacks_->onRespValue(std::move(cmd_request));

                  ExternalAuth::AuthenticateResponsePtr auth_response =
                      std::make_unique<ExternalAuth::AuthenticateResponse>();
                  auth_response->status = ExternalAuth::AuthenticationRequestStatus::Authorized;

                  Common::Redis::RespValuePtr auth_ok(new Common::Redis::RespValue());
                  auth_ok->type(Common::Redis::RespType::SimpleString);
                  auth_ok->asString() = "OK";
                  EXPECT_CALL(*encoder_, encode(Eq(ByRef(*auth_ok)), _));
                  EXPECT_CALL(filter_callbacks_.connection_, write(_, _));

                  // Held HELLO 3 replays via resumeAuthHeldRequests → splitter HELLO branch.
                  // The splitter mock synthesizes setDownstreamRespVersion(3) + HELLO Map.
                  EXPECT_CALL(splitter_, makeRequest_(Ref(*hello_ptr), _, _, _))
                      .WillOnce(Invoke(
                          [&](const Common::Redis::RespValue&,
                              CommandSplitter::SplitCallbacks& hello_callbacks, Event::Dispatcher&,
                              const StreamInfo::StreamInfo&) -> CommandSplitter::SplitRequest* {
                            EXPECT_CALL(*encoder_, setProtocolVersion(
                                                       Common::Redis::RespProtocolVersion::Resp3));
                            hello_callbacks.setDownstreamRespVersion(3);

                            Common::Redis::RespValuePtr hello_map(new Common::Redis::RespValue());
                            hello_map->type(Common::Redis::RespType::Map);
                            EXPECT_CALL(*encoder_, encode(Eq(ByRef(*hello_map)), _));
                            EXPECT_CALL(filter_callbacks_.connection_, write(_, _));
                            hello_callbacks.onResponse(std::move(hello_map));
                            return nullptr;
                          }));

                  // Held ``HGETALL`` replays last. The mock captures the splitter callbacks so
                  // the upstream reply can be fired AFTER the splitter has already returned —
                  // mirrors how a real upstream Map reply lands and is the branch where the
                  // stamp drift would manifest.
                  EXPECT_CALL(splitter_, makeRequest_(Ref(*cmd_ptr), _, _, _))
                      .WillOnce(Invoke(
                          [&cmd_callbacks_capture](
                              const Common::Redis::RespValue&,
                              CommandSplitter::SplitCallbacks& cmd_cb, Event::Dispatcher&,
                              const StreamInfo::StreamInfo&) -> CommandSplitter::SplitRequest* {
                            cmd_callbacks_capture = &cmd_cb;
                            return nullptr;
                          }));

                  callback.onAuthenticateExternal(auth_pending, std::move(auth_response));
                })));

        callbacks.onAuth("p");
        return nullptr;
      }));

  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(fake_data, false));

  // The ``HGETALL`` upstream reply arrives after onData returns. The wire-level assertion is the
  // critical one: the encoder must NOT be flipped to Resp2 for this reply, because the queued
  // ``HGETALL`` was re-stamped to 3 when HELLO 3 negotiated.
  ASSERT_NE(nullptr, cmd_callbacks_capture);
  Common::Redis::RespValuePtr hgetall_reply(new Common::Redis::RespValue());
  hgetall_reply->type(Common::Redis::RespType::Map);
  EXPECT_CALL(*encoder_, setProtocolVersion(Common::Redis::RespProtocolVersion::Resp2)).Times(0);
  EXPECT_CALL(*encoder_, encode(Eq(ByRef(*hgetall_reply)), _));
  EXPECT_CALL(filter_callbacks_.connection_, write(_, _));
  cmd_callbacks_capture->onResponse(std::move(hgetall_reply));
}

TEST_F(RedisProxyFilterWithExternalAuthAndExpiration, ExternalAuthPasswordCorrectButThenExpires) {
  InSequence s;

  Buffer::OwnedImpl fake_data;
  Common::Redis::RespValuePtr request(new Common::Redis::RespValue());
  EXPECT_CALL(*decoder_, decode(Ref(fake_data))).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    decoder_callbacks_->onRespValue(std::move(request));
  }));
  EXPECT_CALL(splitter_, makeRequest_(Ref(*request), _, _, _))
      .WillOnce(Invoke([&](const Common::Redis::RespValue&,
                           CommandSplitter::SplitCallbacks& callbacks, Event::Dispatcher&,
                           const StreamInfo::StreamInfo&) -> CommandSplitter::SplitRequest* {
        EXPECT_FALSE(callbacks.connectionAllowed());
        EXPECT_CALL(*external_auth_client_, authenticateExternal(_, _, _, EMPTY_STRING, "password"))
            .WillOnce(WithArgs<0, 1>(
                Invoke([&](ExternalAuth::AuthenticateCallback& callback,
                           CommandSplitter::SplitCallbacks& pending_request) -> void {
                  ExternalAuth::AuthenticateResponsePtr auth_response =
                      std::make_unique<ExternalAuth::AuthenticateResponse>(
                          ExternalAuth::AuthenticateResponse{});
                  auth_response->status = ExternalAuth::AuthenticationRequestStatus::Authorized;
                  auto time = time_source_.systemTime() + std::chrono::hours(1);
                  auth_response->expiration.set_seconds(
                      duration_cast<std::chrono::seconds>(time.time_since_epoch()).count());
                  callback.onAuthenticateExternal(pending_request, std::move(auth_response));
                })));
        Common::Redis::RespValuePtr reply(new Common::Redis::RespValue());
        reply->type(Common::Redis::RespType::SimpleString);
        reply->asString() = "OK";
        EXPECT_CALL(*encoder_, encode(Eq(ByRef(*reply)), _));
        EXPECT_CALL(filter_callbacks_.connection_, write(_, _));
        callbacks.onAuth("password");
        // callbacks can be accessed now.
        EXPECT_TRUE(filter_->connectionAllowed());
        // but then expiration passes
        time_source_.advanceTimeWait(std::chrono::hours(2));
        // and callbacks are not accessible anymore.
        EXPECT_FALSE(filter_->connectionAllowed());
        return nullptr;
      }));

  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(fake_data, false));
}

TEST_F(RedisProxyFilterWithExternalAuthAndExpiration, ExternalAuthError) {
  InSequence s;

  Buffer::OwnedImpl fake_data;
  Common::Redis::RespValuePtr request(new Common::Redis::RespValue());
  EXPECT_CALL(*decoder_, decode(Ref(fake_data))).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    decoder_callbacks_->onRespValue(std::move(request));
  }));
  EXPECT_CALL(splitter_, makeRequest_(Ref(*request), _, _, _))
      .WillOnce(Invoke([&](const Common::Redis::RespValue&,
                           CommandSplitter::SplitCallbacks& callbacks, Event::Dispatcher&,
                           const StreamInfo::StreamInfo&) -> CommandSplitter::SplitRequest* {
        EXPECT_FALSE(callbacks.connectionAllowed());
        EXPECT_CALL(*external_auth_client_, authenticateExternal(_, _, _, EMPTY_STRING, "password"))
            .WillOnce(WithArgs<0, 1>(
                Invoke([&](ExternalAuth::AuthenticateCallback& callback,
                           CommandSplitter::SplitCallbacks& pending_request) -> void {
                  ExternalAuth::AuthenticateResponsePtr auth_response =
                      std::make_unique<ExternalAuth::AuthenticateResponse>(
                          ExternalAuth::AuthenticateResponse{});
                  auth_response->status = ExternalAuth::AuthenticationRequestStatus::Error;
                  auth_response->message = "sorry, error";
                  callback.onAuthenticateExternal(pending_request, std::move(auth_response));
                })));
        Common::Redis::RespValuePtr reply(new Common::Redis::RespValue());
        reply->type(Common::Redis::RespType::Error);
        reply->asString() = "ERR external authentication failed";
        EXPECT_CALL(*encoder_, encode(Eq(ByRef(*reply)), _));
        EXPECT_CALL(filter_callbacks_.connection_, write(_, _));
        callbacks.onAuth("password");
        // callbacks cannot be accessed now.
        EXPECT_FALSE(filter_->connectionAllowed());
        return nullptr;
      }));

  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(fake_data, false));
}

// HELLO 3 flips downstream version mid-request: the HELLO reply itself must encode in
// RESP3. Pins setDownstreamRespVersion updating resp_version_at_creation_.
TEST_F(RedisProxyFilterTest, HelloReplyEncodedInNegotiatedVersion) {
  Buffer::OwnedImpl fake_data;
  Common::Redis::RespValuePtr request(new Common::Redis::RespValue());
  Common::Redis::RespValue* request_ptr = request.get();

  Common::Redis::RespValuePtr reply(new Common::Redis::RespValue());
  reply->type(Common::Redis::RespType::Map);
  Common::Redis::RespValue* reply_ptr = reply.get();

  // No Resp2 flip; one Resp3 flip from setDownstreamRespVersion.
  EXPECT_CALL(*encoder_, setProtocolVersion(Common::Redis::RespProtocolVersion::Resp2)).Times(0);
  EXPECT_CALL(*encoder_, setProtocolVersion(Common::Redis::RespProtocolVersion::Resp3));
  EXPECT_CALL(*encoder_, encode(Ref(*reply_ptr), _));
  EXPECT_CALL(filter_callbacks_.connection_, write(_, _));

  EXPECT_CALL(*decoder_, decode(Ref(fake_data))).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    decoder_callbacks_->onRespValue(std::move(request));
  }));
  EXPECT_CALL(splitter_, makeRequest_(Ref(*request_ptr), _, _, _))
      .WillOnce(Invoke([&reply](const Common::Redis::RespValue&,
                                CommandSplitter::SplitCallbacks& callbacks, Event::Dispatcher&,
                                const StreamInfo::StreamInfo&) -> CommandSplitter::SplitRequest* {
        // Mimic the splitter's HELLO 3: flip version, then deliver the Map reply.
        callbacks.setDownstreamRespVersion(3);
        callbacks.onResponse(std::move(reply));
        return nullptr;
      }));

  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(fake_data, false));
}

// HELLO N AUTH ... with external auth Authorized → deferred HELLO Map encoded in RESP3.
TEST_F(RedisProxyFilterWithExternalAuthAndExpiration,
       HelloAuthExternalAuthAuthorizedEmitsHelloMap) {
  InSequence s;

  // The deferred HELLO Map must match what buildHelloReply(3) produces — same shape that the
  // synchronous-allowed path would emit. Comparing against the canonical builder pins both the
  // deferred-emission contract and the version-flip ordering: ByRef does pointer-equal
  // comparison through Eq's operator==, which on RespValue checks structural equality.
  auto expected_hello = CommandSplitter::buildHelloReply(3);

  Buffer::OwnedImpl fake_data;
  Common::Redis::RespValuePtr request(new Common::Redis::RespValue());
  EXPECT_CALL(*decoder_, decode(Ref(fake_data))).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    decoder_callbacks_->onRespValue(std::move(request));
  }));
  EXPECT_CALL(splitter_, makeRequest_(Ref(*request), _, _, _))
      .WillOnce(Invoke([&](const Common::Redis::RespValue&,
                           CommandSplitter::SplitCallbacks& callbacks, Event::Dispatcher&,
                           const StreamInfo::StreamInfo&) -> CommandSplitter::SplitRequest* {
        EXPECT_FALSE(callbacks.connectionAllowed());
        EXPECT_CALL(*external_auth_client_, authenticateExternal(_, _, _, "alice", "secret"))
            .WillOnce(WithArgs<0, 1>(
                Invoke([&](ExternalAuth::AuthenticateCallback& callback,
                           CommandSplitter::SplitCallbacks& pending_request) -> void {
                  ExternalAuth::AuthenticateResponsePtr auth_response =
                      std::make_unique<ExternalAuth::AuthenticateResponse>();
                  auth_response->status = ExternalAuth::AuthenticationRequestStatus::Authorized;
                  auto time = time_source_.systemTime() + std::chrono::hours(12);
                  auth_response->expiration.set_seconds(
                      duration_cast<std::chrono::seconds>(time.time_since_epoch()).count());
                  callback.onAuthenticateExternal(pending_request, std::move(auth_response));
                })));
        // Encoder must flip to RESP3 (the negotiated version) BEFORE the HELLO Map is encoded
        // — otherwise the Map down-converts to a flat *2N array on the wire and a strict
        // RESP3 client rejects the shape. NiceMock's ON_CALL forwards encode() to the real
        // encoder so encoder_buffer_ fills and connection.write fires.
        EXPECT_CALL(*encoder_, setProtocolVersion(Common::Redis::RespProtocolVersion::Resp3));
        EXPECT_CALL(*encoder_, encode(Eq(ByRef(*expected_hello)), _));
        EXPECT_CALL(filter_callbacks_.connection_, write(_, _));
        EXPECT_EQ(CommandSplitter::SplitCallbacks::AuthAttempt::ImplOwnsResponse,
                  callbacks.attemptDownstreamAuthInline("alice", "secret", 3));
        EXPECT_TRUE(filter_->connectionAllowed());
        return nullptr;
      }));

  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(fake_data, false));
}

// HELLO N AUTH ... + external auth Unauthorized: deferred reply is WRONGPASS (matching the
// splitter's local-auth Denied shape so RESP3 clients see a uniform error code).
TEST_F(RedisProxyFilterWithExternalAuthAndExpiration,
       HelloAuthExternalAuthUnauthorizedEmitsWrongpass) {
  InSequence s;

  Buffer::OwnedImpl fake_data;
  Common::Redis::RespValuePtr request(new Common::Redis::RespValue());
  EXPECT_CALL(*decoder_, decode(Ref(fake_data))).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    decoder_callbacks_->onRespValue(std::move(request));
  }));
  EXPECT_CALL(splitter_, makeRequest_(Ref(*request), _, _, _))
      .WillOnce(Invoke([&](const Common::Redis::RespValue&,
                           CommandSplitter::SplitCallbacks& callbacks, Event::Dispatcher&,
                           const StreamInfo::StreamInfo&) -> CommandSplitter::SplitRequest* {
        EXPECT_CALL(*external_auth_client_, authenticateExternal(_, _, _, "alice", "wrong"))
            .WillOnce(WithArgs<0, 1>(
                Invoke([&](ExternalAuth::AuthenticateCallback& callback,
                           CommandSplitter::SplitCallbacks& pending_request) -> void {
                  ExternalAuth::AuthenticateResponsePtr auth_response =
                      std::make_unique<ExternalAuth::AuthenticateResponse>();
                  auth_response->status = ExternalAuth::AuthenticationRequestStatus::Unauthorized;
                  auth_response->message = "rejected";
                  callback.onAuthenticateExternal(pending_request, std::move(auth_response));
                })));
        Common::Redis::RespValuePtr reply(new Common::Redis::RespValue());
        reply->type(Common::Redis::RespType::Error);
        reply->asString() = "WRONGPASS rejected";
        EXPECT_CALL(*encoder_, encode(Eq(ByRef(*reply)), _));
        EXPECT_CALL(filter_callbacks_.connection_, write(_, _));
        EXPECT_EQ(CommandSplitter::SplitCallbacks::AuthAttempt::ImplOwnsResponse,
                  callbacks.attemptDownstreamAuthInline("alice", "wrong", 3));
        // Failed HELLO AUTH must NOT flip connection_allowed_ to true. A subsequent ordinary
        // command on this connection still hits the auth gate.
        EXPECT_FALSE(filter_->connectionAllowed());
        return nullptr;
      }));

  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(fake_data, false));
}

// The external provider's detail message flows into a RESP Error line; CR/LF in it would
// re-frame the downstream protocol. Both the WRONGPASS (HELLO AUTH) sink and the plain-AUTH
// ERR sink must sanitize control bytes to spaces.
TEST_F(RedisProxyFilterWithExternalAuthAndExpiration,
       HelloAuthExternalAuthUnauthorizedMessageIsSanitized) {
  InSequence s;

  Buffer::OwnedImpl fake_data;
  Common::Redis::RespValuePtr request(new Common::Redis::RespValue());
  EXPECT_CALL(*decoder_, decode(Ref(fake_data))).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    decoder_callbacks_->onRespValue(std::move(request));
  }));
  EXPECT_CALL(splitter_, makeRequest_(Ref(*request), _, _, _))
      .WillOnce(Invoke([&](const Common::Redis::RespValue&,
                           CommandSplitter::SplitCallbacks& callbacks, Event::Dispatcher&,
                           const StreamInfo::StreamInfo&) -> CommandSplitter::SplitRequest* {
        EXPECT_CALL(*external_auth_client_, authenticateExternal(_, _, _, "alice", "wrong"))
            .WillOnce(WithArgs<0, 1>(
                Invoke([&](ExternalAuth::AuthenticateCallback& callback,
                           CommandSplitter::SplitCallbacks& pending_request) -> void {
                  ExternalAuth::AuthenticateResponsePtr auth_response =
                      std::make_unique<ExternalAuth::AuthenticateResponse>();
                  auth_response->status = ExternalAuth::AuthenticationRequestStatus::Unauthorized;
                  auth_response->message = "rejected\r\n-FAKE injected frame";
                  callback.onAuthenticateExternal(pending_request, std::move(auth_response));
                })));
        Common::Redis::RespValuePtr reply(new Common::Redis::RespValue());
        reply->type(Common::Redis::RespType::Error);
        reply->asString() = "WRONGPASS rejected  -FAKE injected frame";
        EXPECT_CALL(*encoder_, encode(Eq(ByRef(*reply)), _));
        EXPECT_CALL(filter_callbacks_.connection_, write(_, _));
        EXPECT_EQ(CommandSplitter::SplitCallbacks::AuthAttempt::ImplOwnsResponse,
                  callbacks.attemptDownstreamAuthInline("alice", "wrong", 3));
        return nullptr;
      }));

  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(fake_data, false));
}

// Pipelined ``HELLO N AUTH ... ; GET``: GET held until auth resolves, then re-stamped to
// RESP3. setProtocolVersion(Resp2).Times(0) below pins the no-Resp2-flip invariant.
TEST_F(RedisProxyFilterWithExternalAuthAndExpiration,
       HelloAuthPipelinedRequestHeldUntilAuthCompletes) {
  InSequence s;
  auto expected_hello = CommandSplitter::buildHelloReply(3);

  Buffer::OwnedImpl fake_data;
  Common::Redis::RespValuePtr hello_request(new Common::Redis::RespValue());
  EXPECT_CALL(*decoder_, decode(Ref(fake_data))).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    decoder_callbacks_->onRespValue(std::move(hello_request));
  }));

  EXPECT_CALL(splitter_, makeRequest_(Ref(*hello_request), _, _, _))
      .WillOnce(Invoke([&](const Common::Redis::RespValue&,
                           CommandSplitter::SplitCallbacks& callbacks, Event::Dispatcher&,
                           const StreamInfo::StreamInfo&) -> CommandSplitter::SplitRequest* {
        // Pin the no-Resp2-flip invariant: any flip means the held GET kept the stale stamp.
        EXPECT_CALL(*encoder_, setProtocolVersion(Common::Redis::RespProtocolVersion::Resp2))
            .Times(0);

        EXPECT_CALL(*external_auth_client_, authenticateExternal(_, _, _, "alice", "secret"))
            .WillOnce(WithArgs<0, 1>(
                Invoke([&](ExternalAuth::AuthenticateCallback& callback,
                           CommandSplitter::SplitCallbacks& pending_request) -> void {
                  ExternalAuth::AuthenticateResponsePtr auth_response =
                      std::make_unique<ExternalAuth::AuthenticateResponse>();
                  auth_response->status = ExternalAuth::AuthenticationRequestStatus::Authorized;
                  auto time = time_source_.systemTime() + std::chrono::hours(1);
                  auth_response->expiration.set_seconds(
                      duration_cast<std::chrono::seconds>(time.time_since_epoch()).count());

                  EXPECT_CALL(*encoder_,
                              setProtocolVersion(Common::Redis::RespProtocolVersion::Resp3));
                  EXPECT_CALL(*encoder_, encode(Eq(ByRef(*expected_hello)), _));
                  EXPECT_CALL(filter_callbacks_.connection_, write(_, _));

                  // After the HELLO reply, the held GET is re-dispatched. By the time the
                  // splitter sees it, connection_allowed_ must already be true (HELLO AUTH
                  // succeeded), proving the pipelining gate respected ordering.
                  Common::Redis::RespValuePtr get_request(new Common::Redis::RespValue());
                  EXPECT_CALL(splitter_, makeRequest_(Ref(*get_request), _, _, _))
                      .WillOnce(Invoke(
                          [&](const Common::Redis::RespValue&, CommandSplitter::SplitCallbacks& cb,
                              Event::Dispatcher&,
                              const StreamInfo::StreamInfo&) -> CommandSplitter::SplitRequest* {
                            EXPECT_TRUE(filter_->connectionAllowed());
                            Common::Redis::RespValuePtr get_reply(new Common::Redis::RespValue());
                            get_reply->type(Common::Redis::RespType::BulkString);
                            get_reply->asString() = "value";
                            EXPECT_CALL(*encoder_, encode(Eq(ByRef(*get_reply)), _));
                            EXPECT_CALL(filter_callbacks_.connection_, write(_, _));
                            cb.onResponse(std::move(get_reply));
                            return nullptr;
                          }));

                  // Simulate the pipelined GET arriving while external_auth_call_status_ is
                  // Pending: the decoder fires it before the auth callback resolves.
                  decoder_callbacks_->onRespValue(std::move(get_request));

                  callback.onAuthenticateExternal(pending_request, std::move(auth_response));
                })));
        EXPECT_EQ(CommandSplitter::SplitCallbacks::AuthAttempt::ImplOwnsResponse,
                  callbacks.attemptDownstreamAuthInline("alice", "secret", 3));
        return nullptr;
      }));

  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(fake_data, false));
}

// Connection close while a HELLO N AUTH ... external-auth round trip is pending: the filter
// must tear down cleanly without firing the deferred HELLO emission. The existing
// pending_requests_ cancellation path on RemoteClose handles this — verifies HELLO AUTH
// inherits that protection without code-path-specific breakage.
TEST_F(RedisProxyFilterWithExternalAuthAndExpiration, HelloAuthPendingCanceledOnConnectionClose) {
  InSequence s;

  Buffer::OwnedImpl fake_data;
  Common::Redis::RespValuePtr request(new Common::Redis::RespValue());
  EXPECT_CALL(*decoder_, decode(Ref(fake_data))).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    decoder_callbacks_->onRespValue(std::move(request));
  }));
  EXPECT_CALL(splitter_, makeRequest_(Ref(*request), _, _, _))
      .WillOnce(Invoke([&](const Common::Redis::RespValue&,
                           CommandSplitter::SplitCallbacks& callbacks, Event::Dispatcher&,
                           const StreamInfo::StreamInfo&) -> CommandSplitter::SplitRequest* {
        // Capture the auth-callback args but DO NOT fire the callback synchronously — the
        // round trip is "in flight" when the connection closes.
        EXPECT_CALL(*external_auth_client_, authenticateExternal(_, _, _, "alice", "secret"));
        EXPECT_EQ(CommandSplitter::SplitCallbacks::AuthAttempt::ImplOwnsResponse,
                  callbacks.attemptDownstreamAuthInline("alice", "secret", 3));
        return nullptr;
      }));
  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(fake_data, false));

  // Connection close before the auth callback fires — onEvent's existing
  // external_auth_call_status_ branch invokes auth_client_->cancel() to release the in-flight
  // round trip, then drains pending_requests_. The HELLO PendingRequest's request_handle_ is
  // nullptr (the splitter returned nullptr in the ImplOwnsResponse case) so the drain pop is a
  // no-op beyond destruction. The filter must NOT emit anything on the closed connection (the
  // deferred HELLO Map would otherwise race the close), pinned here with Times(0) expectations so
  // a regression cannot hide behind the NiceMock.
  EXPECT_CALL(*encoder_, encode(_, _)).Times(0);
  EXPECT_CALL(filter_callbacks_.connection_, write(_, _)).Times(0);
  EXPECT_CALL(*external_auth_client_, cancel());
  filter_->onEvent(Network::ConnectionEvent::RemoteClose);
}

// HELLO 3 AUTH ... + GET k1 + GET k2 pipelined: after auth, BOTH held GETs must be
// dispatched. Pins resumeAuthHeldRequests scanning the full queue, not just the front.
TEST_F(RedisProxyFilterWithExternalAuthAndExpiration,
       HelloAuthMultiplePipelinedRequestsAllResumed) {
  InSequence s;
  auto expected_hello = CommandSplitter::buildHelloReply(3);

  Buffer::OwnedImpl fake_data;
  Common::Redis::RespValuePtr hello_request(new Common::Redis::RespValue());
  EXPECT_CALL(*decoder_, decode(Ref(fake_data))).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    decoder_callbacks_->onRespValue(std::move(hello_request));
  }));

  EXPECT_CALL(splitter_, makeRequest_(Ref(*hello_request), _, _, _))
      .WillOnce(Invoke([&](const Common::Redis::RespValue&,
                           CommandSplitter::SplitCallbacks& callbacks, Event::Dispatcher&,
                           const StreamInfo::StreamInfo&) -> CommandSplitter::SplitRequest* {
        EXPECT_CALL(*encoder_, setProtocolVersion(Common::Redis::RespProtocolVersion::Resp2))
            .Times(0);
        EXPECT_CALL(*external_auth_client_, authenticateExternal(_, _, _, "alice", "secret"))
            .WillOnce(WithArgs<0, 1>(
                Invoke([&](ExternalAuth::AuthenticateCallback& callback,
                           CommandSplitter::SplitCallbacks& pending_request) -> void {
                  ExternalAuth::AuthenticateResponsePtr auth_response =
                      std::make_unique<ExternalAuth::AuthenticateResponse>();
                  auth_response->status = ExternalAuth::AuthenticationRequestStatus::Authorized;
                  auto time = time_source_.systemTime() + std::chrono::hours(1);
                  auth_response->expiration.set_seconds(
                      duration_cast<std::chrono::seconds>(time.time_since_epoch()).count());

                  EXPECT_CALL(*encoder_,
                              setProtocolVersion(Common::Redis::RespProtocolVersion::Resp3));
                  EXPECT_CALL(*encoder_, encode(Eq(ByRef(*expected_hello)), _));
                  EXPECT_CALL(filter_callbacks_.connection_, write(_, _));

                  // Both held GETs must hit the splitter. Each lambda holds its request
                  // upstream (returns a non-null handle) so neither is popped by a synchronous
                  // onResponse — k1 stays at the front with no pending_request_value_, so a
                  // scan that only inspects the front would miss k2.
                  Common::Redis::RespValuePtr get1_request(new Common::Redis::RespValue());
                  Common::Redis::RespValuePtr get2_request(new Common::Redis::RespValue());
                  auto get1_handle = new CommandSplitter::MockSplitRequest();
                  auto get2_handle = new CommandSplitter::MockSplitRequest();
                  EXPECT_CALL(splitter_, makeRequest_(Ref(*get1_request), _, _, _))
                      .WillOnce(Return(get1_handle));
                  EXPECT_CALL(splitter_, makeRequest_(Ref(*get2_request), _, _, _))
                      .WillOnce(Return(get2_handle));
                  // Both held handles get cancelled at filter teardown — pending_requests_
                  // still owns them when the connection mock is destroyed.
                  EXPECT_CALL(*get1_handle, cancel());
                  EXPECT_CALL(*get2_handle, cancel());

                  decoder_callbacks_->onRespValue(std::move(get1_request));
                  decoder_callbacks_->onRespValue(std::move(get2_request));

                  callback.onAuthenticateExternal(pending_request, std::move(auth_response));
                })));
        EXPECT_EQ(CommandSplitter::SplitCallbacks::AuthAttempt::ImplOwnsResponse,
                  callbacks.attemptDownstreamAuthInline("alice", "secret", 3));
        return nullptr;
      }));

  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(fake_data, false));
  // Drain pending_requests_ so ~ProxyFilter()'s ASSERT(pending_requests_.empty()) holds —
  // the two GETs are still upstream-in-flight at this point because their split-request
  // mocks return a handle without firing onResponse. RemoteClose cancels each handle.
  filter_->onEvent(Network::ConnectionEvent::RemoteClose);
}

// A held GET pipelined AFTER a HELLO AUTH must resume even when an older GET (decoded BEFORE
// the HELLO) is still in flight at the front of pending_requests_. resumeAuthHeldRequests
// walks past the in-flight front entry to find the held one.
TEST_F(RedisProxyFilterWithExternalAuthAndExpiration,
       HelloAuthHeldRequestResumesEvenWithPriorOutstandingRequest) {
  InSequence s;

  // Hoist both upstream-in-flight handles to test scope so the cancel expectations can be
  // declared at the very end of the InSequence chain (after every other ordered expectation
  // has been declared and consumed). Otherwise an interleaved cancel expectation creates a
  // pre-requisite that the next decode/makeRequest cannot satisfy.
  auto* old_handle = new CommandSplitter::MockSplitRequest();
  auto* held_handle = new CommandSplitter::MockSplitRequest();

  // First onData: a regular GET that goes upstream and stays in flight.
  Buffer::OwnedImpl old_data;
  Common::Redis::RespValuePtr old_request(new Common::Redis::RespValue());
  EXPECT_CALL(*decoder_, decode(Ref(old_data))).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    decoder_callbacks_->onRespValue(std::move(old_request));
  }));
  EXPECT_CALL(splitter_, makeRequest_(Ref(*old_request), _, _, _)).WillOnce(Return(old_handle));

  // Second onData: HELLO 3 AUTH ... that the splitter routes to external auth, plus a GET
  // that the decoder fires while external_auth_call_status_ is Pending so it lands in the
  // held queue with the older GET still in flight in front of it.
  Buffer::OwnedImpl new_data;
  Common::Redis::RespValuePtr hello_request(new Common::Redis::RespValue());
  Common::Redis::RespValuePtr held_get(new Common::Redis::RespValue());
  EXPECT_CALL(*decoder_, decode(Ref(new_data))).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    decoder_callbacks_->onRespValue(std::move(hello_request));
  }));
  EXPECT_CALL(splitter_, makeRequest_(Ref(*hello_request), _, _, _))
      .WillOnce(Invoke([&](const Common::Redis::RespValue&,
                           CommandSplitter::SplitCallbacks& callbacks, Event::Dispatcher&,
                           const StreamInfo::StreamInfo&) -> CommandSplitter::SplitRequest* {
        EXPECT_CALL(*external_auth_client_, authenticateExternal(_, _, _, "alice", "secret"))
            .WillOnce(WithArgs<0, 1>(
                Invoke([&](ExternalAuth::AuthenticateCallback& callback,
                           CommandSplitter::SplitCallbacks& pending_request) -> void {
                  ExternalAuth::AuthenticateResponsePtr auth_response =
                      std::make_unique<ExternalAuth::AuthenticateResponse>();
                  auth_response->status = ExternalAuth::AuthenticationRequestStatus::Authorized;
                  auto time = time_source_.systemTime() + std::chrono::hours(1);
                  auth_response->expiration.set_seconds(
                      duration_cast<std::chrono::seconds>(time.time_since_epoch()).count());

                  // The HELLO Map is built and request.onResponse runs, but front-pop in
                  // onResponse stops at the older in-flight GET — the HELLO Map stays
                  // buffered behind that GET's not-yet-arrived response. setProtocolVersion
                  // (Resp3) still fires when setDownstreamRespVersion runs.
                  EXPECT_CALL(*encoder_,
                              setProtocolVersion(Common::Redis::RespProtocolVersion::Resp3));

                  // Property under test: the held GET dispatched after HELLO is found and
                  // dispatched even though the front entry is the old in-flight GET (no
                  // pending_request_value_), because the scan walks the full list.
                  EXPECT_CALL(splitter_, makeRequest_(Ref(*held_get), _, _, _))
                      .WillOnce(Return(held_handle));

                  decoder_callbacks_->onRespValue(std::move(held_get));

                  callback.onAuthenticateExternal(pending_request, std::move(auth_response));
                })));
        EXPECT_EQ(CommandSplitter::SplitCallbacks::AuthAttempt::ImplOwnsResponse,
                  callbacks.attemptDownstreamAuthInline("alice", "secret", 3));
        return nullptr;
      }));

  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(old_data, false));
  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(new_data, false));

  // Cancel expectations declared LAST, after every other sequence-bound expectation has been
  // declared (the inside-lambda ones run during the second onData). They fire in
  // pending_requests_ FIFO order during the RemoteClose drain: old_handle first, then
  // held_handle. The HELLO entry has no handle (splitter returned nullptr in the
  // ImplOwnsResponse case) so no cancel for it.
  EXPECT_CALL(*old_handle, cancel());
  EXPECT_CALL(*held_handle, cancel());
  filter_->onEvent(Network::ConnectionEvent::RemoteClose);
}

// HELLO N AUTH ... + AUTH bob secret pipelined: the second AUTH is held while the first
// external-auth round trip runs. When that round trip resolves and resumeAuthHeldRequests
// dispatches the held AUTH, the AUTH itself starts a SECOND external-auth round trip — the
// drain helper must observe external_auth_call_status_ = Pending again and stop, leaving any
// further held entries queued for the next onAuthenticateExternal to drain.
TEST_F(RedisProxyFilterWithExternalAuthAndExpiration,
       HelloAuthResumedHeldAuthStartsSecondExternalAuth) {
  InSequence s;
  auto expected_hello = CommandSplitter::buildHelloReply(3);

  Buffer::OwnedImpl fake_data;
  Common::Redis::RespValuePtr hello_request(new Common::Redis::RespValue());
  EXPECT_CALL(*decoder_, decode(Ref(fake_data))).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    decoder_callbacks_->onRespValue(std::move(hello_request));
  }));

  EXPECT_CALL(splitter_, makeRequest_(Ref(*hello_request), _, _, _))
      .WillOnce(Invoke([&](const Common::Redis::RespValue&,
                           CommandSplitter::SplitCallbacks& callbacks, Event::Dispatcher&,
                           const StreamInfo::StreamInfo&) -> CommandSplitter::SplitRequest* {
        EXPECT_CALL(*external_auth_client_, authenticateExternal(_, _, _, "alice", "secret"))
            .WillOnce(WithArgs<0, 1>(
                Invoke([&](ExternalAuth::AuthenticateCallback& callback,
                           CommandSplitter::SplitCallbacks& pending_request) -> void {
                  ExternalAuth::AuthenticateResponsePtr auth_response =
                      std::make_unique<ExternalAuth::AuthenticateResponse>();
                  auth_response->status = ExternalAuth::AuthenticationRequestStatus::Authorized;
                  auto time = time_source_.systemTime() + std::chrono::hours(1);
                  auth_response->expiration.set_seconds(
                      duration_cast<std::chrono::seconds>(time.time_since_epoch()).count());

                  EXPECT_CALL(*encoder_,
                              setProtocolVersion(Common::Redis::RespProtocolVersion::Resp3));
                  EXPECT_CALL(*encoder_, encode(Eq(ByRef(*expected_hello)), _));
                  EXPECT_CALL(filter_callbacks_.connection_, write(_, _));

                  // Held AUTH that, when dispatched, triggers a second external-auth round
                  // trip. The mock invokes callbacks.onAuth("bob", "secret2") which forwards
                  // to ProxyFilter::onAuth → external auth Pending again.
                  Common::Redis::RespValuePtr held_auth(new Common::Redis::RespValue());
                  EXPECT_CALL(splitter_, makeRequest_(Ref(*held_auth), _, _, _))
                      .WillOnce(Invoke(
                          [&](const Common::Redis::RespValue&, CommandSplitter::SplitCallbacks& cb,
                              Event::Dispatcher&,
                              const StreamInfo::StreamInfo&) -> CommandSplitter::SplitRequest* {
                            EXPECT_CALL(*external_auth_client_,
                                        authenticateExternal(_, _, _, "bob", "secret2"));
                            cb.onAuth("bob", "secret2");
                            return nullptr;
                          }));

                  // A FURTHER held GET that must NOT be dispatched yet: when the second AUTH
                  // re-enters Pending, resumeAuthHeldRequests must stop. Times(0) on its
                  // splitter expectation pins the early-out.
                  Common::Redis::RespValuePtr extra_get(new Common::Redis::RespValue());
                  EXPECT_CALL(splitter_, makeRequest_(Ref(*extra_get), _, _, _)).Times(0);

                  decoder_callbacks_->onRespValue(std::move(held_auth));
                  decoder_callbacks_->onRespValue(std::move(extra_get));

                  callback.onAuthenticateExternal(pending_request, std::move(auth_response));
                })));
        EXPECT_EQ(CommandSplitter::SplitCallbacks::AuthAttempt::ImplOwnsResponse,
                  callbacks.attemptDownstreamAuthInline("alice", "secret", 3));
        return nullptr;
      }));

  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(fake_data, false));
  // Connection close at end of test: second-auth round trip is still in flight so
  // ProxyFilter::onEvent's external-auth branch invokes auth_client_->cancel() before
  // draining pending_requests_ (which still contains the in-flight AUTH PR and the held GET
  // PR). Drain satisfies the ~ProxyFilter() assertion.
  EXPECT_CALL(*external_auth_client_, cancel());
  filter_->onEvent(Network::ConnectionEvent::RemoteClose);
}

// Regression test for the resumeAuthHeldRequests drain loop: a resumed held AUTH whose
// external-auth round trip resolves SYNCHRONOUSLY (the gRPC client may invoke the callback
// inline from send(), e.g. when the auth cluster is unavailable) re-enters
// resumeAuthHeldRequests from inside the drain. The nested call must be a no-op (reentrant
// guard) and the outer loop must hold no iterator across the nested pops, so the trailing held
// PING is still dispatched exactly once, in FIFO order, after the denial is emitted.
TEST_F(RedisProxyFilterWithExternalAuthAndExpiration,
       HelloAuthResumedHeldAuthResolvesSynchronouslyThenDrainContinues) {
  InSequence s;
  auto expected_hello = CommandSplitter::buildHelloReply(3);
  Common::Redis::RespValue expected_auth_error;
  expected_auth_error.type(Common::Redis::RespType::Error);
  expected_auth_error.asString() = "ERR unauthorized";
  Common::Redis::RespValue expected_pong;
  expected_pong.type(Common::Redis::RespType::SimpleString);
  expected_pong.asString() = "PONG";

  Buffer::OwnedImpl fake_data;
  Common::Redis::RespValuePtr hello_request(new Common::Redis::RespValue());
  EXPECT_CALL(*decoder_, decode(Ref(fake_data))).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    decoder_callbacks_->onRespValue(std::move(hello_request));
  }));

  EXPECT_CALL(splitter_, makeRequest_(Ref(*hello_request), _, _, _))
      .WillOnce(Invoke([&](const Common::Redis::RespValue&,
                           CommandSplitter::SplitCallbacks& callbacks, Event::Dispatcher&,
                           const StreamInfo::StreamInfo&) -> CommandSplitter::SplitRequest* {
        EXPECT_CALL(*external_auth_client_, authenticateExternal(_, _, _, "alice", "secret"))
            .WillOnce(WithArgs<0, 1>(Invoke([&](ExternalAuth::AuthenticateCallback& callback,
                                                CommandSplitter::SplitCallbacks& pending_request)
                                                -> void {
              ExternalAuth::AuthenticateResponsePtr auth_response =
                  std::make_unique<ExternalAuth::AuthenticateResponse>();
              auth_response->status = ExternalAuth::AuthenticationRequestStatus::Authorized;
              auto time = time_source_.systemTime() + std::chrono::hours(1);
              auth_response->expiration.set_seconds(
                  duration_cast<std::chrono::seconds>(time.time_since_epoch()).count());

              EXPECT_CALL(*encoder_, setProtocolVersion(Common::Redis::RespProtocolVersion::Resp3));
              EXPECT_CALL(*encoder_, encode(Eq(ByRef(*expected_hello)), _));
              EXPECT_CALL(filter_callbacks_.connection_, write(_, _));

              // Trailing held PING. Its splitter expectation is declared later, inside the
              // second-auth lambda, to keep InSequence declaration order aligned with
              // execution order; capture the raw pointer before the move.
              Common::Redis::RespValuePtr held_ping(new Common::Redis::RespValue());
              Common::Redis::RespValue* held_ping_raw = held_ping.get();

              // Held AUTH whose second round trip resolves INLINE with Unauthorized —
              // before cb.onAuth() even returns.
              Common::Redis::RespValuePtr held_auth(new Common::Redis::RespValue());
              EXPECT_CALL(splitter_, makeRequest_(Ref(*held_auth), _, _, _))
                  .WillOnce(
                      Invoke([&, held_ping_raw](
                                 const Common::Redis::RespValue&,
                                 CommandSplitter::SplitCallbacks& cb, Event::Dispatcher&,
                                 const StreamInfo::StreamInfo&) -> CommandSplitter::SplitRequest* {
                        EXPECT_CALL(*external_auth_client_,
                                    authenticateExternal(_, _, _, "bob", "secret2"))
                            .WillOnce(WithArgs<0, 1>(
                                Invoke([&, held_ping_raw](
                                           ExternalAuth::AuthenticateCallback& inner_callback,
                                           CommandSplitter::SplitCallbacks& inner_request) -> void {
                                  ExternalAuth::AuthenticateResponsePtr denied =
                                      std::make_unique<ExternalAuth::AuthenticateResponse>();
                                  denied->status =
                                      ExternalAuth::AuthenticationRequestStatus::Unauthorized;
                                  EXPECT_CALL(*encoder_, encode(Eq(ByRef(expected_auth_error)), _));
                                  EXPECT_CALL(filter_callbacks_.connection_, write(_, _));
                                  // The PING is dispatched by the OUTER drain loop after
                                  // this synchronous resolution returns.
                                  EXPECT_CALL(splitter_, makeRequest_(Ref(*held_ping_raw), _, _, _))
                                      .WillOnce(Invoke([&](const Common::Redis::RespValue&,
                                                           CommandSplitter::SplitCallbacks& ping_cb,
                                                           Event::Dispatcher&,
                                                           const StreamInfo::StreamInfo&)
                                                           -> CommandSplitter::SplitRequest* {
                                        EXPECT_CALL(*encoder_, encode(Eq(ByRef(expected_pong)), _));
                                        EXPECT_CALL(filter_callbacks_.connection_, write(_, _));
                                        Common::Redis::RespValuePtr pong(
                                            new Common::Redis::RespValue());
                                        pong->type(Common::Redis::RespType::SimpleString);
                                        pong->asString() = "PONG";
                                        ping_cb.onResponse(std::move(pong));
                                        return nullptr;
                                      }));
                                  inner_callback.onAuthenticateExternal(inner_request,
                                                                        std::move(denied));
                                })));
                        cb.onAuth("bob", "secret2");
                        return nullptr;
                      }));

              decoder_callbacks_->onRespValue(std::move(held_auth));
              decoder_callbacks_->onRespValue(std::move(held_ping));

              callback.onAuthenticateExternal(pending_request, std::move(auth_response));
            })));
        EXPECT_EQ(CommandSplitter::SplitCallbacks::AuthAttempt::ImplOwnsResponse,
                  callbacks.attemptDownstreamAuthInline("alice", "secret", 3));
        return nullptr;
      }));

  // Everything resolves within this single onData: HELLO Map, then the denied AUTH error,
  // then the PONG — all popped, so no teardown drain is needed.
  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(fake_data, false));
}

// Same reentrant shape as above but the second round trip resolves inline with status Error —
// the exact response GrpcExternalAuthClient::onFailure builds when send() fails synchronously
// (auth cluster removed via xDS). Pre-fix this scenario dereferenced an erased list iterator
// in resumeAuthHeldRequests.
TEST_F(RedisProxyFilterWithExternalAuthAndExpiration,
       HelloAuthResumedHeldAuthSynchronousFailureThenDrainContinues) {
  InSequence s;
  auto expected_hello = CommandSplitter::buildHelloReply(3);
  Common::Redis::RespValue expected_auth_error;
  expected_auth_error.type(Common::Redis::RespType::Error);
  expected_auth_error.asString() = "ERR external authentication failed";
  Common::Redis::RespValue expected_pong;
  expected_pong.type(Common::Redis::RespType::SimpleString);
  expected_pong.asString() = "PONG";

  Buffer::OwnedImpl fake_data;
  Common::Redis::RespValuePtr hello_request(new Common::Redis::RespValue());
  EXPECT_CALL(*decoder_, decode(Ref(fake_data))).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    decoder_callbacks_->onRespValue(std::move(hello_request));
  }));

  EXPECT_CALL(splitter_, makeRequest_(Ref(*hello_request), _, _, _))
      .WillOnce(Invoke([&](const Common::Redis::RespValue&,
                           CommandSplitter::SplitCallbacks& callbacks, Event::Dispatcher&,
                           const StreamInfo::StreamInfo&) -> CommandSplitter::SplitRequest* {
        EXPECT_CALL(*external_auth_client_, authenticateExternal(_, _, _, "alice", "secret"))
            .WillOnce(WithArgs<0, 1>(Invoke([&](ExternalAuth::AuthenticateCallback& callback,
                                                CommandSplitter::SplitCallbacks& pending_request)
                                                -> void {
              ExternalAuth::AuthenticateResponsePtr auth_response =
                  std::make_unique<ExternalAuth::AuthenticateResponse>();
              auth_response->status = ExternalAuth::AuthenticationRequestStatus::Authorized;
              auto time = time_source_.systemTime() + std::chrono::hours(1);
              auth_response->expiration.set_seconds(
                  duration_cast<std::chrono::seconds>(time.time_since_epoch()).count());

              EXPECT_CALL(*encoder_, setProtocolVersion(Common::Redis::RespProtocolVersion::Resp3));
              EXPECT_CALL(*encoder_, encode(Eq(ByRef(*expected_hello)), _));
              EXPECT_CALL(filter_callbacks_.connection_, write(_, _));

              Common::Redis::RespValuePtr held_ping(new Common::Redis::RespValue());
              Common::Redis::RespValue* held_ping_raw = held_ping.get();

              Common::Redis::RespValuePtr held_auth(new Common::Redis::RespValue());
              EXPECT_CALL(splitter_, makeRequest_(Ref(*held_auth), _, _, _))
                  .WillOnce(
                      Invoke([&, held_ping_raw](
                                 const Common::Redis::RespValue&,
                                 CommandSplitter::SplitCallbacks& cb, Event::Dispatcher&,
                                 const StreamInfo::StreamInfo&) -> CommandSplitter::SplitRequest* {
                        EXPECT_CALL(*external_auth_client_,
                                    authenticateExternal(_, _, _, "bob", "secret2"))
                            .WillOnce(WithArgs<0, 1>(
                                Invoke([&, held_ping_raw](
                                           ExternalAuth::AuthenticateCallback& inner_callback,
                                           CommandSplitter::SplitCallbacks& inner_request) -> void {
                                  ExternalAuth::AuthenticateResponsePtr failed =
                                      std::make_unique<ExternalAuth::AuthenticateResponse>();
                                  failed->status = ExternalAuth::AuthenticationRequestStatus::Error;
                                  EXPECT_CALL(*encoder_, encode(Eq(ByRef(expected_auth_error)), _));
                                  EXPECT_CALL(filter_callbacks_.connection_, write(_, _));
                                  EXPECT_CALL(splitter_, makeRequest_(Ref(*held_ping_raw), _, _, _))
                                      .WillOnce(Invoke([&](const Common::Redis::RespValue&,
                                                           CommandSplitter::SplitCallbacks& ping_cb,
                                                           Event::Dispatcher&,
                                                           const StreamInfo::StreamInfo&)
                                                           -> CommandSplitter::SplitRequest* {
                                        EXPECT_CALL(*encoder_, encode(Eq(ByRef(expected_pong)), _));
                                        EXPECT_CALL(filter_callbacks_.connection_, write(_, _));
                                        Common::Redis::RespValuePtr pong(
                                            new Common::Redis::RespValue());
                                        pong->type(Common::Redis::RespType::SimpleString);
                                        pong->asString() = "PONG";
                                        ping_cb.onResponse(std::move(pong));
                                        return nullptr;
                                      }));
                                  inner_callback.onAuthenticateExternal(inner_request,
                                                                        std::move(failed));
                                })));
                        cb.onAuth("bob", "secret2");
                        return nullptr;
                      }));

              decoder_callbacks_->onRespValue(std::move(held_auth));
              decoder_callbacks_->onRespValue(std::move(held_ping));

              callback.onAuthenticateExternal(pending_request, std::move(auth_response));
            })));
        EXPECT_EQ(CommandSplitter::SplitCallbacks::AuthAttempt::ImplOwnsResponse,
                  callbacks.attemptDownstreamAuthInline("alice", "secret", 3));
        return nullptr;
      }));

  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(fake_data, false));
}

// HELLO N AUTH ... succeeded + expiration set: the existing connectionAllowed expiration
// check applies to the HELLO AUTH path too. After the deferred HELLO Map is emitted the
// connection is allowed; advancing past expiration revokes it. Pins that the auth-expiration
// machinery is shared between the AUTH-command and HELLO-AUTH paths.
TEST_F(RedisProxyFilterWithExternalAuthAndExpiration, HelloAuthExternalAuthExpirationStillTracked) {
  InSequence s;
  auto expected_hello = CommandSplitter::buildHelloReply(3);

  Buffer::OwnedImpl fake_data;
  Common::Redis::RespValuePtr request(new Common::Redis::RespValue());
  EXPECT_CALL(*decoder_, decode(Ref(fake_data))).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    decoder_callbacks_->onRespValue(std::move(request));
  }));
  EXPECT_CALL(splitter_, makeRequest_(Ref(*request), _, _, _))
      .WillOnce(Invoke([&](const Common::Redis::RespValue&,
                           CommandSplitter::SplitCallbacks& callbacks, Event::Dispatcher&,
                           const StreamInfo::StreamInfo&) -> CommandSplitter::SplitRequest* {
        EXPECT_CALL(*external_auth_client_, authenticateExternal(_, _, _, "alice", "secret"))
            .WillOnce(WithArgs<0, 1>(
                Invoke([&](ExternalAuth::AuthenticateCallback& callback,
                           CommandSplitter::SplitCallbacks& pending_request) -> void {
                  ExternalAuth::AuthenticateResponsePtr auth_response =
                      std::make_unique<ExternalAuth::AuthenticateResponse>();
                  auth_response->status = ExternalAuth::AuthenticationRequestStatus::Authorized;
                  auto time = time_source_.systemTime() + std::chrono::hours(1);
                  auth_response->expiration.set_seconds(
                      duration_cast<std::chrono::seconds>(time.time_since_epoch()).count());
                  callback.onAuthenticateExternal(pending_request, std::move(auth_response));
                })));
        EXPECT_CALL(*encoder_, setProtocolVersion(Common::Redis::RespProtocolVersion::Resp3));
        EXPECT_CALL(*encoder_, encode(Eq(ByRef(*expected_hello)), _));
        EXPECT_CALL(filter_callbacks_.connection_, write(_, _));
        EXPECT_EQ(CommandSplitter::SplitCallbacks::AuthAttempt::ImplOwnsResponse,
                  callbacks.attemptDownstreamAuthInline("alice", "secret", 3));
        EXPECT_TRUE(filter_->connectionAllowed());
        time_source_.advanceTimeWait(std::chrono::hours(2));
        EXPECT_FALSE(filter_->connectionAllowed());
        return nullptr;
      }));

  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(fake_data, false));
}

// a pub/sub subscriber whose downstream write buffer overflows the high watermark is a slow
// consumer (it cannot pace unsolicited Push frames). Close it — matching Redis's
// client-output-buffer-limit pubsub eviction — rather than buffer without bound; a non-subscriber
// connection is left alone, and the close happens exactly once.
TEST_F(RedisProxyFilterTest, SlowSubscriberClosedOnWriteBufferHighWatermark) {
  // Non-subscriber connection: the high watermark is a no-op (a request/reply client drains its own
  // backlog by reading).
  filter_->onAboveWriteBufferHighWatermark();
  EXPECT_EQ(0UL, config_->stats_.pubsub_slow_subscriber_closed_.value());

  // Attach a pub/sub subscriber with active pub/sub work (a parked message push) to this
  // connection.
  attachSubscriberForTest();
  setHeldPushBytesForTest(1);
  EXPECT_CALL(filter_callbacks_.connection_, close(Network::ConnectionCloseType::NoFlush));
  filter_->onAboveWriteBufferHighWatermark();
  EXPECT_EQ(1UL, config_->stats_.pubsub_slow_subscriber_closed_.value());

  // Idempotent: the high watermark can re-fire before the close completes — do not close/count
  // twice.
  filter_->onAboveWriteBufferHighWatermark();
  EXPECT_EQ(1UL, config_->stats_.pubsub_slow_subscriber_closed_.value());
}

// The slow-subscriber watermark eviction only applies to a connection with
// ACTIVE pub/sub work. ``subscriber_`` lingers after the last UNSUBSCRIBE (and a bare UNSUBSCRIBE
// can create it), so a plain request/reply connection that merely once subscribed — now with no
// live subscriptions and no parked push bytes — must NOT be evicted on its own reply backlog.
TEST_F(RedisProxyFilterTest, SlowSubscriberNotClosedWithNoActiveSubscriptions) {
  attachSubscriberForTest(); // subscriber present but 0 subscriptions and 0 parked push bytes
  EXPECT_CALL(filter_callbacks_.connection_, close(_)).Times(0);
  filter_->onAboveWriteBufferHighWatermark();
  EXPECT_EQ(0UL, config_->stats_.pubsub_slow_subscriber_closed_.value());
}

// a MESSAGE push must not overtake an in-flight ordinary reply on a client that is both
// subscribed and issuing commands (self-``PUBLISH`` being the canonical case). With a GET in flight
// the push is parked behind it; when the GET reply lands, flushReadyResponses writes the reply and
// THEN the parked push, in one ordered batch — reply bytes strictly before push bytes on the wire.
TEST_F(RedisProxyFilterTest, MessagePushHeldBehindPendingReplyThenFlushedInOrder) {
  // Wire the subscriber through the production path so deliverMessage routes into the filter's
  // FIFO.
  DownstreamSubscriberPtr subscriber = getWiredSubscriberForTest();

  // One in-flight GET keeps pending_requests_ non-empty (its reply has not landed yet).
  Buffer::OwnedImpl fake_data;
  CommandSplitter::MockSplitRequest* get_handle = new CommandSplitter::MockSplitRequest();
  CommandSplitter::SplitCallbacks* get_callbacks{};
  EXPECT_CALL(*decoder_, decode(Ref(fake_data))).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    Common::Redis::RespValuePtr get_req(new Common::Redis::RespValue());
    EXPECT_CALL(splitter_, makeRequest_(Ref(*get_req), _, _, _))
        .WillOnce(DoAll(WithArg<1>(SaveArgAddress(&get_callbacks)), Return(get_handle)));
    decoder_callbacks_->onRespValue(std::move(get_req));
  }));
  // No write while the GET is in flight — including when the message push is parked below.
  EXPECT_CALL(filter_callbacks_.connection_, write(_, _)).Times(0);
  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(fake_data, false));

  // parked behind the in-flight GET, not written
  subscriber->deliverMessage(*makeMessagePush("message", "chat", "hello"));
  testing::Mock::VerifyAndClearExpectations(&filter_callbacks_.connection_);

  // GET reply lands: a single ordered write carrying the reply bytes BEFORE the parked push bytes.
  std::string written;
  EXPECT_CALL(filter_callbacks_.connection_, write(_, _))
      .WillOnce(Invoke([&](Buffer::Instance& data, bool) {
        written += data.toString();
        data.drain(data.length());
      }));
  Common::Redis::RespValuePtr get_reply(new Common::Redis::RespValue());
  get_reply->type(Common::Redis::RespType::SimpleString);
  get_reply->asString() = "OK";
  get_callbacks->onResponse(std::move(get_reply));

  ASSERT_NE(std::string::npos, written.find("+OK"));
  ASSERT_NE(std::string::npos, written.find("message"));
  EXPECT_LT(written.find("+OK"), written.find("message")); // reply strictly before the push

  testing::Mock::AllowLeak(get_handle);
  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
}

// Item 4 full-strict: a fresh SUBSCRIBE ack is deferred until its upstream SSUBSCRIBE confirms,
// so the request is HELD in the response FIFO at its arrival position (its handler returns a live
// handle rather than completing synchronously). A GET pipelined BEHIND it cannot have its reply
// flushed until the SUBSCRIBE completes — so the subscribe ack is strictly ordered BEFORE the GET
// reply, matching a single serial Redis connection (the SUBSCRIBE was issued first), even though
// the GET reply arrived first. The deferred counterpart to
// the FIFO-ordered unsubscribe-ack test above.
TEST_F(RedisProxyFilterTest, DeferredSubscribeAckOrderedBeforeLaterPipelinedReply) {
  // SUBSCRIBE arrives first; its handler holds the request (deferred ack) — capture its callbacks
  // and return a live handle so the FIFO entry stays open.
  Buffer::OwnedImpl sub_data;
  CommandSplitter::MockSplitRequest* sub_handle = new CommandSplitter::MockSplitRequest();
  CommandSplitter::SplitCallbacks* sub_callbacks{};
  EXPECT_CALL(*decoder_, decode(Ref(sub_data))).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    Common::Redis::RespValuePtr sub_req(new Common::Redis::RespValue());
    EXPECT_CALL(splitter_, makeRequest_(Ref(*sub_req), _, _, _))
        .WillOnce(DoAll(WithArg<1>(SaveArgAddress(&sub_callbacks)), Return(sub_handle)));
    decoder_callbacks_->onRespValue(std::move(sub_req));
  }));
  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(sub_data, false));

  // A GET pipelined behind the still-incomplete SUBSCRIBE.
  Buffer::OwnedImpl get_data;
  CommandSplitter::MockSplitRequest* get_handle = new CommandSplitter::MockSplitRequest();
  CommandSplitter::SplitCallbacks* get_callbacks{};
  EXPECT_CALL(*decoder_, decode(Ref(get_data))).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    Common::Redis::RespValuePtr get_req(new Common::Redis::RespValue());
    EXPECT_CALL(splitter_, makeRequest_(Ref(*get_req), _, _, _))
        .WillOnce(DoAll(WithArg<1>(SaveArgAddress(&get_callbacks)), Return(get_handle)));
    decoder_callbacks_->onRespValue(std::move(get_req));
  }));
  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(get_data, false));

  std::string written;
  auto capture = [&](Buffer::Instance& data, bool) {
    written += data.toString();
    data.drain(data.length());
  };

  // The GET reply lands FIRST but is held behind the incomplete SUBSCRIBE — nothing on the wire
  // yet.
  EXPECT_CALL(filter_callbacks_.connection_, write(_, _)).Times(0);
  Common::Redis::RespValuePtr get_reply(new Common::Redis::RespValue());
  get_reply->type(Common::Redis::RespType::SimpleString);
  get_reply->asString() = "OK";
  get_callbacks->onResponse(std::move(get_reply));
  testing::Mock::VerifyAndClearExpectations(&filter_callbacks_.connection_);

  // The upstream SSUBSCRIBE ack completes the SUBSCRIBE (respond with its ack frame): the subscribe
  // ack THEN the held GET reply flush in one contiguous write — subscribe ack first.
  EXPECT_CALL(filter_callbacks_.connection_, write(_, _)).WillOnce(Invoke(capture));
  CommandSplitter::RespValueFrames sub_frames;
  sub_frames.push_back(makeSubscribeAckPush("subscribe", "chat", 1));
  sub_callbacks->respond(std::move(sub_frames));

  ASSERT_NE(std::string::npos, written.find("subscribe"));
  ASSERT_NE(std::string::npos, written.find("OK"));
  EXPECT_LT(written.find("subscribe"), written.find("OK"));

  testing::Mock::AllowLeak(sub_handle);
  testing::Mock::AllowLeak(get_handle);
  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
}

// Blocker (full-strict FIFO ordering): a SUBSCRIBE is parked in the FIFO awaiting its upstream ack;
// an UNSUBSCRIBE of the SAME channel arrives BEHIND it (before the ack) and completes its own
// ``unsubscribe`` ack immediately. That ack must NOT overtake the still-pending SUBSCRIBE — it
// stays queued behind it. When the upstream SSUBSCRIBE ack finally completes the SUBSCRIBE, both
// flush in one contiguous write in COMMAND order: ``subscribe chat 1`` THEN ``unsubscribe chat 0``.
// (The registry-side guarantee that the parked SUBSCRIBE is completed via post rather than orphaned
// is covered by subscription_registry_test's UnsubscribeBeforeAckCompletesLiveTargetViaPost.)
TEST_F(RedisProxyFilterTest, DeferredSubscribeThenUnsubscribeFlushInCommandOrder) {
  // SUBSCRIBE chat first; its handler holds the request (deferred ack). Capture its callbacks and
  // return a live handle so the FIFO entry stays open.
  Buffer::OwnedImpl sub_data;
  CommandSplitter::MockSplitRequest* sub_handle = new CommandSplitter::MockSplitRequest();
  CommandSplitter::SplitCallbacks* sub_callbacks{};
  EXPECT_CALL(*decoder_, decode(Ref(sub_data))).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    Common::Redis::RespValuePtr sub_req(new Common::Redis::RespValue());
    EXPECT_CALL(splitter_, makeRequest_(Ref(*sub_req), _, _, _))
        .WillOnce(DoAll(WithArg<1>(SaveArgAddress(&sub_callbacks)), Return(sub_handle)));
    decoder_callbacks_->onRespValue(std::move(sub_req));
  }));
  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(sub_data, false));

  // UNSUBSCRIBE chat pipelined BEHIND the still-incomplete SUBSCRIBE.
  Buffer::OwnedImpl unsub_data;
  CommandSplitter::MockSplitRequest* unsub_handle = new CommandSplitter::MockSplitRequest();
  CommandSplitter::SplitCallbacks* unsub_callbacks{};
  EXPECT_CALL(*decoder_, decode(Ref(unsub_data))).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    Common::Redis::RespValuePtr unsub_req(new Common::Redis::RespValue());
    EXPECT_CALL(splitter_, makeRequest_(Ref(*unsub_req), _, _, _))
        .WillOnce(DoAll(WithArg<1>(SaveArgAddress(&unsub_callbacks)), Return(unsub_handle)));
    decoder_callbacks_->onRespValue(std::move(unsub_req));
  }));
  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(unsub_data, false));

  std::string written;
  auto capture = [&](Buffer::Instance& data, bool) {
    written += data.toString();
    data.drain(data.length());
  };

  // The UNSUBSCRIBE completes first (its ack is immediate) but is held behind the incomplete
  // SUBSCRIBE — nothing on the wire yet.
  EXPECT_CALL(filter_callbacks_.connection_, write(_, _)).Times(0);
  CommandSplitter::RespValueFrames unsub_frames;
  unsub_frames.push_back(makeSubscribeAckPush("unsubscribe", "chat", 0));
  unsub_callbacks->respond(std::move(unsub_frames));
  EXPECT_TRUE(written.empty());
  testing::Mock::VerifyAndClearExpectations(&filter_callbacks_.connection_);

  // The upstream SSUBSCRIBE ack completes the SUBSCRIBE: subscribe ack THEN the held unsubscribe
  // ack flush in one contiguous write, in command order.
  EXPECT_CALL(filter_callbacks_.connection_, write(_, _)).WillOnce(Invoke(capture));
  CommandSplitter::RespValueFrames sub_frames;
  sub_frames.push_back(makeSubscribeAckPush("subscribe", "chat", 1));
  sub_callbacks->respond(std::move(sub_frames));

  // Wire order: the subscribe ack precedes the unsubscribe ack. Disambiguate the substring
  // collision (``subscribe`` is a substring of ``unsubscribe``) via the RESP bulk-string length
  // prefix: ``$9\r\nsubscribe`` matches only the 9-char subscribe verb, ``$11\r\nunsubscribe`` only
  // the 11-char unsubscribe verb.
  const auto sub_pos = written.find("$9\r\nsubscribe");
  const auto unsub_pos = written.find("$11\r\nunsubscribe");
  ASSERT_NE(std::string::npos, sub_pos);
  ASSERT_NE(std::string::npos, unsub_pos);
  EXPECT_LT(sub_pos, unsub_pos);

  testing::Mock::AllowLeak(sub_handle);
  testing::Mock::AllowLeak(unsub_handle);
  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
}

// Item 4 (control-ack FIFO ordering): an UNSUBSCRIBE ack is known immediately at command time, so
// the splitter emits it as a response FRAME on the UNSUBSCRIBE request rather than out-of-band via
// deliver(). It therefore flows through the pending-request FIFO and flushes at that request's
// position, and CANNOT overtake a MESSAGE already parked behind an earlier in-flight reply. A slow
// GET keeps the FIFO non-empty; a ``message chat`` parks behind it; an UNSUBSCRIBE arrives behind
// the GET and completes with its ``unsubscribe chat 0`` ack frame but stays queued behind the
// incomplete GET; when the GET reply lands the drain emits ``+OK``, then the parked message, THEN
// the unsubscribe ack. Wire order ``+OK``, ``message chat ...``, ``unsubscribe chat 0`` — the ack
// no longer trails after a message the client already saw for a channel it just closed.
TEST_F(RedisProxyFilterTest, UnsubscribeAckOrderedBehindParkedMessageViaFifo) {
  DownstreamSubscriberPtr subscriber = getWiredSubscriberForTest();

  // One in-flight GET keeps pending_requests_ non-empty (its reply has not landed yet).
  Buffer::OwnedImpl fake_data;
  CommandSplitter::MockSplitRequest* get_handle = new CommandSplitter::MockSplitRequest();
  CommandSplitter::SplitCallbacks* get_callbacks{};
  EXPECT_CALL(*decoder_, decode(Ref(fake_data))).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    Common::Redis::RespValuePtr get_req(new Common::Redis::RespValue());
    EXPECT_CALL(splitter_, makeRequest_(Ref(*get_req), _, _, _))
        .WillOnce(DoAll(WithArg<1>(SaveArgAddress(&get_callbacks)), Return(get_handle)));
    decoder_callbacks_->onRespValue(std::move(get_req));
  }));
  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(fake_data, false));

  std::string written;
  auto capture = [&](Buffer::Instance& data, bool) {
    written += data.toString();
    data.drain(data.length());
  };

  // A MESSAGE for "chat" parks behind the in-flight GET — nothing on the wire yet.
  EXPECT_CALL(filter_callbacks_.connection_, write(_, _)).Times(0);
  subscriber->deliverMessage(*makeMessagePush("message", "chat", "hello"));
  testing::Mock::VerifyAndClearExpectations(&filter_callbacks_.connection_);

  // An UNSUBSCRIBE arrives BEHIND the GET; the splitter completes it with its ack as a response
  // frame (respond()), NOT an out-of-band deliver(). Queued behind the still-incomplete GET, so
  // nothing reaches the wire yet.
  Buffer::OwnedImpl unsub_data;
  CommandSplitter::MockSplitRequest* unsub_handle = new CommandSplitter::MockSplitRequest();
  CommandSplitter::SplitCallbacks* unsub_callbacks{};
  EXPECT_CALL(*decoder_, decode(Ref(unsub_data))).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    Common::Redis::RespValuePtr unsub_req(new Common::Redis::RespValue());
    EXPECT_CALL(splitter_, makeRequest_(Ref(*unsub_req), _, _, _))
        .WillOnce(DoAll(WithArg<1>(SaveArgAddress(&unsub_callbacks)), Return(unsub_handle)));
    decoder_callbacks_->onRespValue(std::move(unsub_req));
  }));
  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(unsub_data, false));

  EXPECT_CALL(filter_callbacks_.connection_, write(_, _)).Times(0);
  CommandSplitter::RespValueFrames unsub_frames;
  unsub_frames.push_back(makeSubscribeAckPush("unsubscribe", "chat", 0));
  unsub_callbacks->respond(std::move(unsub_frames));
  EXPECT_TRUE(written.empty()); // ack held behind the incomplete GET
  testing::Mock::VerifyAndClearExpectations(&filter_callbacks_.connection_);

  // The GET reply lands: the drain flushes ``+OK``, then the parked message, THEN the unsubscribe
  // ack — one contiguous write.
  EXPECT_CALL(filter_callbacks_.connection_, write(_, _)).WillOnce(Invoke(capture));
  Common::Redis::RespValuePtr get_reply(new Common::Redis::RespValue());
  get_reply->type(Common::Redis::RespType::SimpleString);
  get_reply->asString() = "OK";
  get_callbacks->onResponse(std::move(get_reply));

  // The MESSAGE reached the wire BEFORE the unsubscribe ack — the ack no longer overtakes it.
  ASSERT_NE(std::string::npos, written.find("message"));
  ASSERT_NE(std::string::npos, written.find("unsubscribe"));
  EXPECT_LT(written.find("message"), written.find("unsubscribe"));

  testing::Mock::AllowLeak(get_handle);
  testing::Mock::AllowLeak(unsub_handle);
  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
}

// Fast path: with no ordinary reply in flight, a MESSAGE push goes straight to the wire in
// arrival order (nothing to order behind).
TEST_F(RedisProxyFilterTest, MessagePushWrittenImmediatelyWithNoPendingReply) {
  DownstreamSubscriberPtr subscriber = getWiredSubscriberForTest();

  std::string written;
  EXPECT_CALL(filter_callbacks_.connection_, write(_, _))
      .WillOnce(Invoke([&](Buffer::Instance& data, bool) {
        written += data.toString();
        data.drain(data.length());
      }));
  subscriber->deliverMessage(*makeMessagePush("message", "chat", "hello"));
  EXPECT_NE(std::string::npos, written.find("message"));
  EXPECT_EQ(1UL, config_->stats_.pubsub_push_messages_delivered_.value());
}

// Backpressure: parked push bytes live in an app buffer the connection high-watermark cannot
// see, so enqueueOrderedPush bounds them by the same per-connection limit. A subscriber whose FIFO
// stalls while pushes pile up past the limit is evicted (closed) — the app-queue analog of the
// watermark path — instead of buffering without limit.
TEST_F(RedisProxyFilterTest, MessagePushBackpressureClosesSubscriberWhenParkedBytesExceedLimit) {
  DownstreamSubscriberPtr subscriber = getWiredSubscriberForTest();
  // A tiny per-connection limit so a single parked message trips the bound.
  ON_CALL(filter_callbacks_.connection_, bufferLimit()).WillByDefault(Return(4));

  // One in-flight GET so the push is parked (not written straight out).
  Buffer::OwnedImpl fake_data;
  CommandSplitter::MockSplitRequest* get_handle = new CommandSplitter::MockSplitRequest();
  CommandSplitter::SplitCallbacks* get_callbacks{};
  EXPECT_CALL(*decoder_, decode(Ref(fake_data))).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    Common::Redis::RespValuePtr get_req(new Common::Redis::RespValue());
    EXPECT_CALL(splitter_, makeRequest_(Ref(*get_req), _, _, _))
        .WillOnce(DoAll(WithArg<1>(SaveArgAddress(&get_callbacks)), Return(get_handle)));
    decoder_callbacks_->onRespValue(std::move(get_req));
  }));
  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(fake_data, false));

  // The parked bytes (~35) exceed the 4-byte limit -> the subscriber is evicted, stat bumped.
  // close(NoFlush) raises LocalClose synchronously, so ProxyFilter::onEvent runs inline and cancels
  // the still-in-flight GET before its PendingRequest (which owns the handle) is destroyed. That
  // synchronous teardown also empties pending_requests_, so no explicit disconnect is needed here.
  testing::Mock::AllowLeak(get_handle);
  EXPECT_CALL(*get_handle, cancel());
  EXPECT_CALL(filter_callbacks_.connection_, close(Network::ConnectionCloseType::NoFlush));
  subscriber->deliverMessage(*makeMessagePush("message", "chat", "hello"));
  EXPECT_EQ(1UL, config_->stats_.pubsub_slow_subscriber_closed_.value());
}

// Backpressure (H-1): the fast path (empty FIFO -> straight to the connection write buffer) cannot
// rely on the EDGE-triggered high-watermark callback alone, which can be latched-consumed while the
// eviction gate is false (e.g. a pre-SUBSCRIBE reply backlog crossing the mark before
// ``subscriber_`` exists). A LEVEL check before the fast-path write evicts an active subscriber
// whose write buffer is already over the high mark, so an unread subscriber's push burst cannot
// grow it without bound even when the callback's latch is stuck set. Without the fix the message
// would be written unbounded and no eviction would occur.
TEST_F(RedisProxyFilterTest, FastPathPushEvictsSubscriberWhenWriteBufferAboveHighWatermark) {
  DownstreamSubscriberPtr subscriber = getWiredSubscriberForTest();
  subscriber->addChannel("chat"); // an active subscription, so the eviction gate applies

  // Simulate the latch-consumed state: the connection is already above the high watermark (an
  // earlier reply backlog fired the edge callback while subscriber_ was still null), so that
  // callback will NOT re-fire for this push.
  ON_CALL(filter_callbacks_.connection_, aboveHighWatermark()).WillByDefault(Return(true));

  // No in-flight request -> pending_requests_ is empty -> the push takes the fast path. The level
  // check evicts the active subscriber (close + stat) BEFORE writing, so the push is not written.
  EXPECT_CALL(filter_callbacks_.connection_, write(_, _)).Times(0);
  EXPECT_CALL(filter_callbacks_.connection_, close(Network::ConnectionCloseType::NoFlush));
  subscriber->deliverMessage(*makeMessagePush("message", "chat", "hello"));
  EXPECT_EQ(1UL, config_->stats_.pubsub_slow_subscriber_closed_.value());

  subscriber->removeChannel("chat"); // reset the active-subscriptions gauge for the dtor check
}

// Backpressure (H-1) negative case: an active subscriber whose connection is BELOW the high
// watermark is not evicted — the level check gates on the buffer level, so a healthy subscriber's
// fast-path push is written normally with no spurious close. Pairs with
// FastPathPushEvictsSubscriberWhenWriteBufferAboveHighWatermark (the above-mark case) so the
// gate is pinned on both sides.
TEST_F(RedisProxyFilterTest, FastPathPushWritesActiveSubscriberBelowHighWatermark) {
  DownstreamSubscriberPtr subscriber = getWiredSubscriberForTest();
  subscriber->addChannel("chat"); // an active subscription, so the eviction gate applies

  // Connection is under the high watermark: the level check must not evict.
  ON_CALL(filter_callbacks_.connection_, aboveHighWatermark()).WillByDefault(Return(false));

  // No in-flight request -> fast path. Below the mark, the push is written to the wire and the
  // subscriber is not closed.
  EXPECT_CALL(filter_callbacks_.connection_, write(_, false));
  EXPECT_CALL(filter_callbacks_.connection_, close(_)).Times(0);
  subscriber->deliverMessage(*makeMessagePush("message", "chat", "hello"));
  EXPECT_EQ(0UL, config_->stats_.pubsub_slow_subscriber_closed_.value());
  EXPECT_EQ(1UL, config_->stats_.pubsub_push_messages_delivered_.value());

  subscriber->removeChannel("chat"); // reset the active-subscriptions gauge for the dtor check
}

// Backpressure (H-1) + real LocalClose: the fast-path eviction closes the connection, and the
// resulting LocalClose event must tear the subscriber down cleanly — registry removeSubscriber,
// unsubscribe accounting, and subscriber_.reset() — with no use-after-free, not just a mock close()
// observed in isolation. Exercises the same closeSlowSubscriber -> close -> onEvent(LocalClose)
// path the park-path eviction test relies on, but for the empty-FIFO fast path.
TEST_F(RedisProxyFilterTest, FastPathEvictionLocalCloseTearsDownSubscriberCleanly) {
  DownstreamSubscriberPtr subscriber = getWiredSubscriberForTest();
  subscriber->addChannel("chat");
  ON_CALL(filter_callbacks_.connection_, aboveHighWatermark()).WillByDefault(Return(true));

  // Fast-path push over the high watermark -> evict + close (no write).
  EXPECT_CALL(filter_callbacks_.connection_, write(_, _)).Times(0);
  EXPECT_CALL(filter_callbacks_.connection_, close(Network::ConnectionCloseType::NoFlush));
  subscriber->deliverMessage(*makeMessagePush("message", "chat", "hello"));
  EXPECT_EQ(1UL, config_->stats_.pubsub_slow_subscriber_closed_.value());

  // Drive the LocalClose (idempotent if close() already raised it inline): onEvent removes the
  // subscriber from its registry, counts the held channel as an unsubscribe, and resets
  // subscriber_. Must not crash / double-free; the disconnect-unsubscribe accounting fires once.
  filter_->onEvent(Network::ConnectionEvent::LocalClose);
  EXPECT_EQ(1UL, config_->stats_.pubsub_unsubscribe_total_.value());
}

// G-1: the transaction client's UPSTREAM connection close (a keyed EXEC/DISCARD closing the
// transaction, or an upstream dying mid-transaction) routes through the upstream_transaction_cb_
// adapter with is_downstream=false. It must NOT tear down the downstream subscriber — real Redis
// keeps subscriptions across MULTI/EXEC. Before the fix ProxyFilter was itself the transaction
// client's callback, so this path ran the subscriber teardown and silently destroyed the
// subscription while the downstream connection stayed open (future messages lost, no error).
TEST_F(RedisProxyFilterTest, SubscriberSurvivesUpstreamTransactionClientClose) {
  DownstreamSubscriberPtr subscriber = getWiredSubscriberForTest();
  subscriber->addChannel("chat");

  // Drive the close through the REAL callback pointer the conn pool registers for the transaction
  // client's upstream connection: transaction().connection_cb_, which the ctor wires to
  // &upstream_transaction_cb_ (routing to handleConnectionEvent with is_downstream=false). Driving
  // this pointer — rather than calling the helper with a hand-injected is_downstream=false — pins
  // the wiring itself: a regression reverting the ctor to transaction_(this) would route the event
  // through is_downstream=true and tear the subscriber down, failing the asserts below (a change
  // that would otherwise only break compilation elsewhere, not this behavioral test).
  ASSERT_NE(nullptr, filter_->transaction().connection_cb_);
  filter_->transaction().connection_cb_->onEvent(Network::ConnectionEvent::LocalClose);
  EXPECT_EQ(0UL, config_->stats_.pubsub_unsubscribe_total_.value());
  EXPECT_FALSE(subscriber->subscribedChannels().empty()); // still subscribed to "chat"
  EXPECT_TRUE(filterHoldsSubscriber());

  // Contrast: a genuine downstream close (is_downstream=true) does tear it down.
  driveConnectionEvent(Network::ConnectionEvent::LocalClose, /*is_downstream=*/true);
  EXPECT_EQ(1UL, config_->stats_.pubsub_unsubscribe_total_.value());
  EXPECT_FALSE(filterHoldsSubscriber());
}

// R8-1 FIFO preservation: an upstream transaction-client close (is_downstream=false) must NOT
// cancel the downstream's in-flight FIFO requests — they belong to a live downstream and must
// survive; only a genuine downstream close cancels them. The sibling test above runs with an empty
// FIFO, so a regression moving the cancel loop outside the is_downstream gate would slip past it —
// this drives a real pending request to pin that behavior.
TEST_F(RedisProxyFilterTest, UpstreamTransactionClientCloseDoesNotCancelPendingRequests) {
  DownstreamSubscriberPtr subscriber = getWiredSubscriberForTest();
  subscriber->addChannel("chat");

  // Drive one in-flight pipelined request so pending_requests_ holds a live handle.
  Buffer::OwnedImpl fake_data;
  CommandSplitter::MockSplitRequest* request_handle1 = new CommandSplitter::MockSplitRequest();
  EXPECT_CALL(*decoder_, decode(Ref(fake_data))).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    Common::Redis::RespValuePtr request1(new Common::Redis::RespValue());
    EXPECT_CALL(splitter_, makeRequest_(Ref(*request1), _, _, _)).WillOnce(Return(request_handle1));
    decoder_callbacks_->onRespValue(std::move(request1));
  }));
  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(fake_data, false));

  // Transaction-client close via the real connection_cb_ (is_downstream=false): the pending
  // request must NOT be cancelled and the subscriber must survive.
  EXPECT_CALL(*request_handle1, cancel()).Times(0);
  ASSERT_NE(nullptr, filter_->transaction().connection_cb_);
  filter_->transaction().connection_cb_->onEvent(Network::ConnectionEvent::LocalClose);
  EXPECT_TRUE(filterHoldsSubscriber());
  // Consume the Times(0) so the downstream-close cancel below applies fresh.
  testing::Mock::VerifyAndClearExpectations(request_handle1);

  // A genuine downstream close then cancels the pending request and tears the subscriber down,
  // leaving pending_requests_ empty for the ~ProxyFilter ASSERT.
  EXPECT_CALL(*request_handle1, cancel());
  driveConnectionEvent(Network::ConnectionEvent::LocalClose, /*is_downstream=*/true);
  EXPECT_FALSE(filterHoldsSubscriber());
}

} // namespace RedisProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
