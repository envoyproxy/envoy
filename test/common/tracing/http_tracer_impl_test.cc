#include <cstdint>
#include <memory>
#include <string>

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/type/tracing/v3/custom_tag.pb.h"

#include "common/common/base64.h"
#include "common/http/header_map_impl.h"
#include "common/http/headers.h"
#include "common/http/message_impl.h"
#include "common/network/utility.h"
#include "common/runtime/runtime_impl.h"
#include "common/runtime/uuid_util.h"
#include "common/tracing/http_tracer_impl.h"

#include "test/mocks/http/mocks.h"
#include "test/mocks/local_info/mocks.h"
#include "test/mocks/router/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/stats/mocks.h"
#include "test/mocks/thread_local/mocks.h"
#include "test/mocks/tracing/mocks.h"
#include "test/mocks/upstream/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::AnyNumber;
using testing::Eq;
using testing::NiceMock;
using testing::Return;
using testing::ReturnPointee;

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
  Http::TestRequestHeaderMapImpl forced_header{{"x-request-id", forced_guid}};

  std::string sampled_guid = random.uuid();
  UuidUtils::setTraceableUuid(sampled_guid, UuidTraceStatus::Sampled);
  Http::TestRequestHeaderMapImpl sampled_header{{"x-request-id", sampled_guid}};

  std::string client_guid = random.uuid();
  UuidUtils::setTraceableUuid(client_guid, UuidTraceStatus::Client);
  Http::TestRequestHeaderMapImpl client_header{{"x-request-id", client_guid}};

  Http::TestRequestHeaderMapImpl not_traceable_header{{"x-request-id", not_traceable_guid}};
  Http::TestRequestHeaderMapImpl empty_header{};

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
    Http::TestRequestHeaderMapImpl traceable_header_hc{{"x-request-id", forced_guid}};
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
    Http::TestRequestHeaderMapImpl headers;
    EXPECT_CALL(stream_info, healthCheck()).WillOnce(Return(false));
    Decision result = HttpTracerUtility::isTracing(stream_info, headers);
    EXPECT_EQ(Reason::NotTraceableRequestId, result.reason);
    EXPECT_FALSE(result.traced);
  }

  // Broken request id.
  {
    Http::TestRequestHeaderMapImpl headers{{"x-request-id", "not-real-x-request-id"}};
    EXPECT_CALL(stream_info, healthCheck()).WillOnce(Return(false));
    Decision result = HttpTracerUtility::isTracing(stream_info, headers);
    EXPECT_EQ(Reason::NotTraceableRequestId, result.reason);
    EXPECT_FALSE(result.traced);
  }
}

class HttpConnManFinalizerImplTest : public testing::Test {
protected:
  struct CustomTagCase {
    std::string custom_tag;
    bool set;
    std::string value;
  };

  void expectSetCustomTags(const std::vector<CustomTagCase>& cases) {
    for (const CustomTagCase& cas : cases) {
      envoy::type::tracing::v3::CustomTag custom_tag;
      TestUtility::loadFromYaml(cas.custom_tag, custom_tag);
      config.custom_tags_.emplace(custom_tag.tag(), HttpTracerUtility::createCustomTag(custom_tag));
      if (cas.set) {
        EXPECT_CALL(span, setTag(Eq(custom_tag.tag()), Eq(cas.value)));
      } else {
        EXPECT_CALL(span, setTag(Eq(custom_tag.tag()), _)).Times(0);
      }
    }
  }

  NiceMock<MockSpan> span;
  NiceMock<MockConfig> config;
  NiceMock<StreamInfo::MockStreamInfo> stream_info;
};

TEST_F(HttpConnManFinalizerImplTest, OriginalAndLongPath) {
  const std::string path(300, 'a');
  const std::string path_prefix = "http://";
  const std::string expected_path(256, 'a');
  const std::string expected_ip = "10.0.0.100";
  const auto remote_address =
      Network::Address::InstanceConstSharedPtr{new Network::Address::Ipv4Instance(expected_ip, 0)};

  Http::TestRequestHeaderMapImpl request_headers{{"x-request-id", "id"},
                                                 {"x-envoy-original-path", path},
                                                 {":method", "GET"},
                                                 {"x-forwarded-proto", "http"}};
  Http::TestResponseHeaderMapImpl response_headers;
  Http::TestResponseTrailerMapImpl response_trailers;

  absl::optional<Http::Protocol> protocol = Http::Protocol::Http2;
  EXPECT_CALL(stream_info, bytesReceived()).WillOnce(Return(10));
  EXPECT_CALL(stream_info, bytesSent()).WillOnce(Return(11));
  EXPECT_CALL(stream_info, protocol()).WillRepeatedly(ReturnPointee(&protocol));
  absl::optional<uint32_t> response_code;
  EXPECT_CALL(stream_info, responseCode()).WillRepeatedly(ReturnPointee(&response_code));
  EXPECT_CALL(stream_info, downstreamDirectRemoteAddress())
      .WillRepeatedly(ReturnPointee(&remote_address));

  EXPECT_CALL(span, setTag(_, _)).Times(testing::AnyNumber());
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().HttpUrl), Eq(path_prefix + expected_path)));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().HttpMethod), Eq("GET")));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().HttpProtocol), Eq("HTTP/2")));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().PeerAddress), Eq(expected_ip)));

  HttpTracerUtility::finalizeDownstreamSpan(span, &request_headers, &response_headers,
                                            &response_trailers, stream_info, config);
}

TEST_F(HttpConnManFinalizerImplTest, NoGeneratedId) {
  const std::string path(300, 'a');
  const std::string path_prefix = "http://";
  const std::string expected_path(256, 'a');
  const std::string expected_ip = "10.0.0.100";
  const auto remote_address =
      Network::Address::InstanceConstSharedPtr{new Network::Address::Ipv4Instance(expected_ip, 0)};

  Http::TestRequestHeaderMapImpl request_headers{
      {"x-envoy-original-path", path}, {":method", "GET"}, {"x-forwarded-proto", "http"}};
  Http::TestResponseHeaderMapImpl response_headers;
  Http::TestResponseTrailerMapImpl response_trailers;

  absl::optional<Http::Protocol> protocol = Http::Protocol::Http2;
  EXPECT_CALL(stream_info, bytesReceived()).WillOnce(Return(10));
  EXPECT_CALL(stream_info, bytesSent()).WillOnce(Return(11));
  EXPECT_CALL(stream_info, protocol()).WillRepeatedly(ReturnPointee(&protocol));
  absl::optional<uint32_t> response_code;
  EXPECT_CALL(stream_info, responseCode()).WillRepeatedly(ReturnPointee(&response_code));
  EXPECT_CALL(stream_info, downstreamDirectRemoteAddress())
      .WillRepeatedly(ReturnPointee(&remote_address));

  EXPECT_CALL(span, setTag(_, _)).Times(testing::AnyNumber());
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().HttpUrl), Eq(path_prefix + expected_path)));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().HttpMethod), Eq("GET")));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().HttpProtocol), Eq("HTTP/2")));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().PeerAddress), Eq(expected_ip)));

  HttpTracerUtility::finalizeDownstreamSpan(span, &request_headers, &response_headers,
                                            &response_trailers, stream_info, config);
}

TEST_F(HttpConnManFinalizerImplTest, NullRequestHeadersAndNullRouteEntry) {
  EXPECT_CALL(stream_info, bytesReceived()).WillOnce(Return(10));
  EXPECT_CALL(stream_info, bytesSent()).WillOnce(Return(11));
  absl::optional<uint32_t> response_code;
  EXPECT_CALL(stream_info, responseCode()).WillRepeatedly(ReturnPointee(&response_code));
  EXPECT_CALL(stream_info, upstreamHost()).WillRepeatedly(Return(nullptr));
  EXPECT_CALL(stream_info, routeEntry()).WillRepeatedly(Return(nullptr));

  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().HttpStatusCode), Eq("0")));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().Error), Eq(Tracing::Tags::get().True)));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().ResponseSize), Eq("11")));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().ResponseFlags), Eq("-")));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().RequestSize), Eq("10")));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().Component), Eq(Tracing::Tags::get().Proxy)));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().UpstreamAddress), _)).Times(0);
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().UpstreamCluster), _)).Times(0);

  expectSetCustomTags({{"{ tag: a, request_header: { name: X-Ax } }", false, ""},
                       {R"EOF(
tag: b
metadata:
  kind: { route: {} }
  metadata_key: { key: m.rot, path: [ {key: not-found } ] }
  default_value: _c)EOF",
                        true, "_c"},
                       {R"EOF(
tag: c
metadata:
  kind: { cluster: {} }
  metadata_key: { key: m.cluster, path: [ {key: not-found } ] })EOF",
                        false, ""},
                       {R"EOF(
tag: d
metadata:
  kind: { host: {} }
  metadata_key: { key: m.host, path: [ {key: not-found } ] })EOF",
                        false, ""}});

  HttpTracerUtility::finalizeDownstreamSpan(span, nullptr, nullptr, nullptr, stream_info, config);
}

TEST_F(HttpConnManFinalizerImplTest, StreamInfoLogs) {
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

  EXPECT_CALL(config, verbose).WillOnce(Return(true));
  HttpTracerUtility::finalizeDownstreamSpan(span, nullptr, nullptr, nullptr, stream_info, config);
}

TEST_F(HttpConnManFinalizerImplTest, UpstreamClusterTagSet) {
  stream_info.host_->cluster_.name_ = "my_upstream_cluster";

  EXPECT_CALL(stream_info, bytesReceived()).WillOnce(Return(10));
  EXPECT_CALL(stream_info, bytesSent()).WillOnce(Return(11));
  absl::optional<uint32_t> response_code;
  EXPECT_CALL(stream_info, responseCode()).WillRepeatedly(ReturnPointee(&response_code));
  EXPECT_CALL(stream_info, upstreamHost()).Times(2);

  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().Component), Eq(Tracing::Tags::get().Proxy)));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().UpstreamCluster), Eq("my_upstream_cluster")));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().HttpStatusCode), Eq("0")));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().Error), Eq(Tracing::Tags::get().True)));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().ResponseSize), Eq("11")));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().ResponseFlags), Eq("-")));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().RequestSize), Eq("10")));

  HttpTracerUtility::finalizeDownstreamSpan(span, nullptr, nullptr, nullptr, stream_info, config);
}

TEST_F(HttpConnManFinalizerImplTest, SpanOptionalHeaders) {
  Http::TestRequestHeaderMapImpl request_headers{{"x-request-id", "id"},
                                                 {":path", "/test"},
                                                 {":method", "GET"},
                                                 {"x-forwarded-proto", "https"}};
  Http::TestResponseHeaderMapImpl response_headers;
  Http::TestResponseTrailerMapImpl response_trailers;
  const std::string expected_ip = "10.0.0.100";
  const auto remote_address =
      Network::Address::InstanceConstSharedPtr{new Network::Address::Ipv4Instance(expected_ip, 0)};

  absl::optional<Http::Protocol> protocol = Http::Protocol::Http10;
  EXPECT_CALL(stream_info, bytesReceived()).WillOnce(Return(10));
  EXPECT_CALL(stream_info, protocol()).WillRepeatedly(ReturnPointee(&protocol));
  EXPECT_CALL(stream_info, downstreamDirectRemoteAddress())
      .WillRepeatedly(ReturnPointee(&remote_address));

  // Check that span is populated correctly.
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().GuidXRequestId), Eq("id")));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().HttpUrl), Eq("https:///test")));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().HttpMethod), Eq("GET")));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().UserAgent), Eq("-")));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().HttpProtocol), Eq("HTTP/1.0")));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().DownstreamCluster), Eq("-")));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().RequestSize), Eq("10")));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().PeerAddress), Eq(expected_ip)));

  absl::optional<uint32_t> response_code;
  EXPECT_CALL(stream_info, responseCode()).WillRepeatedly(ReturnPointee(&response_code));
  EXPECT_CALL(stream_info, bytesSent()).WillOnce(Return(100));
  EXPECT_CALL(stream_info, upstreamHost()).WillOnce(Return(nullptr));

  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().HttpStatusCode), Eq("0")));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().Error), Eq(Tracing::Tags::get().True)));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().ResponseSize), Eq("100")));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().ResponseFlags), Eq("-")));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().Component), Eq(Tracing::Tags::get().Proxy)));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().UpstreamAddress), _)).Times(0);
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().UpstreamCluster), _)).Times(0);

  HttpTracerUtility::finalizeDownstreamSpan(span, &request_headers, &response_headers,
                                            &response_trailers, stream_info, config);
}

TEST_F(HttpConnManFinalizerImplTest, UnixDomainSocketPeerAddressTag) {
  Http::TestRequestHeaderMapImpl request_headers{{"x-request-id", "id"},
                                                 {":path", "/test"},
                                                 {":method", "GET"},
                                                 {"x-forwarded-proto", "https"}};
  Http::TestResponseHeaderMapImpl response_headers;
  Http::TestResponseTrailerMapImpl response_trailers;
  const std::string path_{TestEnvironment::unixDomainSocketPath("foo")};
  const auto remote_address = Network::Utility::resolveUrl("unix://" + path_);

  EXPECT_CALL(stream_info, downstreamDirectRemoteAddress())
      .WillRepeatedly(ReturnPointee(&remote_address));

  // Check that the PeerAddress is populated correctly for Unix domain sockets.
  EXPECT_CALL(span, setTag(_, _)).Times(AnyNumber());
  EXPECT_CALL(span,
              setTag(Eq(Tracing::Tags::get().PeerAddress), Eq(remote_address->logicalName())));

  HttpTracerUtility::finalizeDownstreamSpan(span, &request_headers, &response_headers,
                                            &response_trailers, stream_info, config);
}

TEST_F(HttpConnManFinalizerImplTest, SpanCustomTags) {
  TestEnvironment::setEnvVar("E_CC", "c", 1);

  Http::TestRequestHeaderMapImpl request_headers{{"x-request-id", "id"},
                                                 {":path", "/test"},
                                                 {":method", "GET"},
                                                 {"x-forwarded-proto", "https"},
                                                 {"x-bb", "b"}};

  ProtobufWkt::Struct fake_struct;
  std::string yaml = R"EOF(
ree:
  foo: bar
  nuu: 1
  boo: true
  poo: false
  stt: { some: thing }
  lii: [ something ]
  emp: "")EOF";
  TestUtility::loadFromYaml(yaml, fake_struct);
  (*stream_info.metadata_.mutable_filter_metadata())["m.req"].MergeFrom(fake_struct);
  NiceMock<Router::MockRouteEntry> route_entry;
  EXPECT_CALL(stream_info, routeEntry()).WillRepeatedly(Return(&route_entry));
  (*route_entry.metadata_.mutable_filter_metadata())["m.rot"].MergeFrom(fake_struct);
  std::shared_ptr<envoy::config::core::v3::Metadata> host_metadata =
      std::make_shared<envoy::config::core::v3::Metadata>();
  (*host_metadata->mutable_filter_metadata())["m.host"].MergeFrom(fake_struct);
  (*stream_info.host_->cluster_.metadata_.mutable_filter_metadata())["m.cluster"].MergeFrom(
      fake_struct);

  absl::optional<Http::Protocol> protocol = Http::Protocol::Http10;
  EXPECT_CALL(stream_info, bytesReceived()).WillOnce(Return(10));
  EXPECT_CALL(stream_info, protocol()).WillRepeatedly(ReturnPointee(&protocol));
  absl::optional<uint32_t> response_code;
  EXPECT_CALL(stream_info, responseCode()).WillRepeatedly(ReturnPointee(&response_code));
  EXPECT_CALL(stream_info, bytesSent()).WillOnce(Return(100));
  EXPECT_CALL(*stream_info.host_, metadata()).WillRepeatedly(Return(host_metadata));

  EXPECT_CALL(config, customTags());
  EXPECT_CALL(span, setTag(_, _)).Times(testing::AnyNumber());

  expectSetCustomTags(
      {{"{ tag: aa, literal: { value: a } }", true, "a"},
       {"{ tag: bb-1, request_header: { name: X-Bb, default_value: _b } }", true, "b"},
       {"{ tag: bb-2, request_header: { name: X-Bb-Not-Found, default_value: b2 } }", true, "b2"},
       {"{ tag: bb-3, request_header: { name: X-Bb-Not-Found } }", false, ""},
       {"{ tag: cc-1, environment: { name: E_CC } }", true, "c"},
       {"{ tag: cc-1-a, environment: { name: E_CC, default_value: _c } }", true, "c"},
       {"{ tag: cc-2, environment: { name: E_CC_NOT_FOUND, default_value: c2 } }", true, "c2"},
       {"{ tag: cc-3, environment: { name: E_CC_NOT_FOUND} }", false, ""},
       {R"EOF(
tag: dd-1,
metadata:
  kind: { request: {} }
  metadata_key: { key: m.req, path: [ { key: ree }, { key: foo } ] })EOF",
        true, "bar"},
       {R"EOF(
tag: dd-2,
metadata:
  kind: { request: {} }
  metadata_key: { key: m.req, path: [ { key: not-found } ] }
  default_value: d2)EOF",
        true, "d2"},
       {R"EOF(
tag: dd-3,
metadata:
  kind: { request: {} }
  metadata_key: { key: m.req, path: [ { key: not-found } ] })EOF",
        false, ""},
       {R"EOF(
tag: dd-4,
metadata:
  kind: { request: {} }
  metadata_key: { key: m.req, path: [ { key: ree }, { key: nuu } ] }
  default_value: _d)EOF",
        true, "1"},
       {R"EOF(
tag: dd-5,
metadata:
  kind: { route: {} }
  metadata_key: { key: m.rot, path: [ { key: ree }, { key: boo } ] })EOF",
        true, "true"},
       {R"EOF(
tag: dd-6,
metadata:
  kind: { route: {} }
  metadata_key: { key: m.rot, path: [ { key: ree }, { key: poo } ] })EOF",
        true, "false"},
       {R"EOF(
tag: dd-7,
metadata:
  kind: { cluster: {} }
  metadata_key: { key: m.cluster, path: [ { key: ree }, { key: emp } ] }
  default_value: _d)EOF",
        true, ""},
       {R"EOF(
tag: dd-8,
metadata:
  kind: { cluster: {} }
  metadata_key: { key: m.cluster, path: [ { key: ree }, { key: lii } ] }
  default_value: _d)EOF",
        true, "[\"something\"]"},
       {R"EOF(
tag: dd-9,
metadata:
  kind: { host: {} }
  metadata_key: { key: m.host, path: [ { key: ree }, { key: stt } ] })EOF",
        true, R"({"some":"thing"})"},
       {R"EOF(
tag: dd-10,
metadata:
  kind: { host: {} }
  metadata_key: { key: m.host, path: [ { key: not-found } ] })EOF",
        false, ""}});

  HttpTracerUtility::finalizeDownstreamSpan(span, &request_headers, nullptr, nullptr, stream_info,
                                            config);
}

TEST_F(HttpConnManFinalizerImplTest, SpanPopulatedFailureResponse) {
  Http::TestRequestHeaderMapImpl request_headers{{"x-request-id", "id"},
                                                 {":path", "/test"},
                                                 {":method", "GET"},
                                                 {"x-forwarded-proto", "http"}};
  Http::TestResponseHeaderMapImpl response_headers;
  Http::TestResponseTrailerMapImpl response_trailers;
  const std::string expected_ip = "10.0.0.100";
  const auto remote_address =
      Network::Address::InstanceConstSharedPtr{new Network::Address::Ipv4Instance(expected_ip, 0)};

  request_headers.setHost("api");
  request_headers.setUserAgent("agent");
  request_headers.setEnvoyDownstreamServiceCluster("downstream_cluster");
  request_headers.setClientTraceId("client_trace_id");

  absl::optional<Http::Protocol> protocol = Http::Protocol::Http10;
  EXPECT_CALL(stream_info, protocol()).WillRepeatedly(ReturnPointee(&protocol));
  EXPECT_CALL(stream_info, bytesReceived()).WillOnce(Return(10));
  EXPECT_CALL(stream_info, downstreamDirectRemoteAddress())
      .WillRepeatedly(ReturnPointee(&remote_address));

  // Check that span is populated correctly.
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().GuidXRequestId), Eq("id")));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().HttpUrl), Eq("http://api/test")));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().HttpMethod), Eq("GET")));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().UserAgent), Eq("agent")));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().HttpProtocol), Eq("HTTP/1.0")));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().DownstreamCluster), Eq("downstream_cluster")));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().RequestSize), Eq("10")));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().GuidXClientTraceId), Eq("client_trace_id")));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().PeerAddress), Eq(expected_ip)));

  EXPECT_CALL(config, verbose).WillOnce(Return(false));
  EXPECT_CALL(config, maxPathTagLength).WillOnce(Return(256));

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
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().Component), Eq(Tracing::Tags::get().Proxy)));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().UpstreamAddress), _)).Times(0);
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().UpstreamCluster), _)).Times(0);

  HttpTracerUtility::finalizeDownstreamSpan(span, &request_headers, &response_headers,
                                            &response_trailers, stream_info, config);
}

TEST_F(HttpConnManFinalizerImplTest, GrpcOkStatus) {
  const std::string path_prefix = "http://";
  const std::string expected_ip = "10.0.0.100";
  const auto remote_address =
      Network::Address::InstanceConstSharedPtr{new Network::Address::Ipv4Instance(expected_ip, 0)};

  Http::TestRequestHeaderMapImpl request_headers{{":method", "POST"},
                                                 {":scheme", "http"},
                                                 {":path", "/pb.Foo/Bar"},
                                                 {":authority", "example.com:80"},
                                                 {"content-type", "application/grpc"},
                                                 {"x-forwarded-proto", "http"},
                                                 {"te", "trailers"}};

  Http::TestResponseHeaderMapImpl response_headers{{":status", "200"},
                                                   {"content-type", "application/grpc"}};
  Http::TestResponseTrailerMapImpl response_trailers{{"grpc-status", "0"}, {"grpc-message", ""}};

  absl::optional<Http::Protocol> protocol = Http::Protocol::Http2;
  absl::optional<uint32_t> response_code(200);
  EXPECT_CALL(stream_info, responseCode()).WillRepeatedly(ReturnPointee(&response_code));
  EXPECT_CALL(stream_info, bytesReceived()).WillOnce(Return(10));
  EXPECT_CALL(stream_info, bytesSent()).WillOnce(Return(11));
  EXPECT_CALL(stream_info, protocol()).WillRepeatedly(ReturnPointee(&protocol));
  EXPECT_CALL(stream_info, downstreamDirectRemoteAddress())
      .WillRepeatedly(ReturnPointee(&remote_address));

  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().Component), Eq(Tracing::Tags::get().Proxy)));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().DownstreamCluster), Eq("-")));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().UpstreamCluster), Eq("fake_cluster")));
  EXPECT_CALL(span,
              setTag(Eq(Tracing::Tags::get().HttpUrl), Eq("http://example.com:80/pb.Foo/Bar")));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().UserAgent), Eq("-")));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().RequestSize), Eq("10")));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().ResponseSize), Eq("11")));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().ResponseFlags), Eq("-")));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().HttpMethod), Eq("POST")));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().HttpProtocol), Eq("HTTP/2")));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().HttpStatusCode), Eq("200")));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().GrpcPath), Eq("/pb.Foo/Bar")));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().GrpcAuthority), Eq("example.com:80")));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().GrpcContentType), Eq("application/grpc")));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().GrpcStatusCode), Eq("0")));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().GrpcMessage), Eq("")));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().PeerAddress), Eq(expected_ip)));

  HttpTracerUtility::finalizeDownstreamSpan(span, &request_headers, &response_headers,
                                            &response_trailers, stream_info, config);
}

TEST_F(HttpConnManFinalizerImplTest, GrpcErrorTag) {
  const std::string path_prefix = "http://";
  const std::string expected_ip = "10.0.0.100";
  const auto remote_address =
      Network::Address::InstanceConstSharedPtr{new Network::Address::Ipv4Instance(expected_ip, 0)};

  Http::TestRequestHeaderMapImpl request_headers{{":method", "POST"},
                                                 {":scheme", "http"},
                                                 {":path", "/pb.Foo/Bar"},
                                                 {":authority", "example.com:80"},
                                                 {"content-type", "application/grpc"},
                                                 {"grpc-timeout", "10s"},
                                                 {"x-forwarded-proto", "http"},
                                                 {"te", "trailers"}};

  Http::TestResponseHeaderMapImpl response_headers{{":status", "200"},
                                                   {"content-type", "application/grpc"}};
  Http::TestResponseTrailerMapImpl response_trailers{{"grpc-status", "7"},
                                                     {"grpc-message", "permission denied"}};

  absl::optional<Http::Protocol> protocol = Http::Protocol::Http2;
  absl::optional<uint32_t> response_code(200);
  EXPECT_CALL(stream_info, responseCode()).WillRepeatedly(ReturnPointee(&response_code));
  EXPECT_CALL(stream_info, bytesReceived()).WillOnce(Return(10));
  EXPECT_CALL(stream_info, bytesSent()).WillOnce(Return(11));
  EXPECT_CALL(stream_info, protocol()).WillRepeatedly(ReturnPointee(&protocol));
  EXPECT_CALL(stream_info, downstreamDirectRemoteAddress())
      .WillRepeatedly(ReturnPointee(&remote_address));

  EXPECT_CALL(span, setTag(_, _)).Times(testing::AnyNumber());
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().Error), Eq(Tracing::Tags::get().True)));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().HttpMethod), Eq("POST")));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().HttpProtocol), Eq("HTTP/2")));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().HttpStatusCode), Eq("200")));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().GrpcPath), Eq("/pb.Foo/Bar")));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().GrpcAuthority), Eq("example.com:80")));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().GrpcContentType), Eq("application/grpc")));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().GrpcTimeout), Eq("10s")));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().GrpcStatusCode), Eq("7")));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().GrpcMessage), Eq("permission denied")));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().PeerAddress), Eq(expected_ip)));

  HttpTracerUtility::finalizeDownstreamSpan(span, &request_headers, &response_headers,
                                            &response_trailers, stream_info, config);
}

TEST_F(HttpConnManFinalizerImplTest, GrpcTrailersOnly) {
  const std::string path_prefix = "http://";
  const std::string expected_ip = "10.0.0.100";
  const auto remote_address =
      Network::Address::InstanceConstSharedPtr{new Network::Address::Ipv4Instance(expected_ip, 0)};

  Http::TestRequestHeaderMapImpl request_headers{{":method", "POST"},
                                                 {":scheme", "http"},
                                                 {":path", "/pb.Foo/Bar"},
                                                 {":authority", "example.com:80"},
                                                 {"content-type", "application/grpc"},
                                                 {"x-forwarded-proto", "http"},
                                                 {"te", "trailers"}};

  Http::TestResponseHeaderMapImpl response_headers{{":status", "200"},
                                                   {"content-type", "application/grpc"},
                                                   {"grpc-status", "7"},
                                                   {"grpc-message", "permission denied"}};
  Http::TestResponseTrailerMapImpl response_trailers;

  absl::optional<Http::Protocol> protocol = Http::Protocol::Http2;
  absl::optional<uint32_t> response_code(200);
  EXPECT_CALL(stream_info, responseCode()).WillRepeatedly(ReturnPointee(&response_code));
  EXPECT_CALL(stream_info, bytesReceived()).WillOnce(Return(10));
  EXPECT_CALL(stream_info, bytesSent()).WillOnce(Return(11));
  EXPECT_CALL(stream_info, protocol()).WillRepeatedly(ReturnPointee(&protocol));
  EXPECT_CALL(stream_info, downstreamDirectRemoteAddress())
      .WillRepeatedly(ReturnPointee(&remote_address));

  EXPECT_CALL(span, setTag(_, _)).Times(testing::AnyNumber());
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().Error), Eq(Tracing::Tags::get().True)));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().HttpMethod), Eq("POST")));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().HttpProtocol), Eq("HTTP/2")));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().HttpStatusCode), Eq("200")));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().GrpcPath), Eq("/pb.Foo/Bar")));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().GrpcAuthority), Eq("example.com:80")));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().GrpcContentType), Eq("application/grpc")));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().GrpcStatusCode), Eq("7")));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().GrpcMessage), Eq("permission denied")));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().PeerAddress), Eq(expected_ip)));

  HttpTracerUtility::finalizeDownstreamSpan(span, &request_headers, &response_headers,
                                            &response_trailers, stream_info, config);
}

TEST(HttpTracerUtilityTest, operationTypeToString) {
  EXPECT_EQ("ingress", HttpTracerUtility::toString(OperationName::Ingress));
  EXPECT_EQ("egress", HttpTracerUtility::toString(OperationName::Egress));
}

TEST(HttpNullTracerTest, BasicFunctionality) {
  HttpNullTracer null_tracer;
  MockConfig config;
  StreamInfo::MockStreamInfo stream_info;
  Http::TestRequestHeaderMapImpl request_headers;
  Http::TestResponseHeaderMapImpl response_headers;
  Http::TestResponseTrailerMapImpl response_trailers;

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

  Http::TestRequestHeaderMapImpl request_headers_{
      {":path", "/"}, {":method", "GET"}, {"x-request-id", "foo"}, {":authority", "test"}};
  Http::TestResponseHeaderMapImpl response_headers;
  Http::TestResponseTrailerMapImpl response_trailers;
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
