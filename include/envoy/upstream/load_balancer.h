#pragma once

#include <cstdint>
#include <memory>

#include "envoy/common/pure.h"
#include "envoy/router/router.h"
#include "envoy/upstream/upstream.h"

namespace Envoy {
namespace Upstream {

/**
 * Context information passed to a load balancer to use when choosing a host. Not all load
 * balancers make use of all context information.
 */
class LoadBalancerContext {
public:
  virtual ~LoadBalancerContext() {}

  /**
   * Compute and return an optional hash key to use during load balancing. This
   * method may modify internal state so it should only be called once per
   * routing attempt.
   * @return Optional<uint64_t> the optional hash key to use.
   */
  virtual Optional<uint64_t> computeHashKey() PURE;

  /**
   * @return Router::MetadataMatchCriteria* metadata for use in selecting a subset of hosts
   *         during load balancing.
   */
  virtual const Router::MetadataMatchCriteria* metadataMatchCriteria() const PURE;

  /**
   * @return const Network::Connection* the incoming connection or nullptr to use during load
   * balancing.
   */
  virtual const Network::Connection* downstreamConnection() const PURE;
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
  virtual HostConstSharedPtr chooseHost(LoadBalancerContext* context) PURE;
};

typedef std::unique_ptr<LoadBalancer> LoadBalancerPtr;

} // namespace Upstream
} // namespace Envoy
