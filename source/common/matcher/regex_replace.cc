#include "source/common/matcher/regex_replace.h"

namespace Envoy {
namespace Matcher {

absl::StatusOr<RegexReplace>
RegexReplace::create(Regex::Engine& engine,
                     const ::envoy::type::matcher::v3::RegexMatchAndSubstitute& proto) {
  ASSERT(!proto.pattern().regex().empty(), "invalid RegexMatchAndSubstitute message");
  auto regex_or_status = Regex::Utility::parseRegex(proto.pattern(), engine);
  RETURN_IF_NOT_OK(regex_or_status.status());
  return RegexReplace(std::move(regex_or_status).value(), std::string{proto.substitution()});
}

std::string RegexReplace::apply(absl::string_view in) const {
  return regex_->replaceAll(in, substitution_);
}

} // namespace Matcher
} // namespace Envoy
