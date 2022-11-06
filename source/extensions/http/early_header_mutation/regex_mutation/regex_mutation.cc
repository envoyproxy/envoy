#include "source/extensions/http/early_header_mutation/regex_mutation/regex_mutation.h"

namespace Envoy {
namespace Extensions {
namespace Http {
namespace EarlyHeaderMutation {
namespace RegexMutation {

bool RegexMutation::mutate(Envoy::Http::RequestHeaderMap& headers) const {
  for (const auto& mutation : mutations_) {
    if (auto header = headers.get(mutation.header_); !header.empty()) {
      auto value = header[0]->value().getStringView();
      auto new_value =
          mutation.regex_rewrite_->replaceAll(value, mutation.regex_rewrite_substitution_);
      headers.setCopy(mutation.rename_, new_value);
    }
  }
  return true;
}

} // namespace RegexMutation
} // namespace EarlyHeaderMutation
} // namespace Http
} // namespace Extensions
} // namespace Envoy
