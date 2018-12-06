#include <chrono>
#include <cstdint>
#include <memory>
#include <string>

#include "envoy/upstream/cluster_manager.h"
#include "envoy/upstream/upstream.h"

#include "common/access_log/access_log_impl.h"
#include "common/config/filter_json.h"
#include "common/runtime/runtime_impl.h"
#include "common/runtime/uuid_util.h"

#include "test/common/stream_info/test_util.h"
#include "test/common/upstream/utility.h"
#include "test/mocks/access_log/mocks.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/filesystem/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/server/mocks.h"
#include "test/mocks/upstream/mocks.h"
#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::NiceMock;
using testing::Return;
using testing::SaveArg;

namespace Envoy {
namespace AccessLog {
namespace {

envoy::config::filter::accesslog::v2::AccessLog
parseAccessLogFromJson(const std::string& json_string) {
  envoy::config::filter::accesslog::v2::AccessLog access_log;
  auto json_object_ptr = Json::Factory::loadFromString(json_string);
  Config::FilterJson::translateAccessLog(*json_object_ptr, access_log);
  return access_log;
}

envoy::config::filter::accesslog::v2::AccessLog parseAccessLogFromV2Yaml(const std::string& yaml) {
  envoy::config::filter::accesslog::v2::AccessLog access_log;
  MessageUtil::loadFromYaml(yaml, access_log);
  return access_log;
}

class AccessLogImplTest : public testing::Test {
public:
  AccessLogImplTest() : file_(new Filesystem::MockFile()) {
    ON_CALL(context_, runtime()).WillByDefault(ReturnRef(runtime_));
    ON_CALL(context_, accessLogManager()).WillByDefault(ReturnRef(log_manager_));
    ON_CALL(log_manager_, createAccessLog(_)).WillByDefault(Return(file_));
    ON_CALL(*file_, write(_)).WillByDefault(SaveArg<0>(&output_));
  }

  Http::TestHeaderMapImpl request_headers_{{":method", "GET"}, {":path", "/"}};
  Http::TestHeaderMapImpl response_headers_;
  Http::TestHeaderMapImpl response_trailers_;
  TestStreamInfo stream_info_;
  std::shared_ptr<Filesystem::MockFile> file_;
  StringViewSaver output_;

  NiceMock<Runtime::MockLoader> runtime_;
  NiceMock<Envoy::AccessLog::MockAccessLogManager> log_manager_;
  NiceMock<Server::Configuration::MockFactoryContext> context_;
};

TEST_F(AccessLogImplTest, LogMoreData) {
  const std::string json = R"EOF(
  {
    "path": "/dev/null"
  }
  )EOF";

  InstanceSharedPtr log = AccessLogFactory::fromProto(parseAccessLogFromJson(json), context_);

  EXPECT_CALL(*file_, write(_));
  stream_info_.response_flags_ = StreamInfo::ResponseFlag::UpstreamConnectionFailure;
  request_headers_.addCopy(Http::Headers::get().UserAgent, "user-agent-set");
  request_headers_.addCopy(Http::Headers::get().RequestId, "id");
  request_headers_.addCopy(Http::Headers::get().Host, "host");
  request_headers_.addCopy(Http::Headers::get().ForwardedFor, "x.x.x.x");

  log->log(&request_headers_, &response_headers_, &response_trailers_, stream_info_);
  EXPECT_EQ("[1999-01-01T00:00:00.000Z] \"GET / HTTP/1.1\" 0 UF 1 2 3 - \"x.x.x.x\" "
            "\"user-agent-set\" \"id\" \"host\" \"-\"\n",
            output_);
}

TEST_F(AccessLogImplTest, EnvoyUpstreamServiceTime) {
  const std::string json = R"EOF(
  {
    "path": "/dev/null"
  }
  )EOF";

  InstanceSharedPtr log = AccessLogFactory::fromProto(parseAccessLogFromJson(json), context_);

  EXPECT_CALL(*file_, write(_));
  response_headers_.addCopy(Http::Headers::get().EnvoyUpstreamServiceTime, "999");

  log->log(&request_headers_, &response_headers_, &response_trailers_, stream_info_);
  EXPECT_EQ("[1999-01-01T00:00:00.000Z] \"GET / HTTP/1.1\" 0 DC 1 2 3 999 \"-\" \"-\" \"-\" \"-\" "
            "\"-\"\n",
            output_);
}

TEST_F(AccessLogImplTest, NoFilter) {
  const std::string json = R"EOF(
    {
      "path": "/dev/null"
    }
    )EOF";

  InstanceSharedPtr log = AccessLogFactory::fromProto(parseAccessLogFromJson(json), context_);

  EXPECT_CALL(*file_, write(_));
  log->log(&request_headers_, &response_headers_, &response_trailers_, stream_info_);
  EXPECT_EQ(
      "[1999-01-01T00:00:00.000Z] \"GET / HTTP/1.1\" 0 DC 1 2 3 - \"-\" \"-\" \"-\" \"-\" \"-\"\n",
      output_);
}

TEST_F(AccessLogImplTest, UpstreamHost) {
  std::shared_ptr<Upstream::MockClusterInfo> cluster{new Upstream::MockClusterInfo()};
  stream_info_.upstream_host_ = Upstream::makeTestHostDescription(cluster, "tcp://10.0.0.5:1234");

  const std::string json = R"EOF(
      {
        "path": "/dev/null"
      }
      )EOF";

  InstanceSharedPtr log = AccessLogFactory::fromProto(parseAccessLogFromJson(json), context_);

  EXPECT_CALL(*file_, write(_));
  log->log(&request_headers_, &response_headers_, &response_trailers_, stream_info_);
  EXPECT_EQ("[1999-01-01T00:00:00.000Z] \"GET / HTTP/1.1\" 0 DC 1 2 3 - \"-\" \"-\" \"-\" \"-\" "
            "\"10.0.0.5:1234\"\n",
            output_);
}

TEST_F(AccessLogImplTest, WithFilterMiss) {
  const std::string json = R"EOF(
  {
    "path": "/dev/null",
    "filter": {"type":"logical_or", "filters": [
        {"type": "status_code", "op": ">=", "value": 500},
        {"type": "duration", "op": ">=", "value": 1000000}
      ]
    }
  }
  )EOF";

  InstanceSharedPtr log = AccessLogFactory::fromProto(parseAccessLogFromJson(json), context_);

  EXPECT_CALL(*file_, write(_)).Times(0);
  log->log(&request_headers_, &response_headers_, &response_trailers_, stream_info_);

  stream_info_.response_code_ = 200;
  log->log(&request_headers_, &response_headers_, &response_trailers_, stream_info_);
}

TEST_F(AccessLogImplTest, WithFilterHit) {
  const std::string json = R"EOF(
  {
    "path": "/dev/null",
    "filter": {"type": "logical_or", "filters": [
        {"type": "status_code", "op": ">=", "value": 500},
        {"type": "status_code", "op": "=", "value": 0},
        {"type": "duration", "op": ">=", "value": 1000000}
      ]
    }
  }
  )EOF";

  InstanceSharedPtr log = AccessLogFactory::fromProto(parseAccessLogFromJson(json), context_);

  EXPECT_CALL(*file_, write(_)).Times(3);
  log->log(&request_headers_, &response_headers_, &response_trailers_, stream_info_);

  stream_info_.response_code_ = 500;
  log->log(&request_headers_, &response_headers_, &response_trailers_, stream_info_);

  stream_info_.response_code_ = 200;
  stream_info_.end_time_ =
      stream_info_.startTimeMonotonic() + std::chrono::microseconds(1001000000000000);
  log->log(&request_headers_, &response_headers_, &response_trailers_, stream_info_);
}

TEST_F(AccessLogImplTest, RuntimeFilter) {
  const std::string json = R"EOF(
  {
    "path": "/dev/null",
    "filter": {"type": "runtime", "key": "access_log.test_key"}
  }
  )EOF";

  InstanceSharedPtr log = AccessLogFactory::fromProto(parseAccessLogFromJson(json), context_);

  // Value is taken from random generator.
  EXPECT_CALL(context_.random_, random()).WillOnce(Return(42));
  EXPECT_CALL(runtime_.snapshot_, featureEnabled("access_log.test_key", 0, 42, 100))
      .WillOnce(Return(true));
  EXPECT_CALL(*file_, write(_));
  log->log(&request_headers_, &response_headers_, &response_trailers_, stream_info_);

  EXPECT_CALL(context_.random_, random()).WillOnce(Return(43));
  EXPECT_CALL(runtime_.snapshot_, featureEnabled("access_log.test_key", 0, 43, 100))
      .WillOnce(Return(false));
  EXPECT_CALL(*file_, write(_)).Times(0);
  log->log(&request_headers_, &response_headers_, &response_trailers_, stream_info_);

  // Value is taken from x-request-id.
  request_headers_.addCopy("x-request-id", "000000ff-0000-0000-0000-000000000000");
  EXPECT_CALL(runtime_.snapshot_, featureEnabled("access_log.test_key", 0, 55, 100))
      .WillOnce(Return(true));
  EXPECT_CALL(*file_, write(_));
  log->log(&request_headers_, &response_headers_, &response_trailers_, stream_info_);

  EXPECT_CALL(runtime_.snapshot_, featureEnabled("access_log.test_key", 0, 55, 100))
      .WillOnce(Return(false));
  EXPECT_CALL(*file_, write(_)).Times(0);
  log->log(&request_headers_, &response_headers_, &response_trailers_, stream_info_);
}

TEST_F(AccessLogImplTest, RuntimeFilterV2) {
  const std::string yaml = R"EOF(
name: envoy.file_access_log
filter:
  runtime_filter:
    runtime_key: access_log.test_key
    percent_sampled:
      numerator: 5
      denominator: TEN_THOUSAND
config:
  path: /dev/null
  )EOF";

  InstanceSharedPtr log = AccessLogFactory::fromProto(parseAccessLogFromV2Yaml(yaml), context_);

  // Value is taken from random generator.
  EXPECT_CALL(context_.random_, random()).WillOnce(Return(42));
  EXPECT_CALL(runtime_.snapshot_, featureEnabled("access_log.test_key", 5, 42, 10000))
      .WillOnce(Return(true));
  EXPECT_CALL(*file_, write(_));
  log->log(&request_headers_, &response_headers_, &response_trailers_, stream_info_);

  EXPECT_CALL(context_.random_, random()).WillOnce(Return(43));
  EXPECT_CALL(runtime_.snapshot_, featureEnabled("access_log.test_key", 5, 43, 10000))
      .WillOnce(Return(false));
  EXPECT_CALL(*file_, write(_)).Times(0);
  log->log(&request_headers_, &response_headers_, &response_trailers_, stream_info_);

  // Value is taken from x-request-id.
  request_headers_.addCopy("x-request-id", "000000ff-0000-0000-0000-000000000000");
  EXPECT_CALL(runtime_.snapshot_, featureEnabled("access_log.test_key", 5, 255, 10000))
      .WillOnce(Return(true));
  EXPECT_CALL(*file_, write(_));
  log->log(&request_headers_, &response_headers_, &response_trailers_, stream_info_);

  EXPECT_CALL(runtime_.snapshot_, featureEnabled("access_log.test_key", 5, 255, 10000))
      .WillOnce(Return(false));
  EXPECT_CALL(*file_, write(_)).Times(0);
  log->log(&request_headers_, &response_headers_, &response_trailers_, stream_info_);
}

TEST_F(AccessLogImplTest, RuntimeFilterV2IndependentRandomness) {
  const std::string yaml = R"EOF(
name: envoy.file_access_log
filter:
  runtime_filter:
    runtime_key: access_log.test_key
    percent_sampled:
      numerator: 5
      denominator: MILLION
    use_independent_randomness: true
config:
  path: /dev/null
  )EOF";

  InstanceSharedPtr log = AccessLogFactory::fromProto(parseAccessLogFromV2Yaml(yaml), context_);

  // Value should not be taken from x-request-id.
  request_headers_.addCopy("x-request-id", "000000ff-0000-0000-0000-000000000000");
  EXPECT_CALL(context_.random_, random()).WillOnce(Return(42));
  EXPECT_CALL(runtime_.snapshot_, featureEnabled("access_log.test_key", 5, 42, 1000000))
      .WillOnce(Return(true));
  EXPECT_CALL(*file_, write(_));
  log->log(&request_headers_, &response_headers_, &response_trailers_, stream_info_);

  EXPECT_CALL(context_.random_, random()).WillOnce(Return(43));
  EXPECT_CALL(runtime_.snapshot_, featureEnabled("access_log.test_key", 5, 43, 1000000))
      .WillOnce(Return(false));
  EXPECT_CALL(*file_, write(_)).Times(0);
  log->log(&request_headers_, &response_headers_, &response_trailers_, stream_info_);
}

TEST_F(AccessLogImplTest, PathRewrite) {
  request_headers_ = {{":method", "GET"}, {":path", "/foo"}, {"x-envoy-original-path", "/bar"}};

  const std::string json = R"EOF(
      {
        "path": "/dev/null"
      }
      )EOF";

  Json::ObjectSharedPtr loader = Json::Factory::loadFromString(json);
  InstanceSharedPtr log = AccessLogFactory::fromProto(parseAccessLogFromJson(json), context_);

  EXPECT_CALL(*file_, write(_));
  log->log(&request_headers_, &response_headers_, &response_trailers_, stream_info_);
  EXPECT_EQ("[1999-01-01T00:00:00.000Z] \"GET /bar HTTP/1.1\" 0 DC 1 2 3 - \"-\" \"-\" \"-\" \"-\" "
            "\"-\"\n",
            output_);
}

TEST_F(AccessLogImplTest, healthCheckTrue) {
  const std::string json = R"EOF(
  {
    "path": "/dev/null",
    "filter": {"type": "not_healthcheck"}
  }
  )EOF";

  InstanceSharedPtr log = AccessLogFactory::fromProto(parseAccessLogFromJson(json), context_);

  Http::TestHeaderMapImpl header_map{};
  stream_info_.hc_request_ = true;
  EXPECT_CALL(*file_, write(_)).Times(0);

  log->log(&header_map, &response_headers_, &response_trailers_, stream_info_);
}

TEST_F(AccessLogImplTest, healthCheckFalse) {
  const std::string json = R"EOF(
  {
    "path": "/dev/null",
    "filter": {"type": "not_healthcheck"}
  }
  )EOF";

  InstanceSharedPtr log = AccessLogFactory::fromProto(parseAccessLogFromJson(json), context_);

  Http::TestHeaderMapImpl header_map{};
  EXPECT_CALL(*file_, write(_));

  log->log(&request_headers_, &response_headers_, &response_trailers_, stream_info_);
}

TEST_F(AccessLogImplTest, requestTracing) {
  Runtime::RandomGeneratorImpl random;
  std::string not_traceable_guid = random.uuid();

  std::string force_tracing_guid = random.uuid();
  UuidUtils::setTraceableUuid(force_tracing_guid, UuidTraceStatus::Forced);

  std::string sample_tracing_guid = random.uuid();
  UuidUtils::setTraceableUuid(sample_tracing_guid, UuidTraceStatus::Sampled);

  const std::string json = R"EOF(
  {
    "path": "/dev/null",
    "filter": {"type": "traceable_request"}
  }
  )EOF";

  InstanceSharedPtr log = AccessLogFactory::fromProto(parseAccessLogFromJson(json), context_);

  {
    Http::TestHeaderMapImpl forced_header{{"x-request-id", force_tracing_guid}};
    EXPECT_CALL(*file_, write(_));
    log->log(&forced_header, &response_headers_, &response_trailers_, stream_info_);
  }

  {
    Http::TestHeaderMapImpl not_traceable{{"x-request-id", not_traceable_guid}};
    EXPECT_CALL(*file_, write(_)).Times(0);
    log->log(&not_traceable, &response_headers_, &response_trailers_, stream_info_);
  }

  {
    Http::TestHeaderMapImpl sampled_header{{"x-request-id", sample_tracing_guid}};
    EXPECT_CALL(*file_, write(_)).Times(0);
    log->log(&sampled_header, &response_headers_, &response_trailers_, stream_info_);
  }
}

TEST(AccessLogImplTestCtor, FiltersMissingInOrAndFilter) {
  NiceMock<Server::Configuration::MockFactoryContext> context;

  {
    const std::string json = R"EOF(
      {
        "path": "/dev/null",
        "filter": {"type": "logical_or"}
      }
    )EOF";

    EXPECT_THROW(AccessLogFactory::fromProto(parseAccessLogFromJson(json), context),
                 EnvoyException);
  }

  {
    const std::string json = R"EOF(
      {
        "path": "/dev/null",
        "filter": {"type": "logical_and"}
      }
    )EOF";

    EXPECT_THROW(AccessLogFactory::fromProto(parseAccessLogFromJson(json), context),
                 EnvoyException);
  }
}

TEST_F(AccessLogImplTest, andFilter) {
  const std::string json = R"EOF(
  {
    "path": "/dev/null",
    "filter": {"type": "logical_and", "filters": [
        {"type": "status_code", "op": ">=", "value": 500},
        {"type": "not_healthcheck"}
      ]
    }
  }
  )EOF";

  InstanceSharedPtr log = AccessLogFactory::fromProto(parseAccessLogFromJson(json), context_);
  stream_info_.response_code_ = 500;

  {
    EXPECT_CALL(*file_, write(_));
    Http::TestHeaderMapImpl header_map{{"user-agent", "NOT/Envoy/HC"}};

    log->log(&header_map, &response_headers_, &response_trailers_, stream_info_);
  }

  {
    EXPECT_CALL(*file_, write(_)).Times(0);
    Http::TestHeaderMapImpl header_map{};
    stream_info_.hc_request_ = true;
    log->log(&header_map, &response_headers_, &response_trailers_, stream_info_);
  }
}

TEST_F(AccessLogImplTest, orFilter) {
  const std::string json = R"EOF(
  {
    "path": "/dev/null",
    "filter": {"type": "logical_or", "filters": [
        {"type": "status_code", "op": ">=", "value": 500},
        {"type": "not_healthcheck"}
      ]
    }
  }
  )EOF";

  InstanceSharedPtr log = AccessLogFactory::fromProto(parseAccessLogFromJson(json), context_);
  stream_info_.response_code_ = 500;

  {
    EXPECT_CALL(*file_, write(_));
    Http::TestHeaderMapImpl header_map{{"user-agent", "NOT/Envoy/HC"}};

    log->log(&header_map, &response_headers_, &response_trailers_, stream_info_);
  }

  {
    EXPECT_CALL(*file_, write(_));
    Http::TestHeaderMapImpl header_map{{"user-agent", "Envoy/HC"}};
    log->log(&header_map, &response_headers_, &response_trailers_, stream_info_);
  }
}

TEST_F(AccessLogImplTest, multipleOperators) {
  const std::string json = R"EOF(
  {
    "path": "/dev/null",
    "filter": {"type": "logical_and", "filters": [
        {"type": "logical_or", "filters": [
            {"type": "duration", "op": ">=", "value": 10000},
            {"type": "status_code", "op": ">=", "value": 500}
          ]
        },
        {"type": "not_healthcheck"}
      ]
    }
  }
  )EOF";

  InstanceSharedPtr log = AccessLogFactory::fromProto(parseAccessLogFromJson(json), context_);
  stream_info_.response_code_ = 500;

  {
    EXPECT_CALL(*file_, write(_));
    Http::TestHeaderMapImpl header_map{};

    log->log(&header_map, &response_headers_, &response_trailers_, stream_info_);
  }

  {
    EXPECT_CALL(*file_, write(_)).Times(0);
    Http::TestHeaderMapImpl header_map{};
    stream_info_.hc_request_ = true;

    log->log(&header_map, &response_headers_, &response_trailers_, stream_info_);
  }
}

TEST(AccessLogFilterTest, DurationWithRuntimeKey) {
  std::string filter_json = R"EOF(
    {
      "filter": {"type": "duration", "op": ">=", "value": 1000000, "runtime_key": "key"}
    }
    )EOF";

  Json::ObjectSharedPtr loader = Json::Factory::loadFromString(filter_json);
  NiceMock<Runtime::MockLoader> runtime;

  Json::ObjectSharedPtr filter_object = loader->getObject("filter");
  envoy::config::filter::accesslog::v2::AccessLogFilter config;
  Config::FilterJson::translateAccessLogFilter(*filter_object, config);
  DurationFilter filter(config.duration_filter(), runtime);
  Http::TestHeaderMapImpl request_headers{{":method", "GET"}, {":path", "/"}};
  TestStreamInfo stream_info;

  stream_info.end_time_ = stream_info.startTimeMonotonic() + std::chrono::microseconds(100000);
  EXPECT_CALL(runtime.snapshot_, getInteger("key", 1000000)).WillOnce(Return(1));
  EXPECT_TRUE(filter.evaluate(stream_info, request_headers));

  EXPECT_CALL(runtime.snapshot_, getInteger("key", 1000000)).WillOnce(Return(1000));
  EXPECT_FALSE(filter.evaluate(stream_info, request_headers));

  stream_info.end_time_ =
      stream_info.startTimeMonotonic() + std::chrono::microseconds(100000001000);
  EXPECT_CALL(runtime.snapshot_, getInteger("key", 1000000)).WillOnce(Return(100000000));
  EXPECT_TRUE(filter.evaluate(stream_info, request_headers));

  stream_info.end_time_ = stream_info.startTimeMonotonic() + std::chrono::microseconds(10000);
  EXPECT_CALL(runtime.snapshot_, getInteger("key", 1000000)).WillOnce(Return(100000000));
  EXPECT_FALSE(filter.evaluate(stream_info, request_headers));
}

TEST(AccessLogFilterTest, StatusCodeWithRuntimeKey) {
  std::string filter_json = R"EOF(
    {
      "filter": {"type": "status_code", "op": ">=", "value": 300, "runtime_key": "key"}
    }
    )EOF";

  Json::ObjectSharedPtr loader = Json::Factory::loadFromString(filter_json);
  NiceMock<Runtime::MockLoader> runtime;

  Json::ObjectSharedPtr filter_object = loader->getObject("filter");
  envoy::config::filter::accesslog::v2::AccessLogFilter config;
  Config::FilterJson::translateAccessLogFilter(*filter_object, config);
  StatusCodeFilter filter(config.status_code_filter(), runtime);

  Http::TestHeaderMapImpl request_headers{{":method", "GET"}, {":path", "/"}};
  TestStreamInfo info;

  info.response_code_ = 400;
  EXPECT_CALL(runtime.snapshot_, getInteger("key", 300)).WillOnce(Return(350));
  EXPECT_TRUE(filter.evaluate(info, request_headers));

  EXPECT_CALL(runtime.snapshot_, getInteger("key", 300)).WillOnce(Return(500));
  EXPECT_FALSE(filter.evaluate(info, request_headers));
}

TEST_F(AccessLogImplTest, StatusCodeLessThan) {
  const std::string yaml = R"EOF(
name: envoy.file_access_log
filter:
  status_code_filter:
    comparison:
      op: LE
      value:
        default_value: 499
        runtime_key: hello
config:
  path: /dev/null
  )EOF";

  InstanceSharedPtr log = AccessLogFactory::fromProto(parseAccessLogFromV2Yaml(yaml), context_);

  stream_info_.response_code_ = 499;
  EXPECT_CALL(runtime_.snapshot_, getInteger("hello", 499)).WillOnce(Return(499));
  EXPECT_CALL(*file_, write(_));
  log->log(&request_headers_, &response_headers_, &response_trailers_, stream_info_);

  stream_info_.response_code_ = 500;
  EXPECT_CALL(runtime_.snapshot_, getInteger("hello", 499)).WillOnce(Return(499));
  EXPECT_CALL(*file_, write(_)).Times(0);
  log->log(&request_headers_, &response_headers_, &response_trailers_, stream_info_);
}

TEST_F(AccessLogImplTest, HeaderPresence) {
  const std::string yaml = R"EOF(
name: envoy.file_access_log
filter:
  header_filter:
    header:
      name: test-header
config:
  path: /dev/null
  )EOF";

  InstanceSharedPtr log = AccessLogFactory::fromProto(parseAccessLogFromV2Yaml(yaml), context_);

  EXPECT_CALL(*file_, write(_)).Times(0);
  log->log(&request_headers_, &response_headers_, &response_trailers_, stream_info_);

  request_headers_.addCopy("test-header", "present");
  EXPECT_CALL(*file_, write(_));
  log->log(&request_headers_, &response_headers_, &response_trailers_, stream_info_);
}

TEST_F(AccessLogImplTest, HeaderExactMatch) {
  const std::string yaml = R"EOF(
name: envoy.file_access_log
filter:
  header_filter:
    header:
      name: test-header
      exact_match: exact-match-value

config:
  path: /dev/null
  )EOF";

  InstanceSharedPtr log = AccessLogFactory::fromProto(parseAccessLogFromV2Yaml(yaml), context_);

  EXPECT_CALL(*file_, write(_)).Times(0);
  log->log(&request_headers_, &response_headers_, &response_trailers_, stream_info_);

  request_headers_.addCopy("test-header", "exact-match-value");
  EXPECT_CALL(*file_, write(_));
  log->log(&request_headers_, &response_headers_, &response_trailers_, stream_info_);

  request_headers_.remove("test-header");
  request_headers_.addCopy("test-header", "not-exact-match-value");
  EXPECT_CALL(*file_, write(_)).Times(0);
  log->log(&request_headers_, &response_headers_, &response_trailers_, stream_info_);
}

TEST_F(AccessLogImplTest, HeaderRegexMatch) {
  const std::string yaml = R"EOF(
name: envoy.file_access_log
filter:
  header_filter:
    header:
      name: test-header
      regex_match: \d{3}
config:
  path: /dev/null
  )EOF";

  InstanceSharedPtr log = AccessLogFactory::fromProto(parseAccessLogFromV2Yaml(yaml), context_);

  EXPECT_CALL(*file_, write(_)).Times(0);
  log->log(&request_headers_, &response_headers_, &response_trailers_, stream_info_);

  request_headers_.addCopy("test-header", "123");
  EXPECT_CALL(*file_, write(_));
  log->log(&request_headers_, &response_headers_, &response_trailers_, stream_info_);

  request_headers_.remove("test-header");
  request_headers_.addCopy("test-header", "1234");
  EXPECT_CALL(*file_, write(_)).Times(0);
  log->log(&request_headers_, &response_headers_, &response_trailers_, stream_info_);

  request_headers_.remove("test-header");
  request_headers_.addCopy("test-header", "123.456");
  EXPECT_CALL(*file_, write(_)).Times(0);
  log->log(&request_headers_, &response_headers_, &response_trailers_, stream_info_);
}

TEST_F(AccessLogImplTest, HeaderRangeMatch) {
  const std::string yaml = R"EOF(
name: envoy.file_access_log
filter:
  header_filter:
    header:
      name: test-header
      range_match:
        start: -10
        end: 0
config:
  path: /dev/null
  )EOF";

  InstanceSharedPtr log = AccessLogFactory::fromProto(parseAccessLogFromV2Yaml(yaml), context_);

  EXPECT_CALL(*file_, write(_)).Times(0);
  log->log(&request_headers_, &response_headers_, &response_trailers_, stream_info_);

  request_headers_.addCopy("test-header", "-1");
  EXPECT_CALL(*file_, write(_));
  log->log(&request_headers_, &response_headers_, &response_trailers_, stream_info_);

  request_headers_.remove("test-header");
  request_headers_.addCopy("test-header", "0");
  EXPECT_CALL(*file_, write(_)).Times(0);
  log->log(&request_headers_, &response_headers_, &response_trailers_, stream_info_);

  request_headers_.remove("test-header");
  request_headers_.addCopy("test-header", "somestring");
  EXPECT_CALL(*file_, write(_)).Times(0);
  log->log(&request_headers_, &response_headers_, &response_trailers_, stream_info_);

  request_headers_.remove("test-header");
  request_headers_.addCopy("test-header", "10.9");
  EXPECT_CALL(*file_, write(_)).Times(0);
  log->log(&request_headers_, &response_headers_, &response_trailers_, stream_info_);

  request_headers_.remove("test-header");
  request_headers_.addCopy("test-header", "-1somestring");
  EXPECT_CALL(*file_, write(_)).Times(0);
  log->log(&request_headers_, &response_headers_, &response_trailers_, stream_info_);
}

TEST_F(AccessLogImplTest, ResponseFlagFilterAnyFlag) {
  const std::string yaml = R"EOF(
name: envoy.file_access_log
filter:
  response_flag_filter: {}
config:
  path: /dev/null
  )EOF";

  InstanceSharedPtr log = AccessLogFactory::fromProto(parseAccessLogFromV2Yaml(yaml), context_);

  EXPECT_CALL(*file_, write(_)).Times(0);
  log->log(&request_headers_, &response_headers_, &response_trailers_, stream_info_);

  stream_info_.setResponseFlag(StreamInfo::ResponseFlag::NoRouteFound);
  EXPECT_CALL(*file_, write(_));
  log->log(&request_headers_, &response_headers_, &response_trailers_, stream_info_);
}

TEST_F(AccessLogImplTest, ResponseFlagFilterSpecificFlag) {
  const std::string yaml = R"EOF(
name: envoy.file_access_log
filter:
  response_flag_filter:
    flags:
      - UO
config:
  path: /dev/null
  )EOF";

  InstanceSharedPtr log = AccessLogFactory::fromProto(parseAccessLogFromV2Yaml(yaml), context_);

  EXPECT_CALL(*file_, write(_)).Times(0);
  log->log(&request_headers_, &response_headers_, &response_trailers_, stream_info_);

  stream_info_.setResponseFlag(StreamInfo::ResponseFlag::NoRouteFound);
  EXPECT_CALL(*file_, write(_)).Times(0);
  log->log(&request_headers_, &response_headers_, &response_trailers_, stream_info_);

  stream_info_.setResponseFlag(StreamInfo::ResponseFlag::UpstreamOverflow);
  EXPECT_CALL(*file_, write(_));
  log->log(&request_headers_, &response_headers_, &response_trailers_, stream_info_);
}

TEST_F(AccessLogImplTest, ResponseFlagFilterSeveralFlags) {
  const std::string yaml = R"EOF(
name: envoy.file_access_log
filter:
  response_flag_filter:
    flags:
      - UO
      - RL
config:
  path: /dev/null
  )EOF";

  InstanceSharedPtr log = AccessLogFactory::fromProto(parseAccessLogFromV2Yaml(yaml), context_);

  EXPECT_CALL(*file_, write(_)).Times(0);
  log->log(&request_headers_, &response_headers_, &response_trailers_, stream_info_);

  stream_info_.setResponseFlag(StreamInfo::ResponseFlag::NoRouteFound);
  EXPECT_CALL(*file_, write(_)).Times(0);
  log->log(&request_headers_, &response_headers_, &response_trailers_, stream_info_);

  stream_info_.setResponseFlag(StreamInfo::ResponseFlag::UpstreamOverflow);
  EXPECT_CALL(*file_, write(_));
  log->log(&request_headers_, &response_headers_, &response_trailers_, stream_info_);
}

TEST_F(AccessLogImplTest, ResponseFlagFilterAllFlagsInPGV) {
  const std::string yaml = R"EOF(
name: envoy.file_access_log
filter:
  response_flag_filter:
    flags:
      - LH
      - UH
      - UT
      - LR
      - UR
      - UF
      - UC
      - UO
      - NR
      - DI
      - FI
      - RL
      - UAEX
      - RLSE
config:
  path: /dev/null
  )EOF";

  static_assert(StreamInfo::ResponseFlag::LastFlag == 0x2000,
                "A flag has been added. Fix this code.");

  std::vector<StreamInfo::ResponseFlag> all_response_flags = {
      StreamInfo::ResponseFlag::FailedLocalHealthCheck,
      StreamInfo::ResponseFlag::NoHealthyUpstream,
      StreamInfo::ResponseFlag::UpstreamRequestTimeout,
      StreamInfo::ResponseFlag::LocalReset,
      StreamInfo::ResponseFlag::UpstreamRemoteReset,
      StreamInfo::ResponseFlag::UpstreamConnectionFailure,
      StreamInfo::ResponseFlag::UpstreamConnectionTermination,
      StreamInfo::ResponseFlag::UpstreamOverflow,
      StreamInfo::ResponseFlag::NoRouteFound,
      StreamInfo::ResponseFlag::DelayInjected,
      StreamInfo::ResponseFlag::FaultInjected,
      StreamInfo::ResponseFlag::RateLimited,
      StreamInfo::ResponseFlag::UnauthorizedExternalService,
      StreamInfo::ResponseFlag::RateLimitServiceError,
  };

  InstanceSharedPtr log = AccessLogFactory::fromProto(parseAccessLogFromV2Yaml(yaml), context_);

  for (const auto response_flag : all_response_flags) {
    TestStreamInfo stream_info;
    stream_info.setResponseFlag(response_flag);
    EXPECT_CALL(*file_, write(_));
    log->log(&request_headers_, &response_headers_, &response_trailers_, stream_info);
  }
}

TEST_F(AccessLogImplTest, ResponseFlagFilterUnsupportedFlag) {
  const std::string yaml = R"EOF(
name: envoy.file_access_log
filter:
  response_flag_filter:
    flags:
      - UnsupportedFlag
config:
  path: /dev/null
  )EOF";

  EXPECT_THROW_WITH_MESSAGE(
      AccessLogFactory::fromProto(parseAccessLogFromV2Yaml(yaml), context_),
      ProtoValidationException,
      "Proto constraint validation failed (AccessLogFilterValidationError.ResponseFlagFilter: "
      "[\"embedded message failed validation\"] | caused by "
      "ResponseFlagFilterValidationError.Flags[i]: [\"value must be in list \" [\"LH\" \"UH\" "
      "\"UT\" \"LR\" \"UR\" \"UF\" \"UC\" \"UO\" \"NR\" \"DI\" \"FI\" \"RL\" \"UAEX\" \"RLSE\"]]): "
      "response_flag_filter {\n  flags: \"UnsupportedFlag\"\n}\n");
}

} // namespace
} // namespace AccessLog
} // namespace Envoy
