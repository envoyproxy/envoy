#pragma once

#include "envoy/common/pure.h"
#include "envoy/upstream/upstream.h"

namespace Upstream {

/**
 * Abstract load balancing interface.
 */
class LoadBalancer {
public:
  virtual ~LoadBalancer() {}

  /**
   * Ask the load balancer for the next host to use depending on the underlying LB algorithm.
   */
  virtual ConstHostPtr chooseHost() PURE;
};

typedef std::unique_ptr<LoadBalancer> LoadBalancerPtr;

} // Upstream
