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

using DatabaseSharedPtr = std::shared_ptr<hs_database_t>;

struct ScratchThreadLocal : public ThreadLocal::ThreadLocalObject {
  ScratchThreadLocal(DatabaseSharedPtr database, DatabaseSharedPtr start_of_match_database);
  ~ScratchThreadLocal() override;

  DatabaseSharedPtr database_;
  DatabaseSharedPtr start_of_match_database_;
  hs_scratch_t* scratch_{};
};

using ScratchThreadLocalPtr = std::unique_ptr<ScratchThreadLocal>;

struct Bound {
  Bound(uint64_t begin, uint64_t end);

  bool operator<(const Bound& other) const;

  uint64_t begin_;
  uint64_t end_;
};

class Matcher : public Envoy::Regex::CompiledMatcher, public Envoy::Matcher::InputMatcher {
public:
  Matcher(const std::vector<const char*>& expressions, const std::vector<unsigned int>& flags,
          const std::vector<unsigned int>& ids, Event::Dispatcher& main_thread_dispatcher,
          ThreadLocal::SlotAllocator& tls, bool report_start_of_matching);
  ~Matcher() override;

  // Envoy::Regex::CompiledMatcher
  bool match(absl::string_view value) const override;
  bool match(absl::string_view value, const Context&) const override { return match(value); }
  std::string replaceAll(absl::string_view value, absl::string_view substitution) const override;

  // Envoy::Matcher::InputMatcher
  ::Envoy::Matcher::MatchResult match(const ::Envoy::Matcher::DataInputGetResult& input) override;

  const std::string& pattern() const override { return EMPTY_STRING; }

private:
  DatabaseSharedPtr database_;
  DatabaseSharedPtr start_of_match_database_;
  Event::Dispatcher& main_thread_dispatcher_;
  ThreadLocal::TypedSlotPtr<ScratchThreadLocal> tls_;

  // Compiles the Hyperscan database. It will throw on failure of insufficient memory or malformed
  // regex patterns and flags. Vector parameters should have the same size.
  void compile(const std::vector<const char*>& expressions, const std::vector<unsigned int>& flags,
               const std::vector<unsigned int>& ids, DatabaseSharedPtr& database);

  hs_scratch_t* getScratch(ScratchThreadLocalPtr& local_scratch) const;
};

} // namespace Hyperscan
} // namespace InputMatchers
} // namespace Matching
} // namespace Extensions
} // namespace Envoy
