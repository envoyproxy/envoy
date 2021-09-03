#include "source/common/stats/custom_namespace.h"

#include "source/common/common/macros.h"

namespace Envoy {
namespace Stats {

bool CustomStatNamespaces::registered(const absl::string_view& name) {
  return namespaces_.find(name) != namespaces_.end();
}

void CustomStatNamespaces::registerStatNamespace(const absl::string_view& name) {
  namespaces_.insert(std::string(name));
};

std::string CustomStatNamespaces::trySanitizeStatName(const absl::string_view& stat_name) const {
  const auto pos = stat_name.find_first_of('.');
  if (namespaces_.find(stat_name.substr(0, pos)) != namespaces_.end()) {
    // Trim the custom namespace.
    return std::string(stat_name.substr(pos + 1));
  }
  return "";
};

CustomStatNamespaces& getCustomStatNamespaces() {
  MUTABLE_CONSTRUCT_ON_FIRST_USE(CustomStatNamespaces);
}

} // namespace Stats
} // namespace Envoy
