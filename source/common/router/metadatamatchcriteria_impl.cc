#include "common/router/metadatamatchcriteria_impl.h"

namespace Envoy {
namespace Router {
std::vector<MetadataMatchCriterionConstSharedPtr>
MetadataMatchCriteriaImpl::extractMetadataMatchCriteria(const MetadataMatchCriteriaImpl* parent,
                                                        const ProtobufWkt::Struct& matches) {
  std::vector<MetadataMatchCriterionConstSharedPtr> v;

  // Track locations of each name (from the parent) in v to make it
  // easier to replace them when the same name exists in matches.
  std::unordered_map<std::string, std::size_t> existing;

  if (parent) {
    for (const auto& it : parent->metadata_match_criteria_) {
      // v.size() is the index of the emplaced name.
      existing.emplace(it->name(), v.size());
      v.emplace_back(it);
    }
  }

  // Add values from matches, replacing name/values copied from parent.
  for (const auto it : matches.fields()) {
    const auto index_it = existing.find(it.first);
    if (index_it != existing.end()) {
      v[index_it->second] = std::make_shared<MetadataMatchCriterionImpl>(it.first, it.second);
    } else {
      v.emplace_back(std::make_shared<MetadataMatchCriterionImpl>(it.first, it.second));
    }
  }

  // Sort criteria by name to speed matching in the subset load balancer.
  // See source/docs/subset_load_balancer.md.
  std::sort(
      v.begin(), v.end(),
      [](const MetadataMatchCriterionConstSharedPtr& a,
         const MetadataMatchCriterionConstSharedPtr& b) -> bool { return a->name() < b->name(); });

  return v;
}
} // namespace Router
} // namespace Envoy
