#pragma once

#include <vector>

#include "envoy/admin/v3/config_dump.pb.h"
#include "envoy/config/core/v3/config_source.pb.h"
#include "envoy/config/listener/v3/listener.pb.h"
#include "envoy/config/listener/v3/listener_components.pb.h"
#include "envoy/filter/config_provider_manager.h"
#include "envoy/network/filter.h"
#include "envoy/network/listen_socket.h"
#include "envoy/network/listener.h"
#include "envoy/network/socket_interface.h"
#include "envoy/server/api_listener.h"
#include "envoy/server/drain_manager.h"
#include "envoy/server/filter_config.h"
#include "envoy/server/guarddog.h"

#include "source/common/protobuf/protobuf.h"

namespace Envoy {
namespace Filter {
class TcpListenerFilterConfigProviderManagerImpl;
} // namespace Filter

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

/**
 * Factory for creating listener components.
 */
class ListenerComponentFactory {
public:
  virtual ~ListenerComponentFactory() = default;

  /**
   * @return an LDS API provider.
   * @param lds_config supplies the management server configuration.
   * @param lds_resources_locator xds::core::v3::ResourceLocator for listener collection.
   */
  virtual LdsApiPtr createLdsApi(const envoy::config::core::v3::ConfigSource& lds_config,
                                 const xds::core::v3::ResourceLocator* lds_resources_locator) PURE;

  enum class BindType {
    // The listener will not bind.
    NoBind,
    // The listener will bind a socket shared by all workers.
    NoReusePort,
    // The listener will use reuse_port sockets independently on each worker.
    ReusePort
  };

  /**
   * Creates a socket.
   * @param address supplies the socket's address.
   * @param socket_type the type of socket (stream or datagram) to create.
   * @param options to be set on the created socket just before calling 'bind()'.
   * @param bind_type supplies the bind type of the listen socket.
   * @param creation_options additional options for how to create the socket.
   * @param worker_index supplies the socket/worker index of the new socket.
   * @return Network::SocketSharedPtr an initialized and potentially bound socket.
   */
  virtual Network::SocketSharedPtr createListenSocket(
      Network::Address::InstanceConstSharedPtr address, Network::Socket::Type socket_type,
      const Network::Socket::OptionsSharedPtr& options, BindType bind_type,
      const Network::SocketCreationOptions& creation_options, uint32_t worker_index) PURE;

  /**
   * Creates a list of filter factories.
   * @param filters supplies the proto configuration.
   * @param context supplies the factory creation context.
   * @return Filter::NetworkFilterFactoriesList the list of filter factories.
   */
  virtual Filter::NetworkFilterFactoriesList createNetworkFilterFactoryList(
      const Protobuf::RepeatedPtrField<envoy::config::listener::v3::Filter>& filters,
      Server::Configuration::FilterChainFactoryContext& filter_chain_factory_context) PURE;

  /**
   * Creates a list of listener filter factories.
   * @param filters supplies the JSON configuration.
   * @param context supplies the factory creation context.
   * @return Filter::ListenerFilterFactoriesList the list of filter factories.
   */
  virtual Filter::ListenerFilterFactoriesList createListenerFilterFactoryList(
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
   * Creates a list of QUIC listener filter factories.
   * @param filters supplies the JSON configuration.
   * @param context supplies the factory creation context.
   * @return Filter::ListenerFilterFactoriesList the list of filter factories.
   */
  virtual Filter::QuicListenerFilterFactoriesList createQuicListenerFilterFactoryList(
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

  /**
   * @return Filter::TcpListenerFilterConfigProviderManagerImpl* the pointer of the TCP listener
   * config provider manager.
   */
  virtual Filter::TcpListenerFilterConfigProviderManagerImpl*
  getTcpListenerConfigProviderManager() PURE;
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

  // The types of listeners to be returned from listeners(ListenerState).
  // An enum instead of enum class so the underlying type is an int and bitwise operations can be
  // used without casting.
  enum ListenerState : uint8_t {
    ACTIVE = 1 << 0,
    WARMING = 1 << 1,
    DRAINING = 1 << 2,
    ALL = ACTIVE | WARMING | DRAINING
  };

  virtual ~ListenerManager() = default;

  /**
   * Add or update a listener. Listeners are referenced by a unique name. If no name is provided,
   * the manager will allocate a UUID. Listeners that expect to be dynamically updated should
   * provide a unique name. The manager will search by name to find the existing listener that
   * should be updated. The old listener will be gracefully drained once the new listener is ready
   * to take traffic (e.g. when RDS has been initialized).
   * @param config supplies the configuration proto.
   * @param version_info supplies the xDS version of the listener.
   * @param modifiable supplies whether the added listener can be updated or removed. If the
   *        listener is not modifiable, future calls to this function or removeListener() on behalf
   *        of this listener will return false.
   * @return TRUE if a listener was added or FALSE if the listener was not updated because it is
   *         a duplicate of the existing listener. This routine will return
   *         absl::InvalidArgumentError if there is a fundamental error preventing the listener
   *         from being added or updated.
   */
  virtual absl::StatusOr<bool>
  addOrUpdateListener(const envoy::config::listener::v3::Listener& config,
                      const std::string& version_info, bool modifiable) PURE;

  /**
   * Instruct the listener manager to create an LDS API provider. This is a separate operation
   * during server initialization because the listener manager is created prior to several core
   * pieces of the server existing.
   * @param lds_config supplies the management server configuration.
   * @param lds_resources_locator xds::core::v3::ResourceLocator for listener collection.
   */
  virtual void createLdsApi(const envoy::config::core::v3::ConfigSource& lds_config,
                            const xds::core::v3::ResourceLocator* lds_resources_locator) PURE;

  /**
   * @param state the type of listener to be returned (defaults to ACTIVE), states can be OR'd
   * together to return multiple different types
   * @return std::vector<std::reference_wrapper<Network::ListenerConfig>> a list of currently known
   * listeners in the requested state. Note that this routine returns references to the existing
   * listeners. The references are only valid in the context of the current call stack and should
   * not be stored.
   */
  virtual std::vector<std::reference_wrapper<Network::ListenerConfig>>
  listeners(ListenerState state = ListenerState::ACTIVE) PURE;

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
   * @param guard_dog supplies the optional guard dog to use for thread watching.
   * @param callback supplies the callback to complete server initialization.
   */
  virtual void startWorkers(OptRef<GuardDog> guard_dog, std::function<void()> callback) PURE;

  /**
   * Stop all listeners from accepting new connections without actually removing any of them. This
   * is used for server draining and /drain_listeners admin endpoint. This method directly stops the
   * listeners on workers. Once a listener is stopped, any listener modifications are not allowed.
   * @param stop_listeners_type indicates listeners to stop.
   * @param options additional options passed through to shutdownListener.
   */
  virtual void stopListeners(StopListenersType stop_listeners_type,
                             const Network::ExtraShutdownListenerOptions& options) PURE;

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

  /*
   * @return TRUE if the worker has started or FALSE if not.
   */
  virtual bool isWorkerStarted() PURE;
};

// overload operator| to allow ListenerManager::listeners(ListenerState) to be called using a
// combination of flags, such as listeners(ListenerState::WARMING|ListenerState::ACTIVE)
constexpr ListenerManager::ListenerState operator|(const ListenerManager::ListenerState lhs,
                                                   const ListenerManager::ListenerState rhs) {
  return static_cast<ListenerManager::ListenerState>(static_cast<uint8_t>(lhs) |
                                                     static_cast<uint8_t>(rhs));
}

} // namespace Server
} // namespace Envoy
