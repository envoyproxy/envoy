#pragma once

#include "envoy/matcher/matcher.h"
#include "envoy/thread_local/thread_local.h"

#include "absl/synchronization/mutex.h"
#include "contrib/envoy/extensions/matching/input_matchers/hyperscan/v3alpha/hyperscan.pb.h"
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

class Matcher : public Envoy::Matcher::InputMatcher {
public:
  explicit Matcher(
      const envoy::extensions::matching::input_matchers::hyperscan::v3alpha::Hyperscan& config,
      ThreadLocal::SlotAllocator& tls);
  ~Matcher() override { hs_free_database(database_); }

  // Envoy::Matcher::InputMatcher
  bool match(absl::optional<absl::string_view> input) override;

private:
  std::vector<unsigned int> flags_{};
  std::vector<unsigned int> ids_{};
  hs_database_t* database_{};
  ThreadLocal::TypedSlotPtr<ScratchThreadLocal> tls_;
};

} // namespace Hyperscan
} // namespace InputMatchers
} // namespace Matching
} // namespace Extensions
} // namespace Envoy
