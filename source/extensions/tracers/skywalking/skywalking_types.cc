#include "extensions/tracers/skywalking/skywalking_types.h"

#include "envoy/common/exception.h"

#include "common/common/base64.h"
#include "common/common/empty_string.h"
#include "common/common/fmt.h"
#include "common/common/hex.h"
#include "common/common/utility.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace SkyWalking {

namespace {

// The standard header name is "sw8", as mentioned in:
// https://github.com/apache/skywalking/blob/v8.1.0/docs/en/protocols/Skywalking-Cross-Process-Propagation-Headers-Protocol-v3.md.
const Http::LowerCaseString& propagationHeader() {
  CONSTRUCT_ON_FIRST_USE(Http::LowerCaseString, "sw8");
}

std::string generateId(Random::RandomGenerator& random_generator) {
  return absl::StrCat(Hex::uint64ToHex(random_generator.random()),
                      Hex::uint64ToHex(random_generator.random()));
}

std::string base64Encode(const absl::string_view input) {
  return Base64::encode(input.data(), input.length());
}

// Decode and validate fields of propagation header.
std::string base64Decode(absl::string_view input) {
  std::string result = input.length() % 4 ? EMPTY_STRING : Base64::decodeWithoutPadding(input);
  if (result.empty()) {
    throw EnvoyException("Invalid propagation header for SkyWalking: parse error");
  }
  return result;
}

} // namespace

SpanContextPtr SpanContext::spanContextFromRequest(Http::RequestHeaderMap& headers) {
  auto propagation_header = headers.get(propagationHeader());
  if (propagation_header == nullptr) {
    // No propagation header then Envoy is first hop.
    return nullptr;
  }

  auto header_value_string = propagation_header->value().getStringView();
  const auto parts = StringUtil::splitToken(header_value_string, "-", false, true);
  // Reference:
  // https://github.com/apache/skywalking/blob/v8.1.0/docs/en/protocols/Skywalking-Cross-Process-Propagation-Headers-Protocol-v3.md.
  if (parts.size() != 8) {
    throw EnvoyException(
        fmt::format("Invalid propagation header for SkyWalking: {}", header_value_string));
  }

  SpanContextPtr previous_span_context = std::unique_ptr<SpanContext>(new SpanContext());

  // Parse and validate sampling flag.
  if (parts[0] == "0") {
    previous_span_context->sampled_ = 0;
  } else if (parts[0] == "1") {
    previous_span_context->sampled_ = 1;
  } else {
    throw EnvoyException(fmt::format("Invalid propagation header for SkyWalking: sampling flag can "
                                     "only be '0' or '1' but '{}' was provided",
                                     parts[0]));
  }

  // Parse trace id.
  previous_span_context->trace_id_ = base64Decode(parts[1]);
  // Parse segment id.
  previous_span_context->trace_segment_id_ = base64Decode(parts[2]);

  // Parse span id.
  if (!absl::SimpleAtoi(parts[3], &previous_span_context->span_id_)) {
    throw EnvoyException(fmt::format(
        "Invalid propagation header for SkyWalking: connot convert '{}' to valid span id",
        parts[3]));
  }

  // Parse service.
  previous_span_context->service_ = base64Decode(parts[4]);
  // Parse service instance.
  previous_span_context->service_instance_ = base64Decode(parts[5]);
  // Parse endpoint. Operation Name of the first entry span in the previous segment.
  previous_span_context->endpoint_ = base64Decode(parts[6]);
  // Parse target address used at downstream side of this request.
  previous_span_context->target_address_ = base64Decode(parts[7]);

  return previous_span_context;
}

SegmentContext::SegmentContext(SpanContextPtr&& previous_span_context, Tracing::Decision decision,
                               Random::RandomGenerator& random_generator)
    : previous_span_context_(std::move(previous_span_context)) {

  if (previous_span_context_) {
    trace_id_ = previous_span_context_->trace_id_;
    sampled_ = previous_span_context_->sampled_;
  } else {
    trace_id_ = generateId(random_generator);
    sampled_ = decision.traced;
  }
  trace_segment_id_ = generateId(random_generator);
}

SpanStore* SegmentContext::createSpanStore(TimeSource& time_source, SpanStore* parent_store) {
  auto store = std::make_unique<SpanStore>(this, time_source);
  store->setSpanId(span_list_.size());
  if (parent_store) {
    // Child span.
    store->setSampled(parent_store->sampled());
    store->setParentSpanId(parent_store->spanId());
  } else {
    // First span.
    store->setSampled(sampled_);
    store->setParentSpanId(-1);
  }
  SpanStore* ref = store.get();
  span_list_.emplace_back(std::move(store));
  return ref;
}

void SpanStore::injectContext(Http::RequestHeaderMap& request_headers) const {
  ASSERT(segment_context_);

  std::string target_address =
      upstream_address_.empty() ? std::string(request_headers.getHostValue()) : upstream_address_;

  // Reference:
  // https://github.com/apache/skywalking/blob/v8.1.0/docs/en/protocols/Skywalking-Cross-Process-Propagation-Headers-Protocol-v3.md.
  const auto value =
      absl::StrCat(sampled_, "-", base64Encode(segment_context_->traceId()), "-",
                   base64Encode(segment_context_->traceSegmentId()), "-", span_id_, "-",
                   base64Encode(segment_context_->service()), "-",
                   base64Encode(segment_context_->serviceInstance()), "-",
                   base64Encode(segment_context_->endpoint()), "-", base64Encode(target_address));
  request_headers.setReferenceKey(propagationHeader(), value);
}

void SpanStore::finish() { end_time_ = DateUtil::nowToMilliseconds(time_source_); }

} // namespace SkyWalking
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
