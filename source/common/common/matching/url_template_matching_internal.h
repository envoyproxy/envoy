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

namespace matching {

namespace url_template_matching_internal {

using Literal = absl::string_view;
enum class Operator { kPathGlob, kTextGlob };

struct Variable {
  absl::string_view var_name;
  std::vector<std::variant<Operator, Literal>> var_match;

  Variable(absl::string_view name, std::vector<std::variant<Operator, Literal>> match)
      : var_name(name), var_match(match) {}

  std::string DebugString() const;
};

using ParsedSegment = std::variant<Operator, Variable, Literal>;

struct ParsedUrlPattern {
  std::vector<ParsedSegment> parsed_segments;
  std::optional<absl::string_view> suffix;
  absl::flat_hash_set<absl::string_view> captured_variables;

  std::string DebugString() const;
};

bool IsValidLiteral(absl::string_view pattern);

bool IsValidRewriteLiteral(absl::string_view pattern);

bool IsValidIdent(absl::string_view pattern);

// Used by the following Consume{Literal.Operator,Variable} functions
// in the return value. The functions would take the given pattern,
// parse what it can into |parsed_value| and return the unconsumed
// portion of the pattern in |unconsumed_pattern|.
template <typename T> struct ParsedResult {
  ParsedResult(T val, absl::string_view pattern) : parsed_value(val), unconsumed_pattern(pattern) {}

  T parsed_value;
  absl::string_view unconsumed_pattern;
};

absl::StatusOr<ParsedResult<Literal>> ConsumeLiteral(absl::string_view pattern);

absl::StatusOr<ParsedResult<Operator>> ConsumeOperator(absl::string_view pattern);

absl::StatusOr<ParsedResult<Variable>> ConsumeVariable(absl::string_view pattern);

absl::StatusOr<ParsedUrlPattern> ParseURLPatternSyntax(absl::string_view url_pattern);

std::string ToRegexPattern(Literal pattern);

std::string ToRegexPattern(Operator pattern);

std::string ToRegexPattern(const Variable& pattern);

std::string ToRegexPattern(const struct ParsedUrlPattern& pattern);

inline re2::StringPiece ToStringPiece(absl::string_view text) { return {text.data(), text.size()}; }

} // namespace url_template_matching_internal

} // namespace matching

#endif // SOURCE_COMMON_COMMON_MATCHING_URL_TEMPLATE_MATCHING_INTERNAL_H
