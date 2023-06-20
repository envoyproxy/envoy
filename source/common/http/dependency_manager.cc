#include "source/common/http/dependency_manager.h"

#include "absl/container/flat_hash_set.h"
#include "absl/strings/substitute.h"

namespace Envoy {
namespace Http {

using envoy::extensions::filters::common::dependency::v3::Dependency;

absl::Status DependencyManager::validDecodeDependencies() {
  using DependencyTuple = std::tuple<const std::string&, int>;
  absl::flat_hash_set<DependencyTuple> satisfied;

  for (const auto& [name, dependencies] : filter_chain_) {
    for (const auto& requirement : dependencies.decode_required()) {
      if (!satisfied.contains({requirement.name(), requirement.type()})) {
        return absl::NotFoundError(absl::Substitute(
            "Dependency violation: filter '$0' requires a $1 named '$2'", name,
            Dependency::DependencyType_Name(requirement.type()), requirement.name()));
      }
    }
    for (const auto& provided : dependencies.decode_provided()) {
      satisfied.insert({provided.name(), provided.type()});
    }
  }

  return absl::OkStatus();
}

} // namespace Http
} // namespace Envoy
