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
  ScratchThreadLocal(const hs_database_t* database, const hs_database_t* som_database);
  ~ScratchThreadLocal() override { hs_free_scratch(scratch_); }

  hs_scratch_t* scratch_{};
};

struct Matched {
  Matched(unsigned long long begin, unsigned long long end) : begin_(begin), end_(end) {}

  bool operator<(const Matched& other) const {
    if (begin_ == other.begin_) {
      return end_ > other.end_;
    }
    return begin_ < other.begin_;
  }

  unsigned long long begin_;
  unsigned long long end_;
};

class Matcher : public Envoy::Regex::CompiledMatcher, public Envoy::Matcher::InputMatcher {
public:
  Matcher(const std::vector<const char*>& expressions, const std::vector<unsigned int>& flags,
          const std::vector<unsigned int>& ids, ThreadLocal::SlotAllocator& tls, bool som);
  ~Matcher() override {
    hs_free_database(database_);
    hs_free_database(som_database_);
  }

  // Envoy::Regex::CompiledMatcher
  bool match(absl::string_view value) const override;
  std::string replaceAll(absl::string_view value, absl::string_view substitution) const override;

  // Envoy::Matcher::InputMatcher
  bool match(absl::optional<absl::string_view> input) override;

private:
  hs_database_t* database_{};
  hs_database_t* som_database_{};
  ThreadLocal::TypedSlotPtr<ScratchThreadLocal> tls_;
};

} // namespace Hyperscan
} // namespace InputMatchers
} // namespace Matching
} // namespace Extensions
} // namespace Envoy
