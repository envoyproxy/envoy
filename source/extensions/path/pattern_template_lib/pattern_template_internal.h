#pragma once

#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"
#include "absl/types/variant.h"
#include "re2/re2.h"

namespace Envoy {
namespace Extensions {
namespace PatternTemplate {

namespace Internal {

using Literal = absl::string_view;

/**
 * Determines what operations to use on the input pattern segment
 */
enum class Operator { KPathGlob, KTextGlob };

/**
 * Represents a segment of the rewritten URL, including any path segments,
 * slash and prefix.
 */
struct RewriteSegment {

  // A string that will be concatenated for rewrite.
  absl::string_view literal_;

  // Represents an index into the RE2 capture which value should be used
  // to construct the rewritten URL. Note that the index should be greater
  // than 0 as 0 index into the whole match RE2 pattern.
  int capture_index_;
};

/**
 * Represents a pattern variable. Variables are included in both path match and rewrite paths.
 */
struct Variable {
  absl::string_view var_name_;
  std::vector<absl::variant<Operator, Literal>> var_match_;

  Variable(absl::string_view name, std::vector<absl::variant<Operator, Literal>> match)
      : var_name_(name), var_match_(match) {}

  std::string debugString() const;
};

using ParsedSegment = absl::variant<Operator, Variable, Literal>;

/**
 * Represents the parsed path including literals and variables.
 */
struct ParsedUrlPattern {
  std::vector<ParsedSegment> parsed_segments_;
  absl::optional<absl::string_view> suffix_;
  absl::flat_hash_set<absl::string_view> captured_variables_;

  std::string debugString() const;
};

/**
 * Check if literal is valid
 */
bool isValidLiteral(absl::string_view literal);

/**
 * Check if rewrite literal is valid
 */
bool isValidRewriteLiteral(absl::string_view literal);

/**
 * Check if indent is valid
 */
bool isValidVariableName(absl::string_view indent);

/**
 * Used by the following Consume{Literal.Operator,Variable} functions
 * in the return value. The functions would take the given pattern,
 * parse what it can into |parsed_value| and return the unconsumed
 *  portion of the pattern in |unconsumed_pattern|.
 */
template <typename T> struct ParsedResult {
  ParsedResult(T val, absl::string_view pattern)
      : parsed_value_(val), unconsumed_pattern_(pattern) {}

  T parsed_value_;
  absl::string_view unconsumed_pattern_;
};

/**
 * Converts input pattern to ParsedResult<Literal>
 */
absl::StatusOr<ParsedResult<Literal>> consumeLiteral(absl::string_view pattern);

/**
 * Converts input pattern to ParsedResult<Operator>
 */
absl::StatusOr<ParsedResult<Operator>> consumeOperator(absl::string_view pattern);

/**
 * Converts input pattern to ParsedResult<Variable>
 */
absl::StatusOr<ParsedResult<Variable>> consumeVariable(absl::string_view pattern);

/**
 * Converts input pattern to ParsedUrlPattern
 */
absl::StatusOr<ParsedUrlPattern> parseURLPatternSyntax(absl::string_view url_pattern);

/**
 * Converts Literal to std::string
 */
std::string toRegexPattern(Literal pattern);

/**
 * Converts Operator to std::string
 */
std::string toRegexPattern(Operator pattern);

/**
 * Converts Variable to std::string
 */
std::string toRegexPattern(const Variable& pattern);

/**
 * Converts ParsedUrlPattern to std::string
 */
std::string toRegexPattern(const struct ParsedUrlPattern& pattern);

/**
 * Converts string_view to be used in re2
 */
inline re2::StringPiece toStringPiece(absl::string_view text) { return {text.data(), text.size()}; }

} // namespace Internal
} // namespace PatternTemplate
} // namespace Extensions
} // namespace Envoy
