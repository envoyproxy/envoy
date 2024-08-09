#include "source/extensions/path/uri_template_lib/uri_template_internal.h"

#include <optional>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>

#include "source/common/common/fmt.h"

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

namespace Envoy {
namespace Extensions {
namespace UriTemplate {

namespace Internal {

namespace {

#ifndef SWIG
// Silence warnings about missing initializers for members of LazyRE2.
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif

constexpr unsigned long kPatternMatchingMaxVariablesPerPath = 5;
constexpr unsigned long kPatternMatchingMaxVariableNameLen = 16;
constexpr unsigned long kPatternMatchingMinVariableNameLen = 1;

// Valid pchar from https://datatracker.ietf.org/doc/html/rfc3986#appendix-A
constexpr absl::string_view kLiteral = "a-zA-Z0-9-._~" // Unreserved
                                       "%"             // pct-encoded
                                       "!$&'()+,;"     // sub-delims excluding *=
                                       ":@"
                                       "="; // user included "=" allowed

// Default operator used for the variable when none specified.
constexpr Operator kDefaultVariableOperator = Operator::PathGlob;

constexpr absl::string_view kPathSeparator = "/";

// Visitor for displaying debug info of a ParsedSegment/Variable.var_match.
struct ToStringVisitor {
  template <typename T> std::string operator()(const T& val) const;
};

// Formatter used to allow joining variants together with StrJoin.
struct ToStringFormatter {
  template <typename T> void operator()(std::string* out, const T& t) const {
    absl::StrAppend(out, absl::visit(ToStringVisitor(), t));
  }
};

// Visitor for converting a ParsedSegment variant to the regex.
struct ToRegexPatternVisitor {
  template <typename T> std::string operator()(const T& val) const { return toRegexPattern(val); }
};

// Formatter used to allow joining variants together with StrJoin.
struct ToRegexPatternFormatter {
  template <typename T> void operator()(std::string* out, const T& t) const {
    absl::StrAppend(out, absl::visit(ToRegexPatternVisitor(), t));
  }
};

std::string toString(const Literal val) { return std::string(val); }

std::string toString(const Operator val) {
  switch (val) {
  case Operator::PathGlob:
    return "*";
  case Operator::TextGlob:
    return "**";
  }
  return "";
}

std::string toString(const Variable val) {
  if (val.match_.empty()) {
    return absl::StrCat("{", val.name_, "}");
  }

  return absl::StrCat("{", val.name_, "=", absl::StrJoin(val.match_, "", ToStringFormatter()), "}");
}

template <typename T> std::string ToStringVisitor::operator()(const T& val) const {
  return toString(val);
}

template <typename T>
absl::StatusOr<T>
alsoUpdatePattern(absl::FunctionRef<absl::StatusOr<ParsedResult<T>>(absl::string_view)> parse_func,
                  absl::string_view* patt) {

  absl::StatusOr<ParsedResult<T>> status = parse_func(*patt);
  if (!status.ok()) {
    return status.status();
  }
  ParsedResult<T> result = *std::move(status);

  *patt = result.unparsed_pattern_;
  return result.parsed_value_;
}

} // namespace

std::string Variable::debugString() const { return toString(*this); }

std::string ParsedPathPattern::debugString() const {
  return absl::StrCat(absl::StrJoin(parsed_segments_, "", ToStringFormatter()), suffix_);
}

bool isValidLiteral(absl::string_view literal) {
  static const std::string* kValidLiteralRegex =
      new std::string(absl::StrCat("^[", kLiteral, "]+$"));
  static const LazyRE2 literal_regex = {kValidLiteralRegex->data()};
  return RE2::FullMatch(literal, *literal_regex);
}

bool isValidRewriteLiteral(absl::string_view literal) {
  static const std::string* kValidLiteralRegex =
      new std::string(absl::StrCat("^[", kLiteral, kPathSeparator, "]+$"));
  static const LazyRE2 literal_regex = {kValidLiteralRegex->data()};
  return RE2::FullMatch(literal, *literal_regex);
}

bool isValidVariableName(absl::string_view variable) {
  static const LazyRE2 variable_regex = {"^[a-zA-Z][a-zA-Z0-9_]*$"};
  return RE2::FullMatch(variable, *variable_regex);
}

absl::StatusOr<ParsedResult<Literal>> parseLiteral(absl::string_view pattern) {
  // Treat path separator as a normal literal.
  if (pattern[0] == '/') {
    return ParsedResult<Literal>(kPathSeparator, pattern.substr(1));
  }

  absl::string_view literal = std::vector<absl::string_view>(
      absl::StrSplit(pattern, absl::MaxSplits(kPathSeparator, 1)))[0];

  // Does the literal contain a `{` ? then stop parsing the literal at that position
  size_t n = literal.find('{');

  // No parens found, treat the whole pattern as literal
  if (n == absl::string_view::npos) {
    absl::string_view unparsed_pattern = pattern.substr(literal.size());
    if (!isValidLiteral(literal)) {
      return absl::InvalidArgumentError(fmt::format("Invalid literal: \"{}\"", literal));
    }
    return ParsedResult<Literal>(literal, unparsed_pattern);
  } else {
    // We found a pathParam, treat everything left of the start as literal and yield.
    absl::string_view left_of_pathparam = literal.substr(0, n);
    absl::string_view unparsed_pattern = pattern.substr(n);
    if (!isValidLiteral(left_of_pathparam)) {
      return absl::InvalidArgumentError(fmt::format("Invalid literal: \"{}\"", left_of_pathparam));
    }
    return ParsedResult<Literal>(left_of_pathparam, unparsed_pattern);
  }
}

absl::StatusOr<ParsedResult<Operator>> parseOperator(absl::string_view pattern) {
  if (absl::StartsWith(pattern, "**")) {
    return ParsedResult<Operator>(Operator::TextGlob, pattern.substr(2));
  }
  if (absl::StartsWith(pattern, "*")) {
    return ParsedResult<Operator>(Operator::PathGlob, pattern.substr(1));
  }
  return absl::InvalidArgumentError(fmt::format("Invalid Operator: \"{}\"", pattern));
}

absl::StatusOr<ParsedResult<Variable>> parseVariable(absl::string_view pattern) {
  // Locate the variable pattern to parse.
  if (pattern.size() < 2 || (pattern)[0] != '{') {
    return absl::InvalidArgumentError(fmt::format("Invalid variable: \"{}\"", pattern));
  }
  std::vector<absl::string_view> parts = absl::StrSplit(pattern.substr(1), absl::MaxSplits('}', 1));
  if (parts.size() != 2) {
    return absl::InvalidArgumentError(fmt::format("Unmatched variable bracket in \"{}\"", pattern));
  }
  absl::string_view unparsed_pattern = parts[1];

  // Parse the actual variable pattern, starting with the variable name.
  std::vector<absl::string_view> variable_parts = absl::StrSplit(parts[0], absl::MaxSplits('=', 1));
  if (!isValidVariableName(variable_parts[0])) {
    return absl::InvalidArgumentError(
        fmt::format("Invalid variable name: \"{}\"", variable_parts[0]));
  }
  Variable var = Variable(variable_parts[0], {});

  // Parse the variable match pattern (if any).
  if (variable_parts.size() < 2) {
    return ParsedResult<Variable>(var, unparsed_pattern);
  }
  absl::string_view pattern_item = variable_parts[1];
  if (pattern_item.empty()) {
    return absl::InvalidArgumentError("Empty variable match");
  }
  while (!pattern_item.empty()) {
    absl::variant<Operator, Literal> match;
    // This must be the first match of glob, otherwise we never allow multiple operators in a row
    if (pattern_item[0] == '*' &&
        (var.match_.empty() || !absl::holds_alternative<Operator>(var.match_.back()))) {

      absl::StatusOr<Operator> status = alsoUpdatePattern<Operator>(parseOperator, &pattern_item);
      if (!status.ok()) {
        return status.status();
      }
      match = *std::move(status);

    } else if (pattern_item[0] != '/' &&
               (!var.match_.empty() && absl::holds_alternative<Operator>(var.match_.back()))) {
      // A literal cannot be prepended by operator in variable match
      return absl::InvalidArgumentError("Invalid variable match, path cannot directly be prepended "
                                        "by operator, is a slash missing?");
    } else {
      absl::StatusOr<Literal> status = alsoUpdatePattern<Literal>(parseLiteral, &pattern_item);
      if (!status.ok()) {
        return status.status();
      }
      match = *std::move(status);
    }
    var.match_.push_back(match);
  }

  return ParsedResult<Variable>(var, unparsed_pattern);
}

absl::StatusOr<absl::flat_hash_set<absl::string_view>>
gatherCaptureNames(const struct ParsedPathPattern& pattern) {
  absl::flat_hash_set<absl::string_view> captured_variables;

  for (const ParsedSegment& segment : pattern.parsed_segments_) {
    if (!absl::holds_alternative<Variable>(segment)) {
      continue;
    }
    if (captured_variables.size() >= kPatternMatchingMaxVariablesPerPath) {
      return absl::InvalidArgumentError(
          fmt::format("Exceeded variable count limit ({})", kPatternMatchingMaxVariablesPerPath));
    }
    absl::string_view name = absl::get<Variable>(segment).name_;

    if (name.size() < kPatternMatchingMinVariableNameLen ||
        name.size() > kPatternMatchingMaxVariableNameLen) {
      return absl::InvalidArgumentError(fmt::format(
          "Invalid variable name length (length of \"{}\" should be at least {} and no more than "
          "{})",
          name, kPatternMatchingMinVariableNameLen, kPatternMatchingMaxVariableNameLen));
    }
    if (captured_variables.contains(name)) {
      return absl::InvalidArgumentError(fmt::format("Repeated variable name: \"{}\"", name));
    }
    captured_variables.emplace(name);
  }

  return captured_variables;
}

absl::Status validateNoOperatorAfterTextGlob(const struct ParsedPathPattern& pattern) {
  bool seen_text_glob = false;
  for (const ParsedSegment& segment : pattern.parsed_segments_) {
    if (absl::holds_alternative<Operator>(segment)) {
      if (seen_text_glob) {
        return absl::InvalidArgumentError("Glob after text glob.");
      }
      seen_text_glob = (absl::get<Operator>(segment) == Operator::TextGlob);
    } else if (absl::holds_alternative<Variable>(segment)) {
      const Variable& var = absl::get<Variable>(segment);
      if (var.match_.empty()) {
        if (seen_text_glob) {
          // A variable with no explicit matcher is treated as a path glob.
          return absl::InvalidArgumentError("Implicit variable path glob after text glob.");
        }
      } else {
        for (const absl::variant<Operator, absl::string_view>& var_seg : var.match_) {
          if (!absl::holds_alternative<Operator>(var_seg)) {
            continue;
          }
          if (seen_text_glob) {
            return absl::InvalidArgumentError("Glob after text glob.");
          }
          seen_text_glob = (absl::get<Operator>(var_seg) == Operator::TextGlob);
        }
      }
    }
  }
  return absl::OkStatus();
}

absl::Status validateNoDoublePathSeparator(const struct ParsedPathPattern& pattern) {
  bool previous_was_path_separator = false;
  for (const ParsedSegment& segment : pattern.parsed_segments_) {
    bool current_is_separator = false;
    if (absl::holds_alternative<Literal>(segment)) {
      current_is_separator = (absl::get<Literal>(segment) == kPathSeparator);
    } else if (absl::holds_alternative<Variable>(segment)) {
      Variable v = absl::get<Variable>(segment);
      if (!v.match_.empty() && absl::holds_alternative<Literal>(v.match_.front())) {
        current_is_separator = (absl::get<Literal>(v.match_.front()) == kPathSeparator);
      }
    }
    if (previous_was_path_separator && current_is_separator) {
      return absl::InvalidArgumentError(
          "Double slashes (/) detected in uri_template, this is not allowed.");
    }
    previous_was_path_separator = current_is_separator;
  }
  return absl::OkStatus();
}

absl::StatusOr<ParsedPathPattern> parsePathPatternSyntax(absl::string_view path) {
  struct ParsedPathPattern parsed_pattern;

  static const LazyRE2 printable_regex = {"^/[[:graph:]]*$"};
  if (!RE2::FullMatch(path, *printable_regex)) {
    return absl::InvalidArgumentError(fmt::format("Invalid pattern: \"{}\"", path));
  }

  // Do the initial lexical parsing.
  while (!path.empty()) {
    ParsedSegment segment;
    if (path[0] == '*') {
      absl::StatusOr<Operator> status = alsoUpdatePattern<Operator>(parseOperator, &path);
      if (!status.ok()) {
        return status.status();
      }
      segment = *std::move(status);
    } else if (path[0] == '{') {
      absl::StatusOr<Variable> status = alsoUpdatePattern<Variable>(parseVariable, &path);
      if (!status.ok()) {
        return status.status();
      }
      segment = *std::move(status);
    } else {
      absl::StatusOr<Literal> status = alsoUpdatePattern<Literal>(parseLiteral, &path);
      if (!status.ok()) {
        return status.status();
      }
      segment = *std::move(status);
    }
    parsed_pattern.parsed_segments_.push_back(segment);
  }

  absl::StatusOr<absl::flat_hash_set<absl::string_view>> status =
      gatherCaptureNames(parsed_pattern);
  if (!status.ok()) {
    return status.status();
  }
  parsed_pattern.captured_variables_ = *std::move(status);

  absl::Status validate_status = validateNoOperatorAfterTextGlob(parsed_pattern);
  if (!validate_status.ok()) {
    return validate_status;
  }
  absl::Status validate_double_path_status = validateNoDoublePathSeparator(parsed_pattern);
  if (!validate_double_path_status.ok()) {
    return validate_double_path_status;
  }

  return parsed_pattern;
}

std::string toRegexPattern(absl::string_view pattern) {
  return absl::StrReplaceAll(
      pattern, {{"$", "\\$"}, {"(", "\\("}, {")", "\\)"}, {"+", "\\+"}, {".", "\\."}});
}

std::string toRegexPattern(Operator pattern) {
  static const std::string* kPathGlobRegex = new std::string(absl::StrCat("[", kLiteral, "]+"));
  static const std::string* kTextGlobRegex =
      new std::string(absl::StrCat("[", kLiteral, kPathSeparator, "]*"));
  switch (pattern) {
  case Operator::PathGlob: // "*"
    return *kPathGlobRegex;
  case Operator::TextGlob: // "**"
    return *kTextGlobRegex;
  }
  return "";
}

std::string toRegexPattern(const Variable& pattern) {
  return absl::StrCat("(?P<", pattern.name_, ">",
                      pattern.match_.empty()
                          ? toRegexPattern(kDefaultVariableOperator)
                          : absl::StrJoin(pattern.match_, "", ToRegexPatternFormatter()),
                      ")");
}

std::string toRegexPattern(const struct ParsedPathPattern& pattern) {
  return absl::StrCat(absl::StrJoin(pattern.parsed_segments_, "", ToRegexPatternFormatter()),
                      toRegexPattern(pattern.suffix_));
}

} // namespace Internal
} // namespace UriTemplate
} // namespace Extensions
} // namespace Envoy
