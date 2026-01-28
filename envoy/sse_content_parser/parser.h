#pragma once

#include <memory>
#include <string>
#include <vector>

#include "envoy/common/pure.h"

#include "source/common/protobuf/protobuf.h"

#include "absl/strings/string_view.h"
#include "absl/types/optional.h"

namespace Envoy {
namespace SseContentParser {

/**
 * Represents a metadata action to be written.
 *
 * Parser implementations must populate namespace_ with a non-empty value.
 * If the config has an empty namespace, the parser should apply its default.
 */
struct MetadataAction {
  std::string namespace_; // Must be non-empty (parser applies defaults)
  std::string key;
  absl::optional<Protobuf::Value> value; // If empty, value extraction failed
  bool preserve_existing = false;
};

/**
 * Result of parsing an SSE event's data field.
 */
struct ParseResult {
  // Metadata actions to execute immediately (returned when rule matches)
  std::vector<MetadataAction> immediate_actions;

  // Indices of rules that matched and returned immediate actions
  std::vector<size_t> matched_rules;

  // Indices of rules where parser's selector/pattern was not found
  std::vector<size_t> selector_not_found_rules;

  // Whether processing should stop after this event
  bool stop_processing = false;

  // Whether an error occurred during parsing (applies to all rules)
  bool has_error = false;
};

/**
 * Interface for SSE content parsers. Implementations parse the data field
 * of an SSE event and determine what metadata actions to take.
 *
 * Lifecycle and Usage:
 * 1. Factory creates one Parser instance per HTTP stream (via ParserFactory::createParser())
 * 2. Filter calls parse() for each complete SSE event extracted from the response body
 * 3. Filter immediately executes MetadataActions returned in ParseResult.immediate_actions
 * 4. Filter tracks per-rule state based on ParseResult:
 *    - matched_rules: Rules that matched and returned immediate actions
 *    - selector_not_found_rules: Rules where parser's pattern/selector was not found
 *    - has_error: Whether any parsing error occurred
 * 5. At end of stream (or when stop_processing triggers), filter calls getDeferredActions()
 *    for each rule that never matched during the stream
 * 6. Filter executes the returned deferred actions (typically fallback values)
 *
 * Thread Safety:
 * - Parser instances are NOT shared across streams
 * - Each HTTP stream gets its own Parser instance
 * - No synchronization is required within Parser implementations
 *
 * Error Handling:
 * - parse() sets ParseResult.has_error = true for parsing failures
 * - Parser should return empty immediate_actions when errors occur
 * - Deferred actions are executed at end-of-stream for rules that never matched
 */
class Parser {
public:
  virtual ~Parser() = default;

  /**
   * Parse an SSE event's data field and return metadata actions.
   *
   * This method is called for each complete SSE event. The filter has already
   * extracted the 'data' field(s) from the SSE event format.
   *
   * @param data the content of the SSE event's data field(s), concatenated if multiple
   * @return ParseResult indicating what actions to take immediately and state to track
   */
  virtual ParseResult parse(absl::string_view data) PURE;

  /**
   * Get deferred actions to execute at end of stream.
   *
   * Called by the filter at end-of-stream for each rule that never matched during the stream.
   * Parser implementations can use the error/selector_not_found flags to determine what
   * fallback actions to return (e.g., error-specific vs. not-found-specific fallbacks).
   *
   * @param rule_index the index of the rule (0-based)
   * @param has_error whether any parsing error occurred during the stream
   * @param selector_not_found whether the parser's selector/pattern was not found in any event
   * @return vector of metadata actions to execute (typically fallback values)
   */
  virtual std::vector<MetadataAction> getDeferredActions(size_t rule_index, bool has_error,
                                                         bool selector_not_found) PURE;

  /**
   * Get the number of rules configured in this parser.
   * Used by the filter to allocate per-rule state tracking.
   * @return number of rules
   */
  virtual size_t numRules() const PURE;
};

using ParserPtr = std::unique_ptr<Parser>;

} // namespace SseContentParser
} // namespace Envoy
