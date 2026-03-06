#pragma once

#include "envoy/type/matcher/v3/regex.pb.h"

#include "source/common/common/regex.h"

#include "absl/status/statusor.h"

namespace Envoy {
namespace Matcher {

class RegexReplace {
public:
  RegexReplace() = default;
  RegexReplace(Regex::CompiledMatcherPtr regex, std::string&& substitution)
      : regex_(std::move(regex)), substitution_(std::move(substitution)) {}

  // If the proto has no pattern, returns a null RegexReplace.
  static absl::StatusOr<absl::optional<RegexReplace>>
  create(Regex::Engine& engine, const ::envoy::type::matcher::v3::RegexMatchAndSubstitute& proto);

  // Returns a string of the input string with the regex replace applied.
  //
  // Must not be called on a null RegexReplace.
  std::string apply(absl::string_view in) const;

private:
  Regex::CompiledMatcherPtr regex_{};
  std::string substitution_{};
};

} // namespace Matcher
} // namespace Envoy
