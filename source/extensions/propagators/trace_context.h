#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>

#include "absl/strings/string_view.h"
#include "absl/types/optional.h"

namespace Envoy {
namespace Extensions {
namespace Propagators {

/**
 * Generic trace identifier that is tracer-agnostic.
 * Represents a unique identifier for a trace, typically 128-bit (32 hex chars).
 */
class TraceId {
public:
  TraceId() = default;
  explicit TraceId(absl::string_view hex_string);

  /**
   * @return true if this is a valid (non-zero) trace ID.
   */
  bool isValid() const;

  /**
   * @return the trace ID as a hex string (lowercase).
   */
  const std::string& toHex() const { return hex_string_; }

  /**
   * @return the trace ID as raw bytes.
   */
  const std::vector<uint8_t>& toBytes() const { return bytes_; }

  bool operator==(const TraceId& other) const { return hex_string_ == other.hex_string_; }
  bool operator!=(const TraceId& other) const { return !(*this == other); }

private:
  std::string hex_string_;
  std::vector<uint8_t> bytes_;
};

/**
 * Generic span identifier that is tracer-agnostic.
 * Represents a unique identifier for a span, typically 64-bit (16 hex chars).
 */
class SpanId {
public:
  SpanId() = default;
  explicit SpanId(absl::string_view hex_string);

  /**
   * @return true if this is a valid (non-zero) span ID.
   */
  bool isValid() const;

  /**
   * @return the span ID as a hex string (lowercase).
   */
  const std::string& toHex() const { return hex_string_; }

  /**
   * @return the span ID as raw bytes.
   */
  const std::vector<uint8_t>& toBytes() const { return bytes_; }

  bool operator==(const SpanId& other) const { return hex_string_ == other.hex_string_; }
  bool operator!=(const SpanId& other) const { return !(*this == other); }

private:
  std::string hex_string_;
  std::vector<uint8_t> bytes_;
};

/**
 * Generic trace flags that are tracer-agnostic.
 * Represents sampling decisions, debug flags, and other trace-level metadata.
 */
class TraceFlags {
public:
  TraceFlags() = default;
  explicit TraceFlags(uint8_t flags);

  /**
   * @return true if the sampled flag is set.
   */
  bool sampled() const { return (flags_ & SAMPLED_FLAG) != 0; }

  /**
   * Set or clear the sampled flag.
   */
  void setSampled(bool sampled);

  /**
   * @return the raw flags value.
   */
  uint8_t value() const { return flags_; }

  bool operator==(const TraceFlags& other) const { return flags_ == other.flags_; }
  bool operator!=(const TraceFlags& other) const { return !(*this == other); }

private:
  static constexpr uint8_t SAMPLED_FLAG = 0x01;
  uint8_t flags_{0};
};

/**
 * Generic span context that is tracer-agnostic.
 * Contains the core information needed for trace propagation across services.
 */
class SpanContext {
public:
  SpanContext() = default;

  SpanContext(TraceId trace_id, SpanId span_id, TraceFlags trace_flags,
              absl::optional<SpanId> parent_span_id = absl::nullopt, std::string tracestate = "");

  /**
   * @return true if this span context is valid (has valid trace and span IDs).
   */
  bool isValid() const;

  /**
   * @return the trace ID.
   */
  const TraceId& traceId() const { return trace_id_; }

  /**
   * @return the span ID.
   */
  const SpanId& spanId() const { return span_id_; }

  /**
   * @return the trace flags.
   */
  const TraceFlags& traceFlags() const { return trace_flags_; }

  /**
   * @return the parent span ID if present.
   */
  const absl::optional<SpanId>& parentSpanId() const { return parent_span_id_; }

  /**
   * @return the tracestate for W3C propagation.
   */
  const std::string& tracestate() const { return tracestate_; }

  /**
   * @return true if this span context indicates sampling.
   */
  bool sampled() const { return trace_flags_.sampled(); }

private:
  TraceId trace_id_;
  SpanId span_id_;
  TraceFlags trace_flags_;
  absl::optional<SpanId> parent_span_id_;
  std::string tracestate_;
};

/**
 * Generic baggage implementation for W3C Baggage propagation.
 * Represents key-value pairs that are propagated across service boundaries.
 */
class Baggage {
public:
  using BaggageMap = std::unordered_map<std::string, std::string>;

  Baggage() = default;
  explicit Baggage(const BaggageMap& entries);

  /**
   * Add a baggage entry.
   */
  void set(const std::string& key, const std::string& value);

  /**
   * Get a baggage entry.
   */
  absl::optional<std::string> get(const std::string& key) const;

  /**
   * Remove a baggage entry.
   */
  void remove(const std::string& key);

  /**
   * @return all baggage entries.
   */
  const BaggageMap& entries() const { return entries_; }

  /**
   * @return true if baggage is empty.
   */
  bool empty() const { return entries_.empty(); }

private:
  BaggageMap entries_;
};

} // namespace Propagators
} // namespace Extensions
} // namespace Envoy

// Utility functions for propagator implementations
namespace Envoy {
namespace Extensions {
namespace Propagators {

/**
 * Parse B3 sampling state to boolean according to B3 specification.
 * Handles "0" (not sampled), "1" (sampled), "d" (debug/sampled).
 * @param sampling_state The B3 sampling state value.
 * @return Optional boolean indicating sampling decision, nullopt if invalid.
 */
inline absl::optional<bool> parseB3SamplingState(absl::string_view sampling_state) {
  if (sampling_state == "0") {
    return false;
  } else if (sampling_state == "1" || sampling_state == "d") {
    return true;
  }
  return absl::nullopt;
}

} // namespace Propagators
} // namespace Extensions
} // namespace Envoy
