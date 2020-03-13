#pragma once

#include <cstdint>
#include <memory>

#include "envoy/common/pure.h"
#include "envoy/network/transport_socket.h"
#include "envoy/router/router.h"
#include "envoy/upstream/types.h"
#include "envoy/upstream/upstream.h"

namespace Envoy {
namespace Upstream {

/**
 * Context information passed to a load balancer to use when choosing a host. Not all load
 * balancers make use of all context information.
 */
class LoadBalancerContext {
public:
  virtual ~LoadBalancerContext() = default;

  /**
   * Compute and return an optional hash key to use during load balancing. This
   * method may modify internal state so it should only be called once per
   * routing attempt.
   * @return absl::optional<uint64_t> the optional hash key to use.
   */
  virtual absl::optional<uint64_t> computeHashKey() PURE;

  /**
   * @return Router::MetadataMatchCriteria* metadata for use in selecting a subset of hosts
   *         during load balancing.
   */
  virtual const Router::MetadataMatchCriteria* metadataMatchCriteria() PURE;

  /**
   * @return const Network::Connection* the incoming connection or nullptr to use during load
   * balancing.
   */
  virtual const Network::Connection* downstreamConnection() const PURE;

  /**
   * @return const Http::HeaderMap* the incoming headers or nullptr to use during load
   * balancing.
   */
  virtual const Http::RequestHeaderMap* downstreamHeaders() const PURE;

  /**
   * Called to retrieve a reference to the priority load data that should be used when selecting a
   * priority. Implementations may return the provided original reference to make no changes, or
   * return a reference to alternative PriorityLoad held internally.
   *
   * @param priority_state current priority state of the cluster being being load balanced.
   * @param original_priority_load the cached priority load for the cluster being load balanced.
   * @return a reference to the priority load data that should be used to select a priority.
   *
   */
  virtual const HealthyAndDegradedLoad&
  determinePriorityLoad(const PrioritySet& priority_set,
                        const HealthyAndDegradedLoad& original_priority_load) PURE;

  /**
   * Called to determine whether we should reperform host selection. The load balancer
   * will retry host selection until either this function returns true or hostSelectionRetryCount is
   * reached.
   */
  virtual bool shouldSelectAnotherHost(const Host& host) PURE;

  /**
   * Called to determine how many times host selection should be retried until the filter is
   * ignored.
   */
  virtual uint32_t hostSelectionRetryCount() const PURE;

  /**
   * Returns the set of socket options which should be applied on upstream connections
   */
  virtual Network::Socket::OptionsSharedPtr upstreamSocketOptions() const PURE;

  /**
   * Returns the transport socket options which should be applied on upstream connections
   */
  virtual Network::TransportSocketOptionsSharedPtr upstreamTransportSocketOptions() const PURE;
};

/**
 * Abstract load balancing interface.
 */
class LoadBalancer {
public:
  virtual ~LoadBalancer() = default;

  /**
   * Ask the load balancer for the next host to use depending on the underlying LB algorithm.
   * @param context supplies the load balancer context. Not all load balancers make use of all
   *        context information. Load balancers should be written to assume that context information
   *        is missing and use sensible defaults.
   */
  virtual HostConstSharedPtr chooseHost(LoadBalancerContext* context) PURE;
};

using LoadBalancerPtr = std::unique_ptr<LoadBalancer>;

/**
 * Factory for load balancers.
 */
class LoadBalancerFactory {
public:
  virtual ~LoadBalancerFactory() = default;

  /**
   * @return LoadBalancerPtr a new load balancer.
   */
  virtual LoadBalancerPtr create() PURE;
};

using LoadBalancerFactorySharedPtr = std::shared_ptr<LoadBalancerFactory>;

/**
 * A thread aware load balancer is a load balancer that is global to all workers on behalf of a
 * cluster. These load balancers are harder to write so not every load balancer has to be one.
 * If a load balancer is a thread aware load balancer, the following semantics are used:
 * 1) A single instance is created on the main thread.
 * 2) The shared factory is passed to all workers.
 * 3) Every time there is a host set update on the main thread, all workers will create a new
 *    worker local load balancer via the factory.
 *
 * The above semantics mean that any global state in the factory must be protected by appropriate
 * locks. Additionally, the factory *must not* refer back to the owning thread aware load
 * balancer. If a cluster is removed via CDS, the thread aware load balancer can be destroyed
 * before cluster destruction reaches each worker. See the ring hash load balancer for one
 * example of how this pattern is used in practice. The common expected pattern is that the
 * factory will be consuming shared immutable state from the main thread
 *
 * TODO(mattklein123): The reason that locking is used in the above threading model vs. pure TLS
 * has to do with the lack of a TLS function that does the following:
 * 1) Create a per-worker data structure on the main thread. E.g., allocate 4 objects for 4
 *    workers.
 * 2) Then fan those objects out to each worker.
 * With the existence of a function like that, the callback locking from the worker to the main
 * thread could be removed. We can look at this in a follow up. The reality though is that the
 * locking is currently only used to protect some small bits of data on host set update and will
 * never be contended.
 */
class ThreadAwareLoadBalancer {
public:
  virtual ~ThreadAwareLoadBalancer() = default;

  /**
   * @return LoadBalancerFactorySharedPtr the shared factory to use for creating new worker local
   * load balancers.
   */
  virtual LoadBalancerFactorySharedPtr factory() PURE;

  /**
   * When a thread aware load balancer is constructed, it should return nullptr for any created
   * load balancer chooseHost() calls. Once initialize is called, the load balancer should
   * instantiate any needed structured and prepare for further updates. The cluster manager
   * will do this at the appropriate time.
   */
  virtual void initialize() PURE;
};

using ThreadAwareLoadBalancerPtr = std::unique_ptr<ThreadAwareLoadBalancer>;

} // namespace Upstream
} // namespace Envoy
