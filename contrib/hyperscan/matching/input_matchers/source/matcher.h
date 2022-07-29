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
  ScratchThreadLocal(const hs_database_t* database, const hs_database_t* start_of_match_database);
  ~ScratchThreadLocal() override;

  hs_scratch_t* scratch_{};
};

struct Bound {
  Bound(uint64_t begin, uint64_t end);

  bool operator<(const Bound& other) const;

  uint64_t begin_;
  uint64_t end_;
};

class Matcher : public Envoy::Regex::CompiledMatcher, public Envoy::Matcher::InputMatcher {
public:
  Matcher(const std::vector<const char*>& expressions, const std::vector<unsigned int>& flags,
          const std::vector<unsigned int>& ids, ThreadLocal::SlotAllocator& tls,
          bool report_start_of_matching);
  ~Matcher() override;

  // Envoy::Regex::CompiledMatcher
  bool match(absl::string_view value) const override;
  std::string replaceAll(absl::string_view value, absl::string_view substitution) const override;

  // Envoy::Matcher::InputMatcher
  bool match(absl::optional<absl::string_view> input) override;

private:
  hs_database_t* database_{};
  hs_database_t* start_of_match_database_{};
  ThreadLocal::TypedSlotPtr<ScratchThreadLocal> tls_;

  // Compiles the Hyperscan database. It will throw on failure of insufficient memory or malformed
  // regex patterns and flags. Vector parameters should have the same size.
  void compile(const std::vector<const char*>& expressions, const std::vector<unsigned int>& flags,
               const std::vector<unsigned int>& ids, hs_database_t** database);
};

} // namespace Hyperscan
} // namespace InputMatchers
} // namespace Matching
} // namespace Extensions
} // namespace Envoy
