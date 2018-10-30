#pragma once

#include <set>
#include <string>
#include <vector>

#include "envoy/api/v2/cds.pb.h"
#include "envoy/common/pure.h"

#include "common/protobuf/protobuf.h"

namespace Envoy {
namespace Upstream {

/**
 * Type of load balancing to perform.
 */
enum class LoadBalancerType { RoundRobin, LeastRequest, Random, RingHash, OriginalDst, Maglev };

/**
 * Load Balancer subset configuration.
 */
class LoadBalancerSubsetInfo {
public:
  virtual ~LoadBalancerSubsetInfo() {}

  /**
   * @return bool true if load balancer subsets are configured.
   */
  virtual bool isEnabled() const PURE;

  /**
   * @return LbSubsetFallbackPolicy the fallback policy used when
   * route metadata does not match any subset.
   */
  virtual envoy::api::v2::Cluster::LbSubsetConfig::LbSubsetFallbackPolicy
  fallbackPolicy() const PURE;

  /**
   * @return ProtobufWkt::Struct the struct describing the metadata for a
   *         host to be included in the default subset.
   */
  virtual const ProtobufWkt::Struct& defaultSubset() const PURE;

  /*
   * @return const std:vector<std:set<std::string>>& a vector of
   * sorted keys used to define load balancer subsets.
   */
  virtual const std::vector<std::set<std::string>>& subsetKeys() const PURE;

  /*
   * @return bool whether routing to subsets should take locality weights into account.
   */
  virtual bool localityWeightAware() const PURE;
};

} // namespace Upstream
} // namespace Envoy
