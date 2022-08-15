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

namespace PatternTemplateInternal {

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

  absl::string_view literal;

  // Represents an index into the RE2 capture which value should be used
  // to construct the rewritten URL. Note that the index should be greater
  // than 0 as 0 index into the whole match RE2 pattern.
  int var_index;
};

/**
 * Represents a pattern variable. Variables are included in both path match and rewrite paths.
 */
struct Variable {
  absl::string_view var_name;
  std::vector<absl::variant<Operator, Literal>> var_match;

  Variable(absl::string_view name, std::vector<absl::variant<Operator, Literal>> match)
      : var_name(name), var_match(match) {}

  std::string debugString() const;
};

using ParsedSegment = absl::variant<Operator, Variable, Literal>;

/**
 * Represents the parsed path including literals and variables.
 */
struct ParsedUrlPattern {
  std::vector<ParsedSegment> parsed_segments;
  absl::optional<absl::string_view> suffix;
  absl::flat_hash_set<absl::string_view> captured_variables;

  std::string debugString() const;
};

/**
 * Check if literal is valid
 */
bool isValidLiteral(absl::string_view pattern);

/**
 * Check if rewrite literal is valid
 */
bool isValidRewriteLiteral(absl::string_view pattern);

/**
 * Check if indent is valid
 */
bool isValidIndent(absl::string_view pattern);

/**
 * Used by the following Consume{Literal.Operator,Variable} functions
 * in the return value. The functions would take the given pattern,
 * parse what it can into |parsed_value| and return the unconsumed
 *  portion of the pattern in |unconsumed_pattern|.
 */
template <typename T> struct ParsedResult {
  ParsedResult(T val, absl::string_view pattern) : parsed_value(val), unconsumed_pattern(pattern) {}

  T parsed_value;
  absl::string_view unconsumed_pattern;
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

} // namespace PatternTemplateInternal
} // namespace PatternTemplate
} // namespace Extensions
} // namespace Envoy
