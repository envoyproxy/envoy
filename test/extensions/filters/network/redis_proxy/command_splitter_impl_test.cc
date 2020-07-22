#include <cstdint>
#include <list>
#include <string>
#include <vector>

#include "common/common/fmt.h"
#include "common/stats/isolated_store_impl.h"

#include "extensions/filters/network/common/redis/fault_impl.h"
#include "extensions/filters/network/common/redis/supported_commands.h"
#include "extensions/filters/network/redis_proxy/command_splitter_impl.h"

#include "test/extensions/filters/network/common/redis/mocks.h"
#include "test/extensions/filters/network/redis_proxy/mocks.h"
#include "test/mocks/common.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/stats/mocks.h"
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
    ON_CALL(fault_manager_, getFaultForCommand(_)).WillByDefault(Return(fault_ptr.get()));
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

  const bool latency_in_micros_;
  ConnPool::MockInstance* conn_pool_{new ConnPool::MockInstance()};
  ConnPool::InstanceSharedPtr conn_pool_shared_ptr_{conn_pool_};
  ConnPool::MockInstance* mirror_conn_pool_{new ConnPool::MockInstance()};
  ConnPool::InstanceSharedPtr mirror_conn_pool_shared_ptr_{mirror_conn_pool_};
  std::shared_ptr<NiceMock<MockRoute>> route_{new NiceMock<MockRoute>(conn_pool_shared_ptr_)};
  std::shared_ptr<NiceMock<MockRouter>> router_{new NiceMock<MockRouter>(route_)};
  NiceMock<Stats::MockIsolatedStatsStore> store_;
  NiceMock<Event::MockDispatcher> dispatcher_;
  NiceMock<MockFaultManager> fault_manager_;

  Event::SimulatedTimeSystem time_system_;
  InstanceImpl splitter_{*router_,           store_,         "redis.foo.", time_system_,
                         latency_in_micros_, fault_manager_, dispatcher_};
  MockSplitCallbacks callbacks_;
  SplitRequestPtr handle_;
};

TEST_F(RedisCommandSplitterImplTest, AuthWithNoPassword) {
  Common::Redis::RespValue response;
  response.type(Common::Redis::RespType::Error);
  response.asString() = Response::get().InvalidRequest;
  EXPECT_CALL(callbacks_, onResponse_(PointeesEq(&response)));
  Common::Redis::RespValuePtr request{new Common::Redis::RespValue()};
  makeBulkStringArray(*request, {"auth"});
  EXPECT_EQ(nullptr, splitter_.makeRequest(std::move(request), callbacks_));

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
  EXPECT_EQ(nullptr, splitter_.makeRequest(std::move(request), callbacks_));
}

TEST_F(RedisCommandSplitterImplTest, InvalidRequestNotArray) {
  Common::Redis::RespValue response;
  response.type(Common::Redis::RespType::Error);
  response.asString() = Response::get().InvalidRequest;
  EXPECT_CALL(callbacks_, onResponse_(PointeesEq(&response)));
  Common::Redis::RespValuePtr request{new Common::Redis::RespValue()};
  EXPECT_EQ(nullptr, splitter_.makeRequest(std::move(request), callbacks_));

  EXPECT_EQ(1UL, store_.counter("redis.foo.splitter.invalid_request").value());
}

TEST_F(RedisCommandSplitterImplTest, InvalidRequestEmptyArray) {
  Common::Redis::RespValue response;
  response.type(Common::Redis::RespType::Error);
  response.asString() = Response::get().InvalidRequest;
  EXPECT_CALL(callbacks_, onResponse_(PointeesEq(&response)));
  Common::Redis::RespValuePtr request{new Common::Redis::RespValue()};
  request->type(Common::Redis::RespType::Array);
  EXPECT_EQ(nullptr, splitter_.makeRequest(std::move(request), callbacks_));

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
  EXPECT_EQ(nullptr, splitter_.makeRequest(std::move(request), callbacks_));

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
  EXPECT_EQ(nullptr, splitter_.makeRequest(std::move(request), callbacks_));

  EXPECT_EQ(1UL, store_.counter("redis.foo.splitter.invalid_request").value());
}

TEST_F(RedisCommandSplitterImplTest, UnsupportedCommand) {
  Common::Redis::RespValue response;
  response.type(Common::Redis::RespType::Error);
  response.asString() = "unsupported command 'newcommand'";
  EXPECT_CALL(callbacks_, connectionAllowed()).WillOnce(Return(true));
  EXPECT_CALL(callbacks_, onResponse_(PointeesEq(&response)));
  Common::Redis::RespValuePtr request{new Common::Redis::RespValue()};
  makeBulkStringArray(*request, {"newcommand", "hello"});
  EXPECT_EQ(nullptr, splitter_.makeRequest(std::move(request), callbacks_));

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
    handle_ = splitter_.makeRequest(std::move(request), callbacks_);
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
  handle_ = splitter_.makeRequest(std::move(request), callbacks_);
  EXPECT_EQ(nullptr, handle_);
};

INSTANTIATE_TEST_SUITE_P(RedisSingleServerRequestTest, RedisSingleServerRequestTest,
                         testing::ValuesIn(Common::Redis::SupportedCommands::simpleCommands()));

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
  handle_ = splitter_.makeRequest(std::move(request), callbacks_);
  EXPECT_EQ(nullptr, handle_);
};

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
  EXPECT_EQ(nullptr, splitter_.makeRequest(std::move(request1), callbacks_));

  response.asString() = "wrong number of arguments for 'evalsha' command";
  EXPECT_CALL(callbacks_, connectionAllowed()).WillOnce(Return(true));
  EXPECT_CALL(callbacks_, onResponse_(PointeesEq(&response)));
  makeBulkStringArray(*request2, {"evalsha", "return {ARGV[1]}", "1"});
  EXPECT_EQ(nullptr, splitter_.makeRequest(std::move(request2), callbacks_));
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
  handle_ = splitter_.makeRequest(std::move(request), callbacks_);
  EXPECT_EQ(nullptr, handle_);

  EXPECT_EQ(1UL, store_.counter("redis.foo.command.eval.total").value());
  EXPECT_EQ(1UL, store_.counter("redis.foo.command.eval.error").value());
};

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

    handle_ = splitter_.makeRequest(std::move(request), callbacks_);
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
  EXPECT_EQ(nullptr, splitter_.makeRequest(std::move(request), callbacks_));
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
                         testing::ValuesIn(Common::Redis::SupportedCommands::simpleCommands()));

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
    ON_CALL(fault_manager_, getFaultForCommand(_)).WillByDefault(Return(fault_ptr_.get()));
  }
};

TEST_P(RedisSingleServerRequestWithErrorFaultTest, Fault) {
  InSequence s;

  std::string lower_command = absl::AsciiStrToLower(GetParam());
  Common::Redis::RespValuePtr request{new Common::Redis::RespValue()};
  makeBulkStringArray(*request, {GetParam(), "hello"});

  EXPECT_CALL(callbacks_, connectionAllowed()).WillOnce(Return(true));
  EXPECT_CALL(callbacks_, onResponse_(_));
  handle_ = splitter_.makeRequest(std::move(request), callbacks_);
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
    ON_CALL(fault_manager_, getFaultForCommand(_)).WillByDefault(Return(fault_ptr_.get()));
    timer_ = new NiceMock<Event::MockTimer>();
  }
};

INSTANTIATE_TEST_SUITE_P(RedisSingleServerRequestWithErrorFaultTest,
                         RedisSingleServerRequestWithErrorFaultTest,
                         testing::ValuesIn(Common::Redis::SupportedCommands::simpleCommands()));

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

  handle_ = splitter_.makeRequest(std::move(request), callbacks_);
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
                         testing::ValuesIn(Common::Redis::SupportedCommands::simpleCommands()));

class RedisSingleServerRequestWithDelayFaultTest : public RedisSingleServerRequestWithFaultTest {
public:
  RedisSingleServerRequestWithDelayFaultTest() {
    delay_ms_ = 15;
    fault_ptr_ = Common::Redis::FaultManagerImpl::makeFaultForTest(
        Common::Redis::FaultType::Delay, std::chrono::milliseconds(delay_ms_));
    ON_CALL(fault_manager_, getFaultForCommand(_)).WillByDefault(Return(fault_ptr_.get()));
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

  handle_ = splitter_.makeRequest(std::move(request), callbacks_);

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
                         testing::ValuesIn(Common::Redis::SupportedCommands::simpleCommands()));

} // namespace CommandSplitter
} // namespace RedisProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
