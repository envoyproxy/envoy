#pragma once

#include "envoy/common/pure.h"
#include "envoy/upstream/upstream.h"

namespace Upstream {

/**
 * Context information passed to a load balancer to use when choosing a host. Not all load
 * balancers make use of all context information.
 */
class LoadBalancerContext {
public:
  virtual ~LoadBalancerContext() {}

  /**
   * @return const Optional<uint64_t>& the optional hash key to use during load balancing.
   */
  virtual const Optional<uint64_t>& hashKey() const PURE;
};

/**
 * Abstract load balancing interface.
 */
class LoadBalancer {
public:
  virtual ~LoadBalancer() {}

  /**
   * Ask the load balancer for the next host to use depending on the underlying LB algorithm.
   * @param context supplies the load balancer context. Not all load balancers make use of all
   *        context information. Load balancers should be written to assume that context information
   *        is missing and use sensible defaults.
   */
  virtual ConstHostPtr chooseHost(const LoadBalancerContext* context) PURE;
};

typedef std::unique_ptr<LoadBalancer> LoadBalancerPtr;

} // Upstream
