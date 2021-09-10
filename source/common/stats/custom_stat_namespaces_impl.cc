#include "source/common/stats/custom_stat_namespaces_impl.h"

namespace Envoy {
namespace Stats {

bool CustomStatNamespacesImpl::registered(const absl::string_view name) const {
  return namespaces_.find(name) != namespaces_.end();
}

void CustomStatNamespacesImpl::registerStatNamespace(const absl::string_view name) {
  namespaces_.insert(std::string(name));
};

absl::optional<absl::string_view>
CustomStatNamespacesImpl::stripRegisteredPrefix(const absl::string_view stat_name) const {
  const auto pos = stat_name.find_first_of('.');
  if (pos != std::string::npos && registered(stat_name.substr(0, pos))) {
    // Trim the custom namespace.
    return stat_name.substr(pos + 1);
  }
  return absl::nullopt;
};

} // namespace Stats
} // namespace Envoy
