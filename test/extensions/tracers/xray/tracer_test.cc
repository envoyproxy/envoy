#include <string>
#include <vector>

#include "envoy/common/time.h"

#include "common/protobuf/utility.h"

#include "source/extensions/tracers/xray/daemon.pb.h"

#include "extensions/tracers/xray/tracer.h"
#include "extensions/tracers/xray/xray_configuration.h"

#include "test/mocks/server/mocks.h"
#include "test/mocks/tracing/mocks.h"

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
using namespace source::extensions::tracers::xray;

struct MockDaemonBroker : DaemonBroker {
  MockDaemonBroker(const std::string& endpoint) { UNREFERENCED_PARAMETER(endpoint); }
  MOCK_METHOD(void, send, (std::string const&), (const, override));
};

class XRayTracerTest : public ::testing::Test {
public:
  XRayTracerTest() : broker_(std::make_unique<MockDaemonBroker>("127.0.0.1:2000")) {}

  NiceMock<Server::MockInstance> server_;
  std::unique_ptr<MockDaemonBroker> broker_;
};

TEST_F(XRayTracerTest, SerializeSpanTest) {
  constexpr auto expected_span_name = "Service 1";
  constexpr auto expected_operation_name = "Create";
  constexpr auto expected_http_method = "POST";
  constexpr auto expected_http_url = "/first/second";
  constexpr auto expected_user_agent = "Mozilla/5.0 (Macintosh; Intel Mac OS X)";
  constexpr auto expected_status_code = "202";
  constexpr auto expected_content_length = "1337";
  constexpr auto expected_client_ip = "10.0.0.100";
  constexpr auto expected_x_forwarded_for = "false";
  constexpr auto expected_upstream_address = "10.0.0.200";

  auto on_send = [](const std::string& json) {
    ASSERT_FALSE(json.empty());
    daemon::Segment s;
    MessageUtil::loadFromJson(json, s, ProtobufMessage::getNullValidationVisitor());
    ASSERT_FALSE(s.trace_id().empty());
    ASSERT_FALSE(s.id().empty());
    ASSERT_EQ(1, s.annotations().size());
    ASSERT_TRUE(s.parent_id().empty());
    ASSERT_STREQ(expected_span_name, s.name().c_str());
    ASSERT_STREQ(expected_http_method, s.http().request().at("method").c_str());
    ASSERT_STREQ(expected_http_url, s.http().request().at("url").c_str());
    ASSERT_STREQ(expected_user_agent, s.http().request().at("user_agent").c_str());
    ASSERT_STREQ(expected_status_code, s.http().response().at("status").c_str());
    ASSERT_STREQ(expected_content_length, s.http().response().at("content_length").c_str());
    ASSERT_STREQ(expected_client_ip, s.http().request().at("client_ip").c_str());
    ASSERT_STREQ(expected_x_forwarded_for, s.http().request().at("x_forwarded_for").c_str());
    ASSERT_STREQ(expected_upstream_address, s.annotations().at("upstream_address").c_str());
  };

  EXPECT_CALL(*broker_, send(_)).WillOnce(Invoke(on_send));
  Tracer tracer{expected_span_name, std::move(broker_), server_.timeSource()};
  auto span = tracer.startSpan(expected_operation_name, server_.timeSource().systemTime(),
                               absl::nullopt /*headers*/);
  span->setTag("http.method", expected_http_method);
  span->setTag("http.url", expected_http_url);
  span->setTag("user_agent", expected_user_agent);
  span->setTag("http.status_code", expected_status_code);
  span->setTag("response_size", expected_content_length);
  span->setTag("peer.address", expected_client_ip);
  span->setTag("upstream_address", expected_upstream_address);
  span->finishSpan();
}

TEST_F(XRayTracerTest, NonSampledSpansNotSerialized) {
  Tracer tracer{"" /*span name*/, std::move(broker_), server_.timeSource()};
  auto span = tracer.createNonSampledSpan();
  span->finishSpan();
}

TEST_F(XRayTracerTest, ChildSpanHasParentInfo) {
  NiceMock<Tracing::MockConfig> config;
  constexpr auto expected_span_name = "Service 1";
  constexpr auto expected_operation_name = "Create";
  const auto& broker = *broker_;
  Tracer tracer{expected_span_name, std::move(broker_), server_.timeSource()};
  auto parent_span = tracer.startSpan(expected_operation_name, server_.timeSource().systemTime(),
                                      absl::nullopt /*headers*/);

  const XRay::Span* xray_parent_span = static_cast<XRay::Span*>(parent_span.get());
  const std::string expected_parent_id = xray_parent_span->Id();
  auto on_send = [xray_parent_span, expected_parent_id](const std::string& json) {
    ASSERT_FALSE(json.empty());
    daemon::Segment s;
    MessageUtil::loadFromJson(json, s, ProtobufMessage::getNullValidationVisitor());
    ASSERT_STREQ(expected_parent_id.c_str(), s.parent_id().c_str());
    ASSERT_STREQ(expected_operation_name, s.name().c_str());
    ASSERT_STREQ(xray_parent_span->traceId().c_str(), s.trace_id().c_str());
    ASSERT_STRNE(xray_parent_span->Id().c_str(), s.id().c_str());
  };

  EXPECT_CALL(broker, send(_)).WillOnce(Invoke(on_send));

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

  Tracer tracer{span_name, std::move(broker_), server_.timeSource()};
  auto span = tracer.startSpan(operation_name, server_.timeSource().systemTime(), xray_header);

  const XRay::Span* xray_span = static_cast<XRay::Span*>(span.get());
  ASSERT_STREQ(xray_header.trace_id_.c_str(), xray_span->traceId().c_str());
  ASSERT_STREQ(xray_header.parent_id_.c_str(), xray_span->parentId().c_str());
}

TEST_F(XRayTracerTest, SpanInjectContextHasXRayHeader) {
  constexpr auto span_name = "my span";
  constexpr auto operation_name = "my operation";

  Tracer tracer{span_name, std::move(broker_), server_.timeSource()};
  auto span = tracer.startSpan(operation_name, server_.timeSource().systemTime(),
                               absl::nullopt /*headers*/);
  Http::RequestHeaderMapImpl request_headers;
  span->injectContext(request_headers);
  auto* header = request_headers.get(Http::LowerCaseString{XRayTraceHeader});
  ASSERT_NE(header, nullptr);
  ASSERT_NE(header->value().getStringView().find("root="), absl::string_view::npos);
  ASSERT_NE(header->value().getStringView().find("parent="), absl::string_view::npos);
  ASSERT_NE(header->value().getStringView().find("sampled=1"), absl::string_view::npos);
}

TEST_F(XRayTracerTest, SpanInjectContextHasXRayHeaderNonSampled) {
  constexpr auto span_name = "my span";
  Tracer tracer{span_name, std::move(broker_), server_.timeSource()};
  auto span = tracer.createNonSampledSpan();
  Http::RequestHeaderMapImpl request_headers;
  span->injectContext(request_headers);
  auto* header = request_headers.get(Http::LowerCaseString{XRayTraceHeader});
  ASSERT_NE(header, nullptr);
  ASSERT_NE(header->value().getStringView().find("root="), absl::string_view::npos);
  ASSERT_NE(header->value().getStringView().find("parent="), absl::string_view::npos);
  ASSERT_NE(header->value().getStringView().find("sampled=0"), absl::string_view::npos);
}

TEST_F(XRayTracerTest, TraceIDFormatTest) {
  constexpr auto span_name = "my span";
  Tracer tracer{span_name, std::move(broker_), server_.timeSource()};
  auto span = tracer.createNonSampledSpan(); // startSpan and createNonSampledSpan use the same
                                             // logic to create a trace ID
  XRay::Span* xray_span = span.get();
  std::vector<std::string> parts = absl::StrSplit(xray_span->traceId(), absl::ByChar('-'));
  ASSERT_EQ(3, parts.size());
  ASSERT_EQ(1, parts[0].length());
  ASSERT_EQ(8, parts[1].length());
  ASSERT_EQ(24, parts[2].length());
}

} // namespace
} // namespace XRay
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
