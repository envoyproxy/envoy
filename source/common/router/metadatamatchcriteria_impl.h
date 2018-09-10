#pragma once

#include "envoy/router/router.h"

namespace Envoy {
namespace Router {

class MetadataMatchCriterionImpl : public MetadataMatchCriterion {
public:
  MetadataMatchCriterionImpl(const std::string& name, const HashedValue& value)
      : name_(name), value_(value) {}

  const std::string& name() const override { return name_; }
  const HashedValue& value() const override { return value_; }

private:
  const std::string name_;
  const HashedValue value_;
};


class MetadataMatchCriteriaImpl;
typedef std::unique_ptr<const MetadataMatchCriteriaImpl> MetadataMatchCriteriaImplConstPtr;

class MetadataMatchCriteriaImpl : public MetadataMatchCriteria {
public:
  MetadataMatchCriteriaImpl(const ProtobufWkt::Struct& metadata_matches)
      : metadata_match_criteria_(extractMetadataMatchCriteria(nullptr, metadata_matches)){};

  MetadataMatchCriteriaConstPtr
  mergeMatchCriteria(const ProtobufWkt::Struct& metadata_matches) const override {
    return MetadataMatchCriteriaImplConstPtr(
        new MetadataMatchCriteriaImpl(extractMetadataMatchCriteria(this, metadata_matches)));
  }

  // MetadataMatchCriteria
  const std::vector<MetadataMatchCriterionConstSharedPtr>& metadataMatchCriteria() const override {
    return metadata_match_criteria_;
  }

private:
  MetadataMatchCriteriaImpl(const std::vector<MetadataMatchCriterionConstSharedPtr>& criteria)
      : metadata_match_criteria_(criteria){};

  static std::vector<MetadataMatchCriterionConstSharedPtr>
  extractMetadataMatchCriteria(const MetadataMatchCriteriaImpl* parent,
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

  const std::vector<MetadataMatchCriterionConstSharedPtr> metadata_match_criteria_;
};

} // namespace Router
} // namespace Envoy
