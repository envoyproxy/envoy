#pragma once

#include "envoy/router/router.h"

namespace Envoy {
namespace Router {

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
                               const ProtobufWkt::Struct& metadata_matches);

  const std::vector<MetadataMatchCriterionConstSharedPtr> metadata_match_criteria_;
};

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

} // namespace Router
} // namespace Envoy
