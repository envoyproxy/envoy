#include "source/common/stats/custom_namespace.h"

#include "envoy/registry/registry.h"

namespace Envoy {
namespace Stats {

void CustomStatNamespaceFactoryImpl::registerStatNamespace(const absl::string_view& name) {
  namespaces_.insert(std::string(name));
};

std::string
CustomStatNamespaceFactoryImpl::trySanitizeStatName(const absl::string_view& stat_name) const {
  const auto pos = stat_name.find_first_of('.');
  if (namespaces_.find(stat_name.substr(0, pos)) != namespaces_.end()) {
    // Trim the custom namespace.
    return std::string(stat_name.substr(pos + 1));
  }
  return "";
};

REGISTER_FACTORY(CustomStatNamespaceFactoryImpl, CustomStatNamespaceFactory);

} // namespace Stats
} // namespace Envoy
