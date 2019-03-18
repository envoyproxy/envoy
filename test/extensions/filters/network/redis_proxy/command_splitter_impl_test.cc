#include <cstdint>
#include <list>
#include <string>
#include <vector>

#include "common/common/fmt.h"
#include "common/stats/isolated_store_impl.h"

#include "extensions/filters/network/common/redis/supported_commands.h"
#include "extensions/filters/network/redis_proxy/command_splitter_impl.h"

#include "test/extensions/filters/network/common/redis/mocks.h"
#include "test/extensions/filters/network/redis_proxy/mocks.h"
#include "test/mocks/common.h"
#include "test/mocks/stats/mocks.h"
#include "test/test_common/printers.h"
#include "test/test_common/simulated_time_system.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::ByRef;
using testing::DoAll;
using testing::Eq;
using testing::InSequence;
using testing::NiceMock;
using testing::Property;
using testing::Ref;
using testing::Return;
using testing::WithArg;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace RedisProxy {
namespace CommandSplitter {

class RedisCommandSplitterImplTest : public testing::Test {
public:
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

  ConnPool::MockInstance* conn_pool_{new ConnPool::MockInstance()};
  NiceMock<Stats::MockIsolatedStatsStore> store_;
  Event::SimulatedTimeSystem time_system_;
  InstanceImpl splitter_{ConnPool::InstancePtr{conn_pool_}, store_, "redis.foo.", time_system_,
                         false};
  MockSplitCallbacks callbacks_;
  SplitRequestPtr handle_;
};

TEST_F(RedisCommandSplitterImplTest, InvalidRequestNotArray) {
  Common::Redis::RespValue response;
  response.type(Common::Redis::RespType::Error);
  response.asString() = Response::get().InvalidRequest;
  EXPECT_CALL(callbacks_, onResponse_(PointeesEq(&response)));
  Common::Redis::RespValue request;
  EXPECT_EQ(nullptr, splitter_.makeRequest(request, callbacks_));

  EXPECT_EQ(1UL, store_.counter("redis.foo.splitter.invalid_request").value());
}

TEST_F(RedisCommandSplitterImplTest, InvalidRequestArrayTooSmall) {
  Common::Redis::RespValue response;
  response.type(Common::Redis::RespType::Error);
  response.asString() = Response::get().InvalidRequest;
  EXPECT_CALL(callbacks_, onResponse_(PointeesEq(&response)));
  Common::Redis::RespValue request;
  makeBulkStringArray(request, {"incr"});
  EXPECT_EQ(nullptr, splitter_.makeRequest(request, callbacks_));

  EXPECT_EQ(1UL, store_.counter("redis.foo.splitter.invalid_request").value());
}

TEST_F(RedisCommandSplitterImplTest, InvalidRequestArrayNotStrings) {
  Common::Redis::RespValue response;
  response.type(Common::Redis::RespType::Error);
  response.asString() = Response::get().InvalidRequest;
  EXPECT_CALL(callbacks_, onResponse_(PointeesEq(&response)));
  Common::Redis::RespValue request;
  makeBulkStringArray(request, {"incr", ""});
  request.asArray()[1].type(Common::Redis::RespType::Null);
  EXPECT_EQ(nullptr, splitter_.makeRequest(request, callbacks_));

  EXPECT_EQ(1UL, store_.counter("redis.foo.splitter.invalid_request").value());
}

TEST_F(RedisCommandSplitterImplTest, UnsupportedCommand) {
  Common::Redis::RespValue response;
  response.type(Common::Redis::RespType::Error);
  response.asString() = "unsupported command 'newcommand'";
  EXPECT_CALL(callbacks_, onResponse_(PointeesEq(&response)));
  Common::Redis::RespValue request;
  makeBulkStringArray(request, {"newcommand", "hello"});
  EXPECT_EQ(nullptr, splitter_.makeRequest(request, callbacks_));

  EXPECT_EQ(1UL, store_.counter("redis.foo.splitter.unsupported_command").value());
}

class RedisSingleServerRequestTest : public RedisCommandSplitterImplTest,
                                     public testing::WithParamInterface<std::string> {
public:
  void makeRequest(const std::string& hash_key, const Common::Redis::RespValue& request) {
    EXPECT_CALL(*conn_pool_, makeRequest(hash_key, Ref(request), _))
        .WillOnce(DoAll(WithArg<2>(SaveArgAddress(&pool_callbacks_)), Return(&pool_request_)));
    handle_ = splitter_.makeRequest(request, callbacks_);
  }

  void fail() {
    Common::Redis::RespValue response;
    response.type(Common::Redis::RespType::Error);
    response.asString() = Response::get().UpstreamFailure;
    EXPECT_CALL(callbacks_, onResponse_(PointeesEq(&response)));
    pool_callbacks_->onFailure();
  }

  void respond() {
    Common::Redis::RespValuePtr response1(new Common::Redis::RespValue());
    Common::Redis::RespValue* response1_ptr = response1.get();
    EXPECT_CALL(callbacks_, onResponse_(PointeesEq(response1_ptr)));
    pool_callbacks_->onResponse(std::move(response1));
  }

  Common::Redis::Client::PoolCallbacks* pool_callbacks_;
  Common::Redis::Client::MockPoolRequest pool_request_;
};

TEST_P(RedisSingleServerRequestTest, Success) {
  InSequence s;

  ToLowerTable table;
  std::string lower_command(GetParam());
  table.toLowerCase(lower_command);

  Common::Redis::RespValue request;
  makeBulkStringArray(request, {GetParam(), "hello"});
  makeRequest("hello", request);
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

TEST_P(RedisSingleServerRequestTest, SuccessMultipleArgs) {
  InSequence s;

  Common::Redis::RespValue request;
  makeBulkStringArray(request, {GetParam(), "hello", "123", "world"});
  makeRequest("hello", request);
  EXPECT_NE(nullptr, handle_);

  ToLowerTable table;
  std::string lower_command(GetParam());
  table.toLowerCase(lower_command);

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

  Common::Redis::RespValue request;
  makeBulkStringArray(request, {GetParam(), "hello"});
  makeRequest("hello", request);
  EXPECT_NE(nullptr, handle_);

  ToLowerTable table;
  std::string lower_command(GetParam());
  table.toLowerCase(lower_command);

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

  Common::Redis::RespValue request;
  makeBulkStringArray(request, {GetParam(), "hello"});
  makeRequest("hello", request);
  EXPECT_NE(nullptr, handle_);

  EXPECT_CALL(pool_request_, cancel());
  handle_->cancel();
};

TEST_P(RedisSingleServerRequestTest, NoUpstream) {
  InSequence s;

  Common::Redis::RespValue request;
  makeBulkStringArray(request, {GetParam(), "hello"});
  EXPECT_CALL(*conn_pool_, makeRequest("hello", Ref(request), _)).WillOnce(Return(nullptr));
  Common::Redis::RespValue response;
  response.type(Common::Redis::RespType::Error);
  response.asString() = Response::get().NoUpstreamHost;
  EXPECT_CALL(callbacks_, onResponse_(PointeesEq(&response)));
  handle_ = splitter_.makeRequest(request, callbacks_);
  EXPECT_EQ(nullptr, handle_);
};

INSTANTIATE_TEST_SUITE_P(RedisSingleServerRequestTest, RedisSingleServerRequestTest,
                         testing::ValuesIn(Common::Redis::SupportedCommands::simpleCommands()));

INSTANTIATE_TEST_SUITE_P(RedisSimpleRequestCommandHandlerMixedCaseTests,
                         RedisSingleServerRequestTest, testing::Values("INCR", "inCrBY"));

TEST_F(RedisSingleServerRequestTest, PingSuccess) {
  InSequence s;

  Common::Redis::RespValue request;
  makeBulkStringArray(request, {"ping"});

  Common::Redis::RespValue response;
  response.type(Common::Redis::RespType::SimpleString);
  response.asString() = "PONG";

  EXPECT_CALL(callbacks_, onResponse_(PointeesEq(&response)));
  handle_ = splitter_.makeRequest(request, callbacks_);
  EXPECT_EQ(nullptr, handle_);
};

TEST_F(RedisSingleServerRequestTest, EvalSuccess) {
  InSequence s;

  Common::Redis::RespValue request;
  makeBulkStringArray(request, {"eval", "return {ARGV[1]}", "1", "key", "arg"});
  makeRequest("key", request);
  EXPECT_NE(nullptr, handle_);

  ToLowerTable table;
  std::string lower_command("eval");
  table.toLowerCase(lower_command);

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

  Common::Redis::RespValue request;
  makeBulkStringArray(request, {"EVALSHA", "return {ARGV[1]}", "1", "keykey", "arg"});
  makeRequest("keykey", request);
  EXPECT_NE(nullptr, handle_);

  ToLowerTable table;
  std::string lower_command("evalsha");
  table.toLowerCase(lower_command);

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

  Common::Redis::RespValue request;
  Common::Redis::RespValue response;
  response.type(Common::Redis::RespType::Error);

  response.asString() = "wrong number of arguments for 'eval' command";
  EXPECT_CALL(callbacks_, onResponse_(PointeesEq(&response)));
  makeBulkStringArray(request, {"eval", "return {ARGV[1]}"});
  EXPECT_EQ(nullptr, splitter_.makeRequest(request, callbacks_));

  response.asString() = "wrong number of arguments for 'evalsha' command";
  EXPECT_CALL(callbacks_, onResponse_(PointeesEq(&response)));
  makeBulkStringArray(request, {"evalsha", "return {ARGV[1]}", "1"});
  EXPECT_EQ(nullptr, splitter_.makeRequest(request, callbacks_));
};

TEST_F(RedisSingleServerRequestTest, EvalNoUpstream) {
  InSequence s;

  Common::Redis::RespValue request;
  makeBulkStringArray(request, {"eval", "return {ARGV[1]}", "1", "key", "arg"});
  EXPECT_CALL(*conn_pool_, makeRequest("key", Ref(request), _)).WillOnce(Return(nullptr));
  Common::Redis::RespValue response;
  response.type(Common::Redis::RespType::Error);
  response.asString() = Response::get().NoUpstreamHost;
  EXPECT_CALL(callbacks_, onResponse_(PointeesEq(&response)));
  handle_ = splitter_.makeRequest(request, callbacks_);
  EXPECT_EQ(nullptr, handle_);

  EXPECT_EQ(1UL, store_.counter("redis.foo.command.eval.total").value());
  EXPECT_EQ(1UL, store_.counter("redis.foo.command.eval.error").value());
};

class RedisMGETCommandHandlerTest : public RedisCommandSplitterImplTest {
public:
  void setup(uint32_t num_gets, const std::list<uint64_t>& null_handle_indexes) {
    std::vector<std::string> request_strings = {"mget"};
    for (uint32_t i = 0; i < num_gets; i++) {
      request_strings.push_back(std::to_string(i));
    }

    Common::Redis::RespValue request;
    makeBulkStringArray(request, request_strings);

    std::vector<Common::Redis::RespValue> tmp_expected_requests(num_gets);
    expected_requests_.swap(tmp_expected_requests);
    pool_callbacks_.resize(num_gets);
    std::vector<Common::Redis::Client::MockPoolRequest> tmp_pool_requests(num_gets);
    pool_requests_.swap(tmp_pool_requests);
    for (uint32_t i = 0; i < num_gets; i++) {
      makeBulkStringArray(expected_requests_[i], {"get", std::to_string(i)});
      Common::Redis::Client::PoolRequest* request_to_use = nullptr;
      if (std::find(null_handle_indexes.begin(), null_handle_indexes.end(), i) ==
          null_handle_indexes.end()) {
        request_to_use = &pool_requests_[i];
      }
      EXPECT_CALL(*conn_pool_, makeRequest(std::to_string(i), Eq(ByRef(expected_requests_[i])), _))
          .WillOnce(DoAll(WithArg<2>(SaveArgAddress(&pool_callbacks_[i])), Return(request_to_use)));
    }

    handle_ = splitter_.makeRequest(request, callbacks_);
  }

  std::vector<Common::Redis::RespValue> expected_requests_;
  std::vector<Common::Redis::Client::PoolCallbacks*> pool_callbacks_;
  std::vector<Common::Redis::Client::MockPoolRequest> pool_requests_;
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

  Common::Redis::RespValuePtr response2(new Common::Redis::RespValue());
  response2->type(Common::Redis::RespType::BulkString);
  response2->asString() = "5";
  pool_callbacks_[1]->onResponse(std::move(response2));

  Common::Redis::RespValuePtr response1(new Common::Redis::RespValue());
  response1->type(Common::Redis::RespType::BulkString);
  response1->asString() = "response";
  time_system_.setMonotonicTime(std::chrono::milliseconds(10));
  EXPECT_CALL(store_, deliverHistogramToSinks(
                          Property(&Stats::Metric::name, "redis.foo.command.mget.latency"), 10));
  EXPECT_CALL(callbacks_, onResponse_(PointeesEq(&expected_response)));
  pool_callbacks_[0]->onResponse(std::move(response1));

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

  Common::Redis::RespValuePtr response1(new Common::Redis::RespValue());
  response1->type(Common::Redis::RespType::BulkString);
  response1->asString() = "response";
  EXPECT_CALL(callbacks_, onResponse_(PointeesEq(&expected_response)));
  pool_callbacks_[0]->onResponse(std::move(response1));
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

  Common::Redis::RespValuePtr response1(new Common::Redis::RespValue());
  response1->type(Common::Redis::RespType::BulkString);
  response1->asString() = "response";
  time_system_.setMonotonicTime(std::chrono::milliseconds(5));
  EXPECT_CALL(store_, deliverHistogramToSinks(
                          Property(&Stats::Metric::name, "redis.foo.command.mget.latency"), 5));
  EXPECT_CALL(callbacks_, onResponse_(PointeesEq(&expected_response)));
  pool_callbacks_[0]->onResponse(std::move(response1));
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

class RedisMSETCommandHandlerTest : public RedisCommandSplitterImplTest {
public:
  void setup(uint32_t num_sets, const std::list<uint64_t>& null_handle_indexes) {
    std::vector<std::string> request_strings = {"mset"};
    for (uint32_t i = 0; i < num_sets; i++) {
      // key
      request_strings.push_back(std::to_string(i));
      // value
      request_strings.push_back(std::to_string(i));
    }

    Common::Redis::RespValue request;
    makeBulkStringArray(request, request_strings);

    std::vector<Common::Redis::RespValue> tmp_expected_requests(num_sets);
    expected_requests_.swap(tmp_expected_requests);
    pool_callbacks_.resize(num_sets);
    std::vector<Common::Redis::Client::MockPoolRequest> tmp_pool_requests(num_sets);
    pool_requests_.swap(tmp_pool_requests);
    for (uint32_t i = 0; i < num_sets; i++) {
      makeBulkStringArray(expected_requests_[i], {"set", std::to_string(i), std::to_string(i)});
      Common::Redis::Client::PoolRequest* request_to_use = nullptr;
      if (std::find(null_handle_indexes.begin(), null_handle_indexes.end(), i) ==
          null_handle_indexes.end()) {
        request_to_use = &pool_requests_[i];
      }
      EXPECT_CALL(*conn_pool_, makeRequest(std::to_string(i), Eq(ByRef(expected_requests_[i])), _))
          .WillOnce(DoAll(WithArg<2>(SaveArgAddress(&pool_callbacks_[i])), Return(request_to_use)));
    }

    handle_ = splitter_.makeRequest(request, callbacks_);
  }

  std::vector<Common::Redis::RespValue> expected_requests_;
  std::vector<Common::Redis::Client::PoolCallbacks*> pool_callbacks_;
  std::vector<Common::Redis::Client::MockPoolRequest> pool_requests_;
};

TEST_F(RedisMSETCommandHandlerTest, Normal) {
  InSequence s;

  setup(2, {});
  EXPECT_NE(nullptr, handle_);

  Common::Redis::RespValue expected_response;
  expected_response.type(Common::Redis::RespType::SimpleString);
  expected_response.asString() = Response::get().OK;

  Common::Redis::RespValuePtr response2(new Common::Redis::RespValue());
  response2->type(Common::Redis::RespType::SimpleString);
  response2->asString() = Response::get().OK;
  pool_callbacks_[1]->onResponse(std::move(response2));

  Common::Redis::RespValuePtr response1(new Common::Redis::RespValue());
  response1->type(Common::Redis::RespType::SimpleString);
  response1->asString() = Response::get().OK;

  time_system_.setMonotonicTime(std::chrono::milliseconds(10));
  EXPECT_CALL(store_, deliverHistogramToSinks(
                          Property(&Stats::Metric::name, "redis.foo.command.mset.latency"), 10));
  EXPECT_CALL(callbacks_, onResponse_(PointeesEq(&expected_response)));
  pool_callbacks_[0]->onResponse(std::move(response1));

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

  Common::Redis::RespValuePtr response2(new Common::Redis::RespValue());
  response2->type(Common::Redis::RespType::SimpleString);
  response2->asString() = Response::get().OK;
  EXPECT_CALL(callbacks_, onResponse_(PointeesEq(&expected_response)));
  pool_callbacks_[1]->onResponse(std::move(response2));
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
  EXPECT_CALL(callbacks_, onResponse_(PointeesEq(&response)));
  Common::Redis::RespValue request;
  makeBulkStringArray(request, {"mset", "foo", "bar", "fizz"});
  EXPECT_EQ(nullptr, splitter_.makeRequest(request, callbacks_));
  EXPECT_EQ(1UL, store_.counter("redis.foo.command.mset.total").value());
  EXPECT_EQ(1UL, store_.counter("redis.foo.command.mset.error").value());
};

class RedisSplitKeysSumResultHandlerTest : public RedisCommandSplitterImplTest,
                                           public testing::WithParamInterface<std::string> {
public:
  void setup(uint32_t num_commands, const std::list<uint64_t>& null_handle_indexes) {
    std::vector<std::string> request_strings = {GetParam()};
    for (uint32_t i = 0; i < num_commands; i++) {
      request_strings.push_back(std::to_string(i));
    }

    Common::Redis::RespValue request;
    makeBulkStringArray(request, request_strings);

    std::vector<Common::Redis::RespValue> tmp_expected_requests(num_commands);
    expected_requests_.swap(tmp_expected_requests);
    pool_callbacks_.resize(num_commands);
    std::vector<Common::Redis::Client::MockPoolRequest> tmp_pool_requests(num_commands);
    pool_requests_.swap(tmp_pool_requests);
    for (uint32_t i = 0; i < num_commands; i++) {
      makeBulkStringArray(expected_requests_[i], {GetParam(), std::to_string(i)});
      Common::Redis::Client::PoolRequest* request_to_use = nullptr;
      if (std::find(null_handle_indexes.begin(), null_handle_indexes.end(), i) ==
          null_handle_indexes.end()) {
        request_to_use = &pool_requests_[i];
      }
      EXPECT_CALL(*conn_pool_, makeRequest(std::to_string(i), Eq(ByRef(expected_requests_[i])), _))
          .WillOnce(DoAll(WithArg<2>(SaveArgAddress(&pool_callbacks_[i])), Return(request_to_use)));
    }

    handle_ = splitter_.makeRequest(request, callbacks_);
  }

  std::vector<Common::Redis::RespValue> expected_requests_;
  std::vector<Common::Redis::Client::PoolCallbacks*> pool_callbacks_;
  std::vector<Common::Redis::Client::MockPoolRequest> pool_requests_;
};

TEST_P(RedisSplitKeysSumResultHandlerTest, Normal) {
  InSequence s;

  setup(2, {});
  EXPECT_NE(nullptr, handle_);

  Common::Redis::RespValue expected_response;
  expected_response.type(Common::Redis::RespType::Integer);
  expected_response.asInteger() = 2;

  Common::Redis::RespValuePtr response2(new Common::Redis::RespValue());
  response2->type(Common::Redis::RespType::Integer);
  response2->asInteger() = 1;
  pool_callbacks_[1]->onResponse(std::move(response2));

  Common::Redis::RespValuePtr response1(new Common::Redis::RespValue());
  response1->type(Common::Redis::RespType::Integer);
  response1->asInteger() = 1;
  time_system_.setMonotonicTime(std::chrono::milliseconds(10));
  EXPECT_CALL(
      store_,
      deliverHistogramToSinks(
          Property(&Stats::Metric::name, "redis.foo.command." + GetParam() + ".latency"), 10));
  EXPECT_CALL(callbacks_, onResponse_(PointeesEq(&expected_response)));
  pool_callbacks_[0]->onResponse(std::move(response1));

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

  Common::Redis::RespValuePtr response2(new Common::Redis::RespValue());
  response2->type(Common::Redis::RespType::Integer);
  response2->asInteger() = 0;
  pool_callbacks_[1]->onResponse(std::move(response2));

  Common::Redis::RespValuePtr response1(new Common::Redis::RespValue());
  response1->type(Common::Redis::RespType::Integer);
  response1->asInteger() = 1;
  EXPECT_CALL(callbacks_, onResponse_(PointeesEq(&expected_response)));
  pool_callbacks_[0]->onResponse(std::move(response1));

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
  void makeRequest(const std::string& hash_key, const Common::Redis::RespValue& request) {
    EXPECT_CALL(*conn_pool_, makeRequest(hash_key, Ref(request), _))
        .WillOnce(DoAll(WithArg<2>(SaveArgAddress(&pool_callbacks_)), Return(&pool_request_)));
    handle_ = splitter_.makeRequest(request, callbacks_);
  }

  ConnPool::MockInstance* conn_pool_{new ConnPool::MockInstance()};
  InstanceImpl splitter_{ConnPool::InstancePtr{conn_pool_}, store_, "redis.foo.", time_system_,
                         true};
};

TEST_P(RedisSingleServerRequestWithLatencyMicrosTest, Success) {
  InSequence s;

  ToLowerTable table;
  std::string lower_command(GetParam());
  table.toLowerCase(lower_command);

  Common::Redis::RespValue request;
  makeBulkStringArray(request, {GetParam(), "hello"});
  makeRequest("hello", request);
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

} // namespace CommandSplitter
} // namespace RedisProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
