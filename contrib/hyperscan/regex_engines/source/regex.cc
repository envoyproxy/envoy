#include "contrib/hyperscan/regex_engines/source/regex.h"

namespace Envoy {
namespace Extensions {
namespace Regex {
namespace Hyperscan {

HyperscanEngine::HyperscanEngine(Event::Dispatcher& dispatcher, ThreadLocal::SlotAllocator& tls)
    : dispatcher_(dispatcher), tls_(tls) {}

absl::StatusOr<Envoy::Regex::CompiledMatcherPtr>
HyperscanEngine::matcher(const std::string& regex) const {
  std::vector<const char*> expressions{regex.c_str()};
  std::vector<unsigned int> flags{HS_FLAG_UTF8};
  std::vector<unsigned int> ids{0};

  return std::make_unique<Matching::InputMatchers::Hyperscan::Matcher>(expressions, flags, ids,
                                                                       dispatcher_, tls_, true);
}

} // namespace Hyperscan
} // namespace Regex
} // namespace Extensions
} // namespace Envoy
