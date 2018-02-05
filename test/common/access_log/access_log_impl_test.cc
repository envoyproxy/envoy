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

using testing::NiceMock;
using testing::Return;
using testing::SaveArg;
using testing::_;

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

class TestRequestInfo : public RequestInfo::RequestInfo {
public:
  TestRequestInfo() {
    tm fake_time;
    memset(&fake_time, 0, sizeof(fake_time));
    fake_time.tm_year = 99; // tm < 1901-12-13 20:45:52 is not valid on osx
    fake_time.tm_mday = 1;
    start_time_ = std::chrono::system_clock::from_time_t(timegm(&fake_time));
  }

  SystemTime startTime() const override { return start_time_; }
  const Optional<std::chrono::microseconds>& requestReceivedDuration() const override {
    return request_received_duration_;
  }
  void requestReceivedDuration(MonotonicTime time) override { UNREFERENCED_PARAMETER(time); }
  const Optional<std::chrono::microseconds>& responseReceivedDuration() const override {
    return request_received_duration_;
  }
  void responseReceivedDuration(MonotonicTime time) override { UNREFERENCED_PARAMETER(time); }
  uint64_t bytesReceived() const override { return 1; }
  const Optional<Http::Protocol>& protocol() const override { return protocol_; }
  void protocol(Http::Protocol protocol) override { protocol_ = protocol; }
  const Optional<uint32_t>& responseCode() const override { return response_code_; }
  uint64_t bytesSent() const override { return 2; }
  std::chrono::microseconds duration() const override {
    return std::chrono::microseconds(duration_);
  }
  bool getResponseFlag(Envoy::RequestInfo::ResponseFlag response_flag) const override {
    return response_flags_ & response_flag;
  }
  void setResponseFlag(Envoy::RequestInfo::ResponseFlag response_flag) override {
    response_flags_ |= response_flag;
  }
  void onUpstreamHostSelected(Upstream::HostDescriptionConstSharedPtr host) override {
    upstream_host_ = host;
  }
  Upstream::HostDescriptionConstSharedPtr upstreamHost() const override { return upstream_host_; }
  const Network::Address::InstanceConstSharedPtr& upstreamLocalAddress() const override {
    return upstream_local_address_;
  }
  bool healthCheck() const override { return hc_request_; }
  void healthCheck(bool is_hc) override { hc_request_ = is_hc; }
  const Network::Address::InstanceConstSharedPtr& downstreamLocalAddress() const override {
    return downstream_local_address_;
  }
  const Network::Address::InstanceConstSharedPtr& downstreamRemoteAddress() const override {
    return downstream_remote_address_;
  }

  const Router::RouteEntry* routeEntry() const override { return route_entry_; }

  SystemTime start_time_;
  Optional<std::chrono::microseconds> request_received_duration_{std::chrono::microseconds(1000)};
  Optional<std::chrono::microseconds> response_received_duration_{std::chrono::microseconds(2000)};
  Optional<Http::Protocol> protocol_{Http::Protocol::Http11};
  Optional<uint32_t> response_code_;
  uint64_t response_flags_{};
  uint64_t duration_{3000};
  Upstream::HostDescriptionConstSharedPtr upstream_host_{};
  bool hc_request_{};
  Network::Address::InstanceConstSharedPtr upstream_local_address_;
  Network::Address::InstanceConstSharedPtr downstream_local_address_;
  Network::Address::InstanceConstSharedPtr downstream_remote_address_;
  const Router::RouteEntry* route_entry_{};
};

class AccessLogImplTest : public testing::Test {
public:
  AccessLogImplTest() : file_(new Filesystem::MockFile()) {
    ON_CALL(context_, runtime()).WillByDefault(ReturnRef(runtime_));
    EXPECT_CALL(context_, accessLogManager()).WillOnce(ReturnRef(log_manager_));
    EXPECT_CALL(log_manager_, createAccessLog(_)).WillOnce(Return(file_));
    ON_CALL(*file_, write(_)).WillByDefault(SaveArg<0>(&output_));
  }

  Http::TestHeaderMapImpl request_headers_{{":method", "GET"}, {":path", "/"}};
  Http::TestHeaderMapImpl response_headers_;
  TestRequestInfo request_info_;
  std::shared_ptr<Filesystem::MockFile> file_;
  std::string output_;

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
  request_info_.response_flags_ = RequestInfo::ResponseFlag::UpstreamConnectionFailure;
  request_headers_.addCopy(Http::Headers::get().UserAgent, "user-agent-set");
  request_headers_.addCopy(Http::Headers::get().RequestId, "id");
  request_headers_.addCopy(Http::Headers::get().Host, "host");
  request_headers_.addCopy(Http::Headers::get().ForwardedFor, "x.x.x.x");

  log->log(&request_headers_, &response_headers_, request_info_);
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

  log->log(&request_headers_, &response_headers_, request_info_);
  EXPECT_EQ(
      "[1999-01-01T00:00:00.000Z] \"GET / HTTP/1.1\" 0 - 1 2 3 999 \"-\" \"-\" \"-\" \"-\" \"-\"\n",
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
  log->log(&request_headers_, &response_headers_, request_info_);
  EXPECT_EQ(
      "[1999-01-01T00:00:00.000Z] \"GET / HTTP/1.1\" 0 - 1 2 3 - \"-\" \"-\" \"-\" \"-\" \"-\"\n",
      output_);
}

TEST_F(AccessLogImplTest, UpstreamHost) {
  std::shared_ptr<Upstream::MockClusterInfo> cluster{new Upstream::MockClusterInfo()};
  request_info_.upstream_host_ = Upstream::makeTestHostDescription(cluster, "tcp://10.0.0.5:1234");

  const std::string json = R"EOF(
      {
        "path": "/dev/null"
      }
      )EOF";

  InstanceSharedPtr log = AccessLogFactory::fromProto(parseAccessLogFromJson(json), context_);

  EXPECT_CALL(*file_, write(_));
  log->log(&request_headers_, &response_headers_, request_info_);
  EXPECT_EQ("[1999-01-01T00:00:00.000Z] \"GET / HTTP/1.1\" 0 - 1 2 3 - \"-\" \"-\" \"-\" \"-\" "
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
  log->log(&request_headers_, &response_headers_, request_info_);

  request_info_.response_code_.value(200);
  log->log(&request_headers_, &response_headers_, request_info_);
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
  log->log(&request_headers_, &response_headers_, request_info_);

  request_info_.response_code_.value(500);
  log->log(&request_headers_, &response_headers_, request_info_);

  request_info_.response_code_.value(200);
  request_info_.duration_ = 1000000000;
  log->log(&request_headers_, &response_headers_, request_info_);
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
  EXPECT_CALL(runtime_.snapshot_, featureEnabled("access_log.test_key", 0)).WillOnce(Return(true));
  EXPECT_CALL(*file_, write(_));
  log->log(&request_headers_, &response_headers_, request_info_);

  EXPECT_CALL(runtime_.snapshot_, featureEnabled("access_log.test_key", 0)).WillOnce(Return(false));
  EXPECT_CALL(*file_, write(_)).Times(0);
  log->log(&request_headers_, &response_headers_, request_info_);

  // Value is taken from x-request-id.
  request_headers_.addCopy("x-request-id", "000000ff-0000-0000-0000-000000000000");
  EXPECT_CALL(runtime_.snapshot_, getInteger("access_log.test_key", 0)).WillOnce(Return(56));
  EXPECT_CALL(*file_, write(_));
  log->log(&request_headers_, &response_headers_, request_info_);

  EXPECT_CALL(runtime_.snapshot_, getInteger("access_log.test_key", 0)).WillOnce(Return(55));
  EXPECT_CALL(*file_, write(_)).Times(0);
  log->log(&request_headers_, &response_headers_, request_info_);
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
  log->log(&request_headers_, &response_headers_, request_info_);
  EXPECT_EQ("[1999-01-01T00:00:00.000Z] \"GET /bar HTTP/1.1\" 0 - 1 2 3 - \"-\" \"-\" \"-\" \"-\" "
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
  request_info_.hc_request_ = true;
  EXPECT_CALL(*file_, write(_)).Times(0);

  log->log(&header_map, &response_headers_, request_info_);
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

  log->log(&request_headers_, &response_headers_, request_info_);
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
    log->log(&forced_header, &response_headers_, request_info_);
  }

  {
    Http::TestHeaderMapImpl not_traceable{{"x-request-id", not_traceable_guid}};
    EXPECT_CALL(*file_, write(_)).Times(0);
    log->log(&not_traceable, &response_headers_, request_info_);
  }

  {
    Http::TestHeaderMapImpl sampled_header{{"x-request-id", sample_tracing_guid}};
    EXPECT_CALL(*file_, write(_)).Times(0);
    log->log(&sampled_header, &response_headers_, request_info_);
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
  request_info_.response_code_.value(500);

  {
    EXPECT_CALL(*file_, write(_));
    Http::TestHeaderMapImpl header_map{{"user-agent", "NOT/Envoy/HC"}};

    log->log(&header_map, &response_headers_, request_info_);
  }

  {
    EXPECT_CALL(*file_, write(_)).Times(0);
    Http::TestHeaderMapImpl header_map{};
    request_info_.hc_request_ = true;
    log->log(&header_map, &response_headers_, request_info_);
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
  request_info_.response_code_.value(500);

  {
    EXPECT_CALL(*file_, write(_));
    Http::TestHeaderMapImpl header_map{{"user-agent", "NOT/Envoy/HC"}};

    log->log(&header_map, &response_headers_, request_info_);
  }

  {
    EXPECT_CALL(*file_, write(_));
    Http::TestHeaderMapImpl header_map{{"user-agent", "Envoy/HC"}};
    log->log(&header_map, &response_headers_, request_info_);
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
  request_info_.response_code_.value(500);

  {
    EXPECT_CALL(*file_, write(_));
    Http::TestHeaderMapImpl header_map{};

    log->log(&header_map, &response_headers_, request_info_);
  }

  {
    EXPECT_CALL(*file_, write(_)).Times(0);
    Http::TestHeaderMapImpl header_map{};
    request_info_.hc_request_ = true;

    log->log(&header_map, &response_headers_, request_info_);
  }
}

TEST_F(AccessLogImplTest, ConfigureFromProto) {
  envoy::config::filter::accesslog::v2::AccessLog config;

  envoy::config::filter::accesslog::v2::FileAccessLog fal_config;
  fal_config.set_path("/dev/null");

  MessageUtil::jsonConvert(fal_config, *config.mutable_config());

  EXPECT_THROW_WITH_MESSAGE(AccessLogFactory::fromProto(config, context_), EnvoyException,
                            "Provided name for static registration lookup was empty.");

  config.set_name(Config::AccessLogNames::get().FILE);

  InstanceSharedPtr log = AccessLogFactory::fromProto(config, context_);

  EXPECT_NE(nullptr, log);
  EXPECT_NE(nullptr, dynamic_cast<FileAccessLog*>(log.get()));

  config.set_name("INVALID");

  EXPECT_THROW_WITH_MESSAGE(AccessLogFactory::fromProto(config, context_), EnvoyException,
                            "Didn't find a registered implementation for name: 'INVALID'");
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
  TestRequestInfo request_info;

  request_info.duration_ = 100000;

  EXPECT_CALL(runtime.snapshot_, getInteger("key", 1000000)).WillOnce(Return(1));
  EXPECT_TRUE(filter.evaluate(request_info, request_headers));

  EXPECT_CALL(runtime.snapshot_, getInteger("key", 1000000)).WillOnce(Return(1000));
  EXPECT_FALSE(filter.evaluate(request_info, request_headers));

  request_info.duration_ = 100000001000;
  EXPECT_CALL(runtime.snapshot_, getInteger("key", 1000000)).WillOnce(Return(100000000));
  EXPECT_TRUE(filter.evaluate(request_info, request_headers));

  request_info.duration_ = 10000;
  EXPECT_CALL(runtime.snapshot_, getInteger("key", 1000000)).WillOnce(Return(100000000));
  EXPECT_FALSE(filter.evaluate(request_info, request_headers));
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
  TestRequestInfo info;

  info.response_code_.value(400);
  EXPECT_CALL(runtime.snapshot_, getInteger("key", 300)).WillOnce(Return(350));
  EXPECT_TRUE(filter.evaluate(info, request_headers));

  EXPECT_CALL(runtime.snapshot_, getInteger("key", 300)).WillOnce(Return(500));
  EXPECT_FALSE(filter.evaluate(info, request_headers));
}

} // namespace
} // namespace AccessLog
} // namespace Envoy
