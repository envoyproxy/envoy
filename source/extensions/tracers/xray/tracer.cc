#include "source/extensions/tracers/xray/tracer.h"

#include <algorithm>
#include <chrono>
#include <string>

#include "envoy/http/header_map.h"
#include "envoy/network/listener.h"

#include "source/common/common/assert.h"
#include "source/common/common/fmt.h"
#include "source/common/protobuf/utility.h"
#include "source/common/tracing/http_tracer_impl.h"
#include "source/extensions/tracers/xray/daemon.pb.validate.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace XRay {

namespace {
constexpr absl::string_view XRaySerializationVersion = "1";
constexpr absl::string_view DirectionKey = "direction";

// X-Ray Trace ID Format
//
// A trace_id consists of three parts separated by hyphens.
// For example, 1-58406cf0-a006649127e371903a2de979.
// This includes:
//
// - The version number, that is, 1.
// - The time of the original request, in Unix epoch time, in 8 hexadecimal digits.
// - A 96-bit unique identifier in 24 hexadecimal digits.
//
// For more details see:
// https://docs.aws.amazon.com/xray/latest/devguide/xray-api-segmentdocuments.html#api-segmentdocuments-fields
std::string generateTraceId(SystemTime point_in_time, Random::RandomGenerator& random) {
  using std::chrono::seconds;
  using std::chrono::time_point_cast;
  // epoch in seconds represented as 8 hexadecimal characters
  const auto epoch = time_point_cast<seconds>(point_in_time).time_since_epoch().count();
  std::string uuid = random.uuid();
  // unique id represented as 24 hexadecimal digits and no dashes
  uuid.erase(std::remove(uuid.begin(), uuid.end(), '-'), uuid.end());
  ASSERT(uuid.length() >= 24);
  const std::string out =
      absl::StrCat(XRaySerializationVersion, "-", Hex::uint32ToHex(epoch), "-", uuid.substr(0, 24));
  return out;
}

} // namespace

void Span::finishSpan() {
  using std::chrono::time_point_cast;
  using namespace source::extensions::tracers::xray;
  // X-Ray expects timestamps to be in epoch seconds with milli/micro-second precision as a fraction
  using SecondsWithFraction = std::chrono::duration<double>;
  if (!sampled()) {
    return;
  }

  daemon::Segment s;
  s.set_name(name());
  s.set_id(id());
  s.set_trace_id(traceId());
  s.set_start_time(time_point_cast<SecondsWithFraction>(startTime()).time_since_epoch().count());
  s.set_end_time(
      time_point_cast<SecondsWithFraction>(time_source_.systemTime()).time_since_epoch().count());
  s.set_origin(origin());
  s.set_parent_id(parentId());
  s.set_error(clientError());
  s.set_fault(serverError());
  s.set_throttle(isThrottled());
  if (type() == Subsegment) {
    s.set_type(std::string(Subsegment));
  }
  auto* aws = s.mutable_aws()->mutable_fields();
  for (const auto& field : aws_metadata_) {
    aws->insert({field.first, field.second});
  }

  auto* request_fields = s.mutable_http()->mutable_request()->mutable_fields();
  for (const auto& field : http_request_annotations_) {
    request_fields->insert({field.first, field.second});
  }

  auto* response_fields = s.mutable_http()->mutable_response()->mutable_fields();
  for (const auto& field : http_response_annotations_) {
    response_fields->insert({field.first, field.second});
  }

  for (const auto& item : custom_annotations_) {
    s.mutable_annotations()->insert({item.first, item.second});
  }
  // `direction` will be either "ingress" or "egress"
  s.mutable_annotations()->insert({std::string(DirectionKey), direction()});

  const std::string json = MessageUtil::getJsonStringFromMessageOrError(
      s, false /* pretty_print  */, false /* always_print_primitive_fields */);

  broker_.send(json);
} // namespace XRay

const Tracing::TraceContextHandler& xRayTraceHeader() {
  CONSTRUCT_ON_FIRST_USE(Tracing::TraceContextHandler, "x-amzn-trace-id");
}

const Tracing::TraceContextHandler& xForwardedForHeader() {
  CONSTRUCT_ON_FIRST_USE(Tracing::TraceContextHandler, "x-forwarded-for");
}

void Span::injectContext(Tracing::TraceContext& trace_context, const Tracing::UpstreamContext&) {
  const std::string xray_header_value =
      fmt::format("Root={};Parent={};Sampled={}", traceId(), id(), sampled() ? "1" : "0");
  xRayTraceHeader().setRefKey(trace_context, xray_header_value);
}

Tracing::SpanPtr Span::spawnChild(const Tracing::Config& config, const std::string& operation_name,
                                  Envoy::SystemTime start_time) {
  auto child_span = std::make_unique<XRay::Span>(time_source_, random_, broker_);
  child_span->setName(operation_name);
  child_span->setOperation(operation_name);
  child_span->setDirection(Tracing::TracerUtility::toString(config.operationName()));
  child_span->setStartTime(start_time);
  child_span->setParentId(id());
  child_span->setTraceId(traceId());
  child_span->setSampled(sampled());
  child_span->setType(Subsegment);
  return child_span;
}

Tracing::SpanPtr Tracer::startSpan(const Tracing::Config& config, const std::string& operation_name,
                                   Envoy::SystemTime start_time,
                                   const absl::optional<XRayHeader>& xray_header,
                                   const absl::optional<absl::string_view> client_ip) {

  auto span_ptr = std::make_unique<XRay::Span>(time_source_, random_, *daemon_broker_);
  span_ptr->setName(segment_name_);
  span_ptr->setOperation(operation_name);
  span_ptr->setDirection(Tracing::TracerUtility::toString(config.operationName()));
  // Even though we have a TimeSource member in the tracer, we assume the start_time argument has a
  // more precise value than calling the systemTime() at this point in time.
  span_ptr->setStartTime(start_time);
  span_ptr->setOrigin(origin_);
  span_ptr->setAwsMetadata(aws_metadata_);
  if (client_ip) {
    span_ptr->addToHttpRequestAnnotations(SpanClientIp,
                                          ValueUtil::stringValue(std::string(*client_ip)));
    // The `client_ip` is the address specified in the HTTP X-Forwarded-For header.
    span_ptr->addToHttpRequestAnnotations(SpanXForwardedFor, ValueUtil::boolValue(true));
  }

  if (xray_header) {
    // There's a previous span that this span should be based-on.
    span_ptr->setParentId(xray_header->parent_id_);
    span_ptr->setTraceId(xray_header->trace_id_);
    switch (xray_header->sample_decision_) {
    case SamplingDecision::Sampled:
      span_ptr->setSampled(true);
      break;
    case SamplingDecision::NotSampled:
      // should never get here. If the header has Sampled=0 then we never call startSpan().
      IS_ENVOY_BUG("unexpected code path hit");
    default:
      break;
    }
  } else {
    span_ptr->setTraceId(generateTraceId(time_source_.systemTime(), random_));
  }
  return span_ptr;
}

XRay::SpanPtr Tracer::createNonSampledSpan(const absl::optional<XRayHeader>& xray_header) const {
  auto span_ptr = std::make_unique<XRay::Span>(time_source_, random_, *daemon_broker_);
  if (xray_header) {
    // There's a previous span that this span should be based-on.
    span_ptr->setParentId(xray_header->parent_id_);
    span_ptr->setTraceId(xray_header->trace_id_);
  } else {
    span_ptr->setTraceId(generateTraceId(time_source_.systemTime(), random_));
  }
  span_ptr->setSampled(false);
  return span_ptr;
}

void Span::setTag(absl::string_view name, absl::string_view value) {
  // For the full set of values see:
  // https://docs.aws.amazon.com/xray/latest/devguide/xray-api-segmentdocuments.html#api-segmentdocuments-http
  constexpr auto SpanContentLength = "content_length";
  constexpr auto SpanMethod = "method";
  constexpr auto SpanUrl = "url";

  if (name.empty() || value.empty()) {
    return;
  }

  if (name == Tracing::Tags::get().HttpUrl) {
    addToHttpRequestAnnotations(SpanUrl, ValueUtil::stringValue(std::string(value)));
  } else if (name == Tracing::Tags::get().HttpMethod) {
    addToHttpRequestAnnotations(SpanMethod, ValueUtil::stringValue(std::string(value)));
  } else if (name == Tracing::Tags::get().UserAgent) {
    addToHttpRequestAnnotations(Tracing::Tags::get().UserAgent,
                                ValueUtil::stringValue(std::string(value)));
  } else if (name == Tracing::Tags::get().HttpStatusCode) {
    uint64_t status_code;
    if (!absl::SimpleAtoi(value, &status_code)) {
      ENVOY_LOG(debug, "{} must be a number, given: {}", Tracing::Tags::get().HttpStatusCode,
                value);
      return;
    }
    setResponseStatusCode(status_code);
    addToHttpResponseAnnotations(Tracing::Tags::get().Status, ValueUtil::numberValue(status_code));
  } else if (name == Tracing::Tags::get().ResponseSize) {
    uint64_t response_size;
    if (!absl::SimpleAtoi(value, &response_size)) {
      ENVOY_LOG(debug, "{} must be a number, given: {}", Tracing::Tags::get().ResponseSize, value);
      return;
    }
    addToHttpResponseAnnotations(SpanContentLength, ValueUtil::numberValue(response_size));
  } else if (name == Tracing::Tags::get().PeerAddress) {
    // Use PeerAddress if client_ip is not already set from the header.
    if (!hasKeyInHttpRequestAnnotations(SpanClientIp)) {
      addToHttpRequestAnnotations(SpanClientIp, ValueUtil::stringValue(std::string(value)));
      // In this case, PeerAddress refers to the client's actual IP address, not
      // the address specified in the HTTP X-Forwarded-For header.
      addToHttpRequestAnnotations(SpanXForwardedFor, ValueUtil::boolValue(false));
    }
  } else if (name == Tracing::Tags::get().Error && value == Tracing::Tags::get().True) {
    setServerError();
  } else {
    custom_annotations_.emplace(name, value);
  }
}

} // namespace XRay
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
