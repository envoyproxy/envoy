#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <list>
#include <map>
#include <memory>
#include <string>

#include "common/protobuf/protobuf.h"
#include "common/protobuf/protobuf_utility.h"

namespace Envoy {
namespace Router {

class MetadataMatchCriterion {
public:
  virtual ~MetadataMatchCriterion() = default;

  /*
   * @return const std::string& the name of the metadata key
   */
  virtual const std::string& name() const PURE;

  /*
   * @return const Envoy::HashedValue& the value for the metadata key
   */
  virtual const HashedValue& value() const PURE;
};

using MetadataMatchCriterionConstSharedPtr = std::shared_ptr<const MetadataMatchCriterion>;

class MetadataMatchCriteria;
using MetadataMatchCriteriaConstPtr = std::unique_ptr<const MetadataMatchCriteria>;

class MetadataMatchCriteria {
public:
  virtual ~MetadataMatchCriteria() = default;

  /*
   * @return std::vector<MetadataMatchCriterionConstSharedPtr>& a vector of
   * metadata to be matched against upstream endpoints when load
   * balancing, sorted lexically by name.
   */
  virtual const std::vector<MetadataMatchCriterionConstSharedPtr>&
  metadataMatchCriteria() const PURE;

  /**
   * Creates a new MetadataMatchCriteria, merging existing
   * metadata criteria with the provided criteria. The result criteria is the
   * combination of both sets of criteria, with those from the metadata_matches
   * ProtobufWkt::Struct taking precedence.
   * @param metadata_matches supplies the new criteria.
   * @return MetadataMatchCriteriaConstPtr the result criteria.
   */
  virtual MetadataMatchCriteriaConstPtr
  mergeMatchCriteria(const ProtobufWkt::Struct& metadata_matches) const PURE;

  /**
   * Creates a new MetadataMatchCriteria with criteria vector reduced to given names
   * @param names names of metadata keys to preserve
   * @return MetadataMatchCriteriaConstPtr the result criteria. Returns nullptr if the result
   * criteria are empty.
   */
  virtual MetadataMatchCriteriaConstPtr
  filterMatchCriteria(const std::set<std::string>& names) const PURE;
};

class UpstreamEndpointInfo {
public:
  virtual ~UpstreamEndpointInfo() = default;

  /**
   * @return const std::string& the upstream cluster that owns the route.
   */
  virtual const std::string& clusterName() const PURE;

  /**
   * @return MetadataMatchCriteria* the metadata that a subset load balancer should match when
   * selecting an upstream host
   */
  virtual const MetadataMatchCriteria* metadataMatchCriteria() const PURE;
};

using UpstreamEndpointInfoConstSharedPtr = std::shared_ptr<const UpstreamEndpointInfo>;
} // namespace Router
} // namespace Envoy
