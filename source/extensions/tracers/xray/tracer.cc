#include "extensions/tracers/xray/tracer.h"

#include <algorithm>
#include <chrono>
#include <string>

#include "envoy/http/header_map.h"
#include "envoy/network/listener.h"

#include "common/common/assert.h"
#include "common/common/fmt.h"
#include "common/common/hex.h"
#include "common/protobuf/message_validator_impl.h"
#include "common/runtime/runtime_impl.h"

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
std::string generateTraceId(SystemTime point_in_time) {
  using std::chrono::seconds;
  using std::chrono::time_point_cast;
  Runtime::RandomGeneratorImpl rng;
  const auto epoch = time_point_cast<seconds>(point_in_time).time_since_epoch().count();
  std::string out;
  out.reserve(35);
  out += XRaySerializationVersion;
  out.push_back('-');
  // epoch in seconds represented as 8 hexadecimal characters
  out += Hex::uint32ToHex(epoch);
  out.push_back('-');
  std::string uuid = rng.uuid();
  // unique id represented as 24 hexadecimal digits and no dashes
  uuid.erase(std::remove(uuid.begin(), uuid.end(), '-'), uuid.end());
  ASSERT(uuid.length() >= 24);
  out += uuid.substr(0, 24);
  return out;
}

} // namespace

void Span::setId(uint64_t id) { id_ = Hex::uint64ToHex(id); }

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
  s.set_id(Id());
  s.set_trace_id(traceId());
  s.set_start_time(time_point_cast<SecondsWithFraction>(startTime()).time_since_epoch().count());
  s.set_end_time(
      time_point_cast<SecondsWithFraction>(time_source_.systemTime()).time_since_epoch().count());
  s.set_parent_id(parentId());
  using KeyValue = Protobuf::Map<std::string, std::string>::value_type;
  for (const auto& item : custom_annotations_) {
    s.mutable_annotations()->insert(KeyValue{item.first, item.second});
  }

  for (const auto& item : http_request_annotations_) {
    s.mutable_http()->mutable_request()->insert(KeyValue{item.first, item.second});
  }

  for (const auto& item : http_response_annotations_) {
    s.mutable_http()->mutable_response()->insert(KeyValue{item.first, item.second});
  }

  // TODO(marcomagdy): test how expensive this validation is. Might be worth turning off in
  // optimized builds..
  MessageUtil::validate(s, ProtobufMessage::getStrictValidationVisitor());
  Protobuf::util::JsonPrintOptions json_options;
  json_options.preserve_proto_field_names = true;
  std::string json;
  const auto status = Protobuf::util::MessageToJsonString(s, &json, json_options);
  ASSERT(status.ok());
  broker_.send(json);
} // namespace XRay

void Span::injectContext(Http::RequestHeaderMap& request_headers) {
  const std::string xray_header_value =
      fmt::format("root={};parent={};sampled={}", traceId(), Id(), sampled() ? "1" : "0");

  // Set the XRay header into envoy header map for visibility to upstream
  request_headers.addCopy(Http::LowerCaseString(XRayTraceHeader), xray_header_value);
}

Tracing::SpanPtr Span::spawnChild(const Tracing::Config&, const std::string& operation_name,
                                  Envoy::SystemTime start_time) {
  auto child_span = std::make_unique<XRay::Span>(time_source_, broker_);
  const auto ticks = time_source_.monotonicTime().time_since_epoch().count();
  child_span->setId(ticks);
  child_span->setName(operation_name);
  child_span->setOperation(operation_name);
  child_span->setStartTime(start_time);
  child_span->setParentId(Id());
  child_span->setTraceId(traceId());
  child_span->setSampled(sampled());
  return child_span;
}

Tracing::SpanPtr Tracer::startSpan(const std::string& operation_name, Envoy::SystemTime start_time,
                                   const absl::optional<XRayHeader>& xray_header) {

  const auto ticks = time_source_.monotonicTime().time_since_epoch().count();
  auto span_ptr = std::make_unique<XRay::Span>(time_source_, *daemon_broker_);
  span_ptr->setId(ticks);
  span_ptr->setName(segment_name_);
  span_ptr->setOperation(operation_name);
  // Even though we have a TimeSource member in the tracer, we assume the start_time argument has a
  // more precise value than calling the systemTime() at this point in time.
  span_ptr->setStartTime(start_time);

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
    span_ptr->setTraceId(generateTraceId(time_source_.systemTime()));
  }
  return span_ptr;
}

XRay::SpanPtr Tracer::createNonSampledSpan() const {
  auto span_ptr = std::make_unique<XRay::Span>(time_source_, *daemon_broker_);
  const auto ticks = time_source_.monotonicTime().time_since_epoch().count();
  span_ptr->setId(ticks);
  span_ptr->setName(segment_name_);
  span_ptr->setTraceId(generateTraceId(time_source_.systemTime()));
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
    http_request_annotations_.emplace(SpanUrl, value);
  } else if (name == HttpMethod) {
    http_request_annotations_.emplace(SpanMethod, value);
  } else if (name == HttpUserAgent) {
    http_request_annotations_.emplace(SpanUserAgent, value);
  } else if (name == HttpStatusCode) {
    http_response_annotations_.emplace(SpanStatus, value);
  } else if (name == HttpResponseSize) {
    http_response_annotations_.emplace(SpanContentLength, value);
  } else if (name == PeerAddress) {
    http_request_annotations_.emplace(SpanClientIp, value);
    // In this case, PeerAddress refers to the client's actual IP address, not
    // the address specified in the the HTTP X-Forwarded-For header.
    http_request_annotations_.emplace(SpanXForwardedFor, "false");
  } else {
    custom_annotations_.emplace(name, value);
  }
}

} // namespace XRay
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
