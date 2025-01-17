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

  return absl::StrCat("{", val.name_, "=", absl::StrJoin(val.match_, "/", ToStringFormatter()),
                      "}");
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
  return absl::StrCat("/", absl::StrJoin(parsed_segments_, "/", ToStringFormatter()), suffix_);
}

bool isValidLiteral(absl::string_view literal) {
  static const std::string* kValidLiteralRegex =
      new std::string(absl::StrCat("^[", kLiteral, "]+$"));
  static const LazyRE2 literal_regex = {kValidLiteralRegex->data()};
  return RE2::FullMatch(literal, *literal_regex);
}

bool isValidRewriteLiteral(absl::string_view literal) {
  static const std::string* kValidLiteralRegex =
      new std::string(absl::StrCat("^[", kLiteral, "/]+$"));
  static const LazyRE2 literal_regex = {kValidLiteralRegex->data()};
  return RE2::FullMatch(literal, *literal_regex);
}

bool isValidVariableName(absl::string_view variable) {
  static const LazyRE2 variable_regex = {"^[a-zA-Z][a-zA-Z0-9_]*$"};
  return RE2::FullMatch(variable, *variable_regex);
}

absl::StatusOr<ParsedResult<Literal>> parseLiteral(absl::string_view pattern) {
  absl::string_view literal =
      std::vector<absl::string_view>(absl::StrSplit(pattern, absl::MaxSplits('/', 1)))[0];
  absl::string_view unparsed_pattern = pattern.substr(literal.size());
  if (!isValidLiteral(literal)) {
    return absl::InvalidArgumentError(fmt::format("Invalid literal: \"{}\"", literal));
  }
  return ParsedResult<Literal>(literal, unparsed_pattern);
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
    if (pattern_item[0] == '*') {

      absl::StatusOr<Operator> status = alsoUpdatePattern<Operator>(parseOperator, &pattern_item);
      if (!status.ok()) {
        return status.status();
      }
      match = *std::move(status);

    } else {
      absl::StatusOr<Literal> status = alsoUpdatePattern<Literal>(parseLiteral, &pattern_item);
      if (!status.ok()) {
        return status.status();
      }
      match = *std::move(status);
    }
    var.match_.push_back(match);
    if (!pattern_item.empty()) {
      if (pattern_item[0] != '/' || pattern_item.size() == 1) {
        return absl::InvalidArgumentError(
            fmt::format("Invalid variable match: \"{}\"", pattern_item));
      }
      pattern_item = pattern_item.substr(1);
    }
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

absl::StatusOr<ParsedPathPattern> parsePathPatternSyntax(absl::string_view path) {
  struct ParsedPathPattern parsed_pattern;

  static const LazyRE2 printable_regex = {"^/[[:graph:]]*$"};
  if (!RE2::FullMatch(path, *printable_regex)) {
    return absl::InvalidArgumentError(fmt::format("Invalid pattern: \"{}\"", path));
  }

  // Parse the leading '/'
  path = path.substr(1);

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

    // Deal with trailing '/' or suffix.
    if (!path.empty()) {
      if (path == "/") {
        // Single trailing '/' at the end, mark this with empty literal.
        parsed_pattern.parsed_segments_.emplace_back("");
        break;
      } else if (path[0] == '/') {
        // Have '/' followed by more text, parse the '/'.
        path = path.substr(1);
      } else {
        // Not followed by '/', treat as suffix.
        absl::StatusOr<Literal> status = alsoUpdatePattern<Literal>(parseLiteral, &path);
        if (!status.ok()) {
          return status.status();
        }
        parsed_pattern.suffix_ = *std::move(status);
        if (!path.empty()) {
          // Suffix didn't parse whole remaining pattern ('/' in path).
          return absl::InvalidArgumentError("Prefix match not supported.");
        }
        break;
      }
    }
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

  return parsed_pattern;
}

std::string toRegexPattern(absl::string_view pattern) {
  return absl::StrReplaceAll(
      pattern, {{"$", "\\$"}, {"(", "\\("}, {")", "\\)"}, {"+", "\\+"}, {".", "\\."}});
}

std::string toRegexPattern(Operator pattern) {
  static const std::string* kPathGlobRegex = new std::string(absl::StrCat("[", kLiteral, "]+"));
  static const std::string* kTextGlobRegex = new std::string(absl::StrCat("[", kLiteral, "/]*"));
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
                          : absl::StrJoin(pattern.match_, "/", ToRegexPatternFormatter()),
                      ")");
}

std::string toRegexPattern(const struct ParsedPathPattern& pattern) {
  return absl::StrCat("/", absl::StrJoin(pattern.parsed_segments_, "/", ToRegexPatternFormatter()),
                      toRegexPattern(pattern.suffix_));
}

} // namespace Internal
} // namespace UriTemplate
} // namespace Extensions
} // namespace Envoy
