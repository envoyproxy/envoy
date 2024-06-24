#include <cstdint>
#include <memory>
#include <string>

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/type/tracing/v3/custom_tag.pb.h"

#include "source/common/common/base64.h"
#include "source/common/http/header_map_impl.h"
#include "source/common/http/headers.h"
#include "source/common/http/message_impl.h"
#include "source/common/network/address_impl.h"
#include "source/common/network/utility.h"
#include "source/common/tracing/custom_tag_impl.h"
#include "source/common/tracing/http_tracer_impl.h"

#include "test/mocks/http/mocks.h"
#include "test/mocks/local_info/mocks.h"
#include "test/mocks/router/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/stats/mocks.h"
#include "test/mocks/thread_local/mocks.h"
#include "test/mocks/tracing/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::AnyNumber;
using testing::Eq;
using testing::InSequence;
using testing::NiceMock;
using testing::Return;
using testing::ReturnPointee;

namespace Envoy {
namespace Tracing {
namespace {

class HttpConnManFinalizerImplTest : public testing::Test {
protected:
  HttpConnManFinalizerImplTest() {
    Upstream::HostDescriptionConstSharedPtr shared_host(host_);
    stream_info.upstreamInfo()->setUpstreamHost(shared_host);
    ON_CALL(stream_info, upstreamClusterInfo())
        .WillByDefault(
            Return(absl::make_optional<Upstream::ClusterInfoConstSharedPtr>(cluster_info_)));
  }
  struct CustomTagCase {
    std::string custom_tag;
    bool set;
    std::string value;
  };

  void expectSetCustomTags(const std::vector<CustomTagCase>& cases) {
    for (const CustomTagCase& cas : cases) {
      envoy::type::tracing::v3::CustomTag custom_tag;
      TestUtility::loadFromYaml(cas.custom_tag, custom_tag);
      config.custom_tags_.emplace(custom_tag.tag(), CustomTagUtility::createCustomTag(custom_tag));
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
  std::shared_ptr<NiceMock<Upstream::MockClusterInfo>> cluster_info_{
      std::make_shared<NiceMock<Upstream::MockClusterInfo>>()};
  Upstream::MockHostDescription* host_{new NiceMock<Upstream::MockHostDescription>()};
};

TEST_F(HttpConnManFinalizerImplTest, OriginalAndLongPath) {
  const std::string path(300, 'a');
  const std::string path_prefix = "http://";
  const std::string expected_path(256, 'a');
  const std::string expected_ip = "10.0.0.100";
  const auto remote_address = Network::Address::InstanceConstSharedPtr{
      new Network::Address::Ipv4Instance(expected_ip, 0, nullptr)};

  Http::TestRequestHeaderMapImpl request_headers{{"x-request-id", "id"},
                                                 {"x-envoy-original-path", path},
                                                 {":method", "GET"},
                                                 {":path", ""},
                                                 {":scheme", "http"}};
  Http::TestResponseHeaderMapImpl response_headers;
  Http::TestResponseTrailerMapImpl response_trailers;

  absl::optional<Http::Protocol> protocol = Http::Protocol::Http2;
  EXPECT_CALL(stream_info, bytesReceived()).WillOnce(Return(10));
  EXPECT_CALL(stream_info, bytesSent()).WillOnce(Return(11));
  EXPECT_CALL(stream_info, protocol()).WillRepeatedly(ReturnPointee(&protocol));
  absl::optional<uint32_t> response_code;
  EXPECT_CALL(stream_info, responseCode()).WillRepeatedly(ReturnPointee(&response_code));
  stream_info.downstream_connection_info_provider_->setDirectRemoteAddressForTest(remote_address);

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
  const auto remote_address = Network::Address::InstanceConstSharedPtr{
      new Network::Address::Ipv4Instance(expected_ip, 0, nullptr)};

  Http::TestRequestHeaderMapImpl request_headers{
      {":path", ""}, {"x-envoy-original-path", path}, {":method", "GET"}, {":scheme", "http"}};
  Http::TestResponseHeaderMapImpl response_headers;
  Http::TestResponseTrailerMapImpl response_trailers;

  absl::optional<Http::Protocol> protocol = Http::Protocol::Http2;
  EXPECT_CALL(stream_info, bytesReceived()).WillOnce(Return(10));
  EXPECT_CALL(stream_info, bytesSent()).WillOnce(Return(11));
  EXPECT_CALL(stream_info, protocol()).WillRepeatedly(ReturnPointee(&protocol));
  absl::optional<uint32_t> response_code;
  EXPECT_CALL(stream_info, responseCode()).WillRepeatedly(ReturnPointee(&response_code));
  stream_info.downstream_connection_info_provider_->setDirectRemoteAddressForTest(remote_address);

  EXPECT_CALL(span, setTag(_, _)).Times(testing::AnyNumber());
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().HttpUrl), Eq(path_prefix + expected_path)));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().HttpMethod), Eq("GET")));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().HttpProtocol), Eq("HTTP/2")));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().PeerAddress), Eq(expected_ip)));

  HttpTracerUtility::finalizeDownstreamSpan(span, &request_headers, &response_headers,
                                            &response_trailers, stream_info, config);
}

TEST_F(HttpConnManFinalizerImplTest, Connect) {
  const std::string path(300, 'a');
  const std::string path_prefix = "http://";
  const std::string expected_path(256, 'a');
  const std::string expected_ip = "10.0.0.100";
  const auto remote_address = Network::Address::InstanceConstSharedPtr{
      new Network::Address::Ipv4Instance(expected_ip, 0, nullptr)};

  Http::TestRequestHeaderMapImpl request_headers{{":method", "CONNECT"}, {":scheme", "http"}};
  Http::TestResponseHeaderMapImpl response_headers;
  Http::TestResponseTrailerMapImpl response_trailers;

  absl::optional<Http::Protocol> protocol = Http::Protocol::Http2;
  EXPECT_CALL(stream_info, bytesReceived()).WillOnce(Return(10));
  EXPECT_CALL(stream_info, bytesSent()).WillOnce(Return(11));
  EXPECT_CALL(stream_info, protocol()).WillRepeatedly(ReturnPointee(&protocol));
  absl::optional<uint32_t> response_code;
  EXPECT_CALL(stream_info, responseCode()).WillRepeatedly(ReturnPointee(&response_code));
  stream_info.downstream_connection_info_provider_->setDirectRemoteAddressForTest(remote_address);

  EXPECT_CALL(span, setTag(_, _)).Times(testing::AnyNumber());
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().HttpUrl), Eq("")));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().HttpMethod), Eq("CONNECT")));
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
  // No upstream info.
  stream_info.upstreamInfo()->setUpstreamHost(nullptr);
  EXPECT_CALL(stream_info, route()).WillRepeatedly(Return(nullptr));
  // No cluster info.
  EXPECT_CALL(stream_info, upstreamClusterInfo()).WillOnce(Return(absl::nullopt));

  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().HttpStatusCode), Eq("0")));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().Error), Eq(Tracing::Tags::get().True)));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().ResponseSize), Eq("11")));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().ResponseFlags), Eq("-")));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().RequestSize), Eq("10")));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().Component), Eq(Tracing::Tags::get().Proxy)));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().UpstreamAddress), _)).Times(0);
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().UpstreamCluster), _)).Times(0);
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().UpstreamClusterName), _)).Times(0);

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
  host_->hostname_ = "my_upstream_cluster";

  EXPECT_CALL(stream_info, bytesReceived()).WillOnce(Return(10));
  EXPECT_CALL(stream_info, bytesSent()).WillOnce(Return(11));
  absl::optional<uint32_t> response_code;
  EXPECT_CALL(stream_info, responseCode()).WillRepeatedly(ReturnPointee(&response_code));
  const auto start_timestamp =
      SystemTime{std::chrono::duration_cast<SystemTime::duration>(std::chrono::hours{123})};
  EXPECT_CALL(stream_info, startTime()).WillRepeatedly(Return(start_timestamp));

  const absl::optional<std::chrono::nanoseconds> nanoseconds = std::chrono::nanoseconds{10};
  const MonotonicTime time = MonotonicTime(nanoseconds.value());
  MockTimeSystem time_system;
  EXPECT_CALL(time_system, monotonicTime)
      .Times(AnyNumber())
      .WillRepeatedly(Return(MonotonicTime(std::chrono::nanoseconds(10))));
  auto& timing = stream_info.upstream_info_->upstreamTiming();
  timing.first_upstream_tx_byte_sent_ = time;
  timing.last_upstream_tx_byte_sent_ = time;
  timing.first_upstream_rx_byte_received_ = time;
  timing.last_upstream_rx_byte_received_ = time;
  stream_info.downstream_timing_.onFirstDownstreamTxByteSent(time_system);
  stream_info.downstream_timing_.onLastDownstreamTxByteSent(time_system);
  stream_info.downstream_timing_.onLastDownstreamRxByteReceived(time_system);

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

TEST_F(HttpConnManFinalizerImplTest, UpstreamClusterTagSetAlthoughNoUpstreamInfo) {
  // No upstream info.
  stream_info.upstreamInfo()->setUpstreamHost(nullptr);

  cluster_info_->name_ = "my_upstream_cluster";
  cluster_info_->observability_name_ = "my_upstream_cluster_observable";

  EXPECT_CALL(stream_info, bytesReceived()).WillOnce(Return(10));
  EXPECT_CALL(stream_info, bytesSent()).WillOnce(Return(11));
  absl::optional<uint32_t> response_code;
  EXPECT_CALL(stream_info, responseCode()).WillRepeatedly(ReturnPointee(&response_code));

  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().Component), Eq(Tracing::Tags::get().Proxy)));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().UpstreamCluster), Eq("my_upstream_cluster")));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().UpstreamClusterName),
                           Eq("my_upstream_cluster_observable")));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().HttpStatusCode), Eq("0")));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().Error), Eq(Tracing::Tags::get().True)));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().ResponseSize), Eq("11")));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().ResponseFlags), Eq("-")));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().RequestSize), Eq("10")));

  HttpTracerUtility::finalizeDownstreamSpan(span, nullptr, nullptr, nullptr, stream_info, config);
}

TEST_F(HttpConnManFinalizerImplTest, NoUpstreamClusterTagSetWhenNoClusterInfo) {
  // No cluster info.
  EXPECT_CALL(stream_info, upstreamClusterInfo()).WillOnce(Return(absl::nullopt));

  EXPECT_CALL(stream_info, bytesReceived()).WillOnce(Return(10));
  EXPECT_CALL(stream_info, bytesSent()).WillOnce(Return(11));
  absl::optional<uint32_t> response_code;
  EXPECT_CALL(stream_info, responseCode()).WillRepeatedly(ReturnPointee(&response_code));

  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().Component), Eq(Tracing::Tags::get().Proxy)));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().UpstreamCluster), _)).Times(0);
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().UpstreamClusterName), _)).Times(0);
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().HttpStatusCode), Eq("0")));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().Error), Eq(Tracing::Tags::get().True)));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().ResponseSize), Eq("11")));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().ResponseFlags), Eq("-")));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().RequestSize), Eq("10")));

  HttpTracerUtility::finalizeDownstreamSpan(span, nullptr, nullptr, nullptr, stream_info, config);
}

TEST_F(HttpConnManFinalizerImplTest, SpanOptionalHeaders) {
  Http::TestRequestHeaderMapImpl request_headers{
      {"x-request-id", "id"}, {":path", "/test"}, {":method", "GET"}, {":scheme", "https"}};
  Http::TestResponseHeaderMapImpl response_headers;
  Http::TestResponseTrailerMapImpl response_trailers;
  const std::string expected_ip = "10.0.0.100";
  const auto remote_address = Network::Address::InstanceConstSharedPtr{
      new Network::Address::Ipv4Instance(expected_ip, 0, nullptr)};

  absl::optional<Http::Protocol> protocol = Http::Protocol::Http10;
  EXPECT_CALL(stream_info, bytesReceived()).WillOnce(Return(10));
  EXPECT_CALL(stream_info, protocol()).WillRepeatedly(ReturnPointee(&protocol));
  stream_info.downstream_connection_info_provider_->setDirectRemoteAddressForTest(remote_address);

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
  stream_info.upstreamInfo()->setUpstreamHost(nullptr);

  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().HttpStatusCode), Eq("0")));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().Error), Eq(Tracing::Tags::get().True)));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().ResponseSize), Eq("100")));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().ResponseFlags), Eq("-")));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().Component), Eq(Tracing::Tags::get().Proxy)));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().UpstreamAddress), _)).Times(0);
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().UpstreamCluster), "fake_cluster"));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().UpstreamClusterName), "observability_name"));

  HttpTracerUtility::finalizeDownstreamSpan(span, &request_headers, &response_headers,
                                            &response_trailers, stream_info, config);
}

TEST_F(HttpConnManFinalizerImplTest, UnixDomainSocketPeerAddressTag) {
  Http::TestRequestHeaderMapImpl request_headers{
      {"x-request-id", "id"}, {":path", "/test"}, {":method", "GET"}, {":scheme", "https"}};
  Http::TestResponseHeaderMapImpl response_headers;
  Http::TestResponseTrailerMapImpl response_trailers;
  const std::string path_{TestEnvironment::unixDomainSocketPath("foo")};
  const auto remote_address = *Network::Utility::resolveUrl("unix://" + path_);

  stream_info.downstream_connection_info_provider_->setDirectRemoteAddressForTest(remote_address);

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
                                                 {":scheme", "https"},
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
  std::shared_ptr<Router::MockRoute> route{new NiceMock<Router::MockRoute>()};
  EXPECT_CALL(stream_info, route()).WillRepeatedly(Return(route));
  (*route->metadata_.mutable_filter_metadata())["m.rot"].MergeFrom(fake_struct);
  std::shared_ptr<envoy::config::core::v3::Metadata> host_metadata =
      std::make_shared<envoy::config::core::v3::Metadata>();
  (*host_metadata->mutable_filter_metadata())["m.host"].MergeFrom(fake_struct);
  (*host_->cluster_.metadata_.mutable_filter_metadata())["m.cluster"].MergeFrom(fake_struct);

  absl::optional<Http::Protocol> protocol = Http::Protocol::Http10;
  EXPECT_CALL(stream_info, bytesReceived()).WillOnce(Return(10));
  EXPECT_CALL(stream_info, protocol()).WillRepeatedly(ReturnPointee(&protocol));
  absl::optional<uint32_t> response_code;
  EXPECT_CALL(stream_info, responseCode()).WillRepeatedly(ReturnPointee(&response_code));
  EXPECT_CALL(stream_info, bytesSent()).WillOnce(Return(100));
  EXPECT_CALL(*host_, metadata()).WillRepeatedly(Return(host_metadata));

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

  ON_CALL(stream_info, getRequestHeaders()).WillByDefault(Return(&request_headers));

  HttpTracerUtility::finalizeDownstreamSpan(span, &request_headers, nullptr, nullptr, stream_info,
                                            config);
}

TEST_F(HttpConnManFinalizerImplTest, SpanPopulatedFailureResponse) {
  Http::TestRequestHeaderMapImpl request_headers{
      {"x-request-id", "id"}, {":path", "/test"}, {":method", "GET"}, {":scheme", "http"}};
  Http::TestResponseHeaderMapImpl response_headers;
  Http::TestResponseTrailerMapImpl response_trailers;
  const std::string expected_ip = "10.0.0.100";
  const auto remote_address = Network::Address::InstanceConstSharedPtr{
      new Network::Address::Ipv4Instance(expected_ip, 0, nullptr)};

  request_headers.setHost("api");
  request_headers.setUserAgent("agent");
  request_headers.setEnvoyDownstreamServiceCluster("downstream_cluster");
  request_headers.setClientTraceId("client_trace_id");

  absl::optional<Http::Protocol> protocol = Http::Protocol::Http10;
  EXPECT_CALL(stream_info, protocol()).WillRepeatedly(ReturnPointee(&protocol));
  EXPECT_CALL(stream_info, bytesReceived()).WillOnce(Return(10));
  stream_info.downstream_connection_info_provider_->setDirectRemoteAddressForTest(remote_address);

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
  stream_info.setResponseFlag(StreamInfo::CoreResponseFlag::UpstreamRequestTimeout);

  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().Error), Eq(Tracing::Tags::get().True)));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().HttpStatusCode), Eq("503")));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().ResponseSize), Eq("100")));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().ResponseFlags), Eq("UT")));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().Component), Eq(Tracing::Tags::get().Proxy)));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().UpstreamAddress), _)).Times(0);
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().UpstreamCluster), "fake_cluster"));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().UpstreamClusterName), "observability_name"));

  HttpTracerUtility::finalizeDownstreamSpan(span, &request_headers, &response_headers,
                                            &response_trailers, stream_info, config);
}

TEST_F(HttpConnManFinalizerImplTest, GrpcOkStatus) {
  const std::string path_prefix = "http://";
  const std::string expected_ip = "10.0.0.100";
  const auto remote_address = Network::Address::InstanceConstSharedPtr{
      new Network::Address::Ipv4Instance(expected_ip, 0, nullptr)};

  Http::TestRequestHeaderMapImpl request_headers{{":method", "POST"},
                                                 {":scheme", "http"},
                                                 {":path", "/pb.Foo/Bar"},
                                                 {":authority", "example.com:80"},
                                                 {"content-type", "application/grpc"},
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
  stream_info.downstream_connection_info_provider_->setDirectRemoteAddressForTest(remote_address);

  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().Component), Eq(Tracing::Tags::get().Proxy)));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().DownstreamCluster), Eq("-")));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().UpstreamCluster), Eq("fake_cluster")));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().UpstreamClusterName), Eq("observability_name")));
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
  const auto remote_address = Network::Address::InstanceConstSharedPtr{
      new Network::Address::Ipv4Instance(expected_ip, 0, nullptr)};

  Http::TestRequestHeaderMapImpl request_headers{{":method", "POST"},
                                                 {":scheme", "http"},
                                                 {":path", "/pb.Foo/Bar"},
                                                 {":authority", "example.com:80"},
                                                 {"content-type", "application/grpc"},
                                                 {"grpc-timeout", "10s"},
                                                 {":scheme", "http"},
                                                 {"te", "trailers"}};

  Http::TestResponseHeaderMapImpl response_headers{{":status", "200"},
                                                   {"content-type", "application/grpc"}};

  Http::TestResponseTrailerMapImpl response_trailers;
  response_trailers.setGrpcStatus("14");
  response_trailers.setGrpcMessage("unavailable");

  absl::optional<Http::Protocol> protocol = Http::Protocol::Http2;
  absl::optional<uint32_t> response_code(200);
  EXPECT_CALL(stream_info, responseCode()).WillRepeatedly(ReturnPointee(&response_code));
  EXPECT_CALL(stream_info, bytesReceived()).WillOnce(Return(10));
  EXPECT_CALL(stream_info, bytesSent()).WillOnce(Return(11));
  EXPECT_CALL(stream_info, protocol()).WillRepeatedly(ReturnPointee(&protocol));
  stream_info.downstream_connection_info_provider_->setDirectRemoteAddressForTest(remote_address);

  EXPECT_CALL(span, setTag(_, _)).Times(testing::AnyNumber());
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().HttpMethod), Eq("POST")));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().HttpProtocol), Eq("HTTP/2")));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().HttpStatusCode), Eq("200")));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().GrpcPath), Eq("/pb.Foo/Bar")));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().GrpcAuthority), Eq("example.com:80")));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().GrpcContentType), Eq("application/grpc")));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().GrpcTimeout), Eq("10s")));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().GrpcStatusCode), Eq("14")));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().GrpcMessage), Eq("unavailable")));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().Error), Eq(Tracing::Tags::get().True)));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().PeerAddress), Eq(expected_ip)));

  HttpTracerUtility::finalizeDownstreamSpan(span, &request_headers, &response_headers,
                                            &response_trailers, stream_info, config);
}

TEST_F(HttpConnManFinalizerImplTest, GrpcTrailersOnly) {
  const std::string path_prefix = "http://";
  const std::string expected_ip = "10.0.0.100";
  const auto remote_address = Network::Address::InstanceConstSharedPtr{
      new Network::Address::Ipv4Instance(expected_ip, 0, nullptr)};

  Http::TestRequestHeaderMapImpl request_headers{{":method", "POST"},
                                                 {":scheme", "http"},
                                                 {":path", "/pb.Foo/Bar"},
                                                 {":authority", "example.com:80"},
                                                 {"content-type", "application/grpc"},
                                                 {":scheme", "http"},
                                                 {"te", "trailers"}};

  Http::TestResponseHeaderMapImpl response_headers{{":status", "200"},
                                                   {"content-type", "application/grpc"}};

  response_headers.setGrpcStatus("14");
  response_headers.setGrpcMessage("unavailable");

  Http::TestResponseTrailerMapImpl response_trailers;

  absl::optional<Http::Protocol> protocol = Http::Protocol::Http2;
  absl::optional<uint32_t> response_code(200);
  EXPECT_CALL(stream_info, responseCode()).WillRepeatedly(ReturnPointee(&response_code));
  EXPECT_CALL(stream_info, bytesReceived()).WillOnce(Return(10));
  EXPECT_CALL(stream_info, bytesSent()).WillOnce(Return(11));
  EXPECT_CALL(stream_info, protocol()).WillRepeatedly(ReturnPointee(&protocol));
  stream_info.downstream_connection_info_provider_->setDirectRemoteAddressForTest(remote_address);

  EXPECT_CALL(span, setTag(_, _)).Times(testing::AnyNumber());
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().HttpMethod), Eq("POST")));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().HttpProtocol), Eq("HTTP/2")));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().HttpStatusCode), Eq("200")));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().GrpcPath), Eq("/pb.Foo/Bar")));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().GrpcAuthority), Eq("example.com:80")));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().GrpcContentType), Eq("application/grpc")));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().GrpcStatusCode), Eq("14")));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().GrpcMessage), Eq("unavailable")));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().Error), Eq(Tracing::Tags::get().True)));
  EXPECT_CALL(span, setTag(Eq(Tracing::Tags::get().PeerAddress), Eq(expected_ip)));

  HttpTracerUtility::finalizeDownstreamSpan(span, &request_headers, &response_headers,
                                            &response_trailers, stream_info, config);
}

TEST_F(HttpConnManFinalizerImplTest, CustomTagOverwritesCommonTag) {

  Http::TestRequestHeaderMapImpl request_headers{
      {":path", "/test"}, {":method", "GET"}, {":scheme", "https"}};

  absl::optional<Http::Protocol> protocol = Http::Protocol::Http10;
  EXPECT_CALL(stream_info, bytesReceived()).WillOnce(Return(10));
  EXPECT_CALL(stream_info, protocol()).WillRepeatedly(ReturnPointee(&protocol));
  absl::optional<uint32_t> response_code;
  EXPECT_CALL(stream_info, responseCode()).WillRepeatedly(ReturnPointee(&response_code));
  EXPECT_CALL(stream_info, bytesSent()).WillOnce(Return(100));

  EXPECT_CALL(config, customTags());

  std::string custom_tag_str = "{ tag: component, literal: { value: override_component } }";
  envoy::type::tracing::v3::CustomTag custom_tag;
  TestUtility::loadFromYaml(custom_tag_str, custom_tag);
  config.custom_tags_.emplace(custom_tag.tag(), CustomTagUtility::createCustomTag(custom_tag));
  EXPECT_CALL(span, setTag(_, _)).Times(testing::AnyNumber());

  {
    // ensure our setTag(..., "override_component") happens later, taking precedence
    InSequence s;
    EXPECT_CALL(span, setTag(Eq(custom_tag.tag()), Eq(Tracing::Tags::get().Proxy)));
    EXPECT_CALL(span, setTag(Eq(custom_tag.tag()), "override_component"));
  }

  HttpTracerUtility::finalizeDownstreamSpan(span, &request_headers, nullptr, nullptr, stream_info,
                                            config);
}

TEST(HttpTraceContextTest, HttpTraceContextTest) {
  {
    Http::TestRequestHeaderMapImpl request_headers;
    HttpTraceContext trace_context(request_headers);

    // Protocol.
    EXPECT_EQ(trace_context.protocol(), "");
    request_headers.addCopy(Http::Headers::get().Protocol, "HTTP/x");
    EXPECT_EQ(trace_context.protocol(), "HTTP/x");

    // Host.
    EXPECT_EQ(trace_context.host(), "");
    request_headers.addCopy(Http::Headers::get().Host, "test.com:233");
    EXPECT_EQ(trace_context.host(), "test.com:233");

    // Path.
    EXPECT_EQ(trace_context.path(), "");
    request_headers.addCopy(Http::Headers::get().Path, "/anything");
    EXPECT_EQ(trace_context.path(), "/anything");

    // Method.
    EXPECT_EQ(trace_context.method(), "");
    request_headers.addCopy(Http::Headers::get().Method, Http::Headers::get().MethodValues.Options);
    EXPECT_EQ(trace_context.method(), Http::Headers::get().MethodValues.Options);

    // Set.
    trace_context.set("foo", "bar");
    EXPECT_EQ(request_headers.get_("foo"), "bar");

    // Get.
    EXPECT_EQ(trace_context.get("foo").value(), "bar");

    // Remove.
    trace_context.remove("foo");
    EXPECT_EQ(request_headers.get_("foo"), "");
    EXPECT_EQ(trace_context.get("foo"), absl::nullopt);
  }

  {
    size_t size = 0;
    Http::TestRequestHeaderMapImpl request_headers{{"host", "foo"}, {"bar", "var"}, {"ok", "no"}};
    HttpTraceContext trace_context(request_headers);
    trace_context.forEach([&size](absl::string_view key, absl::string_view val) {
      size += key.size();
      size += val.size();
      return true;
    });
    // 'host' will be converted to ':authority'.
    EXPECT_EQ(23, size);
  }
}

} // namespace
} // namespace Tracing
} // namespace Envoy
