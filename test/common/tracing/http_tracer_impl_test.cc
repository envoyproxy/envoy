#include <cstdint>
#include <memory>
#include <string>

#include "common/common/base64.h"
#include "common/http/header_map_impl.h"
#include "common/http/headers.h"
#include "common/http/message_impl.h"
#include "common/runtime/runtime_impl.h"
#include "common/runtime/uuid_util.h"
#include "common/tracing/http_tracer_impl.h"

#include "test/mocks/http/mocks.h"
#include "test/mocks/local_info/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/stats/mocks.h"
#include "test/mocks/thread_local/mocks.h"
#include "test/mocks/tracing/mocks.h"
#include "test/mocks/upstream/mocks.h"
#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::Eq;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;
using testing::ReturnPointee;
using testing::ReturnRef;

namespace Envoy {
namespace Tracing {
namespace {

TEST(HttpTracerUtilityTest, IsTracing) {
  NiceMock<StreamInfo::MockStreamInfo> stream_info;
  NiceMock<Stats::MockStore> stats;
  Runtime::RandomGeneratorImpl random;
  std::string not_traceable_guid = random.uuid();

  std::string forced_guid = random.uuid();
  UuidUtils::setTraceableUuid(forced_guid, UuidTraceStatus::Forced);
  Http::TestHeaderMapImpl forced_header{{"x-request-id", forced_guid}};

  std::string sampled_guid = random.uuid();
  UuidUtils::setTraceableUuid(sampled_guid, UuidTraceStatus::Sampled);
  Http::TestHeaderMapImpl sampled_header{{"x-request-id", sampled_guid}};

  std::string client_guid = random.uuid();
  UuidUtils::setTraceableUuid(client_guid, UuidTraceStatus::Client);
  Http::TestHeaderMapImpl client_header{{"x-request-id", client_guid}};

  Http::TestHeaderMapImpl not_traceable_header{{"x-request-id", not_traceable_guid}};
  Http::TestHeaderMapImpl empty_header{};

  // Force traced.
  {
    EXPECT_CALL(stream_info, healthCheck()).WillOnce(Return(false));

    Decision result = HttpTracerUtility::isTracing(stream_info, forced_header);
    EXPECT_EQ(Reason::ServiceForced, result.reason);
    EXPECT_TRUE(result.traced);
  }

  // Sample traced.
  {
    EXPECT_CALL(stream_info, healthCheck()).WillOnce(Return(false));

    Decision result = HttpTracerUtility::isTracing(stream_info, sampled_header);
    EXPECT_EQ(Reason::Sampling, result.reason);
    EXPECT_TRUE(result.traced);
  }

  // Health Check request.
  {
    Http::TestHeaderMapImpl traceable_header_hc{{"x-request-id", forced_guid}};
    EXPECT_CALL(stream_info, healthCheck()).WillOnce(Return(true));

    Decision result = HttpTracerUtility::isTracing(stream_info, traceable_header_hc);
    EXPECT_EQ(Reason::HealthCheck, result.reason);
    EXPECT_FALSE(result.traced);
  }

  // Client traced.
  {
    EXPECT_CALL(stream_info, healthCheck()).WillOnce(Return(false));

    Decision result = HttpTracerUtility::isTracing(stream_info, client_header);
    EXPECT_EQ(Reason::ClientForced, result.reason);
    EXPECT_TRUE(result.traced);
  }

  // No request id.
  {
    Http::TestHeaderMapImpl headers;
    EXPECT_CALL(stream_info, healthCheck()).WillOnce(Return(false));
    Decision result = HttpTracerUtility::isTracing(stream_info, headers);
    EXPECT_EQ(Reason::NotTraceableRequestId, result.reason);
    EXPECT_FALSE(result.traced);
  }

  // Broken request id.
  {
    Http::TestHeaderMapImpl headers{{"x-request-id", "not-real-x-request-id"}};
    EXPECT_CALL(stream_info, healthCheck()).WillOnce(Return(false));
    Decision result = HttpTracerUtility::isTracing(stream_info, headers);
    EXPECT_EQ(Reason::NotTraceableRequestId, result.reason);
    EXPECT_FALSE(result.traced);
  }
}

TEST(HttpConnManFinalizerImpl, OriginalAndLongPath) {
  const std::string path(300, 'a');
  const std::string path_prefix = "http://";
  const std::string expected_path(256, 'a');
  NiceMock<MockSpan> span;

  Http::TestHeaderMapImpl request_headers{{"x-request-id", "id"},
                                          {"x-envoy-original-path", path},
                                          {":method", "GET"},
                                          {"x-forwarded-proto", "http"}};
  Http::TestHeaderMapImpl response_headers;
  Http::TestHeaderMapImpl response_trailers;
  NiceMock<StreamInfo::MockStreamInfo> stream_info;

  absl::optional<Http::Protocol> protocol = Http::Protocol::Http2;
  EXPECT_CALL(stream_info, bytesReceived()).WillOnce(Return(10));
  EXPECT_CALL(stream_info, bytesSent()).WillOnce(Return(11));
  EXPECT_CALL(stream_info, protocol()).WillRepeatedly(ReturnPointee(&protocol));
  absl::optional<uint32_t> response_code;
  EXPECT_CALL(stream_info, responseCode()).WillRepeatedly(ReturnPointee(&response_code));

  EXPECT_CALL(span, setTag(_, _)).Times(testing::AnyNumber());
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().HttpUrl), Eq(path_prefix + expected_path)));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().HttpMethod), Eq("GET")));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().HttpProtocol), Eq("HTTP/2")));

  NiceMock<MockConfig> config;
  HttpTracerUtility::finalizeSpan(span, &request_headers, &response_headers, &response_trailers,
                                  stream_info, config);
}

TEST(HttpConnManFinalizerImpl, NoGeneratedId) {
  const std::string path(300, 'a');
  const std::string path_prefix = "http://";
  const std::string expected_path(256, 'a');
  NiceMock<MockSpan> span;

  Http::TestHeaderMapImpl request_headers{
      {"x-envoy-original-path", path}, {":method", "GET"}, {"x-forwarded-proto", "http"}};
  Http::TestHeaderMapImpl response_headers;
  Http::TestHeaderMapImpl response_trailers;
  NiceMock<StreamInfo::MockStreamInfo> stream_info;

  absl::optional<Http::Protocol> protocol = Http::Protocol::Http2;
  EXPECT_CALL(stream_info, bytesReceived()).WillOnce(Return(10));
  EXPECT_CALL(stream_info, bytesSent()).WillOnce(Return(11));
  EXPECT_CALL(stream_info, protocol()).WillRepeatedly(ReturnPointee(&protocol));
  absl::optional<uint32_t> response_code;
  EXPECT_CALL(stream_info, responseCode()).WillRepeatedly(ReturnPointee(&response_code));

  EXPECT_CALL(span, setTag(_, _)).Times(testing::AnyNumber());
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().HttpUrl), Eq(path_prefix + expected_path)));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().HttpMethod), Eq("GET")));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().HttpProtocol), Eq("HTTP/2")));

  NiceMock<MockConfig> config;
  HttpTracerUtility::finalizeSpan(span, &request_headers, &response_headers, &response_trailers,
                                  stream_info, config);
}

TEST(HttpConnManFinalizerImpl, NullRequestHeaders) {
  NiceMock<MockSpan> span;
  NiceMock<StreamInfo::MockStreamInfo> stream_info;

  EXPECT_CALL(stream_info, bytesReceived()).WillOnce(Return(10));
  EXPECT_CALL(stream_info, bytesSent()).WillOnce(Return(11));
  absl::optional<uint32_t> response_code;
  EXPECT_CALL(stream_info, responseCode()).WillRepeatedly(ReturnPointee(&response_code));
  EXPECT_CALL(stream_info, upstreamHost()).WillOnce(Return(nullptr));

  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().HttpStatusCode), Eq("0")));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().Error), Eq(Tracing::Tags::get().True)));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().ResponseSize), Eq("11")));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().ResponseFlags), Eq("-")));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().RequestSize), Eq("10")));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().UpstreamCluster), _)).Times(0);

  NiceMock<MockConfig> config;
  HttpTracerUtility::finalizeSpan(span, nullptr, nullptr, nullptr, stream_info, config);
}

TEST(HttpConnManFinalizerImpl, StreamInfoLogs) {
  NiceMock<MockSpan> span;
  NiceMock<StreamInfo::MockStreamInfo> stream_info;
  stream_info.host_->cluster_.name_ = "my_upstream_cluster";

  EXPECT_CALL(stream_info, bytesReceived()).WillOnce(Return(10));
  EXPECT_CALL(stream_info, bytesSent()).WillOnce(Return(11));
  absl::optional<uint32_t> response_code;
  EXPECT_CALL(stream_info, responseCode()).WillRepeatedly(ReturnPointee(&response_code));
  EXPECT_CALL(stream_info, upstreamHost()).Times(2);
  const auto start_timestamp =
      SystemTime{std::chrono::duration_cast<SystemTime::duration>(std::chrono::hours{123})};
  EXPECT_CALL(stream_info, startTime()).WillRepeatedly(Return(start_timestamp));

  const absl::optional<std::chrono::nanoseconds> nanoseconds = std::chrono::nanoseconds{10};
  EXPECT_CALL(stream_info, lastDownstreamRxByteReceived()).WillRepeatedly(Return(nanoseconds));
  EXPECT_CALL(stream_info, firstUpstreamTxByteSent()).WillRepeatedly(Return(nanoseconds));
  EXPECT_CALL(stream_info, lastUpstreamTxByteSent()).WillRepeatedly(Return(nanoseconds));
  EXPECT_CALL(stream_info, firstUpstreamRxByteReceived()).WillRepeatedly(Return(nanoseconds));
  EXPECT_CALL(stream_info, lastUpstreamRxByteReceived()).WillRepeatedly(Return(nanoseconds));
  EXPECT_CALL(stream_info, firstDownstreamTxByteSent()).WillRepeatedly(Return(nanoseconds));
  EXPECT_CALL(stream_info, lastDownstreamTxByteSent()).WillRepeatedly(Return(nanoseconds));

  const auto log_timestamp =
      start_timestamp + std::chrono::duration_cast<SystemTime::duration>(*nanoseconds);
  EXPECT_CALL(span, log(log_timestamp, Tracing::Logs::get().LastDownstreamRxByteReceived));
  EXPECT_CALL(span, log(log_timestamp, Tracing::Logs::get().FirstUpstreamTxByteSent));
  EXPECT_CALL(span, log(log_timestamp, Tracing::Logs::get().LastUpstreamTxByteSent));
  EXPECT_CALL(span, log(log_timestamp, Tracing::Logs::get().FirstUpstreamRxByteReceived));
  EXPECT_CALL(span, log(log_timestamp, Tracing::Logs::get().LastUpstreamRxByteReceived));
  EXPECT_CALL(span, log(log_timestamp, Tracing::Logs::get().FirstDownstreamTxByteSent));
  EXPECT_CALL(span, log(log_timestamp, Tracing::Logs::get().LastDownstreamTxByteSent));

  NiceMock<MockConfig> config;
  EXPECT_CALL(config, verbose).WillOnce(Return(true));
  HttpTracerUtility::finalizeSpan(span, nullptr, nullptr, nullptr, stream_info, config);
}

TEST(HttpConnManFinalizerImpl, UpstreamClusterTagSet) {
  NiceMock<MockSpan> span;
  NiceMock<StreamInfo::MockStreamInfo> stream_info;
  stream_info.host_->cluster_.name_ = "my_upstream_cluster";

  EXPECT_CALL(stream_info, bytesReceived()).WillOnce(Return(10));
  EXPECT_CALL(stream_info, bytesSent()).WillOnce(Return(11));
  absl::optional<uint32_t> response_code;
  EXPECT_CALL(stream_info, responseCode()).WillRepeatedly(ReturnPointee(&response_code));
  EXPECT_CALL(stream_info, upstreamHost()).Times(2);

  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().UpstreamCluster), Eq("my_upstream_cluster")));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().HttpStatusCode), Eq("0")));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().Error), Eq(Tracing::Tags::get().True)));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().ResponseSize), Eq("11")));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().ResponseFlags), Eq("-")));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().RequestSize), Eq("10")));

  NiceMock<MockConfig> config;
  HttpTracerUtility::finalizeSpan(span, nullptr, nullptr, nullptr, stream_info, config);
}

TEST(HttpConnManFinalizerImpl, SpanOptionalHeaders) {
  NiceMock<MockSpan> span;

  Http::TestHeaderMapImpl request_headers{{"x-request-id", "id"},
                                          {":path", "/test"},
                                          {":method", "GET"},
                                          {"x-forwarded-proto", "https"}};
  Http::TestHeaderMapImpl response_headers;
  Http::TestHeaderMapImpl response_trailers;
  NiceMock<StreamInfo::MockStreamInfo> stream_info;

  absl::optional<Http::Protocol> protocol = Http::Protocol::Http10;
  EXPECT_CALL(stream_info, bytesReceived()).WillOnce(Return(10));
  EXPECT_CALL(stream_info, protocol()).WillRepeatedly(ReturnPointee(&protocol));
  const std::string service_node = "i-453";

  // Check that span is populated correctly.
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().GuidXRequestId), Eq("id")));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().HttpUrl), Eq("https:///test")));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().HttpMethod), Eq("GET")));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().UserAgent), Eq("-")));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().HttpProtocol), Eq("HTTP/1.0")));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().DownstreamCluster), Eq("-")));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().RequestSize), Eq("10")));

  absl::optional<uint32_t> response_code;
  EXPECT_CALL(stream_info, responseCode()).WillRepeatedly(ReturnPointee(&response_code));
  EXPECT_CALL(stream_info, bytesSent()).WillOnce(Return(100));
  EXPECT_CALL(stream_info, upstreamHost()).WillOnce(Return(nullptr));

  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().HttpStatusCode), Eq("0")));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().Error), Eq(Tracing::Tags::get().True)));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().ResponseSize), Eq("100")));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().ResponseFlags), Eq("-")));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().UpstreamCluster), _)).Times(0);

  NiceMock<MockConfig> config;
  HttpTracerUtility::finalizeSpan(span, &request_headers, &response_headers, &response_trailers,
                                  stream_info, config);
}

TEST(HttpConnManFinalizerImpl, SpanPopulatedFailureResponse) {
  NiceMock<MockSpan> span;
  Http::TestHeaderMapImpl request_headers{{"x-request-id", "id"},
                                          {":path", "/test"},
                                          {":method", "GET"},
                                          {"x-forwarded-proto", "http"}};
  Http::TestHeaderMapImpl response_headers;
  Http::TestHeaderMapImpl response_trailers;
  NiceMock<StreamInfo::MockStreamInfo> stream_info;

  request_headers.insertHost().value(std::string("api"));
  request_headers.insertUserAgent().value(std::string("agent"));
  request_headers.insertEnvoyDownstreamServiceCluster().value(std::string("downstream_cluster"));
  request_headers.insertClientTraceId().value(std::string("client_trace_id"));

  absl::optional<Http::Protocol> protocol = Http::Protocol::Http10;
  EXPECT_CALL(stream_info, protocol()).WillRepeatedly(ReturnPointee(&protocol));
  EXPECT_CALL(stream_info, bytesReceived()).WillOnce(Return(10));
  const std::string service_node = "i-453";

  // Check that span is populated correctly.
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().GuidXRequestId), Eq("id")));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().HttpUrl), Eq("http://api/test")));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().HttpMethod), Eq("GET")));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().UserAgent), Eq("agent")));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().HttpProtocol), Eq("HTTP/1.0")));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().DownstreamCluster), Eq("downstream_cluster")));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().RequestSize), Eq("10")));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().GuidXClientTraceId), Eq("client_trace_id")));

  // Check that span has tags from custom headers.
  request_headers.addCopy(Http::LowerCaseString("aa"), "a");
  request_headers.addCopy(Http::LowerCaseString("bb"), "b");
  request_headers.addCopy(Http::LowerCaseString("cc"), "c");
  MockConfig config;
  config.headers_.push_back(Http::LowerCaseString("aa"));
  config.headers_.push_back(Http::LowerCaseString("cc"));
  config.headers_.push_back(Http::LowerCaseString("ee"));
  EXPECT_CALL(span, setTag(Eq("aa"), Eq("a")));
  EXPECT_CALL(span, setTag(Eq("cc"), Eq("c")));
  EXPECT_CALL(config, requestHeadersForTags());
  EXPECT_CALL(config, verbose).WillOnce(Return(false));

  absl::optional<uint32_t> response_code(503);
  EXPECT_CALL(stream_info, responseCode()).WillRepeatedly(ReturnPointee(&response_code));
  EXPECT_CALL(stream_info, bytesSent()).WillOnce(Return(100));
  ON_CALL(stream_info, hasResponseFlag(StreamInfo::ResponseFlag::UpstreamRequestTimeout))
      .WillByDefault(Return(true));
  EXPECT_CALL(stream_info, upstreamHost()).WillOnce(Return(nullptr));

  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().Error), Eq(Tracing::Tags::get().True)));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().HttpStatusCode), Eq("503")));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().ResponseSize), Eq("100")));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().ResponseFlags), Eq("UT")));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().UpstreamCluster), _)).Times(0);

  HttpTracerUtility::finalizeSpan(span, &request_headers, &response_headers, &response_trailers,
                                  stream_info, config);
}

TEST(HttpConnManFinalizerImpl, GrpcOkStatus) {
  const std::string path_prefix = "http://";
  NiceMock<MockSpan> span;

  Http::TestHeaderMapImpl request_headers{{":method", "POST"},
                                          {":scheme", "http"},
                                          {":path", "/pb.Foo/Bar"},
                                          {":authority", "example.com:80"},
                                          {"content-type", "application/grpc"},
                                          {"te", "trailers"}};

  Http::TestHeaderMapImpl response_headers{{":status", "200"},
                                           {"content-type", "application/grpc"}};
  Http::TestHeaderMapImpl response_trailers{{"grpc-status", "0"}, {"grpc-message", ""}};
  NiceMock<StreamInfo::MockStreamInfo> stream_info;

  absl::optional<Http::Protocol> protocol = Http::Protocol::Http2;
  absl::optional<uint32_t> response_code(200);
  EXPECT_CALL(stream_info, responseCode()).WillRepeatedly(ReturnPointee(&response_code));
  EXPECT_CALL(stream_info, bytesReceived()).WillOnce(Return(10));
  EXPECT_CALL(stream_info, bytesSent()).WillOnce(Return(11));
  EXPECT_CALL(stream_info, protocol()).WillRepeatedly(ReturnPointee(&protocol));

  EXPECT_CALL(span, setTag(_, _)).Times(testing::AnyNumber());
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().HttpMethod), Eq("POST")));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().HttpProtocol), Eq("HTTP/2")));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().HttpStatusCode), Eq("200")));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().GrpcStatusCode), Eq("0")));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().GrpcMessage), Eq("")));

  NiceMock<MockConfig> config;
  HttpTracerUtility::finalizeSpan(span, &request_headers, &response_headers, &response_trailers,
                                  stream_info, config);
}

TEST(HttpConnManFinalizerImpl, GrpcErrorTag) {
  const std::string path_prefix = "http://";
  NiceMock<MockSpan> span;

  Http::TestHeaderMapImpl request_headers{{":method", "POST"},
                                          {":scheme", "http"},
                                          {":path", "/pb.Foo/Bar"},
                                          {":authority", "example.com:80"},
                                          {"content-type", "application/grpc"},
                                          {"te", "trailers"}};

  Http::TestHeaderMapImpl response_headers{{":status", "200"},
                                           {"content-type", "application/grpc"}};
  Http::TestHeaderMapImpl response_trailers{{"grpc-status", "7"},
                                            {"grpc-message", "permission denied"}};
  NiceMock<StreamInfo::MockStreamInfo> stream_info;

  absl::optional<Http::Protocol> protocol = Http::Protocol::Http2;
  absl::optional<uint32_t> response_code(200);
  EXPECT_CALL(stream_info, responseCode()).WillRepeatedly(ReturnPointee(&response_code));
  EXPECT_CALL(stream_info, bytesReceived()).WillOnce(Return(10));
  EXPECT_CALL(stream_info, bytesSent()).WillOnce(Return(11));
  EXPECT_CALL(stream_info, protocol()).WillRepeatedly(ReturnPointee(&protocol));

  EXPECT_CALL(span, setTag(_, _)).Times(testing::AnyNumber());
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().Error), Eq(Tracing::Tags::get().True)));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().HttpMethod), Eq("POST")));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().HttpProtocol), Eq("HTTP/2")));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().HttpStatusCode), Eq("200")));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().GrpcStatusCode), Eq("7")));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().GrpcMessage), Eq("permission denied")));

  NiceMock<MockConfig> config;
  HttpTracerUtility::finalizeSpan(span, &request_headers, &response_headers, &response_trailers,
                                  stream_info, config);
}

TEST(HttpConnManFinalizerImpl, GrpcTrailersOnly) {
  const std::string path_prefix = "http://";
  NiceMock<MockSpan> span;

  Http::TestHeaderMapImpl request_headers{{":method", "POST"},
                                          {":scheme", "http"},
                                          {":path", "/pb.Foo/Bar"},
                                          {":authority", "example.com:80"},
                                          {"content-type", "application/grpc"},
                                          {"te", "trailers"}};

  Http::TestHeaderMapImpl response_headers{{":status", "200"},
                                           {"content-type", "application/grpc"},
                                           {"grpc-status", "7"},
                                           {"grpc-message", "permission denied"}};
  Http::TestHeaderMapImpl response_trailers;
  NiceMock<StreamInfo::MockStreamInfo> stream_info;

  absl::optional<Http::Protocol> protocol = Http::Protocol::Http2;
  absl::optional<uint32_t> response_code(200);
  EXPECT_CALL(stream_info, responseCode()).WillRepeatedly(ReturnPointee(&response_code));
  EXPECT_CALL(stream_info, bytesReceived()).WillOnce(Return(10));
  EXPECT_CALL(stream_info, bytesSent()).WillOnce(Return(11));
  EXPECT_CALL(stream_info, protocol()).WillRepeatedly(ReturnPointee(&protocol));

  EXPECT_CALL(span, setTag(_, _)).Times(testing::AnyNumber());
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().Error), Eq(Tracing::Tags::get().True)));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().HttpMethod), Eq("POST")));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().HttpProtocol), Eq("HTTP/2")));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().HttpStatusCode), Eq("200")));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().GrpcStatusCode), Eq("7")));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().GrpcMessage), Eq("permission denied")));

  NiceMock<MockConfig> config;
  HttpTracerUtility::finalizeSpan(span, &request_headers, &response_headers, &response_trailers,
                                  stream_info, config);
}

TEST(HttpTracerUtilityTest, operationTypeToString) {
  EXPECT_EQ("ingress", HttpTracerUtility::toString(OperationName::Ingress));
  EXPECT_EQ("egress", HttpTracerUtility::toString(OperationName::Egress));
}

TEST(HttpNullTracerTest, BasicFunctionality) {
  HttpNullTracer null_tracer;
  MockConfig config;
  StreamInfo::MockStreamInfo stream_info;
  Http::TestHeaderMapImpl request_headers;
  Http::TestHeaderMapImpl response_headers;
  Http::TestHeaderMapImpl response_trailers;

  SpanPtr span_ptr =
      null_tracer.startSpan(config, request_headers, stream_info, {Reason::Sampling, true});
  EXPECT_TRUE(dynamic_cast<NullSpan*>(span_ptr.get()) != nullptr);

  span_ptr->setOperation("foo");
  span_ptr->setTag("foo", "bar");
  span_ptr->injectContext(request_headers);

  EXPECT_NE(nullptr, span_ptr->spawnChild(config, "foo", SystemTime()));
}

class HttpTracerImplTest : public testing::Test {
public:
  HttpTracerImplTest() {
    driver_ = new MockDriver();
    DriverPtr driver_ptr(driver_);
    tracer_ = std::make_unique<HttpTracerImpl>(std::move(driver_ptr), local_info_);
  }

  Http::TestHeaderMapImpl request_headers_{
      {":path", "/"}, {":method", "GET"}, {"x-request-id", "foo"}, {":authority", "test"}};
  Http::TestHeaderMapImpl response_headers;
  Http::TestHeaderMapImpl response_trailers;
  StreamInfo::MockStreamInfo stream_info_;
  NiceMock<LocalInfo::MockLocalInfo> local_info_;
  MockConfig config_;
  MockDriver* driver_;
  HttpTracerPtr tracer_;
};

TEST_F(HttpTracerImplTest, BasicFunctionalityNullSpan) {
  EXPECT_CALL(config_, operationName()).Times(2);
  EXPECT_CALL(stream_info_, startTime());
  const std::string operation_name = "ingress";
  EXPECT_CALL(*driver_, startSpan_(_, _, operation_name, stream_info_.start_time_, _))
      .WillOnce(Return(nullptr));
  tracer_->startSpan(config_, request_headers_, stream_info_, {Reason::Sampling, true});
}

TEST_F(HttpTracerImplTest, BasicFunctionalityNodeSet) {
  EXPECT_CALL(stream_info_, startTime());
  EXPECT_CALL(local_info_, nodeName());
  EXPECT_CALL(config_, operationName()).Times(2).WillRepeatedly(Return(OperationName::Egress));

  NiceMock<MockSpan>* span = new NiceMock<MockSpan>();
  const std::string operation_name = "egress test";
  EXPECT_CALL(*driver_, startSpan_(_, _, operation_name, stream_info_.start_time_, _))
      .WillOnce(Return(span));
  EXPECT_CALL(*span, setTag(_, _)).Times(testing::AnyNumber());
  EXPECT_CALL(*span, setTag(Eq(Tracing::Tags::get().NodeId), Eq("node_name")));

  tracer_->startSpan(config_, request_headers_, stream_info_, {Reason::Sampling, true});
}

} // namespace
} // namespace Tracing
} // namespace Envoy
