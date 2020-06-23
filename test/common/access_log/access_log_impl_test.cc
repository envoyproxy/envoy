#include <chrono>
#include <cstdint>
#include <memory>
#include <string>

#include "envoy/config/accesslog/v3/accesslog.pb.h"
#include "envoy/config/accesslog/v3/accesslog.pb.validate.h"
#include "envoy/upstream/cluster_manager.h"
#include "envoy/upstream/upstream.h"

#include "common/access_log/access_log_impl.h"
#include "common/config/utility.h"
#include "common/protobuf/message_validator_impl.h"
#include "common/runtime/runtime_impl.h"

#include "test/common/stream_info/test_util.h"
#include "test/common/upstream/utility.h"
#include "test/mocks/access_log/mocks.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/filesystem/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/server/mocks.h"
#include "test/mocks/upstream/mocks.h"
#include "test/test_common/printers.h"
#include "test/test_common/registry.h"
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

envoy::config::accesslog::v3::AccessLog parseAccessLogFromV2Yaml(const std::string& yaml) {
  envoy::config::accesslog::v3::AccessLog access_log;
  TestUtility::loadFromYamlAndValidate(yaml, access_log);
  return access_log;
}

class AccessLogImplTest : public testing::Test {
public:
  AccessLogImplTest() : file_(new MockAccessLogFile()) {
    ON_CALL(context_, runtime()).WillByDefault(ReturnRef(runtime_));
    ON_CALL(context_, accessLogManager()).WillByDefault(ReturnRef(log_manager_));
    ON_CALL(log_manager_, createAccessLog(_)).WillByDefault(Return(file_));
    ON_CALL(*file_, write(_)).WillByDefault(SaveArg<0>(&output_));
  }

  Http::TestRequestHeaderMapImpl request_headers_{{":method", "GET"}, {":path", "/"}};
  Http::TestResponseHeaderMapImpl response_headers_;
  Http::TestResponseTrailerMapImpl response_trailers_;
  TestStreamInfo stream_info_;
  std::shared_ptr<MockAccessLogFile> file_;
  StringViewSaver output_;

  NiceMock<Runtime::MockLoader> runtime_;
  NiceMock<Envoy::AccessLog::MockAccessLogManager> log_manager_;
  NiceMock<Server::Configuration::MockFactoryContext> context_;
};

TEST_F(AccessLogImplTest, LogMoreData) {
  const std::string yaml = R"EOF(
name: accesslog
typed_config:
  "@type": type.googleapis.com/envoy.config.accesslog.v2.FileAccessLog
  path: /dev/null
  )EOF";

  InstanceSharedPtr log = AccessLogFactory::fromProto(parseAccessLogFromV2Yaml(yaml), context_);

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

TEST_F(AccessLogImplTest, DownstreamDisconnect) {
  const std::string yaml = R"EOF(
name: accesslog
typed_config:
  "@type": type.googleapis.com/envoy.config.accesslog.v2.FileAccessLog
  path: /dev/null
  )EOF";

  InstanceSharedPtr log = AccessLogFactory::fromProto(parseAccessLogFromV2Yaml(yaml), context_);

  EXPECT_CALL(*file_, write(_));

  auto cluster = std::make_shared<NiceMock<Upstream::MockClusterInfo>>();
  stream_info_.upstream_host_ = Upstream::makeTestHostDescription(cluster, "tcp://10.0.0.5:1234");
  stream_info_.response_flags_ = StreamInfo::ResponseFlag::DownstreamConnectionTermination;

  log->log(&request_headers_, &response_headers_, &response_trailers_, stream_info_);
  EXPECT_EQ("[1999-01-01T00:00:00.000Z] \"GET / HTTP/1.1\" 0 DC 1 2 3 - \"-\" \"-\" \"-\" \"-\" "
            "\"10.0.0.5:1234\"\n",
            output_);
}

TEST_F(AccessLogImplTest, RouteName) {
  const std::string yaml = R"EOF(
name: accesslog
typed_config:
  "@type": type.googleapis.com/envoy.config.accesslog.v2.FileAccessLog
  path: /dev/null
  format: "[%START_TIME%] \"%REQ(:METHOD)% %REQ(X-ENVOY-ORIGINAL-PATH?:PATH):256% %PROTOCOL%\" %RESPONSE_CODE% %RESPONSE_FLAGS% %ROUTE_NAME% %BYTES_RECEIVED% %BYTES_SENT% %DURATION% %RESP(X-ENVOY-UPSTREAM-SERVICE-TIME)% \"%REQ(X-FORWARDED-FOR)%\" \"%REQ(USER-AGENT)%\" \"%REQ(X-REQUEST-ID)%\"  \"%REQ(:AUTHORITY)%\"\n"
  )EOF";

  InstanceSharedPtr log = AccessLogFactory::fromProto(parseAccessLogFromV2Yaml(yaml), context_);

  EXPECT_CALL(*file_, write(_));
  stream_info_.route_name_ = "route-test-name";
  stream_info_.response_flags_ = StreamInfo::ResponseFlag::UpstreamConnectionFailure;
  request_headers_.addCopy(Http::Headers::get().UserAgent, "user-agent-set");
  request_headers_.addCopy(Http::Headers::get().RequestId, "id");
  request_headers_.addCopy(Http::Headers::get().Host, "host");
  request_headers_.addCopy(Http::Headers::get().ForwardedFor, "x.x.x.x");

  log->log(&request_headers_, &response_headers_, &response_trailers_, stream_info_);

  EXPECT_EQ(
      "[1999-01-01T00:00:00.000Z] \"GET / HTTP/1.1\" 0 UF route-test-name 1 2 3 - \"x.x.x.x\" "
      "\"user-agent-set\" \"id\"  \"host\"\n",
      output_);
}

TEST_F(AccessLogImplTest, EnvoyUpstreamServiceTime) {
  const std::string yaml = R"EOF(
name: accesslog
typed_config:
  "@type": type.googleapis.com/envoy.config.accesslog.v2.FileAccessLog
  path: /dev/null
  )EOF";

  InstanceSharedPtr log = AccessLogFactory::fromProto(parseAccessLogFromV2Yaml(yaml), context_);

  EXPECT_CALL(*file_, write(_));
  response_headers_.addCopy(Http::Headers::get().EnvoyUpstreamServiceTime, "999");

  log->log(&request_headers_, &response_headers_, &response_trailers_, stream_info_);
  EXPECT_EQ("[1999-01-01T00:00:00.000Z] \"GET / HTTP/1.1\" 0 - 1 2 3 999 \"-\" \"-\" \"-\" \"-\" "
            "\"-\"\n",
            output_);
}

TEST_F(AccessLogImplTest, NoFilter) {
  const std::string yaml = R"EOF(
name: accesslog
typed_config:
  "@type": type.googleapis.com/envoy.config.accesslog.v2.FileAccessLog
  path: /dev/null
  )EOF";

  InstanceSharedPtr log = AccessLogFactory::fromProto(parseAccessLogFromV2Yaml(yaml), context_);

  EXPECT_CALL(*file_, write(_));
  log->log(&request_headers_, &response_headers_, &response_trailers_, stream_info_);
  EXPECT_EQ(
      "[1999-01-01T00:00:00.000Z] \"GET / HTTP/1.1\" 0 - 1 2 3 - \"-\" \"-\" \"-\" \"-\" \"-\"\n",
      output_);
}

TEST_F(AccessLogImplTest, UpstreamHost) {
  auto cluster = std::make_shared<NiceMock<Upstream::MockClusterInfo>>();
  stream_info_.upstream_host_ = Upstream::makeTestHostDescription(cluster, "tcp://10.0.0.5:1234");

  const std::string yaml = R"EOF(
name: accesslog
typed_config:
  "@type": type.googleapis.com/envoy.config.accesslog.v2.FileAccessLog
  path: /dev/null
  )EOF";

  InstanceSharedPtr log = AccessLogFactory::fromProto(parseAccessLogFromV2Yaml(yaml), context_);

  EXPECT_CALL(*file_, write(_));
  log->log(&request_headers_, &response_headers_, &response_trailers_, stream_info_);
  EXPECT_EQ("[1999-01-01T00:00:00.000Z] \"GET / HTTP/1.1\" 0 - 1 2 3 - \"-\" \"-\" \"-\" \"-\" "
            "\"10.0.0.5:1234\"\n",
            output_);
}

TEST_F(AccessLogImplTest, WithFilterMiss) {
  const std::string yaml = R"EOF(
name: accesslog
filter:
  or_filter:
    filters:
    - status_code_filter:
        comparison:
          op: GE
          value:
            default_value: 500
            runtime_key: key_a
    - duration_filter:
        comparison:
          op: GE
          value:
            default_value: 1000000
            runtime_key: key_b
typed_config:
  "@type": type.googleapis.com/envoy.config.accesslog.v2.FileAccessLog
  path: /dev/null
  )EOF";

  InstanceSharedPtr log = AccessLogFactory::fromProto(parseAccessLogFromV2Yaml(yaml), context_);

  EXPECT_CALL(*file_, write(_)).Times(0);
  log->log(&request_headers_, &response_headers_, &response_trailers_, stream_info_);

  stream_info_.response_code_ = 200;
  log->log(&request_headers_, &response_headers_, &response_trailers_, stream_info_);
}

TEST_F(AccessLogImplTest, WithFilterHit) {
  const std::string yaml = R"EOF(
name: accesslog
filter:
    or_filter:
      filters:
      - status_code_filter:
          comparison:
            op: GE
            value:
              default_value: 500
              runtime_key: key_a
      - status_code_filter:
          comparison:
            op: EQ
            value:
              default_value: 0
              runtime_key: key_b
      - duration_filter:
          comparison:
            op: GE
            value:
              default_value: 1000000
              runtime_key: key_c
typed_config:
  "@type": type.googleapis.com/envoy.config.accesslog.v2.FileAccessLog
  path: /dev/null
  )EOF";

  InstanceSharedPtr log = AccessLogFactory::fromProto(parseAccessLogFromV2Yaml(yaml), context_);

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
  const std::string yaml = R"EOF(
name: accesslog
filter:
  runtime_filter:
    runtime_key: access_log.test_key
typed_config:
  "@type": type.googleapis.com/envoy.config.accesslog.v2.FileAccessLog
  path: /dev/null
  )EOF";

  Runtime::RandomGeneratorImpl random;
  InstanceSharedPtr log = AccessLogFactory::fromProto(parseAccessLogFromV2Yaml(yaml), context_);

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
name: accesslog
filter:
  runtime_filter:
    runtime_key: access_log.test_key
    percent_sampled:
      numerator: 5
      denominator: TEN_THOUSAND
typed_config:
  "@type": type.googleapis.com/envoy.config.accesslog.v2.FileAccessLog
  path: /dev/null
  )EOF";

  Runtime::RandomGeneratorImpl random;
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
name: accesslog
filter:
  runtime_filter:
    runtime_key: access_log.test_key
    percent_sampled:
      numerator: 5
      denominator: MILLION
    use_independent_randomness: true
typed_config:
  "@type": type.googleapis.com/envoy.config.accesslog.v2.FileAccessLog
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

  const std::string yaml = R"EOF(
name: accesslog
typed_config:
  "@type": type.googleapis.com/envoy.config.accesslog.v2.FileAccessLog
  path: /dev/null
  )EOF";

  InstanceSharedPtr log = AccessLogFactory::fromProto(parseAccessLogFromV2Yaml(yaml), context_);

  EXPECT_CALL(*file_, write(_));
  log->log(&request_headers_, &response_headers_, &response_trailers_, stream_info_);
  EXPECT_EQ("[1999-01-01T00:00:00.000Z] \"GET /bar HTTP/1.1\" 0 - 1 2 3 - \"-\" \"-\" \"-\" \"-\" "
            "\"-\"\n",
            output_);
}

TEST_F(AccessLogImplTest, HealthCheckTrue) {
  const std::string yaml = R"EOF(
name: accesslog
filter:
  not_health_check_filter: {}
typed_config:
  "@type": type.googleapis.com/envoy.config.accesslog.v2.FileAccessLog
  path: /dev/null
  )EOF";

  InstanceSharedPtr log = AccessLogFactory::fromProto(parseAccessLogFromV2Yaml(yaml), context_);

  Http::TestRequestHeaderMapImpl header_map{};
  stream_info_.health_check_request_ = true;
  EXPECT_CALL(*file_, write(_)).Times(0);

  log->log(&header_map, &response_headers_, &response_trailers_, stream_info_);
}

TEST_F(AccessLogImplTest, HealthCheckFalse) {
  const std::string yaml = R"EOF(
name: accesslog
filter:
  not_health_check_filter: {}
typed_config:
  "@type": type.googleapis.com/envoy.config.accesslog.v2.FileAccessLog
  path: "/dev/null"
  )EOF";

  InstanceSharedPtr log = AccessLogFactory::fromProto(parseAccessLogFromV2Yaml(yaml), context_);

  Http::TestRequestHeaderMapImpl header_map{};
  EXPECT_CALL(*file_, write(_));

  log->log(&request_headers_, &response_headers_, &response_trailers_, stream_info_);
}

TEST_F(AccessLogImplTest, RequestTracing) {
  Runtime::RandomGeneratorImpl random;

  const std::string yaml = R"EOF(
name: accesslog
filter:
  traceable_filter: {}
typed_config:
  "@type": type.googleapis.com/envoy.config.accesslog.v2.FileAccessLog
  path: /dev/null
  )EOF";

  InstanceSharedPtr log = AccessLogFactory::fromProto(parseAccessLogFromV2Yaml(yaml), context_);

  {
    Http::TestRequestHeaderMapImpl forced_header{{"x-request-id", random.uuid()}};
    stream_info_.getRequestIDExtension()->setTraceStatus(forced_header, Http::TraceStatus::Forced);
    EXPECT_CALL(*file_, write(_));
    log->log(&forced_header, &response_headers_, &response_trailers_, stream_info_);
  }

  {
    Http::TestRequestHeaderMapImpl not_traceable{{"x-request-id", random.uuid()}};
    EXPECT_CALL(*file_, write(_)).Times(0);
    log->log(&not_traceable, &response_headers_, &response_trailers_, stream_info_);
  }

  {
    Http::TestRequestHeaderMapImpl sampled_header{{"x-request-id", random.uuid()}};
    stream_info_.getRequestIDExtension()->setTraceStatus(sampled_header,
                                                         Http::TraceStatus::Sampled);
    EXPECT_CALL(*file_, write(_)).Times(0);
    log->log(&sampled_header, &response_headers_, &response_trailers_, stream_info_);
  }
}

TEST(AccessLogImplTestCtor, FiltersMissingInOrAndFilter) {
  NiceMock<Server::Configuration::MockFactoryContext> context;

  {
    const std::string yaml = R"EOF(
name: accesslog
filter:
  or_filter: {}
typed_config:
  "@type": type.googleapis.com/envoy.config.accesslog.v2.FileAccessLog
  path: /dev/null
    )EOF";

    EXPECT_THROW(AccessLogFactory::fromProto(parseAccessLogFromV2Yaml(yaml), context),
                 EnvoyException);
  }

  {
    const std::string yaml = R"EOF(
name: accesslog
filter:
  and_filter: {}
typed_config:
  "@type": type.googleapis.com/envoy.config.accesslog.v2.FileAccessLog
  path: /dev/null
    )EOF";

    EXPECT_THROW(AccessLogFactory::fromProto(parseAccessLogFromV2Yaml(yaml), context),
                 EnvoyException);
  }
}

TEST_F(AccessLogImplTest, AndFilter) {
  const std::string yaml = R"EOF(
name: accesslog
filter:
  and_filter:
    filters:
      - status_code_filter:
          comparison:
            op: GE
            value:
              default_value: 500
              runtime_key: key
      - not_health_check_filter: {}
typed_config:
  "@type": type.googleapis.com/envoy.config.accesslog.v2.FileAccessLog
  path: /dev/null
  )EOF";

  InstanceSharedPtr log = AccessLogFactory::fromProto(parseAccessLogFromV2Yaml(yaml), context_);
  stream_info_.response_code_ = 500;

  {
    EXPECT_CALL(*file_, write(_));
    Http::TestRequestHeaderMapImpl header_map{{"user-agent", "NOT/Envoy/HC"}};

    log->log(&header_map, &response_headers_, &response_trailers_, stream_info_);
  }

  {
    EXPECT_CALL(*file_, write(_)).Times(0);
    Http::TestRequestHeaderMapImpl header_map{};
    stream_info_.health_check_request_ = true;
    log->log(&header_map, &response_headers_, &response_trailers_, stream_info_);
  }
}

TEST_F(AccessLogImplTest, OrFilter) {
  const std::string yaml = R"EOF(
name: accesslog
filter:
  or_filter:
    filters:
    - status_code_filter:
        comparison:
          op: GE
          value:
            default_value: 500
            runtime_key: key
    - not_health_check_filter: {}
typed_config:
  "@type": type.googleapis.com/envoy.config.accesslog.v2.FileAccessLog
  path: /dev/null
  )EOF";

  InstanceSharedPtr log = AccessLogFactory::fromProto(parseAccessLogFromV2Yaml(yaml), context_);
  stream_info_.response_code_ = 500;

  {
    EXPECT_CALL(*file_, write(_));
    Http::TestRequestHeaderMapImpl header_map{{"user-agent", "NOT/Envoy/HC"}};

    log->log(&header_map, &response_headers_, &response_trailers_, stream_info_);
  }

  {
    EXPECT_CALL(*file_, write(_));
    Http::TestRequestHeaderMapImpl header_map{{"user-agent", "Envoy/HC"}};
    log->log(&header_map, &response_headers_, &response_trailers_, stream_info_);
  }
}

TEST_F(AccessLogImplTest, MultipleOperators) {
  const std::string yaml = R"EOF(
name: accesslog
filter:
  and_filter:
    filters:
    - or_filter:
        filters:
        - duration_filter:
            comparison:
              op: GE
              value:
                default_value: 10000
                runtime_key: key_a
        - status_code_filter:
            comparison:
              op: GE
              value:
                default_value: 500
                runtime_key: key_b
    - not_health_check_filter: {}
typed_config:
  "@type": type.googleapis.com/envoy.config.accesslog.v2.FileAccessLog
  path: /dev/null
  )EOF";

  InstanceSharedPtr log = AccessLogFactory::fromProto(parseAccessLogFromV2Yaml(yaml), context_);
  stream_info_.response_code_ = 500;

  {
    EXPECT_CALL(*file_, write(_));
    Http::TestRequestHeaderMapImpl header_map{};

    log->log(&header_map, &response_headers_, &response_trailers_, stream_info_);
  }

  {
    EXPECT_CALL(*file_, write(_)).Times(0);
    Http::TestRequestHeaderMapImpl header_map{};
    stream_info_.health_check_request_ = true;

    log->log(&header_map, &response_headers_, &response_trailers_, stream_info_);
  }
}

TEST(AccessLogFilterTest, DurationWithRuntimeKey) {
  const std::string filter_yaml = R"EOF(
duration_filter:
  comparison:
    op: GE
    value:
      default_value: 1000000
      runtime_key: key
    )EOF";

  NiceMock<Runtime::MockLoader> runtime;

  envoy::config::accesslog::v3::AccessLogFilter config;
  TestUtility::loadFromYaml(filter_yaml, config);
  DurationFilter filter(config.duration_filter(), runtime);
  Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"}, {":path", "/"}};
  Http::TestResponseHeaderMapImpl response_headers;
  Http::TestResponseTrailerMapImpl response_trailers;
  TestStreamInfo stream_info;

  stream_info.end_time_ = stream_info.startTimeMonotonic() + std::chrono::microseconds(100000);
  EXPECT_CALL(runtime.snapshot_, getInteger("key", 1000000)).WillOnce(Return(1));
  EXPECT_TRUE(filter.evaluate(stream_info, request_headers, response_headers, response_trailers));

  EXPECT_CALL(runtime.snapshot_, getInteger("key", 1000000)).WillOnce(Return(1000));
  EXPECT_FALSE(filter.evaluate(stream_info, request_headers, response_headers, response_trailers));

  stream_info.end_time_ =
      stream_info.startTimeMonotonic() + std::chrono::microseconds(100000001000);
  EXPECT_CALL(runtime.snapshot_, getInteger("key", 1000000)).WillOnce(Return(100000000));
  EXPECT_TRUE(filter.evaluate(stream_info, request_headers, response_headers, response_trailers));

  stream_info.end_time_ = stream_info.startTimeMonotonic() + std::chrono::microseconds(10000);
  EXPECT_CALL(runtime.snapshot_, getInteger("key", 1000000)).WillOnce(Return(100000000));
  EXPECT_FALSE(filter.evaluate(stream_info, request_headers, response_headers, response_trailers));
}

TEST(AccessLogFilterTest, StatusCodeWithRuntimeKey) {
  const std::string filter_yaml = R"EOF(
status_code_filter:
  comparison:
    op: GE
    value:
      default_value: 300
      runtime_key: key
    )EOF";

  NiceMock<Runtime::MockLoader> runtime;

  envoy::config::accesslog::v3::AccessLogFilter config;
  TestUtility::loadFromYaml(filter_yaml, config);
  StatusCodeFilter filter(config.status_code_filter(), runtime);

  Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"}, {":path", "/"}};
  Http::TestResponseHeaderMapImpl response_headers;
  Http::TestResponseTrailerMapImpl response_trailers;
  TestStreamInfo info;

  info.response_code_ = 400;
  EXPECT_CALL(runtime.snapshot_, getInteger("key", 300)).WillOnce(Return(350));
  EXPECT_TRUE(filter.evaluate(info, request_headers, response_headers, response_trailers));

  EXPECT_CALL(runtime.snapshot_, getInteger("key", 300)).WillOnce(Return(500));
  EXPECT_FALSE(filter.evaluate(info, request_headers, response_headers, response_trailers));
}

TEST_F(AccessLogImplTest, StatusCodeLessThan) {
  const std::string yaml = R"EOF(
name: accesslog
filter:
  status_code_filter:
    comparison:
      op: LE
      value:
        default_value: 499
        runtime_key: hello
typed_config:
  "@type": type.googleapis.com/envoy.config.accesslog.v2.FileAccessLog
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
name: accesslog
filter:
  header_filter:
    header:
      name: test-header
typed_config:
  "@type": type.googleapis.com/envoy.config.accesslog.v2.FileAccessLog
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
name: accesslog
filter:
  header_filter:
    header:
      name: test-header
      exact_match: exact-match-value

typed_config:
  "@type": type.googleapis.com/envoy.config.accesslog.v2.FileAccessLog
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
name: accesslog
filter:
  header_filter:
    header:
      name: test-header
      safe_regex_match:
        google_re2: {}
        regex: "\\d{3}"
typed_config:
  "@type": type.googleapis.com/envoy.config.accesslog.v2.FileAccessLog
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
name: accesslog
filter:
  header_filter:
    header:
      name: test-header
      range_match:
        start: -10
        end: 0
typed_config:
  "@type": type.googleapis.com/envoy.config.accesslog.v2.FileAccessLog
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
name: accesslog
filter:
  response_flag_filter: {}
typed_config:
  "@type": type.googleapis.com/envoy.config.accesslog.v2.FileAccessLog
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
name: accesslog
filter:
  response_flag_filter:
    flags:
      - UO
typed_config:
  "@type": type.googleapis.com/envoy.config.accesslog.v2.FileAccessLog
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
name: accesslog
filter:
  response_flag_filter:
    flags:
      - UO
      - RL
typed_config:
  "@type": type.googleapis.com/envoy.config.accesslog.v2.FileAccessLog
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
name: accesslog
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
      - DC
      - URX
      - SI
      - IH
      - DPE
      - UMSDR
      - RFCF
typed_config:
  "@type": type.googleapis.com/envoy.config.accesslog.v2.FileAccessLog
  path: /dev/null
  )EOF";

  static_assert(StreamInfo::ResponseFlag::LastFlag == 0x100000,
                "A flag has been added. Fix this code.");

  const std::vector<StreamInfo::ResponseFlag> all_response_flags = {
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
      StreamInfo::ResponseFlag::DownstreamConnectionTermination,
      StreamInfo::ResponseFlag::UpstreamRetryLimitExceeded,
      StreamInfo::ResponseFlag::StreamIdleTimeout,
      StreamInfo::ResponseFlag::InvalidEnvoyRequestHeaders,
      StreamInfo::ResponseFlag::DownstreamProtocolError,
      StreamInfo::ResponseFlag::UpstreamMaxStreamDurationReached,
      StreamInfo::ResponseFlag::ResponseFromCacheFilter};

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
name: accesslog
filter:
  response_flag_filter:
    flags:
      - UnsupportedFlag
typed_config:
  "@type": type.googleapis.com/envoy.config.accesslog.v2.FileAccessLog
  path: /dev/null
  )EOF";

  EXPECT_THROW_WITH_MESSAGE(
      AccessLogFactory::fromProto(parseAccessLogFromV2Yaml(yaml), context_),
      ProtoValidationException,
      "Proto constraint validation failed (AccessLogValidationError.Filter: [\"embedded message "
      "failed validation\"] | caused by AccessLogFilterValidationError.ResponseFlagFilter: "
      "[\"embedded message failed validation\"] | caused by "
      "ResponseFlagFilterValidationError.Flags[i]: [\"value must be in list \" [\"LH\" \"UH\" "
      "\"UT\" \"LR\" \"UR\" \"UF\" \"UC\" \"UO\" \"NR\" \"DI\" \"FI\" \"RL\" \"UAEX\" \"RLSE\" "
      "\"DC\" \"URX\" \"SI\" \"IH\" \"DPE\" \"UMSDR\" \"RFCF\"]]): name: \"accesslog\"\nfilter {\n "
      " "
      "response_flag_filter {\n    flags: \"UnsupportedFlag\"\n  }\n}\ntyped_config {\n  "
      "[type.googleapis.com/envoy.config.accesslog.v2.FileAccessLog] {\n    path: \"/dev/null\"\n  "
      "}\n}\n");
}

TEST_F(AccessLogImplTest, ValidateTypedConfig) {
  const std::string yaml = R"EOF(
name: accesslog
filter:
  response_flag_filter:
    flags:
      - UnsupportedFlag
typed_config:
  "@type": type.googleapis.com/envoy.config.accesslog.v2.FileAccessLog
  path: /dev/null
  )EOF";

  EXPECT_THROW_WITH_MESSAGE(
      AccessLogFactory::fromProto(parseAccessLogFromV2Yaml(yaml), context_),
      ProtoValidationException,
      "Proto constraint validation failed (AccessLogValidationError.Filter: [\"embedded message "
      "failed validation\"] | caused by AccessLogFilterValidationError.ResponseFlagFilter: "
      "[\"embedded message failed validation\"] | caused by "
      "ResponseFlagFilterValidationError.Flags[i]: [\"value must be in list \" [\"LH\" \"UH\" "
      "\"UT\" \"LR\" \"UR\" \"UF\" \"UC\" \"UO\" \"NR\" \"DI\" \"FI\" \"RL\" \"UAEX\" \"RLSE\" "
      "\"DC\" \"URX\" \"SI\" \"IH\" \"DPE\" \"UMSDR\" \"RFCF\"]]): name: \"accesslog\"\nfilter {\n "
      " "
      "response_flag_filter {\n    flags: \"UnsupportedFlag\"\n  }\n}\ntyped_config {\n  "
      "[type.googleapis.com/envoy.config.accesslog.v2.FileAccessLog] {\n    path: \"/dev/null\"\n  "
      "}\n}\n");
}

TEST_F(AccessLogImplTest, ValidGrpcStatusMessage) {
  const std::string yaml = R"EOF(
name: accesslog
typed_config:
  "@type": type.googleapis.com/envoy.config.accesslog.v2.FileAccessLog
  path: /dev/null
  format: "%GRPC_STATUS%\n"
  )EOF";

  InstanceSharedPtr log = AccessLogFactory::fromProto(parseAccessLogFromV2Yaml(yaml), context_);
  {
    EXPECT_CALL(*file_, write(_));
    response_trailers_.addCopy(Http::Headers::get().GrpcStatus, "0");
    log->log(&request_headers_, &response_headers_, &response_trailers_, stream_info_);
    EXPECT_EQ("OK\n", output_);
    response_trailers_.remove(Http::Headers::get().GrpcStatus);
  }
  {
    InstanceSharedPtr log = AccessLogFactory::fromProto(parseAccessLogFromV2Yaml(yaml), context_);
    EXPECT_CALL(*file_, write(_));
    response_headers_.addCopy(Http::Headers::get().GrpcStatus, "1");
    log->log(&request_headers_, &response_headers_, &response_trailers_, stream_info_);
    EXPECT_EQ("Canceled\n", output_);
    response_headers_.remove(Http::Headers::get().GrpcStatus);
  }
  {
    InstanceSharedPtr log = AccessLogFactory::fromProto(parseAccessLogFromV2Yaml(yaml), context_);
    EXPECT_CALL(*file_, write(_));
    response_headers_.addCopy(Http::Headers::get().GrpcStatus, "-1");
    log->log(&request_headers_, &response_headers_, &response_trailers_, stream_info_);
    EXPECT_EQ("-1\n", output_);
    response_headers_.remove(Http::Headers::get().GrpcStatus);
  }
}

TEST_F(AccessLogImplTest, GrpcStatusFilterValues) {
  const std::string yaml_template = R"EOF(
name: accesslog
filter:
  grpc_status_filter:
    statuses:
      - {}
typed_config:
  "@type": type.googleapis.com/envoy.config.accesslog.v2.FileAccessLog
  path: /dev/null
)EOF";

  const auto desc = envoy::config::accesslog::v3::GrpcStatusFilter::Status_descriptor();
  const int grpcStatuses = static_cast<int>(Grpc::Status::WellKnownGrpcStatus::MaximumKnown) + 1;
  if (desc->value_count() != grpcStatuses) {
    FAIL() << "Mismatch in number of gRPC statuses, GrpcStatus has " << grpcStatuses
           << ", GrpcStatusFilter_Status has " << desc->value_count() << ".";
  }

  for (int i = 0; i < desc->value_count(); i++) {
    InstanceSharedPtr log = AccessLogFactory::fromProto(
        parseAccessLogFromV2Yaml(fmt::format(yaml_template, desc->value(i)->name())), context_);

    EXPECT_CALL(*file_, write(_));

    response_trailers_.addCopy(Http::Headers::get().GrpcStatus, std::to_string(i));
    log->log(&request_headers_, &response_headers_, &response_trailers_, stream_info_);
    response_trailers_.remove(Http::Headers::get().GrpcStatus);
  }
}

TEST_F(AccessLogImplTest, GrpcStatusFilterUnsupportedValue) {
  const std::string yaml = R"EOF(
name: accesslog
filter:
  grpc_status_filter:
    statuses:
      - NOT_A_VALID_CODE
typed_config:
  "@type": type.googleapis.com/envoy.config.accesslog.v2.FileAccessLog
  path: /dev/null
  )EOF";

  EXPECT_THROW_WITH_REGEX(AccessLogFactory::fromProto(parseAccessLogFromV2Yaml(yaml), context_),
                          EnvoyException, ".*\"NOT_A_VALID_CODE\" for type TYPE_ENUM.*");
}

TEST_F(AccessLogImplTest, GrpcStatusFilterBlock) {
  const std::string yaml = R"EOF(
name: accesslog
filter:
  grpc_status_filter:
    statuses:
      - OK
typed_config:
  "@type": type.googleapis.com/envoy.config.accesslog.v2.FileAccessLog
  path: /dev/null
  )EOF";

  const InstanceSharedPtr log =
      AccessLogFactory::fromProto(parseAccessLogFromV2Yaml(yaml), context_);

  response_trailers_.addCopy(Http::Headers::get().GrpcStatus, "1");

  EXPECT_CALL(*file_, write(_)).Times(0);
  log->log(&request_headers_, &response_headers_, &response_trailers_, stream_info_);
}

TEST_F(AccessLogImplTest, GrpcStatusFilterHttpCodes) {
  const std::string yaml_template = R"EOF(
name: accesslog
filter:
  grpc_status_filter:
    statuses:
      - {}
typed_config:
  "@type": type.googleapis.com/envoy.config.accesslog.v2.FileAccessLog
  path: /dev/null
)EOF";

  // This mapping includes UNKNOWN <-> 200 because we expect that gRPC should provide an explicit
  // status code for successes. In general, the only status codes that receive an HTTP mapping are
  // those enumerated below with a non-UNKNOWN mapping. See: //source/common/grpc/status.cc and
  // https://github.com/grpc/grpc/blob/master/doc/http-grpc-status-mapping.md.
  const std::vector<std::pair<std::string, uint64_t>> statusMapping = {
      {"UNKNOWN", 200},           {"INTERNAL", 400},    {"UNAUTHENTICATED", 401},
      {"PERMISSION_DENIED", 403}, {"UNAVAILABLE", 429}, {"UNIMPLEMENTED", 404},
      {"UNAVAILABLE", 502},       {"UNAVAILABLE", 503}, {"UNAVAILABLE", 504}};

  for (const auto& pair : statusMapping) {
    stream_info_.response_code_ = pair.second;

    const InstanceSharedPtr log = AccessLogFactory::fromProto(
        parseAccessLogFromV2Yaml(fmt::format(yaml_template, pair.first)), context_);

    EXPECT_CALL(*file_, write(_));
    log->log(&request_headers_, &response_headers_, &response_trailers_, stream_info_);
  }
}

TEST_F(AccessLogImplTest, GrpcStatusFilterNoCode) {
  const std::string yaml = R"EOF(
name: accesslog
filter:
  grpc_status_filter:
    statuses:
      - UNKNOWN
typed_config:
  "@type": type.googleapis.com/envoy.config.accesslog.v2.FileAccessLog
  path: /dev/null
  )EOF";

  const InstanceSharedPtr log =
      AccessLogFactory::fromProto(parseAccessLogFromV2Yaml(yaml), context_);

  EXPECT_CALL(*file_, write(_));
  log->log(&request_headers_, &response_headers_, &response_trailers_, stream_info_);
}

TEST_F(AccessLogImplTest, GrpcStatusFilterExclude) {
  const std::string yaml = R"EOF(
name: accesslog
filter:
  grpc_status_filter:
    exclude: true
    statuses:
      - OK
typed_config:
  "@type": type.googleapis.com/envoy.config.accesslog.v2.FileAccessLog
  path: /dev/null
  )EOF";

  const InstanceSharedPtr log =
      AccessLogFactory::fromProto(parseAccessLogFromV2Yaml(yaml), context_);

  for (int i = 0; i <= static_cast<int>(Grpc::Status::WellKnownGrpcStatus::MaximumKnown); i++) {
    EXPECT_CALL(*file_, write(_)).Times(i == 0 ? 0 : 1);

    response_trailers_.addCopy(Http::Headers::get().GrpcStatus, std::to_string(i));
    log->log(&request_headers_, &response_headers_, &response_trailers_, stream_info_);
    response_trailers_.remove(Http::Headers::get().GrpcStatus);
  }
}

TEST_F(AccessLogImplTest, GrpcStatusFilterExcludeFalse) {
  const std::string yaml = R"EOF(
name: accesslog
filter:
  grpc_status_filter:
    exclude: false
    statuses:
      - OK
typed_config:
  "@type": type.googleapis.com/envoy.config.accesslog.v2.FileAccessLog
  path: /dev/null
  )EOF";

  const InstanceSharedPtr log =
      AccessLogFactory::fromProto(parseAccessLogFromV2Yaml(yaml), context_);

  response_trailers_.addCopy(Http::Headers::get().GrpcStatus, "0");

  EXPECT_CALL(*file_, write(_));
  log->log(&request_headers_, &response_headers_, &response_trailers_, stream_info_);
}

TEST_F(AccessLogImplTest, GrpcStatusFilterHeader) {
  const std::string yaml = R"EOF(
name: accesslog
filter:
  grpc_status_filter:
    statuses:
      - OK
typed_config:
  "@type": type.googleapis.com/envoy.config.accesslog.v2.FileAccessLog
  path: /dev/null
  )EOF";

  const InstanceSharedPtr log =
      AccessLogFactory::fromProto(parseAccessLogFromV2Yaml(yaml), context_);

  EXPECT_CALL(*file_, write(_));

  response_headers_.addCopy(Http::Headers::get().GrpcStatus, "0");
  log->log(&request_headers_, &response_headers_, &response_trailers_, stream_info_);
}

class TestHeaderFilterFactory : public ExtensionFilterFactory {
public:
  ~TestHeaderFilterFactory() override = default;

  FilterPtr createFilter(const envoy::config::accesslog::v3::ExtensionFilter& config,
                         Runtime::Loader&, Runtime::RandomGenerator&) override {
    auto factory_config = Config::Utility::translateToFactoryConfig(
        config, Envoy::ProtobufMessage::getNullValidationVisitor(), *this);
    const auto& header_config =
        TestUtility::downcastAndValidate<const envoy::config::accesslog::v3::HeaderFilter&>(
            *factory_config);
    return std::make_unique<HeaderFilter>(header_config);
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<envoy::config::accesslog::v3::HeaderFilter>();
  }

  std::string name() const override { return "test_header_filter"; }
};

TEST_F(AccessLogImplTest, TestHeaderFilterPresence) {
  TestHeaderFilterFactory factory;
  Registry::InjectFactory<ExtensionFilterFactory> registration(factory);

  const std::string yaml = R"EOF(
name: accesslog
filter:
  extension_filter:
    name: test_header_filter
    typed_config:
      "@type": type.googleapis.com/envoy.config.filter.accesslog.v2.HeaderFilter
      header:
        name: test-header
typed_config:
  "@type": type.googleapis.com/envoy.config.accesslog.v2.FileAccessLog
  path: /dev/null
  )EOF";

  InstanceSharedPtr logger = AccessLogFactory::fromProto(parseAccessLogFromV2Yaml(yaml), context_);

  EXPECT_CALL(*file_, write(_)).Times(0);
  logger->log(&request_headers_, &response_headers_, &response_trailers_, stream_info_);

  request_headers_.addCopy("test-header", "foo/bar");
  EXPECT_CALL(*file_, write(_));
  logger->log(&request_headers_, &response_headers_, &response_trailers_, stream_info_);
}

/**
 * Sample extension filter which allows every 1 of every `sample_rate` log attempts.
 */
class SampleExtensionFilter : public Filter {
public:
  SampleExtensionFilter(uint32_t sample_rate) : sample_rate_(sample_rate) {}

  // AccessLog::Filter
  bool evaluate(const StreamInfo::StreamInfo&, const Http::RequestHeaderMap&,
                const Http::ResponseHeaderMap&, const Http::ResponseTrailerMap&) const override {
    if (current_++ == 0) {
      return true;
    }
    if (current_ >= sample_rate_) {
      current_ = 0;
    }
    return false;
  }

private:
  mutable uint32_t current_ = 0;
  uint32_t sample_rate_;
};

/**
 * Sample extension filter factory which creates SampleExtensionFilter.
 */
class SampleExtensionFilterFactory : public ExtensionFilterFactory {
public:
  ~SampleExtensionFilterFactory() override = default;

  FilterPtr createFilter(const envoy::config::accesslog::v3::ExtensionFilter& config,
                         Runtime::Loader&, Runtime::RandomGenerator&) override {
    auto factory_config = Config::Utility::translateToFactoryConfig(
        config, Envoy::ProtobufMessage::getNullValidationVisitor(), *this);

    ProtobufWkt::Struct struct_config =
        *dynamic_cast<const ProtobufWkt::Struct*>(factory_config.get());
    return std::make_unique<SampleExtensionFilter>(
        static_cast<uint32_t>(struct_config.fields().at("rate").number_value()));
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<ProtobufWkt::Struct>();
  }

  std::string name() const override { return "sample_extension_filter"; }
};

TEST_F(AccessLogImplTest, SampleExtensionFilter) {
  SampleExtensionFilterFactory factory;
  Registry::InjectFactory<ExtensionFilterFactory> registration(factory);

  const std::string yaml = R"EOF(
name: accesslog
filter:
  extension_filter:
    name: sample_extension_filter
    typed_config:
      "@type": type.googleapis.com/google.protobuf.Struct
      value:
        rate: 5
typed_config:
  "@type": type.googleapis.com/envoy.config.accesslog.v2.FileAccessLog
  path: /dev/null
  )EOF";

  InstanceSharedPtr logger = AccessLogFactory::fromProto(parseAccessLogFromV2Yaml(yaml), context_);
  // For rate=5 expect 1st request to be recorded, 2nd-5th skipped, and 6th recorded.
  EXPECT_CALL(*file_, write(_));
  logger->log(&request_headers_, &response_headers_, &response_trailers_, stream_info_);
  for (int i = 0; i <= 3; ++i) {
    EXPECT_CALL(*file_, write(_)).Times(0);
    logger->log(&request_headers_, &response_headers_, &response_trailers_, stream_info_);
  }
  EXPECT_CALL(*file_, write(_));
  logger->log(&request_headers_, &response_headers_, &response_trailers_, stream_info_);
}

TEST_F(AccessLogImplTest, UnregisteredExtensionFilter) {
  {
    const std::string yaml = R"EOF(
name: accesslog
filter:
  extension_filter:
    name: unregistered_extension_filter
    typed_config:
      "@type": type.googleapis.com/google.protobuf.Struct
      value:
        foo: bar
typed_config:
  "@type": type.googleapis.com/envoy.config.accesslog.v2.FileAccessLog
  path: /dev/null
  )EOF";

    EXPECT_THROW(AccessLogFactory::fromProto(parseAccessLogFromV2Yaml(yaml), context_),
                 EnvoyException);
  }

  {
    const std::string yaml = R"EOF(
name: accesslog
filter:
  extension_filter:
    name: bar
typed_config:
  "@type": type.googleapis.com/envoy.config.accesslog.v2.FileAccessLog
  path: /dev/null
    )EOF";

    EXPECT_THROW(AccessLogFactory::fromProto(parseAccessLogFromV2Yaml(yaml), context_),
                 EnvoyException);
  }
}

// Test that the deprecated extension names still function.
TEST_F(AccessLogImplTest, DEPRECATED_FEATURE_TEST(DeprecatedExtensionFilterName)) {
  {
    envoy::config::accesslog::v3::AccessLog config;
    config.set_name("envoy.access_loggers.file");

    EXPECT_NO_THROW(
        Config::Utility::getAndCheckFactory<Server::Configuration::AccessLogInstanceFactory>(
            config));
  }

  {
    envoy::config::accesslog::v3::AccessLog config;
    config.set_name("envoy.file_access_log");

    EXPECT_NO_THROW(
        Config::Utility::getAndCheckFactory<Server::Configuration::AccessLogInstanceFactory>(
            config));
  }

  {
    envoy::config::accesslog::v3::AccessLog config;
    config.set_name("envoy.http_grpc_access_log");

    EXPECT_NO_THROW(
        Config::Utility::getAndCheckFactory<Server::Configuration::AccessLogInstanceFactory>(
            config));
  }

  {
    envoy::config::accesslog::v3::AccessLog config;
    config.set_name("envoy.tcp_grpc_access_log");

    EXPECT_NO_THROW(
        Config::Utility::getAndCheckFactory<Server::Configuration::AccessLogInstanceFactory>(
            config));
  }
}

} // namespace
} // namespace AccessLog
} // namespace Envoy
