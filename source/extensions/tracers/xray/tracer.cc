#include "extensions/tracers/xray/tracer.h"

#include <algorithm>
#include <chrono>
#include <string>

#include "envoy/http/header_map.h"
#include "envoy/network/listener.h"

#include "common/common/assert.h"
#include "common/common/fmt.h"
#include "common/protobuf/utility.h"

#include "source/extensions/tracers/xray/daemon.pb.validate.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace XRay {

namespace {
constexpr auto XRaySerializationVersion = "1";

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
  const auto epoch = time_point_cast<seconds>(point_in_time).time_since_epoch().count();
  std::string out;
  out.reserve(35);
  out += XRaySerializationVersion;
  out.push_back('-');
  // epoch in seconds represented as 8 hexadecimal characters
  out += Hex::uint32ToHex(epoch);
  out.push_back('-');
  std::string uuid = random.uuid();
  // unique id represented as 24 hexadecimal digits and no dashes
  uuid.erase(std::remove(uuid.begin(), uuid.end(), '-'), uuid.end());
  ASSERT(uuid.length() >= 24);
  out += uuid.substr(0, 24);
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

  const std::string json = MessageUtil::getJsonStringFromMessageOrDie(
      s, false /* pretty_print  */, false /* always_print_primitive_fields */);

  broker_.send(json);
} // namespace XRay

void Span::injectContext(Http::RequestHeaderMap& request_headers) {
  const std::string xray_header_value =
      fmt::format("Root={};Parent={};Sampled={}", traceId(), id(), sampled() ? "1" : "0");
  request_headers.setCopy(Http::LowerCaseString(XRayTraceHeader), xray_header_value);
}

Tracing::SpanPtr Span::spawnChild(const Tracing::Config&, const std::string& operation_name,
                                  Envoy::SystemTime start_time) {
  auto child_span = std::make_unique<XRay::Span>(time_source_, random_, broker_);
  child_span->setName(name());
  child_span->setOperation(operation_name);
  child_span->setStartTime(start_time);
  child_span->setParentId(id());
  child_span->setTraceId(traceId());
  child_span->setSampled(sampled());
  return child_span;
}

Tracing::SpanPtr Tracer::startSpan(const std::string& operation_name, Envoy::SystemTime start_time,
                                   const absl::optional<XRayHeader>& xray_header) {

  auto span_ptr = std::make_unique<XRay::Span>(time_source_, random_, *daemon_broker_);
  span_ptr->setName(segment_name_);
  span_ptr->setOperation(operation_name);
  // Even though we have a TimeSource member in the tracer, we assume the start_time argument has a
  // more precise value than calling the systemTime() at this point in time.
  span_ptr->setStartTime(start_time);
  span_ptr->setOrigin(origin_);
  span_ptr->setAwsMetadata(aws_metadata_);

  if (xray_header) { // there's a previous span that this span should be based-on
    span_ptr->setParentId(xray_header->parent_id_);
    span_ptr->setTraceId(xray_header->trace_id_);
    switch (xray_header->sample_decision_) {
    case SamplingDecision::Sampled:
      span_ptr->setSampled(true);
      break;
    case SamplingDecision::NotSampled:
      // should never get here. If the header has Sampled=0 then we never call startSpan().
      NOT_REACHED_GCOVR_EXCL_LINE;
    default:
      break;
    }
  } else {
    span_ptr->setTraceId(generateTraceId(time_source_.systemTime(), random_));
  }
  return span_ptr;
}

XRay::SpanPtr Tracer::createNonSampledSpan() const {
  auto span_ptr = std::make_unique<XRay::Span>(time_source_, random_, *daemon_broker_);
  span_ptr->setName(segment_name_);
  span_ptr->setOrigin(origin_);
  span_ptr->setTraceId(generateTraceId(time_source_.systemTime(), random_));
  span_ptr->setAwsMetadata(aws_metadata_);
  span_ptr->setSampled(false);
  return span_ptr;
}

void Span::setTag(absl::string_view name, absl::string_view value) {
  // For the full set of values see:
  // https://docs.aws.amazon.com/xray/latest/devguide/xray-api-segmentdocuments.html#api-segmentdocuments-http
  constexpr auto SpanContentLength = "content_length";
  constexpr auto SpanMethod = "method";
  constexpr auto SpanStatus = "status";
  constexpr auto SpanUserAgent = "user_agent";
  constexpr auto SpanUrl = "url";
  constexpr auto SpanClientIp = "client_ip";
  constexpr auto SpanXForwardedFor = "x_forwarded_for";

  constexpr auto HttpUrl = "http.url";
  constexpr auto HttpMethod = "http.method";
  constexpr auto HttpStatusCode = "http.status_code";
  constexpr auto HttpUserAgent = "user_agent";
  constexpr auto HttpResponseSize = "response_size";
  constexpr auto PeerAddress = "peer.address";

  if (name.empty() || value.empty()) {
    return;
  }

  if (name == HttpUrl) {
    http_request_annotations_.emplace(SpanUrl, ValueUtil::stringValue(std::string(value)));
  } else if (name == HttpMethod) {
    http_request_annotations_.emplace(SpanMethod, ValueUtil::stringValue(std::string(value)));
  } else if (name == HttpUserAgent) {
    http_request_annotations_.emplace(SpanUserAgent, ValueUtil::stringValue(std::string(value)));
  } else if (name == HttpStatusCode) {
    uint64_t status_code;
    if (!absl::SimpleAtoi(value, &status_code)) {
      ENVOY_LOG(debug, "{} must be a number, given: {}", HttpStatusCode, value);
      return;
    }
    http_response_annotations_.emplace(SpanStatus, ValueUtil::numberValue(status_code));
  } else if (name == HttpResponseSize) {
    uint64_t response_size;
    if (!absl::SimpleAtoi(value, &response_size)) {
      ENVOY_LOG(debug, "{} must be a number, given: {}", HttpResponseSize, value);
      return;
    }
    http_response_annotations_.emplace(SpanContentLength, ValueUtil::numberValue(response_size));
  } else if (name == PeerAddress) {
    http_request_annotations_.emplace(SpanClientIp, ValueUtil::stringValue(std::string(value)));
    // In this case, PeerAddress refers to the client's actual IP address, not
    // the address specified in the HTTP X-Forwarded-For header.
    http_request_annotations_.emplace(SpanXForwardedFor, ValueUtil::boolValue(false));
  } else {
    custom_annotations_.emplace(name, value);
  }
}

} // namespace XRay
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
