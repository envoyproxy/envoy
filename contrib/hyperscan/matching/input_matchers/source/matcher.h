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

struct Bound {
  Bound(uint64_t start, uint64_t end) : start_(start), end_(end) {}

  bool operator<(const Bound& other) const {
    if (start_ == other.start_) {
      return end_ > other.end_;
    }
    return start_ < other.start_;
  }

  uint64_t start_;
  uint64_t end_;
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

  void compile(const std::vector<const char*>& expressions, const std::vector<unsigned int>& flags,
               const std::vector<unsigned int>& ids, hs_database_t** database);
};

} // namespace Hyperscan
} // namespace InputMatchers
} // namespace Matching
} // namespace Extensions
} // namespace Envoy
