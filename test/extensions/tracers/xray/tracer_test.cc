#include <string>
#include <vector>

#include "envoy/common/time.h"

#include "common/protobuf/utility.h"

#include "source/extensions/tracers/xray/daemon.pb.h"

#include "extensions/tracers/xray/tracer.h"
#include "extensions/tracers/xray/xray_configuration.h"

#include "test/mocks/server/instance.h"
#include "test/mocks/tracing/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/network_utility.h"

#include "absl/strings/str_format.h"
#include "absl/strings/str_split.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace XRay {

namespace {
using ::testing::_;
using ::testing::Invoke;
using ::testing::Return;
using namespace source::extensions::tracers::xray;

struct MockDaemonBroker : DaemonBroker {
  MockDaemonBroker(const std::string& endpoint) { UNREFERENCED_PARAMETER(endpoint); }
  MOCK_METHOD(void, send, (std::string const&), (const, override));
};

class XRayTracerTest : public ::testing::Test {
public:
  XRayTracerTest() : broker_(std::make_unique<MockDaemonBroker>("127.0.0.1:2000")) {}

  absl::flat_hash_map<std::string, ProtobufWkt::Value> aws_metadata_;
  NiceMock<Server::MockInstance> server_;
  std::unique_ptr<MockDaemonBroker> broker_;
};

TEST_F(XRayTracerTest, SerializeSpanTest) {
  constexpr auto expected_span_name = "Service 1";
  constexpr auto expected_origin_name = "AWS::Service::Proxy";
  constexpr auto expected_aws_key_value = "test_value";
  constexpr auto expected_operation_name = "Create";
  constexpr auto expected_http_method = "POST";
  constexpr auto expected_http_url = "/first/second";
  constexpr auto expected_user_agent = "Mozilla/5.0 (Macintosh; Intel Mac OS X)";
  constexpr uint32_t expected_status_code = 202;
  constexpr uint32_t expected_content_length = 1337;
  constexpr auto expected_client_ip = "10.0.0.100";
  constexpr auto expected_x_forwarded_for = false;
  constexpr auto expected_upstream_address = "10.0.0.200";

  auto on_send = [&](const std::string& json) {
    ASSERT_FALSE(json.empty());
    daemon::Segment s;
    MessageUtil::loadFromJson(json, s, ProtobufMessage::getNullValidationVisitor());
    ASSERT_FALSE(s.trace_id().empty());
    ASSERT_FALSE(s.id().empty());
    ASSERT_EQ(1, s.annotations().size());
    ASSERT_TRUE(s.parent_id().empty());
    ASSERT_STREQ(expected_span_name, s.name().c_str());
    ASSERT_STREQ(expected_origin_name, s.origin().c_str());
    ASSERT_STREQ(expected_aws_key_value, s.aws().fields().at("key").string_value().c_str());
    ASSERT_STREQ(expected_http_method,
                 s.http().request().fields().at("method").string_value().c_str());
    ASSERT_STREQ(expected_http_url, s.http().request().fields().at("url").string_value().c_str());
    ASSERT_STREQ(expected_user_agent,
                 s.http().request().fields().at("user_agent").string_value().c_str());
    ASSERT_DOUBLE_EQ(expected_status_code,
                     s.http().response().fields().at("status").number_value());
    ASSERT_DOUBLE_EQ(expected_content_length,
                     s.http().response().fields().at("content_length").number_value());
    ASSERT_STREQ(expected_client_ip,
                 s.http().request().fields().at("client_ip").string_value().c_str());
    ASSERT_EQ(expected_x_forwarded_for,
              s.http().request().fields().at("x_forwarded_for").bool_value());
    ASSERT_STREQ(expected_upstream_address, s.annotations().at("upstream_address").c_str());
  };

  EXPECT_CALL(*broker_, send(_)).WillOnce(Invoke(on_send));
  aws_metadata_.insert({"key", ValueUtil::stringValue(expected_aws_key_value)});
  Tracer tracer{expected_span_name, expected_origin_name, aws_metadata_,
                std::move(broker_), server_.timeSource(), server_.api().randomGenerator()};
  auto span = tracer.startSpan(expected_operation_name, server_.timeSource().systemTime(),
                               absl::nullopt /*headers*/);
  span->setTag("http.method", expected_http_method);
  span->setTag("http.url", expected_http_url);
  span->setTag("user_agent", expected_user_agent);
  span->setTag("http.status_code", absl::StrFormat("%d", expected_status_code));
  span->setTag("response_size", absl::StrFormat("%d", expected_content_length));
  span->setTag("peer.address", expected_client_ip);
  span->setTag("upstream_address", expected_upstream_address);
  span->finishSpan();
}

TEST_F(XRayTracerTest, NonSampledSpansNotSerialized) {
  Tracer tracer{"" /*span name*/,   "" /*origin*/,        aws_metadata_,
                std::move(broker_), server_.timeSource(), server_.api().randomGenerator()};
  auto span = tracer.createNonSampledSpan();
  span->finishSpan();
}

TEST_F(XRayTracerTest, BaggageNotImplemented) {
  Tracer tracer{"" /*span name*/,   "" /*origin*/,        aws_metadata_,
                std::move(broker_), server_.timeSource(), server_.api().randomGenerator()};
  auto span = tracer.createNonSampledSpan();
  span->setBaggage("baggage_key", "baggage_value");
  span->finishSpan();

  // Baggage isn't supported so getBaggage should always return empty
  ASSERT_EQ("", span->getBaggage("baggage_key"));
}

TEST_F(XRayTracerTest, ChildSpanHasParentInfo) {
  NiceMock<Tracing::MockConfig> config;
  constexpr auto expected_span_name = "Service 1";
  constexpr auto expected_operation_name = "Create";
  const auto& broker = *broker_;
  Tracer tracer{expected_span_name,   "",
                aws_metadata_,        std::move(broker_),
                server_.timeSource(), server_.api().randomGenerator()};
  // Span id taken from random generator
  EXPECT_CALL(server_.api_.random_, random()).WillOnce(Return(999));
  auto parent_span = tracer.startSpan(expected_operation_name, server_.timeSource().systemTime(),
                                      absl::nullopt /*headers*/);

  const XRay::Span* xray_parent_span = static_cast<XRay::Span*>(parent_span.get());
  auto on_send = [&](const std::string& json) {
    ASSERT_FALSE(json.empty());
    daemon::Segment s;
    MessageUtil::loadFromJson(json, s, ProtobufMessage::getNullValidationVisitor());
    // Hex encoded 64 bit identifier
    ASSERT_STREQ("00000000000003e7", s.parent_id().c_str());
    ASSERT_STREQ(expected_span_name, s.name().c_str());
    ASSERT_STREQ(xray_parent_span->traceId().c_str(), s.trace_id().c_str());
    ASSERT_STREQ("0000003d25bebe62", s.id().c_str());
  };

  EXPECT_CALL(broker, send(_)).WillOnce(Invoke(on_send));

  // Span id taken from random generator
  EXPECT_CALL(server_.api_.random_, random()).WillOnce(Return(262626262626));
  auto child =
      parent_span->spawnChild(config, expected_operation_name, server_.timeSource().systemTime());
  child->finishSpan();
}

TEST_F(XRayTracerTest, UseExistingHeaderInformation) {
  XRayHeader xray_header;
  xray_header.trace_id_ = "a";
  xray_header.parent_id_ = "b";
  constexpr auto span_name = "my span";
  constexpr auto operation_name = "my operation";

  Tracer tracer{span_name,
                "",
                aws_metadata_,
                std::move(broker_),
                server_.timeSource(),
                server_.api().randomGenerator()};
  auto span = tracer.startSpan(operation_name, server_.timeSource().systemTime(), xray_header);

  const XRay::Span* xray_span = static_cast<XRay::Span*>(span.get());
  ASSERT_STREQ(xray_header.trace_id_.c_str(), xray_span->traceId().c_str());
  ASSERT_STREQ(xray_header.parent_id_.c_str(), xray_span->parentId().c_str());
}

TEST_F(XRayTracerTest, SpanInjectContextHasXRayHeader) {
  constexpr auto span_name = "my span";
  constexpr auto operation_name = "my operation";

  Tracer tracer{span_name,
                "",
                aws_metadata_,
                std::move(broker_),
                server_.timeSource(),
                server_.api().randomGenerator()};
  auto span = tracer.startSpan(operation_name, server_.timeSource().systemTime(),
                               absl::nullopt /*headers*/);
  Http::TestRequestHeaderMapImpl request_headers;
  span->injectContext(request_headers);
  auto header = request_headers.get(Http::LowerCaseString{XRayTraceHeader});
  ASSERT_FALSE(header.empty());
  ASSERT_NE(header[0]->value().getStringView().find("Root="), absl::string_view::npos);
  ASSERT_NE(header[0]->value().getStringView().find("Parent="), absl::string_view::npos);
  ASSERT_NE(header[0]->value().getStringView().find("Sampled=1"), absl::string_view::npos);
}

TEST_F(XRayTracerTest, SpanInjectContextHasXRayHeaderNonSampled) {
  constexpr auto span_name = "my span";
  Tracer tracer{span_name,
                "",
                aws_metadata_,
                std::move(broker_),
                server_.timeSource(),
                server_.api().randomGenerator()};
  auto span = tracer.createNonSampledSpan();
  Http::TestRequestHeaderMapImpl request_headers;
  span->injectContext(request_headers);
  auto header = request_headers.get(Http::LowerCaseString{XRayTraceHeader});
  ASSERT_FALSE(header.empty());
  ASSERT_NE(header[0]->value().getStringView().find("Root="), absl::string_view::npos);
  ASSERT_NE(header[0]->value().getStringView().find("Parent="), absl::string_view::npos);
  ASSERT_NE(header[0]->value().getStringView().find("Sampled=0"), absl::string_view::npos);
}

TEST_F(XRayTracerTest, TraceIDFormatTest) {
  constexpr auto span_name = "my span";
  Tracer tracer{span_name,
                "",
                aws_metadata_,
                std::move(broker_),
                server_.timeSource(),
                server_.api().randomGenerator()};
  auto span = tracer.createNonSampledSpan(); // startSpan and createNonSampledSpan use the same
                                             // logic to create a trace ID
  XRay::Span* xray_span = span.get();
  std::vector<std::string> parts = absl::StrSplit(xray_span->traceId(), absl::ByChar('-'));
  ASSERT_EQ(3, parts.size());
  ASSERT_EQ(1, parts[0].length());
  ASSERT_EQ(8, parts[1].length());
  ASSERT_EQ(24, parts[2].length());
}

class XRayDaemonTest : public testing::TestWithParam<Network::Address::IpVersion> {};

INSTANTIATE_TEST_SUITE_P(IpVersions, XRayDaemonTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(XRayDaemonTest, VerifyUdpPacketContents) {
  absl::flat_hash_map<std::string, ProtobufWkt::Value> aws_metadata;
  NiceMock<Server::MockInstance> server;
  Network::Test::UdpSyncPeer xray_fake_daemon(GetParam());
  const std::string daemon_endpoint = xray_fake_daemon.localAddress()->asString();
  Tracer tracer{"my_segment",        "origin",
                aws_metadata,        std::make_unique<DaemonBrokerImpl>(daemon_endpoint),
                server.timeSource(), server.api().randomGenerator()};
  auto span = tracer.startSpan("ingress" /*operation name*/, server.timeSource().systemTime(),
                               absl::nullopt /*headers*/);

  span->setTag("http.status_code", "202");
  span->finishSpan();

  Network::UdpRecvData datagram;
  xray_fake_daemon.recv(datagram);

  const std::string header_json = R"EOF({"format":"json","version":1})EOF";
  // The UDP datagram contains two independent, consecutive JSON documents; a header and a body.
  const std::string payload = datagram.buffer_->toString();
  // Make sure the payload has enough data.
  ASSERT_GT(payload.length(), header_json.length());
  // Skip the header since we're only interested in the body.
  const std::string body = payload.substr(header_json.length());

  EXPECT_EQ(0, payload.find(header_json));

  // Deserialize the body to verify it.
  source::extensions::tracers::xray::daemon::Segment seg;
  MessageUtil::loadFromJson(body, seg, ProtobufMessage::getNullValidationVisitor());
  EXPECT_STREQ("my_segment", seg.name().c_str());
  for (auto&& f : seg.http().request().fields()) {
    // there should only be a single field
    EXPECT_EQ(202, f.second.number_value());
  }
}

} // namespace
} // namespace XRay
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
