#include <memory>
#include <string>

#include "envoy/extensions/filters/network/redis_proxy/v3/redis_proxy.pb.h"

#include "source/extensions/filters/network/redis_proxy/proxy_filter.h"

#include "test/common/stats/stat_test_utility.h"
#include "test/extensions/common/dynamic_forward_proxy/mocks.h"
#include "test/extensions/filters/network/common/redis/mocks.h"
#include "test/extensions/filters/network/redis_proxy/mocks.h"
#include "test/mocks/api/mocks.h"
#include "test/mocks/common.h"
#include "test/mocks/network/mocks.h"
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
                           *this);
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
                           *this);
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
                           *this);
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
                           *this);
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
                           *this);
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
                           *this);
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
                           *this);
  EXPECT_EQ(config.downstream_auth_username_, "someusername");
  EXPECT_EQ(config.downstream_auth_passwords_.size(), 2);
  EXPECT_EQ(config.downstream_auth_passwords_[0], "newpassword1");
  EXPECT_EQ(config.downstream_auth_passwords_[1], "newpassword2");
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
    config_ = std::make_shared<ProxyFilterConfig>(proto_config, *store_.rootScope(),
                                                  drain_decision_, runtime_, api_, *this);
    filter_ = std::make_unique<ProxyFilter>(*this, Common::Redis::EncoderPtr{encoder_}, splitter_,
                                            config_);
    filter_->initializeReadFilterCallbacks(filter_callbacks_);
    EXPECT_EQ(Network::FilterStatus::Continue, filter_->onNewConnection());
    EXPECT_EQ(1UL, config_->stats_.downstream_cx_total_.value());
    EXPECT_EQ(1UL, config_->stats_.downstream_cx_active_.value());

    // NOP currently.
    filter_->onAboveWriteBufferHighWatermark();
    filter_->onBelowWriteBufferLowWatermark();
  }

  RedisProxyFilterTest() : RedisProxyFilterTest(DefaultConfig) {}

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

  Common::Redis::MockEncoder* encoder_{new Common::Redis::MockEncoder()};
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
  EXPECT_CALL(drain_decision_, drainClose()).WillOnce(Return(true));
  EXPECT_CALL(runtime_.snapshot_, featureEnabled("redis.drain_close_enabled", 100))
      .WillOnce(Return(true));
  EXPECT_CALL(filter_callbacks_.connection_, close(Network::ConnectionCloseType::FlushWrite));
  request_callbacks1_->onResponse(std::move(response1));

  EXPECT_EQ(1UL, config_->stats_.downstream_cx_drain_close_.value());
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

} // namespace RedisProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
