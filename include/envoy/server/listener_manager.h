#pragma once

#include "envoy/api/v2/listener/listener.pb.h"
#include "envoy/network/filter.h"
#include "envoy/network/listen_socket.h"
#include "envoy/network/listener.h"
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
  virtual ~LdsApi() {}

  /**
   * @return std::string the last received version by the xDS API for LDS.
   */
  virtual std::string versionInfo() const PURE;
};

typedef std::unique_ptr<LdsApi> LdsApiPtr;

/**
 * Factory for creating listener components.
 */
class ListenerComponentFactory {
public:
  virtual ~ListenerComponentFactory() {}

  /**
   * @return an LDS API provider.
   * @param lds_config supplies the management server configuration.
   */
  virtual LdsApiPtr createLdsApi(const envoy::api::v2::core::ConfigSource& lds_config) PURE;

  /**
   * Creates a socket.
   * @param address supplies the socket's address.
   * @param options to be set on the created socket just before calling 'bind()'.
   * @param bind_to_port supplies whether to actually bind the socket.
   * @return Network::SocketSharedPtr an initialized and potentially bound socket.
   */
  virtual Network::SocketSharedPtr
  createListenSocket(Network::Address::InstanceConstSharedPtr address,
                     const Network::Socket::OptionsSharedPtr& options, bool bind_to_port) PURE;

  /**
   * Creates a list of filter factories.
   * @param filters supplies the proto configuration.
   * @param context supplies the factory creation context.
   * @return std::vector<Network::FilterFactoryCb> the list of filter factories.
   */
  virtual std::vector<Network::FilterFactoryCb> createNetworkFilterFactoryList(
      const Protobuf::RepeatedPtrField<envoy::api::v2::listener::Filter>& filters,
      Configuration::FactoryContext& context) PURE;

  /**
   * Creates a list of listener filter factories.
   * @param filters supplies the JSON configuration.
   * @param context supplies the factory creation context.
   * @return std::vector<Network::ListenerFilterFactoryCb> the list of filter factories.
   */
  virtual std::vector<Network::ListenerFilterFactoryCb> createListenerFilterFactoryList(
      const Protobuf::RepeatedPtrField<envoy::api::v2::listener::ListenerFilter>& filters,
      Configuration::ListenerFactoryContext& context) PURE;

  /**
   * @return DrainManagerPtr a new drain manager.
   * @param drain_type supplies the type of draining to do for the owning listener.
   */
  virtual DrainManagerPtr createDrainManager(envoy::api::v2::Listener::DrainType drain_type) PURE;

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
  virtual ~ListenerManager() {}

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
  virtual bool addOrUpdateListener(const envoy::api::v2::Listener& config,
                                   const std::string& version_info, bool modifiable) PURE;

  /**
   * Instruct the listener manager to create an LDS API provider. This is a separate operation
   * during server initialization because the listener manager is created prior to several core
   * pieces of the server existing.
   * @param lds_config supplies the management server configuration.
   */
  virtual void createLdsApi(const envoy::api::v2::core::ConfigSource& lds_config) PURE;

  /**
   * @return std::vector<std::reference_wrapper<Network::ListenerConfig>> a list of the currently
   * loaded listeners. Note that this routine returns references to the existing listeners. The
   * references are only valid in the context of the current call stack and should not be stored.
   */
  virtual std::vector<std::reference_wrapper<Network::ListenerConfig>> listeners() PURE;

  /**
   * @return uint64_t the total number of connections owned by all listeners across all workers.
   */
  virtual uint64_t numConnections() PURE;

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
   * is used for server draining.
   */
  virtual void stopListeners() PURE;

  /**
   * Stop all threaded workers from running. When this routine returns all worker threads will
   * have exited.
   */
  virtual void stopWorkers() PURE;
};

} // namespace Server
} // namespace Envoy
