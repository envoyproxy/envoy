#pragma once

#include <string>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"

namespace Envoy {
namespace Extensions {
namespace Propagators {
namespace W3C {

/**
 * W3C Trace Context specification constants.
 * See https://www.w3.org/TR/trace-context/
 */
namespace Constants {
// W3C traceparent header format: version-trace-id-parent-id-trace-flags
constexpr int kTraceparentHeaderSize = 55; // 2 + 1 + 32 + 1 + 16 + 1 + 2
constexpr int kVersionSize = 2;
constexpr int kTraceIdSize = 32;
constexpr int kParentIdSize = 16;
constexpr int kTraceFlagsSize = 2;

// Header names as defined in W3C specification
constexpr absl::string_view kTraceparentHeader = "traceparent";
constexpr absl::string_view kTracestateHeader = "tracestate";
constexpr absl::string_view kBaggageHeader = "baggage";

// Current version
constexpr absl::string_view kCurrentVersion = "00";

// Trace flags
constexpr uint8_t kSampledFlag = 0x01;

// W3C Baggage specification limits
// See https://www.w3.org/TR/baggage/
constexpr size_t kMaxBaggageSize = 8192;   // 8KB total size limit
constexpr size_t kMaxBaggageMembers = 180; // Practical limit to prevent abuse
constexpr size_t kMaxKeyLength = 256;
constexpr size_t kMaxValueLength = 4096;
} // namespace Constants

/**
 * Represents a W3C traceparent header value.
 * Format: version-trace-id-parent-id-trace-flags
 * See https://www.w3.org/TR/trace-context/#traceparent-header
 */
class TraceParent {
public:
  /**
   * Construct a TraceParent from parsed components.
   * @param version the version field (2 hex characters)
   * @param trace_id the trace-id field (32 hex characters)
   * @param parent_id the parent-id field (16 hex characters)
   * @param trace_flags the trace-flags field (2 hex characters)
   */
  TraceParent(absl::string_view version, absl::string_view trace_id, absl::string_view parent_id,
              absl::string_view trace_flags);

  /**
   * Parse a traceparent header value into a TraceParent object.
   * @param traceparent_value the raw traceparent header value
   * @return TraceParent object or error status if parsing fails
   */
  static absl::StatusOr<TraceParent> parse(absl::string_view traceparent_value);

  /**
   * Serialize this TraceParent to a traceparent header value.
   * @return the formatted traceparent header value
   */
  std::string toString() const;

  // Accessors following W3C terminology
  const std::string& version() const { return version_; }
  const std::string& traceId() const { return trace_id_; }
  const std::string& parentId() const { return parent_id_; }
  const std::string& traceFlags() const { return trace_flags_; }

  /**
   * Check if the sampled flag is set.
   * @return true if the trace is sampled
   */
  bool isSampled() const;

  /**
   * Set the sampled flag.
   * @param sampled whether the trace should be sampled
   */
  void setSampled(bool sampled);

private:
  std::string version_;
  std::string trace_id_;
  std::string parent_id_;
  std::string trace_flags_;

  // Validation helpers
  static bool isValidHex(absl::string_view input);
  static bool isAllZeros(absl::string_view input);
};

/**
 * Represents a W3C tracestate header value.
 * Contains vendor-specific trace information as key-value pairs.
 * See https://www.w3.org/TR/trace-context/#tracestate-header
 */
class TraceState {
public:
  /**
   * Construct an empty TraceState.
   */
  TraceState() = default;

  /**
   * Construct a TraceState from a tracestate header value.
   * @param tracestate_value the raw tracestate header value
   */
  explicit TraceState(absl::string_view tracestate_value);

  /**
   * Parse a tracestate header value into a TraceState object.
   * @param tracestate_value the raw tracestate header value
   * @return TraceState object or error status if parsing fails
   */
  static absl::StatusOr<TraceState> parse(absl::string_view tracestate_value);

  /**
   * Serialize this TraceState to a tracestate header value.
   * @return the formatted tracestate header value
   */
  std::string toString() const;

  /**
   * Get a value by key.
   * @param key the key to look up
   * @return the value if found, nullopt otherwise
   */
  absl::optional<absl::string_view> get(absl::string_view key) const;

  /**
   * Set a key-value pair.
   * @param key the key
   * @param value the value
   */
  void set(absl::string_view key, absl::string_view value);

  /**
   * Remove a key-value pair.
   * @param key the key to remove
   */
  void remove(absl::string_view key);

  /**
   * Check if the tracestate is empty.
   * @return true if no key-value pairs are present
   */
  bool empty() const { return entries_.empty(); }

private:
  // Store as vector to preserve order (required by W3C spec)
  std::vector<std::pair<std::string, std::string>> entries_;

  // Validation helpers
  static bool isValidKey(absl::string_view key);
  static bool isValidValue(absl::string_view value);
};

/**
 * Represents a single W3C baggage member (key-value pair with optional properties).
 * See https://www.w3.org/TR/baggage/#definition-baggage-member
 */
class BaggageMember {
public:
  /**
   * Construct a BaggageMember with key and value.
   * @param key the baggage key (will be URL-decoded if needed)
   * @param value the baggage value (will be URL-decoded if needed)
   */
  BaggageMember(absl::string_view key, absl::string_view value);

  /**
   * Construct a BaggageMember with key, value, and properties.
   * @param key the baggage key
   * @param value the baggage value
   * @param properties optional metadata properties
   */
  BaggageMember(absl::string_view key, absl::string_view value,
                const std::vector<std::pair<std::string, std::string>>& properties);

  /**
   * Parse a single baggage member from a string.
   * Format: key=value;property1=propvalue1;property2=propvalue2
   * @param member_string the raw baggage member string
   * @return BaggageMember object or error status if parsing fails
   */
  static absl::StatusOr<BaggageMember> parse(absl::string_view member_string);

  /**
   * Serialize this BaggageMember to a string.
   * @return the formatted baggage member string
   */
  std::string toString() const;

  // Accessors
  const std::string& key() const { return key_; }
  const std::string& value() const { return value_; }
  const std::vector<std::pair<std::string, std::string>>& properties() const { return properties_; }

  /**
   * Get a property value by key.
   * @param property_key the property key to look up
   * @return the property value if found, nullopt otherwise
   */
  absl::optional<absl::string_view> getProperty(absl::string_view property_key) const;

  /**
   * Set a property.
   * @param property_key the property key
   * @param property_value the property value
   */
  void setProperty(absl::string_view property_key, absl::string_view property_value);

private:
  std::string key_;
  std::string value_;
  std::vector<std::pair<std::string, std::string>> properties_;

  // Validation and encoding helpers
  static bool isValidKey(absl::string_view key);
  static bool isValidValue(absl::string_view value);
  static std::string urlDecode(absl::string_view input);
  static std::string urlEncode(absl::string_view input);
};

/**
 * Represents a W3C baggage header value containing multiple baggage members.
 * See https://www.w3.org/TR/baggage/#definition-baggage
 */
class Baggage {
public:
  /**
   * Construct an empty Baggage.
   */
  Baggage() = default;

  /**
   * Construct a Baggage from a baggage header value.
   * @param baggage_value the raw baggage header value
   */
  explicit Baggage(absl::string_view baggage_value);

  /**
   * Parse a baggage header value into a Baggage object.
   * @param baggage_value the raw baggage header value
   * @return Baggage object or error status if parsing fails
   */
  static absl::StatusOr<Baggage> parse(absl::string_view baggage_value);

  /**
   * Serialize this Baggage to a baggage header value.
   * @return the formatted baggage header value
   */
  std::string toString() const;

  /**
   * Get a baggage value by key.
   * @param key the key to look up
   * @return the value if found, nullopt otherwise
   */
  absl::optional<absl::string_view> get(absl::string_view key) const;

  /**
   * Set a key-value pair.
   * @param key the key
   * @param value the value
   * @return true if set successfully, false if limits exceeded
   */
  bool set(absl::string_view key, absl::string_view value);

  /**
   * Set a key-value pair with properties.
   * @param member the complete baggage member
   * @return true if set successfully, false if limits exceeded
   */
  bool set(const BaggageMember& member);

  /**
   * Remove a key-value pair.
   * @param key the key to remove
   */
  void remove(absl::string_view key);

  /**
   * Get all baggage members.
   * @return vector of all baggage members
   */
  const std::vector<BaggageMember>& getMembers() const { return members_; }

  /**
   * Check if the baggage is empty.
   * @return true if no members are present
   */
  bool empty() const { return members_.empty(); }

  /**
   * Get the total serialized size of the baggage.
   * @return size in bytes
   */
  size_t size() const;

  /**
   * Check if adding a member would exceed size limits.
   * @param member the member to check
   * @return true if within limits
   */
  bool wouldExceedLimits(const BaggageMember& member) const;

private:
  std::vector<BaggageMember> members_;

  // Helper to find member index by key
  absl::optional<size_t> findMemberIndex(absl::string_view key) const;
};

/**
 * Complete W3C context containing traceparent, tracestate, and baggage.
 * This represents the full W3C distributed tracing context.
 */
class TraceContext {
public:
  /**
   * Construct with traceparent only.
   */
  explicit TraceContext(TraceParent traceparent);

  /**
   * Construct with traceparent and tracestate.
   */
  TraceContext(TraceParent traceparent, TraceState tracestate);

  /**
   * Construct with traceparent, tracestate, and baggage.
   */
  TraceContext(TraceParent traceparent, TraceState tracestate, Baggage baggage);

  // Accessors
  const TraceParent& traceParent() const { return traceparent_; }
  TraceParent& traceParent() { return traceparent_; }

  const TraceState& traceState() const { return tracestate_; }
  TraceState& traceState() { return tracestate_; }

  const Baggage& baggage() const { return baggage_; }
  Baggage& baggage() { return baggage_; }

  /**
   * Check if tracestate is present.
   */
  bool hasTraceState() const { return !tracestate_.empty(); }

  /**
   * Check if baggage is present.
   */
  bool hasBaggage() const { return !baggage_.empty(); }

private:
  TraceParent traceparent_;
  TraceState tracestate_;
  Baggage baggage_;
};

} // namespace W3C
} // namespace Propagators
} // namespace Extensions
} // namespace Envoy
