#pragma once

#include "envoy/json/json_object.h"
#include "envoy/network/filter.h"
#include "envoy/network/listen_socket.h"
#include "envoy/server/filter_config.h"
#include "envoy/ssl/context.h"

namespace Envoy {
namespace Server {

/**
 * Factory for creating listener components.
 */
class ListenerComponentFactory {
public:
  virtual ~ListenerComponentFactory() {}

  /**
   * Creates a socket.
   * @param address supplies the socket's address.
   * @param bind_to_port supplies whether to actually bind the socket.
   * @return Network::ListenSocketPtr an initialized and potentially bound socket.
   */
  virtual Network::ListenSocketPtr
  createListenSocket(Network::Address::InstanceConstSharedPtr address, bool bind_to_port) PURE;

  /**
   * Creates a list of filter factories.
   * @param filters supplies the JSON configuration.
   * @param context supplies the factory creation context.
   * @return std::list<Configuration::NetworkFilterFactoryCb> the list of filter factories.
   */
  virtual std::list<Configuration::NetworkFilterFactoryCb>
  createFilterFactoryList(const std::vector<Json::ObjectSharedPtr>& filters,
                          Configuration::FactoryContext& context) PURE;
};

/**
 * A configuration for an individual listener.
 */
class Listener {
public:
  virtual ~Listener() {}

  /**
   * @return Network::FilterChainFactory& the factory for setting up the filter chain on a new
   *         connection.
   */
  virtual Network::FilterChainFactory& filterChainFactory() PURE;

  /**
   * @return Network::Address::InstanceConstSharedPtr the configured address for the listener. This
   *         may be distinct to the bound address, e.g. if the port is zero.
   */
  virtual Network::Address::InstanceConstSharedPtr address() PURE;

  /**
   * @return Network::ListenSocket& the actual listen socket. The address of this socket may be
   *         different from address() if for example the configured address binds to port zero.
   */
  virtual Network::ListenSocket& socket() PURE;

  /**
   * @return Ssl::ServerContext* the SSL context
   */
  virtual Ssl::ServerContext* sslContext() PURE;

  /**
   * @return bool whether to use the PROXY Protocol V1
   * (http://www.haproxy.org/download/1.5/doc/proxy-protocol.txt)
   */
  virtual bool useProxyProto() PURE;

  /**
   * @return bool specifies whether the listener should actually listen on the port.
   *         A listener that doesn't listen on a port can only receive connections
   *         redirected from other listeners.
   */
  virtual bool bindToPort() PURE;

  /**
   * @return bool if a connection was redirected to this listener address using iptables,
   *         allow the listener to hand it off to the listener associated to the original address
   */
  virtual bool useOriginalDst() PURE;

  /**
   * @return uint32_t providing a soft limit on size of the listener's new connection read and write
   *         buffers.
   */
  virtual uint32_t perConnectionBufferLimitBytes() PURE;

  /**
   * @return Stats::Scope& the stats scope to use for all listener specific stats.
   */
  virtual Stats::Scope& listenerScope() PURE;
};

typedef std::unique_ptr<Listener> ListenerPtr;

/**
 * A manager for all listeners.
 */
class ListenerManager {
public:
  virtual ~ListenerManager() {}

  /**
   * Add a listener to the manager.
   * @param json supplies the configuration JSON. Will throw an EnvoyException if the listener can
   *        not be added.
   */
  virtual void addListener(const Json::Object& json) PURE;

  /**
   * @return std::list<std::reference_wrapper<Listener>> a list of the currently loaded listeners.
   */
  virtual std::list<std::reference_wrapper<Listener>> listeners() PURE;
};

} // Server
} // Envoy
