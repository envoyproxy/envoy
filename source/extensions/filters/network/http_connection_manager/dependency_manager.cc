#include "extensions/filters/network/http_connection_manager/dependency_manager.h"

#include "absl/container/flat_hash_set.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace HttpConnectionManager {

using envoy::extensions::filters::common::dependency::v3::Dependency;

bool DependencyManager::validDecodeDependencies() {
  using DependencyTuple = std::tuple<const std::string&, int>;
  absl::flat_hash_set<DependencyTuple> satisfied;

  for (const auto& [name, dependencies] : filter_chain_) {
    for (const auto& requirement : dependencies.decode_required()) {
      if (!satisfied.contains({requirement.name(), requirement.type()})) {
        return false;
      }
    }
    for (const auto& provided : dependencies.decode_provided()) {
      satisfied.insert({provided.name(), provided.type()});
    }
  }

  return true;
}

} // namespace HttpConnectionManager
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
