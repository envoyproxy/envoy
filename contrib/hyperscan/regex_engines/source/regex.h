#pragma once

#include "envoy/common/regex.h"

#include "contrib/hyperscan/matching/input_matchers/source/matcher.h"

namespace Envoy {
namespace Extensions {
namespace Regex {
namespace Hyperscan {

class HyperscanEngine : public Envoy::Regex::Engine {
public:
  HyperscanEngine(unsigned int flag, ThreadLocal::SlotAllocator& tls);
  Envoy::Regex::CompiledMatcherPtr matcher(const std::string& regex) const override;

private:
  unsigned int flag_{};
  ThreadLocal::SlotAllocator& tls_;
};

} // namespace Hyperscan
} // namespace Regex
} // namespace Extensions
} // namespace Envoy
