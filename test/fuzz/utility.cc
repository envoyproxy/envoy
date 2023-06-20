#include "test/fuzz/utility.h"

#include "source/common/common/logger.h"

#include "absl/strings/str_format.h"

namespace Envoy {
namespace Fuzz {

std::vector<std::string> fuzzFindDiffs(absl::string_view expected, absl::string_view actual) {
  std::vector<std::string> diffs;
  const uint32_t max_diffs = 5;
  if (expected.size() != actual.size()) {
    diffs.push_back(absl::StrCat("Size mismatch: ", expected.size(), " != ", actual.size()));
  }
  uint32_t min_size = std::min(expected.size(), actual.size());
  for (uint32_t i = 0; i < min_size && diffs.size() < max_diffs; ++i) {
    if (expected[i] != actual[i]) {
      diffs.push_back(absl::StrFormat("[%d]: %c(%u) != %c(%u)", i, expected[i], expected[i],
                                      actual[i], actual[i]));
    }
  }
  return diffs;
}

} // namespace Fuzz
} // namespace Envoy
