#pragma once

#include "envoy/common/regex.h"
#include "envoy/matcher/matcher.h"
#include "envoy/thread_local/thread_local.h"

#include "hs/hs.h"

namespace Envoy {
namespace Extensions {
namespace Matching {
namespace InputMatchers {
namespace Hyperscan {

struct ScratchThreadLocal : public ThreadLocal::ThreadLocalObject {
  explicit ScratchThreadLocal(const hs_database_t* database);
  ~ScratchThreadLocal() override { hs_free_scratch(scratch_); }
  hs_scratch_t* scratch_{};
};

class Matcher : public Envoy::Regex::CompiledMatcher, public Envoy::Matcher::InputMatcher {
public:
  Matcher(const std::vector<const char*>& expressions, const std::vector<unsigned int>& flags,
          const std::vector<unsigned int>& ids, ThreadLocal::SlotAllocator& tls);
  ~Matcher() override { hs_free_database(database_); }

  // Envoy::Regex::CompiledMatcher
  bool match(absl::string_view value) const override;
  std::string replaceAll(absl::string_view value, absl::string_view substitution) const override;

  // Envoy::Matcher::InputMatcher
  bool match(absl::optional<absl::string_view> input) override;

private:
  hs_database_t* database_{};
  ThreadLocal::TypedSlotPtr<ScratchThreadLocal> tls_;
};

} // namespace Hyperscan
} // namespace InputMatchers
} // namespace Matching
} // namespace Extensions
} // namespace Envoy
