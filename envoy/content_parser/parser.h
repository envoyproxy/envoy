#pragma once

#include <memory>
#include <string>
#include <vector>

#include "envoy/common/pure.h"

#include "source/common/protobuf/protobuf.h"

#include "absl/strings/string_view.h"
#include "absl/types/optional.h"

namespace Envoy {
namespace ContentParser {

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
 * Result of parsing.
 */
struct ParseResult {
  // Metadata actions to execute immediately (returned when rule matches)
  std::vector<MetadataAction> immediate_actions;

  // Indices of rules that matched and returned immediate actions
  std::vector<size_t> matched_rules;

  // Indices of rules where parser's selector/pattern was not found
  std::vector<size_t> selector_not_found_rules;

  // Whether processing should stop after.
  bool stop_processing = false;

  // Whether an error occurred during parsing (applies to all rules)
  bool has_error = false;
};

/**
 * Content parser interface for extracting metadata from content strings.
 *
 * Lifecycle and Thread Safety:
 * - Parser instances may maintain internal state (e.g., match counts for rules)
 * - Callers should create a new parser instance per request/stream
 * - Parser instances should not be reused across different requests
 */
class Parser {
public:
  virtual ~Parser() = default;

  /**
   * Parse a data string.
   *
   * @param data a data string to be processed
   * @return ParseResult indicating what actions to take immediately and state to track
   */
  virtual ParseResult parse(absl::string_view data) PURE;

  /**
   * Get deferred actions to execute.
   *
   * @param rule_index the index of the rule (0-based)
   * @param has_error whether any parsing error occurred
   * @param selector_not_found whether the parser's selector/pattern was not found
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

} // namespace ContentParser
} // namespace Envoy
