#include <vector>

#include "envoy/extensions/filters/network/redis_proxy/v3/redis_proxy.pb.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/network/utility.h"
#include "source/common/upstream/upstream_impl.h"
#include "source/extensions/filters/network/common/redis/client_impl.h"
#include "source/extensions/filters/network/common/redis/utility.h"

#include "test/extensions/common/aws/mocks.h"
#include "test/extensions/filters/network/common/redis/mocks.h"
#include "test/extensions/filters/network/common/redis/test_utils.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/upstream/host.h"
#include "test/test_common/simulated_time_system.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::An;
using testing::DoAll;
using testing::Eq;
using testing::InSequence;
using testing::Invoke;
using testing::Property;
using testing::Ref;
using testing::Return;
using testing::SaveArg;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Common {
namespace Redis {
namespace Client {

class RedisClientImplTest : public testing::Test,
                            public Event::TestUsingSimulatedTime,
                            public Common::Redis::DecoderFactory {
public:
  // Common::Redis::DecoderFactory
  Common::Redis::DecoderPtr create(Common::Redis::DecoderCallbacks& callbacks) override {
    callbacks_ = &callbacks;
    return Common::Redis::DecoderPtr{decoder_};
  }

  ~RedisClientImplTest() override {
    client_.reset();

    EXPECT_TRUE(TestUtility::gaugesZeroed(host_->cluster_.stats_store_.gauges()));
    EXPECT_TRUE(TestUtility::gaugesZeroed(host_->stats_.gauges()));
  }

  void setup() {
    config_ = std::make_shared<ConfigImpl>(createConnPoolSettings());
    finishSetup();
  }

  void setup(std::shared_ptr<Config> config) {
    config_ = config;
    finishSetup();
  }

  void finishSetup() {
    upstream_connection_ = new NiceMock<Network::MockClientConnection>();
    Upstream::MockHost::MockCreateConnectionData conn_info;
    conn_info.connection_ = upstream_connection_;

    // Create timers in order they are created in client_impl.cc
    connect_or_op_timer_ = new Event::MockTimer(&dispatcher_);
    flush_timer_ = new Event::MockTimer(&dispatcher_);

    EXPECT_CALL(*connect_or_op_timer_, enableTimer(_, _));
    EXPECT_CALL(*host_, createConnection_(_, _)).WillOnce(Return(conn_info));
    EXPECT_CALL(*upstream_connection_, addReadFilter(_))
        .WillOnce(SaveArg<0>(&upstream_read_filter_));
    EXPECT_CALL(*upstream_connection_, connect());
    EXPECT_CALL(*upstream_connection_, noDelay(true));

    redis_command_stats_ =
        Common::Redis::RedisCommandStats::createRedisCommandStats(stats_.symbolTable());

    client_ = ClientImpl::create(host_, dispatcher_, Common::Redis::EncoderPtr{encoder_}, *this,
                                 config_, redis_command_stats_, *stats_.rootScope(), false,
                                 aws_iam_config_, aws_iam_authenticator_,
                                 upstream_protocol_version_, hello3_failure_counter_);
    EXPECT_EQ(1UL, host_->cluster_.traffic_stats_->upstream_cx_total_.value());
    EXPECT_EQ(1UL, host_->stats_.cx_total_.value());
    EXPECT_EQ(false, client_->active());

    // NOP currently.
    upstream_connection_->runHighWatermarkCallbacks();
    upstream_connection_->runLowWatermarkCallbacks();
  }

  void onConnected() {
    EXPECT_CALL(*connect_or_op_timer_, enableTimer(_, _));
    upstream_connection_->raiseEvent(Network::ConnectionEvent::Connected);
  }

  void respond() {
    Common::Redis::RespValuePtr response1{new Common::Redis::RespValue()};
    response1->type(Common::Redis::RespType::SimpleString);
    response1->asString() = "OK";
    EXPECT_EQ(true, client_->active());
    ClientImpl* client_impl = dynamic_cast<ClientImpl*>(client_.get());
    EXPECT_NE(client_impl, nullptr);
    client_impl->onRespValue(std::move(response1));
  }

  // Drive an arbitrary upstream response through ClientImpl's private onRespValue. The fixture
  // is friend-declared on ClientImpl; TEST_F subclasses are not, so they go through this helper.
  void respondWith(Common::Redis::RespValuePtr value) {
    ClientImpl* client_impl = dynamic_cast<ClientImpl*>(client_.get());
    ASSERT_NE(client_impl, nullptr);
    client_impl->onRespValue(std::move(value));
  }

  void testInitializeReadPolicy(
      envoy::extensions::filters::network::redis_proxy::v3::RedisProxy::ConnPoolSettings::ReadPolicy
          read_policy) {
    InSequence s;

    setup(std::make_unique<ConfigImpl>(createConnPoolSettings(20, true, true, 100, read_policy)));

    Common::Redis::RespValue readonly_request = Utility::ReadOnlyRequest::instance();
    EXPECT_CALL(*encoder_, encode(Eq(readonly_request), _));
    EXPECT_CALL(*flush_timer_, enabled()).WillOnce(Return(false));
    client_->initialize(auth_username_, auth_password_);

    EXPECT_EQ(1UL, host_->cluster_.traffic_stats_->upstream_rq_total_.value());
    EXPECT_EQ(1UL, host_->cluster_.traffic_stats_->upstream_rq_active_.value());
    EXPECT_EQ(1UL, host_->stats_.rq_total_.value());
    EXPECT_EQ(1UL, host_->stats_.rq_active_.value());

    EXPECT_CALL(*upstream_connection_, close(Network::ConnectionCloseType::NoFlush));
    EXPECT_CALL(*connect_or_op_timer_, disableTimer());
    client_->close();
  }

  // Helper: configure the fixture for an RESP3 client. Sets the upstream protocol version that
  // finishSetup() threads into ClientImpl::create, and wires a real HELLO 3 failure counter
  // (resolved against the test's IsolatedStoreImpl) so RESP3 negotiation-failure tests can
  // assert the increment.
  void enableResp3() {
    upstream_protocol_version_ = envoy::extensions::filters::network::redis_proxy::v3::
        RedisProtocolOptions::UpstreamProtocol::RESP3;
    hello3_failure_counter_ = &stats_.counterFromString("redis.upstream_resp3_hello_failure_test");
  }

  // Helper: wire up AWS IAM auth for the test. After this, finishSetup()'s ClientImpl::create
  // call passes a non-null authenticator + config; ClientImpl::initialize will route through
  // the AWS IAM init path (WaitingForAwsToken state) instead of the synchronous RESP2/RESP3
  // paths.
  void enableAwsIam() {
    auto signer = std::make_unique<Extensions::Common::Aws::MockSigner>();
    mock_aws_iam_authenticator_ = std::make_shared<
        NetworkFilters::Common::Redis::AwsIamAuthenticator::MockAwsIamAuthenticator>(
        std::move(signer));
    aws_iam_authenticator_ = mock_aws_iam_authenticator_;
    envoy::extensions::filters::network::redis_proxy::v3::AwsIam cfg;
    cfg.set_region("us-east-1");
    cfg.set_cache_name("cachename");
    cfg.set_service_name("elasticache");
    aws_iam_config_ = cfg;
  }

  const std::string cluster_name_{"foo"};
  std::shared_ptr<Upstream::MockHost> host_{new NiceMock<Upstream::MockHost>()};
  Event::MockDispatcher dispatcher_;
  Event::MockTimer* flush_timer_{};
  Event::MockTimer* connect_or_op_timer_{};
  MockEncoder* encoder_{new MockEncoder()};
  MockDecoder* decoder_{new MockDecoder()};
  Common::Redis::DecoderCallbacks* callbacks_{};
  NiceMock<Network::MockClientConnection>* upstream_connection_{};
  Network::ReadFilterSharedPtr upstream_read_filter_;
  std::shared_ptr<Config> config_;
  ClientPtr client_;
  NiceMock<Stats::MockIsolatedStatsStore> stats_;
  Stats::ScopeSharedPtr stats_scope_;
  Common::Redis::RedisCommandStatsSharedPtr redis_command_stats_;
  std::string auth_username_;
  std::string auth_password_;
  // RESP3 plumbing for tests that exercise the new init pipeline. Default to UNSPECIFIED +
  // nullptr so existing tests stay on the legacy RESP2 path.
  envoy::extensions::filters::network::redis_proxy::v3::RedisProtocolOptions::UpstreamProtocol::
      Version upstream_protocol_version_{envoy::extensions::filters::network::redis_proxy::v3::
                                             RedisProtocolOptions::UpstreamProtocol::UNSPECIFIED};
  Stats::Counter* hello3_failure_counter_{nullptr};
  // AWS IAM plumbing — set via enableAwsIam() before setup() to engage the AWS IAM init path.
  absl::optional<envoy::extensions::filters::network::redis_proxy::v3::AwsIam> aws_iam_config_;
  absl::optional<NetworkFilters::Common::Redis::AwsIamAuthenticator::AwsIamAuthenticatorSharedPtr>
      aws_iam_authenticator_;
  std::shared_ptr<NetworkFilters::Common::Redis::AwsIamAuthenticator::MockAwsIamAuthenticator>
      mock_aws_iam_authenticator_;
};

TEST_F(RedisClientImplTest, BatchWithZeroBufferAndTimeout) {
  // Basic test with a single request, default buffer size (0) and timeout (0).
  // This means we do not batch requests, and thus the flush timer is never enabled.
  InSequence s;

  setup();

  // Make the dummy request
  Common::Redis::RespValue request1;
  MockClientCallbacks callbacks1;
  EXPECT_CALL(*encoder_, encode(Ref(request1), _));
  EXPECT_CALL(*flush_timer_, enabled()).WillOnce(Return(false));
  PoolRequest* handle1 = client_->makeRequest(request1, callbacks1);
  EXPECT_NE(nullptr, handle1);

  // Process the dummy request
  Buffer::OwnedImpl fake_data;
  EXPECT_CALL(*decoder_, decode(Ref(fake_data))).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    InSequence s;
    Common::Redis::RespValuePtr response1(new Common::Redis::RespValue());
    EXPECT_CALL(callbacks1, onResponse_(Ref(response1)));
    EXPECT_CALL(*connect_or_op_timer_, disableTimer());
    EXPECT_CALL(host_->outlier_detector_,
                putResult(Upstream::Outlier::Result::ExtOriginRequestSuccess, _));
    callbacks_->onRespValue(std::move(response1));
  }));
  upstream_read_filter_->onData(fake_data, false);

  EXPECT_CALL(*connect_or_op_timer_, disableTimer());
  client_->close();
}

class ConfigBufferSizeGTSingleRequest : public Config {
  bool disableOutlierEvents() const override { return false; }
  std::chrono::milliseconds opTimeout() const override { return std::chrono::milliseconds(25); }
  bool enableHashtagging() const override { return false; }
  bool enableRedirection() const override { return false; }
  unsigned int maxBufferSizeBeforeFlush() const override { return 8; }
  std::chrono::milliseconds bufferFlushTimeoutInMs() const override {
    return std::chrono::milliseconds(1);
  }
  uint32_t maxUpstreamUnknownConnections() const override { return 0; }
  bool enableCommandStats() const override { return false; }
  ReadPolicy readPolicy() const override { return ReadPolicy::Primary; }
  bool connectionRateLimitEnabled() const override { return false; }
  uint32_t connectionRateLimitPerSec() const override { return 0; }
};

TEST_F(RedisClientImplTest, BatchWithTimerFiring) {
  // With a flush buffer > single request length, the flush timer comes into play.
  // In this test, we make a single request that doesn't fill the buffer, so we
  // have to wait for the flush timer to fire.
  InSequence s;

  setup(std::make_shared<ConfigBufferSizeGTSingleRequest>());

  // Make the dummy request
  Common::Redis::RespValue request1;
  MockClientCallbacks callbacks1;
  EXPECT_CALL(*encoder_, encode(Ref(request1), _));
  EXPECT_CALL(*flush_timer_, enableTimer(_, _));
  PoolRequest* handle1 = client_->makeRequest(request1, callbacks1);
  EXPECT_NE(nullptr, handle1);

  // Pretend the flush timer fires.
  // The timer callback is the general-purpose flush function, also used when
  // the buffer is filled. If the buffer fills before the timer fires, we need
  // to check if the timer is active and cancel it. However, if the timer fires
  // the callback, this internal check returns false as the timer is finished.
  EXPECT_CALL(*flush_timer_, enabled()).WillOnce(Return(false));
  flush_timer_->invokeCallback();

  // Process the dummy request
  Buffer::OwnedImpl fake_data;
  EXPECT_CALL(*decoder_, decode(Ref(fake_data))).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    InSequence s;
    Common::Redis::RespValuePtr response1(new Common::Redis::RespValue());
    EXPECT_CALL(callbacks1, onResponse_(Ref(response1)));
    EXPECT_CALL(*connect_or_op_timer_, disableTimer());
    EXPECT_CALL(host_->outlier_detector_,
                putResult(Upstream::Outlier::Result::ExtOriginRequestSuccess, _));
    callbacks_->onRespValue(std::move(response1));
  }));
  upstream_read_filter_->onData(fake_data, false);

  EXPECT_CALL(*connect_or_op_timer_, disableTimer());
  client_->close();
}

TEST_F(RedisClientImplTest, BatchWithTimerCancelledByBufferFlush) {
  // Expanding on the previous test, let's the flush buffer is filled by two requests.
  // In this test, we make a single request that doesn't fill the buffer, and the timer
  // starts. However, a second request comes in, which should cancel the timer, such
  // that it is never invoked.
  InSequence s;

  setup(std::make_shared<ConfigBufferSizeGTSingleRequest>());

  // Make the dummy request (doesn't fill buffer, starts timer)
  Common::Redis::RespValue request1;
  MockClientCallbacks callbacks1;
  EXPECT_CALL(*encoder_, encode(Ref(request1), _));
  EXPECT_CALL(*flush_timer_, enableTimer(_, _));
  PoolRequest* handle1 = client_->makeRequest(request1, callbacks1);
  EXPECT_NE(nullptr, handle1);

  // Make a second dummy request (fills buffer, cancels timer)
  Common::Redis::RespValue request2;
  MockClientCallbacks callbacks2;
  EXPECT_CALL(*encoder_, encode(Ref(request2), _));
  EXPECT_CALL(*flush_timer_, enabled()).WillOnce(Return(true));
  ;
  EXPECT_CALL(*flush_timer_, disableTimer());
  PoolRequest* handle2 = client_->makeRequest(request2, callbacks2);
  EXPECT_NE(nullptr, handle2);

  // Process the dummy requests
  Buffer::OwnedImpl fake_data;
  EXPECT_CALL(*decoder_, decode(Ref(fake_data))).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    InSequence s;
    Common::Redis::RespValuePtr response1(new Common::Redis::RespValue());
    EXPECT_CALL(callbacks1, onResponse_(Ref(response1)));
    EXPECT_CALL(*connect_or_op_timer_, enableTimer(_, _));
    EXPECT_CALL(host_->outlier_detector_,
                putResult(Upstream::Outlier::Result::ExtOriginRequestSuccess, _));
    callbacks_->onRespValue(std::move(response1));

    Common::Redis::RespValuePtr response2(new Common::Redis::RespValue());
    EXPECT_CALL(callbacks2, onResponse_(Ref(response2)));
    EXPECT_CALL(*connect_or_op_timer_, disableTimer());
    EXPECT_CALL(host_->outlier_detector_,
                putResult(Upstream::Outlier::Result::ExtOriginRequestSuccess, _));
    callbacks_->onRespValue(std::move(response2));
  }));
  upstream_read_filter_->onData(fake_data, false);

  EXPECT_CALL(*upstream_connection_, close(Network::ConnectionCloseType::NoFlush));
  EXPECT_CALL(*connect_or_op_timer_, disableTimer());
  client_->close();
}

TEST_F(RedisClientImplTest, Basic) {
  InSequence s;

  setup();

  client_->initialize(auth_username_, auth_password_);

  Common::Redis::RespValue request1;
  MockClientCallbacks callbacks1;
  EXPECT_CALL(*encoder_, encode(Ref(request1), _));
  EXPECT_CALL(*flush_timer_, enabled()).WillOnce(Return(false));
  PoolRequest* handle1 = client_->makeRequest(request1, callbacks1);
  EXPECT_NE(nullptr, handle1);

  onConnected();

  Common::Redis::RespValue request2;
  MockClientCallbacks callbacks2;
  EXPECT_CALL(*encoder_, encode(Ref(request2), _));
  EXPECT_CALL(*flush_timer_, enabled()).WillOnce(Return(false));
  PoolRequest* handle2 = client_->makeRequest(request2, callbacks2);
  EXPECT_NE(nullptr, handle2);

  EXPECT_EQ(2UL, host_->cluster_.traffic_stats_->upstream_rq_total_.value());
  EXPECT_EQ(2UL, host_->cluster_.traffic_stats_->upstream_rq_active_.value());
  EXPECT_EQ(2UL, host_->stats_.rq_total_.value());
  EXPECT_EQ(2UL, host_->stats_.rq_active_.value());

  Buffer::OwnedImpl fake_data;
  EXPECT_CALL(*decoder_, decode(Ref(fake_data))).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    InSequence s;
    Common::Redis::RespValuePtr response1(new Common::Redis::RespValue());
    EXPECT_CALL(callbacks1, onResponse_(Ref(response1)));
    EXPECT_CALL(*connect_or_op_timer_, enableTimer(_, _));
    EXPECT_CALL(host_->outlier_detector_,
                putResult(Upstream::Outlier::Result::ExtOriginRequestSuccess, _));
    callbacks_->onRespValue(std::move(response1));

    Common::Redis::RespValuePtr response2(new Common::Redis::RespValue());
    EXPECT_CALL(callbacks2, onResponse_(Ref(response2)));
    EXPECT_CALL(*connect_or_op_timer_, disableTimer());
    EXPECT_CALL(host_->outlier_detector_,
                putResult(Upstream::Outlier::Result::ExtOriginRequestSuccess, _));
    callbacks_->onRespValue(std::move(response2));
  }));
  upstream_read_filter_->onData(fake_data, false);

  EXPECT_CALL(*upstream_connection_, close(Network::ConnectionCloseType::NoFlush));
  EXPECT_CALL(*connect_or_op_timer_, disableTimer());
  client_->close();
}

class ConfigEnableCommandStats : public Config {
  bool disableOutlierEvents() const override { return false; }
  std::chrono::milliseconds opTimeout() const override { return std::chrono::milliseconds(25); }
  bool enableHashtagging() const override { return false; }
  bool enableRedirection() const override { return false; }
  unsigned int maxBufferSizeBeforeFlush() const override { return 0; }
  std::chrono::milliseconds bufferFlushTimeoutInMs() const override {
    return std::chrono::milliseconds(0);
  }
  ReadPolicy readPolicy() const override { return ReadPolicy::Primary; }
  uint32_t maxUpstreamUnknownConnections() const override { return 0; }
  bool enableCommandStats() const override { return true; }
  bool connectionRateLimitEnabled() const override { return false; }
  uint32_t connectionRateLimitPerSec() const override { return 0; }
};

void initializeRedisSimpleCommand(Common::Redis::RespValue* request, std::string command_name,
                                  std::string key) {
  std::vector<Common::Redis::RespValue> command(2);
  command[0].type(Common::Redis::RespType::BulkString);
  command[0].asString() = command_name;
  command[1].type(Common::Redis::RespType::BulkString);
  command[1].asString() = key;

  request->type(Common::Redis::RespType::Array);
  request->asArray().swap(command);
}

TEST_F(RedisClientImplTest, CommandStatsDisabledSingleRequest) {
  // Single successful GET request. The upstream command timer works even with stats disabled;
  // however the per command timers and counts will not be recorded.
  InSequence s;

  setup();

  client_->initialize(auth_username_, auth_password_);

  std::string get_command = "get";

  Common::Redis::RespValue request1;
  initializeRedisSimpleCommand(&request1, get_command, "foo");
  MockClientCallbacks callbacks1;
  EXPECT_CALL(*encoder_, encode(Ref(request1), _));
  EXPECT_CALL(*flush_timer_, enabled()).WillOnce(Return(false));
  PoolRequest* handle1 = client_->makeRequest(request1, callbacks1);
  EXPECT_NE(nullptr, handle1);

  onConnected();

  // Regular Envoy stats function as normal
  EXPECT_EQ(1UL, host_->cluster_.traffic_stats_->upstream_rq_total_.value());
  EXPECT_EQ(1UL, host_->cluster_.traffic_stats_->upstream_rq_active_.value());
  EXPECT_EQ(1UL, host_->stats_.rq_total_.value());
  EXPECT_EQ(1UL, host_->stats_.rq_active_.value());

  Buffer::OwnedImpl fake_data;
  EXPECT_CALL(*decoder_, decode(Ref(fake_data))).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    InSequence s;

    simTime().setMonotonicTime(std::chrono::microseconds(10));

    EXPECT_CALL(stats_,
                deliverHistogramToSinks(
                    Property(&Stats::Metric::name, "upstream_commands.upstream_rq_time"), 10));

    Common::Redis::RespValuePtr response1(new Common::Redis::RespValue());
    EXPECT_CALL(callbacks1, onResponse_(Ref(response1)));
    EXPECT_CALL(*connect_or_op_timer_, disableTimer());
    EXPECT_CALL(host_->outlier_detector_,
                putResult(Upstream::Outlier::Result::ExtOriginRequestSuccess, _));

    callbacks_->onRespValue(std::move(response1));
  }));

  upstream_read_filter_->onData(fake_data, false);
  EXPECT_CALL(*upstream_connection_, close(Network::ConnectionCloseType::NoFlush));
  EXPECT_CALL(*connect_or_op_timer_, disableTimer());
  client_->close();

  // The redis command stats should not show any requests
  EXPECT_EQ(0UL, stats_.counter("upstream_commands.get.success").value());
  EXPECT_EQ(0UL, stats_.counter("upstream_commands.get.failure").value());
  EXPECT_EQ(0UL, stats_.counter("upstream_commands.get.total").value());
}

TEST_F(RedisClientImplTest, CommandStatsEnabledTwoRequests) {
  // Make two GET requests (one success, one failure) and verify command stats are recorded
  InSequence s;

  setup(std::make_shared<ConfigEnableCommandStats>());

  client_->initialize(auth_username_, auth_password_);

  std::string get_command = "get";

  Common::Redis::RespValue request1;
  initializeRedisSimpleCommand(&request1, get_command, "foo");
  MockClientCallbacks callbacks1;
  EXPECT_CALL(*encoder_, encode(Ref(request1), _));
  EXPECT_CALL(*flush_timer_, enabled()).WillOnce(Return(false));
  PoolRequest* handle1 = client_->makeRequest(request1, callbacks1);
  EXPECT_NE(nullptr, handle1);

  onConnected();

  Common::Redis::RespValue request2;
  initializeRedisSimpleCommand(&request2, get_command, "bar");
  MockClientCallbacks callbacks2;
  EXPECT_CALL(*encoder_, encode(Ref(request2), _));
  EXPECT_CALL(*flush_timer_, enabled()).WillOnce(Return(false));
  PoolRequest* handle2 = client_->makeRequest(request2, callbacks2);
  EXPECT_NE(nullptr, handle2);

  // Regular Envoy stats function as normal
  EXPECT_EQ(2UL, host_->cluster_.traffic_stats_->upstream_rq_total_.value());
  EXPECT_EQ(2UL, host_->cluster_.traffic_stats_->upstream_rq_active_.value());
  EXPECT_EQ(2UL, host_->stats_.rq_total_.value());
  EXPECT_EQ(2UL, host_->stats_.rq_active_.value());

  Buffer::OwnedImpl fake_data;
  EXPECT_CALL(*decoder_, decode(Ref(fake_data))).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    InSequence s;

    simTime().setMonotonicTime(std::chrono::microseconds(10));

    EXPECT_CALL(stats_, deliverHistogramToSinks(
                            Property(&Stats::Metric::name, "upstream_commands.get.latency"), 10));
    EXPECT_CALL(stats_,
                deliverHistogramToSinks(
                    Property(&Stats::Metric::name, "upstream_commands.upstream_rq_time"), 10));

    // First request is successful
    Common::Redis::RespValuePtr response1(new Common::Redis::RespValue());
    EXPECT_CALL(callbacks1, onResponse_(Ref(response1)));
    EXPECT_CALL(*connect_or_op_timer_, enableTimer(_, _));
    EXPECT_CALL(host_->outlier_detector_,
                putResult(Upstream::Outlier::Result::ExtOriginRequestSuccess, _));
    callbacks_->onRespValue(std::move(response1));

    EXPECT_CALL(stats_, deliverHistogramToSinks(
                            Property(&Stats::Metric::name, "upstream_commands.get.latency"), 10));
    EXPECT_CALL(stats_,
                deliverHistogramToSinks(
                    Property(&Stats::Metric::name, "upstream_commands.upstream_rq_time"), 10));

    // Second request errors out
    Common::Redis::RespValuePtr response2(new Common::Redis::RespValue());
    response2->type(Common::Redis::RespType::Error);
    EXPECT_CALL(callbacks2, onResponse_(Ref(response2)));
    EXPECT_CALL(*connect_or_op_timer_, disableTimer());
    EXPECT_CALL(host_->outlier_detector_,
                putResult(Upstream::Outlier::Result::ExtOriginRequestSuccess, _));
    callbacks_->onRespValue(std::move(response2));

    // Redis command stats reflect one successful and one failed request
    EXPECT_EQ(1UL, stats_.counter("upstream_commands.get.success").value());
    EXPECT_EQ(1UL, stats_.counter("upstream_commands.get.failure").value());
    EXPECT_EQ(2UL, stats_.counter("upstream_commands.get.total").value());
  }));

  upstream_read_filter_->onData(fake_data, false);

  EXPECT_CALL(*upstream_connection_, close(Network::ConnectionCloseType::NoFlush));
  EXPECT_CALL(*connect_or_op_timer_, disableTimer());
  client_->close();
}

TEST_F(RedisClientImplTest, InitializedWithAuthPassword) {
  InSequence s;

  setup();

  auth_password_ = "testing password";
  Utility::AuthRequest auth_request(auth_password_);
  EXPECT_CALL(*encoder_, encode(Eq(auth_request), _));
  EXPECT_CALL(*flush_timer_, enabled()).WillOnce(Return(false));
  client_->initialize(auth_username_, auth_password_);

  EXPECT_EQ(1UL, host_->cluster_.traffic_stats_->upstream_rq_total_.value());
  EXPECT_EQ(1UL, host_->cluster_.traffic_stats_->upstream_rq_active_.value());
  EXPECT_EQ(1UL, host_->stats_.rq_total_.value());
  EXPECT_EQ(1UL, host_->stats_.rq_active_.value());

  EXPECT_CALL(*upstream_connection_, close(Network::ConnectionCloseType::NoFlush));
  EXPECT_CALL(*connect_or_op_timer_, disableTimer());
  client_->close();
}

TEST_F(RedisClientImplTest, InitializedWithAuthAcl) {
  InSequence s;

  setup();

  auth_username_ = "testing username";
  auth_password_ = "testing password";
  Utility::AuthRequest auth_request(auth_username_, auth_password_);
  EXPECT_CALL(*encoder_, encode(Eq(auth_request), _));
  EXPECT_CALL(*flush_timer_, enabled()).WillOnce(Return(false));
  client_->initialize(auth_username_, auth_password_);

  EXPECT_EQ(1UL, host_->cluster_.traffic_stats_->upstream_rq_total_.value());
  EXPECT_EQ(1UL, host_->cluster_.traffic_stats_->upstream_rq_active_.value());
  EXPECT_EQ(1UL, host_->stats_.rq_total_.value());
  EXPECT_EQ(1UL, host_->stats_.rq_active_.value());

  EXPECT_CALL(*upstream_connection_, close(Network::ConnectionCloseType::NoFlush));
  EXPECT_CALL(*connect_or_op_timer_, disableTimer());
  client_->close();
}

TEST_F(RedisClientImplTest, InitializedWithPreferPrimaryReadPolicy) {
  testInitializeReadPolicy(envoy::extensions::filters::network::redis_proxy::v3::RedisProxy::
                               ConnPoolSettings::PREFER_MASTER);
}

TEST_F(RedisClientImplTest, InitializedWithReplicaReadPolicy) {
  testInitializeReadPolicy(
      envoy::extensions::filters::network::redis_proxy::v3::RedisProxy::ConnPoolSettings::REPLICA);
}

TEST_F(RedisClientImplTest, InitializedWithPreferReplicaReadPolicy) {
  testInitializeReadPolicy(envoy::extensions::filters::network::redis_proxy::v3::RedisProxy::
                               ConnPoolSettings::PREFER_REPLICA);
}

TEST_F(RedisClientImplTest, InitializedWithAnyReadPolicy) {
  testInitializeReadPolicy(
      envoy::extensions::filters::network::redis_proxy::v3::RedisProxy::ConnPoolSettings::ANY);
}

TEST_F(RedisClientImplTest, InitializedWithLocalZoneAffinityReadPolicy) {
  testInitializeReadPolicy(envoy::extensions::filters::network::redis_proxy::v3::RedisProxy::
                               ConnPoolSettings::LOCAL_ZONE_AFFINITY);
}

TEST_F(RedisClientImplTest, InitializedWithLocalZoneAffinityReplicasAndPrimaryReadPolicy) {
  testInitializeReadPolicy(envoy::extensions::filters::network::redis_proxy::v3::RedisProxy::
                               ConnPoolSettings::LOCAL_ZONE_AFFINITY_REPLICAS_AND_PRIMARY);
}

TEST_F(RedisClientImplTest, Cancel) {
  InSequence s;

  setup();

  Common::Redis::RespValue request1;
  MockClientCallbacks callbacks1;
  EXPECT_CALL(*encoder_, encode(Ref(request1), _));
  EXPECT_CALL(*flush_timer_, enabled()).WillOnce(Return(false));
  PoolRequest* handle1 = client_->makeRequest(request1, callbacks1);
  EXPECT_NE(nullptr, handle1);

  onConnected();

  Common::Redis::RespValue request2;
  MockClientCallbacks callbacks2;
  EXPECT_CALL(*encoder_, encode(Ref(request2), _));
  EXPECT_CALL(*flush_timer_, enabled()).WillOnce(Return(false));
  PoolRequest* handle2 = client_->makeRequest(request2, callbacks2);
  EXPECT_NE(nullptr, handle2);

  handle1->cancel();

  Buffer::OwnedImpl fake_data;
  EXPECT_CALL(*decoder_, decode(Ref(fake_data))).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    InSequence s;

    Common::Redis::RespValuePtr response1(new Common::Redis::RespValue());
    EXPECT_CALL(callbacks1, onResponse_(_)).Times(0);
    EXPECT_CALL(*connect_or_op_timer_, enableTimer(_, _));
    EXPECT_CALL(host_->outlier_detector_,
                putResult(Upstream::Outlier::Result::ExtOriginRequestSuccess, _));
    callbacks_->onRespValue(std::move(response1));

    Common::Redis::RespValuePtr response2(new Common::Redis::RespValue());
    EXPECT_CALL(callbacks2, onResponse_(Ref(response2)));
    EXPECT_CALL(*connect_or_op_timer_, disableTimer());
    EXPECT_CALL(host_->outlier_detector_,
                putResult(Upstream::Outlier::Result::ExtOriginRequestSuccess, _));
    callbacks_->onRespValue(std::move(response2));
  }));
  upstream_read_filter_->onData(fake_data, false);

  EXPECT_CALL(*upstream_connection_, close(Network::ConnectionCloseType::NoFlush));
  EXPECT_CALL(*connect_or_op_timer_, disableTimer());
  client_->close();

  EXPECT_EQ(1UL, host_->cluster_.traffic_stats_->upstream_rq_cancelled_.value());
}

TEST_F(RedisClientImplTest, FailAll) {
  InSequence s;

  setup();

  NiceMock<Network::MockConnectionCallbacks> connection_callbacks;
  client_->addConnectionCallbacks(connection_callbacks);

  Common::Redis::RespValue request1;
  MockClientCallbacks callbacks1;
  EXPECT_CALL(*encoder_, encode(Ref(request1), _));
  EXPECT_CALL(*flush_timer_, enabled()).WillOnce(Return(false));
  PoolRequest* handle1 = client_->makeRequest(request1, callbacks1);
  EXPECT_NE(nullptr, handle1);

  onConnected();

  EXPECT_CALL(host_->outlier_detector_,
              putResult(Upstream::Outlier::Result::LocalOriginConnectFailed, _));
  EXPECT_CALL(callbacks1, onFailure());
  EXPECT_CALL(*connect_or_op_timer_, disableTimer());
  EXPECT_CALL(connection_callbacks, onEvent(Network::ConnectionEvent::RemoteClose));
  upstream_connection_->raiseEvent(Network::ConnectionEvent::RemoteClose);

  EXPECT_EQ(1UL, host_->cluster_.traffic_stats_->upstream_cx_destroy_with_active_rq_.value());
  EXPECT_EQ(1UL,
            host_->cluster_.traffic_stats_->upstream_cx_destroy_remote_with_active_rq_.value());
}

TEST_F(RedisClientImplTest, FailAllWithCancel) {
  InSequence s;

  setup();

  NiceMock<Network::MockConnectionCallbacks> connection_callbacks;
  client_->addConnectionCallbacks(connection_callbacks);

  Common::Redis::RespValue request1;
  MockClientCallbacks callbacks1;
  EXPECT_CALL(*encoder_, encode(Ref(request1), _));
  EXPECT_CALL(*flush_timer_, enabled()).WillOnce(Return(false));
  PoolRequest* handle1 = client_->makeRequest(request1, callbacks1);
  EXPECT_NE(nullptr, handle1);

  onConnected();
  handle1->cancel();

  EXPECT_CALL(callbacks1, onFailure()).Times(0);
  EXPECT_CALL(*connect_or_op_timer_, disableTimer());
  EXPECT_CALL(connection_callbacks, onEvent(Network::ConnectionEvent::LocalClose));
  upstream_connection_->raiseEvent(Network::ConnectionEvent::LocalClose);

  EXPECT_EQ(1UL, host_->cluster_.traffic_stats_->upstream_cx_destroy_with_active_rq_.value());
  EXPECT_EQ(1UL, host_->cluster_.traffic_stats_->upstream_cx_destroy_local_with_active_rq_.value());
  EXPECT_EQ(1UL, host_->cluster_.traffic_stats_->upstream_rq_cancelled_.value());
}

TEST_F(RedisClientImplTest, ProtocolError) {
  InSequence s;

  setup();

  Common::Redis::RespValue request1;
  MockClientCallbacks callbacks1;
  EXPECT_CALL(*encoder_, encode(Ref(request1), _));
  EXPECT_CALL(*flush_timer_, enabled()).WillOnce(Return(false));
  PoolRequest* handle1 = client_->makeRequest(request1, callbacks1);
  EXPECT_NE(nullptr, handle1);

  onConnected();

  Buffer::OwnedImpl fake_data;
  EXPECT_CALL(*decoder_, decode(Ref(fake_data))).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    throw Common::Redis::ProtocolError("error");
  }));
  EXPECT_CALL(host_->outlier_detector_,
              putResult(Upstream::Outlier::Result::ExtOriginRequestFailed, _));
  EXPECT_CALL(*upstream_connection_, close(Network::ConnectionCloseType::NoFlush));
  EXPECT_CALL(callbacks1, onFailure());
  EXPECT_CALL(*connect_or_op_timer_, disableTimer());
  upstream_read_filter_->onData(fake_data, false);

  EXPECT_EQ(1UL, host_->cluster_.traffic_stats_->upstream_cx_protocol_error_.value());
  EXPECT_EQ(1UL, host_->stats_.rq_error_.value());
}

TEST_F(RedisClientImplTest, ConnectFail) {
  InSequence s;

  setup();

  Common::Redis::RespValue request1;
  MockClientCallbacks callbacks1;
  EXPECT_CALL(*encoder_, encode(Ref(request1), _));
  EXPECT_CALL(*flush_timer_, enabled()).WillOnce(Return(false));
  PoolRequest* handle1 = client_->makeRequest(request1, callbacks1);
  EXPECT_NE(nullptr, handle1);

  EXPECT_CALL(host_->outlier_detector_,
              putResult(Upstream::Outlier::Result::LocalOriginConnectFailed, _));
  EXPECT_CALL(callbacks1, onFailure());
  EXPECT_CALL(*connect_or_op_timer_, disableTimer());
  upstream_connection_->raiseEvent(Network::ConnectionEvent::RemoteClose);

  EXPECT_EQ(1UL, host_->cluster_.traffic_stats_->upstream_cx_connect_fail_.value());
  EXPECT_EQ(1UL, host_->stats_.cx_connect_fail_.value());
}

class ConfigOutlierDisabled : public Config {
  bool disableOutlierEvents() const override { return true; }
  std::chrono::milliseconds opTimeout() const override { return std::chrono::milliseconds(25); }
  bool enableHashtagging() const override { return false; }
  bool enableRedirection() const override { return false; }
  unsigned int maxBufferSizeBeforeFlush() const override { return 0; }
  std::chrono::milliseconds bufferFlushTimeoutInMs() const override {
    return std::chrono::milliseconds(0);
  }
  ReadPolicy readPolicy() const override { return ReadPolicy::Primary; }
  uint32_t maxUpstreamUnknownConnections() const override { return 0; }
  bool enableCommandStats() const override { return false; }
  bool connectionRateLimitEnabled() const override { return false; }
  uint32_t connectionRateLimitPerSec() const override { return 0; }
};

TEST_F(RedisClientImplTest, OutlierDisabled) {
  InSequence s;

  setup(std::make_shared<ConfigOutlierDisabled>());

  Common::Redis::RespValue request1;
  MockClientCallbacks callbacks1;
  EXPECT_CALL(*encoder_, encode(Ref(request1), _));
  EXPECT_CALL(*flush_timer_, enabled()).WillOnce(Return(false));
  PoolRequest* handle1 = client_->makeRequest(request1, callbacks1);
  EXPECT_NE(nullptr, handle1);

  EXPECT_CALL(host_->outlier_detector_, putResult(_, _)).Times(0);
  EXPECT_CALL(callbacks1, onFailure());
  EXPECT_CALL(*connect_or_op_timer_, disableTimer());
  upstream_connection_->raiseEvent(Network::ConnectionEvent::RemoteClose);

  EXPECT_EQ(1UL, host_->cluster_.traffic_stats_->upstream_cx_connect_fail_.value());
  EXPECT_EQ(1UL, host_->stats_.cx_connect_fail_.value());
}

TEST_F(RedisClientImplTest, ConnectTimeout) {
  InSequence s;

  setup();

  Common::Redis::RespValue request1;
  MockClientCallbacks callbacks1;
  EXPECT_CALL(*encoder_, encode(Ref(request1), _));
  EXPECT_CALL(*flush_timer_, enabled()).WillOnce(Return(false));
  PoolRequest* handle1 = client_->makeRequest(request1, callbacks1);
  EXPECT_NE(nullptr, handle1);

  EXPECT_CALL(host_->outlier_detector_,
              putResult(Upstream::Outlier::Result::LocalOriginTimeout, _));
  EXPECT_CALL(*upstream_connection_, close(Network::ConnectionCloseType::NoFlush));
  EXPECT_CALL(callbacks1, onFailure());
  EXPECT_CALL(*connect_or_op_timer_, disableTimer());
  connect_or_op_timer_->invokeCallback();

  EXPECT_EQ(1UL, host_->cluster_.traffic_stats_->upstream_cx_connect_timeout_.value());
  EXPECT_EQ(1UL, host_->stats_.cx_connect_fail_.value());
}

TEST_F(RedisClientImplTest, OpTimeout) {
  InSequence s;

  setup();

  Common::Redis::RespValue request1;
  MockClientCallbacks callbacks1;
  EXPECT_CALL(*encoder_, encode(Ref(request1), _));
  EXPECT_CALL(*flush_timer_, enabled()).WillOnce(Return(false));
  PoolRequest* handle1 = client_->makeRequest(request1, callbacks1);
  EXPECT_NE(nullptr, handle1);

  onConnected();

  EXPECT_EQ(1UL, host_->cluster_.traffic_stats_->upstream_rq_total_.value());
  EXPECT_EQ(1UL, host_->cluster_.traffic_stats_->upstream_rq_active_.value());

  EXPECT_CALL(callbacks1, onResponse_(_));
  EXPECT_CALL(*connect_or_op_timer_, disableTimer());
  EXPECT_CALL(host_->outlier_detector_,
              putResult(Upstream::Outlier::Result::ExtOriginRequestSuccess, _));
  respond();

  EXPECT_EQ(1UL, host_->cluster_.traffic_stats_->upstream_rq_total_.value());
  EXPECT_EQ(0UL, host_->cluster_.traffic_stats_->upstream_rq_active_.value());

  EXPECT_CALL(*encoder_, encode(Ref(request1), _));
  EXPECT_CALL(*flush_timer_, enabled()).WillOnce(Return(false));
  EXPECT_CALL(*connect_or_op_timer_, enableTimer(_, _));
  handle1 = client_->makeRequest(request1, callbacks1);
  EXPECT_NE(nullptr, handle1);

  EXPECT_CALL(host_->outlier_detector_,
              putResult(Upstream::Outlier::Result::LocalOriginTimeout, _));
  EXPECT_CALL(*upstream_connection_, close(Network::ConnectionCloseType::NoFlush));
  EXPECT_CALL(callbacks1, onFailure());
  EXPECT_CALL(*connect_or_op_timer_, disableTimer());
  connect_or_op_timer_->invokeCallback();

  EXPECT_EQ(1UL, host_->cluster_.traffic_stats_->upstream_rq_timeout_.value());
  EXPECT_EQ(1UL, host_->stats_.rq_timeout_.value());
  EXPECT_EQ(2UL, host_->cluster_.traffic_stats_->upstream_rq_total_.value());
  EXPECT_EQ(0UL, host_->cluster_.traffic_stats_->upstream_rq_active_.value());
}

TEST_F(RedisClientImplTest, AskRedirection) {
  InSequence s;

  setup();

  Common::Redis::RespValue request1;
  MockClientCallbacks callbacks1;
  EXPECT_CALL(*encoder_, encode(Ref(request1), _));
  EXPECT_CALL(*flush_timer_, enabled()).WillOnce(Return(false));
  PoolRequest* handle1 = client_->makeRequest(request1, callbacks1);
  EXPECT_NE(nullptr, handle1);

  onConnected();

  Common::Redis::RespValue request2;
  MockClientCallbacks callbacks2;
  EXPECT_CALL(*encoder_, encode(Ref(request2), _));
  EXPECT_CALL(*flush_timer_, enabled()).WillOnce(Return(false));
  PoolRequest* handle2 = client_->makeRequest(request2, callbacks2);
  EXPECT_NE(nullptr, handle2);

  EXPECT_EQ(2UL, host_->cluster_.traffic_stats_->upstream_rq_total_.value());
  EXPECT_EQ(2UL, host_->cluster_.traffic_stats_->upstream_rq_active_.value());
  EXPECT_EQ(2UL, host_->stats_.rq_total_.value());
  EXPECT_EQ(2UL, host_->stats_.rq_active_.value());

  Buffer::OwnedImpl fake_data;
  EXPECT_CALL(*decoder_, decode(Ref(fake_data))).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    InSequence s;
    Common::Redis::RespValuePtr response1(new Common::Redis::RespValue());
    response1->type(Common::Redis::RespType::Error);
    // The exact values of the hash slot and IP info are not important.
    response1->asString() = "ASK 1111 10.1.2.3:4321";
    // Simulate redirection failure.
    EXPECT_CALL(callbacks1, onRedirection_(Ref(response1), "10.1.2.3:4321", true));
    EXPECT_CALL(*connect_or_op_timer_, enableTimer(_, _));
    EXPECT_CALL(host_->outlier_detector_,
                putResult(Upstream::Outlier::Result::ExtOriginRequestSuccess, _));
    callbacks_->onRespValue(std::move(response1));

    Common::Redis::RespValuePtr response2(new Common::Redis::RespValue());
    response2->type(Common::Redis::RespType::Error);
    // The exact values of the hash slot and IP info are not important.
    response2->asString() = "ASK 2222 10.1.2.4:4321";
    EXPECT_CALL(callbacks2, onRedirection_(Ref(response2), "10.1.2.4:4321", true));
    EXPECT_CALL(*connect_or_op_timer_, disableTimer());
    EXPECT_CALL(host_->outlier_detector_,
                putResult(Upstream::Outlier::Result::ExtOriginRequestSuccess, _));
    callbacks_->onRespValue(std::move(response2));
  }));
  upstream_read_filter_->onData(fake_data, false);

  EXPECT_CALL(*upstream_connection_, close(Network::ConnectionCloseType::NoFlush));
  EXPECT_CALL(*connect_or_op_timer_, disableTimer());
  client_->close();
}

TEST_F(RedisClientImplTest, MovedRedirection) {
  InSequence s;

  setup();

  Common::Redis::RespValue request1;
  MockClientCallbacks callbacks1;
  EXPECT_CALL(*encoder_, encode(Ref(request1), _));
  EXPECT_CALL(*flush_timer_, enabled()).WillOnce(Return(false));
  PoolRequest* handle1 = client_->makeRequest(request1, callbacks1);
  EXPECT_NE(nullptr, handle1);

  onConnected();

  Common::Redis::RespValue request2;
  MockClientCallbacks callbacks2;
  EXPECT_CALL(*encoder_, encode(Ref(request2), _));
  EXPECT_CALL(*flush_timer_, enabled()).WillOnce(Return(false));
  PoolRequest* handle2 = client_->makeRequest(request2, callbacks2);
  EXPECT_NE(nullptr, handle2);

  EXPECT_EQ(2UL, host_->cluster_.traffic_stats_->upstream_rq_total_.value());
  EXPECT_EQ(2UL, host_->cluster_.traffic_stats_->upstream_rq_active_.value());
  EXPECT_EQ(2UL, host_->stats_.rq_total_.value());
  EXPECT_EQ(2UL, host_->stats_.rq_active_.value());

  Buffer::OwnedImpl fake_data;
  EXPECT_CALL(*decoder_, decode(Ref(fake_data))).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    InSequence s;
    Common::Redis::RespValuePtr response1(new Common::Redis::RespValue());
    response1->type(Common::Redis::RespType::Error);
    // The exact values of the hash slot and IP info are not important.
    response1->asString() = "MOVED 1111 10.1.2.3:4321";
    // Simulate redirection failure.
    EXPECT_CALL(callbacks1, onRedirection_(Ref(response1), "10.1.2.3:4321", false));
    EXPECT_CALL(*connect_or_op_timer_, enableTimer(_, _));
    EXPECT_CALL(host_->outlier_detector_,
                putResult(Upstream::Outlier::Result::ExtOriginRequestSuccess, _));
    callbacks_->onRespValue(std::move(response1));

    Common::Redis::RespValuePtr response2(new Common::Redis::RespValue());
    response2->type(Common::Redis::RespType::Error);
    // The exact values of the hash slot and IP info are not important.
    response2->asString() = "MOVED 2222 10.1.2.4:4321";
    EXPECT_CALL(callbacks2, onRedirection_(Ref(response2), "10.1.2.4:4321", false));
    EXPECT_CALL(*connect_or_op_timer_, disableTimer());
    EXPECT_CALL(host_->outlier_detector_,
                putResult(Upstream::Outlier::Result::ExtOriginRequestSuccess, _));
    callbacks_->onRespValue(std::move(response2));
  }));
  upstream_read_filter_->onData(fake_data, false);

  EXPECT_CALL(*upstream_connection_, close(Network::ConnectionCloseType::NoFlush));
  EXPECT_CALL(*connect_or_op_timer_, disableTimer());
  client_->close();
}

TEST_F(RedisClientImplTest, RedirectionFailure) {
  InSequence s;

  setup();

  Common::Redis::RespValue request1;
  MockClientCallbacks callbacks1;
  EXPECT_CALL(*encoder_, encode(Ref(request1), _));
  EXPECT_CALL(*flush_timer_, enabled()).WillOnce(Return(false));
  PoolRequest* handle1 = client_->makeRequest(request1, callbacks1);
  EXPECT_NE(nullptr, handle1);

  onConnected();

  Common::Redis::RespValue request2;
  MockClientCallbacks callbacks2;
  EXPECT_CALL(*encoder_, encode(Ref(request2), _));
  EXPECT_CALL(*flush_timer_, enabled()).WillOnce(Return(false));
  PoolRequest* handle2 = client_->makeRequest(request2, callbacks2);
  EXPECT_NE(nullptr, handle2);

  EXPECT_EQ(2UL, host_->cluster_.traffic_stats_->upstream_rq_total_.value());
  EXPECT_EQ(2UL, host_->cluster_.traffic_stats_->upstream_rq_active_.value());
  EXPECT_EQ(2UL, host_->stats_.rq_total_.value());
  EXPECT_EQ(2UL, host_->stats_.rq_active_.value());

  Buffer::OwnedImpl fake_data;
  EXPECT_CALL(*decoder_, decode(Ref(fake_data))).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    InSequence s;

    // Test an error that looks like it might be a MOVED or ASK redirection error except for the
    // first non-whitespace substring.
    Common::Redis::RespValuePtr response1{new Common::Redis::RespValue()};
    response1->type(Common::Redis::RespType::Error);
    response1->asString() = "NOTMOVEDORASK 1111 1.1.1.1:1";

    EXPECT_CALL(callbacks1, onResponse_(Ref(response1)));
    EXPECT_CALL(*connect_or_op_timer_, enableTimer(_, _));
    EXPECT_CALL(host_->outlier_detector_,
                putResult(Upstream::Outlier::Result::ExtOriginRequestSuccess, _));
    callbacks_->onRespValue(std::move(response1));

    EXPECT_EQ(0UL,
              host_->cluster_.traffic_stats_->upstream_internal_redirect_succeeded_total_.value());
    EXPECT_EQ(0UL,
              host_->cluster_.traffic_stats_->upstream_internal_redirect_failed_total_.value());

    // Test a truncated MOVED error response that cannot be parsed properly.
    Common::Redis::RespValuePtr response2(new Common::Redis::RespValue());
    response2->type(Common::Redis::RespType::Error);
    response2->asString() = "MOVED 1111";
    EXPECT_CALL(callbacks2, onResponse_(Ref(response2)));
    EXPECT_CALL(*connect_or_op_timer_, disableTimer());
    EXPECT_CALL(host_->outlier_detector_,
                putResult(Upstream::Outlier::Result::ExtOriginRequestSuccess, _));
    callbacks_->onRespValue(std::move(response2));

    EXPECT_EQ(0UL,
              host_->cluster_.traffic_stats_->upstream_internal_redirect_succeeded_total_.value());
    EXPECT_EQ(0UL,
              host_->cluster_.traffic_stats_->upstream_internal_redirect_failed_total_.value());
  }));
  upstream_read_filter_->onData(fake_data, false);

  EXPECT_CALL(*upstream_connection_, close(Network::ConnectionCloseType::NoFlush));
  EXPECT_CALL(*connect_or_op_timer_, disableTimer());
  client_->close();
}

// RESP3 Push (`>N`) is server-initiated, not a reply to any pending request. The proxy has no
// out-of-band routing for it on this code path. Drop unexpected Push frames silently: NEVER pop
// pending_requests_ (which would mismatch every subsequent reply) and NEVER close the connection
// (Push is in-spec RESP3 framing, and future Push-producing features may use it; closing here
// would unnecessarily kill the connection before any owner can route the frame). Verify that:
//   1. An unexpected Push does NOT invoke the pending request's callback.
//   2. No close occurs.
//   3. The protocol_error counter does NOT increment.
//   4. A subsequent ordinary reply still matches the original pending request — proving the
//      pending_requests_ FIFO was not misaligned by the Push.
TEST_F(RedisClientImplTest, UpstreamPushIsDroppedAndPipelineSurvives) {
  InSequence s;
  setup();

  Common::Redis::RespValue request1;
  MockClientCallbacks callbacks1;
  EXPECT_CALL(*encoder_, encode(Ref(request1), _));
  EXPECT_CALL(*flush_timer_, enabled()).WillOnce(Return(false));
  PoolRequest* handle1 = client_->makeRequest(request1, callbacks1);
  EXPECT_NE(nullptr, handle1);

  onConnected();

  // Drive a Push frame. callbacks1 must NOT fire; no close; no protocol_error increment.
  Buffer::OwnedImpl push_data;
  EXPECT_CALL(*decoder_, decode(Ref(push_data))).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    auto push = std::make_unique<Common::Redis::RespValue>();
    push->type(Common::Redis::RespType::Push);
    callbacks_->onRespValue(std::move(push));
  }));
  upstream_read_filter_->onData(push_data, false);
  EXPECT_EQ(0UL, host_->cluster_.traffic_stats_->upstream_cx_protocol_error_.value());

  // Now drive the real reply for the still-pending request1. It must route to callbacks1 —
  // the Push above did not corrupt pending_requests_ framing.
  Buffer::OwnedImpl reply_data;
  EXPECT_CALL(*decoder_, decode(Ref(reply_data))).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    Common::Redis::RespValuePtr response{new Common::Redis::RespValue()};
    response->type(Common::Redis::RespType::SimpleString);
    response->asString() = "OK";
    EXPECT_CALL(callbacks1, onResponse_(Ref(response)));
    EXPECT_CALL(*connect_or_op_timer_, disableTimer());
    EXPECT_CALL(host_->outlier_detector_,
                putResult(Upstream::Outlier::Result::ExtOriginRequestSuccess, _));
    callbacks_->onRespValue(std::move(response));
  }));
  upstream_read_filter_->onData(reply_data, false);

  EXPECT_CALL(*upstream_connection_, close(Network::ConnectionCloseType::NoFlush));
  EXPECT_CALL(*connect_or_op_timer_, disableTimer());
  client_->close();
}

// Empty Error reply ("-\r\n"). splitToken("", " ", keep_empty_string=false)
// returns an empty vector — accessing err[0] in the post-MOVED/ASK
// fall-through would dereference past the end. Verify it falls through to
// onResponse cleanly without crashing.
TEST_F(RedisClientImplTest, EmptyErrorReplyDoesNotCrashRedirectionParse) {
  InSequence s;

  setup();

  Common::Redis::RespValue request1;
  MockClientCallbacks callbacks1;
  EXPECT_CALL(*encoder_, encode(Ref(request1), _));
  EXPECT_CALL(*flush_timer_, enabled()).WillOnce(Return(false));
  PoolRequest* handle1 = client_->makeRequest(request1, callbacks1);
  EXPECT_NE(nullptr, handle1);

  onConnected();

  Buffer::OwnedImpl fake_data;
  EXPECT_CALL(*decoder_, decode(Ref(fake_data))).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    Common::Redis::RespValuePtr response{new Common::Redis::RespValue()};
    response->type(Common::Redis::RespType::Error);
    response->asString() = "";
    EXPECT_CALL(callbacks1, onResponse_(Ref(response)));
    EXPECT_CALL(*connect_or_op_timer_, disableTimer());
    EXPECT_CALL(host_->outlier_detector_,
                putResult(Upstream::Outlier::Result::ExtOriginRequestSuccess, _));
    callbacks_->onRespValue(std::move(response));
  }));
  upstream_read_filter_->onData(fake_data, false);

  EXPECT_CALL(*upstream_connection_, close(Network::ConnectionCloseType::NoFlush));
  EXPECT_CALL(*connect_or_op_timer_, disableTimer());
  client_->close();
}

TEST_F(RedisClientImplTest, AskRedirectionNotEnabled) {
  InSequence s;

  setup(std::make_shared<ConfigImpl>(createConnPoolSettings(20, true, false)));

  Common::Redis::RespValue request1;
  MockClientCallbacks callbacks1;
  EXPECT_CALL(*encoder_, encode(Ref(request1), _));
  EXPECT_CALL(*flush_timer_, enabled()).WillOnce(Return(false));
  PoolRequest* handle1 = client_->makeRequest(request1, callbacks1);
  EXPECT_NE(nullptr, handle1);

  onConnected();

  Common::Redis::RespValue request2;
  MockClientCallbacks callbacks2;
  EXPECT_CALL(*encoder_, encode(Ref(request2), _));
  EXPECT_CALL(*flush_timer_, enabled()).WillOnce(Return(false));
  PoolRequest* handle2 = client_->makeRequest(request2, callbacks2);
  EXPECT_NE(nullptr, handle2);

  EXPECT_EQ(2UL, host_->cluster_.traffic_stats_->upstream_rq_total_.value());
  EXPECT_EQ(2UL, host_->cluster_.traffic_stats_->upstream_rq_active_.value());
  EXPECT_EQ(2UL, host_->stats_.rq_total_.value());
  EXPECT_EQ(2UL, host_->stats_.rq_active_.value());

  Buffer::OwnedImpl fake_data;
  EXPECT_CALL(*decoder_, decode(Ref(fake_data))).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    InSequence s;
    Common::Redis::RespValuePtr response1(new Common::Redis::RespValue());
    response1->type(Common::Redis::RespType::Error);
    // The exact values of the hash slot and IP info are not important.
    response1->asString() = "ASK 1111 10.1.2.3:4321";
    // Simulate redirection failure.
    EXPECT_CALL(callbacks1, onResponse_(Ref(response1)));
    EXPECT_CALL(*connect_or_op_timer_, enableTimer(_, _));
    EXPECT_CALL(host_->outlier_detector_,
                putResult(Upstream::Outlier::Result::ExtOriginRequestSuccess, _));
    callbacks_->onRespValue(std::move(response1));

    EXPECT_EQ(0UL,
              host_->cluster_.traffic_stats_->upstream_internal_redirect_failed_total_.value());
    EXPECT_EQ(0UL,
              host_->cluster_.traffic_stats_->upstream_internal_redirect_succeeded_total_.value());

    Common::Redis::RespValuePtr response2(new Common::Redis::RespValue());
    response2->type(Common::Redis::RespType::Error);
    // The exact values of the hash slot and IP info are not important.
    response2->asString() = "ASK 2222 10.1.2.4:4321";
    EXPECT_CALL(callbacks2, onResponse_(Ref(response2)));
    EXPECT_CALL(*connect_or_op_timer_, disableTimer());
    EXPECT_CALL(host_->outlier_detector_,
                putResult(Upstream::Outlier::Result::ExtOriginRequestSuccess, _));
    callbacks_->onRespValue(std::move(response2));

    EXPECT_EQ(0UL,
              host_->cluster_.traffic_stats_->upstream_internal_redirect_failed_total_.value());
    EXPECT_EQ(0UL,
              host_->cluster_.traffic_stats_->upstream_internal_redirect_succeeded_total_.value());
  }));
  upstream_read_filter_->onData(fake_data, false);

  EXPECT_CALL(*upstream_connection_, close(Network::ConnectionCloseType::NoFlush));
  EXPECT_CALL(*connect_or_op_timer_, disableTimer());
  client_->close();
}

TEST_F(RedisClientImplTest, MovedRedirectionNotEnabled) {
  InSequence s;

  setup(std::make_shared<ConfigImpl>(createConnPoolSettings(20, true, false)));

  Common::Redis::RespValue request1;
  MockClientCallbacks callbacks1;
  EXPECT_CALL(*encoder_, encode(Ref(request1), _));
  EXPECT_CALL(*flush_timer_, enabled()).WillOnce(Return(false));
  PoolRequest* handle1 = client_->makeRequest(request1, callbacks1);
  EXPECT_NE(nullptr, handle1);

  onConnected();

  Common::Redis::RespValue request2;
  MockClientCallbacks callbacks2;
  EXPECT_CALL(*encoder_, encode(Ref(request2), _));
  EXPECT_CALL(*flush_timer_, enabled()).WillOnce(Return(false));
  PoolRequest* handle2 = client_->makeRequest(request2, callbacks2);
  EXPECT_NE(nullptr, handle2);

  EXPECT_EQ(2UL, host_->cluster_.traffic_stats_->upstream_rq_total_.value());
  EXPECT_EQ(2UL, host_->cluster_.traffic_stats_->upstream_rq_active_.value());
  EXPECT_EQ(2UL, host_->stats_.rq_total_.value());
  EXPECT_EQ(2UL, host_->stats_.rq_active_.value());

  Buffer::OwnedImpl fake_data;
  EXPECT_CALL(*decoder_, decode(Ref(fake_data))).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    InSequence s;
    Common::Redis::RespValuePtr response1(new Common::Redis::RespValue());
    response1->type(Common::Redis::RespType::Error);
    // The exact values of the hash slot and IP info are not important.
    response1->asString() = "MOVED 1111 10.1.2.3:4321";
    EXPECT_CALL(callbacks1, onResponse_(Ref(response1)));
    EXPECT_CALL(*connect_or_op_timer_, enableTimer(_, _));
    EXPECT_CALL(host_->outlier_detector_,
                putResult(Upstream::Outlier::Result::ExtOriginRequestSuccess, _));
    callbacks_->onRespValue(std::move(response1));

    EXPECT_EQ(0UL,
              host_->cluster_.traffic_stats_->upstream_internal_redirect_succeeded_total_.value());
    EXPECT_EQ(0UL,
              host_->cluster_.traffic_stats_->upstream_internal_redirect_failed_total_.value());

    Common::Redis::RespValuePtr response2(new Common::Redis::RespValue());
    response2->type(Common::Redis::RespType::Error);
    // The exact values of the hash slot and IP info are not important.
    response2->asString() = "MOVED 2222 10.1.2.4:4321";
    EXPECT_CALL(callbacks2, onResponse_(Ref(response2)));
    EXPECT_CALL(*connect_or_op_timer_, disableTimer());
    EXPECT_CALL(host_->outlier_detector_,
                putResult(Upstream::Outlier::Result::ExtOriginRequestSuccess, _));
    callbacks_->onRespValue(std::move(response2));

    EXPECT_EQ(0UL,
              host_->cluster_.traffic_stats_->upstream_internal_redirect_succeeded_total_.value());
    EXPECT_EQ(0UL,
              host_->cluster_.traffic_stats_->upstream_internal_redirect_failed_total_.value());
  }));
  upstream_read_filter_->onData(fake_data, false);

  EXPECT_CALL(*upstream_connection_, close(Network::ConnectionCloseType::NoFlush));
  EXPECT_CALL(*connect_or_op_timer_, disableTimer());
  client_->close();
}

TEST_F(RedisClientImplTest, RemoveFailedHealthCheck) {
  // This test simulates a health check response signaling traffic should be drained from the host.
  // As a result, the health checker will close the client in the call back.
  InSequence s;

  setup();

  Common::Redis::RespValue request1;
  MockClientCallbacks callbacks1;
  EXPECT_CALL(*encoder_, encode(Ref(request1), _));
  EXPECT_CALL(*flush_timer_, enabled()).WillOnce(Return(false));
  PoolRequest* handle1 = client_->makeRequest(request1, callbacks1);
  EXPECT_NE(nullptr, handle1);

  onConnected();

  Common::Redis::RespValuePtr response1(new Common::Redis::RespValue());
  // Each call should result in either onResponse or onFailure, never both.
  EXPECT_CALL(callbacks1, onFailure()).Times(0);
  EXPECT_CALL(callbacks1, onResponse_(Ref(response1)))
      .WillOnce(Invoke([&](Common::Redis::RespValuePtr&) {
        // The health checker might fail the active health check based on the response content, and
        // result in removing the host and closing the client.
        client_->close();
      }));
  EXPECT_CALL(*connect_or_op_timer_, disableTimer()).Times(2);
  EXPECT_CALL(host_->outlier_detector_,
              putResult(Upstream::Outlier::Result::ExtOriginRequestSuccess, _));
  callbacks_->onRespValue(std::move(response1));
}

TEST_F(RedisClientImplTest, RemoveFailedHost) {
  // This test simulates a health check request failed due to remote host closing the connection.
  // As a result the health checker will close the client in the call back.
  InSequence s;

  setup();

  NiceMock<Network::MockConnectionCallbacks> connection_callbacks;
  client_->addConnectionCallbacks(connection_callbacks);

  Common::Redis::RespValue request1;
  MockClientCallbacks callbacks1;
  EXPECT_CALL(*encoder_, encode(Ref(request1), _));
  EXPECT_CALL(*flush_timer_, enabled()).WillOnce(Return(false));
  PoolRequest* handle1 = client_->makeRequest(request1, callbacks1);
  EXPECT_NE(nullptr, handle1);

  onConnected();

  EXPECT_CALL(host_->outlier_detector_,
              putResult(Upstream::Outlier::Result::LocalOriginConnectFailed, _));
  EXPECT_CALL(callbacks1, onFailure()).WillOnce(Invoke([&]() { client_->close(); }));
  EXPECT_CALL(*connect_or_op_timer_, disableTimer());
  EXPECT_CALL(connection_callbacks, onEvent(Network::ConnectionEvent::RemoteClose));
  upstream_connection_->raiseEvent(Network::ConnectionEvent::RemoteClose);
}

// RESP3 init pipeline tests. Each uses InSequence; sendResp3InitCommands suppresses the inner
// makeRequestInternal auto-flush via queue_enabled_ and emits a single explicit
// flushBufferAndResetTimer, so each initialize() expects exactly one
// flush_timer_->enabled() call.

namespace {
[[maybe_unused]] Common::Redis::RespValue makeBareHello3Request() {
  return Utility::makeRequest("HELLO", {"3"});
}
[[maybe_unused]] Common::Redis::RespValue makeHello3AuthRequest(const std::string& user,
                                                                const std::string& pass) {
  return Utility::makeRequest("HELLO", {"3", "AUTH", user, pass});
}
[[maybe_unused]] Common::Redis::RespValuePtr makeHelloMapReply(int64_t proto_value) {
  auto reply = std::make_unique<Common::Redis::RespValue>();
  reply->type(Common::Redis::RespType::Map);
  std::vector<Common::Redis::RespValue> kv(2);
  kv[0].type(Common::Redis::RespType::BulkString);
  kv[0].asString() = "proto";
  kv[1].type(Common::Redis::RespType::Integer);
  kv[1].asInteger() = proto_value;
  reply->asArray().swap(kv);
  return reply;
}
} // namespace

// HELLO 3 (no auth) is encoded; pending=[HELLO]; client is active.
TEST_F(RedisClientImplTest, Resp3InitSendsBareHelloThenAwaitsReply) {
  InSequence s;
  enableResp3();
  setup();

  EXPECT_CALL(*encoder_, encode(Eq(makeBareHello3Request()), _));
  EXPECT_CALL(*flush_timer_, enabled()).WillOnce(Return(false));
  client_->initialize("", "");
  EXPECT_TRUE(client_->active());

  EXPECT_CALL(*upstream_connection_, close(Network::ConnectionCloseType::NoFlush));
  EXPECT_CALL(*connect_or_op_timer_, disableTimer());
  client_->close();
  EXPECT_EQ(1UL, hello3_failure_counter_->value());
}

// HELLO 3 AUTH user pass — single combined command.
TEST_F(RedisClientImplTest, Resp3InitWithAuthSendsHelloAuth) {
  InSequence s;
  enableResp3();
  setup();

  EXPECT_CALL(*encoder_, encode(Eq(makeHello3AuthRequest("alice", "secret")), _));
  EXPECT_CALL(*flush_timer_, enabled()).WillOnce(Return(false));
  client_->initialize("alice", "secret");

  EXPECT_CALL(*upstream_connection_, close(Network::ConnectionCloseType::NoFlush));
  EXPECT_CALL(*connect_or_op_timer_, disableTimer());
  client_->close();
}

// Username-only credentials (Redis 6 ACL user with no password) must still emit HELLO 3 AUTH.
// This mirrors the RESP2 AUTH behavior — without it, an ACL user that authorizes via username
// alone would silently end up unauthenticated on the upstream RESP3 connection.
TEST_F(RedisClientImplTest, Resp3InitializeWithUsernameOnlySendsHello3Auth) {
  InSequence s;
  enableResp3();
  setup();

  // HELLO 3 AUTH alice "" — username present, password empty.
  EXPECT_CALL(*encoder_, encode(Eq(makeHello3AuthRequest("alice", "")), _));
  EXPECT_CALL(*flush_timer_, enabled()).WillOnce(Return(false));
  client_->initialize("alice", "");

  EXPECT_CALL(*upstream_connection_, close(Network::ConnectionCloseType::NoFlush));
  EXPECT_CALL(*connect_or_op_timer_, disableTimer());
  client_->close();
}

// Empty username + non-empty password → AUTH "default" (Redis 6 ACL synonym for the legacy
// AUTH-with-just-password contract).
TEST_F(RedisClientImplTest, Resp3InitOnlyPasswordUsesDefaultUsername) {
  InSequence s;
  enableResp3();
  setup();

  EXPECT_CALL(*encoder_, encode(Eq(makeHello3AuthRequest("default", "secret")), _));
  EXPECT_CALL(*flush_timer_, enabled()).WillOnce(Return(false));
  client_->initialize("", "secret");

  EXPECT_CALL(*upstream_connection_, close(Network::ConnectionCloseType::NoFlush));
  EXPECT_CALL(*connect_or_op_timer_, disableTimer());
  client_->close();
}

// User makeRequest while init_state_ != Ready returns a HeldUserRequest (not nullptr); active()
// includes held; pre-replay cancel erases the entry and increments upstream_rq_cancelled_
// without firing the user callback.
TEST_F(RedisClientImplTest, Resp3UserRequestHeldDuringInitAndCancelBeforeReplay) {
  InSequence s;
  enableResp3();
  setup();

  EXPECT_CALL(*encoder_, encode(Eq(makeBareHello3Request()), _));
  EXPECT_CALL(*flush_timer_, enabled()).WillOnce(Return(false));
  client_->initialize("", "");

  Common::Redis::RespValue user_request;
  MockClientCallbacks user_callbacks;
  EXPECT_CALL(*encoder_, encode(Ref(user_request), _)).Times(0);
  PoolRequest* held = client_->makeRequest(user_request, user_callbacks);
  ASSERT_NE(nullptr, held);
  EXPECT_TRUE(client_->active());

  held->cancel();
  EXPECT_EQ(1UL, host_->cluster_.traffic_stats_->upstream_rq_cancelled_.value());
  EXPECT_TRUE(client_->active()); // HELLO still pending.

  EXPECT_CALL(*upstream_connection_, close(Network::ConnectionCloseType::NoFlush));
  EXPECT_CALL(*connect_or_op_timer_, disableTimer());
  client_->close();
  EXPECT_EQ(1UL, hello3_failure_counter_->value());
}

// HELLO success → state→Ready (Primary policy → no READONLY).
TEST_F(RedisClientImplTest, Resp3HelloSuccessTransitionsToReadyForPrimaryPolicy) {
  InSequence s;
  enableResp3();
  setup();

  EXPECT_CALL(*encoder_, encode(Eq(makeBareHello3Request()), _));
  EXPECT_CALL(*flush_timer_, enabled()).WillOnce(Return(false));
  client_->initialize("", "");
  onConnected();

  EXPECT_CALL(*connect_or_op_timer_, disableTimer());
  EXPECT_CALL(host_->outlier_detector_,
              putResult(Upstream::Outlier::Result::ExtOriginRequestSuccess, _));
  respondWith(makeHelloMapReply(3));
  EXPECT_FALSE(client_->active());
  EXPECT_EQ(0UL, hello3_failure_counter_->value());

  EXPECT_CALL(*upstream_connection_, close(Network::ConnectionCloseType::NoFlush));
  EXPECT_CALL(*connect_or_op_timer_, disableTimer());
  client_->close();
}

// HELLO Error reply → onInitFailure → close + counter inc + held SET sees onFailure.
TEST_F(RedisClientImplTest, Resp3HelloErrorReplyClosesAndFailsHeld) {
  InSequence s;
  enableResp3();
  setup();

  EXPECT_CALL(*encoder_, encode(Eq(makeBareHello3Request()), _));
  EXPECT_CALL(*flush_timer_, enabled()).WillOnce(Return(false));
  client_->initialize("", "");

  Common::Redis::RespValue user_request;
  MockClientCallbacks user_callbacks;
  client_->makeRequest(user_request, user_callbacks);

  onConnected();

  Common::Redis::RespValuePtr err{new Common::Redis::RespValue()};
  err->type(Common::Redis::RespType::Error);
  err->asString() = "ERR HELLO not supported";

  EXPECT_CALL(user_callbacks, onFailure());
  EXPECT_CALL(*upstream_connection_, close(Network::ConnectionCloseType::NoFlush));
  EXPECT_CALL(*connect_or_op_timer_, disableTimer());
  EXPECT_CALL(*connect_or_op_timer_, disableTimer());
  EXPECT_CALL(host_->outlier_detector_,
              putResult(Upstream::Outlier::Result::ExtOriginRequestSuccess, _));
  respondWith(std::move(err));
  EXPECT_EQ(1UL, hello3_failure_counter_->value());
}

// HELLO Map with proto != 3 — isHello3SuccessResponse rejects → failure cascade.
TEST_F(RedisClientImplTest, Resp3HelloMapWithProtoTwoIsTreatedAsFailure) {
  InSequence s;
  enableResp3();
  setup();

  EXPECT_CALL(*encoder_, encode(Eq(makeBareHello3Request()), _));
  EXPECT_CALL(*flush_timer_, enabled()).WillOnce(Return(false));
  client_->initialize("", "");

  Common::Redis::RespValue user_request;
  MockClientCallbacks user_callbacks;
  client_->makeRequest(user_request, user_callbacks);

  onConnected();

  EXPECT_CALL(user_callbacks, onFailure());
  EXPECT_CALL(*upstream_connection_, close(Network::ConnectionCloseType::NoFlush));
  EXPECT_CALL(*connect_or_op_timer_, disableTimer());
  EXPECT_CALL(*connect_or_op_timer_, disableTimer());
  EXPECT_CALL(host_->outlier_detector_,
              putResult(Upstream::Outlier::Result::ExtOriginRequestSuccess, _));
  respondWith(makeHelloMapReply(2));
  EXPECT_EQ(1UL, hello3_failure_counter_->value());
}

// HELLO replied with bare "+OK" SimpleString (some RESP2 servers ignore the version arg) —
// isHello3SuccessResponse rejects (type != Map/Array) → failure cascade.
TEST_F(RedisClientImplTest, Resp3HelloPlainOkSimpleStringIsTreatedAsFailure) {
  InSequence s;
  enableResp3();
  setup();

  EXPECT_CALL(*encoder_, encode(Eq(makeBareHello3Request()), _));
  EXPECT_CALL(*flush_timer_, enabled()).WillOnce(Return(false));
  client_->initialize("", "");

  Common::Redis::RespValue user_request;
  MockClientCallbacks user_callbacks;
  client_->makeRequest(user_request, user_callbacks);

  onConnected();

  Common::Redis::RespValuePtr ok{new Common::Redis::RespValue()};
  ok->type(Common::Redis::RespType::SimpleString);
  ok->asString() = "OK";

  EXPECT_CALL(user_callbacks, onFailure());
  EXPECT_CALL(*upstream_connection_, close(Network::ConnectionCloseType::NoFlush));
  EXPECT_CALL(*connect_or_op_timer_, disableTimer());
  EXPECT_CALL(*connect_or_op_timer_, disableTimer());
  EXPECT_CALL(host_->outlier_detector_,
              putResult(Upstream::Outlier::Result::ExtOriginRequestSuccess, _));
  respondWith(std::move(ok));
  EXPECT_EQ(1UL, hello3_failure_counter_->value());
}

// HELLO MOVED — onRedirection in hello_init_callbacks_ is treated as failure (HELLO does not
// honor MOVED/ASK redirection per Redis semantics).
TEST_F(RedisClientImplTest, Resp3HelloRedirectionTreatedAsFailure) {
  InSequence s;
  enableResp3();
  setup();

  EXPECT_CALL(*encoder_, encode(Eq(makeBareHello3Request()), _));
  EXPECT_CALL(*flush_timer_, enabled()).WillOnce(Return(false));
  client_->initialize("", "");

  Common::Redis::RespValue user_request;
  MockClientCallbacks user_callbacks;
  client_->makeRequest(user_request, user_callbacks);

  onConnected();

  Common::Redis::RespValuePtr moved{new Common::Redis::RespValue()};
  moved->type(Common::Redis::RespType::Error);
  moved->asString() = "MOVED 1234 10.0.0.1:6379";

  EXPECT_CALL(user_callbacks, onFailure());
  EXPECT_CALL(*upstream_connection_, close(Network::ConnectionCloseType::NoFlush));
  EXPECT_CALL(*connect_or_op_timer_, disableTimer());
  EXPECT_CALL(*connect_or_op_timer_, disableTimer());
  EXPECT_CALL(host_->outlier_detector_,
              putResult(Upstream::Outlier::Result::ExtOriginRequestSuccess, _));
  respondWith(std::move(moved));
  EXPECT_EQ(1UL, hello3_failure_counter_->value());
}

// Push during AwaitingHello: dropped (no close, no counter, FIFO survives — the subsequent
// HELLO ack still routes to hello_init_callbacks_).
TEST_F(RedisClientImplTest, Resp3PushFrameDuringInitIsDroppedThenHelloAck) {
  InSequence s;
  enableResp3();
  setup();

  EXPECT_CALL(*encoder_, encode(Eq(makeBareHello3Request()), _));
  EXPECT_CALL(*flush_timer_, enabled()).WillOnce(Return(false));
  client_->initialize("", "");
  onConnected();

  auto push = std::make_unique<Common::Redis::RespValue>();
  push->type(Common::Redis::RespType::Push);
  respondWith(std::move(push));
  EXPECT_TRUE(client_->active());
  EXPECT_EQ(0UL, host_->cluster_.traffic_stats_->upstream_cx_protocol_error_.value());
  EXPECT_EQ(0UL, hello3_failure_counter_->value());

  EXPECT_CALL(*connect_or_op_timer_, disableTimer());
  EXPECT_CALL(host_->outlier_detector_,
              putResult(Upstream::Outlier::Result::ExtOriginRequestSuccess, _));
  respondWith(makeHelloMapReply(3));
  EXPECT_FALSE(client_->active());

  EXPECT_CALL(*upstream_connection_, close(Network::ConnectionCloseType::NoFlush));
  EXPECT_CALL(*connect_or_op_timer_, disableTimer());
  client_->close();
}

// Held SET during AwaitingHello → HELLO success replays via makeRequestInternal (encode fires
// for the deep-copied user request; Ref(user_request) would fail to match — Eq does).
TEST_F(RedisClientImplTest, Resp3UserRequestHeldThenReplayedOnReady) {
  InSequence s;
  enableResp3();
  setup();

  EXPECT_CALL(*encoder_, encode(Eq(makeBareHello3Request()), _));
  EXPECT_CALL(*flush_timer_, enabled()).WillOnce(Return(false));
  client_->initialize("", "");

  Common::Redis::RespValue user_request;
  MockClientCallbacks user_callbacks;
  PoolRequest* held = client_->makeRequest(user_request, user_callbacks);
  ASSERT_NE(nullptr, held);
  EXPECT_TRUE(client_->active());

  onConnected();

  EXPECT_CALL(*encoder_, encode(Eq(user_request), _));
  EXPECT_CALL(*connect_or_op_timer_, enableTimer(_, _));
  EXPECT_CALL(*flush_timer_, enabled()).WillOnce(Return(false));
  EXPECT_CALL(*connect_or_op_timer_, enableTimer(_, _));
  EXPECT_CALL(host_->outlier_detector_,
              putResult(Upstream::Outlier::Result::ExtOriginRequestSuccess, _));
  respondWith(makeHelloMapReply(3));
  EXPECT_TRUE(client_->active());

  // Tear down via cancel-then-close so the user_callbacks dtor doesn't see an unfulfilled
  // expectation. The cancel stat is incremented by the close drain's canceled branch.
  held->cancel();
  EXPECT_CALL(*upstream_connection_, close(Network::ConnectionCloseType::NoFlush));
  EXPECT_CALL(*connect_or_op_timer_, disableTimer());
  client_->close();
  EXPECT_EQ(1UL, host_->cluster_.traffic_stats_->upstream_rq_cancelled_.value());
}

// Non-Primary read policy: HELLO succeeds → AwaitingReadonly → READONLY sent (NOT before HELLO
// ack); READONLY succeeds → Ready. Pins the strict HELLO-then-READONLY phasing.
TEST_F(RedisClientImplTest, Resp3HelloSuccessThenReadonlyForNonPrimary) {
  InSequence s;
  enableResp3();
  setup(std::make_shared<ConfigImpl>(
      createConnPoolSettings(20, true, true, 100,
                             envoy::extensions::filters::network::redis_proxy::v3::RedisProxy::
                                 ConnPoolSettings::REPLICA)));

  EXPECT_CALL(*encoder_, encode(Eq(makeBareHello3Request()), _));
  EXPECT_CALL(*flush_timer_, enabled()).WillOnce(Return(false));
  client_->initialize("", "");
  onConnected();

  // sendReadonlyInit now uses queue_enabled_ to suppress makeRequestInternal's auto-flush;
  // single explicit flush below. The op timer arms once during makeRequestInternal (READONLY
  // is the first pending request) and again at the end of onRespValue (after the HELLO pop
  // leaves READONLY as the sole pending request).
  EXPECT_CALL(*encoder_, encode(Eq(Utility::ReadOnlyRequest::instance()), _));
  EXPECT_CALL(*connect_or_op_timer_, enableTimer(_, _));
  EXPECT_CALL(*flush_timer_, enabled()).WillOnce(Return(false));
  EXPECT_CALL(*connect_or_op_timer_, enableTimer(_, _));
  EXPECT_CALL(host_->outlier_detector_,
              putResult(Upstream::Outlier::Result::ExtOriginRequestSuccess, _));
  respondWith(makeHelloMapReply(3));

  Common::Redis::RespValuePtr ok{new Common::Redis::RespValue()};
  ok->type(Common::Redis::RespType::SimpleString);
  ok->asString() = "OK";
  EXPECT_CALL(*connect_or_op_timer_, disableTimer());
  EXPECT_CALL(host_->outlier_detector_,
              putResult(Upstream::Outlier::Result::ExtOriginRequestSuccess, _));
  respondWith(std::move(ok));
  EXPECT_FALSE(client_->active());

  EXPECT_CALL(*upstream_connection_, close(Network::ConnectionCloseType::NoFlush));
  EXPECT_CALL(*connect_or_op_timer_, disableTimer());
  client_->close();
  EXPECT_EQ(0UL, hello3_failure_counter_->value());
}

// AWS IAM + RESP3 sync token: addCallbackIfCredentialsPending returns false → token fetched
// immediately → encode HELLO 3 AUTH user token.
TEST_F(RedisClientImplTest, Resp3AwsIamSyncTokenSendsHelloAuth) {
  InSequence s;
  enableResp3();
  enableAwsIam();
  setup();

  EXPECT_CALL(
      *mock_aws_iam_authenticator_,
      addCallbackIfCredentialsPending(An<Extensions::Common::Aws::CredentialsPendingCallback&&>()))
      .WillOnce(Return(false));
  EXPECT_CALL(*mock_aws_iam_authenticator_, getAuthToken("alice", _)).WillOnce(Return("iam_token"));

  EXPECT_CALL(*encoder_, encode(Eq(makeHello3AuthRequest("alice", "iam_token")), _));
  EXPECT_CALL(*flush_timer_, enabled()).WillOnce(Return(false));
  client_->initialize("alice", "");

  EXPECT_CALL(*upstream_connection_, close(Network::ConnectionCloseType::NoFlush));
  EXPECT_CALL(*connect_or_op_timer_, disableTimer());
  client_->close();
}

// AWS IAM + RESP2 sync token: post-token path goes via AwaitingAuth → plain AUTH (not HELLO 3).
TEST_F(RedisClientImplTest, Resp2AwsIamSyncTokenSendsAuth) {
  InSequence s;
  enableAwsIam();
  setup();

  EXPECT_CALL(
      *mock_aws_iam_authenticator_,
      addCallbackIfCredentialsPending(An<Extensions::Common::Aws::CredentialsPendingCallback&&>()))
      .WillOnce(Return(false));
  EXPECT_CALL(*mock_aws_iam_authenticator_, getAuthToken("alice", _)).WillOnce(Return("iam_token"));

  EXPECT_CALL(*encoder_, encode(Eq(Utility::AuthRequest("alice", "iam_token")), _));
  EXPECT_CALL(*flush_timer_, enabled()).WillOnce(Return(false));
  client_->initialize("alice", "");

  EXPECT_CALL(*upstream_connection_, close(Network::ConnectionCloseType::NoFlush));
  EXPECT_CALL(*connect_or_op_timer_, disableTimer());
  client_->close();
}

// RESP2 + IAM with a non-Primary read policy must follow AUTH with READONLY, matching the
// RESP2-no-IAM and RESP3 init paths. Without this, replicas-targeted reads land on the master
// instead of the replica because the connection was never marked READONLY.
TEST_F(RedisClientImplTest, Resp2AwsIamReplicaReadPolicySendsReadonlyAfterAuth) {
  InSequence s;
  enableAwsIam();
  setup(std::make_shared<ConfigImpl>(
      createConnPoolSettings(20, true, true, 100,
                             envoy::extensions::filters::network::redis_proxy::v3::RedisProxy::
                                 ConnPoolSettings::REPLICA)));

  EXPECT_CALL(
      *mock_aws_iam_authenticator_,
      addCallbackIfCredentialsPending(An<Extensions::Common::Aws::CredentialsPendingCallback&&>()))
      .WillOnce(Return(false));
  EXPECT_CALL(*mock_aws_iam_authenticator_, getAuthToken("alice", _)).WillOnce(Return("iam_token"));

  // AUTH goes out post-token. READONLY must NOT be sent before AUTH succeeds.
  EXPECT_CALL(*encoder_, encode(Eq(Utility::AuthRequest("alice", "iam_token")), _));
  EXPECT_CALL(*flush_timer_, enabled()).WillOnce(Return(false));
  client_->initialize("alice", "");
  onConnected();

  // AUTH +OK → READONLY is encoded (the symmetry being pinned). Mirrors the RESP3-non-Primary
  // expectation block in Resp3HelloSuccessThenReadonlyForNonPrimary. sendReadonlyInit now
  // uses queue_enabled_ so makeRequestInternal skips its auto-flush; one explicit flush.
  EXPECT_CALL(*encoder_, encode(Eq(Utility::ReadOnlyRequest::instance()), _));
  EXPECT_CALL(*connect_or_op_timer_, enableTimer(_, _));
  EXPECT_CALL(*flush_timer_, enabled()).WillOnce(Return(false));
  EXPECT_CALL(*connect_or_op_timer_, enableTimer(_, _));
  EXPECT_CALL(host_->outlier_detector_,
              putResult(Upstream::Outlier::Result::ExtOriginRequestSuccess, _));
  Common::Redis::RespValuePtr auth_ok{new Common::Redis::RespValue()};
  auth_ok->type(Common::Redis::RespType::SimpleString);
  auth_ok->asString() = "OK";
  respondWith(std::move(auth_ok));

  // READONLY +OK → Ready. Client becomes idle.
  EXPECT_CALL(*connect_or_op_timer_, disableTimer());
  EXPECT_CALL(host_->outlier_detector_,
              putResult(Upstream::Outlier::Result::ExtOriginRequestSuccess, _));
  Common::Redis::RespValuePtr readonly_ok{new Common::Redis::RespValue()};
  readonly_ok->type(Common::Redis::RespType::SimpleString);
  readonly_ok->asString() = "OK";
  respondWith(std::move(readonly_ok));
  EXPECT_FALSE(client_->active());

  EXPECT_CALL(*upstream_connection_, close(Network::ConnectionCloseType::NoFlush));
  EXPECT_CALL(*connect_or_op_timer_, disableTimer());
  client_->close();
}

// AWS IAM pending token: user makeRequest during WaitingForAwsToken is held. On token arrival,
// HELLO 3 AUTH user token is encoded; held SET stays held. Closes the wire/pending mismatch
// hazard that the held queue exists to fix.
TEST_F(RedisClientImplTest, Resp3AwsIamPendingTokenHoldsUserRequestUntilHello) {
  InSequence s;
  enableResp3();
  enableAwsIam();
  setup();

  Extensions::Common::Aws::CredentialsPendingCallback captured_on_token;
  EXPECT_CALL(
      *mock_aws_iam_authenticator_,
      addCallbackIfCredentialsPending(An<Extensions::Common::Aws::CredentialsPendingCallback&&>()))
      .WillOnce(DoAll(SaveArg<0>(&captured_on_token), Return(true)));
  client_->initialize("alice", "");

  Common::Redis::RespValue user_request;
  MockClientCallbacks user_callbacks;
  EXPECT_CALL(*encoder_, encode(Ref(user_request), _)).Times(0);
  PoolRequest* held = client_->makeRequest(user_request, user_callbacks);
  ASSERT_NE(nullptr, held);
  EXPECT_TRUE(client_->active());

  EXPECT_CALL(*mock_aws_iam_authenticator_, getAuthToken("alice", _)).WillOnce(Return("iam_token"));
  EXPECT_CALL(*encoder_, encode(Eq(makeHello3AuthRequest("alice", "iam_token")), _));
  EXPECT_CALL(*flush_timer_, enabled()).WillOnce(Return(false));
  captured_on_token();

  EXPECT_CALL(*upstream_connection_, close(Network::ConnectionCloseType::NoFlush));
  EXPECT_CALL(user_callbacks, onFailure());
  EXPECT_CALL(*connect_or_op_timer_, disableTimer());
  client_->close();
  EXPECT_EQ(1UL, hello3_failure_counter_->value());
}

// AWS IAM token-fetch callback fires after ClientImpl destruction → weak_ptr guard short-
// circuits, getAuthToken is never called. Pins against UAF.
TEST_F(RedisClientImplTest, Resp3AwsIamCallbackAfterClientDestroyedIsNoOp) {
  InSequence s;
  enableResp3();
  enableAwsIam();
  setup();

  Extensions::Common::Aws::CredentialsPendingCallback captured_on_token;
  EXPECT_CALL(
      *mock_aws_iam_authenticator_,
      addCallbackIfCredentialsPending(An<Extensions::Common::Aws::CredentialsPendingCallback&&>()))
      .WillOnce(DoAll(SaveArg<0>(&captured_on_token), Return(true)));
  client_->initialize("alice", "");

  EXPECT_CALL(*upstream_connection_, close(Network::ConnectionCloseType::NoFlush));
  EXPECT_CALL(*connect_or_op_timer_, disableTimer());
  client_->close();
  client_.reset();

  EXPECT_CALL(*mock_aws_iam_authenticator_, getAuthToken(_, _)).Times(0);
  captured_on_token();
}

// HeldUserRequest::cancel post-replay forwards to live_request_->cancel() and self-destructs.
// The cancel stat is NOT incremented at cancel() time — it increments when onRespValue's
// canceled branch later processes the live PendingRequest. user_callbacks must NEVER fire.
TEST_F(RedisClientImplTest, Resp3HeldUserRequestCancelAfterReplay) {
  InSequence s;
  enableResp3();
  setup();

  EXPECT_CALL(*encoder_, encode(Eq(makeBareHello3Request()), _));
  EXPECT_CALL(*flush_timer_, enabled()).WillOnce(Return(false));
  client_->initialize("", "");

  Common::Redis::RespValue user_request;
  MockClientCallbacks user_callbacks;
  PoolRequest* held = client_->makeRequest(user_request, user_callbacks);
  ASSERT_NE(nullptr, held);
  onConnected();

  EXPECT_CALL(*encoder_, encode(Eq(user_request), _));
  EXPECT_CALL(*connect_or_op_timer_, enableTimer(_, _));
  EXPECT_CALL(*flush_timer_, enabled()).WillOnce(Return(false));
  EXPECT_CALL(*connect_or_op_timer_, enableTimer(_, _));
  EXPECT_CALL(host_->outlier_detector_,
              putResult(Upstream::Outlier::Result::ExtOriginRequestSuccess, _));
  respondWith(makeHelloMapReply(3));

  held->cancel();
  EXPECT_EQ(0UL, host_->cluster_.traffic_stats_->upstream_rq_cancelled_.value());

  Common::Redis::RespValuePtr response{new Common::Redis::RespValue()};
  response->type(Common::Redis::RespType::SimpleString);
  response->asString() = "OK";
  EXPECT_CALL(*connect_or_op_timer_, disableTimer());
  EXPECT_CALL(host_->outlier_detector_,
              putResult(Upstream::Outlier::Result::ExtOriginRequestSuccess, _));
  respondWith(std::move(response));
  EXPECT_EQ(1UL, host_->cluster_.traffic_stats_->upstream_rq_cancelled_.value());

  EXPECT_CALL(*upstream_connection_, close(Network::ConnectionCloseType::NoFlush));
  EXPECT_CALL(*connect_or_op_timer_, disableTimer());
  client_->close();
}

// Connection dies upstream while AwaitingHello with held SET: held SET sees onFailure via
// failHeldUserRequests; HELLO callback's onInitFailure short-circuits (state already
// Failed by the top-of-onEvent transition).
TEST_F(RedisClientImplTest, ConnectionCloseDuringAwaitingHelloFailsHeld) {
  InSequence s;
  enableResp3();
  setup();

  EXPECT_CALL(*encoder_, encode(Eq(makeBareHello3Request()), _));
  EXPECT_CALL(*flush_timer_, enabled()).WillOnce(Return(false));
  client_->initialize("", "");

  Common::Redis::RespValue user_request;
  MockClientCallbacks user_callbacks;
  client_->makeRequest(user_request, user_callbacks);

  onConnected();

  EXPECT_CALL(user_callbacks, onFailure());
  EXPECT_CALL(host_->outlier_detector_,
              putResult(Upstream::Outlier::Result::LocalOriginConnectFailed, _));
  EXPECT_CALL(*connect_or_op_timer_, disableTimer());
  upstream_connection_->raiseEvent(Network::ConnectionEvent::RemoteClose);
  EXPECT_EQ(1UL, hello3_failure_counter_->value());
}

// READONLY error reply during AwaitingReadonly: the init pipeline must fail; held user
// requests get onFailure via failHeldUserRequests; connection closes; HELLO failure counter
// stays at 0 (READONLY error is a separate stat not tied to HELLO).
TEST_F(RedisClientImplTest, Resp3ReadonlyErrorReplyFailsInit) {
  InSequence s;
  enableResp3();
  setup(std::make_shared<ConfigImpl>(
      createConnPoolSettings(20, true, true, 100,
                             envoy::extensions::filters::network::redis_proxy::v3::RedisProxy::
                                 ConnPoolSettings::REPLICA)));

  EXPECT_CALL(*encoder_, encode(Eq(makeBareHello3Request()), _));
  EXPECT_CALL(*flush_timer_, enabled()).WillOnce(Return(false));
  client_->initialize("", "");

  Common::Redis::RespValue user_request;
  MockClientCallbacks user_callbacks;
  client_->makeRequest(user_request, user_callbacks);

  onConnected();

  EXPECT_CALL(*encoder_, encode(Eq(Utility::ReadOnlyRequest::instance()), _));
  EXPECT_CALL(*connect_or_op_timer_, enableTimer(_, _));
  EXPECT_CALL(*flush_timer_, enabled()).WillOnce(Return(false));
  EXPECT_CALL(*connect_or_op_timer_, enableTimer(_, _));
  EXPECT_CALL(host_->outlier_detector_,
              putResult(Upstream::Outlier::Result::ExtOriginRequestSuccess, _));
  respondWith(makeHelloMapReply(3));

  Common::Redis::RespValuePtr err{new Common::Redis::RespValue()};
  err->type(Common::Redis::RespType::Error);
  err->asString() = "ERR READONLY not supported on this server";

  EXPECT_CALL(user_callbacks, onFailure());
  EXPECT_CALL(*upstream_connection_, close(Network::ConnectionCloseType::NoFlush));
  EXPECT_CALL(*connect_or_op_timer_, disableTimer());
  EXPECT_CALL(*connect_or_op_timer_, disableTimer());
  EXPECT_CALL(host_->outlier_detector_,
              putResult(Upstream::Outlier::Result::ExtOriginRequestSuccess, _));
  respondWith(std::move(err));
  EXPECT_EQ(0UL, hello3_failure_counter_->value());
}

// READONLY MOVED redirection during AwaitingReadonly: redirection is treated as failure (per
// the same rationale as HELLO MOVED — READONLY is a non-keyed init command, redirection is
// unspec'd here). Init fails; held user requests fail; connection closes.
TEST_F(RedisClientImplTest, Resp3ReadonlyRedirectionTreatedAsFailure) {
  InSequence s;
  enableResp3();
  setup(std::make_shared<ConfigImpl>(
      createConnPoolSettings(20, true, true, 100,
                             envoy::extensions::filters::network::redis_proxy::v3::RedisProxy::
                                 ConnPoolSettings::REPLICA)));

  EXPECT_CALL(*encoder_, encode(Eq(makeBareHello3Request()), _));
  EXPECT_CALL(*flush_timer_, enabled()).WillOnce(Return(false));
  client_->initialize("", "");

  Common::Redis::RespValue user_request;
  MockClientCallbacks user_callbacks;
  client_->makeRequest(user_request, user_callbacks);

  onConnected();

  EXPECT_CALL(*encoder_, encode(Eq(Utility::ReadOnlyRequest::instance()), _));
  EXPECT_CALL(*connect_or_op_timer_, enableTimer(_, _));
  EXPECT_CALL(*flush_timer_, enabled()).WillOnce(Return(false));
  EXPECT_CALL(*connect_or_op_timer_, enableTimer(_, _));
  EXPECT_CALL(host_->outlier_detector_,
              putResult(Upstream::Outlier::Result::ExtOriginRequestSuccess, _));
  respondWith(makeHelloMapReply(3));

  Common::Redis::RespValuePtr moved{new Common::Redis::RespValue()};
  moved->type(Common::Redis::RespType::Error);
  moved->asString() = "MOVED 1234 10.0.0.1:6379";

  EXPECT_CALL(user_callbacks, onFailure());
  EXPECT_CALL(*upstream_connection_, close(Network::ConnectionCloseType::NoFlush));
  EXPECT_CALL(*connect_or_op_timer_, disableTimer());
  EXPECT_CALL(*connect_or_op_timer_, disableTimer());
  EXPECT_CALL(host_->outlier_detector_,
              putResult(Upstream::Outlier::Result::ExtOriginRequestSuccess, _));
  respondWith(std::move(moved));
}

// AWS IAM AUTH error reply during AwaitingAuth: the init pipeline must fail; held user
// requests fail; connection closes.
TEST_F(RedisClientImplTest, Resp2AwsIamAuthErrorReplyFailsInit) {
  InSequence s;
  enableAwsIam();
  setup();

  EXPECT_CALL(
      *mock_aws_iam_authenticator_,
      addCallbackIfCredentialsPending(An<Extensions::Common::Aws::CredentialsPendingCallback&&>()))
      .WillOnce(Return(false));
  EXPECT_CALL(*mock_aws_iam_authenticator_, getAuthToken("alice", _)).WillOnce(Return("iam_token"));

  EXPECT_CALL(*encoder_, encode(Eq(Utility::AuthRequest("alice", "iam_token")), _));
  EXPECT_CALL(*flush_timer_, enabled()).WillOnce(Return(false));
  client_->initialize("alice", "");

  Common::Redis::RespValue user_request;
  MockClientCallbacks user_callbacks;
  client_->makeRequest(user_request, user_callbacks);

  onConnected();

  Common::Redis::RespValuePtr err{new Common::Redis::RespValue()};
  err->type(Common::Redis::RespType::Error);
  err->asString() = "WRONGPASS invalid IAM credentials";

  EXPECT_CALL(user_callbacks, onFailure());
  EXPECT_CALL(*upstream_connection_, close(Network::ConnectionCloseType::NoFlush));
  EXPECT_CALL(*connect_or_op_timer_, disableTimer());
  EXPECT_CALL(*connect_or_op_timer_, disableTimer());
  EXPECT_CALL(host_->outlier_detector_,
              putResult(Upstream::Outlier::Result::ExtOriginRequestSuccess, _));
  respondWith(std::move(err));
}

// AWS IAM AUTH MOVED redirection during AwaitingAuth: treated as failure for the same reason
// HELLO MOVED is — AUTH/READONLY/HELLO are non-keyed init commands and redirection on them
// has no defined semantics.
TEST_F(RedisClientImplTest, Resp2AwsIamAuthRedirectionTreatedAsFailure) {
  InSequence s;
  enableAwsIam();
  setup();

  EXPECT_CALL(
      *mock_aws_iam_authenticator_,
      addCallbackIfCredentialsPending(An<Extensions::Common::Aws::CredentialsPendingCallback&&>()))
      .WillOnce(Return(false));
  EXPECT_CALL(*mock_aws_iam_authenticator_, getAuthToken("alice", _)).WillOnce(Return("iam_token"));

  EXPECT_CALL(*encoder_, encode(Eq(Utility::AuthRequest("alice", "iam_token")), _));
  EXPECT_CALL(*flush_timer_, enabled()).WillOnce(Return(false));
  client_->initialize("alice", "");

  Common::Redis::RespValue user_request;
  MockClientCallbacks user_callbacks;
  client_->makeRequest(user_request, user_callbacks);

  onConnected();

  Common::Redis::RespValuePtr moved{new Common::Redis::RespValue()};
  moved->type(Common::Redis::RespType::Error);
  moved->asString() = "MOVED 1234 10.0.0.1:6379";

  EXPECT_CALL(user_callbacks, onFailure());
  EXPECT_CALL(*upstream_connection_, close(Network::ConnectionCloseType::NoFlush));
  EXPECT_CALL(*connect_or_op_timer_, disableTimer());
  EXPECT_CALL(*connect_or_op_timer_, disableTimer());
  EXPECT_CALL(host_->outlier_detector_,
              putResult(Upstream::Outlier::Result::ExtOriginRequestSuccess, _));
  respondWith(std::move(moved));
}

// HeldUserRequest::onRedirection forwarding: after init completes and the held request is
// replayed, the live PendingRequest's callback IS the HeldUserRequest wrapper. When upstream
// answers with MOVED, the wrapper's onRedirection forwards to the original ClientCallbacks the
// user supplied at makeRequest time.
TEST_F(RedisClientImplTest, Resp3HeldUserRequestUpstreamRedirectionForwarded) {
  InSequence s;
  enableResp3();
  setup();

  EXPECT_CALL(*encoder_, encode(Eq(makeBareHello3Request()), _));
  EXPECT_CALL(*flush_timer_, enabled()).WillOnce(Return(false));
  client_->initialize("", "");

  Common::Redis::RespValue user_request;
  MockClientCallbacks user_callbacks;
  PoolRequest* held = client_->makeRequest(user_request, user_callbacks);
  ASSERT_NE(nullptr, held);

  onConnected();

  // HELLO success → replay the held request upstream.
  EXPECT_CALL(*encoder_, encode(Eq(user_request), _));
  EXPECT_CALL(*connect_or_op_timer_, enableTimer(_, _));
  EXPECT_CALL(*flush_timer_, enabled()).WillOnce(Return(false));
  EXPECT_CALL(*connect_or_op_timer_, enableTimer(_, _));
  EXPECT_CALL(host_->outlier_detector_,
              putResult(Upstream::Outlier::Result::ExtOriginRequestSuccess, _));
  respondWith(makeHelloMapReply(3));

  // Upstream replies MOVED to the live (replayed) request. The wrapper forwards to user.
  Common::Redis::RespValuePtr moved{new Common::Redis::RespValue()};
  moved->type(Common::Redis::RespType::Error);
  moved->asString() = "MOVED 1234 10.0.0.1:6379";

  EXPECT_CALL(user_callbacks, onRedirection_(_, "10.0.0.1:6379", false));
  EXPECT_CALL(*connect_or_op_timer_, disableTimer());
  EXPECT_CALL(host_->outlier_detector_,
              putResult(Upstream::Outlier::Result::ExtOriginRequestSuccess, _));
  respondWith(std::move(moved));

  EXPECT_CALL(*upstream_connection_, close(Network::ConnectionCloseType::NoFlush));
  EXPECT_CALL(*connect_or_op_timer_, disableTimer());
  client_->close();
}

// HeldUserRequest::onFailure forwarding: after replay, an upstream connection close fails the
// live PendingRequest, whose callback is the HeldUserRequest wrapper. The wrapper forwards
// onFailure to the original ClientCallbacks. Distinct from failHeldUserRequests, which fires
// onFailure on entries that NEVER got replayed (i.e., init itself failed).
TEST_F(RedisClientImplTest, Resp3HeldUserRequestUpstreamFailureForwarded) {
  InSequence s;
  enableResp3();
  setup();

  NiceMock<Network::MockConnectionCallbacks> connection_callbacks;
  client_->addConnectionCallbacks(connection_callbacks);

  EXPECT_CALL(*encoder_, encode(Eq(makeBareHello3Request()), _));
  EXPECT_CALL(*flush_timer_, enabled()).WillOnce(Return(false));
  client_->initialize("", "");

  Common::Redis::RespValue user_request;
  MockClientCallbacks user_callbacks;
  PoolRequest* held = client_->makeRequest(user_request, user_callbacks);
  ASSERT_NE(nullptr, held);

  onConnected();

  // HELLO success → replay → live request issued upstream.
  EXPECT_CALL(*encoder_, encode(Eq(user_request), _));
  EXPECT_CALL(*connect_or_op_timer_, enableTimer(_, _));
  EXPECT_CALL(*flush_timer_, enabled()).WillOnce(Return(false));
  EXPECT_CALL(*connect_or_op_timer_, enableTimer(_, _));
  EXPECT_CALL(host_->outlier_detector_,
              putResult(Upstream::Outlier::Result::ExtOriginRequestSuccess, _));
  respondWith(makeHelloMapReply(3));

  // Upstream RemoteClose with an active live request. ClientImpl fails all pending → wrapper's
  // onFailure → user.onFailure.
  EXPECT_CALL(host_->outlier_detector_,
              putResult(Upstream::Outlier::Result::LocalOriginConnectFailed, _));
  EXPECT_CALL(user_callbacks, onFailure());
  EXPECT_CALL(*connect_or_op_timer_, disableTimer());
  EXPECT_CALL(connection_callbacks, onEvent(Network::ConnectionEvent::RemoteClose));
  upstream_connection_->raiseEvent(Network::ConnectionEvent::RemoteClose);
}

// Connection close during AwaitingReadonly: the READONLY PendingRequest fails via
// ReadOnlyInitCallbacks::onFailure → onInitFailure. Distinct from the error-reply path
// exercised by Resp3ReadonlyErrorReplyFailsInit above.
TEST_F(RedisClientImplTest, Resp3ConnectionCloseDuringAwaitingReadonlyFailsInit) {
  InSequence s;
  enableResp3();
  setup(std::make_shared<ConfigImpl>(
      createConnPoolSettings(20, true, true, 100,
                             envoy::extensions::filters::network::redis_proxy::v3::RedisProxy::
                                 ConnPoolSettings::REPLICA)));

  EXPECT_CALL(*encoder_, encode(Eq(makeBareHello3Request()), _));
  EXPECT_CALL(*flush_timer_, enabled()).WillOnce(Return(false));
  client_->initialize("", "");

  Common::Redis::RespValue user_request;
  MockClientCallbacks user_callbacks;
  client_->makeRequest(user_request, user_callbacks);

  onConnected();

  EXPECT_CALL(*encoder_, encode(Eq(Utility::ReadOnlyRequest::instance()), _));
  EXPECT_CALL(*connect_or_op_timer_, enableTimer(_, _));
  EXPECT_CALL(*flush_timer_, enabled()).WillOnce(Return(false));
  EXPECT_CALL(*connect_or_op_timer_, enableTimer(_, _));
  EXPECT_CALL(host_->outlier_detector_,
              putResult(Upstream::Outlier::Result::ExtOriginRequestSuccess, _));
  respondWith(makeHelloMapReply(3));

  // Upstream RemoteClose while READONLY is still in flight. Held user request gets onFailure
  // via failHeldUserRequests; ReadOnlyInitCallbacks::onFailure routes through onInitFailure.
  EXPECT_CALL(user_callbacks, onFailure());
  EXPECT_CALL(host_->outlier_detector_,
              putResult(Upstream::Outlier::Result::LocalOriginConnectFailed, _));
  EXPECT_CALL(*connect_or_op_timer_, disableTimer());
  upstream_connection_->raiseEvent(Network::ConnectionEvent::RemoteClose);
  EXPECT_EQ(0UL, hello3_failure_counter_->value());
}

TEST(RedisClientFactoryImplTest, Basic) {
  ClientFactoryImpl factory;
  Upstream::MockHost::MockCreateConnectionData conn_info;
  conn_info.connection_ = new NiceMock<Network::MockClientConnection>();
  std::shared_ptr<Upstream::MockHost> host(new NiceMock<Upstream::MockHost>());
  EXPECT_CALL(*host, createConnection_(_, _)).WillOnce(Return(conn_info));
  NiceMock<Event::MockDispatcher> dispatcher;
  auto config = std::make_shared<ConfigImpl>(createConnPoolSettings());
  Stats::IsolatedStoreImpl stats_;
  auto redis_command_stats =
      Common::Redis::RedisCommandStats::createRedisCommandStats(stats_.symbolTable());
  const std::string auth_username;
  const std::string auth_password;
  ClientPtr client =
      factory.create(host, dispatcher, config, redis_command_stats, *stats_.rootScope(),
                     auth_username, auth_password, false, absl::nullopt, absl::nullopt,
                     envoy::extensions::filters::network::redis_proxy::v3::RedisProtocolOptions::
                         UpstreamProtocol::UNSPECIFIED);
  client->close();
}
} // namespace Client
} // namespace Redis
} // namespace Common
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
