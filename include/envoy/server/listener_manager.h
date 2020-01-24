#pragma once

#include <vector>

#include "envoy/admin/v3/config_dump.pb.h"
#include "envoy/config/core/v3/config_source.pb.h"
#include "envoy/config/listener/v3/listener.pb.h"
#include "envoy/config/listener/v3/listener_components.pb.h"
#include "envoy/network/filter.h"
#include "envoy/network/listen_socket.h"
#include "envoy/network/listener.h"
#include "envoy/server/api_listener.h"
#include "envoy/server/drain_manager.h"
#include "envoy/server/filter_config.h"
#include "envoy/server/guarddog.h"

#include "common/protobuf/protobuf.h"

namespace Envoy {
namespace Server {

/**
 * Interface for an LDS API provider.
 */
class LdsApi {
public:
  virtual ~LdsApi() = default;

  /**
   * @return std::string the last received version by the xDS API for LDS.
   */
  virtual std::string versionInfo() const PURE;
};

using LdsApiPtr = std::unique_ptr<LdsApi>;

struct ListenSocketCreationParams {
  ListenSocketCreationParams(bool bind_to_port, bool duplicate_parent_socket = true)
      : bind_to_port(bind_to_port), duplicate_parent_socket(duplicate_parent_socket) {}

  // For testing.
  bool operator==(const ListenSocketCreationParams& rhs) const;
  bool operator!=(const ListenSocketCreationParams& rhs) const;

  // whether to actually bind the socket.
  bool bind_to_port;
  // whether to duplicate socket from hot restart parent.
  bool duplicate_parent_socket;
};

/**
 * Factory for creating listener components.
 */
class ListenerComponentFactory {
public:
  virtual ~ListenerComponentFactory() = default;

  /**
   * @return an LDS API provider.
   * @param lds_config supplies the management server configuration.
   */
  virtual LdsApiPtr createLdsApi(const envoy::config::core::v3::ConfigSource& lds_config) PURE;

  /**
   * Creates a socket.
   * @param address supplies the socket's address.
   * @param socket_type the type of socket (stream or datagram) to create.
   * @param options to be set on the created socket just before calling 'bind()'.
   * @param params used to control how a socket being created.
   * @return Network::SocketSharedPtr an initialized and potentially bound socket.
   */
  virtual Network::SocketSharedPtr
  createListenSocket(Network::Address::InstanceConstSharedPtr address,
                     Network::Address::SocketType socket_type,
                     const Network::Socket::OptionsSharedPtr& options,
                     const ListenSocketCreationParams& params) PURE;

  /**
   * Creates a list of filter factories.
   * @param filters supplies the proto configuration.
   * @param context supplies the factory creation context.
   * @return std::vector<Network::FilterFactoryCb> the list of filter factories.
   */
  virtual std::vector<Network::FilterFactoryCb> createNetworkFilterFactoryList(
      const Protobuf::RepeatedPtrField<envoy::config::listener::v3::Filter>& filters,
      Server::Configuration::FilterChainFactoryContext& filter_chain_factory_context) PURE;

  /**
   * Creates a list of listener filter factories.
   * @param filters supplies the JSON configuration.
   * @param context supplies the factory creation context.
   * @return std::vector<Network::ListenerFilterFactoryCb> the list of filter factories.
   */
  virtual std::vector<Network::ListenerFilterFactoryCb> createListenerFilterFactoryList(
      const Protobuf::RepeatedPtrField<envoy::config::listener::v3::ListenerFilter>& filters,
      Configuration::ListenerFactoryContext& context) PURE;

  /**
   * Creates a list of UDP listener filter factories.
   * @param filters supplies the configuration.
   * @param context supplies the factory creation context.
   * @return std::vector<Network::UdpListenerFilterFactoryCb> the list of filter factories.
   */
  virtual std::vector<Network::UdpListenerFilterFactoryCb> createUdpListenerFilterFactoryList(
      const Protobuf::RepeatedPtrField<envoy::config::listener::v3::ListenerFilter>& filters,
      Configuration::ListenerFactoryContext& context) PURE;

  /**
   * @return DrainManagerPtr a new drain manager.
   * @param drain_type supplies the type of draining to do for the owning listener.
   */
  virtual DrainManagerPtr
  createDrainManager(envoy::config::listener::v3::Listener::DrainType drain_type) PURE;

  /**
   * @return uint64_t a listener tag usable for connection handler tracking.
   */
  virtual uint64_t nextListenerTag() PURE;
};

/**
 * A manager for all listeners and all threaded connection handling workers.
 */
class ListenerManager {
public:
  // Indicates listeners to stop.
  enum class StopListenersType {
    // Listeners in the inbound direction are only stopped.
    InboundOnly,
    // All listeners are stopped.
    All,
  };

  virtual ~ListenerManager() = default;

  /**
   * Add or update a listener. Listeners are referenced by a unique name. If no name is provided,
   * the manager will allocate a UUID. Listeners that expect to be dynamically updated should
   * provide a unique name. The manager will search by name to find the existing listener that
   * should be updated. The new listener must have the same configured address. The old listener
   * will be gracefully drained once the new listener is ready to take traffic (e.g. when RDS has
   * been initialized).
   * @param config supplies the configuration proto.
   * @param version_info supplies the xDS version of the listener.
   * @param modifiable supplies whether the added listener can be updated or removed. If the
   *        listener is not modifiable, future calls to this function or removeListener() on behalf
   *        of this listener will return false.
   * @return TRUE if a listener was added or FALSE if the listener was not updated because it is
   *         a duplicate of the existing listener. This routine will throw an EnvoyException if
   *         there is a fundamental error preventing the listener from being added or updated.
   */
  virtual bool addOrUpdateListener(const envoy::config::listener::v3::Listener& config,
                                   const std::string& version_info, bool modifiable) PURE;

  /**
   * Instruct the listener manager to create an LDS API provider. This is a separate operation
   * during server initialization because the listener manager is created prior to several core
   * pieces of the server existing.
   * @param lds_config supplies the management server configuration.
   */
  virtual void createLdsApi(const envoy::config::core::v3::ConfigSource& lds_config) PURE;

  /**
   * @return std::vector<std::reference_wrapper<Network::ListenerConfig>> a list of the currently
   * loaded listeners. Note that this routine returns references to the existing listeners. The
   * references are only valid in the context of the current call stack and should not be stored.
   */
  virtual std::vector<std::reference_wrapper<Network::ListenerConfig>> listeners() PURE;

  /**
   * @return uint64_t the total number of connections owned by all listeners across all workers.
   */
  virtual uint64_t numConnections() const PURE;

  /**
   * Remove a listener by name.
   * @param name supplies the listener name to remove.
   * @return TRUE if the listener was found and removed. Note that when this routine returns TRUE,
   * the listener has not necessarily been actually deleted right away. The listener will be
   * drained and fully removed at some later time.
   */
  virtual bool removeListener(const std::string& name) PURE;

  /**
   * Start all workers accepting new connections on all added listeners.
   * @param guard_dog supplies the guard dog to use for thread watching.
   */
  virtual void startWorkers(GuardDog& guard_dog) PURE;

  /**
   * Stop all listeners from accepting new connections without actually removing any of them. This
   * is used for server draining and /drain_listeners admin endpoint. This method directly stops the
   * listeners on workers. Once a listener is stopped, any listener modifications are not allowed.
   * @param stop_listeners_type indicates listeners to stop.
   */
  virtual void stopListeners(StopListenersType stop_listeners_type) PURE;

  /**
   * Stop all threaded workers from running. When this routine returns all worker threads will
   * have exited.
   */
  virtual void stopWorkers() PURE;

  /*
   * Warn the listener manager of an impending update. This allows the listener to clear per-update
   * state.
   */
  virtual void beginListenerUpdate() PURE;

  /*
   * Inform the listener manager that the update has completed, and informs the listener of any
   * errors handled by the reload source.
   */
  using FailureStates = std::vector<std::unique_ptr<envoy::admin::v3::UpdateFailureState>>;
  virtual void endListenerUpdate(FailureStates&& failure_states) PURE;

  // TODO(junr03): once ApiListeners support warming and draining, this function should return a
  // weak_ptr to its caller. This would allow the caller to verify if the
  // ApiListener is available to receive API calls on it.
  /**
   * @return the server's API Listener if it exists, nullopt if it does not.
   */
  virtual ApiListenerOptRef apiListener() PURE;
};

} // namespace Server
} // namespace Envoy
