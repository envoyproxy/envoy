#pragma once

#include <cstdint>
#include <memory>

#include "envoy/common/pure.h"
#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/network/transport_socket.h"
#include "envoy/router/router.h"
#include "envoy/stream_info/stream_info.h"
#include "envoy/upstream/types.h"
#include "envoy/upstream/upstream.h"

#include "xds/data/orca/v3/orca_load_report.pb.h"

namespace Envoy {
namespace Server {
namespace Configuration {
class ServerFactoryContext;
} // namespace Configuration
} // namespace Server
namespace Http {
namespace ConnectionPool {
class ConnectionLifetimeCallbacks;
} // namespace ConnectionPool
} // namespace Http
namespace Upstream {

using ClusterProto = envoy::config::cluster::v3::Cluster;

/*
 * A handle to allow cancelation of asynchronous host selection.
 * If chooseHost returns a HostSelectionResponse with an AsyncHostSelectionHandle
 * handle, and the endpoint does not wish to receive onAsyncHostSelction call,
 * it must call cancel() on the provided handle.
 *
 * Please note that the AsyncHostSelectionHandle may be deleted after the
 * cancel() call. It is up to the implemention of the asynchronous load balancer
 * to ensure the cancelation state persists until the load balancer checks it.
 */
class AsyncHostSelectionHandle {
public:
  AsyncHostSelectionHandle& operator=(const AsyncHostSelectionHandle&) = delete;
  virtual ~AsyncHostSelectionHandle() = default;
  virtual void cancel() PURE;
};

/*
 * The response to a LoadBalancer::chooseHost call.
 *
 * chooseHost either returns a host directly or, in the case of asynchronous
 * load balancing, returns an AsyncHostSelectionHandle handle.
 *
 * If it returns a AsyncHostSelectionHandle handle, the load balancer guarantees an
 * eventual call to LoadBalancerContext::onAsyncHostSelction unless
 * AsyncHostSelectionHandle::cancel is called.
 */
struct HostSelectionResponse {
  HostSelectionResponse(HostConstSharedPtr host,
                        std::unique_ptr<AsyncHostSelectionHandle> cancelable = nullptr)
      : host(host), cancelable(std::move(cancelable)) {}
  HostSelectionResponse(HostConstSharedPtr host, std::string details)
      : host(host), details(details) {}
  HostConstSharedPtr host;
  // Optional details if host selection fails (empty string implies no details).
  std::string details;
  std::unique_ptr<AsyncHostSelectionHandle> cancelable;
};

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
   * @return const StreamInfo* the incoming request stream info or nullptr to use during load
   * balancing.
   */
  virtual StreamInfo::StreamInfo* requestStreamInfo() const PURE;

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
   * @param priority_mapping_func see @Upstream::RetryPriority::PriorityMappingFunc.
   * @return a reference to the priority load data that should be used to select a priority.
   *
   */
  virtual const HealthyAndDegradedLoad& determinePriorityLoad(
      const PrioritySet& priority_set, const HealthyAndDegradedLoad& original_priority_load,
      const Upstream::RetryPriority::PriorityMappingFunc& priority_mapping_func) PURE;

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
  virtual Network::TransportSocketOptionsConstSharedPtr upstreamTransportSocketOptions() const PURE;

  /**
   * Upstream override host. The first element is the target host address and the second element is
   * a boolean indicating whether the host should be selected strictly or not.
   * If the host should be selected strictly and no valid host is found, the load balancer should
   * return  nullptr.
   * If the host should not be selected strictly, the load balancer will select another host is the
   * target host is not valid.
   */
  using OverrideHost = std::pair<absl::string_view, bool>;
  /**
   * Returns the host the load balancer should select directly. If the expected host exists and
   * the host can be selected directly, the load balancer can bypass the load balancing algorithm
   * and return the corresponding host directly.
   */
  virtual absl::optional<OverrideHost> overrideHostToSelect() const PURE;

  /* Called by the load balancer when asynchronous host selection completes
   * @param host supplies the upstream host selected
   * @param details gives optional details about the resolution success/failure.
   */
  virtual void onAsyncHostSelection(HostConstSharedPtr&& host, std::string&& details) PURE;

  /**
   * Called by the load balancer to set the headers modifier that will be used to modify the
   * response headers before sending them downstream.
   * NOTE: this should be called only once per request, no matter how many times the retrying
   * happens.
   * @param modifier supplies the function that will modify the response headers.
   */
  virtual void setHeadersModifier(std::function<void(Http::ResponseHeaderMap&)> modifier) PURE;
};

/**
 * Identifies a specific connection within a pool.
 */
struct SelectedPoolAndConnection {
  Envoy::ConnectionPool::Instance& pool_;
  const Network::Connection& connection_;
};

/**
 * Abstract load balancing interface.
 */
class LoadBalancer {
public:
  virtual ~LoadBalancer() = default;

  /*
   * This is a convenience wrapper function for code which does not yet support
   * asynchronous host selection. It cancels any asynchronous lookup and treats
   * it as host selection failure.
   */
  static HostConstSharedPtr
  onlyAllowSynchronousHostSelection(HostSelectionResponse host_selection) {
    if (host_selection.cancelable) {
      // Async host selection not handled yet. Treat this as host selection
      // failure.
      host_selection.cancelable->cancel();
    }
    return std::move(host_selection.host);
  }

  /**
   * Ask the load balancer for the next host to use depending on the underlying LB algorithm.
   * @param context supplies the load balancer context. Not all load balancers make use of all
   *        context information. Load balancers should be written to assume that context information
   *        is missing and use sensible defaults.
   * @return a HostSelectionResponse either containing a host, or AsyncHostSelectionHandle handle.
   *
   * Please note that asynchronous host selection is not yet fully supported in
   * Envoy. It works for HTTP load balancing (with the notable exclusion of the
   * subset load balancer) but not for TCP proxy or other load balancers.
   * Updates to functionality should be reflected in load_balancing_policies.rst
   */
  virtual HostSelectionResponse chooseHost(LoadBalancerContext* context) PURE;

  /**
   * Returns a best effort prediction of the next host to be picked, or nullptr if not predictable.
   * Advances with subsequent calls, so while the first call will return the next host to be picked,
   * a subsequent call will return the second host to be picked.
   * @param context supplies the context which is used in host selection.
   */
  virtual HostConstSharedPtr peekAnotherHost(LoadBalancerContext* context) PURE;

  /**
   * Returns connection lifetime callbacks that may be used to inform the load balancer of
   * connection events. Load balancers which do not intend to track connection lifetime events
   * will return nullopt.
   * @return optional lifetime callbacks for this load balancer.
   */
  virtual OptRef<Envoy::Http::ConnectionPool::ConnectionLifetimeCallbacks> lifetimeCallbacks() PURE;

  /**
   * Returns a specific pool and existing connection to be used for the specified host.
   *
   * @return selected pool and connection to be used, or nullopt if no selection is made,
   *         for example if no matching connection is found.
   */
  virtual absl::optional<SelectedPoolAndConnection>
  selectExistingConnection(LoadBalancerContext* context, const Host& host,
                           std::vector<uint8_t>& hash_key) PURE;
};

using LoadBalancerPtr = std::unique_ptr<LoadBalancer>;

/**
 * Necessary parameters for creating a worker local load balancer.
 */
struct LoadBalancerParams {
  // The worker local priority set of the target cluster.
  const PrioritySet& priority_set;
  // The worker local priority set of the local cluster.
  const PrioritySet* local_priority_set{};
};

/**
 * Factory for load balancers.
 */
class LoadBalancerFactory {
public:
  virtual ~LoadBalancerFactory() = default;

  /**
   * @return LoadBalancerPtr a new worker local load balancer.
   */
  virtual LoadBalancerPtr create(LoadBalancerParams params) PURE;

  /**
   * @return bool whether the load balancer should be recreated when the host set changes.
   */
  virtual bool recreateOnHostChange() const { return true; }
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
  virtual absl::Status initialize() PURE;
};

using ThreadAwareLoadBalancerPtr = std::unique_ptr<ThreadAwareLoadBalancer>;

/*
 * Parsed load balancer configuration that will be used to create load balancer.
 */
class LoadBalancerConfig {
public:
  virtual ~LoadBalancerConfig() = default;

  /**
   * Optional method to allow a load balancer to validate endpoints before they're applied. If an
   * error is returned from this method, the endpoints are rejected. If this method does not return
   * an error, the load balancer must be able to use these endpoints in an update from the priority
   * set.
   */
  virtual absl::Status validateEndpoints(const PriorityState&) const { return absl::OkStatus(); }
};
using LoadBalancerConfigPtr = std::unique_ptr<LoadBalancerConfig>;

/**
 * Factory config for load balancers. To support a load balancing policy of
 * LOAD_BALANCING_POLICY_CONFIG, at least one load balancer factory corresponding to a policy in
 * load_balancing_policy must be registered with Envoy. Envoy will use the first policy for which
 * it has a registered factory.
 */
class TypedLoadBalancerFactory : public Config::TypedFactory {
public:
  ~TypedLoadBalancerFactory() override = default;

  /**
   * @return ThreadAwareLoadBalancerPtr a new thread-aware load balancer.
   *
   * @param lb_config supplies the parsed config of the load balancer.
   * @param cluster_info supplies the cluster info.
   * @param priority_set supplies the priority set on the main thread.
   * @param runtime supplies the runtime loader.
   * @param random supplies the random generator.
   * @param time_source supplies the time source.
   */
  virtual ThreadAwareLoadBalancerPtr
  create(OptRef<const LoadBalancerConfig> lb_config, const ClusterInfo& cluster_info,
         const PrioritySet& priority_set, Runtime::Loader& runtime, Random::RandomGenerator& random,
         TimeSource& time_source) PURE;

  /**
   * This method is used to validate and create load balancer config from typed proto config.
   *
   * @return LoadBalancerConfigPtr a new load balancer config or error.
   *
   * @param factory_context supplies the load balancer factory context.
   * @param config supplies the typed proto config of the load balancer. A dynamic_cast could
   *        be performed on the config to the expected proto type.
   */
  virtual absl::StatusOr<LoadBalancerConfigPtr>
  loadConfig(Server::Configuration::ServerFactoryContext& factory_context,
             const Protobuf::Message& config) PURE;

  /**
   * This method is used to validate and create load balancer config from legacy proto config.
   * This method is only used for backwards compatibility with the legacy cluster config.
   *
   * @return LoadBalancerConfigPtr a new load balancer config or error.
   *
   * @param factory_context supplies the load balancer factory context.
   * @param cluster supplies the legacy proto config of the cluster.
   */
  virtual absl::StatusOr<LoadBalancerConfigPtr>
  loadLegacy(Server::Configuration::ServerFactoryContext& factory_context,
             const ClusterProto& cluster) {
    UNREFERENCED_PARAMETER(cluster);
    UNREFERENCED_PARAMETER(factory_context);
    return nullptr;
  }

  std::string category() const override { return "envoy.load_balancing_policies"; }
};

} // namespace Upstream
} // namespace Envoy
