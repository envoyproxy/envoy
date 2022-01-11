#pragma once

#include "envoy/matcher/matcher.h"

#include "absl/synchronization/mutex.h"
#include "contrib/envoy/extensions/matching/input_matchers/hyperscan/v3alpha/hyperscan.pb.h"
#include "hs/hs.h"

namespace Envoy {
namespace Extensions {
namespace Matching {
namespace InputMatchers {
namespace Hyperscan {

class Matcher : public Envoy::Matcher::InputMatcher {
public:
  explicit Matcher(
      const envoy::extensions::matching::input_matchers::hyperscan::v3alpha::Hyperscan& config);
  ~Matcher() override {
    hs_free_scratch(scratch_);
    hs_free_database(database_);
  }

  // Envoy::Matcher::InputMatcher
  bool match(absl::optional<absl::string_view> input) override;

private:
  std::vector<unsigned int> flags_{};
  std::vector<unsigned int> ids_{};
  hs_database_t* database_{};
  hs_scratch_t* scratch_{};
  absl::Mutex scratch_mutex_{};
};

} // namespace Hyperscan
} // namespace InputMatchers
} // namespace Matching
} // namespace Extensions
} // namespace Envoy
