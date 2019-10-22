#pragma once

#include <memory>

#include "envoy/api/v2/listener/listener.pb.h"
#include "envoy/network/filter.h"
#include "envoy/server/filter_config.h"
#include "envoy/server/instance.h"
#include "envoy/server/listener_manager.h"
#include "envoy/server/transport_socket_config.h"
#include "envoy/server/worker.h"
#include "envoy/stats/scope.h"

#include "server/filter_chain_manager_impl.h"
#include "server/lds_api.h"
#include "server/listener_impl.h"

namespace Envoy {
namespace Server {

namespace Configuration {
class TransportSocketFactoryContextImpl;
}

class ListenerFilterChainFactoryBuilder;

/**
 * Prod implementation of ListenerComponentFactory that creates real sockets and attempts to fetch
 * sockets from the parent process via the hot restarter. The filter factory list is created from
 * statically registered filters.
 */
class ProdListenerComponentFactory : public ListenerComponentFactory,
                                     Logger::Loggable<Logger::Id::config> {
public:
  ProdListenerComponentFactory(Instance& server) : server_(server) {}

  /**
   * Static worker for createNetworkFilterFactoryList() that can be used directly in tests.
   */
  static std::vector<Network::FilterFactoryCb> createNetworkFilterFactoryList_(
      const Protobuf::RepeatedPtrField<envoy::api::v2::listener::Filter>& filters,
      Configuration::FactoryContext& context);
  /**
   * Static worker for createListenerFilterFactoryList() that can be used directly in tests.
   */
  static std::vector<Network::ListenerFilterFactoryCb> createListenerFilterFactoryList_(
      const Protobuf::RepeatedPtrField<envoy::api::v2::listener::ListenerFilter>& filters,
      Configuration::ListenerFactoryContext& context);

  /**
   * Static worker for createUdpListenerFilterFactoryList() that can be used directly in tests.
   */
  static std::vector<Network::UdpListenerFilterFactoryCb> createUdpListenerFilterFactoryList_(
      const Protobuf::RepeatedPtrField<envoy::api::v2::listener::ListenerFilter>& filters,
      Configuration::ListenerFactoryContext& context);

  // Server::ListenerComponentFactory
  LdsApiPtr createLdsApi(const envoy::api::v2::core::ConfigSource& lds_config) override {
    return std::make_unique<LdsApiImpl>(
        lds_config, server_.clusterManager(), server_.initManager(), server_.stats(),
        server_.listenerManager(), server_.messageValidationContext().dynamicValidationVisitor());
  }
  std::vector<Network::FilterFactoryCb> createNetworkFilterFactoryList(
      const Protobuf::RepeatedPtrField<envoy::api::v2::listener::Filter>& filters,
      Configuration::FactoryContext& context) override {
    return createNetworkFilterFactoryList_(filters, context);
  }
  std::vector<Network::ListenerFilterFactoryCb> createListenerFilterFactoryList(
      const Protobuf::RepeatedPtrField<envoy::api::v2::listener::ListenerFilter>& filters,
      Configuration::ListenerFactoryContext& context) override {
    return createListenerFilterFactoryList_(filters, context);
  }
  std::vector<Network::UdpListenerFilterFactoryCb> createUdpListenerFilterFactoryList(
      const Protobuf::RepeatedPtrField<envoy::api::v2::listener::ListenerFilter>& filters,
      Configuration::ListenerFactoryContext& context) override {
    return createUdpListenerFilterFactoryList_(filters, context);
  }
  Network::SocketSharedPtr createListenSocket(Network::Address::InstanceConstSharedPtr address,
                                              Network::Address::SocketType socket_type,
                                              const Network::Socket::OptionsSharedPtr& options,
                                              bool bind_to_port) override;
  DrainManagerPtr createDrainManager(envoy::api::v2::Listener::DrainType drain_type) override;
  uint64_t nextListenerTag() override { return next_listener_tag_++; }

private:
  Instance& server_;
  uint64_t next_listener_tag_{1};
};

class ListenerImpl;
using ListenerImplPtr = std::unique_ptr<ListenerImpl>;

/**
 * All listener manager stats. @see stats_macros.h
 */
#define ALL_LISTENER_MANAGER_STATS(COUNTER, GAUGE)                                                 \
  COUNTER(listener_added)                                                                          \
  COUNTER(listener_create_failure)                                                                 \
  COUNTER(listener_create_success)                                                                 \
  COUNTER(listener_modified)                                                                       \
  COUNTER(listener_removed)                                                                        \
  COUNTER(listener_stopped)                                                                        \
  GAUGE(total_listeners_active, NeverImport)                                                       \
  GAUGE(total_listeners_draining, NeverImport)                                                     \
  GAUGE(total_listeners_warming, NeverImport)

/**
 * Struct definition for all listener manager stats. @see stats_macros.h
 */
struct ListenerManagerStats {
  ALL_LISTENER_MANAGER_STATS(GENERATE_COUNTER_STRUCT, GENERATE_GAUGE_STRUCT)
};

/**
 * Implementation of ListenerManager.
 */
class ListenerManagerImpl : public ListenerManager, Logger::Loggable<Logger::Id::config> {
public:
  ListenerManagerImpl(Instance& server, ListenerComponentFactory& listener_factory,
                      WorkerFactory& worker_factory, bool enable_dispatcher_stats);

  void onListenerWarmed(ListenerImpl& listener);

  // Server::ListenerManager
  bool addOrUpdateListener(const envoy::api::v2::Listener& config, const std::string& version_info,
                           bool added_via_api) override;
  void createLdsApi(const envoy::api::v2::core::ConfigSource& lds_config) override {
    ASSERT(lds_api_ == nullptr);
    lds_api_ = factory_.createLdsApi(lds_config);
  }
  std::vector<std::reference_wrapper<Network::ListenerConfig>> listeners() override;
  uint64_t numConnections() override;
  bool removeListener(const std::string& listener_name) override;
  void startWorkers(GuardDog& guard_dog) override;
  void stopListeners(StopListenersType stop_listeners_type) override;
  void stopWorkers() override;
  Http::Context& httpContext() { return server_.httpContext(); }

  Instance& server_;
  ListenerComponentFactory& factory_;

private:
  using ListenerList = std::list<ListenerImplPtr>;

  struct DrainingListener {
    DrainingListener(ListenerImplPtr&& listener, uint64_t workers_pending_removal)
        : listener_(std::move(listener)), workers_pending_removal_(workers_pending_removal) {}

    ListenerImplPtr listener_;
    uint64_t workers_pending_removal_;
  };

  void addListenerToWorker(Worker& worker, ListenerImpl& listener);
  ProtobufTypes::MessagePtr dumpListenerConfigs();
  static ListenerManagerStats generateStats(Stats::Scope& scope);
  static bool hasListenerWithAddress(const ListenerList& list,
                                     const Network::Address::Instance& address);
  void updateWarmingActiveGauges() {
    // Using set() avoids a multiple modifiers problem during the multiple processes phase of hot
    // restart.
    stats_.total_listeners_warming_.set(warming_listeners_.size());
    stats_.total_listeners_active_.set(active_listeners_.size());
  }
  bool listenersStopped(const envoy::api::v2::Listener& config) {
    // Currently all listeners in a given direction are stopped because of the way admin
    // drain_listener functionality is implemented. This needs to be revisited, if that changes - if
    // we support drain by listener name,for example.
    return stop_listeners_type_ == StopListenersType::All ||
           (stop_listeners_type_ == StopListenersType::InboundOnly &&
            config.traffic_direction() == envoy::api::v2::core::TrafficDirection::INBOUND);
  }

  /**
   * Mark a listener for draining. The listener will no longer be considered active but will remain
   * present to allow connection draining.
   * @param listener supplies the listener to drain.
   */
  void drainListener(ListenerImplPtr&& listener);

  /**
   * Get a listener by name. This routine is used because listeners have inherent order in static
   * configuration and especially for tests. Thus, we can't use a map.
   * @param listeners supplies the listener list to look in.
   * @param name supplies the name to search for.
   */
  ListenerList::iterator getListenerByName(ListenerList& listeners, const std::string& name);

  // Active listeners are listeners that are currently accepting new connections on the workers.
  ListenerList active_listeners_;
  // Warming listeners are listeners that may need further initialization via the listener's init
  // manager. For example, RDS, or in the future KDS. Once a listener is done warming it will
  // be transitioned to active.
  ListenerList warming_listeners_;
  // Draining listeners are listeners that are in the process of being drained and removed. They
  // go through two phases where first the workers stop accepting new connections and existing
  // connections are drained. Then after that time period the listener is removed from all workers
  // and any remaining connections are closed.
  std::list<DrainingListener> draining_listeners_;
  std::list<WorkerPtr> workers_;
  bool workers_started_{};
  absl::optional<StopListenersType> stop_listeners_type_;
  Stats::ScopePtr scope_;
  ListenerManagerStats stats_;
  ConfigTracker::EntryOwnerPtr config_tracker_entry_;
  LdsApiPtr lds_api_;
  const bool enable_dispatcher_stats_{};
};

class ListenerFilterChainFactoryBuilder : public FilterChainFactoryBuilder {
public:
  ListenerFilterChainFactoryBuilder(
      ListenerImpl& listener, Configuration::TransportSocketFactoryContextImpl& factory_context);
  std::unique_ptr<Network::FilterChain>
  buildFilterChain(const ::envoy::api::v2::listener::FilterChain& filter_chain) const override;

private:
  ListenerImpl& parent_;
  Configuration::TransportSocketFactoryContextImpl& factory_context_;
};

} // namespace Server
} // namespace Envoy
