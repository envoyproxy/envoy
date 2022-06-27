#include "source/common/common/matching/url_template_matching_internal.h"

#include <optional>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>

#include "absl/container/flat_hash_set.h"
#include "absl/flags/flag.h"
#include "absl/functional/function_ref.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_replace.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "absl/types/variant.h"
#include "re2/re2.h"

namespace matching {

namespace url_template_matching_internal {

namespace {

#ifndef SWIG
// Silence warnings about missing initializers for members of LazyRE2.
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif

unsigned long pattern_matching_max_variables_per_url = 5;
unsigned long pattern_matching_max_variable_name_len = 16;
unsigned long pattern_matching_min_variable_name_len = 1;

// Valid pchar from https://datatracker.ietf.org/doc/html/rfc3986#appendix-A
constexpr absl::string_view kLiteral = "a-zA-Z0-9-._~" // Unreserved
                                       "%"             // pct-encoded
                                       "!$&'()+,;"     // sub-delims excluding *=
                                       ":@";

// Default operator used for the variable when none specified.
constexpr Operator kDefaultVariableOperator = Operator::kPathGlob;

// Visitor for displaying debug info of a ParsedSegment/Variable.var_match.
struct ToStringVisitor {
  template <typename T> std::string operator()(const T& val) const;
};

// Formatter used to allow joining variants together with StrJoin.
struct ToStringFormatter {
  template <typename T> void operator()(std::string* out, const T& t) const {
    absl::StrAppend(out, std::visit(ToStringVisitor(), t));
  }
};

// Visitor for converting a ParsedSegment variant to the regex.
struct ToRegexPatternVisitor {
  template <typename T> std::string operator()(const T& val) const { return ToRegexPattern(val); }
};

// Formatter used to allow joining variants together with StrJoin.
struct ToRegexPatternFormatter {
  template <typename T> void operator()(std::string* out, const T& t) const {
    absl::StrAppend(out, std::visit(ToRegexPatternVisitor(), t));
  }
};

std::string ToString(const Literal val) { return std::string(val); }

std::string ToString(const Operator val) {
  switch (val) {
  case Operator::kPathGlob:
    return "*";
  case Operator::kTextGlob:
    return "**";
  }
}

std::string ToString(const Variable val) {
  if (val.var_match.empty()) {
    return absl::StrCat("{", val.var_name, "}");
  }

  return absl::StrCat("{", val.var_name, "=",
                      absl::StrJoin(val.var_match, "/", ToStringFormatter()), "}");
}

template <typename T> std::string ToStringVisitor::operator()(const T& val) const {
  return ToString(val);
}

template <typename T>
absl::StatusOr<T> AlsoUpdatePattern(
    absl::FunctionRef<absl::StatusOr<ParsedResult<T>>(absl::string_view)> consume_func,
    absl::string_view* patt) {

  absl::StatusOr<ParsedResult<T>> status = consume_func(*patt);
  if (!status.ok()) {
    return status.status();
  }
  ParsedResult<T> result = *std::move(status);

  *patt = result.unconsumed_pattern;
  return result.parsed_value;
}

} // namespace

std::string Variable::DebugString() const { return ToString(*this); }

std::string ParsedUrlPattern::DebugString() const {
  return absl::StrCat("/", absl::StrJoin(parsed_segments, "/", ToStringFormatter()),
                      suffix.value_or(""));
}

bool IsValidLiteral(absl::string_view pattern) {
  static const std::string* kValidLiteralRegex =
      new std::string(absl::StrCat("^[", kLiteral, "]+$"));
  static const LazyRE2 literal_regex = {kValidLiteralRegex->data()};
  return RE2::FullMatch(ToStringPiece(pattern), *literal_regex);
}

bool IsValidRewriteLiteral(absl::string_view pattern) {
  static const std::string* kValidLiteralRegex =
      new std::string(absl::StrCat("^[", kLiteral, "/]+$"));
  static const LazyRE2 literal_regex = {kValidLiteralRegex->data()};
  return RE2::FullMatch(ToStringPiece(pattern), *literal_regex);
}

bool IsValidIdent(absl::string_view pattern) {
  static const LazyRE2 ident_regex = {"^[a-zA-Z][a-zA-Z0-9_]*$"};
  return RE2::FullMatch(ToStringPiece(pattern), *ident_regex);
}

absl::StatusOr<ParsedResult<Literal>> ConsumeLiteral(absl::string_view pattern) {
  absl::string_view lit =
      std::vector<absl::string_view>(absl::StrSplit(pattern, absl::MaxSplits('/', 1)))[0];
  absl::string_view unconsumed_pattern = pattern.substr(lit.size());
  if (!IsValidLiteral(lit)) {
    return absl::InvalidArgumentError("Invalid literal");
  }
  return ParsedResult<Literal>(lit, unconsumed_pattern);
}

absl::StatusOr<ParsedResult<Operator>> ConsumeOperator(absl::string_view pattern) {
  if (absl::StartsWith(pattern, "**")) {
    return ParsedResult<Operator>(Operator::kTextGlob, pattern.substr(2));
  }
  if (absl::StartsWith(pattern, "*")) {
    return ParsedResult<Operator>(Operator::kPathGlob, pattern.substr(1));
  }
  return absl::InvalidArgumentError("Invalid Operator");
}

absl::StatusOr<ParsedResult<Variable>> ConsumeVariable(absl::string_view pattern) {
  // Locate the variable pattern to parse.
  if (pattern.size() < 2 || (pattern)[0] != '{') {
    return absl::InvalidArgumentError("Invalid variable");
  }
  std::vector<absl::string_view> parts = absl::StrSplit(pattern.substr(1), absl::MaxSplits('}', 1));
  if (parts.size() != 2) {
    return absl::InvalidArgumentError("Unmatched variable bracket");
  }
  absl::string_view unconsumed_pattern = parts[1];

  // Parse the actual variable pattern, starting with the variable name.
  std::vector<absl::string_view> var_parts = absl::StrSplit(parts[0], absl::MaxSplits('=', 1));
  if (!IsValidIdent(var_parts[0])) {
    return absl::InvalidArgumentError("Invalid variable name");
  }
  Variable var = Variable(var_parts[0], {});

  // Parse the variable match pattern (if any).
  if (var_parts.size() < 2) {
    return ParsedResult<Variable>(var, unconsumed_pattern);
  }
  absl::string_view var_patt = var_parts[1];
  if (var_patt.empty()) {
    return absl::InvalidArgumentError("Empty variable match");
  }
  while (!var_patt.empty()) {
    std::variant<Operator, Literal> var_match;
    if (var_patt[0] == '*') {

      absl::StatusOr<Operator> status = AlsoUpdatePattern<Operator>(ConsumeOperator, &var_patt);
      if (!status.ok()) {
        return status.status();
      }
      var_match = *std::move(status);

    } else {

      absl::StatusOr<Literal> status = AlsoUpdatePattern<Literal>(ConsumeLiteral, &var_patt);
      if (!status.ok()) {
        return status.status();
      }
      var_match = *std::move(status);
    }
    var.var_match.push_back(var_match);
    if (!var_patt.empty()) {
      if (var_patt[0] != '/' || var_patt.size() == 1) {
        return absl::InvalidArgumentError("Invalid variable match");
      }
      var_patt = var_patt.substr(1);
    }
  }

  return ParsedResult<Variable>(var, unconsumed_pattern);
}

absl::StatusOr<absl::flat_hash_set<absl::string_view>>
GatherCaptureNames(struct ParsedUrlPattern pattern) {
  absl::flat_hash_set<absl::string_view> captured_variables;

  for (const ParsedSegment& segment : pattern.parsed_segments) {
    if (!std::holds_alternative<Variable>(segment)) {
      continue;
    }
    if (captured_variables.size() >= pattern_matching_max_variables_per_url) {
      return absl::InvalidArgumentError("Exceeded variable count limit");
    }
    absl::string_view var_name = std::get<Variable>(segment).var_name;

    if (var_name.size() < pattern_matching_min_variable_name_len ||
        var_name.size() > pattern_matching_max_variable_name_len) {
      return absl::InvalidArgumentError("Invalid variable length");
    }
    if (captured_variables.contains(var_name)) {
      return absl::InvalidArgumentError("Repeated variable name");
    }
    captured_variables.emplace(var_name);
  }

  return captured_variables;
}

absl::Status ValidateNoOperatorAfterTextGlob(struct ParsedUrlPattern pattern) {
  bool seen_text_glob = false;
  for (const ParsedSegment& segment : pattern.parsed_segments) {
    if (std::holds_alternative<Operator>(segment)) {
      if (seen_text_glob) {
        return absl::InvalidArgumentError("Glob after text glob.");
      }
      seen_text_glob = (std::get<Operator>(segment) == Operator::kTextGlob);
    } else if (std::holds_alternative<Variable>(segment)) {
      const Variable& var = std::get<Variable>(segment);
      if (var.var_match.empty()) {
        if (seen_text_glob) {
          // A variable with no explicit matcher is treated as a path glob.
          return absl::InvalidArgumentError("Implicit variable path glob after text glob.");
        }
      } else {
        for (const std::variant<Operator, absl::string_view>& var_seg : var.var_match) {
          if (!std::holds_alternative<Operator>(var_seg)) {
            continue;
          }
          if (seen_text_glob) {
            return absl::InvalidArgumentError("Glob after text glob.");
          }
          seen_text_glob = (std::get<Operator>(var_seg) == Operator::kTextGlob);
        }
      }
    }
  }
  return absl::OkStatus();
}

absl::StatusOr<ParsedUrlPattern> ParseURLPatternSyntax(absl::string_view url_pattern) {
  struct ParsedUrlPattern parsed_pattern;

  static const LazyRE2 printable_regex = {"^/[[:graph:]]*$"};
  if (!RE2::FullMatch(ToStringPiece(url_pattern), *printable_regex)) {

    return absl::InvalidArgumentError("Invalid pattern");
  }

  // Consume the leading '/'
  url_pattern = url_pattern.substr(1);

  // Do the initial lexical parsing.
  while (!url_pattern.empty()) {
    ParsedSegment segment;
    if (url_pattern[0] == '*') {

      absl::StatusOr<Operator> status = AlsoUpdatePattern<Operator>(ConsumeOperator, &url_pattern);
      if (!status.ok()) {
        return status.status();
      }
      segment = *std::move(status);
    } else if (url_pattern[0] == '{') {

      absl::StatusOr<Variable> status = AlsoUpdatePattern<Variable>(ConsumeVariable, &url_pattern);
      if (!status.ok()) {
        return status.status();
      }
      segment = *std::move(status);
    } else {

      absl::StatusOr<Literal> status = AlsoUpdatePattern<Literal>(ConsumeLiteral, &url_pattern);
      if (!status.ok()) {
        return status.status();
      }
      segment = *std::move(status);
    }
    parsed_pattern.parsed_segments.push_back(segment);

    // Deal with trailing '/' or suffix.
    if (!url_pattern.empty()) {
      if (url_pattern == "/") {
        // Single trailing '/' at the end, mark this with empty literal.
        parsed_pattern.parsed_segments.emplace_back("");
        break;
      } else if (url_pattern[0] == '/') {
        // Have '/' followed by more text, consume the '/'.
        url_pattern = url_pattern.substr(1);
      } else {
        // Not followed by '/', treat as suffix.

        absl::StatusOr<Literal> status = AlsoUpdatePattern<Literal>(ConsumeLiteral, &url_pattern);
        if (!status.ok()) {
          return status.status();
        }
        parsed_pattern.suffix = *std::move(status);
        if (!url_pattern.empty()) {
          // Suffix didn't consume whole remaining pattern ('/' in url_pattern).
          return absl::InvalidArgumentError("Prefix match not supported.");
        }
        break;
      }
    }
  }
  absl::StatusOr<absl::flat_hash_set<absl::string_view>> status =
      GatherCaptureNames(parsed_pattern);
  if (!status.ok()) {
    return status.status();
  }
  parsed_pattern.captured_variables = *std::move(status);

  absl::Status validate_status = ValidateNoOperatorAfterTextGlob(parsed_pattern);
  if (!validate_status.ok()) {
    return validate_status;
  }

  return parsed_pattern;
}

std::string ToRegexPattern(Literal pattern) {
  return absl::StrReplaceAll(
      pattern, {{"$", "\\$"}, {"(", "\\("}, {")", "\\)"}, {"+", "\\+"}, {".", "\\."}});
}

std::string ToRegexPattern(Operator pattern) {
  static const std::string* kPathGlobRegex = new std::string(absl::StrCat("[", kLiteral, "]+"));
  static const std::string* kTextGlobRegex = new std::string(absl::StrCat("[", kLiteral, "/]*"));
  switch (pattern) {
  case Operator::kPathGlob: // "*"
    return *kPathGlobRegex;
  case Operator::kTextGlob: // "**"
    return *kTextGlobRegex;
  }
}

std::string ToRegexPattern(const Variable& pattern) {
  return absl::StrCat("(?P<", pattern.var_name, ">",
                      pattern.var_match.empty()
                          ? ToRegexPattern(kDefaultVariableOperator)
                          : absl::StrJoin(pattern.var_match, "/", ToRegexPatternFormatter()),
                      ")");
}

std::string ToRegexPattern(const struct ParsedUrlPattern& pattern) {
  return absl::StrCat("/", absl::StrJoin(pattern.parsed_segments, "/", ToRegexPatternFormatter()),
                      ToRegexPattern(pattern.suffix.value_or("")));
}

} // namespace url_template_matching_internal

} // namespace matching
