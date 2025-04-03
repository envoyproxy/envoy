#include "source/extensions/tracers/fluentd/fluentd_tracer_impl.h"

#include <cstdint>

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/backoff_strategy.h"
#include "source/common/common/hex.h"
#include "source/common/tracing/trace_context_impl.h"

#include "msgpack.hpp"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace Fluentd {

// Handle Span and Trace context extraction and validation
// Adapted from OpenTelemetry tracer extension @alexanderellis @yanavlasov
// See https://www.w3.org/TR/trace-context/#traceparent-header
constexpr int kTraceparentHeaderSize = 55; // 2 + 1 + 32 + 1 + 16 + 1 + 2
constexpr int kVersionHexSize = 2;
constexpr int kTraceIdHexSize = 32;
constexpr int kParentIdHexSize = 16;
constexpr int kTraceFlagsHexSize = 2;

bool isValidHex(const absl::string_view& input) {
  return std::all_of(input.begin(), input.end(),
                     [](const char& c) { return absl::ascii_isxdigit(c); });
}

bool isAllZeros(const absl::string_view& input) {
  return std::all_of(input.begin(), input.end(), [](const char& c) { return c == '0'; });
}

SpanContextExtractor::SpanContextExtractor(Tracing::TraceContext& trace_context)
    : trace_context_(trace_context) {}

SpanContextExtractor::~SpanContextExtractor() = default;

bool SpanContextExtractor::propagationHeaderPresent() {
  auto propagation_header = FluentdConstants::get().TRACE_PARENT.get(trace_context_);
  return propagation_header.has_value();
}

absl::StatusOr<SpanContext> SpanContextExtractor::extractSpanContext() {
  auto propagation_header = FluentdConstants::get().TRACE_PARENT.get(trace_context_);
  if (!propagation_header.has_value()) {
    // We should have already caught this, but just in case.
    return absl::InvalidArgumentError("No propagation header found");
  }
  auto header_value_string = propagation_header.value();

  if (header_value_string.size() != kTraceparentHeaderSize) {
    return absl::InvalidArgumentError("Invalid traceparent header length");
  }
  // Try to split it into its component parts:
  std::vector<absl::string_view> propagation_header_components =
      absl::StrSplit(header_value_string, '-', absl::SkipEmpty());
  if (propagation_header_components.size() != 4) {
    return absl::InvalidArgumentError("Invalid traceparent hyphenation");
  }
  absl::string_view version = propagation_header_components[0];
  absl::string_view trace_id = propagation_header_components[1];
  absl::string_view parent_id = propagation_header_components[2];
  absl::string_view trace_flags = propagation_header_components[3];
  if (version.size() != kVersionHexSize || trace_id.size() != kTraceIdHexSize ||
      parent_id.size() != kParentIdHexSize || trace_flags.size() != kTraceFlagsHexSize) {
    return absl::InvalidArgumentError("Invalid traceparent field sizes");
  }
  if (!isValidHex(version) || !isValidHex(trace_id) || !isValidHex(parent_id) ||
      !isValidHex(trace_flags)) {
    return absl::InvalidArgumentError("Invalid header hex");
  }
  // As per the traceparent header definition, if the trace-id or parent-id are all zeros, they are
  // invalid and must be ignored.
  if (isAllZeros(trace_id)) {
    return absl::InvalidArgumentError("Invalid trace id");
  }
  if (isAllZeros(parent_id)) {
    return absl::InvalidArgumentError("Invalid parent id");
  }

  // Set whether or not the span is sampled from the trace flags.
  // See https://w3c.github.io/trace-context/#trace-flags.
  char decoded_trace_flags = absl::HexStringToBytes(trace_flags).front();
  bool sampled = (decoded_trace_flags & 1);

  // If a tracestate header is received without an accompanying traceparent header,
  // it is invalid and MUST be discarded. Because we're already checking for the
  // traceparent header above, we don't need to check here.
  // See https://www.w3.org/TR/trace-context/#processing-model-for-working-with-trace-context
  absl::string_view tracestate_key = FluentdConstants::get().TRACE_STATE.key();
  std::vector<std::string> tracestate_values;
  // Multiple tracestate header fields MUST be handled as specified by RFC7230 Section 3.2.2 Field
  // Order.
  trace_context_.forEach(
      [&tracestate_key, &tracestate_values](absl::string_view key, absl::string_view value) {
        if (key == tracestate_key) {
          tracestate_values.push_back(std::string{value});
        }
        return true;
      });
  std::string tracestate = absl::StrJoin(tracestate_values, ",");

  SpanContext span_context(version, trace_id, parent_id, sampled, tracestate);
  return span_context;
}

// Define default version and trace context construction// Define default version and trace context
// construction
constexpr absl::string_view kDefaultVersion = "00";

const Tracing::TraceContextHandler& traceParentHeader() {
  CONSTRUCT_ON_FIRST_USE(Tracing::TraceContextHandler, "traceparent");
}

const Tracing::TraceContextHandler& traceStateHeader() {
  CONSTRUCT_ON_FIRST_USE(Tracing::TraceContextHandler, "tracestate");
}

// Initialize the Fluentd driver
Driver::Driver(const FluentdConfigSharedPtr fluentd_config,
               Server::Configuration::TracerFactoryContext& context,
               FluentdTracerCacheSharedPtr tracer_cache)
    : tls_slot_(context.serverFactoryContext().threadLocal().allocateSlot()),
      fluentd_config_(fluentd_config), tracer_cache_(tracer_cache) {
  Random::RandomGenerator& random = context.serverFactoryContext().api().randomGenerator();

  uint64_t base_interval_ms = DefaultBaseBackoffIntervalMs;
  uint64_t max_interval_ms = base_interval_ms * DefaultMaxBackoffIntervalFactor;

  if (fluentd_config->has_retry_policy() && fluentd_config->retry_policy().has_retry_back_off()) {
    base_interval_ms = PROTOBUF_GET_MS_OR_DEFAULT(fluentd_config->retry_policy().retry_back_off(),
                                                  base_interval, DefaultBaseBackoffIntervalMs);
    max_interval_ms =
        PROTOBUF_GET_MS_OR_DEFAULT(fluentd_config->retry_policy().retry_back_off(), max_interval,
                                   base_interval_ms * DefaultMaxBackoffIntervalFactor);
  }

  // Create a thread local tracer
  tls_slot_->set([fluentd_config = fluentd_config_, &random, tracer_cache = tracer_cache_,
                  base_interval_ms, max_interval_ms](Event::Dispatcher&) {
    BackOffStrategyPtr backoff_strategy = std::make_unique<JitteredExponentialBackOffStrategy>(
        base_interval_ms, max_interval_ms, random);
    return std::make_shared<ThreadLocalTracer>(
        tracer_cache->getOrCreate(fluentd_config, random, std::move(backoff_strategy)));
  });
}

// Handles driver logic for starting a new span
Tracing::SpanPtr Driver::startSpan(const Tracing::Config& /*config*/,
                                   Tracing::TraceContext& trace_context,
                                   const StreamInfo::StreamInfo& stream_info,
                                   const std::string& operation_name,
                                   Tracing::Decision tracing_decision) {
  // Get the thread local tracer
  auto& tracer = tls_slot_->getTyped<ThreadLocalTracer>().tracer();

  // Decide which tracer.startSpan function to call based on available span context
  SpanContextExtractor extractor(trace_context);
  if (!extractor.propagationHeaderPresent()) {
    // No propagation header, so we can create a fresh span with the given decision.

    return tracer.startSpan(trace_context, stream_info.startTime(), operation_name,
                            tracing_decision);
  } else {
    // Try to extract the span context. If we can't, just return a null span.
    absl::StatusOr<SpanContext> span_context = extractor.extractSpanContext();
    if (span_context.ok()) {

      return tracer.startSpan(trace_context, stream_info.startTime(), operation_name,
                              tracing_decision, span_context.value());

    } else {
      ENVOY_LOG(trace, "Unable to extract span context: ", span_context.status());
      return std::make_unique<Tracing::NullSpan>();
    }
  }
}

// Initialize the Fluentd tracer
FluentdTracerImpl::FluentdTracerImpl(Upstream::ThreadLocalCluster& cluster,
                                     Tcp::AsyncTcpClientPtr client, Event::Dispatcher& dispatcher,
                                     const FluentdConfig& config,
                                     BackOffStrategyPtr backoff_strategy,
                                     Stats::Scope& parent_scope, Random::RandomGenerator& random)
    : FluentdBase(
          cluster, std::move(client), dispatcher, config.tag(),
          config.has_retry_policy() && config.retry_policy().has_num_retries()
              ? absl::optional<uint32_t>(config.retry_policy().num_retries().value())
              : absl::nullopt,
          parent_scope, config.stat_prefix(), std::move(backoff_strategy),
          PROTOBUF_GET_MS_OR_DEFAULT(config, buffer_flush_interval, DefaultBufferFlushIntervalMs),
          PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, buffer_size_bytes, DefaultMaxBufferSize)),
      option_({{"fluent_signal", "2"}, {"TimeFormat", "DateTime"}}), random_(random),
      time_source_(dispatcher.timeSource()) {}

// Initialize a span object
Span::Span(Tracing::TraceContext& trace_context, SystemTime start_time,
           const std::string& operation_name, Tracing::Decision tracing_decision,
           FluentdTracerSharedPtr tracer, const SpanContext& span_context, TimeSource& time_source)
    : trace_context_(trace_context), start_time_(start_time), operation_(operation_name),
      tracing_decision_(tracing_decision), tracer_(tracer), span_context_(span_context),
      time_source_(time_source) {}

// Set the operation name for the span
void Span::setOperation(absl::string_view operation) { operation_ = std::string(operation); }

// Adds a tag to the span
void Span::setTag(absl::string_view name, absl::string_view value) {
  tags_[std::string(name)] = std::string(value);
}

// Log an event as a Fluentd entry
void Span::log(SystemTime /*timestamp*/, const std::string& event) {
  uint64_t time =
      std::chrono::duration_cast<std::chrono::seconds>(time_source_.systemTime().time_since_epoch())
          .count();

  EntryPtr entry =
      std::make_unique<Entry>(time, std::map<std::string, std::string>{{"event", event}});

  tracer_->log(std::move(entry));
}

// Finish and log a span as a Fluentd entry
void Span::finishSpan() {
  uint64_t time =
      std::chrono::duration_cast<std::chrono::seconds>(time_source_.systemTime().time_since_epoch())
          .count();

  // Make the record map
  std::map<std::string, std::string> record_map;
  record_map["operation"] = operation_;
  record_map["trace_id"] = span_context_.traceId();
  record_map["span_id"] = span_context_.parentId();
  record_map["start_time"] = std::to_string(
      std::chrono::duration_cast<std::chrono::seconds>(start_time_.time_since_epoch()).count());
  record_map["end_time"] = std::to_string(time);

  // Add the tags to the record map
  for (const auto& tag : tags_) {
    record_map[tag.first] = tag.second;
  }

  EntryPtr entry = std::make_unique<Entry>(time, std::move(record_map));

  tracer_->log(std::move(entry));
}

// Inject the span context into the trace context
void Span::injectContext(Tracing::TraceContext& trace_context,
                         const Tracing::UpstreamContext& /*upstream*/) {

  std::string trace_id_hex = span_context_.traceId();
  std::string parent_id_hex = span_context_.parentId();
  std::vector<uint8_t> trace_flags_vec{sampled()};
  std::string trace_flags_hex = Hex::encode(trace_flags_vec);
  std::string traceparent_header_value =
      absl::StrCat(kDefaultVersion, "-", trace_id_hex, "-", parent_id_hex, "-", trace_flags_hex);

  // Set the traceparent in the trace_context.
  traceParentHeader().setRefKey(trace_context, traceparent_header_value);
  // Also set the tracestate.
  traceStateHeader().setRefKey(trace_context, span_context_.tracestate());
}

// Spawns a child span
Tracing::SpanPtr Span::spawnChild(const Tracing::Config&, const std::string& name,
                                  SystemTime start_time) {
  SpanContext span_context =
      SpanContext(kDefaultVersion, span_context_.traceId(), span_context_.parentId(), sampled(),
                  span_context_.tracestate());
  return tracer_->startSpan(trace_context_, start_time, name, tracing_decision_, span_context);
}

// Set the sampled flag for the span
void Span::setSampled(bool sampled) { sampled_ = sampled; }

std::string Span::getBaggage(absl::string_view /*key*/) {
  // not implemented
  return EMPTY_STRING;
}

void Span::setBaggage(absl::string_view /*key*/, absl::string_view /*value*/) {
  // not implemented
}

std::string Span::getTraceId() const { return span_context_.traceId(); }

std::string Span::getSpanId() const { return span_context_.parentId(); }

// Start a new span with no parent context
Tracing::SpanPtr FluentdTracerImpl::startSpan(Tracing::TraceContext& trace_context,
                                              SystemTime start_time,
                                              const std::string& operation_name,
                                              Tracing::Decision tracing_decision) {
  // make a new span context
  uint64_t trace_id_high = random_.random();
  uint64_t trace_id = random_.random();
  uint64_t span_id = random_.random();

  SpanContext span_context = SpanContext(
      kDefaultVersion, absl::StrCat(Hex::uint64ToHex(trace_id_high), Hex::uint64ToHex(trace_id)),
      Hex::uint64ToHex(span_id), tracing_decision.traced, "");

  Span new_span(trace_context, start_time, operation_name, tracing_decision, shared_from_this(),
                span_context, time_source_);

  new_span.setSampled(tracing_decision.traced);

  return std::make_unique<Span>(new_span);
}

// Start a new span with a parent context
Tracing::SpanPtr FluentdTracerImpl::startSpan(Tracing::TraceContext& trace_context,
                                              SystemTime start_time,
                                              const std::string& operation_name,
                                              Tracing::Decision tracing_decision,
                                              const SpanContext& previous_span_context) {
  SpanContext span_context = SpanContext(
      kDefaultVersion, previous_span_context.traceId(), Hex::uint64ToHex(random_.random()),
      previous_span_context.sampled(), previous_span_context.tracestate());

  Span new_span(trace_context, start_time, operation_name, tracing_decision, shared_from_this(),
                span_context, time_source_);

  new_span.setSampled(previous_span_context.sampled());

  return std::make_unique<Span>(new_span);
}

void FluentdTracerImpl::packMessage(MessagePackPacker& packer) {
  packer.pack_array(3); // 1 - tag field, 2 - entries array, 3 - options map.
  packer.pack(tag_);
  packer.pack_array(entries_.size());

  for (auto& entry : entries_) {
    packer.pack_array(2); // 1 - time, 2 - record.
    packer.pack(entry->time_);
    packer.pack_map(entry->map_record_.size());
    for (const auto& pair : entry->map_record_) {
      packer.pack(pair.first);
      packer.pack(pair.second);
    }
  }

  packer.pack(option_);
}

} // namespace Fluentd
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
