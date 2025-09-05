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
                         testing::Values("get", "set", "incr", "zadd"));

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

TEST_F(RedisSingleServerRequestTest, SelectWrongNumberOfArgs) {
  InSequence s;

  Common::Redis::RespValuePtr request{new Common::Redis::RespValue()};
  makeBulkStringArray(*request, {"select", "1", "2"});

  Common::Redis::RespValue response;
  response.type(Common::Redis::RespType::Error);
  response.asString() = "wrong number of arguments for 'select' command";

  EXPECT_CALL(callbacks_, connectionAllowed()).WillOnce(Return(true));
  EXPECT_CALL(callbacks_, onResponse_(PointeesEq(&response)));
  handle_ = splitter_.makeRequest(std::move(request), callbacks_, dispatcher_, stream_info_);
  EXPECT_EQ(nullptr, handle_);
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

TEST_F(RedisSingleServerRequestTest, Hello) {
  InSequence s;

  Common::Redis::RespValuePtr request{new Common::Redis::RespValue()};
  makeBulkStringArray(*request, {"hello", "2", "auth", "mypass"});

  Common::Redis::RespValue response;
  response.type(Common::Redis::RespType::Error);
  response.asString() = "ERR unknown command 'hello', with args beginning with: 2";

  EXPECT_CALL(callbacks_, onResponse_(PointeesEq(&response)));
  handle_ = splitter_.makeRequest(std::move(request), callbacks_, dispatcher_, stream_info_);
  EXPECT_EQ(nullptr, handle_);
};

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

class KeysHandlerTest : public FragmentedRequestCommandHandlerTest,
                        public testing::WithParamInterface<std::string> {
public:
  void setup(uint16_t shard_size, const std::list<uint64_t>& null_handle_indexes,
             bool mirrored = false) {
    std::vector<std::string> request_strings = {"keys", "*"};
    makeRequestToShard(shard_size, request_strings, null_handle_indexes, mirrored);
  }

  Common::Redis::RespValuePtr response() {
    Common::Redis::RespValuePtr response = std::make_unique<Common::Redis::RespValue>();
    response->type(Common::Redis::RespType::Array);
    return response;
  }
};

TEST_P(KeysHandlerTest, Normal) {
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

TEST_P(KeysHandlerTest, Mirrored) {
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

TEST_F(KeysHandlerTest, Cancel) {
  InSequence s;

  setup(2, {});
  EXPECT_NE(nullptr, handle_);

  EXPECT_CALL(pool_requests_[0], cancel());
  EXPECT_CALL(pool_requests_[1], cancel());
  handle_->cancel();
};

TEST_P(KeysHandlerTest, NormalOneZero) {
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

TEST_P(KeysHandlerTest, UpstreamError) {
  Common::Redis::RespValue expected_response;
  expected_response.type(Common::Redis::RespType::Error);
  expected_response.asString() = "finished with 2 error(s)";

  EXPECT_CALL(callbacks_, onResponse_(PointeesEq(&expected_response)));
  setup(2, {0, 1});
  EXPECT_EQ(nullptr, handle_);
  EXPECT_EQ(1UL, store_.counter("redis.foo.command." + GetParam() + ".total").value());
  EXPECT_EQ(1UL, store_.counter("redis.foo.command." + GetParam() + ".error").value());
};

TEST_P(KeysHandlerTest, NoUpstreamHostForAll) {
  Common::Redis::RespValue expected_response;
  expected_response.type(Common::Redis::RespType::Error);
  expected_response.asString() = "no upstream host";

  EXPECT_CALL(callbacks_, onResponse_(PointeesEq(&expected_response)));
  setup(0, {});
  EXPECT_EQ(nullptr, handle_);
  EXPECT_EQ(1UL, store_.counter("redis.foo.command." + GetParam() + ".total").value());
  EXPECT_EQ(1UL, store_.counter("redis.foo.command." + GetParam() + ".error").value());
};

TEST_F(KeysHandlerTest, KeysWrongNumberOfArgs) {
  InSequence s;

  Common::Redis::RespValuePtr request1{new Common::Redis::RespValue()};
  Common::Redis::RespValuePtr request2{new Common::Redis::RespValue()};
  Common::Redis::RespValue response;
  response.type(Common::Redis::RespType::Error);

  response.asString() = "wrong number of arguments for 'keys' command";
  EXPECT_CALL(callbacks_, connectionAllowed()).WillOnce(Return(true));
  EXPECT_CALL(callbacks_, onResponse_(PointeesEq(&response)));
  makeBulkStringArray(*request1, {"keys", "a*", "b*"});
  EXPECT_EQ(nullptr,
            splitter_.makeRequest(std::move(request1), callbacks_, dispatcher_, stream_info_));

  response.asString() = "invalid request";
  EXPECT_CALL(callbacks_, connectionAllowed()).WillOnce(Return(true));
  EXPECT_CALL(callbacks_, onResponse_(PointeesEq(&response)));
  makeBulkStringArray(*request2, {"keys"});
  EXPECT_EQ(nullptr,
            splitter_.makeRequest(std::move(request2), callbacks_, dispatcher_, stream_info_));
};

INSTANTIATE_TEST_SUITE_P(KeysHandlerTest, KeysHandlerTest, testing::Values("keys"));

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

class InfoHandlerTest : public FragmentedRequestCommandHandlerTest,
                        public testing::WithParamInterface<std::string> {
public:
  void setup(uint16_t shard_size, const std::list<uint64_t>& null_handle_indexes,
             bool mirrored = false, bool is_single_param = false) {
    if (is_single_param) {
      std::vector<std::string> request_strings = {"info"};
      makeRequestToShard(shard_size, request_strings, null_handle_indexes, mirrored);
      return;
    }
    std::vector<std::string> request_strings = {"info", "default"};
    makeRequestToShard(shard_size, request_strings, null_handle_indexes, mirrored);
  }

  Common::Redis::RespValuePtr response() {
    Common::Redis::RespValuePtr response = std::make_unique<Common::Redis::RespValue>();
    response->type(Common::Redis::RespType::BulkString);
    response->asString() = "# Server\r\nredis_version:6.2.6\r\n";
    return response;
  }
};

TEST_P(InfoHandlerTest, Normal) {
  InSequence s;

  setup(2, {});
  EXPECT_NE(nullptr, handle_);
  Common::Redis::RespValue expected_response;
  expected_response.type(Common::Redis::RespType::Array);
  std::vector<Common::Redis::RespValue> elements(2);
  elements[0].type(Common::Redis::RespType::BulkString);
  elements[0].asString() = "# Server\r\nredis_version:6.2.6\r\n";
  elements[1].type(Common::Redis::RespType::BulkString);
  elements[1].asString() = "# Server\r\nredis_version:6.2.6\r\n";
  expected_response.asArray().swap(elements);

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

TEST_P(InfoHandlerTest, UpstreamError) {
  Common::Redis::RespValue expected_response;
  expected_response.type(Common::Redis::RespType::Error);
  expected_response.asString() = "finished with 2 error(s)";

  EXPECT_CALL(callbacks_, onResponse_(PointeesEq(&expected_response)));
  setup(2, {0, 1});
  EXPECT_EQ(nullptr, handle_);
  EXPECT_EQ(1UL, store_.counter("redis.foo.command." + GetParam() + ".total").value());
  EXPECT_EQ(1UL, store_.counter("redis.foo.command." + GetParam() + ".error").value());
};

TEST_P(InfoHandlerTest, Mirrored) {
  InSequence s;

  setupMirrorPolicy();
  setup(2, {}, true);
  EXPECT_NE(nullptr, handle_);

  Common::Redis::RespValue expected_response;
  expected_response.type(Common::Redis::RespType::Array);
  std::vector<Common::Redis::RespValue> elements(2);
  elements[0].type(Common::Redis::RespType::BulkString);
  elements[0].asString() = "# Server\r\nredis_version:6.2.6\r\n";
  elements[1].type(Common::Redis::RespType::BulkString);
  elements[1].asString() = "# Server\r\nredis_version:6.2.6\r\n";
  expected_response.asArray().swap(elements);

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

TEST_F(InfoHandlerTest, Cancel) {
  InSequence s;

  setup(2, {});
  EXPECT_NE(nullptr, handle_);

  EXPECT_CALL(pool_requests_[0], cancel());
  EXPECT_CALL(pool_requests_[1], cancel());
  handle_->cancel();
};

TEST_P(InfoHandlerTest, NoUpstreamHostForAll) {
  Common::Redis::RespValue expected_response;
  expected_response.type(Common::Redis::RespType::Error);
  expected_response.asString() = "no upstream host";

  EXPECT_CALL(callbacks_, onResponse_(PointeesEq(&expected_response)));
  setup(0, {});
  EXPECT_EQ(nullptr, handle_);
  EXPECT_EQ(1UL, store_.counter("redis.foo.command." + GetParam() + ".total").value());
  EXPECT_EQ(1UL, store_.counter("redis.foo.command." + GetParam() + ".error").value());
};

TEST_F(InfoHandlerTest, InfoWrongNumberOfArgs) {
  InSequence s;

  Common::Redis::RespValuePtr request{new Common::Redis::RespValue()};
  Common::Redis::RespValue response;
  response.type(Common::Redis::RespType::Error);

  response.asString() = "ERR syntax error";
  EXPECT_CALL(callbacks_, connectionAllowed()).WillOnce(Return(true));
  EXPECT_CALL(callbacks_, onResponse_(PointeesEq(&response)));
  makeBulkStringArray(*request, {"info", "a", "b"});
  EXPECT_EQ(nullptr,
            splitter_.makeRequest(std::move(request), callbacks_, dispatcher_, stream_info_));
};

TEST_P(InfoHandlerTest, SingleShardErrorResponse) {
  InSequence s;

  setup(2, {});
  EXPECT_NE(nullptr, handle_);

  Common::Redis::RespValue expected_final_error_response;
  expected_final_error_response.type(Common::Redis::RespType::Error);
  expected_final_error_response.asString() = "finished with 1 error(s)";

  pool_callbacks_[0]->onResponse(response());
  Common::Redis::RespValuePtr error_resp = Common::Redis::Utility::makeError("shard error");

  EXPECT_CALL(callbacks_, onResponse_(PointeesEq(&expected_final_error_response)));
  pool_callbacks_[1]->onResponse(std::move(error_resp));

  EXPECT_EQ(1UL, store_.counter("redis.foo.command." + GetParam() + ".total").value());
  EXPECT_EQ(1UL, store_.counter("redis.foo.command." + GetParam() + ".error").value());
}

TEST_P(InfoHandlerTest, SingleShardUnexpectedResponseType) {
  InSequence s;

  setup(2, {});
  EXPECT_NE(nullptr, handle_);

  Common::Redis::RespValue expected_final_error_response;
  expected_final_error_response.type(Common::Redis::RespType::Error);
  expected_final_error_response.asString() = "finished with 1 error(s)";

  pool_callbacks_[0]->onResponse(response());

  Common::Redis::RespValuePtr unexpected_resp = std::make_unique<Common::Redis::RespValue>();
  unexpected_resp->type(Common::Redis::RespType::Integer);
  unexpected_resp->asInteger() = 123;

  EXPECT_CALL(callbacks_, onResponse_(PointeesEq(&expected_final_error_response)));
  pool_callbacks_[1]->onResponse(std::move(unexpected_resp));

  EXPECT_EQ(1UL, store_.counter("redis.foo.command." + GetParam() + ".total").value());
  EXPECT_EQ(1UL, store_.counter("redis.foo.command." + GetParam() + ".error").value());
}

TEST_P(InfoHandlerTest, SingleShardSuccessResponse) {
  InSequence s;

  setup(1, {});
  EXPECT_NE(nullptr, handle_);

  Common::Redis::RespValuePtr expected_single_response = response();

  time_system_.setMonotonicTime(std::chrono::milliseconds(5));
  EXPECT_CALL(
      store_,
      deliverHistogramToSinks(
          Property(&Stats::Metric::name, "redis.foo.command." + GetParam() + ".latency"), 5));

  EXPECT_CALL(callbacks_, onResponse_(PointeesEq(expected_single_response.get())));
  pool_callbacks_[0]->onResponse(response());

  EXPECT_EQ(1UL, store_.counter("redis.foo.command." + GetParam() + ".total").value());
  EXPECT_EQ(1UL, store_.counter("redis.foo.command." + GetParam() + ".success").value());
}

TEST_P(InfoHandlerTest, SingleShardResponseUnwrapped) {
  InSequence s;

  setup(1, {});
  EXPECT_NE(nullptr, handle_);

  Common::Redis::RespValue expected_response;
  expected_response.type(Common::Redis::RespType::BulkString);
  expected_response.asString() = "# Server\r\nredis_version:6.2.6\r\n";

  EXPECT_CALL(callbacks_, onResponse_(PointeesEq(&expected_response)));
  pool_callbacks_[0]->onResponse(response());

  EXPECT_EQ(1UL, store_.counter("redis.foo.command." + GetParam() + ".total").value());
  EXPECT_EQ(1UL, store_.counter("redis.foo.command." + GetParam() + ".success").value());
}

TEST_P(InfoHandlerTest, InfoNoArgumentAddsDefault) {
  InSequence s;

  Common::Redis::RespValuePtr request{new Common::Redis::RespValue()};
  makeBulkStringArray(*request, {"info"});

  pool_callbacks_.resize(1);
  std::vector<Common::Redis::Client::MockPoolRequest> tmp_pool_requests(1);
  pool_requests_.swap(tmp_pool_requests);

  EXPECT_CALL(callbacks_, connectionAllowed()).WillOnce(Return(true));
  std::vector<Common::Redis::Client::MockPoolRequest> dummy_requests(1);

  EXPECT_CALL(*conn_pool_, shardSize_()).WillRepeatedly(Return(1));
  ConnPool::RespVariant keys(*request);
  EXPECT_CALL(*conn_pool_, makeRequestToShard_(0, _, _))
      .WillOnce(DoAll(WithArg<2>(SaveArgAddress(&pool_callbacks_[0])), Return(&pool_requests_[0])));

  handle_ = splitter_.makeRequest(std::move(request), callbacks_, dispatcher_, stream_info_);
  EXPECT_NE(nullptr, handle_);

  Common::Redis::RespValuePtr response_from_shard = std::make_unique<Common::Redis::RespValue>();
  response_from_shard->type(Common::Redis::RespType::BulkString);
  response_from_shard->asString() = "# Server\r\nredis_version:6.2.6\r\n";

  Common::Redis::RespValue expected_final_response;
  expected_final_response.type(Common::Redis::RespType::BulkString);
  expected_final_response.asString() = "# Server\r\nredis_version:6.2.6\r\n";

  EXPECT_CALL(callbacks_, onResponse_(PointeesEq(&expected_final_response)));
  pool_callbacks_[0]->onResponse(std::move(response_from_shard));

  EXPECT_EQ(1UL, store_.counter("redis.foo.command." + GetParam() + ".total").value());
  EXPECT_EQ(1UL, store_.counter("redis.foo.command." + GetParam() + ".success").value());
}

INSTANTIATE_TEST_SUITE_P(InfoHandlerTest, InfoHandlerTest, testing::Values("info"));

class SelectHandlerTest : public FragmentedRequestCommandHandlerTest,
                          public testing::WithParamInterface<std::string> {
public:
  void setup(uint16_t shard_size, const std::list<uint64_t>& null_handle_indexes,
             bool mirrored = false) {
    std::vector<std::string> request_strings = {"select", "0"};
    makeRequestToShard(shard_size, request_strings, null_handle_indexes, mirrored);
  }

  Common::Redis::RespValuePtr response() {
    Common::Redis::RespValuePtr response = std::make_unique<Common::Redis::RespValue>();
    response->type(Common::Redis::RespType::SimpleString);
    response->asString() = "OK";
    return response;
  }
};

TEST_P(SelectHandlerTest, Normal) {
  InSequence s;

  setup(2, {});
  EXPECT_NE(nullptr, handle_);
  Common::Redis::RespValue expected_response;
  expected_response.type(Common::Redis::RespType::SimpleString);
  expected_response.asString() = "OK";
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

TEST_F(SelectHandlerTest, SelectInvalid) {
  InSequence s;

  Common::Redis::RespValuePtr request{new Common::Redis::RespValue()};
  makeBulkStringArray(*request, {"select"});

  Common::Redis::RespValue response;
  response.type(Common::Redis::RespType::Error);
  response.asString() = "ERR wrong number of arguments for 'select' command";

  EXPECT_CALL(callbacks_, connectionAllowed()).WillOnce(Return(true));
  EXPECT_CALL(callbacks_, onResponse_(PointeesEq(&response)));
  handle_ = splitter_.makeRequest(std::move(request), callbacks_, dispatcher_, stream_info_);
  EXPECT_EQ(nullptr, handle_);
};

TEST_F(SelectHandlerTest, WrongNumberOfArguments) {
  Common::Redis::RespValuePtr request{new Common::Redis::RespValue()};
  makeBulkStringArray(*request, {"select", "0", "extra_arg"});
  Common::Redis::RespValue response;
  response.type(Common::Redis::RespType::Error);
  response.asString() = "wrong number of arguments for 'select' command";

  EXPECT_CALL(callbacks_, connectionAllowed()).WillOnce(Return(true));
  EXPECT_CALL(callbacks_, onResponse_(PointeesEq(&response)));
  EXPECT_EQ(nullptr,
            splitter_.makeRequest(std::move(request), callbacks_, dispatcher_, stream_info_));
  EXPECT_EQ(1UL, store_.counter("redis.foo.command.select.error").value());
}

TEST_P(SelectHandlerTest, NoUpstreamHost) {
  Common::Redis::RespValue expected_response;
  expected_response.type(Common::Redis::RespType::Error);
  expected_response.asString() = "no upstream host";

  EXPECT_CALL(callbacks_, onResponse_(PointeesEq(&expected_response)));
  setup(0, {});
  EXPECT_EQ(nullptr, handle_);
  EXPECT_EQ(1UL, store_.counter("redis.foo.command." + GetParam() + ".total").value());
  EXPECT_EQ(1UL, store_.counter("redis.foo.command." + GetParam() + ".error").value());
}

TEST_P(SelectHandlerTest, SingleShardErrorResponse) {
  InSequence s;
  setup(2, {});
  EXPECT_NE(nullptr, handle_);

  Common::Redis::RespValue expected_error;
  expected_error.type(Common::Redis::RespType::Error);
  expected_error.asString() = "finished with 1 error(s) from select";

  pool_callbacks_[1]->onResponse(response());

  Common::Redis::RespValuePtr error_resp = Common::Redis::Utility::makeError("some error");

  EXPECT_CALL(callbacks_, onResponse_(PointeesEq(&expected_error)));
  pool_callbacks_[0]->onResponse(std::move(error_resp));
  EXPECT_EQ(1UL, store_.counter("redis.foo.command." + GetParam() + ".error").value());
}

INSTANTIATE_TEST_SUITE_P(SelectHandlerTest, SelectHandlerTest, testing::Values("select"));

class RoleHandlerTest : public FragmentedRequestCommandHandlerTest,
                        public testing::WithParamInterface<std::string> {
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
};

TEST_P(RoleHandlerTest, Normal) {
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
  EXPECT_CALL(
      store_,
      deliverHistogramToSinks(
          Property(&Stats::Metric::name, "redis.foo.command." + GetParam() + ".latency"), 10));
  EXPECT_CALL(callbacks_, onResponse_(PointeesEq(&expected_response)));
  pool_callbacks_[1]->onResponse(slaveResponse());

  EXPECT_EQ(1UL, store_.counter("redis.foo.command." + GetParam() + ".total").value());
  EXPECT_EQ(1UL, store_.counter("redis.foo.command." + GetParam() + ".success").value());
}

TEST_P(RoleHandlerTest, Mirrored) {
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
  EXPECT_CALL(
      store_,
      deliverHistogramToSinks(
          Property(&Stats::Metric::name, "redis.foo.command." + GetParam() + ".latency"), 10));
  EXPECT_CALL(callbacks_, onResponse_(PointeesEq(&expected_response)));
  pool_callbacks_[1]->onResponse(masterResponse());
  mirror_pool_callbacks_[1]->onResponse(masterResponse());

  EXPECT_EQ(1UL, store_.counter("redis.foo.command." + GetParam() + ".total").value());
  EXPECT_EQ(1UL, store_.counter("redis.foo.command." + GetParam() + ".success").value());
}

TEST_P(RoleHandlerTest, BulkStringResponse) {
  InSequence s;

  setup(1, {});
  EXPECT_NE(nullptr, handle_);

  Common::Redis::RespValue expected_response;
  expected_response.type(Common::Redis::RespType::Array);
  std::vector<Common::Redis::RespValue> elements(1);
  elements[0].type(Common::Redis::RespType::BulkString);
  elements[0].asString() = "master";
  expected_response.asArray().swap(elements);

  time_system_.setMonotonicTime(std::chrono::milliseconds(5));
  EXPECT_CALL(
      store_,
      deliverHistogramToSinks(
          Property(&Stats::Metric::name, "redis.foo.command." + GetParam() + ".latency"), 5));
  EXPECT_CALL(callbacks_, onResponse_(PointeesEq(&expected_response)));
  pool_callbacks_[0]->onResponse(bulkStringResponse());

  EXPECT_EQ(1UL, store_.counter("redis.foo.command." + GetParam() + ".total").value());
  EXPECT_EQ(1UL, store_.counter("redis.foo.command." + GetParam() + ".success").value());
}

TEST_P(RoleHandlerTest, NoUpstreamHostForAll) {
  Common::Redis::RespValue expected_response;
  expected_response.type(Common::Redis::RespType::Error);
  expected_response.asString() = "no upstream host";

  EXPECT_CALL(callbacks_, onResponse_(PointeesEq(&expected_response)));
  setup(0, {});
  EXPECT_EQ(nullptr, handle_);
  EXPECT_EQ(1UL, store_.counter("redis.foo.command." + GetParam() + ".total").value());
  EXPECT_EQ(1UL, store_.counter("redis.foo.command." + GetParam() + ".error").value());
}

TEST_P(RoleHandlerTest, NoUpstreamHostForOne) {
  InSequence s;

  setup(2, {0});
  EXPECT_NE(nullptr, handle_);

  Common::Redis::RespValue expected_response;
  expected_response.type(Common::Redis::RespType::Error);
  expected_response.asString() = "finished with 1 error(s)";

  EXPECT_CALL(callbacks_, onResponse_(PointeesEq(&expected_response)));
  pool_callbacks_[1]->onResponse(masterResponse());
  EXPECT_EQ(1UL, store_.counter("redis.foo.command." + GetParam() + ".total").value());
  EXPECT_EQ(1UL, store_.counter("redis.foo.command." + GetParam() + ".error").value());
}

TEST_P(RoleHandlerTest, UpstreamFailure) {
  InSequence s;

  setup(2, {});
  EXPECT_NE(nullptr, handle_);

  Common::Redis::RespValue expected_response;
  expected_response.type(Common::Redis::RespType::Error);
  expected_response.asString() = "finished with 1 error(s)";

  pool_callbacks_[1]->onFailure();

  time_system_.setMonotonicTime(std::chrono::milliseconds(5));
  EXPECT_CALL(
      store_,
      deliverHistogramToSinks(
          Property(&Stats::Metric::name, "redis.foo.command." + GetParam() + ".latency"), 5));
  EXPECT_CALL(callbacks_, onResponse_(PointeesEq(&expected_response)));
  pool_callbacks_[0]->onResponse(masterResponse());
  EXPECT_EQ(1UL, store_.counter("redis.foo.command." + GetParam() + ".total").value());
  EXPECT_EQ(1UL, store_.counter("redis.foo.command." + GetParam() + ".error").value());
}

TEST_P(RoleHandlerTest, InvalidUpstreamResponse) {
  InSequence s;

  setup(2, {});
  EXPECT_NE(nullptr, handle_);

  Common::Redis::RespValue expected_response;
  expected_response.type(Common::Redis::RespType::Error);
  expected_response.asString() = "finished with 1 error(s)";

  pool_callbacks_[1]->onResponse(masterResponse());

  Common::Redis::RespValuePtr invalid_response = std::make_unique<Common::Redis::RespValue>();
  invalid_response->type(Common::Redis::RespType::Integer);
  invalid_response->asInteger() = 123;

  time_system_.setMonotonicTime(std::chrono::milliseconds(10));
  EXPECT_CALL(
      store_,
      deliverHistogramToSinks(
          Property(&Stats::Metric::name, "redis.foo.command." + GetParam() + ".latency"), 10));
  EXPECT_CALL(callbacks_, onResponse_(PointeesEq(&expected_response)));
  pool_callbacks_[0]->onResponse(std::move(invalid_response));
  EXPECT_EQ(1UL, store_.counter("redis.foo.command." + GetParam() + ".total").value());
  EXPECT_EQ(1UL, store_.counter("redis.foo.command." + GetParam() + ".error").value());
}

TEST_P(RoleHandlerTest, Cancel) {
  InSequence s;

  setup(2, {});
  EXPECT_NE(nullptr, handle_);

  EXPECT_CALL(pool_requests_[0], cancel());
  EXPECT_CALL(pool_requests_[1], cancel());
  handle_->cancel();
}

TEST_F(RoleHandlerTest, RoleWithMultipleArguments) {
  InSequence s;

  Common::Redis::RespValuePtr request{new Common::Redis::RespValue()};
  makeBulkStringArray(*request, {"role", "1", "2"});

  Common::Redis::RespValue response;
  response.type(Common::Redis::RespType::Error);
  response.asString() = "ERR wrong number of arguments for 'role' command";

  EXPECT_CALL(callbacks_, connectionAllowed()).WillOnce(Return(true));
  EXPECT_CALL(callbacks_, onResponse_(PointeesEq(&response)));
  handle_ = splitter_.makeRequest(std::move(request), callbacks_, dispatcher_, stream_info_);
  EXPECT_EQ(nullptr, handle_);
}

TEST_F(RoleHandlerTest, RoleNoArgs) {
  InSequence s;

  // Test the special handling path for ROLE command in makeRequest
  Common::Redis::RespValuePtr request{new Common::Redis::RespValue()};
  makeBulkStringArray(*request, {"role"});

  // Set up the mock calls for the ROLE special handling path
  EXPECT_CALL(callbacks_, connectionAllowed()).WillOnce(Return(true));
  EXPECT_CALL(*conn_pool_, shardSize_()).WillOnce(Return(1));

  ConnPool::PoolCallbacks* pool_callback;
  Common::Redis::Client::MockPoolRequest pool_request;

  EXPECT_CALL(*conn_pool_, makeRequestToShard_(0, _, _))
      .WillOnce(DoAll(WithArg<2>(SaveArgAddress(&pool_callback)), Return(&pool_request)));

  handle_ = splitter_.makeRequest(std::move(request), callbacks_, dispatcher_, stream_info_);
  EXPECT_NE(nullptr, handle_);

  // Verify that the total counter is incremented in the special handling path
  EXPECT_EQ(1UL, store_.counter("redis.foo.command.role.total").value());

  // Complete the request successfully
  Common::Redis::RespValuePtr role_response = std::make_unique<Common::Redis::RespValue>();
  role_response->type(Common::Redis::RespType::Array);
  std::vector<Common::Redis::RespValue> elements(1);
  elements[0].type(Common::Redis::RespType::BulkString);
  elements[0].asString() = "master";
  role_response->asArray().swap(elements);

  EXPECT_CALL(callbacks_, onResponse_(_));
  pool_callback->onResponse(std::move(role_response));
}

INSTANTIATE_TEST_SUITE_P(RoleHandlerTest, RoleHandlerTest, testing::Values("role"));
} // namespace CommandSplitter
} // namespace RedisProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
