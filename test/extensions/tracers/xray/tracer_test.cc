#include <string>
#include <vector>

#include "envoy/common/time.h"

#include "source/common/protobuf/utility.h"
#include "source/extensions/tracers/xray/daemon.pb.h"
#include "source/extensions/tracers/xray/daemon.pb.validate.h"
#include "source/extensions/tracers/xray/tracer.h"
#include "source/extensions/tracers/xray/xray_configuration.h"

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

struct TraceProperties {
  TraceProperties(const std::string& span_name, const std::string& origin_name,
                  const std::string& aws_key_value, const std::string& operation_name,
                  const std::string& http_method, const std::string& http_url,
                  const std::string& user_agent, const std::string& direction)
      : span_name(span_name), origin_name(origin_name), aws_key_value(aws_key_value),
        operation_name(operation_name), http_method(http_method), http_url(http_url),
        user_agent(user_agent), direction(direction) {}
  const std::string span_name;
  const std::string origin_name;
  const std::string aws_key_value;
  const std::string operation_name;
  const std::string http_method;
  const std::string http_url;
  const std::string user_agent;
  const std::string direction;
};

class XRayTracerTest : public ::testing::Test {
public:
  XRayTracerTest()
      : broker_(std::make_unique<MockDaemonBroker>("127.0.0.1:2000")),
        expected_(std::make_unique<TraceProperties>(
            "Service 1", "AWS::Service::Proxy", "test_value", "egress hostname", "POST",
            "/first/second", "Mozilla/5.0 (Macintosh; Intel Mac OS X)", "egress")) {}
  absl::flat_hash_map<std::string, ProtobufWkt::Value> aws_metadata_;
  NiceMock<Server::MockInstance> server_;
  NiceMock<Tracing::MockConfig> config_;
  std::unique_ptr<MockDaemonBroker> broker_;
  std::unique_ptr<TraceProperties> expected_;
  void commonAsserts(daemon::Segment& s);
};

void XRayTracerTest::commonAsserts(daemon::Segment& s) {
  EXPECT_EQ(expected_->span_name, s.name().c_str());
  EXPECT_EQ(expected_->origin_name, s.origin().c_str());
  EXPECT_EQ(expected_->aws_key_value, s.aws().fields().at("key").string_value().c_str());
  EXPECT_EQ(expected_->http_method,
            s.http().request().fields().at("method").string_value().c_str());
  EXPECT_EQ(expected_->http_url, s.http().request().fields().at("url").string_value().c_str());
  EXPECT_EQ(expected_->user_agent,
            s.http().request().fields().at(Tracing::Tags::get().UserAgent).string_value().c_str());
  EXPECT_EQ(expected_->direction, s.annotations().at("direction").c_str());
}

TEST_F(XRayTracerTest, SerializeSpanTest) {
  constexpr uint32_t expected_status_code = 202;
  constexpr uint32_t expected_content_length = 1337;
  constexpr absl::string_view expected_client_ip = "10.0.0.100";
  constexpr bool expected_x_forwarded_for = false;
  constexpr absl::string_view expected_upstream_address = "10.0.0.200";

  auto on_send = [&](const std::string& json) {
    ASSERT_FALSE(json.empty());
    daemon::Segment s;
    MessageUtil::loadFromJson(json, s, ProtobufMessage::getNullValidationVisitor());
    TestUtility::validate(s);
    commonAsserts(s);
    EXPECT_FALSE(s.trace_id().empty());
    EXPECT_FALSE(s.id().empty());
    EXPECT_EQ(2, s.annotations().size());
    EXPECT_TRUE(s.parent_id().empty());
    EXPECT_TRUE(s.type().empty());
    EXPECT_FALSE(s.fault());    /*server error*/
    EXPECT_FALSE(s.error());    /*client error*/
    EXPECT_FALSE(s.throttle()); /*request throttled*/
    EXPECT_EQ(expected_status_code,
              s.http().response().fields().at(Tracing::Tags::get().Status).number_value());
    EXPECT_EQ(expected_content_length,
              s.http().response().fields().at("content_length").number_value());
    EXPECT_EQ(expected_client_ip,
              s.http().request().fields().at("client_ip").string_value().c_str());
    EXPECT_EQ(expected_x_forwarded_for,
              s.http().request().fields().at("x_forwarded_for").bool_value());
    EXPECT_EQ(expected_upstream_address,
              s.annotations().at(Tracing::Tags::get().UpstreamAddress).c_str());
  };

  ON_CALL(config_, operationName()).WillByDefault(Return(Tracing::OperationName::Egress));
  EXPECT_CALL(*broker_, send(_)).WillOnce(Invoke(on_send));
  aws_metadata_.insert({"key", ValueUtil::stringValue(expected_->aws_key_value)});
  Tracer tracer{expected_->span_name, expected_->origin_name, aws_metadata_,
                std::move(broker_),   server_.timeSource(),   server_.api().randomGenerator()};
  auto span = tracer.startSpan(config_, expected_->operation_name,
                               server_.timeSource().systemTime(), absl::nullopt /*headers*/,
                               absl::nullopt /*client_ip from x-forwarded-for header*/);
  span->setTag(Tracing::Tags::get().HttpMethod, expected_->http_method);
  span->setTag(Tracing::Tags::get().HttpUrl, expected_->http_url);
  span->setTag(Tracing::Tags::get().UserAgent, expected_->user_agent);
  span->setTag(Tracing::Tags::get().HttpStatusCode, absl::StrFormat("%d", expected_status_code));
  span->setTag(Tracing::Tags::get().ResponseSize, absl::StrFormat("%d", expected_content_length));
  span->setTag(Tracing::Tags::get().PeerAddress, expected_client_ip);
  span->setTag(Tracing::Tags::get().UpstreamAddress, expected_upstream_address);
  span->finishSpan();
}

TEST_F(XRayTracerTest, SerializeSpanTestXForwardedForSet) {
  constexpr uint32_t expected_status_code = 202;
  constexpr uint32_t expected_content_length = 1337;
  constexpr absl::string_view expected_client_ip = "190.0.0.100";
  constexpr absl::string_view unexpected_client_ip = "10.0.0.100";
  constexpr bool expected_x_forwarded_for = true;
  constexpr absl::string_view expected_upstream_address = "10.0.0.200";

  auto on_send = [&](const std::string& json) {
    ASSERT_FALSE(json.empty());
    daemon::Segment s;
    MessageUtil::loadFromJson(json, s, ProtobufMessage::getNullValidationVisitor());
    TestUtility::validate(s);
    commonAsserts(s);
    EXPECT_FALSE(s.trace_id().empty());
    EXPECT_FALSE(s.id().empty());
    EXPECT_EQ(2, s.annotations().size());
    EXPECT_TRUE(s.parent_id().empty());
    EXPECT_FALSE(s.fault());    /*server error*/
    EXPECT_FALSE(s.error());    /*client error*/
    EXPECT_FALSE(s.throttle()); /*request throttled*/
    EXPECT_EQ(expected_status_code,
              s.http().response().fields().at(Tracing::Tags::get().Status).number_value());
    EXPECT_EQ(expected_content_length,
              s.http().response().fields().at("content_length").number_value());
    EXPECT_EQ(expected_client_ip,
              s.http().request().fields().at("client_ip").string_value().c_str());
    EXPECT_EQ(expected_x_forwarded_for,
              s.http().request().fields().at("x_forwarded_for").bool_value());
    EXPECT_EQ(expected_upstream_address,
              s.annotations().at(Tracing::Tags::get().UpstreamAddress).c_str());
  };

  ON_CALL(config_, operationName()).WillByDefault(Return(Tracing::OperationName::Egress));
  EXPECT_CALL(*broker_, send(_)).WillOnce(Invoke(on_send));
  aws_metadata_.insert({"key", ValueUtil::stringValue(expected_->aws_key_value)});
  Tracer tracer{expected_->span_name, expected_->origin_name, aws_metadata_,
                std::move(broker_),   server_.timeSource(),   server_.api().randomGenerator()};
  auto span = tracer.startSpan(config_, expected_->operation_name,
                               server_.timeSource().systemTime(), absl::nullopt /*headers*/,
                               expected_client_ip /*client_ip from x-forwarded-for header*/);
  span->setTag(Tracing::Tags::get().HttpMethod, expected_->http_method);
  span->setTag(Tracing::Tags::get().HttpUrl, expected_->http_url);
  span->setTag(Tracing::Tags::get().UserAgent, expected_->user_agent);
  span->setTag(Tracing::Tags::get().HttpStatusCode, absl::StrFormat("%d", expected_status_code));
  span->setTag(Tracing::Tags::get().ResponseSize, absl::StrFormat("%d", expected_content_length));
  span->setTag(Tracing::Tags::get().PeerAddress, unexpected_client_ip);
  span->setTag(Tracing::Tags::get().UpstreamAddress, expected_upstream_address);
  span->finishSpan();
}

TEST_F(XRayTracerTest, SerializeSpanTestServerError) {
  constexpr absl::string_view expected_error = "true";
  constexpr uint32_t expected_status_code = 503;

  auto on_send = [&](const std::string& json) {
    ASSERT_FALSE(json.empty());
    daemon::Segment s;
    MessageUtil::loadFromJson(json, s, ProtobufMessage::getNullValidationVisitor());
    TestUtility::validate(s);
    commonAsserts(s);
    EXPECT_FALSE(s.trace_id().empty());
    EXPECT_FALSE(s.id().empty());
    EXPECT_TRUE(s.parent_id().empty());
    EXPECT_TRUE(s.type().empty());
    EXPECT_TRUE(s.fault());  /*server error*/
    EXPECT_FALSE(s.error()); /*client error*/
    EXPECT_EQ(expected_status_code,
              s.http().response().fields().at(Tracing::Tags::get().Status).number_value());
  };

  ON_CALL(config_, operationName()).WillByDefault(Return(Tracing::OperationName::Egress));
  EXPECT_CALL(*broker_, send(_)).WillOnce(Invoke(on_send));
  aws_metadata_.insert({"key", ValueUtil::stringValue(expected_->aws_key_value)});
  Tracer tracer{expected_->span_name, expected_->origin_name, aws_metadata_,
                std::move(broker_),   server_.timeSource(),   server_.api().randomGenerator()};
  auto span = tracer.startSpan(config_, expected_->operation_name,
                               server_.timeSource().systemTime(), absl::nullopt /*headers*/,
                               absl::nullopt /*client_ip from x-forwarded-for header*/);
  span->setTag(Tracing::Tags::get().HttpMethod, expected_->http_method);
  span->setTag(Tracing::Tags::get().HttpUrl, expected_->http_url);
  span->setTag(Tracing::Tags::get().UserAgent, expected_->user_agent);
  span->setTag(Tracing::Tags::get().Error, expected_error);
  span->setTag(Tracing::Tags::get().HttpStatusCode, absl::StrFormat("%d", expected_status_code));
  span->finishSpan();
}

TEST_F(XRayTracerTest, SerializeSpanTestClientError) {
  constexpr uint32_t expected_status_code = 404;

  auto on_send = [&](const std::string& json) {
    ASSERT_FALSE(json.empty());
    daemon::Segment s;
    MessageUtil::loadFromJson(json, s, ProtobufMessage::getNullValidationVisitor());
    TestUtility::validate(s);
    commonAsserts(s);
    EXPECT_FALSE(s.trace_id().empty());
    EXPECT_FALSE(s.id().empty());
    EXPECT_TRUE(s.parent_id().empty());
    EXPECT_TRUE(s.type().empty());
    EXPECT_FALSE(s.fault());    /*server error*/
    EXPECT_TRUE(s.error());     /*client error*/
    EXPECT_FALSE(s.throttle()); /*request throttled*/
    EXPECT_EQ(expected_status_code,
              s.http().response().fields().at(Tracing::Tags::get().Status).number_value());
  };

  ON_CALL(config_, operationName()).WillByDefault(Return(Tracing::OperationName::Egress));
  EXPECT_CALL(*broker_, send(_)).WillOnce(Invoke(on_send));
  aws_metadata_.insert({"key", ValueUtil::stringValue(expected_->aws_key_value)});
  Tracer tracer{expected_->span_name, expected_->origin_name, aws_metadata_,
                std::move(broker_),   server_.timeSource(),   server_.api().randomGenerator()};
  auto span = tracer.startSpan(config_, expected_->operation_name,
                               server_.timeSource().systemTime(), absl::nullopt /*headers*/,
                               absl::nullopt /*client_ip from x-forwarded-for header*/);
  span->setTag(Tracing::Tags::get().HttpMethod, expected_->http_method);
  span->setTag(Tracing::Tags::get().HttpUrl, expected_->http_url);
  span->setTag(Tracing::Tags::get().UserAgent, expected_->user_agent);
  span->setTag(Tracing::Tags::get().HttpStatusCode, absl::StrFormat("%d", expected_status_code));
  span->finishSpan();
}

TEST_F(XRayTracerTest, SerializeSpanTestClientErrorWithThrottle) {
  constexpr uint32_t expected_status_code = 429;

  auto on_send = [&](const std::string& json) {
    ASSERT_FALSE(json.empty());
    daemon::Segment s;
    MessageUtil::loadFromJson(json, s, ProtobufMessage::getNullValidationVisitor());
    TestUtility::validate(s);
    commonAsserts(s);
    EXPECT_FALSE(s.trace_id().empty());
    EXPECT_FALSE(s.id().empty());
    EXPECT_TRUE(s.parent_id().empty());
    EXPECT_TRUE(s.type().empty());
    EXPECT_FALSE(s.fault());   /*server error*/
    EXPECT_TRUE(s.error());    /*client error*/
    EXPECT_TRUE(s.throttle()); /*request throttled*/
    EXPECT_EQ(expected_status_code,
              s.http().response().fields().at(Tracing::Tags::get().Status).number_value());
  };

  ON_CALL(config_, operationName()).WillByDefault(Return(Tracing::OperationName::Egress));
  EXPECT_CALL(*broker_, send(_)).WillOnce(Invoke(on_send));
  aws_metadata_.insert({"key", ValueUtil::stringValue(expected_->aws_key_value)});
  Tracer tracer{expected_->span_name, expected_->origin_name, aws_metadata_,
                std::move(broker_),   server_.timeSource(),   server_.api().randomGenerator()};
  auto span = tracer.startSpan(config_, expected_->operation_name,
                               server_.timeSource().systemTime(), absl::nullopt /*headers*/,
                               absl::nullopt /*client_ip from x-forwarded-for header*/);
  span->setTag(Tracing::Tags::get().HttpMethod, expected_->http_method);
  span->setTag(Tracing::Tags::get().HttpUrl, expected_->http_url);
  span->setTag(Tracing::Tags::get().UserAgent, expected_->user_agent);
  span->setTag(Tracing::Tags::get().HttpStatusCode, absl::StrFormat("%d", expected_status_code));
  span->finishSpan();
}

TEST_F(XRayTracerTest, SerializeSpanTestWithEmptyValue) {
  auto on_send = [&](const std::string& json) {
    ASSERT_FALSE(json.empty());
    daemon::Segment s;
    MessageUtil::loadFromJson(json, s, ProtobufMessage::getNullValidationVisitor());
    TestUtility::validate(s);
    commonAsserts(s);
    EXPECT_FALSE(s.trace_id().empty());
    EXPECT_FALSE(s.id().empty());
    EXPECT_TRUE(s.parent_id().empty());
    EXPECT_TRUE(s.type().empty());
    EXPECT_FALSE(s.http().request().fields().contains(Tracing::Tags::get().Status));
  };

  ON_CALL(config_, operationName()).WillByDefault(Return(Tracing::OperationName::Egress));
  EXPECT_CALL(*broker_, send(_)).WillOnce(Invoke(on_send));
  aws_metadata_.insert({"key", ValueUtil::stringValue(expected_->aws_key_value)});
  Tracer tracer{expected_->span_name, expected_->origin_name, aws_metadata_,
                std::move(broker_),   server_.timeSource(),   server_.api().randomGenerator()};
  auto span = tracer.startSpan(config_, expected_->operation_name,
                               server_.timeSource().systemTime(), absl::nullopt /*headers*/,
                               absl::nullopt /*client_ip from x-forwarded-for header*/);
  span->setTag(Tracing::Tags::get().HttpMethod, expected_->http_method);
  span->setTag(Tracing::Tags::get().HttpUrl, expected_->http_url);
  span->setTag(Tracing::Tags::get().UserAgent, expected_->user_agent);
  span->setTag(Tracing::Tags::get().HttpStatusCode, ""); // Send empty string for value
  span->finishSpan();
}

TEST_F(XRayTracerTest, SerializeSpanTestWithStatusCodeNotANumber) {
  constexpr absl::string_view expected_status_code = "ok"; // status code which is not a number
  constexpr absl::string_view expected_content_length =
      "huge"; // response length which is not a number

  auto on_send = [&](const std::string& json) {
    ASSERT_FALSE(json.empty());
    daemon::Segment s;
    MessageUtil::loadFromJson(json, s, ProtobufMessage::getNullValidationVisitor());
    TestUtility::validate(s);
    commonAsserts(s);
    EXPECT_FALSE(s.trace_id().empty());
    EXPECT_FALSE(s.id().empty());
    EXPECT_TRUE(s.parent_id().empty());
    EXPECT_TRUE(s.type().empty());
    EXPECT_FALSE(s.http().request().fields().contains(Tracing::Tags::get().Status));
    EXPECT_FALSE(s.http().request().fields().contains("content_length"));
  };

  ON_CALL(config_, operationName()).WillByDefault(Return(Tracing::OperationName::Egress));
  EXPECT_CALL(*broker_, send(_)).WillOnce(Invoke(on_send));
  aws_metadata_.insert({"key", ValueUtil::stringValue(expected_->aws_key_value)});
  Tracer tracer{expected_->span_name, expected_->origin_name, aws_metadata_,
                std::move(broker_),   server_.timeSource(),   server_.api().randomGenerator()};
  auto span = tracer.startSpan(config_, expected_->operation_name,
                               server_.timeSource().systemTime(), absl::nullopt /*headers*/,
                               absl::nullopt /*client_ip from x-forwarded-for header*/);
  span->setTag(Tracing::Tags::get().HttpMethod, expected_->http_method);
  span->setTag(Tracing::Tags::get().HttpUrl, expected_->http_url);
  span->setTag(Tracing::Tags::get().UserAgent, expected_->user_agent);
  span->setTag(Tracing::Tags::get().HttpStatusCode, expected_status_code);
  span->setTag(Tracing::Tags::get().ResponseSize, expected_content_length);
  span->setTag("", "");
  span->finishSpan();
}

TEST_F(XRayTracerTest, NonSampledSpansNotSerialized) {
  Tracer tracer{"" /*span name*/,   "" /*origin*/,        aws_metadata_,
                std::move(broker_), server_.timeSource(), server_.api().randomGenerator()};
  auto span = tracer.createNonSampledSpan(absl::nullopt /*headers*/);
  span->finishSpan();
}

TEST_F(XRayTracerTest, BaggageNotImplemented) {
  Tracer tracer{"" /*span name*/,   "" /*origin*/,        aws_metadata_,
                std::move(broker_), server_.timeSource(), server_.api().randomGenerator()};
  auto span = tracer.createNonSampledSpan(absl::nullopt /*headers*/);
  span->setBaggage("baggage_key", "baggage_value");
  span->finishSpan();

  // Baggage isn't supported so getBaggage should always return empty
  EXPECT_EQ("", span->getBaggage("baggage_key"));
}

TEST_F(XRayTracerTest, LogNotImplemented) {
  Tracer tracer{"" /*span name*/,   "" /*origin*/,        aws_metadata_,
                std::move(broker_), server_.timeSource(), server_.api().randomGenerator()};
  auto span = tracer.createNonSampledSpan(absl::nullopt /*headers*/);
  span->log(SystemTime{std::chrono::duration<int, std::milli>(100)}, "dummy log value");
  span->finishSpan();
  // Nothing to assert here as log is a dummy function
}

TEST_F(XRayTracerTest, GetTraceId) {
  Tracer tracer{"" /*span name*/,   "" /*origin*/,        aws_metadata_,
                std::move(broker_), server_.timeSource(), server_.api().randomGenerator()};
  auto span = tracer.createNonSampledSpan(absl::nullopt /*headers*/);
  span->finishSpan();

  // Trace ID is always generated
  EXPECT_NE(span->getTraceId(), "");

  // This method is unimplemented and a noop.
  EXPECT_EQ(span->getSpanId(), "");
}

TEST_F(XRayTracerTest, ChildSpanHasParentInfo) {
  ON_CALL(config_, operationName()).WillByDefault(Return(Tracing::OperationName::Ingress));
  const auto& broker = *broker_;
  Tracer tracer{expected_->span_name, "",
                aws_metadata_,        std::move(broker_),
                server_.timeSource(), server_.api().randomGenerator()};
  // Span id taken from random generator
  EXPECT_CALL(server_.api_.random_, random()).WillOnce(Return(999));
  auto parent_span = tracer.startSpan(config_, expected_->operation_name,
                                      server_.timeSource().systemTime(), absl::nullopt /*headers*/,
                                      absl::nullopt /*client_ip from x-forwarded-for header*/);

  const XRay::Span* xray_parent_span = static_cast<XRay::Span*>(parent_span.get());
  auto on_send = [&](const std::string& json) {
    ASSERT_FALSE(json.empty());
    daemon::Segment s;
    MessageUtil::loadFromJson(json, s, ProtobufMessage::getNullValidationVisitor());
    TestUtility::validate(s);
    // Hex encoded 64 bit identifier
    EXPECT_STREQ("00000000000003e7", s.parent_id().c_str());
    EXPECT_EQ(expected_->operation_name, s.name().c_str());
    EXPECT_TRUE(xray_parent_span->type().empty());
    EXPECT_EQ(Subsegment, s.type().c_str());
    EXPECT_STREQ(xray_parent_span->traceId().c_str(), s.trace_id().c_str());
    EXPECT_STREQ("0000003d25bebe62", s.id().c_str());
  };

  EXPECT_CALL(broker, send(_)).WillOnce(Invoke(on_send));

  // Span id taken from random generator
  EXPECT_CALL(server_.api_.random_, random()).WillOnce(Return(262626262626));
  auto child = parent_span->spawnChild(config_, expected_->operation_name,
                                       server_.timeSource().systemTime());
  child->finishSpan();
}

TEST_F(XRayTracerTest, UseExistingHeaderInformation) {
  ON_CALL(config_, operationName()).WillByDefault(Return(Tracing::OperationName::Ingress));
  XRayHeader xray_header;
  xray_header.trace_id_ = "a";
  xray_header.parent_id_ = "b";
  constexpr absl::string_view span_name = "my span";

  Tracer tracer{span_name,
                "",
                aws_metadata_,
                std::move(broker_),
                server_.timeSource(),
                server_.api().randomGenerator()};
  auto span = tracer.startSpan(config_, "ingress", server_.timeSource().systemTime(), xray_header,
                               absl::nullopt /*client_ip from x-forwarded-for header*/);

  const XRay::Span* xray_span = static_cast<XRay::Span*>(span.get());
  EXPECT_STREQ(xray_header.trace_id_.c_str(), xray_span->traceId().c_str());
  EXPECT_STREQ(xray_header.parent_id_.c_str(), xray_span->parentId().c_str());
}

TEST_F(XRayTracerTest, DontStartSpanOnNonSampledSpans) {
  ON_CALL(config_, operationName()).WillByDefault(Return(Tracing::OperationName::Ingress));
  XRayHeader xray_header;
  xray_header.trace_id_ = "a";
  xray_header.parent_id_ = "b";
  xray_header.sample_decision_ =
      SamplingDecision::NotSampled; // not sampled means we should panic on calling startSpan
  constexpr absl::string_view span_name = "my span";

  Tracer tracer{span_name,
                "",
                aws_metadata_,
                std::move(broker_),
                server_.timeSource(),
                server_.api().randomGenerator()};
  Tracing::SpanPtr span;
  EXPECT_ENVOY_BUG(span = tracer.startSpan(config_, "ingress", server_.timeSource().systemTime(),
                                           xray_header,
                                           absl::nullopt /*client_ip from x-forwarded-for header*/),
                   "unexpected code path hit");
}

TEST_F(XRayTracerTest, UnknownSpanStillSampled) {
  ON_CALL(config_, operationName()).WillByDefault(Return(Tracing::OperationName::Ingress));
  XRayHeader xray_header;
  xray_header.trace_id_ = "a";
  xray_header.parent_id_ = "b";
  xray_header.sample_decision_ = SamplingDecision::Unknown;
  constexpr absl::string_view span_name = "my span";

  Tracer tracer{span_name,
                "",
                aws_metadata_,
                std::move(broker_),
                server_.timeSource(),
                server_.api().randomGenerator()};
  auto span = tracer.startSpan(config_, "ingress", server_.timeSource().systemTime(), xray_header,
                               absl::nullopt /*client_ip from x-forwarded-for header*/);

  const XRay::Span* xray_span = static_cast<XRay::Span*>(span.get());
  EXPECT_STREQ(xray_header.trace_id_.c_str(), xray_span->traceId().c_str());
  EXPECT_STREQ(xray_header.parent_id_.c_str(), xray_span->parentId().c_str());
  // Doesn't matter if the x-ray header says that the sampling decision if unknown,
  // as soon as we start a span it is by default sampled.
  EXPECT_TRUE(xray_span->sampled());
}

TEST_F(XRayTracerTest, SpanInjectContextHasXRayHeader) {
  ON_CALL(config_, operationName()).WillByDefault(Return(Tracing::OperationName::Ingress));
  constexpr absl::string_view span_name = "my span";

  Tracer tracer{span_name,
                "",
                aws_metadata_,
                std::move(broker_),
                server_.timeSource(),
                server_.api().randomGenerator()};
  auto span = tracer.startSpan(config_, "ingress", server_.timeSource().systemTime(),
                               absl::nullopt /*headers*/,
                               absl::nullopt /*client_ip from x-forwarded-for header*/);
  Tracing::TestTraceContextImpl request_headers{};
  span->injectContext(request_headers, Tracing::UpstreamContext());
  auto header = request_headers.get(xRayTraceHeader().key());
  ASSERT_FALSE(!header.has_value());
  EXPECT_NE(header.value().find("Root="), absl::string_view::npos);
  EXPECT_NE(header.value().find("Parent="), absl::string_view::npos);
  EXPECT_NE(header.value().find("Sampled=1"), absl::string_view::npos);
}

TEST_F(XRayTracerTest, SpanInjectContextHasXRayHeaderNonSampled) {
  constexpr absl::string_view span_name = "my span";
  Tracer tracer{span_name,
                "",
                aws_metadata_,
                std::move(broker_),
                server_.timeSource(),
                server_.api().randomGenerator()};
  auto span = tracer.createNonSampledSpan(absl::nullopt /*headers*/);
  Tracing::TestTraceContextImpl request_headers{};
  span->injectContext(request_headers, Tracing::UpstreamContext());
  auto header = request_headers.get(xRayTraceHeader().key());
  ASSERT_FALSE(!header.has_value());
  EXPECT_NE(header.value().find("Root="), absl::string_view::npos);
  EXPECT_NE(header.value().find("Parent="), absl::string_view::npos);
  EXPECT_NE(header.value().find("Sampled=0"), absl::string_view::npos);
}

TEST_F(XRayTracerTest, TraceIDFormatTest) {
  constexpr absl::string_view span_name = "my span";
  Tracer tracer{span_name,
                "",
                aws_metadata_,
                std::move(broker_),
                server_.timeSource(),
                server_.api().randomGenerator()};
  // startSpan and createNonSampledSpan use the same logic to create a trace ID
  auto span = tracer.createNonSampledSpan(absl::nullopt /*headers*/);
  XRay::Span* xray_span = span.get();
  std::vector<std::string> parts = absl::StrSplit(xray_span->traceId(), absl::ByChar('-'));
  EXPECT_EQ(3, parts.size());
  EXPECT_EQ(1, parts[0].length());
  EXPECT_EQ(8, parts[1].length());
  EXPECT_EQ(24, parts[2].length());
}

TEST_F(XRayTracerTest, UseExistingHeaderInformationNonSampled) {
  XRayHeader xray_header;
  xray_header.trace_id_ = "a";
  xray_header.parent_id_ = "b";
  constexpr absl::string_view span_name = "my span";

  Tracer tracer{span_name,
                "",
                aws_metadata_,
                std::move(broker_),
                server_.timeSource(),
                server_.api().randomGenerator()};
  auto span = tracer.createNonSampledSpan(xray_header);

  const XRay::Span* xray_span = static_cast<XRay::Span*>(span.get());
  EXPECT_STREQ(xray_header.trace_id_.c_str(), xray_span->traceId().c_str());
  EXPECT_STREQ(xray_header.parent_id_.c_str(), xray_span->parentId().c_str());
  EXPECT_FALSE(xray_span->sampled());
}

class XRayDaemonTest : public testing::TestWithParam<Network::Address::IpVersion> {};

INSTANTIATE_TEST_SUITE_P(IpVersions, XRayDaemonTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(XRayDaemonTest, VerifyUdpPacketContents) {
  NiceMock<Tracing::MockConfig> config_;
  ON_CALL(config_, operationName()).WillByDefault(Return(Tracing::OperationName::Ingress));
  absl::flat_hash_map<std::string, ProtobufWkt::Value> aws_metadata;
  NiceMock<Server::MockInstance> server;
  Network::Test::UdpSyncPeer xray_fake_daemon(GetParam());
  const std::string daemon_endpoint = xray_fake_daemon.localAddress()->asString();
  Tracer tracer{"my_segment",        "origin",
                aws_metadata,        std::make_unique<DaemonBrokerImpl>(daemon_endpoint),
                server.timeSource(), server.api().randomGenerator()};
  auto span = tracer.startSpan(config_, "ingress" /*operation name*/,
                               server.timeSource().systemTime(), absl::nullopt /*headers*/,
                               absl::nullopt /*client_ip from x-forwarded-for header*/);

  span->setTag(Tracing::Tags::get().HttpStatusCode, "202");
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
  TestUtility::validate(seg);
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
