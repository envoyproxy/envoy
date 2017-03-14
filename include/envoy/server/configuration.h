#pragma once

#include "envoy/common/optional.h"
#include "envoy/ratelimit/ratelimit.h"
#include "envoy/ssl/context.h"
#include "envoy/tracing/http_tracer.h"
#include "envoy/upstream/cluster_manager.h"

namespace Server {
namespace Configuration {

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
   * @return std::string the address.
   */
  virtual std::string address() PURE;

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
};

typedef std::unique_ptr<Listener> ListenerPtr;

/**
 * Configuration for local disk runtime support.
 */
class Runtime {
public:
  virtual ~Runtime() {}

  /**
   * @return const std::string& the root symlink to watch for swapping.
   */
  virtual const std::string& symlinkRoot() PURE;

  /**
   * @return const std::string& the subdirectory to load with runtime data.
   */
  virtual const std::string& subdirectory() PURE;

  /**
   * @return const std::string& the override subdirectory.
   * Read runtime values from subdirectory and overrideSubdirectory, overrideSubdirectory wins.
   */
  virtual const std::string& overrideSubdirectory() PURE;
};

/**
 * The main server configuration.
 */
class Main {
public:
  virtual ~Main() {}

  /**
   * @return Upstream::ClusterManager& singleton for use by the entire server.
   */
  virtual Upstream::ClusterManager& clusterManager() PURE;

  /**
   * @return Tracing::HttpTracer& singleton for use by the entire server.
   */
  virtual Tracing::HttpTracer& httpTracer() PURE;

  /**
   * @return const std::vector<ListenerPtr>& all listeners.
   */
  virtual const std::list<ListenerPtr>& listeners() PURE;

  /**
   * @return RateLimit::ClientFactory& the global rate limit service client factory.
   */
  virtual RateLimit::ClientFactory& rateLimitClientFactory() PURE;

  /**
   * @return Optional<std::string> the optional local/remote TCP statsd cluster to write to.
   *         This cluster must be defined via the cluster manager configuration.
   */
  virtual Optional<std::string> statsdTcpClusterName() PURE;

  /**
   * @return Optional<uint32_t> the optional local UDP statsd port to write to.
   */
  virtual Optional<uint32_t> statsdUdpPort() PURE;

  /**
   * @return the time interval between flushing to configured stat sinks. The server latches
   *         counters.
   */
  virtual std::chrono::milliseconds statsFlushInterval() PURE;
};

/**
 * Admin configuration.
 */
class Admin {
public:
  virtual ~Admin() {}

  /**
   * @return const std::string& the admin access log path.
   */
  virtual const std::string& accessLogPath() PURE;

  /**
   * @return uint32_t the server admin HTTP port.
   */
  virtual uint32_t port() PURE;
};

/**
 * Initial configuration values that are needed before the main configuration load.
 */
class Initial {
public:
  virtual ~Initial() {}

  /**
   * @return Admin& the admin config.
   */
  virtual Admin& admin() PURE;

  /**
   * @return Optional<std::string> the path to look for flag files.
   */
  virtual Optional<std::string> flagsPath() PURE;

  /**
   * @return Runtime* the local disk runtime configuration or nullptr if there is no configuration.
   */
  virtual Runtime* runtime() PURE;
};

} // Configuration
} // Server
