#pragma once

#include <string>
#include <vector>

#include "common/protobuf/protobuf.h"

#include "api/cds.pb.h"

namespace Envoy {
namespace Upstream {

/**
 * Type of load balancing to perform.
 */
enum class LoadBalancerType { RoundRobin, LeastRequest, Random, RingHash, OriginalDst };

class LoadBalancerSubsetInfo {
public:
  virtual ~LoadBalancerSubsetInfo() {}

  virtual bool isEnabled() const PURE;

  virtual envoy::api::v2::Cluster::LbSubsetConfig::LbSubsetFallbackPolicy
  fallbackPolicy() const PURE;

  virtual const ProtobufWkt::Struct& defaultSubset() const PURE;

  /*
   * @return const std:vector<std:vector<std::string>>& a vector of
   * keys used to define load balancer subsets. The inner vector is
   * lexically sorted.
   */
  virtual const std::vector<std::vector<std::string>>& subsetKeys() const PURE;
};

} // namespace Upstream
} // namespace Envoy
