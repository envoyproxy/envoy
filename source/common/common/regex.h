#pragma once

#include <memory>
#include <regex>

#include "envoy/common/regex.h"
#include "envoy/type/matcher/v3/regex.pb.h"

namespace Envoy {
namespace Regex {

/**
 * Utilities for constructing regular expressions.
 */
class Utility {
public:
  /**
   * Constructs a std::regex, converting any std::regex_error exception into an EnvoyException.
   * @param regex std::string containing the regular expression to parse.
   * @param flags std::regex::flag_type containing parser flags. Defaults to std::regex::optimize.
   * @return std::regex constructed from regex and flags.
   * @throw EnvoyException if the regex string is invalid.
   */
  static std::regex parseStdRegex(const std::string& regex,
                                  std::regex::flag_type flags = std::regex::optimize);

  /**
   * Construct an std::regex compiled regex matcher.
   *
   * TODO(mattklein123): In general this is only currently used in deprecated code paths and can be
   * removed once all of those code paths are removed.
   */
  static CompiledMatcherPtr
  parseStdRegexAsCompiledMatcher(const std::string& regex,
                                 std::regex::flag_type flags = std::regex::optimize);

  /**
   * Construct a compiled regex matcher from a match config.
   */
  static CompiledMatcherPtr parseRegex(const envoy::type::matcher::v3::RegexMatcher& matcher);
};

} // namespace Regex
} // namespace Envoy
