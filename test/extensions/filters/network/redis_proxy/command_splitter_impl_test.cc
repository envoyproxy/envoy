#include <cstdint>
#include <list>
#include <string>
#include <utility>
#include <vector>

#include "source/common/common/fmt.h"
#include "source/common/stats/isolated_store_impl.h"
#include "source/extensions/filters/network/common/redis/fault_impl.h"
#include "source/extensions/filters/network/common/redis/supported_commands.h"
#include "source/extensions/filters/network/redis_proxy/command_splitter_impl.h"

#include "test/extensions/filters/network/common/redis/mocks.h"
#include "test/extensions/filters/network/redis_proxy/mocks.h"
#include "test/mocks/common.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/stats/mocks.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/test_common/simulated_time_system.h"

using testing::_;
using testing::DoAll;
using testing::InSequence;
using testing::NiceMock;
using testing::Property;
using testing::Return;
using testing::WithArg;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace RedisProxy {
namespace CommandSplitter {

class RedisCommandSplitterImplTest : public testing::Test {
public:
  RedisCommandSplitterImplTest() : RedisCommandSplitterImplTest(false) {}
  RedisCommandSplitterImplTest(bool latency_in_macro)
      : RedisCommandSplitterImplTest(latency_in_macro, nullptr) {}
  RedisCommandSplitterImplTest(bool latency_in_macro, Common::Redis::FaultSharedPtr fault_ptr)
      : latency_in_micros_(latency_in_macro) {
    ON_CALL(*getFaultManager(), getFaultForCommand(_)).WillByDefault(Return(fault_ptr.get()));
  }
  void makeBulkStringArray(Common::Redis::RespValue& value,
                           const std::vector<std::string>& strings) {
    std::vector<Common::Redis::RespValue> values(strings.size());
    for (uint64_t i = 0; i < strings.size(); i++) {
      values[i].type(Common::Redis::RespType::BulkString);
      values[i].asString() = strings[i];
    }

    value.type(Common::Redis::RespType::Array);
    value.asArray().swap(values);
  }

  void setupMirrorPolicy() {
    auto mirror_policy = std::make_shared<NiceMock<MockMirrorPolicy>>(mirror_conn_pool_shared_ptr_);
    route_->policies_.push_back(mirror_policy);
  }

  MockFaultManager* getFaultManager() {
    auto fault_manager_ptr = splitter_.fault_manager_.get();
    return static_cast<MockFaultManager*>(fault_manager_ptr);
  }

  InstanceImpl getSplitter(absl::flat_hash_set<std::string>&& custom_commands) {
    return InstanceImpl{std::make_unique<NiceMock<MockRouter>>(route_),
                        *store_.rootScope(),
                        "redis.foo.",
                        time_system_,
                        latency_in_micros_,
                        std::make_unique<NiceMock<MockFaultManager>>(fault_manager_),
                        std::move(custom_commands)};
  }

  const bool latency_in_micros_;
  ConnPool::MockInstance* conn_pool_{new ConnPool::MockInstance()};
  ConnPool::MockInstance* mirror_conn_pool_{new ConnPool::MockInstance()};
  ConnPool::InstanceSharedPtr mirror_conn_pool_shared_ptr_{mirror_conn_pool_};
  std::shared_ptr<NiceMock<MockRoute>> route_{
      new NiceMock<MockRoute>(ConnPool::InstanceSharedPtr{conn_pool_})};
  NiceMock<Stats::MockIsolatedStatsStore> store_;
  NiceMock<StreamInfo::MockStreamInfo> stream_info_;
  NiceMock<Event::MockDispatcher> dispatcher_;
  NiceMock<MockFaultManager> fault_manager_;

  Event::SimulatedTimeSystem time_system_;
  absl::flat_hash_set<std::string> custom_commands_;
  InstanceImpl splitter_{std::make_unique<NiceMock<MockRouter>>(route_),
                         *store_.rootScope(),
                         "redis.foo.",
                         time_system_,
                         latency_in_micros_,
                         std::make_unique<NiceMock<MockFaultManager>>(fault_manager_),
                         std::move(custom_commands_)};
  MockSplitCallbacks callbacks_;
  SplitRequestPtr handle_;
};

TEST_F(RedisCommandSplitterImplTest, QuitSuccess) {
  Common::Redis::RespValuePtr request{new Common::Redis::RespValue()};
  makeBulkStringArray(*request, {"quit"});

  EXPECT_EQ(0UL, store_.counter("redis.foo.splitter.invalid_request").value());
}

TEST_F(RedisCommandSplitterImplTest, AuthWithUser) {
  EXPECT_CALL(callbacks_, onAuth("user", "password"));
  Common::Redis::RespValuePtr request{new Common::Redis::RespValue()};
  makeBulkStringArray(*request, {"auth", "user", "password"});
  EXPECT_EQ(nullptr,
            splitter_.makeRequest(std::move(request), callbacks_, dispatcher_, stream_info_));

  EXPECT_EQ(0UL, store_.counter("redis.foo.splitter.invalid_request").value());
}

TEST_F(RedisCommandSplitterImplTest, AuthWithNoPassword) {
  Common::Redis::RespValue response;
  response.type(Common::Redis::RespType::Error);
  response.asString() = Response::get().InvalidRequest;
  EXPECT_CALL(callbacks_, onResponse_(PointeesEq(&response)));
  Common::Redis::RespValuePtr request{new Common::Redis::RespValue()};
  makeBulkStringArray(*request, {"auth"});
  EXPECT_EQ(nullptr,
            splitter_.makeRequest(std::move(request), callbacks_, dispatcher_, stream_info_));

  EXPECT_EQ(1UL, store_.counter("redis.foo.splitter.invalid_request").value());
}

TEST_F(RedisCommandSplitterImplTest, CommandWhenAuthStillNeeded) {
  Common::Redis::RespValue response;
  response.type(Common::Redis::RespType::Error);
  response.asString() = "NOAUTH Authentication required.";
  EXPECT_CALL(callbacks_, connectionAllowed()).WillOnce(Return(false));
  EXPECT_CALL(callbacks_, onResponse_(PointeesEq(&response)));
  Common::Redis::RespValuePtr request{new Common::Redis::RespValue()};
  makeBulkStringArray(*request, {"get", "foo"});
  EXPECT_EQ(nullptr,
            splitter_.makeRequest(std::move(request), callbacks_, dispatcher_, stream_info_));
}

TEST_F(RedisCommandSplitterImplTest, InvalidRequestNotArray) {
  Common::Redis::RespValue response;
  response.type(Common::Redis::RespType::Error);
  response.asString() = Response::get().InvalidRequest;
  EXPECT_CALL(callbacks_, onResponse_(PointeesEq(&response)));
  Common::Redis::RespValuePtr request{new Common::Redis::RespValue()};
  EXPECT_EQ(nullptr,
            splitter_.makeRequest(std::move(request), callbacks_, dispatcher_, stream_info_));

  EXPECT_EQ(1UL, store_.counter("redis.foo.splitter.invalid_request").value());
}

TEST_F(RedisCommandSplitterImplTest, InvalidRequestEmptyArray) {
  Common::Redis::RespValue response;
  response.type(Common::Redis::RespType::Error);
  response.asString() = Response::get().InvalidRequest;
  EXPECT_CALL(callbacks_, onResponse_(PointeesEq(&response)));
  Common::Redis::RespValuePtr request{new Common::Redis::RespValue()};
  request->type(Common::Redis::RespType::Array);
  EXPECT_EQ(nullptr,
            splitter_.makeRequest(std::move(request), callbacks_, dispatcher_, stream_info_));

  EXPECT_EQ(1UL, store_.counter("redis.foo.splitter.invalid_request").value());
}

TEST_F(RedisCommandSplitterImplTest, InvalidRequestArrayTooSmall) {
  Common::Redis::RespValue response;
  response.type(Common::Redis::RespType::Error);
  response.asString() = Response::get().InvalidRequest;
  EXPECT_CALL(callbacks_, connectionAllowed()).WillOnce(Return(true));
  EXPECT_CALL(callbacks_, onResponse_(PointeesEq(&response)));
  Common::Redis::RespValuePtr request{new Common::Redis::RespValue()};
  makeBulkStringArray(*request, {"incr"});
  EXPECT_EQ(nullptr,
            splitter_.makeRequest(std::move(request), callbacks_, dispatcher_, stream_info_));

  EXPECT_EQ(1UL, store_.counter("redis.foo.splitter.invalid_request").value());
}

TEST_F(RedisCommandSplitterImplTest, InvalidRequestArrayNotStrings) {
  Common::Redis::RespValue response;
  response.type(Common::Redis::RespType::Error);
  response.asString() = Response::get().InvalidRequest;
  EXPECT_CALL(callbacks_, onResponse_(PointeesEq(&response)));
  Common::Redis::RespValuePtr request{new Common::Redis::RespValue()};
  makeBulkStringArray(*request, {"incr", ""});
  request->asArray()[1].type(Common::Redis::RespType::Null);
  EXPECT_EQ(nullptr,
            splitter_.makeRequest(std::move(request), callbacks_, dispatcher_, stream_info_));

  EXPECT_EQ(1UL, store_.counter("redis.foo.splitter.invalid_request").value());
}

TEST_F(RedisCommandSplitterImplTest, UnsupportedCommand) {
  Common::Redis::RespValue response;
  response.type(Common::Redis::RespType::Error);
  response.asString() = "ERR unknown command 'newcommand', with args beginning with: hello";
  EXPECT_CALL(callbacks_, onResponse_(PointeesEq(&response)));
  Common::Redis::RespValuePtr request{new Common::Redis::RespValue()};
  makeBulkStringArray(*request, {"newcommand", "hello"});
  EXPECT_EQ(nullptr,
            splitter_.makeRequest(std::move(request), callbacks_, dispatcher_, stream_info_));

  EXPECT_EQ(1UL, store_.counter("redis.foo.splitter.unsupported_command").value());
}

MATCHER_P(RespVariantEq, rhs, "RespVariant should be equal") {
  const ConnPool::RespVariant& obj = arg;
  EXPECT_EQ(obj.index(), 1);
  EXPECT_EQ(*(absl::get<Common::Redis::RespValueConstSharedPtr>(obj)), rhs);
  return true;
}

MATCHER_P(RespValueVariantEq, rhs, "RespVariant with RespValue should be equal") {
  const ConnPool::RespVariant& obj = arg;
  EXPECT_EQ(obj.index(), 0);
  EXPECT_EQ(absl::get<const Common::Redis::RespValue>(obj), rhs);
  return true;
}

class RedisSingleServerRequestTest : public RedisCommandSplitterImplTest,
                                     public testing::WithParamInterface<std::string> {
public:
  RedisSingleServerRequestTest() : RedisSingleServerRequestTest(false) {}
  RedisSingleServerRequestTest(bool latency_in_micros)
      : RedisCommandSplitterImplTest(latency_in_micros) {}
  void makeRequest(const std::string& hash_key, Common::Redis::RespValuePtr&& request,
                   bool mirrored = false) {
    EXPECT_CALL(callbacks_, connectionAllowed()).WillOnce(Return(true));
    EXPECT_CALL(*conn_pool_, makeRequest_(hash_key, RespVariantEq(*request), _))
        .WillOnce(DoAll(WithArg<2>(SaveArgAddress(&pool_callbacks_)), Return(&pool_request_)));
    if (mirrored) {
      EXPECT_CALL(*mirror_conn_pool_, makeRequest_(hash_key, RespVariantEq(*request), _))
          .WillOnce(DoAll(WithArg<2>(SaveArgAddress(&mirror_pool_callbacks_)),
                          Return(&mirror_pool_request_)));
    }
    handle_ = splitter_.makeRequest(std::move(request), callbacks_, dispatcher_, stream_info_);
  }

  void fail() {
    Common::Redis::RespValue response;
    response.type(Common::Redis::RespType::Error);
    response.asString() = Response::get().UpstreamFailure;
    EXPECT_CALL(callbacks_, onResponse_(PointeesEq(&response)));
    pool_callbacks_->onFailure();
  }

  void respond(bool mirrored = false) {
    Common::Redis::RespValuePtr response1(new Common::Redis::RespValue());
    Common::Redis::RespValue* response1_ptr = response1.get();
    if (mirrored) {
      // expect no-opt for mirrored requests
      mirror_pool_callbacks_->onResponse(std::move(response1));
    } else {
      EXPECT_CALL(callbacks_, onResponse_(PointeesEq(response1_ptr)));
      pool_callbacks_->onResponse(std::move(response1));
    }
  }

  ConnPool::PoolCallbacks* pool_callbacks_;
  Common::Redis::Client::MockPoolRequest pool_request_;

  ConnPool::PoolCallbacks* mirror_pool_callbacks_;
  Common::Redis::Client::MockPoolRequest mirror_pool_request_;
};

TEST_P(RedisSingleServerRequestTest, Success) {
  InSequence s;

  std::string lower_command = absl::AsciiStrToLower(GetParam());

  Common::Redis::RespValuePtr request{new Common::Redis::RespValue()};
  makeBulkStringArray(*request, {GetParam(), "hello"});
  makeRequest("hello", std::move(request));
  EXPECT_NE(nullptr, handle_);

  time_system_.setMonotonicTime(std::chrono::milliseconds(10));
  EXPECT_CALL(store_, deliverHistogramToSinks(
                          Property(&Stats::Metric::name,
                                   fmt::format("redis.foo.command.{}.latency", lower_command)),
                          10));
  respond();

  EXPECT_EQ(1UL, store_.counter(fmt::format("redis.foo.command.{}.total", lower_command)).value());
  EXPECT_EQ(1UL,
            store_.counter(fmt::format("redis.foo.command.{}.success", lower_command)).value());
};

TEST_P(RedisSingleServerRequestTest, Mirrored) {
  InSequence s;

  setupMirrorPolicy();

  std::string lower_command = absl::AsciiStrToLower(GetParam());

  Common::Redis::RespValuePtr request{new Common::Redis::RespValue()};
  makeBulkStringArray(*request, {GetParam(), "hello"});
  makeRequest("hello", std::move(request), true);
  EXPECT_NE(nullptr, handle_);

  time_system_.setMonotonicTime(std::chrono::milliseconds(10));
  EXPECT_CALL(store_, deliverHistogramToSinks(
                          Property(&Stats::Metric::name,
                                   fmt::format("redis.foo.command.{}.latency", lower_command)),
                          10));
  respond();
  respond(true);

  EXPECT_EQ(1UL, store_.counter(fmt::format("redis.foo.command.{}.total", lower_command)).value());
  EXPECT_EQ(1UL,
            store_.counter(fmt::format("redis.foo.command.{}.success", lower_command)).value());
};

TEST_P(RedisSingleServerRequestTest, MirroredFailed) {
  InSequence s;

  setupMirrorPolicy();

  std::string lower_command = absl::AsciiStrToLower(GetParam());

  Common::Redis::RespValuePtr request{new Common::Redis::RespValue()};
  makeBulkStringArray(*request, {GetParam(), "hello"});
  makeRequest("hello", std::move(request), true);
  EXPECT_NE(nullptr, handle_);

  time_system_.setMonotonicTime(std::chrono::milliseconds(10));
  EXPECT_CALL(store_, deliverHistogramToSinks(
                          Property(&Stats::Metric::name,
                                   fmt::format("redis.foo.command.{}.latency", lower_command)),
                          10));
  // Mirrored request failure should not result in main path failure
  mirror_pool_callbacks_->onFailure();
  respond();

  EXPECT_EQ(1UL, store_.counter(fmt::format("redis.foo.command.{}.total", lower_command)).value());
  EXPECT_EQ(1UL,
            store_.counter(fmt::format("redis.foo.command.{}.success", lower_command)).value());
};

TEST_P(RedisSingleServerRequestTest, SuccessMultipleArgs) {
  InSequence s;

  Common::Redis::RespValuePtr request{new Common::Redis::RespValue()};
  makeBulkStringArray(*request, {GetParam(), "hello", "123", "world"});
  makeRequest("hello", std::move(request));
  EXPECT_NE(nullptr, handle_);

  std::string lower_command = absl::AsciiStrToLower(GetParam());

  time_system_.setMonotonicTime(std::chrono::milliseconds(10));
  EXPECT_CALL(store_, deliverHistogramToSinks(
                          Property(&Stats::Metric::name,
                                   fmt::format("redis.foo.command.{}.latency", lower_command)),
                          10));
  respond();

  EXPECT_EQ(1UL, store_.counter(fmt::format("redis.foo.command.{}.total", lower_command)).value());
  EXPECT_EQ(1UL,
            store_.counter(fmt::format("redis.foo.command.{}.success", lower_command)).value());
};

TEST_P(RedisSingleServerRequestTest, Fail) {
  InSequence s;

  Common::Redis::RespValuePtr request{new Common::Redis::RespValue()};
  makeBulkStringArray(*request, {GetParam(), "hello"});
  makeRequest("hello", std::move(request));
  EXPECT_NE(nullptr, handle_);

  std::string lower_command = absl::AsciiStrToLower(GetParam());

  time_system_.setMonotonicTime(std::chrono::milliseconds(5));
  EXPECT_CALL(store_, deliverHistogramToSinks(
                          Property(&Stats::Metric::name,
                                   fmt::format("redis.foo.command.{}.latency", lower_command)),
                          5));
  fail();

  EXPECT_EQ(1UL, store_.counter(fmt::format("redis.foo.command.{}.total", lower_command)).value());
  EXPECT_EQ(1UL, store_.counter(fmt::format("redis.foo.command.{}.error", lower_command)).value());
};

TEST_P(RedisSingleServerRequestTest, Cancel) {
  InSequence s;

  Common::Redis::RespValuePtr request{new Common::Redis::RespValue()};
  makeBulkStringArray(*request, {GetParam(), "hello"});
  makeRequest("hello", std::move(request));
  EXPECT_NE(nullptr, handle_);

  EXPECT_CALL(pool_request_, cancel());
  handle_->cancel();
};

TEST_P(RedisSingleServerRequestTest, NoUpstream) {
  InSequence s;

  EXPECT_CALL(callbacks_, connectionAllowed()).WillOnce(Return(true));
  Common::Redis::RespValuePtr request{new Common::Redis::RespValue()};
  makeBulkStringArray(*request, {GetParam(), "hello"});
  EXPECT_CALL(*conn_pool_, makeRequest_("hello", RespVariantEq(*request), _))
      .WillOnce(Return(nullptr));

  Common::Redis::RespValue response;
  response.type(Common::Redis::RespType::Error);
  response.asString() = Response::get().NoUpstreamHost;
  EXPECT_CALL(callbacks_, onResponse_(PointeesEq(&response)));
  handle_ = splitter_.makeRequest(std::move(request), callbacks_, dispatcher_, stream_info_);
  EXPECT_EQ(nullptr, handle_);
  std::string lower_command = absl::AsciiStrToLower(GetParam());
  EXPECT_EQ(1UL, store_.counter("redis.foo.command." + lower_command + ".total").value());
  EXPECT_EQ(1UL, store_.counter("redis.foo.command." + lower_command + ".error").value());
};

INSTANTIATE_TEST_SUITE_P(RedisSingleServerRequestTest, RedisSingleServerRequestTest,
                         testing::Values("get", "set", "incr", "zadd", "bitfield_ro"));

INSTANTIATE_TEST_SUITE_P(RedisSimpleRequestCommandHandlerMixedCaseTests,
                         RedisSingleServerRequestTest, testing::Values("INCR", "inCrBY"));

TEST_F(RedisSingleServerRequestTest, PingSuccess) {
  InSequence s;

  Common::Redis::RespValuePtr request{new Common::Redis::RespValue()};
  makeBulkStringArray(*request, {"ping"});

  Common::Redis::RespValue response;
  response.type(Common::Redis::RespType::SimpleString);
  response.asString() = "PONG";

  EXPECT_CALL(callbacks_, connectionAllowed()).WillOnce(Return(true));
  EXPECT_CALL(callbacks_, onResponse_(PointeesEq(&response)));
  handle_ = splitter_.makeRequest(std::move(request), callbacks_, dispatcher_, stream_info_);
  EXPECT_EQ(nullptr, handle_);
};

TEST_F(RedisSingleServerRequestTest, EchoSuccess) {
  InSequence s;

  Common::Redis::RespValuePtr request{new Common::Redis::RespValue()};
  makeBulkStringArray(*request, {"echo", "foobar"});

  Common::Redis::RespValue response;
  response.type(Common::Redis::RespType::BulkString);
  response.asString() = "foobar";

  EXPECT_CALL(callbacks_, connectionAllowed()).WillOnce(Return(true));
  EXPECT_CALL(callbacks_, onResponse_(PointeesEq(&response)));
  handle_ = splitter_.makeRequest(std::move(request), callbacks_, dispatcher_, stream_info_);
  EXPECT_EQ(nullptr, handle_);
};

TEST_F(RedisSingleServerRequestTest, EchoInvalid) {
  InSequence s;

  Common::Redis::RespValuePtr request{new Common::Redis::RespValue()};
  makeBulkStringArray(*request, {"echo", "hello", "world"});

  Common::Redis::RespValue response;
  response.type(Common::Redis::RespType::Error);
  response.asString() = RedisProxy::CommandSplitter::Response::get().InvalidRequest;

  EXPECT_CALL(callbacks_, connectionAllowed()).WillOnce(Return(true));
  EXPECT_CALL(callbacks_, onResponse_(PointeesEq(&response)));
  handle_ = splitter_.makeRequest(std::move(request), callbacks_, dispatcher_, stream_info_);
  EXPECT_EQ(nullptr, handle_);
};

TEST_F(RedisSingleServerRequestTest, Time) {
  InSequence s;

  Common::Redis::RespValuePtr request{new Common::Redis::RespValue()};
  makeBulkStringArray(*request, {"time"});

  auto now = dispatcher_.timeSource().systemTime().time_since_epoch();
  auto secs = std::to_string(std::chrono::duration_cast<std::chrono::seconds>(now).count());
  auto msecs = std::to_string(std::chrono::duration_cast<std::chrono::microseconds>(now).count());

  Common::Redis::RespValue response;
  response.type(Common::Redis::RespType::Array);
  std::vector<Common::Redis::RespValue> elements(2);
  elements[0].type(Common::Redis::RespType::BulkString);
  elements[0].asString() = secs;
  elements[1].type(Common::Redis::RespType::BulkString);
  elements[1].asString() = msecs;
  response.asArray().swap(elements);

  EXPECT_CALL(callbacks_, connectionAllowed()).WillOnce(Return(true));
  EXPECT_CALL(callbacks_, onResponse_(PointeesEq(&response)));
  handle_ = splitter_.makeRequest(std::move(request), callbacks_, dispatcher_, stream_info_);
  EXPECT_EQ(nullptr, handle_);
}

TEST_F(RedisSingleServerRequestTest, EvalSuccess) {
  InSequence s;

  Common::Redis::RespValuePtr request{new Common::Redis::RespValue()};
  makeBulkStringArray(*request, {"eval", "return {ARGV[1]}", "1", "key", "arg"});
  makeRequest("key", std::move(request));
  EXPECT_NE(nullptr, handle_);

  std::string lower_command = absl::AsciiStrToLower("eval");

  time_system_.setMonotonicTime(std::chrono::milliseconds(10));
  EXPECT_CALL(store_, deliverHistogramToSinks(
                          Property(&Stats::Metric::name,
                                   fmt::format("redis.foo.command.{}.latency", lower_command)),
                          10));
  respond();

  EXPECT_EQ(1UL, store_.counter(fmt::format("redis.foo.command.{}.total", lower_command)).value());
  EXPECT_EQ(1UL,
            store_.counter(fmt::format("redis.foo.command.{}.success", lower_command)).value());
};

TEST_F(RedisSingleServerRequestTest, EvalShaSuccess) {
  InSequence s;

  Common::Redis::RespValuePtr request{new Common::Redis::RespValue()};
  makeBulkStringArray(*request, {"EVALSHA", "return {ARGV[1]}", "1", "keykey", "arg"});
  makeRequest("keykey", std::move(request));
  EXPECT_NE(nullptr, handle_);

  std::string lower_command = absl::AsciiStrToLower("evalsha");

  time_system_.setMonotonicTime(std::chrono::milliseconds(10));
  EXPECT_CALL(store_, deliverHistogramToSinks(
                          Property(&Stats::Metric::name,
                                   fmt::format("redis.foo.command.{}.latency", lower_command)),
                          10));
  respond();

  EXPECT_EQ(1UL, store_.counter(fmt::format("redis.foo.command.{}.total", lower_command)).value());
  EXPECT_EQ(1UL,
            store_.counter(fmt::format("redis.foo.command.{}.success", lower_command)).value());
};

TEST_F(RedisSingleServerRequestTest, EvalWrongNumberOfArgs) {
  InSequence s;

  Common::Redis::RespValuePtr request1{new Common::Redis::RespValue()};
  Common::Redis::RespValuePtr request2{new Common::Redis::RespValue()};
  Common::Redis::RespValue response;
  response.type(Common::Redis::RespType::Error);

  response.asString() = "wrong number of arguments for 'eval' command";
  EXPECT_CALL(callbacks_, connectionAllowed()).WillOnce(Return(true));
  EXPECT_CALL(callbacks_, onResponse_(PointeesEq(&response)));
  makeBulkStringArray(*request1, {"eval", "return {ARGV[1]}"});
  EXPECT_EQ(nullptr,
            splitter_.makeRequest(std::move(request1), callbacks_, dispatcher_, stream_info_));

  response.asString() = "wrong number of arguments for 'evalsha' command";
  EXPECT_CALL(callbacks_, connectionAllowed()).WillOnce(Return(true));
  EXPECT_CALL(callbacks_, onResponse_(PointeesEq(&response)));
  makeBulkStringArray(*request2, {"evalsha", "return {ARGV[1]}", "1"});
  EXPECT_EQ(nullptr,
            splitter_.makeRequest(std::move(request2), callbacks_, dispatcher_, stream_info_));
};

TEST_F(RedisSingleServerRequestTest, EvalNoUpstream) {
  InSequence s;

  EXPECT_CALL(callbacks_, connectionAllowed()).WillOnce(Return(true));
  Common::Redis::RespValuePtr request{new Common::Redis::RespValue()};
  makeBulkStringArray(*request, {"eval", "return {ARGV[1]}", "1", "key", "arg"});
  EXPECT_CALL(*conn_pool_, makeRequest_("key", RespVariantEq(*request), _))
      .WillOnce(Return(nullptr));

  Common::Redis::RespValue response;
  response.type(Common::Redis::RespType::Error);
  response.asString() = Response::get().NoUpstreamHost;
  EXPECT_CALL(callbacks_, onResponse_(PointeesEq(&response)));
  handle_ = splitter_.makeRequest(std::move(request), callbacks_, dispatcher_, stream_info_);
  EXPECT_EQ(nullptr, handle_);

  EXPECT_EQ(1UL, store_.counter("redis.foo.command.eval.total").value());
  EXPECT_EQ(1UL, store_.counter("redis.foo.command.eval.error").value());
};

// OBJECT command tests - hashes on the third argument (index 2)
TEST_F(RedisSingleServerRequestTest, ObjectEncodingSuccess) {
  InSequence s;

  Common::Redis::RespValuePtr request{new Common::Redis::RespValue()};
  // OBJECT ENCODING key -> [0]=OBJECT, [1]=ENCODING, [2]=key
  makeBulkStringArray(*request, {"object", "encoding", "mykey"});
  makeRequest("mykey", std::move(request));
  EXPECT_NE(nullptr, handle_);

  std::string lower_command = absl::AsciiStrToLower("object");

  time_system_.setMonotonicTime(std::chrono::milliseconds(10));
  EXPECT_CALL(store_, deliverHistogramToSinks(
                          Property(&Stats::Metric::name,
                                   fmt::format("redis.foo.command.{}.latency", lower_command)),
                          10));
  respond();

  EXPECT_EQ(1UL, store_.counter(fmt::format("redis.foo.command.{}.total", lower_command)).value());
  EXPECT_EQ(1UL,
            store_.counter(fmt::format("redis.foo.command.{}.success", lower_command)).value());
};

TEST_F(RedisSingleServerRequestTest, ObjectRefcountSuccess) {
  InSequence s;

  Common::Redis::RespValuePtr request{new Common::Redis::RespValue()};
  // OBJECT REFCOUNT key -> [0]=OBJECT, [1]=REFCOUNT, [2]=key
  makeBulkStringArray(*request, {"OBJECT", "REFCOUNT", "testkey"});
  makeRequest("testkey", std::move(request));
  EXPECT_NE(nullptr, handle_);

  std::string lower_command = absl::AsciiStrToLower("object");

  time_system_.setMonotonicTime(std::chrono::milliseconds(10));
  EXPECT_CALL(store_, deliverHistogramToSinks(
                          Property(&Stats::Metric::name,
                                   fmt::format("redis.foo.command.{}.latency", lower_command)),
                          10));
  respond();

  EXPECT_EQ(1UL, store_.counter(fmt::format("redis.foo.command.{}.total", lower_command)).value());
  EXPECT_EQ(1UL,
            store_.counter(fmt::format("redis.foo.command.{}.success", lower_command)).value());
};

TEST_F(RedisSingleServerRequestTest, ObjectWrongNumberOfArgs) {
  InSequence s;

  Common::Redis::RespValuePtr request1{new Common::Redis::RespValue()};
  Common::Redis::RespValuePtr request2{new Common::Redis::RespValue()};
  Common::Redis::RespValue response;
  response.type(Common::Redis::RespType::Error);

  // Missing key argument: OBJECT ENCODING (no key)
  response.asString() = "wrong number of arguments for 'object' command";
  EXPECT_CALL(callbacks_, connectionAllowed()).WillOnce(Return(true));
  EXPECT_CALL(callbacks_, onResponse_(PointeesEq(&response)));
  makeBulkStringArray(*request1, {"object", "encoding"});
  EXPECT_EQ(nullptr,
            splitter_.makeRequest(std::move(request1), callbacks_, dispatcher_, stream_info_));

  // Only command name: OBJECT (no subcommand, no key) - returns "invalid request"
  Common::Redis::RespValue response2;
  response2.type(Common::Redis::RespType::Error);
  response2.asString() = "invalid request";
  EXPECT_CALL(callbacks_, connectionAllowed()).WillOnce(Return(true));
  EXPECT_CALL(callbacks_, onResponse_(PointeesEq(&response2)));
  makeBulkStringArray(*request2, {"object"});
  EXPECT_EQ(nullptr,
            splitter_.makeRequest(std::move(request2), callbacks_, dispatcher_, stream_info_));
};

TEST_F(RedisSingleServerRequestTest, ObjectNoUpstream) {
  InSequence s;

  EXPECT_CALL(callbacks_, connectionAllowed()).WillOnce(Return(true));
  Common::Redis::RespValuePtr request{new Common::Redis::RespValue()};
  makeBulkStringArray(*request, {"object", "encoding", "mykey"});
  EXPECT_CALL(*conn_pool_, makeRequest_("mykey", RespVariantEq(*request), _))
      .WillOnce(Return(nullptr));

  Common::Redis::RespValue response;
  response.type(Common::Redis::RespType::Error);
  response.asString() = Response::get().NoUpstreamHost;
  EXPECT_CALL(callbacks_, onResponse_(PointeesEq(&response)));
  handle_ = splitter_.makeRequest(std::move(request), callbacks_, dispatcher_, stream_info_);
  EXPECT_EQ(nullptr, handle_);

  EXPECT_EQ(1UL, store_.counter("redis.foo.command.object.total").value());
  EXPECT_EQ(1UL, store_.counter("redis.foo.command.object.error").value());
};

// HELLO N AUTH ... routed to attemptDownstreamAuthInline. Allowed: splitter emits HELLO Map
// for the requested protocol version (synchronous local-credentials match path).
TEST_F(RedisSingleServerRequestTest, HelloWithAuthOptionAllowedEmitsHelloMap) {
  InSequence s;
  callbacks_.inline_auth_attempt_ = CommandSplitter::SplitCallbacks::AuthAttempt::Allowed;

  Common::Redis::RespValuePtr request{new Common::Redis::RespValue()};
  makeBulkStringArray(*request, {"hello", "2", "AUTH", "user", "mypass"});

  Common::Redis::RespValuePtr captured;
  EXPECT_CALL(callbacks_, onResponse_(_)).WillOnce(Invoke([&](Common::Redis::RespValuePtr& r) {
    captured = std::move(r);
  }));
  handle_ = splitter_.makeRequest(std::move(request), callbacks_, dispatcher_, stream_info_);
  EXPECT_EQ(nullptr, handle_);
  ASSERT_NE(nullptr, captured);
  EXPECT_EQ(Common::Redis::RespType::Map, captured->type());
  EXPECT_EQ("user", callbacks_.last_inline_auth_username_);
  EXPECT_EQ("mypass", callbacks_.last_inline_auth_password_);
  EXPECT_EQ(2u, callbacks_.last_inline_auth_requested_version_);
}

// Denied: splitter emits WRONGPASS without firing the connection-allowed gate.
TEST_F(RedisSingleServerRequestTest, HelloWithAuthOptionDeniedEmitsWrongpass) {
  InSequence s;
  callbacks_.inline_auth_attempt_ = CommandSplitter::SplitCallbacks::AuthAttempt::Denied;

  Common::Redis::RespValuePtr request{new Common::Redis::RespValue()};
  makeBulkStringArray(*request, {"hello", "3", "AUTH", "user", "wrongpass"});

  Common::Redis::RespValue response;
  response.type(Common::Redis::RespType::Error);
  response.asString() = "WRONGPASS invalid username-password pair";
  EXPECT_CALL(callbacks_, onResponse_(PointeesEq(&response)));
  handle_ = splitter_.makeRequest(std::move(request), callbacks_, dispatcher_, stream_info_);
  EXPECT_EQ(nullptr, handle_);
  EXPECT_EQ(3u, callbacks_.last_inline_auth_requested_version_);
}

// Pending (external auth in flight): splitter emits NO response and yields control. The
// implementation behind attemptDownstreamAuthInline (ProxyFilter in production) is responsible
// for emitting the deferred HELLO Map (success) or error (failure) when the async auth
// completes. Verifies the splitter's no-emit / nullptr-handle contract for the Pending case;
// the deferred-reply emission contract is covered in proxy_filter_test.cc.
TEST_F(RedisSingleServerRequestTest, HelloWithAuthOptionPendingEmitsNothing) {
  InSequence s;
  callbacks_.inline_auth_attempt_ = CommandSplitter::SplitCallbacks::AuthAttempt::Pending;

  Common::Redis::RespValuePtr request{new Common::Redis::RespValue()};
  makeBulkStringArray(*request, {"hello", "3", "AUTH", "alice", "secret"});

  EXPECT_CALL(callbacks_, onResponse_(_)).Times(0);
  handle_ = splitter_.makeRequest(std::move(request), callbacks_, dispatcher_, stream_info_);
  EXPECT_EQ(nullptr, handle_);
  EXPECT_EQ("alice", callbacks_.last_inline_auth_username_);
  EXPECT_EQ("secret", callbacks_.last_inline_auth_password_);
  EXPECT_EQ(3u, callbacks_.last_inline_auth_requested_version_);
}

// HELLO 2 SETNAME ... is now recognized: name is accepted and ignored, the
// HELLO Map reply is emitted.
TEST_F(RedisSingleServerRequestTest, HelloWithSetnameOptionAccepted) {
  InSequence s;

  Common::Redis::RespValuePtr request{new Common::Redis::RespValue()};
  makeBulkStringArray(*request, {"hello", "2", "SETNAME", "myclient"});

  Common::Redis::RespValuePtr captured;
  EXPECT_CALL(callbacks_, connectionAllowed()).WillOnce(Return(true));
  EXPECT_CALL(callbacks_, onResponse_(_)).WillOnce([&captured](Common::Redis::RespValuePtr& v) {
    captured = std::move(v);
  });
  handle_ = splitter_.makeRequest(std::move(request), callbacks_, dispatcher_, stream_info_);
  EXPECT_EQ(nullptr, handle_);
  ASSERT_NE(nullptr, captured);
  EXPECT_EQ(Common::Redis::RespType::Map, captured->type());
}

// HELLO with the AUTH option but missing username and/or password tokens emits a
// ``-ERR Syntax error: HELLO AUTH requires <username> <password>`` reply. The HELLO
// dispatcher returns at the option-parser error site BEFORE the connection-allowed gate,
// so connectionAllowed() is NOT consulted on this path.
TEST_F(RedisSingleServerRequestTest, HelloWithAuthOptionMissingArgsEmitsSyntaxError) {
  InSequence s;

  Common::Redis::RespValuePtr request{new Common::Redis::RespValue()};
  makeBulkStringArray(*request, {"hello", "2", "AUTH", "alice"});

  Common::Redis::RespValue expected_err;
  expected_err.type(Common::Redis::RespType::Error);
  expected_err.asString() = "ERR Syntax error: HELLO AUTH requires <username> <password>";

  EXPECT_CALL(callbacks_, onResponse_(PointeesEq(&expected_err)));
  handle_ = splitter_.makeRequest(std::move(request), callbacks_, dispatcher_, stream_info_);
  EXPECT_EQ(nullptr, handle_);
}

// HELLO with the SETNAME option but no following ``<clientname>`` token emits a
// ``-ERR Syntax error: HELLO SETNAME requires a <clientname>`` reply. Same early-return
// rationale as the AUTH-missing-args case above — connectionAllowed() is not consulted.
TEST_F(RedisSingleServerRequestTest, HelloWithSetnameOptionMissingArgEmitsSyntaxError) {
  InSequence s;

  Common::Redis::RespValuePtr request{new Common::Redis::RespValue()};
  makeBulkStringArray(*request, {"hello", "2", "SETNAME"});

  Common::Redis::RespValue expected_err;
  expected_err.type(Common::Redis::RespType::Error);
  expected_err.asString() = "ERR Syntax error: HELLO SETNAME requires a <clientname>";

  EXPECT_CALL(callbacks_, onResponse_(PointeesEq(&expected_err)));
  handle_ = splitter_.makeRequest(std::move(request), callbacks_, dispatcher_, stream_info_);
  EXPECT_EQ(nullptr, handle_);
}

// HELLO with an unknown option keyword (not AUTH or SETNAME) emits a
// ``-ERR Syntax error: unknown HELLO option`` reply, returning before the auth gate.
TEST_F(RedisSingleServerRequestTest, HelloWithUnknownOptionEmitsSyntaxError) {
  InSequence s;

  Common::Redis::RespValuePtr request{new Common::Redis::RespValue()};
  makeBulkStringArray(*request, {"hello", "2", "UNKNOWN"});

  Common::Redis::RespValue expected_err;
  expected_err.type(Common::Redis::RespType::Error);
  expected_err.asString() = "ERR Syntax error: unknown HELLO option 'UNKNOWN'";

  EXPECT_CALL(callbacks_, onResponse_(PointeesEq(&expected_err)));
  handle_ = splitter_.makeRequest(std::move(request), callbacks_, dispatcher_, stream_info_);
  EXPECT_EQ(nullptr, handle_);
}

// Non-numeric protocol version: -NOPROTO. The protocol-version parse runs before the
// connection-allowed gate, so an invalid protover returns ``NOPROTO`` directly without consulting
// the gate.
TEST_F(RedisSingleServerRequestTest, HelloWithInvalidProtocolVersion) {
  InSequence s;

  Common::Redis::RespValuePtr request{new Common::Redis::RespValue()};
  makeBulkStringArray(*request, {"hello", "abc"}); // Non-numeric protocol version

  Common::Redis::RespValue response;
  response.type(Common::Redis::RespType::Error);
  response.asString() = "NOPROTO unsupported protocol version";

  EXPECT_CALL(callbacks_, onResponse_(PointeesEq(&response)));
  handle_ = splitter_.makeRequest(std::move(request), callbacks_, dispatcher_, stream_info_);
  EXPECT_EQ(nullptr, handle_);
}

TEST_F(RedisSingleServerRequestTest, HelloWithUnsupportedProtocolVersion) {
  InSequence s;

  Common::Redis::RespValuePtr request{new Common::Redis::RespValue()};
  makeBulkStringArray(*request, {"hello", "4"}); // RESP4 not supported

  Common::Redis::RespValue response;
  response.type(Common::Redis::RespType::Error);
  response.asString() = "NOPROTO unsupported protocol version";

  EXPECT_CALL(callbacks_, onResponse_(PointeesEq(&response)));
  handle_ = splitter_.makeRequest(std::move(request), callbacks_, dispatcher_, stream_info_);
  EXPECT_EQ(nullptr, handle_);
}

// =============================================================================
// LOCAL HELLO policy (default) — proxy answers HELLO synchronously from its
// own capability table, without fanning out to upstream. The encoder version
// flips immediately when the client sent an explicit HELLO N.
//
// Covers:
//   - Bare HELLO (preserves current downstream version)
//   - Explicit HELLO 2 / HELLO 3 (flips version)
//   - HELLO ceiling check against max_version
//   - Shape of the reply (Map of 14 entries when stored)
// =============================================================================

TEST_F(RedisSingleServerRequestTest, HelloLocalBarePreservesVersion) {
  InSequence s;
  // Client is currently on RESP2 and sends a bare HELLO: the proxy answers
  // locally, no upstream round-trip, and does not flip the encoder.
  callbacks_.downstream_resp_version_ = 2;
  callbacks_.cluster_resp_version_ = 3; // cap allows RESP3 upgrade

  Common::Redis::RespValuePtr request{new Common::Redis::RespValue()};
  makeBulkStringArray(*request, {"hello"});

  EXPECT_CALL(callbacks_, connectionAllowed()).WillOnce(Return(true));
  // We expect onResponse with a Map reply that contains proto=2 (current
  // version). Rather than build the full expected RespValue, capture the reply
  // and assert on the proto field.
  Common::Redis::RespValuePtr captured;
  EXPECT_CALL(callbacks_, onResponse_(_)).WillOnce([&captured](Common::Redis::RespValuePtr& v) {
    captured = std::move(v);
  });
  handle_ = splitter_.makeRequest(std::move(request), callbacks_, dispatcher_, stream_info_);
  EXPECT_EQ(nullptr, handle_);

  ASSERT_NE(nullptr, captured);
  EXPECT_EQ(Common::Redis::RespType::Map, captured->type());
  // Stored as flat 2N = 14 entries for 7 k/v pairs.
  EXPECT_EQ(14U, captured->asArray().size());
  // Find proto field.
  bool found_proto = false;
  for (size_t i = 0; i + 1 < captured->asArray().size(); i += 2) {
    if (captured->asArray()[i].type() == Common::Redis::RespType::BulkString &&
        captured->asArray()[i].asString() == "proto") {
      EXPECT_EQ(Common::Redis::RespType::Integer, captured->asArray()[i + 1].type());
      EXPECT_EQ(2, captured->asArray()[i + 1].asInteger());
      found_proto = true;
      break;
    }
  }
  EXPECT_TRUE(found_proto);
  // Bare HELLO leaves downstream version unchanged.
  EXPECT_EQ(2U, callbacks_.downstream_resp_version_);
}

TEST_F(RedisSingleServerRequestTest, HelloLocalExplicit3FlipsEncoder) {
  InSequence s;
  callbacks_.downstream_resp_version_ = 2;
  callbacks_.cluster_resp_version_ = 3;

  Common::Redis::RespValuePtr request{new Common::Redis::RespValue()};
  makeBulkStringArray(*request, {"hello", "3"});

  EXPECT_CALL(callbacks_, connectionAllowed()).WillOnce(Return(true));
  Common::Redis::RespValuePtr captured;
  EXPECT_CALL(callbacks_, onResponse_(_)).WillOnce([&captured](Common::Redis::RespValuePtr& v) {
    captured = std::move(v);
  });
  handle_ = splitter_.makeRequest(std::move(request), callbacks_, dispatcher_, stream_info_);
  EXPECT_EQ(nullptr, handle_);

  ASSERT_NE(nullptr, captured);
  // Downstream was flipped to 3 before onResponse fired; the mock's
  // setDownstreamRespVersion recorded it.
  EXPECT_EQ(3U, callbacks_.downstream_resp_version_);

  // Pin the HELLO Map's ``mode`` field at ``standalone`` per the RESP3 spec
  // (https://redis.io/docs/latest/develop/reference/protocol-spec/). The proxy presents one
  // logical Redis endpoint and hides Redis Cluster routing, so any other value (notably the
  // earlier "proxy" placeholder) would be unspec'd and risk strict-client rejection.
  ASSERT_EQ(Common::Redis::RespType::Map, captured->type());
  const auto& kv = captured->asArray();
  bool saw_mode_standalone = false;
  for (size_t i = 0; i + 1 < kv.size(); i += 2) {
    if ((kv[i].type() == Common::Redis::RespType::BulkString ||
         kv[i].type() == Common::Redis::RespType::SimpleString) &&
        kv[i].asString() == "mode") {
      ASSERT_EQ(Common::Redis::RespType::BulkString, kv[i + 1].type());
      EXPECT_EQ("standalone", kv[i + 1].asString());
      saw_mode_standalone = true;
      break;
    }
  }
  EXPECT_TRUE(saw_mode_standalone);
}

TEST_F(RedisSingleServerRequestTest, HelloLocalExplicit2FlipsEncoder) {
  InSequence s;
  callbacks_.downstream_resp_version_ = 3;
  callbacks_.cluster_resp_version_ = 3;

  Common::Redis::RespValuePtr request{new Common::Redis::RespValue()};
  makeBulkStringArray(*request, {"hello", "2"});

  EXPECT_CALL(callbacks_, connectionAllowed()).WillOnce(Return(true));
  EXPECT_CALL(callbacks_, onResponse_(_));
  handle_ = splitter_.makeRequest(std::move(request), callbacks_, dispatcher_, stream_info_);
  EXPECT_EQ(nullptr, handle_);

  EXPECT_EQ(2U, callbacks_.downstream_resp_version_);
}

TEST_F(RedisSingleServerRequestTest, HelloLocalAboveMaxRejected) {
  InSequence s;
  // max_version = 2 means HELLO 3 must be rejected with `-NOPROTO`. The version-cap check runs
  // before the auth gate, so connectionAllowed is not consulted on the rejection path.
  callbacks_.cluster_resp_version_ = 2;

  Common::Redis::RespValuePtr request{new Common::Redis::RespValue()};
  makeBulkStringArray(*request, {"hello", "3"});

  Common::Redis::RespValue response;
  response.type(Common::Redis::RespType::Error);
  response.asString() = "NOPROTO unsupported protocol version";

  EXPECT_CALL(callbacks_, onResponse_(PointeesEq(&response)));
  handle_ = splitter_.makeRequest(std::move(request), callbacks_, dispatcher_, stream_info_);
  EXPECT_EQ(nullptr, handle_);
}

// Bare HELLO (no explicit version) inherits the connection's previously-negotiated downstream
// RESP version. If the cluster cap dropped below that under us (CDS RESP3 -> RESP2 downgrade
// after the connection negotiated RESP3), re-affirming the higher version would leave the
// connection on a RESP version the upstream wire can no longer carry. Splitter must reply
// -NOPROTO and schedule downstream close so the client reconnects and re-negotiates against
// the new lower cap. The companion guard for non-HELLO commands lives in
// NonHelloCommandClosesOnCapDowngrade below; this test pins the symmetric HELLO path.
TEST_F(RedisSingleServerRequestTest, BareHelloRejectsWhenCurrentVersionExceedsCap) {
  InSequence s;
  callbacks_.cluster_resp_version_ = 2;
  callbacks_.downstream_resp_version_ = 3;

  Common::Redis::RespValuePtr request{new Common::Redis::RespValue()};
  makeBulkStringArray(*request, {"hello"});

  Common::Redis::RespValue response;
  response.type(Common::Redis::RespType::Error);
  response.asString() = "NOPROTO unsupported protocol version";

  EXPECT_CALL(callbacks_, onResponse_(PointeesEq(&response)));
  handle_ = splitter_.makeRequest(std::move(request), callbacks_, dispatcher_, stream_info_);
  EXPECT_EQ(nullptr, handle_);
  EXPECT_TRUE(callbacks_.close_downstream_after_response_called_);
}

// Per-command guard for already-negotiated downstream connections. The
// cluster's RESP cap is dynamic — CDS may flip a cluster from RESP3 to
// RESP2 under a downstream connection that has already finished HELLO 3.
// The HELLO branch only re-validates on a fresh HELLO; non-HELLO commands
// arriving on the still-RESP3 connection would otherwise keep running
// under a contract the upstream can no longer honor (no Map/Set/Push).
// The splitter must respond with -NOPROTO and schedule the downstream
// connection close so the client reconnects and renegotiates.
TEST_F(RedisSingleServerRequestTest, NonHelloCommandClosesOnCapDowngrade) {
  InSequence s;
  // Negotiated RESP3 downstream, but cluster has dropped to RESP2.
  callbacks_.downstream_resp_version_ = 3;
  callbacks_.cluster_resp_version_ = 2;

  Common::Redis::RespValuePtr request{new Common::Redis::RespValue()};
  makeBulkStringArray(*request, {"get", "foo"});

  Common::Redis::RespValue expected_err;
  expected_err.type(Common::Redis::RespType::Error);
  expected_err.asString() = "NOPROTO unsupported protocol version";

  EXPECT_CALL(callbacks_, onResponse_(PointeesEq(&expected_err)));
  handle_ = splitter_.makeRequest(std::move(request), callbacks_, dispatcher_, stream_info_);
  EXPECT_EQ(nullptr, handle_);
  EXPECT_TRUE(callbacks_.close_downstream_after_response_called_);
}

// HELLO bypasses the per-command guard — HELLO IS the renegotiation path.
// The HELLO branch downstream applies its own version check against the
// (lower) cluster cap and rejects HELLO 3 with -NOPROTO.
TEST_F(RedisSingleServerRequestTest, HelloBypassesPerCmdCapGuard) {
  InSequence s;
  callbacks_.downstream_resp_version_ = 3;
  callbacks_.cluster_resp_version_ = 2;

  Common::Redis::RespValuePtr request{new Common::Redis::RespValue()};
  makeBulkStringArray(*request, {"hello", "2"}); // requesting RESP2 — within cap

  EXPECT_CALL(callbacks_, connectionAllowed()).WillOnce(Return(true));
  EXPECT_CALL(callbacks_, onResponse_(_));
  handle_ = splitter_.makeRequest(std::move(request), callbacks_, dispatcher_, stream_info_);
  EXPECT_EQ(nullptr, handle_);
  // HELLO did NOT trigger the per-cmd close path.
  EXPECT_FALSE(callbacks_.close_downstream_after_response_called_);
}

// AUTH bypasses the per-command guard — credentials are protocol-agnostic.
TEST_F(RedisSingleServerRequestTest, AuthBypassesPerCmdCapGuard) {
  InSequence s;
  callbacks_.downstream_resp_version_ = 3;
  callbacks_.cluster_resp_version_ = 2;

  Common::Redis::RespValuePtr request{new Common::Redis::RespValue()};
  makeBulkStringArray(*request, {"auth", "secret"});

  EXPECT_CALL(callbacks_, onAuth(testing::TypedEq<const std::string&>("secret")));
  handle_ = splitter_.makeRequest(std::move(request), callbacks_, dispatcher_, stream_info_);
  EXPECT_EQ(nullptr, handle_);
  EXPECT_FALSE(callbacks_.close_downstream_after_response_called_);
}

// QUIT bypasses the per-command guard — the client is leaving anyway.
TEST_F(RedisSingleServerRequestTest, QuitBypassesPerCmdCapGuard) {
  InSequence s;
  callbacks_.downstream_resp_version_ = 3;
  callbacks_.cluster_resp_version_ = 2;

  Common::Redis::RespValuePtr request{new Common::Redis::RespValue()};
  makeBulkStringArray(*request, {"quit"});

  EXPECT_CALL(callbacks_, connectionAllowed()).WillOnce(Return(true));
  EXPECT_CALL(callbacks_, onQuit());
  handle_ = splitter_.makeRequest(std::move(request), callbacks_, dispatcher_, stream_info_);
  EXPECT_EQ(nullptr, handle_);
  EXPECT_FALSE(callbacks_.close_downstream_after_response_called_);
}

// Sanity: when downstream and cluster are both RESP3, the guard is not
// triggered for normal commands (regression check on the boundary).
TEST_F(RedisSingleServerRequestTest, NonHelloCommandNotClosedWhenCapMatches) {
  InSequence s;
  callbacks_.downstream_resp_version_ = 3;
  callbacks_.cluster_resp_version_ = 3;

  Common::Redis::RespValuePtr request{new Common::Redis::RespValue()};
  makeBulkStringArray(*request, {"get", "foo"});
  // Normal request flow — splitter routes through to the conn pool.
  makeRequest("foo", std::move(request));
  EXPECT_NE(nullptr, handle_);
  EXPECT_FALSE(callbacks_.close_downstream_after_response_called_);
  // Cleanup the in-flight pool request.
  EXPECT_CALL(pool_request_, cancel());
  handle_->cancel();
}

// Cluster RESP cap of 2 (RESP2-only upstream) means HELLO 3 must be rejected
// with -NOPROTO so the proxy never advertises a RESP version it cannot honor
// on the upstream wire.
TEST_F(RedisSingleServerRequestTest, HelloRejectedAboveClusterCap) {
  InSequence s;
  callbacks_.cluster_resp_version_ = 2;

  Common::Redis::RespValuePtr request{new Common::Redis::RespValue()};
  makeBulkStringArray(*request, {"hello", "3"});

  Common::Redis::RespValue expected_err;
  expected_err.type(Common::Redis::RespType::Error);
  expected_err.asString() = "NOPROTO unsupported protocol version";

  // HELLO version check runs before connectionAllowed gate.
  EXPECT_CALL(callbacks_, onResponse_(PointeesEq(&expected_err)));
  handle_ = splitter_.makeRequest(std::move(request), callbacks_, dispatcher_, stream_info_);
  EXPECT_EQ(nullptr, handle_);
}

// HELLO 3 AUTH <user> <pass>: client authenticates inline as part of the
// HELLO handshake (the lettuce / node-redis v4+ / redis-py protocol=3
// pattern). The mock returns AuthAttempt::Allowed, so the splitter must:
//   - parse the AUTH option (3 tokens after protover);
//   - call attemptDownstreamAuthInline with the supplied credentials;
//   - emit a single Map reply (not an extra +OK from a separate AUTH).
TEST_F(RedisSingleServerRequestTest, HelloAuthInlineAllowed) {
  InSequence s;
  callbacks_.cluster_resp_version_ = 3;
  callbacks_.inline_auth_attempt_ = CommandSplitter::SplitCallbacks::AuthAttempt::Allowed;

  Common::Redis::RespValuePtr request{new Common::Redis::RespValue()};
  makeBulkStringArray(*request, {"hello", "3", "AUTH", "alice", "s3cret"});

  EXPECT_CALL(callbacks_, connectionAllowed())
      .Times(0); // HELLO bypasses the gate when AUTH is present.
  Common::Redis::RespValuePtr captured;
  EXPECT_CALL(callbacks_, onResponse_(_)).WillOnce([&captured](Common::Redis::RespValuePtr& v) {
    captured = std::move(v);
  });
  handle_ = splitter_.makeRequest(std::move(request), callbacks_, dispatcher_, stream_info_);
  EXPECT_EQ(nullptr, handle_);

  ASSERT_NE(nullptr, captured);
  EXPECT_EQ(Common::Redis::RespType::Map, captured->type());
  EXPECT_EQ("alice", callbacks_.last_inline_auth_username_);
  EXPECT_EQ("s3cret", callbacks_.last_inline_auth_password_);
  EXPECT_EQ(3U, callbacks_.downstream_resp_version_);
}

// Wrong credentials over inline HELLO AUTH: -WRONGPASS.
TEST_F(RedisSingleServerRequestTest, HelloAuthInlineDenied) {
  InSequence s;
  callbacks_.cluster_resp_version_ = 3;
  callbacks_.inline_auth_attempt_ = CommandSplitter::SplitCallbacks::AuthAttempt::Denied;

  Common::Redis::RespValuePtr request{new Common::Redis::RespValue()};
  makeBulkStringArray(*request, {"hello", "3", "AUTH", "alice", "wrong"});

  Common::Redis::RespValue expected_err;
  expected_err.type(Common::Redis::RespType::Error);
  expected_err.asString() = "WRONGPASS invalid username-password pair";

  EXPECT_CALL(callbacks_, onResponse_(PointeesEq(&expected_err)));
  handle_ = splitter_.makeRequest(std::move(request), callbacks_, dispatcher_, stream_info_);
  EXPECT_EQ(nullptr, handle_);
  // Denied auth must NOT flip the version state.
  EXPECT_NE(3U, callbacks_.downstream_resp_version_);
}

// HELLO 3 SETNAME <name>: accepted, name is ignored. The reply is the
// usual HELLO Map. Locks the contract that SETNAME does not block the
// handshake even though the proxy has no client-identity tracking.
TEST_F(RedisSingleServerRequestTest, HelloSetnameAcceptedAndIgnored) {
  InSequence s;
  callbacks_.cluster_resp_version_ = 3;

  Common::Redis::RespValuePtr request{new Common::Redis::RespValue()};
  makeBulkStringArray(*request, {"hello", "3", "SETNAME", "my-app"});

  Common::Redis::RespValuePtr captured;
  EXPECT_CALL(callbacks_, connectionAllowed()).WillOnce(Return(true));
  EXPECT_CALL(callbacks_, onResponse_(_)).WillOnce([&captured](Common::Redis::RespValuePtr& v) {
    captured = std::move(v);
  });
  handle_ = splitter_.makeRequest(std::move(request), callbacks_, dispatcher_, stream_info_);
  EXPECT_EQ(nullptr, handle_);
  ASSERT_NE(nullptr, captured);
  EXPECT_EQ(Common::Redis::RespType::Map, captured->type());
  EXPECT_EQ(3U, callbacks_.downstream_resp_version_);
}

// HELLO 3 AUTH <user> <pass> SETNAME <name>: combined options. Auth is
// validated inline (Allowed); SETNAME is parsed and ignored. Single Map reply.
TEST_F(RedisSingleServerRequestTest, HelloAuthAndSetnameTogether) {
  InSequence s;
  callbacks_.cluster_resp_version_ = 3;
  callbacks_.inline_auth_attempt_ = CommandSplitter::SplitCallbacks::AuthAttempt::Allowed;

  Common::Redis::RespValuePtr request{new Common::Redis::RespValue()};
  makeBulkStringArray(*request, {"hello", "3", "AUTH", "u", "p", "SETNAME", "client-1"});

  Common::Redis::RespValuePtr captured;
  EXPECT_CALL(callbacks_, onResponse_(_)).WillOnce([&captured](Common::Redis::RespValuePtr& v) {
    captured = std::move(v);
  });
  handle_ = splitter_.makeRequest(std::move(request), callbacks_, dispatcher_, stream_info_);
  EXPECT_EQ(nullptr, handle_);
  ASSERT_NE(nullptr, captured);
  EXPECT_EQ(Common::Redis::RespType::Map, captured->type());
  EXPECT_EQ("u", callbacks_.last_inline_auth_username_);
  EXPECT_EQ("p", callbacks_.last_inline_auth_password_);
}

// HELLO 3 with an unknown option must fail with a clear syntax error.
// Defends against silently swallowing an option a client thought it was
// passing (e.g. typo, future Redis option not yet supported).
TEST_F(RedisSingleServerRequestTest, HelloUnknownOptionRejected) {
  InSequence s;
  Common::Redis::RespValuePtr request{new Common::Redis::RespValue()};
  makeBulkStringArray(*request, {"hello", "3", "BOGUS", "x"});

  Common::Redis::RespValue expected_err;
  expected_err.type(Common::Redis::RespType::Error);
  expected_err.asString() = "ERR Syntax error: unknown HELLO option 'BOGUS'";
  EXPECT_CALL(callbacks_, onResponse_(PointeesEq(&expected_err)));
  handle_ = splitter_.makeRequest(std::move(request), callbacks_, dispatcher_, stream_info_);
  EXPECT_EQ(nullptr, handle_);
}

// CLIENT ``SETINFO`` is recognized by RESP3 clients (node-redis v4+, redis-py
// protocol=3) for connection identity metadata. The proxy has no client
// tracking; accept and reply +OK locally so client startup proceeds.
TEST_F(RedisSingleServerRequestTest, ClientSetinfoAcceptedLocally) {
  InSequence s;
  Common::Redis::RespValuePtr request{new Common::Redis::RespValue()};
  makeBulkStringArray(*request, {"client", "SETINFO", "lib-name", "node-redis"});

  Common::Redis::RespValue expected;
  expected.type(Common::Redis::RespType::SimpleString);
  expected.asString() = "OK";

  EXPECT_CALL(callbacks_, connectionAllowed()).WillOnce(Return(true));
  EXPECT_CALL(callbacks_, onResponse_(PointeesEq(&expected)));
  handle_ = splitter_.makeRequest(std::move(request), callbacks_, dispatcher_, stream_info_);
  EXPECT_EQ(nullptr, handle_);
}

// CLIENT SETNAME is the older form some go-redis variants still send.
TEST_F(RedisSingleServerRequestTest, ClientSetnameAcceptedLocally) {
  InSequence s;
  Common::Redis::RespValuePtr request{new Common::Redis::RespValue()};
  makeBulkStringArray(*request, {"client", "SETNAME", "my-app"});

  Common::Redis::RespValue expected;
  expected.type(Common::Redis::RespType::SimpleString);
  expected.asString() = "OK";

  EXPECT_CALL(callbacks_, connectionAllowed()).WillOnce(Return(true));
  EXPECT_CALL(callbacks_, onResponse_(PointeesEq(&expected)));
  handle_ = splitter_.makeRequest(std::move(request), callbacks_, dispatcher_, stream_info_);
  EXPECT_EQ(nullptr, handle_);
}

// Other CLIENT subcommands are deliberately not exposed: their semantics
// over a multiplexed proxy connection are ambiguous (CLIENT LIST would
// list one upstream host's sessions; CLIENT KILL would target one shard).
TEST_F(RedisSingleServerRequestTest, ClientUnsupportedSubcommandRejected) {
  InSequence s;
  Common::Redis::RespValuePtr request{new Common::Redis::RespValue()};
  makeBulkStringArray(*request, {"client", "LIST"});

  Common::Redis::RespValue expected_err;
  expected_err.type(Common::Redis::RespType::Error);
  expected_err.asString() = "ERR CLIENT subcommand 'LIST' is not supported by the proxy";

  EXPECT_CALL(callbacks_, connectionAllowed()).WillOnce(Return(true));
  EXPECT_CALL(callbacks_, onResponse_(PointeesEq(&expected_err)));
  handle_ = splitter_.makeRequest(std::move(request), callbacks_, dispatcher_, stream_info_);
  EXPECT_EQ(nullptr, handle_);
}

// Bare HELLO on an auth-required listener still requires prior auth — the
// HELLO + connectionAllowed reorder must NOT have removed the gate for
// non-AUTH HELLO calls.
TEST_F(RedisSingleServerRequestTest, BareHelloStillRequiresAuthGate) {
  InSequence s;
  Common::Redis::RespValuePtr request{new Common::Redis::RespValue()};
  makeBulkStringArray(*request, {"hello"});

  Common::Redis::RespValue expected_err;
  expected_err.type(Common::Redis::RespType::Error);
  expected_err.asString() = "NOAUTH Authentication required.";
  EXPECT_CALL(callbacks_, connectionAllowed()).WillOnce(Return(false));
  EXPECT_CALL(callbacks_, onResponse_(PointeesEq(&expected_err)));
  handle_ = splitter_.makeRequest(std::move(request), callbacks_, dispatcher_, stream_info_);
  EXPECT_EQ(nullptr, handle_);
}

// AUTO (the default) leaves min_version=2 so a bare HELLO and HELLO 2 are
// both still valid. Lock the existing default behavior so the new floor
// logic does not accidentally raise the floor for AUTO configs.
TEST_F(RedisSingleServerRequestTest, HelloVersionAutoAcceptsHello2) {
  InSequence s;
  callbacks_.cluster_resp_version_ = 3;

  Common::Redis::RespValuePtr request{new Common::Redis::RespValue()};
  makeBulkStringArray(*request, {"hello", "2"});

  Common::Redis::RespValuePtr captured;
  EXPECT_CALL(callbacks_, connectionAllowed()).WillOnce(Return(true));
  EXPECT_CALL(callbacks_, onResponse_(_)).WillOnce([&captured](Common::Redis::RespValuePtr& v) {
    captured = std::move(v);
  });
  handle_ = splitter_.makeRequest(std::move(request), callbacks_, dispatcher_, stream_info_);
  EXPECT_EQ(nullptr, handle_);
  ASSERT_NE(nullptr, captured);
  EXPECT_EQ(Common::Redis::RespType::Map, captured->type());
  EXPECT_EQ(2U, callbacks_.downstream_resp_version_);
}

TEST_F(RedisSingleServerRequestTest, CustomCommand) {
  absl::flat_hash_set<std::string> cmds = {"example"};
  auto splitter = getSplitter(std::move(cmds));

  Common::Redis::RespValuePtr request{new Common::Redis::RespValue()};
  makeBulkStringArray(*request, {"example", "test"});

  EXPECT_CALL(callbacks_, connectionAllowed()).WillOnce(Return(true));
  EXPECT_CALL(*conn_pool_, makeRequest_("test", RespVariantEq(*request), _))
      .WillOnce(DoAll(WithArg<2>(SaveArgAddress(&pool_callbacks_)), Return(&pool_request_)));

  handle_ = splitter.makeRequest(std::move(request), callbacks_, dispatcher_, stream_info_);
  EXPECT_NE(nullptr, handle_);

  respond();
}

MATCHER_P(CompositeArrayEq, rhs, "CompositeArray should be equal") {
  const ConnPool::RespVariant& obj = arg;
  const auto& lhs = absl::get<const Common::Redis::RespValue>(obj);
  EXPECT_TRUE(lhs.type() == Common::Redis::RespType::CompositeArray);
  EXPECT_EQ(lhs.asCompositeArray().size(), rhs.size());
  std::vector<std::string> array;
  for (auto const& entry : lhs.asCompositeArray()) {
    array.emplace_back(entry.asString());
  }
  EXPECT_EQ(array, rhs);
  return true;
}

class FragmentedRequestCommandHandlerTest : public RedisCommandSplitterImplTest {
public:
  void makeRequest(std::vector<std::string>& request_strings,
                   const std::list<uint64_t>& null_handle_indexes, bool mirrored) {
    uint32_t num_gets = expected_requests_.size();

    Common::Redis::RespValuePtr request{new Common::Redis::RespValue()};
    makeBulkStringArray(*request, request_strings);

    pool_callbacks_.resize(num_gets);
    mirror_pool_callbacks_.resize(num_gets);
    std::vector<Common::Redis::Client::MockPoolRequest> tmp_pool_requests(num_gets);
    pool_requests_.swap(tmp_pool_requests);
    std::vector<Common::Redis::Client::MockPoolRequest> tmp_mirrored_pool_requests(num_gets);
    mirror_pool_requests_.swap(tmp_mirrored_pool_requests);

    EXPECT_CALL(callbacks_, connectionAllowed()).WillOnce(Return(true));

    std::vector<Common::Redis::Client::MockPoolRequest> dummy_requests(num_gets);
    for (uint32_t i = 0; i < num_gets; i++) {
      Common::Redis::Client::PoolRequest* request_to_use = nullptr;
      if (std::find(null_handle_indexes.begin(), null_handle_indexes.end(), i) ==
          null_handle_indexes.end()) {
        request_to_use = &pool_requests_[i];
      }
      Common::Redis::Client::PoolRequest* mirror_request_to_use = nullptr;
      if (std::find(null_handle_indexes.begin(), null_handle_indexes.end(), i) ==
          null_handle_indexes.end()) {
        mirror_request_to_use = &dummy_requests[i];
      }
      EXPECT_CALL(*conn_pool_,
                  makeRequest_(std::to_string(i), CompositeArrayEq(expected_requests_[i]), _))
          .WillOnce(DoAll(WithArg<2>(SaveArgAddress(&pool_callbacks_[i])), Return(request_to_use)));
      if (mirrored) {
        EXPECT_CALL(*mirror_conn_pool_,
                    makeRequest_(std::to_string(i), CompositeArrayEq(expected_requests_[i]), _))
            .WillOnce(DoAll(WithArg<2>(SaveArgAddress(&mirror_pool_callbacks_[i])),
                            Return(mirror_request_to_use)));
      }
    }

    handle_ = splitter_.makeRequest(std::move(request), callbacks_, dispatcher_, stream_info_);
  }

  void makeRequestToShard(uint16_t shard_size, std::vector<std::string>& request_strings,
                          const std::list<uint64_t>& null_handle_indexes, bool mirrored) {
    Common::Redis::RespValuePtr request{new Common::Redis::RespValue()};
    makeBulkStringArray(*request, request_strings);

    pool_callbacks_.resize(shard_size);
    mirror_pool_callbacks_.resize(shard_size);
    std::vector<Common::Redis::Client::MockPoolRequest> tmp_pool_requests(shard_size);
    pool_requests_.swap(tmp_pool_requests);
    std::vector<Common::Redis::Client::MockPoolRequest> tmp_mirrored_pool_requests(shard_size);
    mirror_pool_requests_.swap(tmp_mirrored_pool_requests);
    EXPECT_CALL(callbacks_, connectionAllowed()).WillOnce(Return(true));
    std::vector<Common::Redis::Client::MockPoolRequest> dummy_requests(shard_size);

    EXPECT_CALL(*conn_pool_, shardSize_()).WillRepeatedly(Return(shard_size));
    if (mirrored) {
      EXPECT_CALL(*mirror_conn_pool_, shardSize_()).WillRepeatedly(Return(shard_size));
    }
    ConnPool::RespVariant keys(*request);
    for (uint32_t i = 0; i < shard_size; i++) {
      Common::Redis::Client::PoolRequest* request_to_use = nullptr;
      if (std::find(null_handle_indexes.begin(), null_handle_indexes.end(), i) ==
          null_handle_indexes.end()) {
        request_to_use = &pool_requests_[i];
      }
      Common::Redis::Client::PoolRequest* mirror_request_to_use = nullptr;
      if (std::find(null_handle_indexes.begin(), null_handle_indexes.end(), i) ==
          null_handle_indexes.end()) {
        mirror_request_to_use = &dummy_requests[i];
      }
      EXPECT_CALL(*conn_pool_, makeRequestToShard_(i, keys, _))
          .WillOnce(DoAll(WithArg<2>(SaveArgAddress(&pool_callbacks_[i])), Return(request_to_use)));
      if (mirrored) {
        EXPECT_CALL(*mirror_conn_pool_, makeRequestToShard_(i, keys, _))
            .WillOnce(DoAll(WithArg<2>(SaveArgAddress(&mirror_pool_callbacks_[i])),
                            Return(mirror_request_to_use)));
      }
    }
    handle_ = splitter_.makeRequest(std::move(request), callbacks_, dispatcher_, stream_info_);
  }

  std::vector<std::vector<std::string>> expected_requests_;
  std::vector<ConnPool::PoolCallbacks*> pool_callbacks_;
  std::vector<Common::Redis::Client::MockPoolRequest> pool_requests_;
  std::vector<ConnPool::PoolCallbacks*> mirror_pool_callbacks_;
  std::vector<Common::Redis::Client::MockPoolRequest> mirror_pool_requests_;
};

class RedisMGETCommandHandlerTest : public FragmentedRequestCommandHandlerTest {
public:
  void setup(uint32_t num_gets, const std::list<uint64_t>& null_handle_indexes,
             bool mirrored = false) {
    expected_requests_.reserve(num_gets);
    std::vector<std::string> request_strings = {"mget"};
    for (uint32_t i = 0; i < num_gets; i++) {
      request_strings.push_back(std::to_string(i));
      expected_requests_.push_back({"get", std::to_string(i)});
    }
    makeRequest(request_strings, null_handle_indexes, mirrored);
  }

  Common::Redis::RespValuePtr response(const std::string& result) {
    Common::Redis::RespValuePtr response = std::make_unique<Common::Redis::RespValue>();
    response->type(Common::Redis::RespType::BulkString);
    response->asString() = result;
    return response;
  }
};

TEST_F(RedisMGETCommandHandlerTest, Normal) {
  InSequence s;

  setup(2, {});
  EXPECT_NE(nullptr, handle_);

  Common::Redis::RespValue expected_response;
  expected_response.type(Common::Redis::RespType::Array);
  std::vector<Common::Redis::RespValue> elements(2);
  elements[0].type(Common::Redis::RespType::BulkString);
  elements[0].asString() = "response";
  elements[1].type(Common::Redis::RespType::BulkString);
  elements[1].asString() = "5";
  expected_response.asArray().swap(elements);

  pool_callbacks_[1]->onResponse(response("5"));

  time_system_.setMonotonicTime(std::chrono::milliseconds(10));
  EXPECT_CALL(store_, deliverHistogramToSinks(
                          Property(&Stats::Metric::name, "redis.foo.command.mget.latency"), 10));
  EXPECT_CALL(callbacks_, onResponse_(PointeesEq(&expected_response)));
  pool_callbacks_[0]->onResponse(response("response"));

  EXPECT_EQ(1UL, store_.counter("redis.foo.command.mget.total").value());
  EXPECT_EQ(1UL, store_.counter("redis.foo.command.mget.success").value());
};

TEST_F(RedisMGETCommandHandlerTest, Mirrored) {
  InSequence s;

  setupMirrorPolicy();
  setup(2, {}, true);
  EXPECT_NE(nullptr, handle_);

  Common::Redis::RespValue expected_response;
  expected_response.type(Common::Redis::RespType::Array);
  std::vector<Common::Redis::RespValue> elements(2);
  elements[0].type(Common::Redis::RespType::BulkString);
  elements[0].asString() = "response";
  elements[1].type(Common::Redis::RespType::BulkString);
  elements[1].asString() = "5";
  expected_response.asArray().swap(elements);

  pool_callbacks_[1]->onResponse(response("5"));
  mirror_pool_callbacks_[1]->onResponse(response("5"));

  time_system_.setMonotonicTime(std::chrono::milliseconds(10));
  EXPECT_CALL(store_, deliverHistogramToSinks(
                          Property(&Stats::Metric::name, "redis.foo.command.mget.latency"), 10));
  EXPECT_CALL(callbacks_, onResponse_(PointeesEq(&expected_response)));
  pool_callbacks_[0]->onResponse(response("response"));
  mirror_pool_callbacks_[0]->onResponse(response("response"));

  EXPECT_EQ(1UL, store_.counter("redis.foo.command.mget.total").value());
  EXPECT_EQ(1UL, store_.counter("redis.foo.command.mget.success").value());
};

TEST_F(RedisMGETCommandHandlerTest, NormalWithNull) {
  InSequence s;

  setup(2, {});
  EXPECT_NE(nullptr, handle_);

  Common::Redis::RespValue expected_response;
  expected_response.type(Common::Redis::RespType::Array);
  std::vector<Common::Redis::RespValue> elements(2);
  elements[0].type(Common::Redis::RespType::BulkString);
  elements[0].asString() = "response";
  expected_response.asArray().swap(elements);

  Common::Redis::RespValuePtr response2(new Common::Redis::RespValue());
  pool_callbacks_[1]->onResponse(std::move(response2));

  EXPECT_CALL(callbacks_, onResponse_(PointeesEq(&expected_response)));
  pool_callbacks_[0]->onResponse(response("response"));
};

TEST_F(RedisMGETCommandHandlerTest, NoUpstreamHostForAll) {
  // No InSequence to avoid making setup() more complicated.

  Common::Redis::RespValue expected_response;
  expected_response.type(Common::Redis::RespType::Array);
  std::vector<Common::Redis::RespValue> elements(2);
  elements[0].type(Common::Redis::RespType::Error);
  elements[0].asString() = Response::get().NoUpstreamHost;
  elements[1].type(Common::Redis::RespType::Error);
  elements[1].asString() = Response::get().NoUpstreamHost;
  expected_response.asArray().swap(elements);

  EXPECT_CALL(callbacks_, onResponse_(PointeesEq(&expected_response)));
  setup(2, {0, 1});
  EXPECT_EQ(nullptr, handle_);
  EXPECT_EQ(1UL, store_.counter("redis.foo.command.mget.total").value());
  EXPECT_EQ(1UL, store_.counter("redis.foo.command.mget.error").value());
};

TEST_F(RedisMGETCommandHandlerTest, NoUpstreamHostForOne) {
  InSequence s;

  setup(2, {0});
  EXPECT_NE(nullptr, handle_);

  Common::Redis::RespValue expected_response;
  expected_response.type(Common::Redis::RespType::Array);
  std::vector<Common::Redis::RespValue> elements(2);
  elements[0].type(Common::Redis::RespType::Error);
  elements[0].asString() = Response::get().NoUpstreamHost;
  elements[1].type(Common::Redis::RespType::Error);
  elements[1].asString() = Response::get().UpstreamFailure;
  expected_response.asArray().swap(elements);

  EXPECT_CALL(callbacks_, onResponse_(PointeesEq(&expected_response)));
  pool_callbacks_[1]->onFailure();
  EXPECT_EQ(1UL, store_.counter("redis.foo.command.mget.total").value());
  EXPECT_EQ(1UL, store_.counter("redis.foo.command.mget.error").value());
};

TEST_F(RedisMGETCommandHandlerTest, Failure) {
  InSequence s;

  setup(2, {});
  EXPECT_NE(nullptr, handle_);

  Common::Redis::RespValue expected_response;
  expected_response.type(Common::Redis::RespType::Array);
  std::vector<Common::Redis::RespValue> elements(2);
  elements[0].type(Common::Redis::RespType::BulkString);
  elements[0].asString() = "response";
  elements[1].type(Common::Redis::RespType::Error);
  elements[1].asString() = Response::get().UpstreamFailure;
  expected_response.asArray().swap(elements);

  pool_callbacks_[1]->onFailure();

  time_system_.setMonotonicTime(std::chrono::milliseconds(5));
  EXPECT_CALL(store_, deliverHistogramToSinks(
                          Property(&Stats::Metric::name, "redis.foo.command.mget.latency"), 5));
  EXPECT_CALL(callbacks_, onResponse_(PointeesEq(&expected_response)));
  pool_callbacks_[0]->onResponse(response("response"));
  EXPECT_EQ(1UL, store_.counter("redis.foo.command.mget.total").value());
  EXPECT_EQ(1UL, store_.counter("redis.foo.command.mget.error").value());
};

TEST_F(RedisMGETCommandHandlerTest, InvalidUpstreamResponse) {
  InSequence s;

  setup(2, {});
  EXPECT_NE(nullptr, handle_);

  Common::Redis::RespValue expected_response;
  expected_response.type(Common::Redis::RespType::Array);
  std::vector<Common::Redis::RespValue> elements(2);
  elements[0].type(Common::Redis::RespType::Error);
  elements[0].asString() = Response::get().UpstreamProtocolError;
  elements[1].type(Common::Redis::RespType::Error);
  elements[1].asString() = Response::get().UpstreamFailure;
  expected_response.asArray().swap(elements);

  pool_callbacks_[1]->onFailure();

  Common::Redis::RespValuePtr response1(new Common::Redis::RespValue());
  response1->type(Common::Redis::RespType::Integer);
  response1->asInteger() = 5;
  time_system_.setMonotonicTime(std::chrono::milliseconds(10));
  EXPECT_CALL(store_, deliverHistogramToSinks(
                          Property(&Stats::Metric::name, "redis.foo.command.mget.latency"), 10));
  EXPECT_CALL(callbacks_, onResponse_(PointeesEq(&expected_response)));
  pool_callbacks_[0]->onResponse(std::move(response1));
  EXPECT_EQ(1UL, store_.counter("redis.foo.command.mget.total").value());
  EXPECT_EQ(1UL, store_.counter("redis.foo.command.mget.error").value());
};

TEST_F(RedisMGETCommandHandlerTest, Cancel) {
  InSequence s;

  setup(2, {});
  EXPECT_NE(nullptr, handle_);

  EXPECT_CALL(pool_requests_[0], cancel());
  EXPECT_CALL(pool_requests_[1], cancel());
  handle_->cancel();
};

class RedisMSETCommandHandlerTest : public FragmentedRequestCommandHandlerTest {
public:
  void setup(uint32_t num_sets, const std::list<uint64_t>& null_handle_indexes,
             bool mirrored = false) {

    expected_requests_.reserve(num_sets);
    std::vector<std::string> request_strings = {"mset"};
    for (uint32_t i = 0; i < num_sets; i++) {
      // key
      request_strings.push_back(std::to_string(i));
      // value
      request_strings.push_back(std::to_string(i));

      expected_requests_.push_back({"set", std::to_string(i), std::to_string(i)});
    }
    makeRequest(request_strings, null_handle_indexes, mirrored);
  }

  Common::Redis::RespValuePtr okResponse() {
    Common::Redis::RespValuePtr response = std::make_unique<Common::Redis::RespValue>();
    response->type(Common::Redis::RespType::SimpleString);
    response->asString() = Response::get().OK;
    return response;
  }
};

TEST_F(RedisMSETCommandHandlerTest, Normal) {
  InSequence s;

  setup(2, {});
  EXPECT_NE(nullptr, handle_);

  Common::Redis::RespValue expected_response;
  expected_response.type(Common::Redis::RespType::SimpleString);
  expected_response.asString() = Response::get().OK;

  pool_callbacks_[1]->onResponse(okResponse());

  time_system_.setMonotonicTime(std::chrono::milliseconds(10));
  EXPECT_CALL(store_, deliverHistogramToSinks(
                          Property(&Stats::Metric::name, "redis.foo.command.mset.latency"), 10));
  EXPECT_CALL(callbacks_, onResponse_(PointeesEq(&expected_response)));
  pool_callbacks_[0]->onResponse(okResponse());

  EXPECT_EQ(1UL, store_.counter("redis.foo.command.mset.total").value());
  EXPECT_EQ(1UL, store_.counter("redis.foo.command.mset.success").value());
};

TEST_F(RedisMSETCommandHandlerTest, Mirrored) {
  InSequence s;

  setupMirrorPolicy();
  setup(2, {}, true);
  EXPECT_NE(nullptr, handle_);

  Common::Redis::RespValue expected_response;
  expected_response.type(Common::Redis::RespType::SimpleString);
  expected_response.asString() = Response::get().OK;

  pool_callbacks_[1]->onResponse(okResponse());
  mirror_pool_callbacks_[1]->onResponse(okResponse());

  time_system_.setMonotonicTime(std::chrono::milliseconds(10));
  EXPECT_CALL(store_, deliverHistogramToSinks(
                          Property(&Stats::Metric::name, "redis.foo.command.mset.latency"), 10));
  EXPECT_CALL(callbacks_, onResponse_(PointeesEq(&expected_response)));
  pool_callbacks_[0]->onResponse(okResponse());
  mirror_pool_callbacks_[0]->onResponse(okResponse());

  EXPECT_EQ(1UL, store_.counter("redis.foo.command.mset.total").value());
  EXPECT_EQ(1UL, store_.counter("redis.foo.command.mset.success").value());
};

TEST_F(RedisMSETCommandHandlerTest, NoUpstreamHostForAll) {
  // No InSequence to avoid making setup() more complicated.

  Common::Redis::RespValue expected_response;
  expected_response.type(Common::Redis::RespType::Error);
  expected_response.asString() = "finished with 2 error(s)";

  EXPECT_CALL(callbacks_, onResponse_(PointeesEq(&expected_response)));
  setup(2, {0, 1});
  EXPECT_EQ(nullptr, handle_);
  EXPECT_EQ(1UL, store_.counter("redis.foo.command.mset.total").value());
  EXPECT_EQ(1UL, store_.counter("redis.foo.command.mset.error").value());
};

TEST_F(RedisMSETCommandHandlerTest, NoUpstreamHostForOne) {
  InSequence s;

  setup(2, {0});
  EXPECT_NE(nullptr, handle_);

  Common::Redis::RespValue expected_response;
  expected_response.type(Common::Redis::RespType::Error);
  expected_response.asString() = "finished with 1 error(s)";

  EXPECT_CALL(callbacks_, onResponse_(PointeesEq(&expected_response)));
  pool_callbacks_[1]->onResponse(okResponse());
  EXPECT_EQ(1UL, store_.counter("redis.foo.command.mset.total").value());
  EXPECT_EQ(1UL, store_.counter("redis.foo.command.mset.error").value());
};

TEST_F(RedisMSETCommandHandlerTest, Cancel) {
  InSequence s;

  setup(2, {});
  EXPECT_NE(nullptr, handle_);

  EXPECT_CALL(pool_requests_[0], cancel());
  EXPECT_CALL(pool_requests_[1], cancel());
  handle_->cancel();
};

TEST_F(RedisMSETCommandHandlerTest, WrongNumberOfArgs) {
  InSequence s;

  Common::Redis::RespValue response;
  response.type(Common::Redis::RespType::Error);
  response.asString() = "wrong number of arguments for 'mset' command";
  EXPECT_CALL(callbacks_, connectionAllowed()).WillOnce(Return(true));
  EXPECT_CALL(callbacks_, onResponse_(PointeesEq(&response)));
  Common::Redis::RespValuePtr request{new Common::Redis::RespValue()};
  makeBulkStringArray(*request, {"mset", "foo", "bar", "fizz"});
  EXPECT_EQ(nullptr,
            splitter_.makeRequest(std::move(request), callbacks_, dispatcher_, stream_info_));
  EXPECT_EQ(1UL, store_.counter("redis.foo.command.mset.total").value());
  EXPECT_EQ(1UL, store_.counter("redis.foo.command.mset.error").value());
};

class RedisSplitKeysSumResultHandlerTest : public FragmentedRequestCommandHandlerTest,
                                           public testing::WithParamInterface<std::string> {
public:
  void setup(uint32_t num_commands, const std::list<uint64_t>& null_handle_indexes,
             bool mirrored = false) {

    expected_requests_.reserve(num_commands);
    std::vector<std::string> request_strings = {GetParam()};
    for (uint32_t i = 0; i < num_commands; i++) {
      request_strings.push_back(std::to_string(i));
      expected_requests_.push_back({GetParam(), std::to_string(i)});
    }
    makeRequest(request_strings, null_handle_indexes, mirrored);
  }

  Common::Redis::RespValuePtr response(int64_t value) {
    Common::Redis::RespValuePtr response = std::make_unique<Common::Redis::RespValue>();
    response->type(Common::Redis::RespType::Integer);
    response->asInteger() = value;
    return response;
  }
};

TEST_P(RedisSplitKeysSumResultHandlerTest, Normal) {
  InSequence s;

  setup(2, {});
  EXPECT_NE(nullptr, handle_);

  Common::Redis::RespValue expected_response;
  expected_response.type(Common::Redis::RespType::Integer);
  expected_response.asInteger() = 2;

  pool_callbacks_[1]->onResponse(response(1));

  time_system_.setMonotonicTime(std::chrono::milliseconds(10));
  EXPECT_CALL(
      store_,
      deliverHistogramToSinks(
          Property(&Stats::Metric::name, "redis.foo.command." + GetParam() + ".latency"), 10));
  EXPECT_CALL(callbacks_, onResponse_(PointeesEq(&expected_response)));
  pool_callbacks_[0]->onResponse(response(1));

  EXPECT_EQ(1UL, store_.counter("redis.foo.command." + GetParam() + ".total").value());
  EXPECT_EQ(1UL, store_.counter("redis.foo.command." + GetParam() + ".success").value());
};

TEST_P(RedisSplitKeysSumResultHandlerTest, Mirrored) {
  InSequence s;

  setupMirrorPolicy();
  setup(2, {}, true);
  EXPECT_NE(nullptr, handle_);

  Common::Redis::RespValue expected_response;
  expected_response.type(Common::Redis::RespType::Integer);
  expected_response.asInteger() = 2;

  pool_callbacks_[1]->onResponse(response(1));
  mirror_pool_callbacks_[1]->onResponse(response(1));

  time_system_.setMonotonicTime(std::chrono::milliseconds(10));
  EXPECT_CALL(
      store_,
      deliverHistogramToSinks(
          Property(&Stats::Metric::name, "redis.foo.command." + GetParam() + ".latency"), 10));
  EXPECT_CALL(callbacks_, onResponse_(PointeesEq(&expected_response)));
  pool_callbacks_[0]->onResponse(response(1));
  mirror_pool_callbacks_[0]->onResponse(response(1));

  EXPECT_EQ(1UL, store_.counter("redis.foo.command." + GetParam() + ".total").value());
  EXPECT_EQ(1UL, store_.counter("redis.foo.command." + GetParam() + ".success").value());
};

TEST_P(RedisSplitKeysSumResultHandlerTest, NormalOneZero) {
  InSequence s;

  setup(2, {});
  EXPECT_NE(nullptr, handle_);

  Common::Redis::RespValue expected_response;
  expected_response.type(Common::Redis::RespType::Integer);
  expected_response.asInteger() = 1;

  pool_callbacks_[1]->onResponse(response(0));

  EXPECT_CALL(callbacks_, onResponse_(PointeesEq(&expected_response)));
  pool_callbacks_[0]->onResponse(response(1));

  EXPECT_EQ(1UL, store_.counter("redis.foo.command." + GetParam() + ".total").value());
  EXPECT_EQ(1UL, store_.counter("redis.foo.command." + GetParam() + ".success").value());
};

TEST_P(RedisSplitKeysSumResultHandlerTest, NoUpstreamHostForAll) {
  // No InSequence to avoid making setup() more complicated.

  Common::Redis::RespValue expected_response;
  expected_response.type(Common::Redis::RespType::Error);
  expected_response.asString() = "finished with 2 error(s)";

  EXPECT_CALL(callbacks_, onResponse_(PointeesEq(&expected_response)));
  setup(2, {0, 1});
  EXPECT_EQ(nullptr, handle_);
  EXPECT_EQ(1UL, store_.counter("redis.foo.command." + GetParam() + ".total").value());
  EXPECT_EQ(1UL, store_.counter("redis.foo.command." + GetParam() + ".error").value());
};

INSTANTIATE_TEST_SUITE_P(
    RedisSplitKeysSumResultHandlerTest, RedisSplitKeysSumResultHandlerTest,
    testing::ValuesIn(Common::Redis::SupportedCommands::hashMultipleSumResultCommands()));

class RedisSingleServerRequestWithLatencyMicrosTest : public RedisSingleServerRequestTest {
public:
  RedisSingleServerRequestWithLatencyMicrosTest() : RedisSingleServerRequestTest(true) {}
};

TEST_P(RedisSingleServerRequestWithLatencyMicrosTest, Success) {
  InSequence s;

  std::string lower_command = absl::AsciiStrToLower(GetParam());

  Common::Redis::RespValuePtr request{new Common::Redis::RespValue()};
  makeBulkStringArray(*request, {GetParam(), "hello"});
  makeRequest("hello", std::move(request));
  EXPECT_NE(nullptr, handle_);

  time_system_.setMonotonicTime(std::chrono::milliseconds(10));
  EXPECT_CALL(store_, deliverHistogramToSinks(
                          Property(&Stats::Metric::name,
                                   fmt::format("redis.foo.command.{}.latency", lower_command)),
                          10000));
  respond();

  EXPECT_EQ(1UL, store_.counter(fmt::format("redis.foo.command.{}.total", lower_command)).value());
  EXPECT_EQ(1UL,
            store_.counter(fmt::format("redis.foo.command.{}.success", lower_command)).value());
};

INSTANTIATE_TEST_SUITE_P(RedisSingleServerRequestWithLatencyMicrosTest,
                         RedisSingleServerRequestWithLatencyMicrosTest,
                         testing::Values("get", "set", "incr", "zadd"));

// In subclasses of fault test, we mock the expected faults in the constructor, as the
// fault manager is owned by the splitter, which is also generated later in construction
// of the base test class.
class RedisSingleServerRequestWithFaultTest : public RedisSingleServerRequestTest {
public:
  NiceMock<Event::MockTimer>* timer_;
  Event::TimerCb timer_cb_;
  int delay_ms_;
  Common::Redis::FaultSharedPtr fault_ptr_;
};

class RedisSingleServerRequestWithErrorFaultTest : public RedisSingleServerRequestWithFaultTest {
public:
  RedisSingleServerRequestWithErrorFaultTest() {
    delay_ms_ = 0;
    fault_ptr_ = Common::Redis::FaultManagerImpl::makeFaultForTest(
        Common::Redis::FaultType::Error, std::chrono::milliseconds(delay_ms_));
    ON_CALL(*getFaultManager(), getFaultForCommand(_)).WillByDefault(Return(fault_ptr_.get()));
  }
};

TEST_P(RedisSingleServerRequestWithErrorFaultTest, Fault) {
  InSequence s;

  std::string lower_command = absl::AsciiStrToLower(GetParam());
  Common::Redis::RespValuePtr request{new Common::Redis::RespValue()};
  makeBulkStringArray(*request, {GetParam(), "hello"});

  EXPECT_CALL(callbacks_, connectionAllowed()).WillOnce(Return(true));
  EXPECT_CALL(callbacks_, onResponse_(_));
  handle_ = splitter_.makeRequest(std::move(request), callbacks_, dispatcher_, stream_info_);
  EXPECT_EQ(nullptr, handle_);

  EXPECT_EQ(1UL, store_.counter(fmt::format("redis.foo.command.{}.total", lower_command)).value());
  EXPECT_EQ(1UL, store_.counter(fmt::format("redis.foo.command.{}.error", lower_command)).value());
  EXPECT_EQ(1UL,
            store_.counter(fmt::format("redis.foo.command.{}.error_fault", lower_command)).value());
};

class RedisSingleServerRequestWithErrorWithDelayFaultTest
    : public RedisSingleServerRequestWithFaultTest {
public:
  RedisSingleServerRequestWithErrorWithDelayFaultTest() {
    delay_ms_ = 5;
    fault_ptr_ = Common::Redis::FaultManagerImpl::makeFaultForTest(
        Common::Redis::FaultType::Error, std::chrono::milliseconds(delay_ms_));
    ON_CALL(*getFaultManager(), getFaultForCommand(_)).WillByDefault(Return(fault_ptr_.get()));
    timer_ = new NiceMock<Event::MockTimer>();
  }
};

INSTANTIATE_TEST_SUITE_P(RedisSingleServerRequestWithErrorFaultTest,
                         RedisSingleServerRequestWithErrorFaultTest,
                         testing::Values("get", "set", "incr", "zadd"));

TEST_P(RedisSingleServerRequestWithErrorWithDelayFaultTest, Fault) {
  InSequence s;

  std::string lower_command = absl::AsciiStrToLower(GetParam());
  Common::Redis::RespValuePtr request{new Common::Redis::RespValue()};
  makeBulkStringArray(*request, {GetParam(), "hello"});

  // As error faults have zero latency, recorded latency is equal to the delay.
  EXPECT_CALL(callbacks_, connectionAllowed()).WillOnce(Return(true));
  EXPECT_CALL(dispatcher_, createTimer_(_)).WillOnce(Invoke([this](Event::TimerCb timer_cb) {
    timer_cb_ = timer_cb;
    return timer_;
  }));

  handle_ = splitter_.makeRequest(std::move(request), callbacks_, dispatcher_, stream_info_);
  EXPECT_NE(nullptr, handle_);
  time_system_.setMonotonicTime(std::chrono::milliseconds(delay_ms_));
  EXPECT_CALL(store_, deliverHistogramToSinks(
                          Property(&Stats::Metric::name,
                                   fmt::format("redis.foo.command.{}.latency", lower_command)),
                          delay_ms_));
  EXPECT_CALL(callbacks_, onResponse_(_));
  timer_cb_();

  EXPECT_EQ(1UL, store_.counter(fmt::format("redis.foo.command.{}.total", lower_command)).value());
  EXPECT_EQ(1UL, store_.counter(fmt::format("redis.foo.command.{}.error", lower_command)).value());
  EXPECT_EQ(1UL,
            store_.counter(fmt::format("redis.foo.command.{}.error_fault", lower_command)).value());
};

INSTANTIATE_TEST_SUITE_P(RedisSingleServerRequestWithErrorWithDelayFaultTest,
                         RedisSingleServerRequestWithErrorWithDelayFaultTest,
                         testing::Values("get", "set", "incr", "zadd"));

class RedisSingleServerRequestWithDelayFaultTest : public RedisSingleServerRequestWithFaultTest {
public:
  RedisSingleServerRequestWithDelayFaultTest() {
    delay_ms_ = 15;
    fault_ptr_ = Common::Redis::FaultManagerImpl::makeFaultForTest(
        Common::Redis::FaultType::Delay, std::chrono::milliseconds(delay_ms_));
    ON_CALL(*getFaultManager(), getFaultForCommand(_)).WillByDefault(Return(fault_ptr_.get()));
    timer_ = new NiceMock<Event::MockTimer>();
  }
};

TEST_P(RedisSingleServerRequestWithDelayFaultTest, Fault) {
  InSequence s;

  std::string lower_command = absl::AsciiStrToLower(GetParam());
  std::string hash_key = "hello";

  Common::Redis::RespValuePtr request{new Common::Redis::RespValue()};
  makeBulkStringArray(*request, {GetParam(), "hello"});

  EXPECT_CALL(callbacks_, connectionAllowed()).WillOnce(Return(true));
  EXPECT_CALL(dispatcher_, createTimer_(_)).WillOnce(Invoke([this](Event::TimerCb timer_cb) {
    timer_cb_ = timer_cb;
    return timer_;
  }));
  EXPECT_CALL(*conn_pool_, makeRequest_(hash_key, RespVariantEq(*request), _))
      .WillOnce(DoAll(WithArg<2>(SaveArgAddress(&pool_callbacks_)), Return(&pool_request_)));

  handle_ = splitter_.makeRequest(std::move(request), callbacks_, dispatcher_, stream_info_);

  EXPECT_NE(nullptr, handle_);

  EXPECT_CALL(store_, deliverHistogramToSinks(
                          Property(&Stats::Metric::name,
                                   fmt::format("redis.foo.command.{}.latency", lower_command)),
                          delay_ms_));
  respond();

  time_system_.setMonotonicTime(std::chrono::milliseconds(delay_ms_));
  timer_cb_();

  EXPECT_EQ(1UL, store_.counter(fmt::format("redis.foo.command.{}.total", lower_command)).value());
  EXPECT_EQ(1UL,
            store_.counter(fmt::format("redis.foo.command.{}.success", lower_command)).value());
  EXPECT_EQ(1UL,
            store_.counter(fmt::format("redis.foo.command.{}.delay_fault", lower_command)).value());
};

INSTANTIATE_TEST_SUITE_P(RedisSingleServerRequestWithDelayFaultTest,
                         RedisSingleServerRequestWithDelayFaultTest,
                         testing::Values("get", "set", "incr", "zadd"));

class ScanHandlerTest : public FragmentedRequestCommandHandlerTest,
                        public testing::WithParamInterface<std::string> {
public:
  void setup(uint16_t shard_size, const std::list<uint64_t>& null_handle_indexes,
             bool mirrored = false) {
    std::vector<std::string> request_strings = {"scan", "0"};
    makeRequestToShard(shard_size, request_strings, null_handle_indexes, mirrored);
  }

  Common::Redis::RespValuePtr response() {
    Common::Redis::RespValuePtr response = std::make_unique<Common::Redis::RespValue>();
    response->type(Common::Redis::RespType::Array);
    return response;
  }
};

TEST_P(ScanHandlerTest, Normal) {
  InSequence s;

  setup(2, {});
  EXPECT_NE(nullptr, handle_);
  Common::Redis::RespValue expected_response;
  expected_response.type(Common::Redis::RespType::Array);
  pool_callbacks_[1]->onResponse(response());
  time_system_.setMonotonicTime(std::chrono::milliseconds(10));
  EXPECT_CALL(
      store_,
      deliverHistogramToSinks(
          Property(&Stats::Metric::name, "redis.foo.command." + GetParam() + ".latency"), 10));
  EXPECT_CALL(callbacks_, onResponse_(PointeesEq(&expected_response)));
  pool_callbacks_[0]->onResponse(response());
  EXPECT_EQ(1UL, store_.counter("redis.foo.command." + GetParam() + ".total").value());
  EXPECT_EQ(1UL, store_.counter("redis.foo.command." + GetParam() + ".success").value());
};

TEST_P(ScanHandlerTest, Mirrored) {
  InSequence s;

  setupMirrorPolicy();
  setup(2, {}, true);
  EXPECT_NE(nullptr, handle_);

  Common::Redis::RespValue expected_response;
  expected_response.type(Common::Redis::RespType::Array);

  pool_callbacks_[1]->onResponse(response());
  mirror_pool_callbacks_[1]->onResponse(response());

  time_system_.setMonotonicTime(std::chrono::milliseconds(10));
  EXPECT_CALL(
      store_,
      deliverHistogramToSinks(
          Property(&Stats::Metric::name, "redis.foo.command." + GetParam() + ".latency"), 10));
  EXPECT_CALL(callbacks_, onResponse_(PointeesEq(&expected_response)));
  pool_callbacks_[0]->onResponse(response());
  mirror_pool_callbacks_[0]->onResponse(response());

  EXPECT_EQ(1UL, store_.counter("redis.foo.command." + GetParam() + ".total").value());
  EXPECT_EQ(1UL, store_.counter("redis.foo.command." + GetParam() + ".success").value());
};

TEST_F(ScanHandlerTest, Cancel) {
  InSequence s;

  setup(2, {});
  EXPECT_NE(nullptr, handle_);

  EXPECT_CALL(pool_requests_[0], cancel());
  EXPECT_CALL(pool_requests_[1], cancel());
  handle_->cancel();
};

TEST_P(ScanHandlerTest, NormalOneZero) {
  InSequence s;

  setup(2, {});
  EXPECT_NE(nullptr, handle_);

  Common::Redis::RespValue expected_response;
  expected_response.type(Common::Redis::RespType::Array);

  pool_callbacks_[1]->onResponse(response());

  EXPECT_CALL(callbacks_, onResponse_(PointeesEq(&expected_response)));
  pool_callbacks_[0]->onResponse(response());

  EXPECT_EQ(1UL, store_.counter("redis.foo.command." + GetParam() + ".total").value());
  EXPECT_EQ(1UL, store_.counter("redis.foo.command." + GetParam() + ".success").value());
};

TEST_P(ScanHandlerTest, UpstreamError) {
  Common::Redis::RespValue expected_response;
  expected_response.type(Common::Redis::RespType::Error);
  expected_response.asString() = "finished with 2 error(s)";

  EXPECT_CALL(callbacks_, onResponse_(PointeesEq(&expected_response)));
  setup(2, {0, 1});
  EXPECT_EQ(nullptr, handle_);
  EXPECT_EQ(1UL, store_.counter("redis.foo.command." + GetParam() + ".total").value());
  EXPECT_EQ(1UL, store_.counter("redis.foo.command." + GetParam() + ".error").value());
};

TEST_P(ScanHandlerTest, NoUpstreamHostForAll) {
  Common::Redis::RespValue expected_response;
  expected_response.type(Common::Redis::RespType::Error);
  expected_response.asString() = "no upstream host";

  EXPECT_CALL(callbacks_, onResponse_(PointeesEq(&expected_response)));
  setup(0, {});
  EXPECT_EQ(nullptr, handle_);
  EXPECT_EQ(1UL, store_.counter("redis.foo.command." + GetParam() + ".total").value());
  EXPECT_EQ(1UL, store_.counter("redis.foo.command." + GetParam() + ".error").value());
};

TEST_F(ScanHandlerTest, ScanWrongNumberOfArgs) {
  InSequence s;

  Common::Redis::RespValuePtr request{new Common::Redis::RespValue()};
  Common::Redis::RespValue response;
  response.type(Common::Redis::RespType::Error);

  response.asString() = "ERR wrong number of arguments for 'scan' command";
  EXPECT_CALL(callbacks_, connectionAllowed()).WillOnce(Return(true));
  EXPECT_CALL(callbacks_, onResponse_(PointeesEq(&response)));
  makeBulkStringArray(*request, {"scan"});
  EXPECT_EQ(nullptr,
            splitter_.makeRequest(std::move(request), callbacks_, dispatcher_, stream_info_));
};

INSTANTIATE_TEST_SUITE_P(ScanHandlerTest, ScanHandlerTest, testing::Values("scan"));

// INFO.SHARD command handler tests - queries a single specific shard
class InfoShardHandlerTest : public FragmentedRequestCommandHandlerTest,
                             public testing::WithParamInterface<std::string> {
public:
  Common::Redis::RespValuePtr response() {
    Common::Redis::RespValuePtr response = std::make_unique<Common::Redis::RespValue>();
    response->type(Common::Redis::RespType::BulkString);
    response->asString() = "# Server\r\nredis_version:6.2.6\r\n";
    return response;
  }
};

TEST_P(InfoShardHandlerTest, Normal) {
  InSequence s;

  Common::Redis::RespValuePtr request{new Common::Redis::RespValue()};
  makeBulkStringArray(*request, {"info.shard", "0"});

  pool_callbacks_.resize(1);
  std::vector<Common::Redis::Client::MockPoolRequest> tmp_pool_requests(1);
  pool_requests_.swap(tmp_pool_requests);

  EXPECT_CALL(callbacks_, connectionAllowed()).WillOnce(Return(true));
  EXPECT_CALL(*conn_pool_, shardSize_()).WillOnce(Return(2));
  EXPECT_CALL(*conn_pool_, makeRequestToShard_(0, _, _))
      .WillOnce(DoAll(WithArg<2>(SaveArgAddress(&pool_callbacks_[0])), Return(&pool_requests_[0])));

  handle_ = splitter_.makeRequest(std::move(request), callbacks_, dispatcher_, stream_info_);
  EXPECT_NE(nullptr, handle_);

  Common::Redis::RespValue expected_response;
  expected_response.type(Common::Redis::RespType::BulkString);
  expected_response.asString() = "# Server\r\nredis_version:6.2.6\r\n";

  time_system_.setMonotonicTime(std::chrono::milliseconds(10));
  EXPECT_CALL(
      store_,
      deliverHistogramToSinks(
          Property(&Stats::Metric::name, "redis.foo.command." + GetParam() + ".latency"), 10));
  EXPECT_CALL(callbacks_, onResponse_(PointeesEq(&expected_response)));
  pool_callbacks_[0]->onResponse(response());
  EXPECT_EQ(1UL, store_.counter("redis.foo.command." + GetParam() + ".total").value());
  EXPECT_EQ(1UL, store_.counter("redis.foo.command." + GetParam() + ".success").value());
};

TEST_F(InfoShardHandlerTest, Cancel) {
  InSequence s;

  Common::Redis::RespValuePtr request{new Common::Redis::RespValue()};
  makeBulkStringArray(*request, {"info.shard", "0"});

  pool_callbacks_.resize(1);
  std::vector<Common::Redis::Client::MockPoolRequest> tmp_pool_requests(1);
  pool_requests_.swap(tmp_pool_requests);

  EXPECT_CALL(callbacks_, connectionAllowed()).WillOnce(Return(true));
  EXPECT_CALL(*conn_pool_, shardSize_()).WillOnce(Return(2));
  EXPECT_CALL(*conn_pool_, makeRequestToShard_(0, _, _))
      .WillOnce(DoAll(WithArg<2>(SaveArgAddress(&pool_callbacks_[0])), Return(&pool_requests_[0])));

  handle_ = splitter_.makeRequest(std::move(request), callbacks_, dispatcher_, stream_info_);
  EXPECT_NE(nullptr, handle_);

  EXPECT_CALL(pool_requests_[0], cancel());
  handle_->cancel();
};

TEST_P(InfoShardHandlerTest, NoUpstreamHostForAll) {
  Common::Redis::RespValue expected_response;
  expected_response.type(Common::Redis::RespType::Error);
  expected_response.asString() = "no upstream host";

  Common::Redis::RespValuePtr request{new Common::Redis::RespValue()};
  makeBulkStringArray(*request, {"info.shard", "0"});

  EXPECT_CALL(callbacks_, connectionAllowed()).WillOnce(Return(true));
  EXPECT_CALL(*conn_pool_, shardSize_()).WillOnce(Return(0));
  EXPECT_CALL(callbacks_, onResponse_(PointeesEq(&expected_response)));

  handle_ = splitter_.makeRequest(std::move(request), callbacks_, dispatcher_, stream_info_);
  EXPECT_EQ(nullptr, handle_);
  EXPECT_EQ(1UL, store_.counter("redis.foo.command." + GetParam() + ".total").value());
  EXPECT_EQ(1UL, store_.counter("redis.foo.command." + GetParam() + ".error").value());
};

TEST_F(InfoShardHandlerTest, InfoShardWrongNumberOfArgs) {
  InSequence s;

  Common::Redis::RespValuePtr request{new Common::Redis::RespValue()};
  makeBulkStringArray(*request, {"info.shard", "0", "server", "extra"});

  Common::Redis::RespValue response;
  response.type(Common::Redis::RespType::Error);
  response.asString() = "wrong number of arguments for 'info.shard' command";

  EXPECT_CALL(callbacks_, connectionAllowed()).WillOnce(Return(true));
  EXPECT_CALL(callbacks_, onResponse_(PointeesEq(&response)));
  EXPECT_EQ(nullptr,
            splitter_.makeRequest(std::move(request), callbacks_, dispatcher_, stream_info_));
}
// When the mandatory shard_id parameter is missing, command splitter rejects the request before
// reaching our handler
TEST_F(InfoShardHandlerTest, InfoShardMissingShardId) {
  InSequence s;

  Common::Redis::RespValuePtr request{new Common::Redis::RespValue()};
  makeBulkStringArray(*request, {"info.shard"});

  Common::Redis::RespValue response;
  response.type(Common::Redis::RespType::Error);
  response.asString() = "invalid request";

  EXPECT_CALL(callbacks_, connectionAllowed()).WillOnce(Return(true));
  EXPECT_CALL(callbacks_, onResponse_(PointeesEq(&response)));
  handle_ = splitter_.makeRequest(std::move(request), callbacks_, dispatcher_, stream_info_);
  EXPECT_EQ(nullptr, handle_);
}

TEST_F(InfoShardHandlerTest, InfoShardInvalidShardId) {
  InSequence s;

  Common::Redis::RespValuePtr request{new Common::Redis::RespValue()};
  Common::Redis::RespValue response;
  response.type(Common::Redis::RespType::Error);
  response.asString() = "ERR invalid shard_id - must be a numeric shard index";

  EXPECT_CALL(callbacks_, connectionAllowed()).WillOnce(Return(true));
  EXPECT_CALL(callbacks_, onResponse_(PointeesEq(&response)));
  makeBulkStringArray(*request, {"info.shard", "abc"});
  EXPECT_EQ(nullptr,
            splitter_.makeRequest(std::move(request), callbacks_, dispatcher_, stream_info_));

  EXPECT_EQ(1UL, store_.counter("redis.foo.command.info.shard.total").value());
  EXPECT_EQ(1UL, store_.counter("redis.foo.command.info.shard.error").value());
}

TEST_F(InfoShardHandlerTest, InfoShardOutOfRange) {
  InSequence s;

  Common::Redis::RespValuePtr request{new Common::Redis::RespValue()};
  Common::Redis::RespValue response;
  response.type(Common::Redis::RespType::Error);
  response.asString() = "ERR shard_id 999 out of range (0-1)";

  EXPECT_CALL(callbacks_, connectionAllowed()).WillOnce(Return(true));
  EXPECT_CALL(*conn_pool_, shardSize_()).WillOnce(Return(2));
  EXPECT_CALL(callbacks_, onResponse_(PointeesEq(&response)));
  makeBulkStringArray(*request, {"info.shard", "999"});
  EXPECT_EQ(nullptr,
            splitter_.makeRequest(std::move(request), callbacks_, dispatcher_, stream_info_));

  EXPECT_EQ(1UL, store_.counter("redis.foo.command.info.shard.total").value());
  EXPECT_EQ(1UL, store_.counter("redis.foo.command.info.shard.error").value());
}

TEST_F(InfoShardHandlerTest, InfoShardWithSection) {
  InSequence s;

  Common::Redis::RespValuePtr request{new Common::Redis::RespValue()};
  makeBulkStringArray(*request, {"info.shard", "0", "server"});

  pool_callbacks_.resize(1);
  std::vector<Common::Redis::Client::MockPoolRequest> tmp_pool_requests(1);
  pool_requests_.swap(tmp_pool_requests);

  EXPECT_CALL(callbacks_, connectionAllowed()).WillOnce(Return(true));
  EXPECT_CALL(*conn_pool_, shardSize_()).WillOnce(Return(2));
  EXPECT_CALL(*conn_pool_, makeRequestToShard_(0, _, _))
      .WillOnce(DoAll(WithArg<2>(SaveArgAddress(&pool_callbacks_[0])), Return(&pool_requests_[0])));

  handle_ = splitter_.makeRequest(std::move(request), callbacks_, dispatcher_, stream_info_);
  EXPECT_NE(nullptr, handle_);

  Common::Redis::RespValue expected_response;
  expected_response.type(Common::Redis::RespType::BulkString);
  expected_response.asString() = "# Server\r\nredis_version:6.2.6\r\n";

  EXPECT_CALL(callbacks_, onResponse_(PointeesEq(&expected_response)));
  pool_callbacks_[0]->onResponse(response());

  EXPECT_EQ(1UL, store_.counter("redis.foo.command.info.shard.total").value());
  EXPECT_EQ(1UL, store_.counter("redis.foo.command.info.shard.success").value());
}

TEST_F(InfoShardHandlerTest, InfoShardNoUpstreamForShard) {
  InSequence s;

  Common::Redis::RespValuePtr request{new Common::Redis::RespValue()};
  makeBulkStringArray(*request, {"info.shard", "0"});

  Common::Redis::RespValue expected_response;
  expected_response.type(Common::Redis::RespType::Error);
  expected_response.asString() = Response::get().NoUpstreamHost;

  EXPECT_CALL(callbacks_, connectionAllowed()).WillOnce(Return(true));
  EXPECT_CALL(*conn_pool_, shardSize_()).WillOnce(Return(2));
  EXPECT_CALL(*conn_pool_, makeRequestToShard_(0, _, _)).WillOnce(Return(nullptr));
  EXPECT_CALL(callbacks_, onResponse_(PointeesEq(&expected_response)));

  handle_ = splitter_.makeRequest(std::move(request), callbacks_, dispatcher_, stream_info_);
  // Even though we get an error, a request object is returned (onResponse is called immediately)
  EXPECT_NE(nullptr, handle_);

  EXPECT_EQ(1UL, store_.counter("redis.foo.command.info.shard.total").value());
  EXPECT_EQ(1UL, store_.counter("redis.foo.command.info.shard.error").value());
}

INSTANTIATE_TEST_SUITE_P(InfoShardHandlerTest, InfoShardHandlerTest, testing::Values("info.shard"));

// Test cluster scope commands - ROLE (ArrayAppendAggregateResponseHandler)
class ClusterScopeRoleTest : public FragmentedRequestCommandHandlerTest {
public:
  void setup(uint16_t shard_size, const std::list<uint64_t>& null_handle_indexes,
             bool mirrored = false) {
    std::vector<std::string> request_strings = {"role"};
    makeRequestToShard(shard_size, request_strings, null_handle_indexes, mirrored);
  }

  Common::Redis::RespValuePtr masterResponse() {
    Common::Redis::RespValuePtr response = std::make_unique<Common::Redis::RespValue>();
    response->type(Common::Redis::RespType::Array);
    std::vector<Common::Redis::RespValue> elements(3);
    elements[0].type(Common::Redis::RespType::BulkString);
    elements[0].asString() = "master";
    elements[1].type(Common::Redis::RespType::Integer);
    elements[1].asInteger() = 0;
    elements[2].type(Common::Redis::RespType::Array);
    response->asArray().swap(elements);
    return response;
  }

  Common::Redis::RespValuePtr slaveResponse() {
    Common::Redis::RespValuePtr response = std::make_unique<Common::Redis::RespValue>();
    response->type(Common::Redis::RespType::Array);
    std::vector<Common::Redis::RespValue> elements(5);
    elements[0].type(Common::Redis::RespType::BulkString);
    elements[0].asString() = "slave";
    elements[1].type(Common::Redis::RespType::BulkString);
    elements[1].asString() = "127.0.0.1";
    elements[2].type(Common::Redis::RespType::Integer);
    elements[2].asInteger() = 6379;
    elements[3].type(Common::Redis::RespType::BulkString);
    elements[3].asString() = "connected";
    elements[4].type(Common::Redis::RespType::Integer);
    elements[4].asInteger() = 0;
    response->asArray().swap(elements);
    return response;
  }

  Common::Redis::RespValuePtr bulkStringResponse() {
    Common::Redis::RespValuePtr response = std::make_unique<Common::Redis::RespValue>();
    response->type(Common::Redis::RespType::BulkString);
    response->asString() = "master";
    return response;
  }

  Common::Redis::RespValuePtr errorResponse(const std::string& error_msg) {
    return Common::Redis::Utility::makeError(error_msg);
  }
};

TEST_F(ClusterScopeRoleTest, RoleNormal) {
  InSequence s;
  setup(2, {});
  EXPECT_NE(nullptr, handle_);

  Common::Redis::RespValue expected_response;
  expected_response.type(Common::Redis::RespType::Array);
  std::vector<Common::Redis::RespValue> elements(2);
  // elements[0] corresponds to pool_callbacks_[0] (master)
  elements[0].type(Common::Redis::RespType::Array);
  std::vector<Common::Redis::RespValue> master_elements(3);
  master_elements[0].type(Common::Redis::RespType::BulkString);
  master_elements[0].asString() = "master";
  master_elements[1].type(Common::Redis::RespType::Integer);
  master_elements[1].asInteger() = 0;
  master_elements[2].type(Common::Redis::RespType::Array);
  elements[0].asArray().swap(master_elements);
  // elements[1] corresponds to pool_callbacks_[1] (slave)
  elements[1].type(Common::Redis::RespType::Array);
  std::vector<Common::Redis::RespValue> slave_elements(5);
  slave_elements[0].type(Common::Redis::RespType::BulkString);
  slave_elements[0].asString() = "slave";
  slave_elements[1].type(Common::Redis::RespType::BulkString);
  slave_elements[1].asString() = "127.0.0.1";
  slave_elements[2].type(Common::Redis::RespType::Integer);
  slave_elements[2].asInteger() = 6379;
  slave_elements[3].type(Common::Redis::RespType::BulkString);
  slave_elements[3].asString() = "connected";
  slave_elements[4].type(Common::Redis::RespType::Integer);
  slave_elements[4].asInteger() = 0;
  elements[1].asArray().swap(slave_elements);
  expected_response.asArray().swap(elements);

  pool_callbacks_[0]->onResponse(masterResponse());
  time_system_.setMonotonicTime(std::chrono::milliseconds(10));
  EXPECT_CALL(store_, deliverHistogramToSinks(
                          Property(&Stats::Metric::name, "redis.foo.command.role.latency"), 10));
  EXPECT_CALL(callbacks_, onResponse_(PointeesEq(&expected_response)));
  pool_callbacks_[1]->onResponse(slaveResponse());

  EXPECT_EQ(1UL, store_.counter("redis.foo.command.role.total").value());
  EXPECT_EQ(1UL, store_.counter("redis.foo.command.role.success").value());
}

TEST_F(ClusterScopeRoleTest, RoleMirrored) {
  InSequence s;
  setupMirrorPolicy();
  setup(2, {}, true);
  EXPECT_NE(nullptr, handle_);

  Common::Redis::RespValue expected_response;
  expected_response.type(Common::Redis::RespType::Array);
  std::vector<Common::Redis::RespValue> elements(2);
  elements[0].type(Common::Redis::RespType::Array);
  std::vector<Common::Redis::RespValue> master_elements(3);
  master_elements[0].type(Common::Redis::RespType::BulkString);
  master_elements[0].asString() = "master";
  master_elements[1].type(Common::Redis::RespType::Integer);
  master_elements[1].asInteger() = 0;
  master_elements[2].type(Common::Redis::RespType::Array);
  elements[0].asArray().swap(master_elements);
  elements[1].type(Common::Redis::RespType::Array);
  std::vector<Common::Redis::RespValue> master_elements2(3);
  master_elements2[0].type(Common::Redis::RespType::BulkString);
  master_elements2[0].asString() = "master";
  master_elements2[1].type(Common::Redis::RespType::Integer);
  master_elements2[1].asInteger() = 0;
  master_elements2[2].type(Common::Redis::RespType::Array);
  elements[1].asArray().swap(master_elements2);
  expected_response.asArray().swap(elements);

  pool_callbacks_[0]->onResponse(masterResponse());
  mirror_pool_callbacks_[0]->onResponse(masterResponse());

  time_system_.setMonotonicTime(std::chrono::milliseconds(10));
  EXPECT_CALL(store_, deliverHistogramToSinks(
                          Property(&Stats::Metric::name, "redis.foo.command.role.latency"), 10));
  EXPECT_CALL(callbacks_, onResponse_(PointeesEq(&expected_response)));
  pool_callbacks_[1]->onResponse(masterResponse());
  mirror_pool_callbacks_[1]->onResponse(masterResponse());

  EXPECT_EQ(1UL, store_.counter("redis.foo.command.role.total").value());
  EXPECT_EQ(1UL, store_.counter("redis.foo.command.role.success").value());
}

TEST_F(ClusterScopeRoleTest, RoleNoUpstreamHostForAll) {
  Common::Redis::RespValue expected_response;
  expected_response.type(Common::Redis::RespType::Error);
  expected_response.asString() = "no upstream host";

  EXPECT_CALL(callbacks_, onResponse_(PointeesEq(&expected_response)));
  setup(0, {});
  EXPECT_EQ(nullptr, handle_);
  EXPECT_EQ(1UL, store_.counter("redis.foo.command.role.total").value());
  EXPECT_EQ(1UL, store_.counter("redis.foo.command.role.error").value());
}

TEST_F(ClusterScopeRoleTest, RoleNoUpstreamHostForOne) {
  InSequence s;
  setup(2, {0});
  EXPECT_NE(nullptr, handle_);

  time_system_.setMonotonicTime(std::chrono::milliseconds(10));
  EXPECT_CALL(store_, deliverHistogramToSinks(
                          Property(&Stats::Metric::name, "redis.foo.command.role.latency"), 10));
  EXPECT_CALL(callbacks_, onResponse_(_));
  pool_callbacks_[1]->onResponse(masterResponse());
  EXPECT_EQ(1UL, store_.counter("redis.foo.command.role.total").value());
  EXPECT_EQ(1UL, store_.counter("redis.foo.command.role.error").value());
}

TEST_F(ClusterScopeRoleTest, RoleUpstreamFailure) {
  InSequence s;
  setup(2, {});
  EXPECT_NE(nullptr, handle_);

  pool_callbacks_[1]->onFailure();

  time_system_.setMonotonicTime(std::chrono::milliseconds(5));
  EXPECT_CALL(store_, deliverHistogramToSinks(
                          Property(&Stats::Metric::name, "redis.foo.command.role.latency"), 5));
  EXPECT_CALL(callbacks_, onResponse_(_));
  pool_callbacks_[0]->onResponse(masterResponse());
  EXPECT_EQ(1UL, store_.counter("redis.foo.command.role.total").value());
  EXPECT_EQ(1UL, store_.counter("redis.foo.command.role.error").value());
}

TEST_F(ClusterScopeRoleTest, RoleInvalidUpstreamResponse) {
  InSequence s;
  setup(2, {});
  EXPECT_NE(nullptr, handle_);

  pool_callbacks_[1]->onResponse(masterResponse());

  Common::Redis::RespValuePtr invalid_response = std::make_unique<Common::Redis::RespValue>();
  invalid_response->type(Common::Redis::RespType::Integer);
  invalid_response->asInteger() = 123;

  time_system_.setMonotonicTime(std::chrono::milliseconds(10));
  EXPECT_CALL(store_, deliverHistogramToSinks(
                          Property(&Stats::Metric::name, "redis.foo.command.role.latency"), 10));
  EXPECT_CALL(callbacks_, onResponse_(_));
  pool_callbacks_[0]->onResponse(std::move(invalid_response));
  EXPECT_EQ(1UL, store_.counter("redis.foo.command.role.total").value());
  EXPECT_EQ(1UL, store_.counter("redis.foo.command.role.error").value());
}

TEST_F(ClusterScopeRoleTest, RoleErrorResponse) {
  InSequence s;
  setup(2, {});
  EXPECT_NE(nullptr, handle_);

  pool_callbacks_[0]->onResponse(masterResponse());

  time_system_.setMonotonicTime(std::chrono::milliseconds(10));
  EXPECT_CALL(store_, deliverHistogramToSinks(
                          Property(&Stats::Metric::name, "redis.foo.command.role.latency"), 10));
  EXPECT_CALL(callbacks_, onResponse_(_));
  pool_callbacks_[1]->onResponse(errorResponse("ERR shard error"));

  EXPECT_EQ(1UL, store_.counter("redis.foo.command.role.total").value());
  EXPECT_EQ(1UL, store_.counter("redis.foo.command.role.error").value());
}

TEST_F(ClusterScopeRoleTest, RoleNullResponse) {
  InSequence s;
  setup(2, {});
  EXPECT_NE(nullptr, handle_);

  pool_callbacks_[0]->onResponse(masterResponse());

  Common::Redis::RespValuePtr null_resp;

  time_system_.setMonotonicTime(std::chrono::milliseconds(10));
  EXPECT_CALL(store_, deliverHistogramToSinks(
                          Property(&Stats::Metric::name, "redis.foo.command.role.latency"), 10));
  EXPECT_CALL(callbacks_, onResponse_(_));
  pool_callbacks_[1]->onResponse(std::move(null_resp));

  EXPECT_EQ(1UL, store_.counter("redis.foo.command.role.total").value());
  EXPECT_EQ(1UL, store_.counter("redis.foo.command.role.error").value());
}

TEST_F(ClusterScopeRoleTest, RoleCancel) {
  InSequence s;
  setup(2, {});
  EXPECT_NE(nullptr, handle_);

  EXPECT_CALL(pool_requests_[0], cancel());
  EXPECT_CALL(pool_requests_[1], cancel());
  handle_->cancel();
}

// ===== RANDOM SHARD COMMAND TESTS =====

// Test random shard commands - these route to a single random shard
class RandomShardRequestTest : public FragmentedRequestCommandHandlerTest {
public:
  void setup(std::vector<std::string> request_strings,
             const std::list<uint64_t>& null_handle_indexes = {}, bool mirrored = false) {
    makeRequestToShard(1, request_strings, null_handle_indexes, mirrored);
  }

  Common::Redis::RespValuePtr response() {
    Common::Redis::RespValuePtr response = std::make_unique<Common::Redis::RespValue>();
    response->type(Common::Redis::RespType::BulkString);
    response->asString() = "test_response";
    return response;
  }

  Common::Redis::RespValuePtr errorResponse(const std::string& error_msg) {
    return Common::Redis::Utility::makeError(error_msg);
  }
};

TEST_F(RandomShardRequestTest, RandomKey) {
  InSequence s;

  setup({"randomkey"});
  EXPECT_NE(nullptr, handle_);

  time_system_.setMonotonicTime(std::chrono::milliseconds(10));
  EXPECT_CALL(store_,
              deliverHistogramToSinks(
                  Property(&Stats::Metric::name, "redis.foo.command.randomkey.latency"), 10));
  EXPECT_CALL(callbacks_, onResponse_(_));
  pool_callbacks_[0]->onResponse(response());

  EXPECT_EQ(1UL, store_.counter("redis.foo.command.randomkey.total").value());
  EXPECT_EQ(1UL, store_.counter("redis.foo.command.randomkey.success").value());
}

TEST_F(RandomShardRequestTest, ClusterNodes) {
  InSequence s;

  setup({"cluster", "nodes"});
  EXPECT_NE(nullptr, handle_);

  time_system_.setMonotonicTime(std::chrono::milliseconds(10));
  EXPECT_CALL(store_, deliverHistogramToSinks(
                          Property(&Stats::Metric::name, "redis.foo.command.cluster.latency"), 10));
  EXPECT_CALL(callbacks_, onResponse_(_));
  pool_callbacks_[0]->onResponse(response());

  EXPECT_EQ(1UL, store_.counter("redis.foo.command.cluster.total").value());
  EXPECT_EQ(1UL, store_.counter("redis.foo.command.cluster.success").value());
}

TEST_F(RandomShardRequestTest, UnsupportedSubcommand) {
  // Test unsupported subcommand for random shard commands (e.g., cluster reset)
  Common::Redis::RespValue expected_response;
  expected_response.type(Common::Redis::RespType::Error);
  expected_response.asString() = "ERR cluster subcommand 'reset' is not supported";

  EXPECT_CALL(callbacks_, connectionAllowed()).WillOnce(Return(true));
  EXPECT_CALL(callbacks_, onResponse_(PointeesEq(&expected_response)));

  Common::Redis::RespValuePtr request{new Common::Redis::RespValue()};
  std::vector<std::string> request_strings = {"cluster", "reset"};
  makeBulkStringArray(*request, request_strings);

  handle_ = splitter_.makeRequest(std::move(request), callbacks_, dispatcher_, stream_info_);
  EXPECT_EQ(nullptr, handle_);

  EXPECT_EQ(1UL, store_.counter("redis.foo.command.cluster.total").value());
  EXPECT_EQ(1UL, store_.counter("redis.foo.command.cluster.error").value());
}

TEST_F(RandomShardRequestTest, MakeRequestToShardReturnsNull) {
  // Test case where route exists but makeFragmentedRequestToShard returns null
  // This tests the condition: if (!pending_request.handle_)

  // We expect an error response when the handle is null (this happens during setup)
  EXPECT_CALL(callbacks_, onResponse_(_));

  setup({"randomkey"}, {0});   // Setup with null_handle_indexes = {0} to mock null handle
  EXPECT_NE(nullptr, handle_); // Request object is created and returned

  // The pending request should receive a NoUpstreamHost error response automatically
  // when makeFragmentedRequestToShard returns null, but since we have 1 pending response,
  // the request_ptr is still returned (not nullptr)

  // Verify the error counter is incremented due to the null handle
  EXPECT_EQ(1UL, store_.counter("redis.foo.command.randomkey.total").value());
}

TEST_F(RandomShardRequestTest, ErrorResponse) {
  // Test case where shard returns an error response
  // This tests the onChildResponse method with error responses to ensure updateStats(false) is
  // called
  InSequence s;

  setup({"randomkey"});
  EXPECT_NE(nullptr, handle_);

  time_system_.setMonotonicTime(std::chrono::milliseconds(15));
  EXPECT_CALL(store_,
              deliverHistogramToSinks(
                  Property(&Stats::Metric::name, "redis.foo.command.randomkey.latency"), 15));
  EXPECT_CALL(callbacks_, onResponse_(_));
  pool_callbacks_[0]->onResponse(errorResponse("ERR some error occurred"));

  EXPECT_EQ(1UL, store_.counter("redis.foo.command.randomkey.total").value());
  EXPECT_EQ(0UL, store_.counter("redis.foo.command.randomkey.success")
                     .value()); // No success for error response
  EXPECT_EQ(1UL, store_.counter("redis.foo.command.randomkey.error")
                     .value()); // Error counter should be incremented
}

TEST_F(RandomShardRequestTest, NoShardsAvailable) {
  // Test that random shard commands fail gracefully when shard_size = 0
  // This tests the condition: if (shard_size == 0)

  Common::Redis::RespValue expected_response;
  expected_response.type(Common::Redis::RespType::Error);
  expected_response.asString() = "no upstream host";

  Common::Redis::RespValuePtr request{new Common::Redis::RespValue()};
  makeBulkStringArray(*request, {"randomkey"});

  EXPECT_CALL(callbacks_, connectionAllowed()).WillOnce(Return(true));
  EXPECT_CALL(*conn_pool_, shardSize_()).WillOnce(Return(0));
  EXPECT_CALL(callbacks_, onResponse_(PointeesEq(&expected_response)));

  auto handle = splitter_.makeRequest(std::move(request), callbacks_, dispatcher_, stream_info_);
  EXPECT_EQ(nullptr, handle);

  EXPECT_EQ(1UL, store_.counter("redis.foo.command.randomkey.total").value());
  EXPECT_EQ(1UL, store_.counter("redis.foo.command.randomkey.error").value());
}

// ===== CLUSTER SCOPE COMMAND TESTS =====

// Test cluster scope commands - CONFIG SET (AllshardSameResponseHandler)
class ClusterScopeConfigTest : public FragmentedRequestCommandHandlerTest {
public:
  void setup(uint16_t shard_size, const std::list<uint64_t>& null_handle_indexes,
             bool mirrored = false) {
    std::vector<std::string> request_strings = {"config", "set", "maxmemory", "100mb"};
    makeRequestToShard(shard_size, request_strings, null_handle_indexes, mirrored);
  }

  Common::Redis::RespValuePtr okResponse() {
    auto response = std::make_unique<Common::Redis::RespValue>();
    response->type(Common::Redis::RespType::SimpleString);
    response->asString() = "OK";
    return response;
  }

  Common::Redis::RespValuePtr errorResponse(const std::string& error_msg) {
    return Common::Redis::Utility::makeError(error_msg);
  }
};

TEST_F(ClusterScopeConfigTest, ConfigSetAllShardsReturnSame) {
  InSequence s;
  setup(3, {});
  EXPECT_NE(nullptr, handle_);

  Common::Redis::RespValue expected_response;
  expected_response.type(Common::Redis::RespType::SimpleString);
  expected_response.asString() = "OK";

  // All shards return OK
  pool_callbacks_[0]->onResponse(okResponse());
  pool_callbacks_[1]->onResponse(okResponse());

  time_system_.setMonotonicTime(std::chrono::milliseconds(10));
  EXPECT_CALL(store_, deliverHistogramToSinks(
                          Property(&Stats::Metric::name, "redis.foo.command.config.latency"), 10));
  EXPECT_CALL(callbacks_, onResponse_(PointeesEq(&expected_response)));
  pool_callbacks_[2]->onResponse(okResponse());

  EXPECT_EQ(1UL, store_.counter("redis.foo.command.config.total").value());
  EXPECT_EQ(1UL, store_.counter("redis.foo.command.config.success").value());
}

TEST_F(ClusterScopeConfigTest, ConfigSetDifferentResponses) {
  InSequence s;
  setup(2, {});
  EXPECT_NE(nullptr, handle_);

  Common::Redis::RespValue expected_error;
  expected_error.type(Common::Redis::RespType::Error);
  expected_error.asString() = "all responses not same";

  pool_callbacks_[0]->onResponse(okResponse());

  // Second shard returns different response
  auto different_response = std::make_unique<Common::Redis::RespValue>();
  different_response->type(Common::Redis::RespType::SimpleString);
  different_response->asString() = "DIFFERENT";

  time_system_.setMonotonicTime(std::chrono::milliseconds(5));
  EXPECT_CALL(store_, deliverHistogramToSinks(
                          Property(&Stats::Metric::name, "redis.foo.command.config.latency"), 5));
  EXPECT_CALL(callbacks_, onResponse_(PointeesEq(&expected_error)));
  pool_callbacks_[1]->onResponse(std::move(different_response));

  EXPECT_EQ(1UL, store_.counter("redis.foo.command.config.total").value());
  EXPECT_EQ(1UL, store_.counter("redis.foo.command.config.error").value());
}

TEST_F(ClusterScopeConfigTest, ConfigSetOneShardError) {
  InSequence s;
  setup(2, {});
  EXPECT_NE(nullptr, handle_);

  pool_callbacks_[0]->onResponse(okResponse());

  time_system_.setMonotonicTime(std::chrono::milliseconds(8));
  EXPECT_CALL(store_, deliverHistogramToSinks(
                          Property(&Stats::Metric::name, "redis.foo.command.config.latency"), 8));
  EXPECT_CALL(callbacks_, onResponse_(_)); // Should return the error
  pool_callbacks_[1]->onResponse(errorResponse("Configuration error"));

  EXPECT_EQ(1UL, store_.counter("redis.foo.command.config.total").value());
  EXPECT_EQ(1UL, store_.counter("redis.foo.command.config.error").value());
}

TEST_F(ClusterScopeConfigTest, ConfigSetShardFailure) {
  InSequence s;
  setup(2, {});
  EXPECT_NE(nullptr, handle_);

  pool_callbacks_[0]->onResponse(okResponse());

  time_system_.setMonotonicTime(std::chrono::milliseconds(12));
  EXPECT_CALL(store_, deliverHistogramToSinks(
                          Property(&Stats::Metric::name, "redis.foo.command.config.latency"), 12));
  EXPECT_CALL(callbacks_, onResponse_(_)); // Should return failure error
  pool_callbacks_[1]->onFailure();

  EXPECT_EQ(1UL, store_.counter("redis.foo.command.config.total").value());
  EXPECT_EQ(1UL, store_.counter("redis.foo.command.config.error").value());
}

TEST_F(ClusterScopeConfigTest, ConfigSetCheckShardCount) {
  setup(3, {});
  EXPECT_NE(nullptr, handle_);

  // Cast handle to ClusterScopeCmdRequest to access getTotalShardCount
  auto* cluster_request = dynamic_cast<ClusterScopeCmdRequest*>(handle_.get());
  ASSERT_NE(nullptr, cluster_request);

  // Verify getTotalShardCount returns the correct number of shards
  EXPECT_EQ(3UL, cluster_request->getTotalShardCount());

  // Complete the request normally
  pool_callbacks_[0]->onResponse(okResponse());
  pool_callbacks_[1]->onResponse(okResponse());

  EXPECT_CALL(callbacks_, onResponse_(_));
  pool_callbacks_[2]->onResponse(okResponse());
}

TEST_F(ClusterScopeConfigTest, ConfigSetNoUpstreamForAll) {
  Common::Redis::RespValue expected_response;
  expected_response.type(Common::Redis::RespType::Error);
  expected_response.asString() = "no upstream host";

  EXPECT_CALL(callbacks_, onResponse_(PointeesEq(&expected_response)));
  setup(0, {});
  EXPECT_EQ(nullptr, handle_);
  EXPECT_EQ(1UL, store_.counter("redis.foo.command.config.total").value());
  EXPECT_EQ(1UL, store_.counter("redis.foo.command.config.error").value());
}

TEST_F(ClusterScopeConfigTest, ConfigSetNoUpstreamForSome) {
  InSequence s;
  setup(2, {0});
  EXPECT_NE(nullptr, handle_);

  time_system_.setMonotonicTime(std::chrono::milliseconds(6));
  EXPECT_CALL(store_, deliverHistogramToSinks(
                          Property(&Stats::Metric::name, "redis.foo.command.config.latency"), 6));
  EXPECT_CALL(callbacks_, onResponse_(_));
  pool_callbacks_[1]->onResponse(okResponse());

  EXPECT_EQ(1UL, store_.counter("redis.foo.command.config.total").value());
  EXPECT_EQ(1UL, store_.counter("redis.foo.command.config.error").value());
}

TEST_F(ClusterScopeConfigTest, ConfigSetCancel) {
  InSequence s;
  setup(2, {});
  EXPECT_NE(nullptr, handle_);

  EXPECT_CALL(pool_requests_[0], cancel());
  EXPECT_CALL(pool_requests_[1], cancel());
  handle_->cancel();
}

TEST_F(ClusterScopeConfigTest, ConfigSetMirrored) {
  InSequence s;
  setupMirrorPolicy();
  setup(2, {}, true);
  EXPECT_NE(nullptr, handle_);

  Common::Redis::RespValue expected_response;
  expected_response.type(Common::Redis::RespType::SimpleString);
  expected_response.asString() = "OK";

  pool_callbacks_[0]->onResponse(okResponse());
  mirror_pool_callbacks_[0]->onResponse(okResponse());

  time_system_.setMonotonicTime(std::chrono::milliseconds(7));
  EXPECT_CALL(store_, deliverHistogramToSinks(
                          Property(&Stats::Metric::name, "redis.foo.command.config.latency"), 7));
  EXPECT_CALL(callbacks_, onResponse_(PointeesEq(&expected_response)));
  pool_callbacks_[1]->onResponse(okResponse());
  mirror_pool_callbacks_[1]->onResponse(okResponse());

  EXPECT_EQ(1UL, store_.counter("redis.foo.command.config.total").value());
  EXPECT_EQ(1UL, store_.counter("redis.foo.command.config.success").value());
}

TEST_F(ClusterScopeConfigTest, MakeRequestToShardReturnsNull) {
  // Test case where route exists but makeFragmentedRequestToShard returns null for some shards
  // This tests the condition: if (!pending_request.handle_)
  InSequence s;

  setup(3, {1}); // Setup with null_handle_indexes = {1} to mock null handle for shard 1
  EXPECT_NE(nullptr, handle_); // Request object is created and returned

  // Shards 0 and 2 should work normally, shard 1 should get NoUpstreamHost error
  // Since we have num_pending_responses_ > 0 (should be 3), the request_ptr is returned

  // Complete the successful requests from shards 0 and 2
  EXPECT_CALL(callbacks_, onResponse_(_));
  pool_callbacks_[0]->onResponse(okResponse()); // shard 0 responds OK
  pool_callbacks_[2]->onResponse(okResponse()); // shard 2 responds OK
  // pool_callbacks_[1] will be null due to null_handle_indexes = {1}
  // The pending request for shard 1 should automatically receive NoUpstreamHost error

  EXPECT_EQ(1UL, store_.counter("redis.foo.command.config.total").value());
  EXPECT_EQ(1UL, store_.counter("redis.foo.command.config.error").value());
}

TEST_F(ClusterScopeConfigTest, FailedResponseHandlerInitialization) {
  // Test case where response handler initialization fails
  // This tests the condition: if (!request_ptr->initializeResponseHandler(*incoming_request,
  // shard_size)) by using a cluster scope command with an unsupported subcommand

  Common::Redis::RespValue expected_response;
  expected_response.type(Common::Redis::RespType::Error);
  expected_response.asString() = "ERR unsupported cluster scope command or invalid arguments";

  EXPECT_CALL(callbacks_, connectionAllowed()).WillOnce(Return(true));
  EXPECT_CALL(*conn_pool_, shardSize_()).WillOnce(Return(3));
  EXPECT_CALL(callbacks_, onResponse_(PointeesEq(&expected_response)));

  Common::Redis::RespValuePtr request{new Common::Redis::RespValue()};
  std::vector<std::string> request_strings = {"config", "invalidsubcommand", "param"};
  makeBulkStringArray(*request, request_strings);

  handle_ = splitter_.makeRequest(std::move(request), callbacks_, dispatcher_, stream_info_);
  EXPECT_EQ(nullptr, handle_);

  EXPECT_EQ(1UL, store_.counter("redis.foo.command.config.total").value());
  EXPECT_EQ(1UL, store_.counter("redis.foo.command.config.error").value());
}

TEST_F(ClusterScopeConfigTest, ConfigSetNullResponseFromShard) {
  InSequence s;
  setup(2, {});
  EXPECT_NE(nullptr, handle_);

  pool_callbacks_[0]->onResponse(okResponse());

  // Send null response from second shard (simulating connection failure, timeout, etc.)
  Common::Redis::RespValuePtr null_resp;

  time_system_.setMonotonicTime(std::chrono::milliseconds(9));
  EXPECT_CALL(store_, deliverHistogramToSinks(
                          Property(&Stats::Metric::name, "redis.foo.command.config.latency"), 9));
  EXPECT_CALL(callbacks_, onResponse_(_)); // Should get "all responses not same" error
  pool_callbacks_[1]->onResponse(std::move(null_resp));

  EXPECT_EQ(1UL, store_.counter("redis.foo.command.config.total").value());
  EXPECT_EQ(1UL, store_.counter("redis.foo.command.config.error").value());
}

TEST_F(ClusterScopeConfigTest, ConfigSetFirstResponseNull) {
  InSequence s;
  setup(2, {});
  EXPECT_NE(nullptr, handle_);

  // Send NULL as FIRST response (this is the key difference)
  Common::Redis::RespValuePtr null_resp;
  pool_callbacks_[0]->onResponse(std::move(null_resp));

  Common::Redis::RespValue expected_error;
  expected_error.type(Common::Redis::RespType::Error);
  expected_error.asString() = "all responses not same";

  time_system_.setMonotonicTime(std::chrono::milliseconds(9));
  EXPECT_CALL(store_, deliverHistogramToSinks(
                          Property(&Stats::Metric::name, "redis.foo.command.config.latency"), 9));
  EXPECT_CALL(callbacks_, onResponse_(PointeesEq(&expected_error)));
  pool_callbacks_[1]->onResponse(okResponse());

  EXPECT_EQ(1UL, store_.counter("redis.foo.command.config.error").value());
}

TEST_F(ClusterScopeConfigTest, UnsupportedClusterScopeCommandNoHandler) {
  // Test a cluster scope command that doesn't have a response handler
  // This should trigger the initializeResponseHandler() failure path

  // Set up mock to return non-zero shard size so we don't hit the early exit
  EXPECT_CALL(*conn_pool_, shardSize_()).WillOnce(Return(2));

  Common::Redis::RespValue expected_error;
  expected_error.type(Common::Redis::RespType::Error);
  expected_error.asString() = "ERR unsupported cluster scope command or invalid arguments";

  EXPECT_CALL(callbacks_, connectionAllowed()).WillOnce(Return(true));
  EXPECT_CALL(callbacks_, onResponse_(PointeesEq(&expected_error)));

  Common::Redis::RespValuePtr request{new Common::Redis::RespValue()};
  makeBulkStringArray(*request, {"config", "unsupported"}); // config with unsupported subcommand

  handle_ = splitter_.makeRequest(std::move(request), callbacks_, dispatcher_, stream_info_);
  EXPECT_EQ(nullptr, handle_);

  EXPECT_EQ(1UL, store_.counter("redis.foo.command.config.total").value());
  EXPECT_EQ(1UL, store_.counter("redis.foo.command.config.error").value());
}

// Test cluster scope commands - SLOWLOG LEN (IntegerSumAggregateResponseHandler)
class ClusterScopeSlowLogLenTest : public FragmentedRequestCommandHandlerTest {
public:
  void setup(uint16_t shard_size, const std::list<uint64_t>& null_handle_indexes,
             bool mirrored = false) {
    std::vector<std::string> request_strings = {"slowlog", "len"};
    makeRequestToShard(shard_size, request_strings, null_handle_indexes, mirrored);
  }

  Common::Redis::RespValuePtr integerResponse(int64_t value) {
    auto response = std::make_unique<Common::Redis::RespValue>();
    response->type(Common::Redis::RespType::Integer);
    response->asInteger() = value;
    return response;
  }

  Common::Redis::RespValuePtr stringResponse(const std::string& value) {
    auto response = std::make_unique<Common::Redis::RespValue>();
    response->type(Common::Redis::RespType::BulkString);
    response->asString() = value;
    return response;
  }
};

TEST_F(ClusterScopeSlowLogLenTest, SlowLogLenIntegerSum) {
  InSequence s;
  setup(3, {});
  EXPECT_NE(nullptr, handle_);

  Common::Redis::RespValue expected_response;
  expected_response.type(Common::Redis::RespType::Integer);
  expected_response.asInteger() = 150; // 50 + 75 + 25

  pool_callbacks_[0]->onResponse(integerResponse(50));
  pool_callbacks_[1]->onResponse(integerResponse(75));

  time_system_.setMonotonicTime(std::chrono::milliseconds(15));
  EXPECT_CALL(store_, deliverHistogramToSinks(
                          Property(&Stats::Metric::name, "redis.foo.command.slowlog.latency"), 15));
  EXPECT_CALL(callbacks_, onResponse_(PointeesEq(&expected_response)));
  pool_callbacks_[2]->onResponse(integerResponse(25));

  EXPECT_EQ(1UL, store_.counter("redis.foo.command.slowlog.total").value());
  EXPECT_EQ(1UL, store_.counter("redis.foo.command.slowlog.success").value());
}

TEST_F(ClusterScopeSlowLogLenTest, SlowLogLenWithNegativeValues) {
  InSequence s;
  setup(2, {});
  EXPECT_NE(nullptr, handle_);

  Common::Redis::RespValue expected_error;
  expected_error.type(Common::Redis::RespType::Error);
  expected_error.asString() = "negative value received from upstream";

  pool_callbacks_[0]->onResponse(integerResponse(50));

  time_system_.setMonotonicTime(std::chrono::milliseconds(6));
  EXPECT_CALL(store_, deliverHistogramToSinks(
                          Property(&Stats::Metric::name, "redis.foo.command.slowlog.latency"), 6));
  EXPECT_CALL(callbacks_, onResponse_(PointeesEq(&expected_error)));
  pool_callbacks_[1]->onResponse(integerResponse(-10)); // Negative value should cause error

  EXPECT_EQ(1UL, store_.counter("redis.foo.command.slowlog.total").value());
  EXPECT_EQ(1UL, store_.counter("redis.foo.command.slowlog.error").value());
}

TEST_F(ClusterScopeSlowLogLenTest, SlowLogLenNonIntegerResponse) {
  InSequence s;
  setup(2, {});
  EXPECT_NE(nullptr, handle_);

  pool_callbacks_[0]->onResponse(integerResponse(50));

  time_system_.setMonotonicTime(std::chrono::milliseconds(9));
  EXPECT_CALL(store_, deliverHistogramToSinks(
                          Property(&Stats::Metric::name, "redis.foo.command.slowlog.latency"), 9));
  EXPECT_CALL(callbacks_, onResponse_(_)); // Should get error response
  pool_callbacks_[1]->onResponse(stringResponse("not a number"));

  EXPECT_EQ(1UL, store_.counter("redis.foo.command.slowlog.total").value());
  EXPECT_EQ(1UL, store_.counter("redis.foo.command.slowlog.error").value());
}

TEST_F(ClusterScopeSlowLogLenTest, SlowLogLenIntegerOverflow) {
  InSequence s;
  setup(2, {});
  EXPECT_NE(nullptr, handle_);

  // Test with very large numbers close to int64_t max
  int64_t large_value = 9223372036854775800LL; // Close to INT64_MAX

  Common::Redis::RespValue expected_response;
  expected_response.type(Common::Redis::RespType::Integer);
  expected_response.asInteger() = large_value + 5;

  pool_callbacks_[0]->onResponse(integerResponse(large_value));

  EXPECT_CALL(callbacks_, onResponse_(PointeesEq(&expected_response)));
  pool_callbacks_[1]->onResponse(integerResponse(5));

  EXPECT_EQ(1UL, store_.counter("redis.foo.command.slowlog.success").value());
}

TEST_F(ClusterScopeSlowLogLenTest, SlowLogLenNullResponse) {
  InSequence s;
  setup(2, {});
  EXPECT_NE(nullptr, handle_);

  pool_callbacks_[0]->onResponse(integerResponse(50));

  // Send nullptr response
  Common::Redis::RespValuePtr null_resp;

  time_system_.setMonotonicTime(std::chrono::milliseconds(10));
  EXPECT_CALL(store_, deliverHistogramToSinks(
                          Property(&Stats::Metric::name, "redis.foo.command.slowlog.latency"), 10));
  EXPECT_CALL(callbacks_, onResponse_(_)); // Should get "null response" error
  pool_callbacks_[1]->onResponse(std::move(null_resp));

  EXPECT_EQ(1UL, store_.counter("redis.foo.command.slowlog.total").value());
  EXPECT_EQ(1UL, store_.counter("redis.foo.command.slowlog.error").value());
}

TEST_F(ClusterScopeSlowLogLenTest, SlowLogLenWithErrorResponse) {
  InSequence s;
  setup(2, {});
  EXPECT_NE(nullptr, handle_);

  pool_callbacks_[0]->onResponse(integerResponse(50));

  // Send an error response from second shard
  Common::Redis::RespValuePtr error_resp = Common::Redis::Utility::makeError("ERR shard error");

  time_system_.setMonotonicTime(std::chrono::milliseconds(10));
  EXPECT_CALL(store_, deliverHistogramToSinks(
                          Property(&Stats::Metric::name, "redis.foo.command.slowlog.latency"), 10));
  EXPECT_CALL(callbacks_, onResponse_(_)); // Should get the error
  pool_callbacks_[1]->onResponse(std::move(error_resp));

  EXPECT_EQ(1UL, store_.counter("redis.foo.command.slowlog.total").value());
  EXPECT_EQ(1UL, store_.counter("redis.foo.command.slowlog.error").value());
}

// Test cluster scope commands - SLOWLOG GET (ArrayMergeAggregateResponseHandler)
class ClusterScopeSlowLogGetTest : public FragmentedRequestCommandHandlerTest {
public:
  void setup(uint16_t shard_size, const std::list<uint64_t>& null_handle_indexes,
             bool mirrored = false) {
    std::vector<std::string> request_strings = {"slowlog", "get", "5"};
    makeRequestToShard(shard_size, request_strings, null_handle_indexes, mirrored);
  }

  Common::Redis::RespValuePtr arrayResponse(const std::vector<std::string>& values) {
    auto response = std::make_unique<Common::Redis::RespValue>();
    response->type(Common::Redis::RespType::Array);
    std::vector<Common::Redis::RespValue> elements;
    for (const auto& val : values) {
      Common::Redis::RespValue elem;
      elem.type(Common::Redis::RespType::BulkString);
      elem.asString() = val;
      elements.push_back(std::move(elem));
    }
    response->asArray().swap(elements);
    return response;
  }

  Common::Redis::RespValuePtr integerResponse(int64_t value) {
    auto response = std::make_unique<Common::Redis::RespValue>();
    response->type(Common::Redis::RespType::Integer);
    response->asInteger() = value;
    return response;
  }
};

TEST_F(ClusterScopeSlowLogGetTest, SlowLogGetArrayMerge) {
  InSequence s;
  setup(2, {});
  EXPECT_NE(nullptr, handle_);

  Common::Redis::RespValue expected_response;
  expected_response.type(Common::Redis::RespType::Array);
  std::vector<Common::Redis::RespValue> elements(4);
  elements[0].type(Common::Redis::RespType::BulkString);
  elements[0].asString() = "entry1";
  elements[1].type(Common::Redis::RespType::BulkString);
  elements[1].asString() = "entry2";
  elements[2].type(Common::Redis::RespType::BulkString);
  elements[2].asString() = "entry3";
  elements[3].type(Common::Redis::RespType::BulkString);
  elements[3].asString() = "entry4";
  expected_response.asArray().swap(elements);

  pool_callbacks_[0]->onResponse(arrayResponse({"entry1", "entry2"}));

  time_system_.setMonotonicTime(std::chrono::milliseconds(20));
  EXPECT_CALL(store_, deliverHistogramToSinks(
                          Property(&Stats::Metric::name, "redis.foo.command.slowlog.latency"), 20));
  EXPECT_CALL(callbacks_, onResponse_(PointeesEq(&expected_response)));
  pool_callbacks_[1]->onResponse(arrayResponse({"entry3", "entry4"}));

  EXPECT_EQ(1UL, store_.counter("redis.foo.command.slowlog.total").value());
  EXPECT_EQ(1UL, store_.counter("redis.foo.command.slowlog.success").value());
}

// Regression: cluster-scoped aggregate commands always emit a flat Array,
// even when shards return RESP3 Map. This pins the deliberate decision
// against preserving Map fidelity (see cluster_response_handler.cc comment
// for the duplicate-key reasoning). RESP2 downstream behavior is unchanged;
// RESP3 downstream sees a flat list rather than a Map of overlapping keys.
TEST_F(ClusterScopeSlowLogGetTest, ArrayMergeFromMapShardsStaysArray) {
  InSequence s;
  setup(2, {});
  EXPECT_NE(nullptr, handle_);

  // Build a Map shard reply: storage is flat 2*N k/v pairs.
  auto mapResponse = [](const std::vector<std::pair<std::string, std::string>>& kvs) {
    auto response = std::make_unique<Common::Redis::RespValue>();
    response->type(Common::Redis::RespType::Map);
    std::vector<Common::Redis::RespValue> elements;
    elements.reserve(kvs.size() * 2);
    for (const auto& kv : kvs) {
      Common::Redis::RespValue k;
      k.type(Common::Redis::RespType::BulkString);
      k.asString() = kv.first;
      Common::Redis::RespValue v;
      v.type(Common::Redis::RespType::BulkString);
      v.asString() = kv.second;
      elements.push_back(std::move(k));
      elements.push_back(std::move(v));
    }
    response->asArray().swap(elements);
    return response;
  };

  // Two shards each return Map with overlapping keys. The merged output
  // must be Array (not Map) and must contain ALL k/v pairs flat — Map
  // would silently drop duplicates.
  Common::Redis::RespValue expected_response;
  expected_response.type(Common::Redis::RespType::Array);
  std::vector<Common::Redis::RespValue> expected(8);
  const std::vector<std::pair<std::string, std::string>> all_kvs = {
      {"maxmemory", "100mb"},
      {"timeout", "0"},
      {"maxmemory", "200mb"},
      {"timeout", "60"},
  };
  for (size_t i = 0; i < all_kvs.size(); ++i) {
    expected[2 * i].type(Common::Redis::RespType::BulkString);
    expected[2 * i].asString() = all_kvs[i].first;
    expected[2 * i + 1].type(Common::Redis::RespType::BulkString);
    expected[2 * i + 1].asString() = all_kvs[i].second;
  }
  expected_response.asArray().swap(expected);

  pool_callbacks_[0]->onResponse(mapResponse({{"maxmemory", "100mb"}, {"timeout", "0"}}));
  time_system_.setMonotonicTime(std::chrono::milliseconds(20));
  EXPECT_CALL(store_, deliverHistogramToSinks(
                          Property(&Stats::Metric::name, "redis.foo.command.slowlog.latency"), 20));
  EXPECT_CALL(callbacks_, onResponse_(PointeesEq(&expected_response)));
  pool_callbacks_[1]->onResponse(mapResponse({{"maxmemory", "200mb"}, {"timeout", "60"}}));

  EXPECT_EQ(1UL, store_.counter("redis.foo.command.slowlog.total").value());
  EXPECT_EQ(1UL, store_.counter("redis.foo.command.slowlog.success").value());
}

TEST_F(ClusterScopeSlowLogGetTest, SlowLogGetEmptyArrays) {
  InSequence s;
  setup(2, {});
  EXPECT_NE(nullptr, handle_);

  Common::Redis::RespValue expected_response;
  expected_response.type(Common::Redis::RespType::Array);
  // Empty array expected

  pool_callbacks_[0]->onResponse(arrayResponse({}));

  EXPECT_CALL(callbacks_, onResponse_(PointeesEq(&expected_response)));
  pool_callbacks_[1]->onResponse(arrayResponse({}));

  EXPECT_EQ(1UL, store_.counter("redis.foo.command.slowlog.success").value());
}

TEST_F(ClusterScopeSlowLogGetTest, SlowLogGetNonArrayResponse) {
  InSequence s;
  setup(2, {});
  EXPECT_NE(nullptr, handle_);

  pool_callbacks_[0]->onResponse(arrayResponse({"entry1"}));

  time_system_.setMonotonicTime(std::chrono::milliseconds(13));
  EXPECT_CALL(store_, deliverHistogramToSinks(
                          Property(&Stats::Metric::name, "redis.foo.command.slowlog.latency"), 13));
  EXPECT_CALL(callbacks_, onResponse_(_));              // Should get error response
  pool_callbacks_[1]->onResponse(integerResponse(123)); // Non-array response

  EXPECT_EQ(1UL, store_.counter("redis.foo.command.slowlog.total").value());
  EXPECT_EQ(1UL, store_.counter("redis.foo.command.slowlog.error").value());
}

TEST_F(ClusterScopeSlowLogGetTest, SlowLogGetNullResponse) {
  InSequence s;
  setup(2, {});
  EXPECT_NE(nullptr, handle_);

  pool_callbacks_[0]->onResponse(arrayResponse({"entry1"}));

  // Send nullptr response
  Common::Redis::RespValuePtr null_resp;

  time_system_.setMonotonicTime(std::chrono::milliseconds(12));
  EXPECT_CALL(store_, deliverHistogramToSinks(
                          Property(&Stats::Metric::name, "redis.foo.command.slowlog.latency"), 12));
  EXPECT_CALL(callbacks_, onResponse_(_)); // Should get error
  pool_callbacks_[1]->onResponse(std::move(null_resp));

  EXPECT_EQ(1UL, store_.counter("redis.foo.command.slowlog.total").value());
  EXPECT_EQ(1UL, store_.counter("redis.foo.command.slowlog.error").value());
}

TEST_F(ClusterScopeSlowLogGetTest, SlowLogGetWithErrorResponse) {
  InSequence s;
  setup(2, {});
  EXPECT_NE(nullptr, handle_);

  pool_callbacks_[0]->onResponse(arrayResponse({"entry1"}));

  // Send an error response from second shard
  Common::Redis::RespValuePtr error_resp = Common::Redis::Utility::makeError("ERR shard error");

  time_system_.setMonotonicTime(std::chrono::milliseconds(12));
  EXPECT_CALL(store_, deliverHistogramToSinks(
                          Property(&Stats::Metric::name, "redis.foo.command.slowlog.latency"), 12));
  EXPECT_CALL(callbacks_, onResponse_(_));
  pool_callbacks_[1]->onResponse(std::move(error_resp));

  EXPECT_EQ(1UL, store_.counter("redis.foo.command.slowlog.total").value());
  EXPECT_EQ(1UL, store_.counter("redis.foo.command.slowlog.error").value());
}

// Test unsupported cluster scope command
TEST_F(RedisCommandSplitterImplTest, UnsupportedClusterScopeCommand) {
  Common::Redis::RespValue response;
  response.type(Common::Redis::RespType::Error);
  response.asString() = "ERR cluster subcommand 'reset' is not supported";

  EXPECT_CALL(callbacks_, connectionAllowed()).WillOnce(Return(true));
  EXPECT_CALL(callbacks_, onResponse_(PointeesEq(&response)));
  Common::Redis::RespValuePtr request{new Common::Redis::RespValue()};
  makeBulkStringArray(*request, {"cluster", "reset"});
  EXPECT_EQ(nullptr,
            splitter_.makeRequest(std::move(request), callbacks_, dispatcher_, stream_info_));

  EXPECT_EQ(1UL, store_.counter("redis.foo.command.cluster.error").value());
}

// Test edge cases
TEST_F(RedisCommandSplitterImplTest, ClusterCommandWithoutSubcommand) {
  InSequence s;

  Common::Redis::RespValue response;
  response.type(Common::Redis::RespType::Error);
  response.asString() = "invalid request";

  EXPECT_CALL(callbacks_, connectionAllowed()).WillOnce(Return(true));
  EXPECT_CALL(callbacks_, onResponse_(PointeesEq(&response)));
  Common::Redis::RespValuePtr request{new Common::Redis::RespValue()};
  makeBulkStringArray(*request, {"cluster"}); // Missing subcommand
  EXPECT_EQ(nullptr,
            splitter_.makeRequest(std::move(request), callbacks_, dispatcher_, stream_info_));
}

TEST_F(RedisCommandSplitterImplTest, ClusterScopeCommandInvalidArgs) {
  Common::Redis::RespValue response;
  response.type(Common::Redis::RespType::Error);
  response.asString() = Response::get().InvalidRequest;

  EXPECT_CALL(callbacks_, connectionAllowed()).WillOnce(Return(true));
  EXPECT_CALL(callbacks_, onResponse_(PointeesEq(&response)));
  Common::Redis::RespValuePtr request{new Common::Redis::RespValue()};
  makeBulkStringArray(*request, {"config"}); // Missing subcommand
  EXPECT_EQ(nullptr,
            splitter_.makeRequest(std::move(request), callbacks_, dispatcher_, stream_info_));
}

// ===== FRAMEWORK INTEGRATION AND INTEGRITY TESTS =====

// Test ClusterResponseHandlerFactory integration
class ClusterResponseHandlerFactoryTest : public testing::Test {
public:
  std::unique_ptr<Common::Redis::RespValue> makeRequest(const std::string& command,
                                                        const std::string& subcommand = "") {
    auto request = std::make_unique<Common::Redis::RespValue>();
    request->type(Common::Redis::RespType::Array);
    std::vector<Common::Redis::RespValue> elements;

    Common::Redis::RespValue cmd;
    cmd.type(Common::Redis::RespType::BulkString);
    cmd.asString() = command;
    elements.push_back(std::move(cmd));

    if (!subcommand.empty()) {
      Common::Redis::RespValue subcmd;
      subcmd.type(Common::Redis::RespType::BulkString);
      subcmd.asString() = subcommand;
      elements.push_back(std::move(subcmd));
    }

    request->asArray().swap(elements);
    return request;
  }
};

TEST_F(ClusterResponseHandlerFactoryTest, CreateAllshardSameHandlers) {
  // Test all commands that should use AllshardSameResponseHandler
  std::vector<std::pair<std::string, std::string>> same_response_commands = {
      {"config", "set"}, {"config", "rewrite"}, {"config", "resetstat"}, {"flushall", ""},
      {"flushdb", ""},   {"script", "flush"},   {"script", "kill"},      {"slowlog", "reset"}};

  for (const auto& cmd_pair : same_response_commands) {
    auto handler = ClusterResponseHandlerFactory::createFromRequest(
        *makeRequest(cmd_pair.first, cmd_pair.second), 3);

    EXPECT_NE(nullptr, handler) << "Handler should be created for " << cmd_pair.first
                                << (cmd_pair.second.empty() ? "" : " " + cmd_pair.second);

    // Verify it's the correct type by testing behavior
    // AllshardSameResponseHandler should be created
  }
}

TEST_F(ClusterResponseHandlerFactoryTest, CreateIntegerSumHandlers) {
  // Test commands that should use IntegerSumAggregateResponseHandler
  std::vector<std::pair<std::string, std::string>> integer_sum_commands = {{"slowlog", "len"}};

  for (const auto& cmd_pair : integer_sum_commands) {
    auto handler = ClusterResponseHandlerFactory::createFromRequest(
        *makeRequest(cmd_pair.first, cmd_pair.second), 3);

    EXPECT_NE(nullptr, handler) << "Handler should be created for " << cmd_pair.first << " "
                                << cmd_pair.second;
  }
}

TEST_F(ClusterResponseHandlerFactoryTest, CreateArrayMergeHandlers) {
  // Test commands that should use ArrayMergeAggregateResponseHandler
  std::vector<std::pair<std::string, std::string>> array_merge_commands = {{"slowlog", "get"},
                                                                           {"config", "get"}};

  for (const auto& cmd_pair : array_merge_commands) {
    auto handler = ClusterResponseHandlerFactory::createFromRequest(
        *makeRequest(cmd_pair.first, cmd_pair.second), 3);

    EXPECT_NE(nullptr, handler) << "Handler should be created for " << cmd_pair.first << " "
                                << cmd_pair.second;
  }
}

TEST_F(ClusterResponseHandlerFactoryTest, InvalidRequestTypes) {
  // Test case 1: Request is not an Array (RespType::BulkString)
  auto string_request = std::make_unique<Common::Redis::RespValue>();
  string_request->type(Common::Redis::RespType::BulkString);
  string_request->asString() = "config";

  auto handler1 = ClusterResponseHandlerFactory::createFromRequest(*string_request, 3);
  EXPECT_EQ(nullptr, handler1) << "Handler should NOT be created for non-array request";

  // Test case 2: Request is not an Array (RespType::Integer)
  auto integer_request = std::make_unique<Common::Redis::RespValue>();
  integer_request->type(Common::Redis::RespType::Integer);
  integer_request->asInteger() = 42;

  auto handler2 = ClusterResponseHandlerFactory::createFromRequest(*integer_request, 3);
  EXPECT_EQ(nullptr, handler2) << "Handler should NOT be created for integer request";

  // Test case 3: Request is not an Array (RespType::Error)
  auto error_request = std::make_unique<Common::Redis::RespValue>();
  error_request->type(Common::Redis::RespType::Error);
  error_request->asString() = "ERR some error";

  auto handler3 = ClusterResponseHandlerFactory::createFromRequest(*error_request, 3);
  EXPECT_EQ(nullptr, handler3) << "Handler should NOT be created for error request";
}

TEST_F(ClusterResponseHandlerFactoryTest, EmptyArrayRequest) {
  // Test case: Request is an Array but empty
  auto empty_request = std::make_unique<Common::Redis::RespValue>();
  empty_request->type(Common::Redis::RespType::Array);
  // asArray() is empty by default

  auto handler = ClusterResponseHandlerFactory::createFromRequest(*empty_request, 3);
  EXPECT_EQ(nullptr, handler) << "Handler should NOT be created for empty array request";
}

TEST_F(ClusterResponseHandlerFactoryTest, UnsupportedCommands) {
  // Test commands that should NOT create handlers (unsupported commands)
  std::vector<std::pair<std::string, std::string>> unsupported_commands = {
      {"get", ""},                    // Regular key-based command
      {"set", ""},                    // Regular key-based command
      {"hget", ""},                   // Hash command
      {"config", "unknown"},          // Unsupported config subcommand
      {"slowlog", "invalid"},         // Unsupported slowlog subcommand
      {"unknown", ""},                // Completely unknown command
      {"randomcommand", "subcommand"} // Unknown command with subcommand
  };

  for (const auto& cmd_pair : unsupported_commands) {
    auto handler = ClusterResponseHandlerFactory::createFromRequest(
        *makeRequest(cmd_pair.first, cmd_pair.second), 3);

    EXPECT_EQ(nullptr, handler) << "Handler should NOT be created for unsupported command: "
                                << cmd_pair.first
                                << (cmd_pair.second.empty() ? "" : " " + cmd_pair.second);
  }
}

TEST_F(ClusterResponseHandlerFactoryTest, SingleCommandNoSubcommand) {
  // Test commands without subcommands (single element arrays)
  std::vector<std::string> single_commands = {"flushall", "flushdb"};

  for (const auto& command : single_commands) {
    auto handler = ClusterResponseHandlerFactory::createFromRequest(*makeRequest(command, ""), 3);

    EXPECT_NE(nullptr, handler) << "Handler should be created for single command: " << command;
  }
}

// Test command routing integration
class ClusterScopeCommandRoutingTest : public RedisCommandSplitterImplTest {
public:
  void testCommandRouting(const std::string& command, const std::string& subcommand,
                          bool should_create_handler) {
    Common::Redis::RespValuePtr request{new Common::Redis::RespValue()};
    std::vector<std::string> cmd_args = {command};
    if (!subcommand.empty()) {
      cmd_args.push_back(subcommand);
    }
    makeBulkStringArray(*request, cmd_args);

    if (should_create_handler) {
      // For supported cluster scope commands, we expect the request to be handled
      EXPECT_CALL(callbacks_, connectionAllowed()).WillOnce(Return(true));
      EXPECT_CALL(*conn_pool_, shardSize_()).WillOnce(Return(2));

      ConnPool::PoolCallbacks* pool_callback1;
      ConnPool::PoolCallbacks* pool_callback2;
      Common::Redis::Client::MockPoolRequest pool_request1;
      Common::Redis::Client::MockPoolRequest pool_request2;

      // Create the variant AFTER moving the request so it has the right reference
      EXPECT_CALL(*conn_pool_, makeRequestToShard_(0, _, _))
          .WillOnce(DoAll(WithArg<2>(SaveArgAddress(&pool_callback1)), Return(&pool_request1)));
      EXPECT_CALL(*conn_pool_, makeRequestToShard_(1, _, _))
          .WillOnce(DoAll(WithArg<2>(SaveArgAddress(&pool_callback2)), Return(&pool_request2)));

      auto handle =
          splitter_.makeRequest(std::move(request), callbacks_, dispatcher_, stream_info_);
      EXPECT_NE(nullptr, handle) << "Should create handle for " << command
                                 << (subcommand.empty() ? "" : " " + subcommand);

      // Clean up the handle by simulating completion
      if (handle) {
        auto response1 = std::make_unique<Common::Redis::RespValue>();
        response1->type(Common::Redis::RespType::SimpleString);
        response1->asString() = "OK";
        auto response2 = std::make_unique<Common::Redis::RespValue>();
        response2->type(Common::Redis::RespType::SimpleString);
        response2->asString() = "OK";

        EXPECT_CALL(callbacks_, onResponse_(_));
        pool_callback1->onResponse(std::move(response1));
        pool_callback2->onResponse(std::move(response2));
      }
    } else {
      // For unsupported commands, we expect an error response
      EXPECT_CALL(callbacks_, connectionAllowed()).WillOnce(Return(true));
      EXPECT_CALL(callbacks_, onResponse_(_));
      auto handle =
          splitter_.makeRequest(std::move(request), callbacks_, dispatcher_, stream_info_);
      EXPECT_EQ(nullptr, handle) << "Should NOT create handle for " << command
                                 << (subcommand.empty() ? "" : " " + subcommand);
    }
  }
};

TEST_F(ClusterScopeCommandRoutingTest, SupportedClusterScopeCommands) {
  // Test that all supported cluster scope commands are properly routed
  std::vector<std::pair<std::string, std::string>> supported_commands = {{"config", "set"},
                                                                         {"config", "get"},
                                                                         {"flushall", ""},
                                                                         {"slowlog", "len"},
                                                                         {"slowlog", "get"}};

  for (const auto& cmd_pair : supported_commands) {
    testCommandRouting(cmd_pair.first, cmd_pair.second, true);
  }
}

TEST_F(ClusterScopeCommandRoutingTest, NoShardsAvailable) {
  // Test that cluster scope commands fail gracefully when no shards are available
  InSequence s;

  Common::Redis::RespValue expected_response;
  expected_response.type(Common::Redis::RespType::Error);
  expected_response.asString() = "no upstream host";

  Common::Redis::RespValuePtr request{new Common::Redis::RespValue()};
  makeBulkStringArray(*request, {"config", "set", "maxmemory", "100mb"});

  EXPECT_CALL(callbacks_, connectionAllowed()).WillOnce(Return(true));
  EXPECT_CALL(*conn_pool_, shardSize_()).WillOnce(Return(0));
  EXPECT_CALL(callbacks_, onResponse_(PointeesEq(&expected_response)));

  auto handle = splitter_.makeRequest(std::move(request), callbacks_, dispatcher_, stream_info_);
  EXPECT_EQ(nullptr, handle);
}

TEST_F(ClusterScopeCommandRoutingTest, InvalidSubcommand) {
  // Test that CLUSTER command with invalid subcommand is rejected
  // Only "cluster" has subcommand validation: {"info", "slots", "keyslot", "nodes"}
  InSequence s;

  Common::Redis::RespValuePtr request{new Common::Redis::RespValue()};
  makeBulkStringArray(*request, {"cluster", "invalidsubcommand"});

  EXPECT_CALL(callbacks_, connectionAllowed()).WillOnce(Return(true));
  EXPECT_CALL(callbacks_, onResponse_(_));

  auto handle = splitter_.makeRequest(std::move(request), callbacks_, dispatcher_, stream_info_);
  EXPECT_EQ(nullptr, handle);
}

// Test cluster scope commands - INFO (InfoCmdAggregateResponseHandler)
class ClusterScopeInfoTest : public FragmentedRequestCommandHandlerTest {
public:
  void setup(uint16_t shard_size, const std::list<uint64_t>& null_handle_indexes,
             const std::string& section = "") {
    std::vector<std::string> request_strings = {"info"};
    if (!section.empty()) {
      request_strings.push_back(section);
    }
    makeRequestToShard(shard_size, request_strings, null_handle_indexes, false);
  }

  Common::Redis::RespValuePtr infoResponse(const std::string& content) {
    auto response = std::make_unique<Common::Redis::RespValue>();
    response->type(Common::Redis::RespType::BulkString);
    response->asString() = content;
    return response;
  }

  Common::Redis::RespValuePtr errorResponse(const std::string& error_msg) {
    return Common::Redis::Utility::makeError(error_msg);
  }
};

// Test all aggregation types: First, Sum, Max, Constant, Custom (Keyspace), PostProcess
// (human-readable) This single test covers all code paths for metric aggregation without section
// filtering
TEST_F(ClusterScopeInfoTest, InfoAggregationAllTypes) {
  InSequence s;
  setup(3, {}); // 3 shards for better Max aggregation testing
  EXPECT_NE(nullptr, handle_);

  // Comprehensive response covering all aggregation types
  std::string shard1_response =
      "# Server\r\n"
      "redis_version:7.0.0\r\n"    // First: takes first shard value
      "redis_mode:cluster\r\n"     // Constant: same across all shards
      "os:Linux 5.10.0\r\n"        // First: takes first shard value
      "arch_bits:64\r\n"           // First: takes first shard value
      "uptime_in_seconds:1000\r\n" // Max: takes maximum value
      "# Clients\r\n"
      "connected_clients:10\r\n" // Sum: adds all shards
      "# Memory\r\n"
      "used_memory:1048576\r\n"      // Sum: adds all shards
      "used_memory_human:1.00M\r\n"  // PostProcess: human-readable conversion
      "used_memory_rss:1572864\r\n"  // Sum: adds all shards
      "used_memory_peak:2097152\r\n" // Max: takes maximum value
      "maxmemory:10485760\r\n"       // Constant: same across all shards
      "# Stats\r\n"
      "total_connections_received:1000\r\n" // Sum: adds all shards
      "total_commands_processed:5000\r\n"   // Sum: adds all shards
      "keyspace_hits:3000\r\n"              // Sum: adds all shards
      "keyspace_misses:500\r\n"             // Sum: adds all shards
      "# CPU\r\n"
      "used_cpu_sys:10.5\r\n"  // Sum: adds all shards (float)
      "used_cpu_user:25.3\r\n" // Sum: adds all shards (float)
      "# Cluster\r\n"
      "cluster_enabled:1\r\n" // Constant: same across all shards
      "# Keyspace\r\n"
      "db0:keys=1000,expires=100,avg_ttl=5000\r\n"; // Custom: keyspace aggregation

  std::string shard2_response = "# Server\r\n"
                                "redis_version:7.0.1\r\n" // Different but First takes shard1
                                "redis_mode:cluster\r\n"
                                "os:Linux 5.10.0\r\n"
                                "arch_bits:64\r\n"
                                "uptime_in_seconds:2500\r\n" // Max: this is maximum
                                "# Clients\r\n"
                                "connected_clients:15\r\n"
                                "# Memory\r\n"
                                "used_memory:2097152\r\n"
                                "used_memory_human:2.00M\r\n"
                                "used_memory_rss:2621440\r\n"
                                "used_memory_peak:3145728\r\n" // Max: this is maximum
                                "maxmemory:10485760\r\n"
                                "# Stats\r\n"
                                "total_connections_received:1500\r\n"
                                "total_commands_processed:7500\r\n"
                                "keyspace_hits:4500\r\n"
                                "keyspace_misses:750\r\n"
                                "# CPU\r\n"
                                "used_cpu_sys:15.2\r\n"
                                "used_cpu_user:35.7\r\n"
                                "# Cluster\r\n"
                                "cluster_enabled:1\r\n"
                                "# Keyspace\r\n"
                                "db0:keys=1500,expires=150,avg_ttl=6000\r\n";

  std::string shard3_response = "# Server\r\n"
                                "redis_version:7.0.2\r\n"
                                "redis_mode:cluster\r\n"
                                "os:Linux 5.10.0\r\n"
                                "arch_bits:64\r\n"
                                "uptime_in_seconds:1800\r\n"
                                "# Clients\r\n"
                                "connected_clients:12\r\n"
                                "# Memory\r\n"
                                "used_memory:1572864\r\n"
                                "used_memory_human:1.50M\r\n"
                                "used_memory_rss:2097152\r\n"
                                "used_memory_peak:2621440\r\n"
                                "maxmemory:10485760\r\n"
                                "# Stats\r\n"
                                "total_connections_received:1200\r\n"
                                "total_commands_processed:6000\r\n"
                                "keyspace_hits:3500\r\n"
                                "keyspace_misses:600\r\n"
                                "# CPU\r\n"
                                "used_cpu_sys:12.8\r\n"
                                "used_cpu_user:28.5\r\n"
                                "# Cluster\r\n"
                                "cluster_enabled:1\r\n"
                                "# Keyspace\r\n"
                                "db0:keys=1200,expires=120,avg_ttl=5500\r\n";

  pool_callbacks_[0]->onResponse(infoResponse(shard1_response));
  pool_callbacks_[1]->onResponse(infoResponse(shard2_response));

  time_system_.setMonotonicTime(std::chrono::milliseconds(10));
  EXPECT_CALL(store_, deliverHistogramToSinks(
                          Property(&Stats::Metric::name, "redis.foo.command.info.latency"), 10));
  EXPECT_CALL(callbacks_, onResponse_(_));
  pool_callbacks_[2]->onResponse(infoResponse(shard3_response));

  EXPECT_EQ(1UL, store_.counter("redis.foo.command.info.total").value());
  EXPECT_EQ(1UL, store_.counter("redis.foo.command.info.success").value());
}

// Test section filtering - iterates through all major sections with same comprehensive response
// This tests shouldIncludeSection() logic with case-insensitive matching
TEST_F(ClusterScopeInfoTest, InfoSectionFiltering) {
  // All major Redis INFO sections
  std::vector<std::string> sections = {"server", "clients", "memory", "stats", "cpu", "keyspace"};

  // Comprehensive response with all sections
  std::string comprehensive_response = "# Server\r\n"
                                       "redis_version:7.0.0\r\n"
                                       "os:Linux\r\n"
                                       "# Clients\r\n"
                                       "connected_clients:10\r\n"
                                       "# Memory\r\n"
                                       "used_memory:1048576\r\n"
                                       "used_memory_human:1.00M\r\n"
                                       "# Stats\r\n"
                                       "total_commands_processed:1000\r\n"
                                       "# CPU\r\n"
                                       "used_cpu_sys:10.5\r\n"
                                       "# Keyspace\r\n"
                                       "db0:keys=1000,expires=100,avg_ttl=5000\r\n";

  for (const auto& section : sections) {
    InSequence s;

    // Test lowercase section name
    setup(2, {}, section);
    EXPECT_NE(nullptr, handle_);
    pool_callbacks_[0]->onResponse(infoResponse(comprehensive_response));
    EXPECT_CALL(callbacks_, onResponse_(_));
    pool_callbacks_[1]->onResponse(infoResponse(comprehensive_response));

    // Test uppercase section name (case-insensitive)
    std::string uppercase_section = section;
    std::transform(uppercase_section.begin(), uppercase_section.end(), uppercase_section.begin(),
                   ::toupper);
    setup(2, {}, uppercase_section);
    EXPECT_NE(nullptr, handle_);
    pool_callbacks_[0]->onResponse(infoResponse(comprehensive_response));
    EXPECT_CALL(callbacks_, onResponse_(_));
    pool_callbacks_[1]->onResponse(infoResponse(comprehensive_response));
  }

  // Test empty section (should include all sections)
  {
    InSequence s;
    setup(2, {}, "");
    EXPECT_NE(nullptr, handle_);
    pool_callbacks_[0]->onResponse(infoResponse(comprehensive_response));
    EXPECT_CALL(callbacks_, onResponse_(_));
    pool_callbacks_[1]->onResponse(infoResponse(comprehensive_response));
  }
}

// Test error handling - non-bulk-string response
TEST_F(ClusterScopeInfoTest, InfoNonBulkStringResponse) {
  InSequence s;
  setup(2, {});
  EXPECT_NE(nullptr, handle_);

  std::string shard1_response = "# Server\r\nredis_version:7.0.0\r\n";
  pool_callbacks_[0]->onResponse(infoResponse(shard1_response));

  // Second shard returns non-bulk-string
  auto invalid_response = std::make_unique<Common::Redis::RespValue>();
  invalid_response->type(Common::Redis::RespType::Integer);
  invalid_response->asInteger() = 123;

  time_system_.setMonotonicTime(std::chrono::milliseconds(8));
  EXPECT_CALL(store_, deliverHistogramToSinks(
                          Property(&Stats::Metric::name, "redis.foo.command.info.latency"), 8));
  EXPECT_CALL(callbacks_, onResponse_(_));
  pool_callbacks_[1]->onResponse(std::move(invalid_response));

  EXPECT_EQ(1UL, store_.counter("redis.foo.command.info.total").value());
  EXPECT_EQ(1UL, store_.counter("redis.foo.command.info.error").value());
}

// Test error handling - null response
TEST_F(ClusterScopeInfoTest, InfoNullResponse) {
  InSequence s;
  setup(2, {});
  EXPECT_NE(nullptr, handle_);

  std::string shard1_response = "# Server\r\nredis_version:7.0.0\r\n";
  pool_callbacks_[0]->onResponse(infoResponse(shard1_response));

  Common::Redis::RespValuePtr null_resp;

  time_system_.setMonotonicTime(std::chrono::milliseconds(10));
  EXPECT_CALL(store_, deliverHistogramToSinks(
                          Property(&Stats::Metric::name, "redis.foo.command.info.latency"), 10));
  EXPECT_CALL(callbacks_, onResponse_(_));
  pool_callbacks_[1]->onResponse(std::move(null_resp));

  EXPECT_EQ(1UL, store_.counter("redis.foo.command.info.error").value());
}

// Test error handling - error response from shard
TEST_F(ClusterScopeInfoTest, InfoErrorResponse) {
  InSequence s;
  setup(2, {});
  EXPECT_NE(nullptr, handle_);

  std::string shard1_response = "# Server\r\nredis_version:7.0.0\r\n";
  pool_callbacks_[0]->onResponse(infoResponse(shard1_response));

  time_system_.setMonotonicTime(std::chrono::milliseconds(12));
  EXPECT_CALL(store_, deliverHistogramToSinks(
                          Property(&Stats::Metric::name, "redis.foo.command.info.latency"), 12));
  EXPECT_CALL(callbacks_, onResponse_(_));
  pool_callbacks_[1]->onResponse(errorResponse("ERR internal error"));

  EXPECT_EQ(1UL, store_.counter("redis.foo.command.info.error").value());
}

// Test shard failure
TEST_F(ClusterScopeInfoTest, InfoShardFailure) {
  InSequence s;
  setup(2, {});
  EXPECT_NE(nullptr, handle_);

  std::string shard1_response = "# Server\r\nredis_version:7.0.0\r\n";
  pool_callbacks_[0]->onResponse(infoResponse(shard1_response));

  time_system_.setMonotonicTime(std::chrono::milliseconds(15));
  EXPECT_CALL(store_, deliverHistogramToSinks(
                          Property(&Stats::Metric::name, "redis.foo.command.info.latency"), 15));
  EXPECT_CALL(callbacks_, onResponse_(_));
  pool_callbacks_[1]->onFailure();

  EXPECT_EQ(1UL, store_.counter("redis.foo.command.info.error").value());
}

// Test cancel operation
TEST_F(ClusterScopeInfoTest, InfoCancel) {
  InSequence s;
  setup(3, {});
  EXPECT_NE(nullptr, handle_);

  EXPECT_CALL(pool_requests_[0], cancel());
  EXPECT_CALL(pool_requests_[1], cancel());
  EXPECT_CALL(pool_requests_[2], cancel());
  handle_->cancel();
}

// Test no upstream hosts
TEST_F(ClusterScopeInfoTest, InfoNoUpstream) {
  Common::Redis::RespValue expected_response;
  expected_response.type(Common::Redis::RespType::Error);
  expected_response.asString() = "no upstream host";

  EXPECT_CALL(callbacks_, onResponse_(PointeesEq(&expected_response)));
  setup(0, {});
  EXPECT_EQ(nullptr, handle_);
  EXPECT_EQ(1UL, store_.counter("redis.foo.command.info.total").value());
  EXPECT_EQ(1UL, store_.counter("redis.foo.command.info.error").value());
}

// Test bytesToHuman conversion for all size ranges and proper metric aggregation
TEST_F(ClusterScopeInfoTest, InfoBytesToHumanAllSizes) {
  InSequence s;
  setup(2, {}); // Use 2 shards to properly test aggregation
  EXPECT_NE(nullptr, handle_);

  // Shard 1: Test various size ranges covering all bytesToHuman branches
  std::string shard1_response =
      "# Memory\r\n"
      "used_memory:512\r\n"                        // Bytes: 512B (Sum aggregation)
      "used_memory_rss:2048\r\n"                   // KB: 2.00K (Sum aggregation)
      "used_memory_peak:5242880\r\n"               // MB: 5.00M (Max aggregation)
      "used_memory_lua:3221225472\r\n"             // GB: 3.00G (Sum, has _human)
      "used_memory_scripts:5497558138880\r\n"      // TB: 5.00T (Sum, has _human)
      "used_memory_vm_total:6755399441055744\r\n"  // PB: 6.00P (Sum, has _human)
      "total_system_memory:576460752303423488\r\n" // EB: 0.50E (Sum, triggers else branch)
      "maxmemory:10485760\r\n";                    // Sum: total max memory

  // Shard 2: Add more values to test Sum and Max aggregation properly
  std::string shard2_response =
      "# Memory\r\n"
      "used_memory:256\r\n"                   // Sum: 512 + 256 = 768B
      "used_memory_rss:1024\r\n"              // Sum: 2048 + 1024 = 3072 = 3.00K
      "used_memory_peak:2621440\r\n"          // Max: max(5242880, 2621440) = 5.00M
      "used_memory_lua:1073741824\r\n"        // Sum: 3221225472 + 1073741824 = 4294967296 = 4.00G
      "used_memory_scripts:2748779069440\r\n" // Sum: 5497558138880 + 2748779069440 = 8246337208320
                                              // = 7.50T
      "used_memory_vm_total:3377699720527872\r\n"  // Sum: 6755399441055744 + 3377699720527872 =
                                                   // 10133099161583616 = 9.00P
      "total_system_memory:576460752303423488\r\n" // Sum: 0.5E + 0.5E = 1.00E (>= 1EB, else)
      "maxmemory:10485760\r\n";                    // Sum: 10485760 + 10485760 = 20971520

  pool_callbacks_[0]->onResponse(infoResponse(shard1_response));

  time_system_.setMonotonicTime(std::chrono::milliseconds(10));
  EXPECT_CALL(store_, deliverHistogramToSinks(
                          Property(&Stats::Metric::name, "redis.foo.command.info.latency"), 10));
  // Verify the response contains correctly formatted human-readable values
  EXPECT_CALL(callbacks_, onResponse_(_)).WillOnce([](Common::Redis::RespValuePtr& response) {
    ASSERT_NE(nullptr, response);
    ASSERT_EQ(Common::Redis::RespType::BulkString, response->type());
    std::string content = response->asString();

    // Verify _human metrics covering ALL size ranges
    EXPECT_THAT(content, testing::HasSubstr("used_memory_human:768B"));      // Bytes: 512+256
    EXPECT_THAT(content, testing::HasSubstr("used_memory_rss_human:3.00K")); // KB: 2048+1024
    EXPECT_THAT(content,
                testing::HasSubstr("used_memory_peak_human:5.00M")); // MB: max(5242880,2621440)
    EXPECT_THAT(content,
                testing::HasSubstr("used_memory_lua_human:4.00G")); // GB: 3221225472+1073741824
    EXPECT_THAT(content,
                testing::HasSubstr("used_memory_scripts_human:7.50T")); // TB: sum of values
    EXPECT_THAT(content,
                testing::HasSubstr("used_memory_vm_total_human:9.00P")); // PB: sum of values
    EXPECT_THAT(content, testing::HasSubstr(
                             "total_system_memory_human:1152921504606846976B")); // >= 1EB (else)
    EXPECT_THAT(content, testing::HasSubstr("maxmemory_human:20.00M"));          // MB: 20971520

    // Verify Sum aggregation worked for numeric values
    EXPECT_THAT(content, testing::HasSubstr("used_memory:768"));                        // 512+256
    EXPECT_THAT(content, testing::HasSubstr("used_memory_rss:3072"));                   // 2048+1024
    EXPECT_THAT(content, testing::HasSubstr("used_memory_peak:5242880"));               // max value
    EXPECT_THAT(content, testing::HasSubstr("used_memory_lua:4294967296"));             // GB sum
    EXPECT_THAT(content, testing::HasSubstr("used_memory_scripts:8246337208320"));      // TB sum
    EXPECT_THAT(content, testing::HasSubstr("used_memory_vm_total:10133099161583616")); // PB sum
    EXPECT_THAT(content, testing::HasSubstr("total_system_memory:1152921504606846976")); // EB sum
    EXPECT_THAT(content, testing::HasSubstr("maxmemory:20971520")); // 10485760+10485760
  });

  pool_callbacks_[1]->onResponse(infoResponse(shard2_response));

  EXPECT_EQ(1UL, store_.counter("redis.foo.command.info.total").value());
  EXPECT_EQ(1UL, store_.counter("redis.foo.command.info.success").value());
}

} // namespace CommandSplitter
} // namespace RedisProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
