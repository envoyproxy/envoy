#include "source/common/stats/custom_stat_namespaces_impl.h"

#include <vector>

#include "source/common/common/assert.h"
#include "source/common/common/thread.h"

#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"

namespace Envoy {
namespace Stats {

bool CustomStatNamespacesImpl::registered(const absl::string_view name) const {
  ASSERT_IS_MAIN_OR_TEST_THREAD();
  return namespaces_.find(name) != namespaces_.end();
}

void CustomStatNamespacesImpl::registerStatNamespace(const absl::string_view name) {
  ASSERT_IS_MAIN_OR_TEST_THREAD();
  namespaces_.insert(std::string(name));
};

absl::optional<absl::string_view>
CustomStatNamespacesImpl::stripRegisteredPrefix(const absl::string_view stat_name) const {
  ASSERT_IS_MAIN_OR_TEST_THREAD();
  if (!namespaces_.empty()) {
    const auto pos = stat_name.find_first_of('.');
    if (pos != std::string::npos && registered(stat_name.substr(0, pos))) {
      // Trim the custom namespace.
      return stat_name.substr(pos + 1);
    }
  }
  return absl::nullopt;
};

absl::optional<std::string>
CustomStatNamespacesImpl::stripRegisteredInnerNamespace(absl::string_view stat_name) const {
  ASSERT_IS_MAIN_OR_TEST_THREAD();
  if (namespaces_.empty()) {
    return absl::nullopt;
  }

  std::vector<absl::string_view> segments = absl::StrSplit(stat_name, '.');
  // Need at least scope.namespace.leaf for an inner segment to exist.
  if (segments.size() < 3) {
    return absl::nullopt;
  }

  // Skip the leading scope segment and the trailing leaf.
  for (size_t i = 1; i + 1 < segments.size(); ++i) {
    if (registered(segments[i])) {
      segments.erase(segments.begin() + i);
      return absl::StrJoin(segments, ".");
    }
  }
  return absl::nullopt;
}

} // namespace Stats
} // namespace Envoy
