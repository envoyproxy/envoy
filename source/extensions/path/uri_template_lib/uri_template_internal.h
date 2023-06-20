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
namespace UriTemplate {

namespace Internal {

/**
 * String to be concatenated in rewritten path.
 */
using Literal = absl::string_view;

/**
 * Determines what operations to use on the input pattern segment.
 */
enum class Operator { PathGlob, TextGlob };

/**
 * Represents a pattern variable. Variables are included in both path match and rewrite paths.
 */
struct Variable {
  Variable(absl::string_view name, std::vector<absl::variant<Operator, Literal>> match)
      : name_(name), match_(match) {}

  std::string debugString() const;

  absl::string_view name_;

  // The pattern which this variable matches.
  std::vector<absl::variant<Operator, Literal>> match_;
};

using ParsedSegment = absl::variant<Operator, Variable, Literal>;

/**
 * Represents the parsed path including literals and variables.
 */
struct ParsedPathPattern {
  std::vector<ParsedSegment> parsed_segments_;

  // Suffix holds end of path that matched a wildcard.
  // For example:
  // Pattern: /foo/bar/**
  // Path: /foo/bar/some/more/stuff
  // Suffix: some/more/stuff
  absl::string_view suffix_;
  absl::flat_hash_set<absl::string_view> captured_variables_;

  std::string debugString() const;
};

/**
 * Returns true if `literal` is valid for pattern match.
 * (does not contain wildcards, forwards slashes, or curly brackets).
 */
bool isValidLiteral(absl::string_view literal);

/**
 * Returns true if `literal` is valid for pattern rewrite.
 * (does not contain wildcards or curly brackets).
 */
bool isValidRewriteLiteral(absl::string_view literal);

/**
 * Return true if variable name is valid.
 */
bool isValidVariableName(absl::string_view variable);

/**
 * Used by the following Parse{Literal,Operator,Variable} functions
 * in the return value. The functions would take the given pattern,
 * parse what it can into |parsed_value| and return the unparsed
 * portion of the pattern in |unparsed_pattern|.
 */
template <typename T> struct ParsedResult {
  ParsedResult(T val, absl::string_view pattern) : parsed_value_(val), unparsed_pattern_(pattern) {}

  T parsed_value_;
  absl::string_view unparsed_pattern_;
};

/**
 * Parses a literal in the front of `pattern` and returns the literal and remaining pattern.
 */
absl::StatusOr<ParsedResult<Literal>> parseLiteral(absl::string_view pattern);

/**
 * Parses a operator in the front of `pattern` and returns the operator and remaining pattern.
 */
absl::StatusOr<ParsedResult<Operator>> parseOperator(absl::string_view pattern);

/**
 * Parses a variable in the front of `pattern`.
 */
absl::StatusOr<ParsedResult<Variable>> parseVariable(absl::string_view pattern);

/**
 * Converts input path to ParsedPathPattern.
 * ParsedPathPattern hold prefix, string literals, and variables for rewrite.
 */
absl::StatusOr<ParsedPathPattern> parsePathPatternSyntax(absl::string_view path);

/**
 * Convert Literal to a re2-compatible regular expression.
 */
std::string toRegexPattern(Literal pattern);

/**
 * Converts Operator to a regex string representation.
 */
std::string toRegexPattern(Operator pattern);

/**
 * Converts Variable to a regex string representation.
 */
std::string toRegexPattern(const Variable& pattern);

/**
 * Converts ParsedPathPattern to a regex string representation.
 */
std::string toRegexPattern(const struct ParsedPathPattern& pattern);

/**
 * Checks end of pattern to ensure glob operator is last.
 */
absl::Status validateNoOperatorAfterTextGlob(const struct ParsedPathPattern& pattern);

} // namespace Internal
} // namespace UriTemplate
} // namespace Extensions
} // namespace Envoy
