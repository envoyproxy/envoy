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
  // The input can be Base64 string with or without padding.
  std::string result = Base64::decodeWithoutPadding(input);
  if (result.empty()) {
    throw EnvoyException("Invalid propagation header for SkyWalking: parse error");
  }
  return result;
}

} // namespace

SpanContextPtr SpanContext::spanContextFromRequest(Http::RequestHeaderMap& headers) {
  auto propagation_header = headers.get(propagationHeader());
  if (propagation_header.empty()) {
    // No propagation header then Envoy is first hop.
    return nullptr;
  }

  auto header_value_string = propagation_header[0]->value().getStringView();
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

  // Some detailed log for debugging.
  ENVOY_LOG(trace, "{} and create new SkyWalking segment:",
            previous_span_context_ ? "Has previous span context" : "No previous span context");

  ENVOY_LOG(trace, "    Trace ID: {}", trace_id_);
  ENVOY_LOG(trace, "    Segment ID: {}", trace_segment_id_);
  ENVOY_LOG(trace, "    Sampled: {}", sampled_);
}

SpanStore* SegmentContext::createSpanStore(const SpanStore* parent_span_store) {
  ENVOY_LOG(trace, "Create new SpanStore object for current segment: {}", trace_segment_id_);
  SpanStorePtr new_span_store = std::make_unique<SpanStore>(this);
  new_span_store->setSpanId(span_list_.size());
  if (!parent_span_store) {
    // The parent SpanStore object does not exist. Create the root SpanStore object in the current
    // segment.
    new_span_store->setSampled(sampled_);
    new_span_store->setParentSpanId(-1);
    // First span of current segment for Envoy Proxy must be a Entry Span. It is created for
    // downstream HTTP request.
    new_span_store->setAsEntrySpan(true);
  } else {
    // Create child SpanStore object.
    new_span_store->setSampled(parent_span_store->sampled());
    new_span_store->setParentSpanId(parent_span_store->spanId());
    new_span_store->setAsEntrySpan(false);
  }
  SpanStore* ref = new_span_store.get();
  span_list_.emplace_back(std::move(new_span_store));
  return ref;
}

void SpanStore::injectContext(Http::RequestHeaderMap& request_headers) const {
  ASSERT(segment_context_);

  // For SkyWalking Entry Span, Envoy does not need to inject tracing context into the request
  // headers.
  if (is_entry_span_) {
    ENVOY_LOG(debug, "Skip tracing context injection for SkyWalking Entry Span");
    return;
  }

  ENVOY_LOG(debug, "Inject or update SkyWalking propagation header in upstream request headers");
  const_cast<SpanStore*>(this)->setPeerAddress(std::string(request_headers.getHostValue()));

  ENVOY_LOG(trace, "'sw8' header: '({}) - ({}) - ({}) - ({}) - ({}) - ({}) - ({}) - ({})'",
            sampled_, segment_context_->traceId(), segment_context_->traceSegmentId(), span_id_,
            segment_context_->service(), segment_context_->serviceInstance(),
            segment_context_->rootSpanStore()->operation(), peer_address_);

  // Reference:
  // https://github.com/apache/skywalking/blob/v8.1.0/docs/en/protocols/Skywalking-Cross-Process-Propagation-Headers-Protocol-v3.md.
  const auto value = absl::StrCat(sampled_, "-", base64Encode(segment_context_->traceId()), "-",
                                  base64Encode(segment_context_->traceSegmentId()), "-", span_id_,
                                  "-", base64Encode(segment_context_->service()), "-",
                                  base64Encode(segment_context_->serviceInstance()), "-",
                                  base64Encode(segment_context_->rootSpanStore()->operation()), "-",
                                  base64Encode(peer_address_));
  request_headers.setReferenceKey(propagationHeader(), value);
}

} // namespace SkyWalking
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
