#ifndef SOURCE_COMMON_COMMON_MATCHING_URL_TEMPLATE_MATCHING_INTERNAL_H
#define SOURCE_COMMON_COMMON_MATCHING_URL_TEMPLATE_MATCHING_INTERNAL_H

#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "absl/container/flat_hash_set.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"
#include "absl/types/variant.h"
#include "re2/re2.h"

namespace Envoy {
namespace matching {

namespace url_template_matching_internal {

using Literal = absl::string_view;
enum class Operator { kPathGlob, kTextGlob };

struct Variable {
  absl::string_view var_name;
  std::vector<absl::variant<Operator, Literal>> var_match;

  Variable(absl::string_view name, std::vector<absl::variant<Operator, Literal>> match)
      : var_name(name), var_match(match) {}

  std::string DebugString() const;
};

using ParsedSegment = absl::variant<Operator, Variable, Literal>;

struct ParsedUrlPattern {
  std::vector<ParsedSegment> parsed_segments;
  absl::optional<absl::string_view> suffix;
  absl::flat_hash_set<absl::string_view> captured_variables;

  std::string DebugString() const;
};

bool isValidLiteral(absl::string_view pattern);

bool isValidRewriteLiteral(absl::string_view pattern);

bool isValidIndent(absl::string_view pattern);

// Used by the following Consume{Literal.Operator,Variable} functions
// in the return value. The functions would take the given pattern,
// parse what it can into |parsed_value| and return the unconsumed
// portion of the pattern in |unconsumed_pattern|.
template <typename T> struct ParsedResult {
  ParsedResult(T val, absl::string_view pattern) : parsed_value(val), unconsumed_pattern(pattern) {}

  T parsed_value;
  absl::string_view unconsumed_pattern;
};

absl::StatusOr<ParsedResult<Literal>> consumeLiteral(absl::string_view pattern);

absl::StatusOr<ParsedResult<Operator>> consumeOperator(absl::string_view pattern);

absl::StatusOr<ParsedResult<Variable>> consumeVariable(absl::string_view pattern);

absl::StatusOr<ParsedUrlPattern> parseURLPatternSyntax(absl::string_view url_pattern);

std::string toRegexPattern(Literal pattern);

std::string toRegexPattern(Operator pattern);

std::string toRegexPattern(const Variable& pattern);

std::string toRegexPattern(const struct ParsedUrlPattern& pattern);

inline re2::StringPiece toStringPiece(absl::string_view text) { return {text.data(), text.size()}; }

} // namespace url_template_matching_internal

} // namespace matching
} // namespace Envoy

#endif // SOURCE_COMMON_COMMON_MATCHING_URL_TEMPLATE_MATCHING_INTERNAL_H
