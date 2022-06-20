#include "contrib/hyperscan/regex_engines/source/regex.h"

namespace Envoy {
namespace Extensions {
namespace Regex {
namespace Hyperscan {

HyperscanEngine::HyperscanEngine(unsigned int flag, ThreadLocal::SlotAllocator& tls)
    : flag_(flag), tls_(tls) {}

Envoy::Regex::CompiledMatcherPtr HyperscanEngine::matcher(const std::string& regex) const {
  std::vector<const char*> expressions{regex.c_str()};
  std::vector<unsigned int> flags{flag_};
  std::vector<unsigned int> ids{0};

  return std::make_unique<Matching::InputMatchers::Hyperscan::Matcher>(expressions, flags, ids,
                                                                       tls_);
}

} // namespace Hyperscan
} // namespace Regex
} // namespace Extensions
} // namespace Envoy
