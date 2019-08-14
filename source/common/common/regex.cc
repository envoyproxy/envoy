#include "common/common/regex.h"

#include "envoy/common/exception.h"

#include "common/common/assert.h"
#include "common/common/fmt.h"

#include "re2/re2.h"

namespace Envoy {
namespace Regex {
namespace {

class CompiledStdMatcher : public CompiledMatcher {
public:
  CompiledStdMatcher(std::regex&& regex) : regex_(std::move(regex)) {}

  // CompiledMatcher
  bool match(absl::string_view value) const override {
    return std::regex_match(value.begin(), value.end(), regex_);
  }

private:
  const std::regex regex_;
};

class CompiledGoogleReMatcher : public CompiledMatcher {
public:
  CompiledGoogleReMatcher(const std::string& regex) : regex_(regex, re2::RE2::Quiet) {
    if (!regex_.ok()) {
      throw EnvoyException(regex_.error());
    }
  }

  // CompiledMatcher
  bool match(absl::string_view value) const override {
    return re2::RE2::FullMatch(re2::StringPiece(value.data(), value.size()), regex_);
  }

private:
  const re2::RE2 regex_;
};

} // namespace

CompiledMatcherPtr Utility::parseRegex(const envoy::type::matcher::RegexMatcher& matcher) {
  // Google Re is the only currently supported engine.
  ASSERT(matcher.has_google_re2());
  return std::make_unique<CompiledGoogleReMatcher>(matcher.regex());
}

CompiledMatcherPtr Utility::parseStdRegexAsCompiledMatcher(const std::string& regex,
                                                           std::regex::flag_type flags) {
  return std::make_unique<CompiledStdMatcher>(parseStdRegex(regex, flags));
}

std::regex Utility::parseStdRegex(const std::string& regex, std::regex::flag_type flags) {
  // TODO(zuercher): In the future, PGV (https://github.com/lyft/protoc-gen-validate) annotations
  // may allow us to remove this in favor of direct validation of regular expressions.
  try {
    return std::regex(regex, flags);
  } catch (const std::regex_error& e) {
    throw EnvoyException(fmt::format("Invalid regex '{}': {}", regex, e.what()));
  }
}

} // namespace Regex
} // namespace Envoy
