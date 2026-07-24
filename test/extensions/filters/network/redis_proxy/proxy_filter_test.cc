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

    // NOP currently.
    filter_->onAboveWriteBufferHighWatermark();
    filter_->onBelowWriteBufferLowWatermark();
  }

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

} // namespace RedisProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
