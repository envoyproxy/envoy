#include "extensions/filters/network/http_connection_manager/dependency_manager.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace HttpConnectionManager {

using envoy::extensions::filters::common::dependency::v3::Dependency;

bool DependencyManager::decodePathIsValid() {
  auto cmp = [](Dependency a, Dependency b) { return a.name() != b.name(); };
  std::set<Dependency, decltype(cmp)> satisfied(cmp);

  for (auto& [name, dependencies] : filter_chain_) {
    for (auto& requirement : dependencies.decode_required()) {
      if (satisfied.count(requirement) == 0) {
        return false;
      }
    }
    for (auto& provided : dependencies.decode_provided()) {
      satisfied.insert(provided);
    }
  }

  return true;
}

} // namespace HttpConnectionManager
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
