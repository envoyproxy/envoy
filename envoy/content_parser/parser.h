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
  // Metadata actions to execute immediately (on_present matched)
  std::vector<MetadataAction> immediate_actions;

  // Whether all rules have reached their match limits (can stop processing)
  bool stop_processing = false;

  // Error message if parsing failed (e.g., invalid JSON). Empty if no error.
  absl::optional<std::string> error_message;
};

/**
 * Content parser interface for extracting metadata from content strings.
 *
 * Lifecycle and Thread Safety:
 * - Parser instances maintain internal state (match counts, error tracking)
 * - Callers should create a new parser instance per request/stream
 * - Parser instances should not be reused across different requests
 *
 * Example usage (caller is responsible for applying metadata actions):
 *
 *   auto parser = factory->createParser();
 *   for (chunk : data_chunks) {
 *     auto result = parser->parse(chunk);
 *     if (result.error_message) {
 *       // Optional: log the error
 *       LOG_DEBUG("Parse error: {}", *result.error_message);
 *     }
 *     for (const auto& action : result.immediate_actions) {
 *       applyAction(action);  // Caller implements this
 *     }
 *     if (result.stop_processing) break;
 *   }
 *   // At end-of-stream, apply deferred actions (on_error/on_missing)
 *   for (const auto& action : parser->getAllDeferredActions()) {
 *     applyAction(action);
 *   }
 */
class Parser {
public:
  virtual ~Parser() = default;

  /**
   * Parse a data string. Call this for each chunk of data.
   *
   * The parser tracks state internally across multiple parse() calls:
   * - Which rules have matched (for on_present)
   * - Which rules had selector not found
   * - Whether any parse error occurred
   *
   * @param data a data string to be processed
   * @return ParseResult with immediate actions and processing state
   */
  virtual ParseResult parse(absl::string_view data) PURE;

  /**
   * Get all deferred actions for rules that never matched.
   *
   * Call this once at end-of-stream. The parser uses internally tracked state
   * to determine which rules need fallback actions.
   *
   * @return vector of metadata actions for all rules needing deferred handling
   */
  virtual std::vector<MetadataAction> getAllDeferredActions() PURE;
};

using ParserPtr = std::unique_ptr<Parser>;

} // namespace ContentParser
} // namespace Envoy
